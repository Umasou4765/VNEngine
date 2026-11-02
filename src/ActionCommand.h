#pragma once

#include <SDL3/SDL.h>

#include <algorithm>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "Tween.h"

// Forward declarations
class SceneManager;

// ActionCommand: non-blocking, frame-driven task abstraction.
//
// Each ActionCommand represents a time-based action that can:
// - execute over multiple frames (Update pattern)
// - have dependencies (wait for previous action to complete)
// - support cancellation
//
// Lua handles immediate state changes directly. ActionQueue is reserved for
// effects that evolve over time (waits, tweened motion, shakes, fades).
class ActionCommand {
public:
    enum class Type {
        // Time-based visual commands
        SCREEN_SHAKE,      // Screen shake effect (wait)
        CAMERA_TWEEN,      // Camera move/zoom over time (wait)
        LETTERBOX_TWEEN,   // Letterbox ratio tween (wait)
        SPRITE_TWEEN,      // Tween sprite transform over time (wait)
        SCREEN_FADE,       // Fullscreen color fade (wait)
        SPRITE_TINT,       // Tween sprite tint and opacity (wait)
        ACTION_GROUP,      // Composite action group (wait)

        // Script flow
        SCRIPT_WAIT        // Explicit wait command (wait N seconds)
    };

    enum class State {
        PENDING,     // Not started
        RUNNING,     // Currently executing
        COMPLETED,   // Finished, safe to remove
        CANCELLED
    };

    virtual ~ActionCommand() = default;

    // Core interface
    [[nodiscard]] virtual Type GetType() const noexcept = 0;
    [[nodiscard]] virtual bool IsWait() const noexcept = 0;
    [[nodiscard]] virtual State GetState() const noexcept { return m_state; }
    [[nodiscard]] virtual bool IsCompleted() const noexcept { return m_state == State::COMPLETED; }

    // Initialize command (called once when added to queue).
    // Use this to capture state pointers and allocate resources.
    virtual void OnInitialize() {}

    // Called each frame while the command is running.
    // Returns true if still running, false if completed this frame.
    virtual bool OnUpdate(float dt) = 0;

    // Optional render-phase logic for immediate visual feedback.
    virtual void OnRender(SDL_Renderer* renderer) {}

    // Clean up when removed from queue (cancelled or completed).
    virtual void OnFinalize() {}

    // State management
    void SetState(State state) noexcept { m_state = state; }
    void Cancel() noexcept { m_state = State::CANCELLED; }

protected:
    State m_state = State::PENDING;
};

// Concrete time-based action implementations.

class ScreenShakeAction : public ActionCommand {
public:
    ScreenShakeAction(SceneManager* scene, float intensity, float duration)
        : m_scene(scene), m_intensity(intensity), m_duration(duration), m_elapsed(0.0f) {}

    [[nodiscard]] Type GetType() const noexcept override { return Type::SCREEN_SHAKE; }
    [[nodiscard]] bool IsWait() const noexcept override { return true; }
    bool OnUpdate(float dt) override;

private:
    SceneManager* m_scene;
    float m_intensity;
    float m_duration;
    float m_elapsed;
};

class ExplicitWaitAction : public ActionCommand {
public:
    explicit ExplicitWaitAction(float durationSeconds)
        : m_duration(durationSeconds), m_elapsed(0.0f) {}

    [[nodiscard]] Type GetType() const noexcept override { return Type::SCRIPT_WAIT; }
    [[nodiscard]] bool IsWait() const noexcept override { return true; }
    bool OnUpdate(float dt) override;

private:
    float m_duration;
    float m_elapsed;
};

class CameraTweenAction : public ActionCommand {
public:
    CameraTweenAction(SceneManager* scene,
                      float targetX,
                      float targetY,
                      float targetZoom,
                      float duration)
        : m_scene(scene)
        , m_targetX(targetX)
        , m_targetY(targetY)
        , m_targetZoom(targetZoom)
        , m_duration(duration) {}

    [[nodiscard]] Type GetType() const noexcept override { return Type::CAMERA_TWEEN; }
    [[nodiscard]] bool IsWait() const noexcept override { return true; }
    void OnInitialize() override;
    bool OnUpdate(float dt) override;
    void OnFinalize() override;

private:
    SceneManager* m_scene = nullptr;
    float m_targetX = 0.0f;
    float m_targetY = 0.0f;
    float m_targetZoom = 1.0f;
    float m_duration = 0.0f;
    float m_elapsed = 0.0f;
};

class LetterboxTweenAction : public ActionCommand {
public:
    LetterboxTweenAction(SceneManager* scene, float ratio, float duration)
        : m_scene(scene)
        , m_ratio(ratio)
        , m_duration(duration) {}

    [[nodiscard]] Type GetType() const noexcept override { return Type::LETTERBOX_TWEEN; }
    [[nodiscard]] bool IsWait() const noexcept override { return true; }
    void OnInitialize() override;
    bool OnUpdate(float dt) override;
    void OnFinalize() override;

private:
    SceneManager* m_scene = nullptr;
    float m_ratio = 0.0f;
    float m_duration = 0.0f;
    float m_elapsed = 0.0f;
};

