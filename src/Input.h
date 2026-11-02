#pragma once

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <vector>

// Input state wrapper for per-frame keyboard, mouse, and gamepad queries.
// JustPressed triggers once on key/button down. IsHeld stays true while held.
class Input {
public:
    enum class Action : std::uint8_t {
        ToggleDebugOverlay,
        ToggleFullscreen,
        CopyWorldCoords,
        OpenSystemMenu,
        OpenSaveMenu,
        OpenLoadMenu,
        QuickSave,
        QuickLoad,
        Advance,
        OpenLog,
        ToggleHideUI,
        SkipModifier,
        DeleteSaveSlot,
        FocusUp,
        FocusDown,
        FocusLeft,
        FocusRight,
        Count
    };

    enum class LastDevice : std::uint8_t {
        KeyboardMouse,
        Gamepad,
    };

    struct KeyChord {
        SDL_Scancode key = SDL_SCANCODE_UNKNOWN;
        bool requireAlt = false;
        bool requireCtrl = false;
        bool requireShift = false;
    };

    struct FocusInput {
        bool active = false;
        bool up = false;
        bool down = false;
        bool left = false;
        bool right = false;
        bool confirm = false;
        bool cancel = false;
        bool deletePressed = false;
    };

    Input() {
        SetDefaultActionBindings();
    }

    ~Input() {
        Shutdown();
    }

    void SetRenderer(SDL_Renderer* renderer) {
        m_renderer = renderer;
    }

    void InitializeGamepads() {
        if (m_gamepad) {
            return;
        }

        int count = 0;
        SDL_JoystickID* gamepads = SDL_GetGamepads(&count);
        if (!gamepads) {
            return;
        }

        for (int i = 0; i < count; ++i) {
            if (OpenGamepad(gamepads[i])) {
                break;
            }
        }

        SDL_free(gamepads);
    }

    void Shutdown() {
        CloseGamepad();
    }

    void BindAction(Action action, const std::vector<KeyChord>& chords) {
        m_actionBindings[ActionIndex(action)] = chords;
    }

    void BindGamepadAction(Action action, const std::vector<SDL_GamepadButton>& buttons) {
        m_gamepadActionBindings[ActionIndex(action)] = buttons;
    }

    [[nodiscard]] bool IsActionJustPressed(Action action) const {
        const auto& chords = m_actionBindings[ActionIndex(action)];
        for (const KeyChord& chord : chords) {
            if (chord.key == SDL_SCANCODE_UNKNOWN) {
                continue;
            }
            if (JustPressed(chord.key) && AreChordModifiersSatisfied(chord)) {
                return true;
            }
        }

        const auto& buttons = m_gamepadActionBindings[ActionIndex(action)];
        for (SDL_GamepadButton button : buttons) {
            if (GamepadButtonJustPressed(button)) {
                return true;
            }
        }

        return VirtualGamepadActionJustPressed(action);
    }

    [[nodiscard]] bool IsActionHeld(Action action) const {
        const auto& chords = m_actionBindings[ActionIndex(action)];
        for (const KeyChord& chord : chords) {
            if (chord.key == SDL_SCANCODE_UNKNOWN) {
                continue;
            }
            if (IsHeld(chord.key) && AreChordModifiersSatisfied(chord)) {
                return true;
            }
        }

        const auto& buttons = m_gamepadActionBindings[ActionIndex(action)];
        for (SDL_GamepadButton button : buttons) {
            if (GamepadButtonHeld(button)) {
                return true;
            }
        }

        return VirtualGamepadActionHeld(action);
    }

    void BeginFrame() {
        m_prevKeys = m_currKeys;
        m_prevGamepadButtons = m_currGamepadButtons;
        m_prevGamepadAxes = m_currGamepadAxes;

        m_justClickedLeft = false;
        m_justClickedRight = false;
        m_justClickedMiddle = false;
        m_leftClickConsumed = false;
        m_rightClickConsumed = false;
        m_middleClickConsumed = false;
        m_scrollY = 0;
    }

