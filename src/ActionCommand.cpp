#include "ActionCommand.h"
#include "SceneManager.h"

#include <cmath>

// ============================================================================
// ScreenShakeAction
// ============================================================================

bool ScreenShakeAction::OnUpdate(float dt) {
    if (!m_scene) return false;

    m_elapsed += dt;
    
    if (m_elapsed >= m_duration) {
        m_scene->StartScreenShake(0.0f, 0.0f);
        return false;  // Completed
    }

    // Linear intensity falloff as time increases
    const float intensity = m_intensity * (1.0f - m_elapsed / m_duration);
    m_scene->StartScreenShake(intensity, 0.0f);  // Continuous shaking
    
    return true;  // Still running
}

// ============================================================================
// ExplicitWaitAction
// ============================================================================

bool ExplicitWaitAction::OnUpdate(float dt) {
    m_elapsed += dt;
    
    if (m_elapsed >= m_duration) {
        return false;  // Completed
    }
    
    return true;  // Still waiting
}

// ============================================================================
// CameraTweenAction
// ============================================================================

void CameraTweenAction::OnInitialize() {
    if (!m_scene) return;
    m_scene->TransitionCamera(m_targetX, m_targetY, m_targetZoom, m_duration);
}

bool CameraTweenAction::OnUpdate(float dt) {
    m_elapsed += dt;
    if (m_elapsed >= m_duration) {
        return false;
    }
    return true;
}

void CameraTweenAction::OnFinalize() {
    if (!m_scene) return;
    m_scene->SetCameraImmediate(m_targetX, m_targetY, m_targetZoom);
}

// ============================================================================
// LetterboxTweenAction
// ============================================================================

void LetterboxTweenAction::OnInitialize() {
    if (!m_scene) return;
    m_scene->SetLetterbox(m_ratio, m_duration, m_duration <= 0.0f);
}

bool LetterboxTweenAction::OnUpdate(float dt) {
    m_elapsed += dt;
    if (m_elapsed >= m_duration) {
        return false;
    }
    return true;
}

void LetterboxTweenAction::OnFinalize() {
    if (!m_scene) return;
    m_scene->SetLetterbox(m_ratio, 0.0f, true);
}

// ============================================================================
// ActionGroupCommand
// ============================================================================

void ActionGroupCommand::OnInitialize() {
    for (auto& child : m_children) {
        if (!child) continue;
        child->SetState(ActionCommand::State::PENDING);
        child->OnInitialize();
    }
}

bool ActionGroupCommand::OnUpdate(float dt) {
    bool anyRunning = false;

    for (auto& child : m_children) {
        if (!child) continue;
        if (child->GetState() == ActionCommand::State::COMPLETED ||
            child->GetState() == ActionCommand::State::CANCELLED) {
            continue;
        }

        if (child->GetState() == ActionCommand::State::PENDING) {
            child->SetState(ActionCommand::State::RUNNING);
        }

        if (child->GetState() == ActionCommand::State::RUNNING) {
            const bool stillRunning = child->OnUpdate(dt);
            if (!stillRunning) {
                child->SetState(ActionCommand::State::COMPLETED);
                child->OnFinalize();
            } else {
                anyRunning = true;
            }
        }
    }

    return anyRunning;
}

void ActionGroupCommand::OnFinalize() {
    for (auto& child : m_children) {
        if (!child) continue;
        if (child->GetState() == ActionCommand::State::COMPLETED) continue;
        child->Cancel();
        child->OnFinalize();
    }
}

// ============================================================================
// SpriteTweenAction
// ============================================================================

void SpriteTweenAction::OnInitialize() {
    if (!m_scene) return;

    const SceneManager::SpriteTransformState initial = m_scene->GetSpriteTransformState(m_slot);
    m_useAbsolute = initial.useAbsolute;
    m_anchorX = initial.anchorX;
    m_anchorY = initial.anchorY;
    m_width = initial.width;
    m_height = initial.height;

    m_currentX = initial.x;
    m_currentY = initial.y;
    m_currentScaleX = initial.scaleX;
    m_currentScaleY = initial.scaleY;
    m_currentRotation = initial.rotationDeg;

    m_endX = m_targetX.value_or(m_currentX);
    m_endY = m_targetY.value_or(m_currentY);
    m_endScaleX = m_targetScaleX.value_or(m_currentScaleX);
    m_endScaleY = m_targetScaleY.value_or(m_currentScaleY);
    m_endRotation = m_targetRotation.value_or(m_currentRotation);

    const bool hasChanges = std::fabs(m_currentX - m_endX) > 0.0001f ||
                            std::fabs(m_currentY - m_endY) > 0.0001f ||
                            std::fabs(m_currentScaleX - m_endScaleX) > 0.0001f ||
                            std::fabs(m_currentScaleY - m_endScaleY) > 0.0001f ||
                            std::fabs(m_currentRotation - m_endRotation) > 0.0001f;

    if (!hasChanges || m_duration <= 0.0f) {
        m_currentX = m_endX;
        m_currentY = m_endY;
        m_currentScaleX = m_endScaleX;
        m_currentScaleY = m_endScaleY;
        m_currentRotation = m_endRotation;
        ApplyCurrentTransform();
        m_running = false;
        return;
    }

    m_xTween.Start(&m_currentX, m_currentX, m_endX, m_duration, Ease::InOutQuad);
    m_yTween.Start(&m_currentY, m_currentY, m_endY, m_duration, Ease::InOutQuad);
    m_scaleXTween.Start(&m_currentScaleX, m_currentScaleX, m_endScaleX, m_duration, Ease::InOutQuad);
    m_scaleYTween.Start(&m_currentScaleY, m_currentScaleY, m_endScaleY, m_duration, Ease::InOutQuad);
    m_rotationTween.Start(&m_currentRotation, m_currentRotation, m_endRotation, m_duration, Ease::InOutQuad);
    m_running = true;
}