class ActionGroupCommand : public ActionCommand {
public:
    explicit ActionGroupCommand(std::vector<std::unique_ptr<ActionCommand>> children)
        : m_children(std::move(children)) {}

    [[nodiscard]] Type GetType() const noexcept override { return Type::ACTION_GROUP; }
    [[nodiscard]] bool IsWait() const noexcept override { return true; }
    void OnInitialize() override;
    bool OnUpdate(float dt) override;
    void OnFinalize() override;

private:
    std::vector<std::unique_ptr<ActionCommand>> m_children;
};

class SpriteTweenAction : public ActionCommand {
public:
    SpriteTweenAction(SceneManager* scene,
                      int slot,
                      float duration,
                      std::optional<float> x,
                      std::optional<float> y,
                      std::optional<float> scaleX,
                      std::optional<float> scaleY,
                      std::optional<float> rotationDeg)
        : m_scene(scene)
        , m_slot(slot)
        , m_duration(duration)
        , m_targetX(x)
        , m_targetY(y)
        , m_targetScaleX(scaleX)
        , m_targetScaleY(scaleY)
        , m_targetRotation(rotationDeg) {}

    [[nodiscard]] Type GetType() const noexcept override { return Type::SPRITE_TWEEN; }
    [[nodiscard]] bool IsWait() const noexcept override { return true; }
    void OnInitialize() override;
    bool OnUpdate(float dt) override;
    void OnFinalize() override;

private:
    void ApplyCurrentTransform() const;

    SceneManager* m_scene = nullptr;
    int m_slot = 0;
    float m_duration = 0.0f;
    float m_elapsed = 0.0f;
    bool m_running = false;

    std::optional<float> m_targetX;
    std::optional<float> m_targetY;
    std::optional<float> m_targetScaleX;
    std::optional<float> m_targetScaleY;
    std::optional<float> m_targetRotation;

    bool m_useAbsolute = false;
    float m_anchorX = 0.5f;
    float m_anchorY = 1.0f;
    float m_width = -1.0f;
    float m_height = -1.0f;

    float m_currentX = 0.0f;
    float m_currentY = 0.0f;
    float m_currentScaleX = 1.0f;
    float m_currentScaleY = 1.0f;
    float m_currentRotation = 0.0f;

    float m_endX = 0.0f;
    float m_endY = 0.0f;
    float m_endScaleX = 1.0f;
    float m_endScaleY = 1.0f;
    float m_endRotation = 0.0f;

    Tween<float> m_xTween;
    Tween<float> m_yTween;
    Tween<float> m_scaleXTween;
    Tween<float> m_scaleYTween;
    Tween<float> m_rotationTween;
};

class ScreenFadeAction : public ActionCommand {
public:
    ScreenFadeAction(SceneManager* scene,
                     Uint8 r,
                     Uint8 g,
                     Uint8 b,
                     float targetAlpha,
                     float duration)
        : m_scene(scene)
        , m_r(r)
        , m_g(g)
        , m_b(b)
        , m_targetAlpha(targetAlpha)
        , m_duration(duration) {}

    [[nodiscard]] Type GetType() const noexcept override { return Type::SCREEN_FADE; }
    [[nodiscard]] bool IsWait() const noexcept override { return true; }
    void OnInitialize() override;
    bool OnUpdate(float dt) override;
    void OnFinalize() override;

private:
    SceneManager* m_scene = nullptr;
    Uint8 m_r = 0;
    Uint8 m_g = 0;
    Uint8 m_b = 0;
    float m_targetAlpha = 0.0f;
    float m_duration = 0.0f;
    float m_elapsed = 0.0f;
};

class SpriteTintAction : public ActionCommand {
public:
    SpriteTintAction(SceneManager* scene,
                     int slot,
                     float duration,
                     std::optional<float> r,
                     std::optional<float> g,
                     std::optional<float> b,
                     std::optional<float> alpha)
        : m_scene(scene)
        , m_slot(slot)
        , m_duration(duration)
        , m_targetR(r)
        , m_targetG(g)
        , m_targetB(b)
        , m_targetAlpha(alpha) {}

    [[nodiscard]] Type GetType() const noexcept override { return Type::SPRITE_TINT; }
    [[nodiscard]] bool IsWait() const noexcept override { return true; }
    void OnInitialize() override;
    bool OnUpdate(float dt) override;
    void OnFinalize() override;

private:
    void ApplyCurrentTint() const;

    SceneManager* m_scene = nullptr;
    int m_slot = 0;
    float m_duration = 0.0f;
    float m_elapsed = 0.0f;
    bool m_running = false;

    std::optional<float> m_targetR;
    std::optional<float> m_targetG;
    std::optional<float> m_targetB;
    std::optional<float> m_targetAlpha;

    float m_currentR = 255.0f;
    float m_currentG = 255.0f;
    float m_currentB = 255.0f;
    float m_currentAlpha = 1.0f;

    float m_endR = 255.0f;
    float m_endG = 255.0f;
    float m_endB = 255.0f;
    float m_endAlpha = 1.0f;

    Tween<float> m_rTween;
    Tween<float> m_gTween;
    Tween<float> m_bTween;
    Tween<float> m_alphaTween;
};