    void ProcessEvent(const SDL_Event& e) {
        if (e.type == SDL_EVENT_QUIT) {
            m_wantsQuit = true;
            return;
        }

        if (e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
            m_wantsQuit = true;
            return;
        }

        if (e.type == SDL_EVENT_KEY_DOWN) {
            m_lastDevice = LastDevice::KeyboardMouse;
        }

        if (e.type == SDL_EVENT_MOUSE_MOTION) {
            if (std::fabs(e.motion.xrel) > 0.01f || std::fabs(e.motion.yrel) > 0.01f) {
                m_lastDevice = LastDevice::KeyboardMouse;
            }
        }

        if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
            m_lastDevice = LastDevice::KeyboardMouse;
            if (e.button.button == SDL_BUTTON_LEFT) m_justClickedLeft = true;
            if (e.button.button == SDL_BUTTON_RIGHT) m_justClickedRight = true;
            if (e.button.button == SDL_BUTTON_MIDDLE) m_justClickedMiddle = true;
        }

        if (e.type == SDL_EVENT_MOUSE_WHEEL) {
            m_lastDevice = LastDevice::KeyboardMouse;
            m_scrollY += static_cast<int>(e.wheel.y);
        }

        if (e.type == SDL_EVENT_GAMEPAD_ADDED) {
            if (!m_gamepad) {
                (void)OpenGamepad(e.gdevice.which);
            }
            return;
        }

        if (e.type == SDL_EVENT_GAMEPAD_REMOVED) {
            if (m_gamepad && e.gdevice.which == m_gamepadId) {
                CloseGamepad();
                InitializeGamepads();
            }
            return;
        }

        if (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN || e.type == SDL_EVENT_GAMEPAD_AXIS_MOTION) {
            if (!m_gamepad) {
                InitializeGamepads();
            }
            m_lastDevice = LastDevice::Gamepad;
        }
    }

    void EndFrame() {
        int numKeys = 0;
        const bool* state = SDL_GetKeyboardState(&numKeys);
        m_currKeys.clear();
        for (int i = 0; i < numKeys; ++i) {
            if (state[i]) {
                m_currKeys[static_cast<SDL_Scancode>(i)] = true;
            }
        }

        const Uint32 mouseState = SDL_GetMouseState(nullptr, nullptr);
        m_heldLeft = (mouseState & SDL_BUTTON_LMASK) != 0;

        PollGamepadState();
    }

    bool JustPressed(SDL_Scancode key) const {
        const bool curr = m_currKeys.count(key) > 0;
        const bool prev = m_prevKeys.count(key) > 0;
        return curr && !prev;
    }

    bool IsHeld(SDL_Scancode key) const {
        return m_currKeys.count(key) > 0;
    }

    bool JustClicked() const { return m_justClickedLeft && !m_leftClickConsumed; }
    bool JustRightClicked() const { return m_justClickedRight && !m_rightClickConsumed; }
    bool JustMiddleClicked() const { return m_justClickedMiddle && !m_middleClickConsumed; }
    bool JustAnyClicked() const { return JustClicked() || JustRightClicked() || JustMiddleClicked(); }
    bool IsLeftHeld() const { return m_heldLeft; }
    bool WantsQuit() const { return m_wantsQuit; }
    void RequestQuit() { m_wantsQuit = true; }

    [[nodiscard]] LastDevice GetLastDevice() const noexcept {
        return m_lastDevice;
    }

    [[nodiscard]] bool IsGamepadActive() const noexcept {
        return m_lastDevice == LastDevice::Gamepad;
    }

    [[nodiscard]] bool HasGamepad() const noexcept {
        return m_gamepad != nullptr;
    }

    [[nodiscard]] FocusInput GetFocusInput() const {
        FocusInput out;
        out.active = IsGamepadActive();
        out.up = IsActionJustPressed(Action::FocusUp);
        out.down = IsActionJustPressed(Action::FocusDown);
        out.left = IsActionJustPressed(Action::FocusLeft);
        out.right = IsActionJustPressed(Action::FocusRight);
        out.confirm = IsActionJustPressed(Action::Advance);
        out.cancel = IsActionJustPressed(Action::OpenSystemMenu);
        out.deletePressed = IsActionJustPressed(Action::DeleteSaveSlot);
        return out;
    }

    void ConsumeLeftClick() {
        if (m_justClickedLeft) m_leftClickConsumed = true;
    }

    void ConsumeRightClick() {
        if (m_justClickedRight) m_rightClickConsumed = true;
    }

    void ConsumeMiddleClick() {
        if (m_justClickedMiddle) m_middleClickConsumed = true;
    }

    float GetMouseX() const {
        float x = 0.0f;
        float y = 0.0f;
        GetMousePosition(x, y);
        return x;
    }

    float GetMouseY() const {
        float x = 0.0f;
        float y = 0.0f;
        GetMousePosition(x, y);
        return y;
    }

    int PopScroll() {
        int s = m_scrollY;
        m_scrollY = 0;
        return s;
    }

