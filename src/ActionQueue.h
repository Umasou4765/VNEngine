#pragma once
#include "ActionCommand.h"

#include <memory>
#include <vector>
#include <deque>
#include <algorithm>

// ActionQueue: frame-driven scheduler for time-based actions only.
//
// Lua performs immediate gameplay logic directly (load background, set flags,
// play one-shot audio, etc.). This queue exists only for effects that need
// progression across frames and may require a Lua yield.
//
// Design:
// - m_waitQueue: sequential, blocking time-based actions (wait, scripted shake)
// - m_taskQueue: concurrent time-based actions that can run in parallel
//
// The front of m_waitQueue is the current blocker. When it completes, Lua can
// resume and/or the next waiting action can begin.
//
// Thread-safety: not thread-safe by design (all calls from main thread).
class ActionQueue {
public:
    ActionQueue() = default;
    ~ActionQueue() = default;

     // Add a command to the queue.
     // Wait commands go to m_waitQueue (sequential blocker).
     // Non-wait commands go to m_taskQueue (parallel over-time tasks).
    void Enqueue(std::unique_ptr<ActionCommand> cmd) {
        if (!cmd) return;

        cmd->SetState(ActionCommand::State::PENDING);
        cmd->OnInitialize();

        if (cmd->IsWait()) {
            m_waitQueue.push_back(std::move(cmd));
        } else {
            m_taskQueue.push_back(std::move(cmd));
        }
    }

    // Update all active commands once per frame.
    void Update(float dt, bool fastForwardWaits = false) {
        // Update non-wait (concurrent) tasks
        std::vector<std::unique_ptr<ActionCommand>> finalizedTasks;
        finalizedTasks.reserve(m_taskQueue.size());

        for (auto& cmd : m_taskQueue) {
            if (!cmd) {
                continue;
            }

            if (cmd->GetState() == ActionCommand::State::COMPLETED ||
                cmd->GetState() == ActionCommand::State::CANCELLED) {
                finalizedTasks.push_back(std::move(cmd));
                continue;
            }

            if (cmd->GetState() == ActionCommand::State::PENDING) {
                cmd->SetState(ActionCommand::State::RUNNING);
            }

            if (cmd->GetState() == ActionCommand::State::RUNNING) {
                const bool stillRunning = cmd->OnUpdate(dt);
                if (!stillRunning) {
                    cmd->SetState(ActionCommand::State::COMPLETED);
                    finalizedTasks.push_back(std::move(cmd));
                }
            }
        }

        m_taskQueue.erase(
            std::remove_if(m_taskQueue.begin(), m_taskQueue.end(),
                           [](const auto& cmd) {
                               return !cmd;
                           }),
            m_taskQueue.end()
        );

        // Finalizers may enqueue new actions; run them after iteration to avoid invalidating traversal.
        for (auto& cmd : finalizedTasks) {
            if (cmd) {
                cmd->OnFinalize();
            }
        }

        // Update the currently active wait command (front of queue)
        if (!m_waitQueue.empty()) {
            if (fastForwardWaits) {
                auto pendingWaits = std::move(m_waitQueue);
                m_waitQueue.clear();

                for (auto& waitCmd : pendingWaits) {
                    if (!waitCmd) {
                        continue;
                    }

                    if (waitCmd->GetState() == ActionCommand::State::CANCELLED) {
                        continue;
                    }
                    if (waitCmd->GetState() == ActionCommand::State::PENDING) {
                        waitCmd->SetState(ActionCommand::State::RUNNING);
                    }
                    waitCmd->SetState(ActionCommand::State::COMPLETED);
                    waitCmd->OnFinalize();
                }
                return;
            }

            auto& currentWait = m_waitQueue.front();

            if (currentWait->GetState() == ActionCommand::State::CANCELLED) {
                auto cancelledWait = std::move(m_waitQueue.front());
                m_waitQueue.pop_front();
                if (cancelledWait) {
                    cancelledWait->OnFinalize();
                }
                return;
            }

            if (currentWait->GetState() == ActionCommand::State::PENDING) {
                currentWait->SetState(ActionCommand::State::RUNNING);
            }

            if (currentWait->GetState() == ActionCommand::State::RUNNING) {
                const bool stillRunning = currentWait->OnUpdate(dt);
                if (!stillRunning) {
                    auto finishedWait = std::move(m_waitQueue.front());
                    m_waitQueue.pop_front();

                    if (finishedWait) {
                        finishedWait->SetState(ActionCommand::State::COMPLETED);
                        finishedWait->OnFinalize();
                    }
                }
            }
        }
    }

    // Render visual elements that need immediate feedback.
    void Render(SDL_Renderer* renderer) {
        for (auto& cmd : m_taskQueue) {
            if (cmd->GetState() == ActionCommand::State::RUNNING) {
                cmd->OnRender(renderer);
            }
        }

        if (!m_waitQueue.empty() && m_waitQueue.front()->GetState() == ActionCommand::State::RUNNING) {
            m_waitQueue.front()->OnRender(renderer);
        }
    }

    // Query whether a blocking time-based action is active.
    // Used by SceneManager/Lua coroutine flow to decide when resume is allowed.
    [[nodiscard]] bool IsBlocked() const noexcept {
        return !m_waitQueue.empty();
    }

    [[nodiscard]] ActionCommand* GetCurrentBlockingCommand() const noexcept {
        return m_waitQueue.empty() ? nullptr : m_waitQueue.front().get();
    }

    // Get the number of active concurrent tasks.
    [[nodiscard]] std::size_t GetActiveTaskCount() const noexcept {
        return m_taskQueue.size();
    }

    // Get the number of queued wait commands.
    [[nodiscard]] std::size_t GetWaitQueueSize() const noexcept {
        return m_waitQueue.size();
    }

    // Clear all commands (useful for scene transitions and chapter changes).
    void FlushAll() {
        std::vector<std::unique_ptr<ActionCommand>> finalizers;
        finalizers.reserve(m_taskQueue.size() + m_waitQueue.size());

        for (auto& cmd : m_taskQueue) {
            if (cmd) {
                finalizers.push_back(std::move(cmd));
            }
        }
        for (auto& cmd : m_waitQueue) {
            if (cmd) {
                finalizers.push_back(std::move(cmd));
            }
        }

        m_taskQueue.clear();
        m_waitQueue.clear();

        for (auto& cmd : finalizers) {
            if (cmd) {
                cmd->OnFinalize();
            }
        }
    }

    // Cancel all concurrent tasks (keeps the wait queue intact).
    void CancelTasks() {
        std::vector<std::unique_ptr<ActionCommand>> finalizers;
        finalizers.reserve(m_taskQueue.size());

        for (auto& cmd : m_taskQueue) {
            if (!cmd) {
                continue;
            }
            cmd->Cancel();
            finalizers.push_back(std::move(cmd));
        }

        m_taskQueue.clear();

        for (auto& cmd : finalizers) {
            if (cmd) {
                cmd->OnFinalize();
            }
        }
    }

private:
    // m_waitQueue: sequential commands that block Lua resume.
    // Only the front command is active at a time.
    // Deque is used for efficient pop_front().
    std::deque<std::unique_ptr<ActionCommand>> m_waitQueue;

    // m_taskQueue: concurrent time-based commands that execute independently.
    // All commands update each frame.
    // Vector is used for cache locality of active tasks.
    std::vector<std::unique_ptr<ActionCommand>> m_taskQueue;
};