bool SpriteTweenAction::OnUpdate(float dt) {
    if (!m_scene || !m_running) {
        return false;
    }

    m_elapsed += dt;
    m_xTween.Update(dt);
    m_yTween.Update(dt);
    m_scaleXTween.Update(dt);
    m_scaleYTween.Update(dt);
    m_rotationTween.Update(dt);
    ApplyCurrentTransform();

    return m_elapsed < m_duration;
}

void SpriteTweenAction::OnFinalize() {
    if (!m_scene) return;

    m_currentX = m_endX;
    m_currentY = m_endY;
    m_currentScaleX = m_endScaleX;
    m_currentScaleY = m_endScaleY;
    m_currentRotation = m_endRotation;
    ApplyCurrentTransform();
}

void SpriteTweenAction::ApplyCurrentTransform() const {
    if (!m_scene) return;
    m_scene->SetSpriteTransform(m_slot,
                                m_currentX,
                                m_currentY,
                                m_currentScaleX,
                                m_currentScaleY,
                                m_currentRotation,
                                m_anchorX,
                                m_anchorY,
                                m_useAbsolute,
                                m_width,
                                m_height);
}

// ============================================================================
// ScreenFadeAction
// ============================================================================

void ScreenFadeAction::OnInitialize() {
    if (!m_scene) return;

    m_scene->SetScreenFadeColor(m_r, m_g, m_b);
    m_scene->TransitionScreenFade(m_targetAlpha, m_duration);
}

bool ScreenFadeAction::OnUpdate(float dt) {
    m_elapsed += dt;
    return m_elapsed < m_duration;
}

void ScreenFadeAction::OnFinalize() {
    if (!m_scene) return;
    m_scene->SetScreenFadeAlphaImmediate(m_targetAlpha);
}

// ============================================================================
// SpriteTintAction
// ============================================================================

void SpriteTintAction::OnInitialize() {
    if (!m_scene) return;

    Uint8 currentR = 255;
    Uint8 currentG = 255;
    Uint8 currentB = 255;
    float currentAlpha = 1.0f;
    m_scene->GetSpriteTint(m_slot, currentR, currentG, currentB, currentAlpha);

    m_currentR = static_cast<float>(currentR);
    m_currentG = static_cast<float>(currentG);
    m_currentB = static_cast<float>(currentB);
    m_currentAlpha = currentAlpha;

    m_endR = std::clamp(m_targetR.value_or(m_currentR), 0.0f, 255.0f);
    m_endG = std::clamp(m_targetG.value_or(m_currentG), 0.0f, 255.0f);
    m_endB = std::clamp(m_targetB.value_or(m_currentB), 0.0f, 255.0f);
    m_endAlpha = std::clamp(m_targetAlpha.value_or(m_currentAlpha), 0.0f, 1.0f);

    const bool hasChanges = std::fabs(m_currentR - m_endR) > 0.0001f ||
                            std::fabs(m_currentG - m_endG) > 0.0001f ||
                            std::fabs(m_currentB - m_endB) > 0.0001f ||
                            std::fabs(m_currentAlpha - m_endAlpha) > 0.0001f;

    if (!hasChanges || m_duration <= 0.0f) {
        m_currentR = m_endR;
        m_currentG = m_endG;
        m_currentB = m_endB;
        m_currentAlpha = m_endAlpha;
        ApplyCurrentTint();
        m_running = false;
        return;
    }

    m_rTween.Start(&m_currentR, m_currentR, m_endR, m_duration, Ease::InOutQuad);
    m_gTween.Start(&m_currentG, m_currentG, m_endG, m_duration, Ease::InOutQuad);
    m_bTween.Start(&m_currentB, m_currentB, m_endB, m_duration, Ease::InOutQuad);
    m_alphaTween.Start(&m_currentAlpha, m_currentAlpha, m_endAlpha, m_duration, Ease::InOutQuad);
    m_running = true;
}

bool SpriteTintAction::OnUpdate(float dt) {
    if (!m_scene || !m_running) {
        return false;
    }

    m_elapsed += dt;
    m_rTween.Update(dt);
    m_gTween.Update(dt);
    m_bTween.Update(dt);
    m_alphaTween.Update(dt);
    ApplyCurrentTint();

    return m_elapsed < m_duration;
}

void SpriteTintAction::OnFinalize() {
    if (!m_scene) return;

    m_currentR = m_endR;
    m_currentG = m_endG;
    m_currentB = m_endB;
    m_currentAlpha = m_endAlpha;
    ApplyCurrentTint();
}

void SpriteTintAction::ApplyCurrentTint() const {
    if (!m_scene) return;

    m_scene->SetSpriteTint(m_slot,
                           static_cast<Uint8>(std::clamp(m_currentR, 0.0f, 255.0f)),
                           static_cast<Uint8>(std::clamp(m_currentG, 0.0f, 255.0f)),
                           static_cast<Uint8>(std::clamp(m_currentB, 0.0f, 255.0f)),
                           std::clamp(m_currentAlpha, 0.0f, 1.0f));
}