private:
    static constexpr std::size_t kActionCount = static_cast<std::size_t>(Action::Count);
    static constexpr Sint16 kAxisDigitalThreshold = 18000;

    [[nodiscard]] static constexpr std::size_t ActionIndex(Action action) {
        return static_cast<std::size_t>(action);
    }

    [[nodiscard]] static constexpr std::size_t GamepadButtonIndex(SDL_GamepadButton button) {
        return static_cast<std::size_t>(button);
    }

    [[nodiscard]] static constexpr std::size_t GamepadAxisIndex(SDL_GamepadAxis axis) {
        return static_cast<std::size_t>(axis);
    }

    [[nodiscard]] static bool IsValidButton(SDL_GamepadButton button) {
        return button >= SDL_GAMEPAD_BUTTON_SOUTH && button < SDL_GAMEPAD_BUTTON_COUNT;
    }

    [[nodiscard]] bool IsAltHeld() const {
        return IsHeld(SDL_SCANCODE_LALT) || IsHeld(SDL_SCANCODE_RALT);
    }

    [[nodiscard]] bool IsCtrlHeld() const {
        return IsHeld(SDL_SCANCODE_LCTRL) || IsHeld(SDL_SCANCODE_RCTRL);
    }

    [[nodiscard]] bool IsShiftHeld() const {
        return IsHeld(SDL_SCANCODE_LSHIFT) || IsHeld(SDL_SCANCODE_RSHIFT);
    }

    [[nodiscard]] bool AreChordModifiersSatisfied(const KeyChord& chord) const {
        if (chord.requireAlt && !IsAltHeld()) {
            return false;
        }
        if (chord.requireCtrl && !IsCtrlHeld()) {
            return false;
        }
        if (chord.requireShift && !IsShiftHeld()) {
            return false;
        }
        return true;
    }

    [[nodiscard]] bool GamepadButtonHeld(SDL_GamepadButton button) const {
        if (!IsValidButton(button)) {
            return false;
        }
        return m_currGamepadButtons[GamepadButtonIndex(button)];
    }

    [[nodiscard]] bool GamepadButtonJustPressed(SDL_GamepadButton button) const {
        if (!IsValidButton(button)) {
            return false;
        }
        const std::size_t idx = GamepadButtonIndex(button);
        return m_currGamepadButtons[idx] && !m_prevGamepadButtons[idx];
    }

    [[nodiscard]] bool AxisHeld(SDL_GamepadAxis axis, int direction) const {
        const Sint16 value = m_currGamepadAxes[GamepadAxisIndex(axis)];
        return direction < 0 ? value <= -kAxisDigitalThreshold : value >= kAxisDigitalThreshold;
    }

    [[nodiscard]] bool AxisJustPressed(SDL_GamepadAxis axis, int direction) const {
        const std::size_t idx = GamepadAxisIndex(axis);
        const Sint16 curr = m_currGamepadAxes[idx];
        const Sint16 prev = m_prevGamepadAxes[idx];
        if (direction < 0) {
            return curr <= -kAxisDigitalThreshold && prev > -kAxisDigitalThreshold;
        }
        return curr >= kAxisDigitalThreshold && prev < kAxisDigitalThreshold;
    }

    [[nodiscard]] bool VirtualGamepadActionHeld(Action action) const {
        switch (action) {
        case Action::FocusUp:
            return AxisHeld(SDL_GAMEPAD_AXIS_LEFTY, -1);
        case Action::FocusDown:
            return AxisHeld(SDL_GAMEPAD_AXIS_LEFTY, 1);
        case Action::FocusLeft:
            return AxisHeld(SDL_GAMEPAD_AXIS_LEFTX, -1);
        case Action::FocusRight:
            return AxisHeld(SDL_GAMEPAD_AXIS_LEFTX, 1);
        default:
            return false;
        }
    }

    [[nodiscard]] bool VirtualGamepadActionJustPressed(Action action) const {
        switch (action) {
        case Action::FocusUp:
            return AxisJustPressed(SDL_GAMEPAD_AXIS_LEFTY, -1);
        case Action::FocusDown:
            return AxisJustPressed(SDL_GAMEPAD_AXIS_LEFTY, 1);
        case Action::FocusLeft:
            return AxisJustPressed(SDL_GAMEPAD_AXIS_LEFTX, -1);
        case Action::FocusRight:
            return AxisJustPressed(SDL_GAMEPAD_AXIS_LEFTX, 1);
        default:
            return false;
        }
    }

    bool OpenGamepad(SDL_JoystickID id) {
        SDL_Gamepad* opened = SDL_OpenGamepad(id);
        if (!opened) {
            return false;
        }

        CloseGamepad();
        m_gamepad = opened;
        m_gamepadId = id;
        m_currGamepadButtons.fill(false);
        m_prevGamepadButtons.fill(false);
        m_currGamepadAxes.fill(0);
        m_prevGamepadAxes.fill(0);
        return true;
    }

    void CloseGamepad() {
        if (m_gamepad) {
            SDL_CloseGamepad(m_gamepad);
            m_gamepad = nullptr;
        }
        m_gamepadId = 0;
        m_currGamepadButtons.fill(false);
        m_prevGamepadButtons.fill(false);
        m_currGamepadAxes.fill(0);
        m_prevGamepadAxes.fill(0);
    }

    void PollGamepadState() {
        if (!m_gamepad) {
            return;
        }

        for (int i = 0; i < SDL_GAMEPAD_BUTTON_COUNT; ++i) {
            m_currGamepadButtons[static_cast<std::size_t>(i)] =
                SDL_GetGamepadButton(m_gamepad, static_cast<SDL_GamepadButton>(i));
        }
        for (int i = 0; i < SDL_GAMEPAD_AXIS_COUNT; ++i) {
            m_currGamepadAxes[static_cast<std::size_t>(i)] =
                SDL_GetGamepadAxis(m_gamepad, static_cast<SDL_GamepadAxis>(i));
        }
    }

    void SetDefaultActionBindings() {
        BindAction(Action::ToggleDebugOverlay, {{ SDL_SCANCODE_F2 }});
        BindAction(Action::ToggleFullscreen, {{ SDL_SCANCODE_F11 }});
        BindAction(Action::CopyWorldCoords, {{ SDL_SCANCODE_F3 }});
        BindAction(Action::OpenSystemMenu, {{ SDL_SCANCODE_ESCAPE }});
        BindAction(Action::OpenSaveMenu, {{ SDL_SCANCODE_F5 }});
        BindAction(Action::OpenLoadMenu, {{ SDL_SCANCODE_F9 }});
        BindAction(Action::QuickSave, {{ SDL_SCANCODE_F6 }, { SDL_SCANCODE_S, true, false, false }});
        BindAction(Action::QuickLoad, {{ SDL_SCANCODE_F8 }, { SDL_SCANCODE_L, true, false, false }});
        BindAction(Action::Advance, {{ SDL_SCANCODE_RETURN }, { SDL_SCANCODE_KP_ENTER }, { SDL_SCANCODE_SPACE }});
        BindAction(Action::OpenLog, {{ SDL_SCANCODE_PAGEUP }});
        BindAction(Action::ToggleHideUI, {{ SDL_SCANCODE_H }});
        BindAction(Action::SkipModifier, {{ SDL_SCANCODE_LCTRL }, { SDL_SCANCODE_RCTRL }});
        BindAction(Action::DeleteSaveSlot, {{ SDL_SCANCODE_DELETE }});
        BindAction(Action::FocusUp, {{ SDL_SCANCODE_UP }});
        BindAction(Action::FocusDown, {{ SDL_SCANCODE_DOWN }});
        BindAction(Action::FocusLeft, {{ SDL_SCANCODE_LEFT }});
        BindAction(Action::FocusRight, {{ SDL_SCANCODE_RIGHT }});

        BindGamepadAction(Action::Advance, { SDL_GAMEPAD_BUTTON_SOUTH });
        BindGamepadAction(Action::OpenSystemMenu, { SDL_GAMEPAD_BUTTON_EAST, SDL_GAMEPAD_BUTTON_START });
        BindGamepadAction(Action::OpenLog, { SDL_GAMEPAD_BUTTON_LEFT_SHOULDER });
        BindGamepadAction(Action::ToggleHideUI, { SDL_GAMEPAD_BUTTON_RIGHT_STICK });
        BindGamepadAction(Action::SkipModifier, { SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER });
        BindGamepadAction(Action::DeleteSaveSlot, { SDL_GAMEPAD_BUTTON_WEST });
        BindGamepadAction(Action::FocusUp, { SDL_GAMEPAD_BUTTON_DPAD_UP });
        BindGamepadAction(Action::FocusDown, { SDL_GAMEPAD_BUTTON_DPAD_DOWN });
        BindGamepadAction(Action::FocusLeft, { SDL_GAMEPAD_BUTTON_DPAD_LEFT });
        BindGamepadAction(Action::FocusRight, { SDL_GAMEPAD_BUTTON_DPAD_RIGHT });
    }

    void GetMousePosition(float& outX, float& outY) const {
        float windowX = 0.0f;
        float windowY = 0.0f;
        SDL_GetMouseState(&windowX, &windowY);

        if (m_renderer && SDL_RenderCoordinatesFromWindow(m_renderer, windowX, windowY, &outX, &outY)) {
            return;
        }

        outX = windowX;
        outY = windowY;
    }

    SDL_Renderer* m_renderer = nullptr;
    SDL_Gamepad* m_gamepad = nullptr;
    SDL_JoystickID m_gamepadId = 0;
    std::unordered_map<SDL_Scancode, bool> m_currKeys;
    std::unordered_map<SDL_Scancode, bool> m_prevKeys;
    std::array<std::vector<KeyChord>, kActionCount> m_actionBindings;
    std::array<std::vector<SDL_GamepadButton>, kActionCount> m_gamepadActionBindings;
    std::array<bool, SDL_GAMEPAD_BUTTON_COUNT> m_currGamepadButtons{};
    std::array<bool, SDL_GAMEPAD_BUTTON_COUNT> m_prevGamepadButtons{};
    std::array<Sint16, SDL_GAMEPAD_AXIS_COUNT> m_currGamepadAxes{};
    std::array<Sint16, SDL_GAMEPAD_AXIS_COUNT> m_prevGamepadAxes{};
    LastDevice m_lastDevice = LastDevice::KeyboardMouse;
    bool m_wantsQuit = false;
    bool m_justClickedLeft = false;
    bool m_justClickedRight = false;
    bool m_justClickedMiddle = false;
    bool m_leftClickConsumed = false;
    bool m_rightClickConsumed = false;
    bool m_middleClickConsumed = false;
    bool m_heldLeft = false;
    int m_scrollY = 0;
};
