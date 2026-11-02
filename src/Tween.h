#pragma once
#include <functional>
#include <vector>
#include <cmath>

// ─────────────────────────────────────────────
//  Easing Functions
// ─────────────────────────────────────────────
namespace Ease {
    inline float Linear(float t)     { return t; }
    inline float InQuad(float t)     { return t * t; }
    inline float OutQuad(float t)    { return t * (2.0f - t); }
    inline float InOutQuad(float t)  { return t < 0.5f ? 2*t*t : -1+(4-2*t)*t; }
    inline float OutBack(float t)    {
        const float c = 1.70158f;
        return 1 + (c+1)*(t-1)*(t-1)*(t-1) + c*(t-1)*(t-1);
    }
}

// ─────────────────────────────────────────────
//  Tween<T>
//  Usage:
//    Tween<float> t(&alpha, 0.f, 1.f, 0.3f, Ease::OutQuad);
//    t.Update(deltaTime);
// ─────────────────────────────────────────────
template<typename T>
class Tween {
public:
    Tween() = default;

    void Start(T* target, T from, T to, float duration,
               std::function<float(float)> easing = Ease::OutQuad,
               std::function<void()> onDone = nullptr)
    {
        if (!target) {
            m_target = nullptr;
            m_running = false;
            return;
        }

        m_target   = target;
        m_from     = from;
        m_to       = to;
        m_duration = std::max(duration, 0.0f);
        m_elapsed  = 0.f;
        m_easing   = easing;
        m_onDone   = onDone;
        m_running  = (m_duration > 0.0f);
        *m_target  = from;

        if (!m_running) {
            *m_target = m_to;
            if (m_onDone) m_onDone();
        }
    }

    void Update(float dt) {
        if (!m_running || !m_target) return;
        m_elapsed += dt;
        float t = std::min(m_elapsed / m_duration, 1.0f);
        float e = m_easing(t);
        *m_target = m_from + (T)((m_to - m_from) * e);

        if (m_elapsed >= m_duration) {
            *m_target = m_to;
            m_running = false;
            if (m_onDone) m_onDone();
        }
    }

    bool IsRunning() const { return m_running; }
    void Stop()            { m_running = false; }

private:
    // Non-owning pointer: target must outlive this Tween instance.
    // If Tween is ever centralized in a manager, prefer a setter callback model.
    T*     m_target   = nullptr;
    T      m_from     = {};
    T      m_to       = {};
    float  m_duration = 1.0f;
    float  m_elapsed  = 0.f;
    bool   m_running  = false;
    std::function<float(float)> m_easing  = Ease::OutQuad;
    std::function<void()>       m_onDone  = nullptr;
};
