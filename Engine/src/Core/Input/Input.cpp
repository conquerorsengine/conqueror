#include "Input.h"
#include "Core/Application.h"
#include "Core/Logging/Log.h"
#include <fstream>
#include <algorithm>
#include <sstream>

#include <GLFW/glfw3.h>

namespace Conqueror
{
    // Static member initialization
    std::unordered_map<KeyCode, bool> Input::s_KeyStates;
    std::unordered_map<KeyCode, bool> Input::s_PreviousKeyStates;
    std::unordered_map<MouseCode, bool> Input::s_MouseButtonStates;
    std::unordered_map<MouseCode, bool> Input::s_PreviousMouseButtonStates;
    
    glm::vec2 Input::s_MousePosition = glm::vec2(0.0f);
    glm::vec2 Input::s_PreviousMousePosition = glm::vec2(0.0f);
    float Input::s_MouseScrollDelta = 0.0f;
    float Input::s_MouseScrollAccumulator = 0.0f;

    void Input::AddMouseScrollDelta(float delta)
    {
        s_MouseScrollAccumulator += delta;
    }
    
    std::unordered_map<int, Input::GamepadState> Input::s_GamepadStates;
    std::unordered_map<std::string, InputContext> Input::s_Contexts;
    std::string Input::s_ActiveContext = "";
    std::vector<std::string> Input::s_ContextStack;
    
    bool Input::s_InputBufferEnabled = false;
    size_t Input::s_InputBufferSize = 100;
    std::queue<InputEvent> Input::s_InputBuffer;
    
    bool Input::s_Recording = false;
    bool Input::s_Replaying = false;
    std::vector<InputEvent> Input::s_RecordedEvents;
    std::vector<InputEvent> Input::s_ReplayEvents;
    size_t Input::s_ReplayIndex = 0;
    uint64_t Input::s_ReplayStartTime = 0;
    
    std::unordered_map<std::string, Input::ComboState> Input::s_Combos;
    float Input::s_DeadZone = 0.2f;
    int Input::s_CursorMode = 0;
    int Input::s_AppliedCursorMode = 0;
    bool Input::s_CursorReleased = false;
    static uint32_t s_CursorModeEpoch = 0;

    bool Input::IsKeyPressed(const KeyCode key)
    {
        auto* window = static_cast<GLFWwindow*>(Application::Get().GetWindow().GetNativeWindow());
        auto state = glfwGetKey(window, static_cast<int32_t>(key));
        return state == GLFW_PRESS || state == GLFW_REPEAT;
    }

    bool Input::IsMouseButtonPressed(const MouseCode button)
    {
        auto* window = static_cast<GLFWwindow*>(Application::Get().GetWindow().GetNativeWindow());
        auto state = glfwGetMouseButton(window, static_cast<int32_t>(button));
        return state == GLFW_PRESS;
    }

    glm::vec2 Input::GetMousePosition()
    {
        auto* window = static_cast<GLFWwindow*>(Application::Get().GetWindow().GetNativeWindow());
        double xpos, ypos;
        glfwGetCursorPos(window, &xpos, &ypos);
        return { (float)xpos, (float)ypos };
    }

    float Input::GetMouseX()
    {
        return GetMousePosition().x;
    }

    float Input::GetMouseY()
    {
        return GetMousePosition().y;
    }

    glm::vec2 Input::GetMouseDelta()
    {
        return s_MousePosition - s_PreviousMousePosition;
    }

    float Input::GetMouseScrollDelta()
    {
        return s_MouseScrollDelta;
    }

    // Gamepad functions
    namespace
    {
        bool s_VirtualPad = false;
        std::string s_VirtualPadName;
        GLFWgamepadstate s_VirtualPadState{};
    }

    void Input::SetVirtualGamepad(bool connected, const std::string& name)
    {
        s_VirtualPad = connected;
        s_VirtualPadName = name;
        s_VirtualPadState = {};
        s_VirtualPadState.axes[GLFW_GAMEPAD_AXIS_LEFT_TRIGGER] = -1.0f;
        s_VirtualPadState.axes[GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER] = -1.0f;
    }

    bool Input::IsVirtualGamepad() { return s_VirtualPad; }

    void Input::SetVirtualGamepadButton(int button, bool down)
    {
        if (button >= 0 && button <= GLFW_GAMEPAD_BUTTON_LAST)
            s_VirtualPadState.buttons[button] = down ? GLFW_PRESS : GLFW_RELEASE;
    }

    void Input::SetVirtualGamepadAxis(int axis, float value)
    {
        if (axis >= 0 && axis <= GLFW_GAMEPAD_AXIS_LAST)
            s_VirtualPadState.axes[axis] = std::clamp(value, -1.0f, 1.0f);
    }

    std::string Input::GetGamepadName(int gamepadID)
    {
        if (gamepadID == 0 && s_VirtualPad)
            return s_VirtualPadName;
        if (!IsGamepadConnected(gamepadID))
            return {};
        const char* name = glfwJoystickIsGamepad(gamepadID) ? glfwGetGamepadName(gamepadID) : glfwGetJoystickName(gamepadID);
        return name ? name : "";
    }

    bool Input::IsGamepadConnected(int gamepadID)
    {
        if (gamepadID == 0 && s_VirtualPad)
            return true;
        if (gamepadID < GLFW_JOYSTICK_1 || gamepadID > GLFW_JOYSTICK_LAST)
            return false;
        return glfwJoystickPresent(gamepadID) == GLFW_TRUE;
    }

    GamepadInfo Input::GetGamepadInfo(int gamepadID)
    {
        GamepadInfo info;
        info.ID = gamepadID;
        info.Connected = IsGamepadConnected(gamepadID);
        
        if (info.Connected)
        {
            info.Name = glfwGetJoystickName(gamepadID);
            
            int buttonCount;
            glfwGetJoystickButtons(gamepadID, &buttonCount);
            info.ButtonCount = buttonCount;
            
            int axisCount;
            glfwGetJoystickAxes(gamepadID, &axisCount);
            info.AxisCount = axisCount;
        }
        else
        {
            info.Name = "Disconnected";
            info.ButtonCount = 0;
            info.AxisCount = 0;
        }
        
        return info;
    }

    int Input::GetConnectedGamepadCount()
    {
        int count = 0;
        for (int i = 0; i < GLFW_JOYSTICK_LAST; i++)
        {
            if (IsGamepadConnected(i))
                count++;
        }
        return count;
    }

    // SDL eşlemesi bilinen kollar için standart düzen (A=0, B=1, X=2, Y=3, LB, RB, Back, Start, Guide,
    // LS, RS, D-pad yukarı/sağ/aşağı/sol; eksenler LX, LY, RX, RY, LT, RT). Bilinmeyen cihazlar ham joystick verisine düşer.
    static bool ReadGamepadState(int gamepadID, GLFWgamepadstate& state)
    {
        if (gamepadID == 0 && s_VirtualPad)
        {
            state = s_VirtualPadState;
            return true;
        }
        if (gamepadID < 0 || gamepadID > GLFW_JOYSTICK_LAST)
            return false;
        return glfwJoystickIsGamepad(gamepadID) == GLFW_TRUE && glfwGetGamepadState(gamepadID, &state) == GLFW_TRUE;
    }

    bool Input::IsGamepadButtonPressed(int button, int gamepadID)
    {
        if (!IsGamepadConnected(gamepadID))
            return false;

        GLFWgamepadstate state;
        if (ReadGamepadState(gamepadID, state))
            return button >= 0 && button <= GLFW_GAMEPAD_BUTTON_LAST && state.buttons[button] == GLFW_PRESS;
            
        int count;
        const unsigned char* buttons = glfwGetJoystickButtons(gamepadID, &count);
        
        if (button >= 0 && button < count)
            return buttons[button] == GLFW_PRESS;
            
        return false;
    }

    bool Input::IsGamepadButtonDown(int button, int gamepadID)
    {
        return IsGamepadButtonPressed(button, gamepadID); // Simplified fallback
    }

    bool Input::IsGamepadButtonUp(int button, int gamepadID)
    {
        if (!IsGamepadConnected(gamepadID))
            return false;

        GLFWgamepadstate state;
        if (ReadGamepadState(gamepadID, state))
            return button >= 0 && button <= GLFW_GAMEPAD_BUTTON_LAST && state.buttons[button] == GLFW_RELEASE;
            
        int count;
        const unsigned char* buttons = glfwGetJoystickButtons(gamepadID, &count);
        
        if (button >= 0 && button < count)
            return buttons[button] == GLFW_RELEASE;
            
        return false;
    }

    float Input::GetGamepadAxis(int axis, int gamepadID)
    {
        if (!IsGamepadConnected(gamepadID))
            return 0.0f;

        GLFWgamepadstate state;
        if (ReadGamepadState(gamepadID, state))
        {
            if (axis < 0 || axis > GLFW_GAMEPAD_AXIS_LAST)
                return 0.0f;
            // Tetikler -1 (bırakılmış) .. 1 aralığındadır; ölü bölge yalnızca çubuklara uygulanır
            if (axis >= GLFW_GAMEPAD_AXIS_LEFT_TRIGGER)
                return state.axes[axis];
            return ApplyDeadZone(state.axes[axis], s_DeadZone);
        }
            
        int count;
        const float* axes = glfwGetJoystickAxes(gamepadID, &count);
        
        if (axis >= 0 && axis < count)
            return ApplyDeadZone(axes[axis], s_DeadZone);
            
        return 0.0f;
    }

    glm::vec2 Input::GetGamepadLeftStick(int gamepadID)
    {
        return glm::vec2(GetGamepadAxis(0, gamepadID), GetGamepadAxis(1, gamepadID));
    }

    glm::vec2 Input::GetGamepadRightStick(int gamepadID)
    {
        return glm::vec2(GetGamepadAxis(2, gamepadID), GetGamepadAxis(3, gamepadID));
    }

    // 0 (bırakılmış) .. 1 (tam basılı)
    float Input::GetGamepadLeftTrigger(int gamepadID)
    {
        GLFWgamepadstate state;
        if (IsGamepadConnected(gamepadID) && ReadGamepadState(gamepadID, state))
            return glm::clamp((state.axes[GLFW_GAMEPAD_AXIS_LEFT_TRIGGER] + 1.0f) * 0.5f, 0.0f, 1.0f);
        return GetGamepadAxis(4, gamepadID);
    }

    float Input::GetGamepadRightTrigger(int gamepadID)
    {
        GLFWgamepadstate state;
        if (IsGamepadConnected(gamepadID) && ReadGamepadState(gamepadID, state))
            return glm::clamp((state.axes[GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER] + 1.0f) * 0.5f, 0.0f, 1.0f);
        return GetGamepadAxis(5, gamepadID);
    }

    void Input::SetDeadZone(float deadZone)
    {
        s_DeadZone = deadZone;
    }

    float Input::GetDeadZone()
    {
        return s_DeadZone;
    }

    float Input::ApplyDeadZone(float value, float deadZone)
    {
        if (std::abs(value) < deadZone)
            return 0.0f;
        return value;
    }

    bool Input::IsKeyDown(const KeyCode key)
    {
        return s_KeyStates[key] && !s_PreviousKeyStates[key];
    }

    bool Input::IsKeyUp(const KeyCode key)
    {
        return !s_KeyStates[key] && s_PreviousKeyStates[key];
    }

    bool Input::IsMouseButtonDown(const MouseCode button)
    {
        return s_MouseButtonStates[button] && !s_PreviousMouseButtonStates[button];
    }

    bool Input::IsMouseButtonUp(const MouseCode button)
    {
        return !s_MouseButtonStates[button] && s_PreviousMouseButtonStates[button];
    }

    // Betik API'si: 0 normal, 1 gizli, 2 kilitli (FPS). GLFW sabitleri (GLFW_CURSOR_*) de kabul edilir.
    static int NormalizeCursorMode(int mode)
    {
        if (mode == GLFW_CURSOR_NORMAL) return 0;
        if (mode == GLFW_CURSOR_HIDDEN) return 1;
        if (mode == GLFW_CURSOR_DISABLED) return 2;
        return (mode >= 0 && mode <= 2) ? mode : -1;
    }

    void Input::SetCursorMode(int mode)
    {
        int m = NormalizeCursorMode(mode);
        if (m < 0)
            return;
        s_CursorMode = m;
        // Kullanıcı bıraktıysa (Esc) yeni kilit isteği oyun alanına tıklanana kadar bekler (Unreal Shift+F1 gibi);
        // aksi halde her karede kilit isteyen bir betik imleci hiç bırakmazdı
        if (m == 0)
            s_CursorReleased = false;
        if (!s_CursorReleased)
            ApplyCursorMode(m);
    }

    void Input::ReleaseCursor(float x, float y)
    {
        if (s_CursorMode == 0 || s_AppliedCursorMode == 0)
            return;
        auto* window = static_cast<GLFWwindow*>(Application::Get().GetWindow().GetNativeWindow());
        if (!window)
            return;
#if defined(GLFW_PLATFORM_WAYLAND)
        const bool wayland = glfwGetPlatform() == GLFW_PLATFORM_WAYLAND;
#else
        const bool wayland = false;
#endif
        // Wayland imleci kendisi taşıyamaz: kilit bırakılmadan önce konum ipucu verilir, imleç orada çıkar.
        // Diğer platformlar kilidi bırakırken eski konuma döner: bıraktıktan sonra taşınır.
        const bool place = x >= 0.0f && y >= 0.0f;
        if (place && wayland && s_AppliedCursorMode == 2)
            glfwSetCursorPos(window, (double)x, (double)y);
        s_CursorReleased = true;
        ApplyCursorMode(0);
        if (place && !wayland)
            glfwSetCursorPos(window, (double)x, (double)y);
    }

    void Input::RecaptureCursor()
    {
        s_CursorReleased = false;
        if (s_CursorMode != 0)
            ApplyCursorMode(s_CursorMode);
    }

    void Input::ApplyCursorMode(int mode)
    {
        int m = NormalizeCursorMode(mode);
        if (m < 0)
            return;
        auto* window = static_cast<GLFWwindow*>(Application::Get().GetWindow().GetNativeWindow());
        if (!window)
            return;
        const int glfwMode = m == 0 ? GLFW_CURSOR_NORMAL : (m == 1 ? GLFW_CURSOR_HIDDEN : GLFW_CURSOR_DISABLED);
        glfwSetInputMode(window, GLFW_CURSOR, glfwMode);
        // Kilitli imleçte ham fare hareketi (hızlanmasız, FPS kamerası için)
        if (glfwRawMouseMotionSupported())
            glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, m == 2 ? GLFW_TRUE : GLFW_FALSE);
        if (s_AppliedCursorMode != m)
        {
            CQ_CORE_INFO("[Input] Cursor {0}", m == 0 ? "normal" : (m == 1 ? "hidden" : "locked"));
            ++s_CursorModeEpoch;
            s_PreviousMousePosition = s_MousePosition;
        }
        s_AppliedCursorMode = m;
    }

    uint32_t Input::GetCursorModeEpoch() { return s_CursorModeEpoch; }

    void Input::SetGamepadVibration(float leftMotor, float rightMotor, int gamepadID)
    {
        // GLFW does not natively support haptic/vibration feedback
        // This is a no-op placeholder for future platform-specific implementation
        (void)leftMotor; (void)rightMotor; (void)gamepadID;
    }

    void Input::StopGamepadVibration(int gamepadID)
    {
        SetGamepadVibration(0.0f, 0.0f, gamepadID);
    }

    void Input::SetCursorPosition(float x, float y)
    {
        auto* window = static_cast<GLFWwindow*>(Application::Get().GetWindow().GetNativeWindow());
        glfwSetCursorPos(window, (double)x, (double)y);
    }

    void Input::Update()
    {
        s_PreviousKeyStates = s_KeyStates;
        s_PreviousMouseButtonStates = s_MouseButtonStates;
        
        auto* window = static_cast<GLFWwindow*>(Application::Get().GetWindow().GetNativeWindow());
        uint64_t currentTimestamp = (uint64_t)(glfwGetTime() * 1000.0);

        for (int i = 32; i <= 348; i++) {
            bool isDown = (glfwGetKey(window, i) == GLFW_PRESS);
            s_KeyStates[(KeyCode)i] = isDown;
            
            if (isDown && !s_PreviousKeyStates[(KeyCode)i]) {
                if (s_InputBufferEnabled) {
                    InputEvent ev; ev.EventType = InputEvent::Type::KeyPress;
                    ev.Timestamp = currentTimestamp; ev.Code = i; ev.Value = 1.0f;
                    s_InputBuffer.push(ev);
                }
                if (s_Recording) {
                    InputEvent ev; ev.EventType = InputEvent::Type::KeyPress;
                    ev.Timestamp = currentTimestamp; ev.Code = i; ev.Value = 1.0f;
                    s_RecordedEvents.push_back(ev);
                }
            }
        }
        for (int i = 0; i <= 7; i++) {
            bool isDown = (glfwGetMouseButton(window, i) == GLFW_PRESS);
            s_MouseButtonStates[(MouseCode)i] = isDown;
        }

        s_PreviousMousePosition = s_MousePosition;
        s_MousePosition = GetMousePosition();
        // Callback'lerde biriken scroll bu frame'in değeri olur, sonra sıfırlanır.
        // (Run döngüsü: Update -> katman OnUpdate'leri, o yüzden okuma güncel kalır.)
        s_MouseScrollDelta = s_MouseScrollAccumulator;
        s_MouseScrollAccumulator = 0.0f;
        
        if (s_Recording && (s_MousePosition != s_PreviousMousePosition)) {
            InputEvent ev; ev.EventType = InputEvent::Type::MouseMove;
            ev.Timestamp = currentTimestamp; ev.Position = s_MousePosition;
            s_RecordedEvents.push_back(ev);
        }
        
        if (s_Replaying) ProcessReplay();
        if (s_InputBufferEnabled) ProcessInputBuffer();
        CheckCombos();

        float currentTime = (float)glfwGetTime();
        for (auto& [ctxName, ctx] : s_Contexts) {
            if (!ctx.Enabled) continue;
            for (auto& [actName, act] : ctx.Actions) {
                bool isPressed = false;
                if (act.IsChord) {
                    bool allPressed = true;
                    for (auto k : act.Keys) if (!IsKeyPressed(k)) allPressed = false;
                    isPressed = allPressed && !act.Keys.empty();
                } else {
                    for (auto k : act.Keys) if (IsKeyPressed(k)) isPressed = true;
                }
                for (auto m : act.MouseButtons) if (IsMouseButtonPressed(m)) isPressed = true;
                for (auto g : act.GamepadButtons) if (IsGamepadButtonPressed(g, act.PlayerIndex)) isPressed = true;
                for (auto a : act.GamepadAxes) if (std::abs(GetGamepadAxis(a, act.PlayerIndex)) > act.DeadZone) isPressed = true;
                
                if (isPressed) {
                    if (act.PressStartTime < 0.0f) act.PressStartTime = currentTime;
                } else {
                    act.PressStartTime = -1.0f;
                }
            }
        }
    }

    void Input::CreateContext(const std::string& name, int priority)
    {
        s_Contexts[name] = { name, {}, priority, true };
    }

    void Input::RemoveContext(const std::string& name)
    {
        s_Contexts.erase(name);
        if (s_ActiveContext == name) s_ActiveContext = "";
    }

    void Input::SetContextEnabled(const std::string& name, bool enabled)
    {
        if (s_Contexts.find(name) != s_Contexts.end()) s_Contexts[name].Enabled = enabled;
    }

    void Input::SwitchContext(const std::string& name)
    {
        if (s_Contexts.find(name) != s_Contexts.end())
        {
            s_ActiveContext = name;
            s_ContextStack.clear();
            s_ContextStack.push_back(name);
        }
    }

    void Input::PushContext(const std::string& name)
    {
        if (s_Contexts.find(name) != s_Contexts.end())
        {
            s_ContextStack.push_back(name);
            s_ActiveContext = name;
        }
    }

    void Input::PopContext()
    {
        if (!s_ContextStack.empty())
        {
            s_ContextStack.pop_back();
            s_ActiveContext = s_ContextStack.empty() ? "" : s_ContextStack.back();
        }
    }

    void Input::BindAction(const std::string& contextName, const std::string& actionName, InputActionType type, const std::vector<KeyCode>& keys)
    {
        if (s_Contexts.find(contextName) == s_Contexts.end()) CreateContext(contextName);
        InputAction action;
        action.Name = actionName;
        action.Type = type;
        action.Keys = keys;
        s_Contexts[contextName].Actions[actionName] = action;
    }

    void Input::BindMouseAction(const std::string& contextName, const std::string& actionName, InputActionType type, const std::vector<MouseCode>& mouseButtons)
    {
        if (s_Contexts.find(contextName) == s_Contexts.end()) CreateContext(contextName);
        InputAction action;
        action.Name = actionName;
        action.Type = type;
        action.MouseButtons = mouseButtons;
        s_Contexts[contextName].Actions[actionName] = action;
    }

    void Input::BindChord(const std::string& contextName, const std::string& actionName, const std::vector<KeyCode>& modifiers, KeyCode key)
    {
        if (s_Contexts.find(contextName) == s_Contexts.end()) CreateContext(contextName);
        InputAction action;
        action.Name = actionName;
        action.Type = InputActionType::Button;
        action.Keys = modifiers;
        action.Keys.push_back(key);
        action.IsChord = true;
        s_Contexts[contextName].Actions[actionName] = action;
    }

    void Input::BindGamepadAction(const std::string& contextName, const std::string& actionName, InputActionType type, const std::vector<int>& buttons, const std::vector<int>& axes)
    {
        if (s_Contexts.find(contextName) == s_Contexts.end()) CreateContext(contextName);
        InputAction action;
        action.Name = actionName;
        action.Type = type;
        action.GamepadButtons = buttons;
        action.GamepadAxes = axes;
        s_Contexts[contextName].Actions[actionName] = action;
    }

    const InputContext* Input::FindContext(const std::string& name)
    {
        auto it = s_Contexts.find(name);
        return it != s_Contexts.end() ? &it->second : nullptr;
    }

    std::vector<std::string> Input::GetContextNames()
    {
        std::vector<std::string> names;
        for (auto& [name, ctx] : s_Contexts)
            names.push_back(name);
        std::sort(names.begin(), names.end());
        return names;
    }

    bool Input::GetActionButton(const std::string& actionName)
    {
        // Search down the priority stack from top to bottom
        for (auto it = s_ContextStack.rbegin(); it != s_ContextStack.rend(); ++it)
        {
            const std::string& contextName = *it;
            if (s_Contexts.find(contextName) == s_Contexts.end()) continue;
            if (!s_Contexts[contextName].Enabled) continue;
            
            auto& actions = s_Contexts[contextName].Actions;
            if (actions.find(actionName) == actions.end()) continue;
            
            auto& action = actions[actionName];
            if (action.Consumed) continue;
            
            bool isPressed = false;
            if (action.IsChord)
            {
                if (!action.Keys.empty()) {
                    bool allPressed = true;
                    for (auto key : action.Keys) {
                        if (!IsKeyPressed(key)) {
                            allPressed = false;
                            break;
                        }
                    }
                    isPressed = allPressed;
                }
            }
            else
            {
                for (auto key : action.Keys) {
                    if (IsKeyPressed(key)) isPressed = true;
                }
            }
            
            for (auto btn : action.MouseButtons) if (IsMouseButtonPressed(btn)) isPressed = true;
            for (auto btn : action.GamepadButtons) if (IsGamepadButtonPressed(btn, action.PlayerIndex)) isPressed = true;
            
            if (isPressed) {
                if (action.RequiredHoldTime > 0.0f) {
                    if (action.PressStartTime >= 0.0f && ((float)glfwGetTime() - action.PressStartTime) >= action.RequiredHoldTime) return true;
                } else {
                    return true;
                }
            }
        }
        
        return false;
    }

    InputTriggerState Input::GetActionState(const std::string& actionName)
    {
        if (s_ActiveContext.empty() || s_Contexts.find(s_ActiveContext) == s_Contexts.end()) return InputTriggerState::None;
        auto& actions = s_Contexts[s_ActiveContext].Actions;
        if (actions.find(actionName) == actions.end()) return InputTriggerState::None;
        auto& action = actions[actionName];
        if (action.Consumed) return InputTriggerState::None;

        bool isPressedNow = false, wasPressedBefore = false;

        if (action.IsChord) {
            bool allNow = true, allPrev = true;
            for (auto key : action.Keys) {
                if (!s_KeyStates[key]) allNow = false;
                if (!s_PreviousKeyStates[key]) allPrev = false;
            }
            isPressedNow = allNow && !action.Keys.empty();
            wasPressedBefore = allPrev && !action.Keys.empty();
        } else {
            for (auto key : action.Keys) {
                if (s_KeyStates[key]) isPressedNow = true;
                if (s_PreviousKeyStates[key]) wasPressedBefore = true;
            }
        }

        for (auto btn : action.MouseButtons) {
            if (s_MouseButtonStates[btn]) isPressedNow = true;
            if (s_PreviousMouseButtonStates[btn]) wasPressedBefore = true;
        }

        if (isPressedNow && !wasPressedBefore) return InputTriggerState::Started;
        if (isPressedNow && wasPressedBefore) return InputTriggerState::Ongoing;
        if (!isPressedNow && wasPressedBefore) return InputTriggerState::Completed;
        return InputTriggerState::None;
    }

    float Input::GetActionAxis1D(const std::string& actionName)
    {
        if (s_ActiveContext.empty() || s_Contexts.find(s_ActiveContext) == s_Contexts.end()) return 0.0f;
        auto& actions = s_Contexts[s_ActiveContext].Actions;
        if (actions.find(actionName) == actions.end()) return 0.0f;
        
        auto& action = actions[actionName];
        if (action.Consumed) return 0.0f;
        
        float val = 0.0f;
        for (auto axis : action.GamepadAxes)
        {
            float v = GetGamepadAxis(axis, action.PlayerIndex);
            if (std::abs(v) > std::abs(val)) val = v;
        }
        
        if (action.Keys.size() >= 2) {
            if (IsKeyPressed(action.Keys[0])) val += 1.0f;
            if (IsKeyPressed(action.Keys[1])) val -= 1.0f;
        }
        return glm::clamp(val, -1.0f, 1.0f) * action.Scale;
    }

    glm::vec2 Input::GetActionAxis2D(const std::string& actionName)
    {
        if (s_ActiveContext.empty() || s_Contexts.find(s_ActiveContext) == s_Contexts.end()) return glm::vec2(0.0f);
        auto& actions = s_Contexts[s_ActiveContext].Actions;
        if (actions.find(actionName) == actions.end()) return glm::vec2(0.0f);
        auto& action = actions[actionName];
        if (action.Consumed) return glm::vec2(0.0f);

        glm::vec2 val(0.0f);
        if (action.GamepadAxes.size() >= 2) {
            val.x = GetGamepadAxis(action.GamepadAxes[0], action.PlayerIndex);
            val.y = GetGamepadAxis(action.GamepadAxes[1], action.PlayerIndex);
        }
        if (action.Keys.size() >= 4) {
            if (IsKeyPressed(action.Keys[0])) val.y += 1.0f; // Up
            if (IsKeyPressed(action.Keys[1])) val.y -= 1.0f; // Down
            if (IsKeyPressed(action.Keys[2])) val.x -= 1.0f; // Left
            if (IsKeyPressed(action.Keys[3])) val.x += 1.0f; // Right
        }

        if (glm::length(val) > 1.0f) val = glm::normalize(val);
        return val * action.Scale;
    }

    void Input::ConsumeAction(const std::string& actionName)
    {
        if (s_ActiveContext.empty() || s_Contexts.find(s_ActiveContext) == s_Contexts.end()) return;
        auto& actions = s_Contexts[s_ActiveContext].Actions;
        if (actions.find(actionName) != actions.end()) actions[actionName].Consumed = true;
    }

    KeyCode Input::GetAnyKeyPressed()
    {
        // Simple mock for GLFW key loop (range 32 to 348 approx)
        for (int i = 32; i <= 348; i++)
        {
            if (IsKeyPressed((KeyCode)i)) return (KeyCode)i;
        }
        return (KeyCode)0;
    }

    std::string Input::GetKeyName(KeyCode key)
    {
        if ((int)key == 0) return "None";
        
        static const std::unordered_map<KeyCode, std::string> keyNameMap = {
            {Key::A, "A"}, {Key::B, "B"}, {Key::C, "C"}, {Key::D, "D"}, {Key::E, "E"}, {Key::F, "F"},
            {Key::G, "G"}, {Key::H, "H"}, {Key::I, "I"}, {Key::J, "J"}, {Key::K, "K"}, {Key::L, "L"},
            {Key::M, "M"}, {Key::N, "N"}, {Key::O, "O"}, {Key::P, "P"}, {Key::Q, "Q"}, {Key::R, "R"},
            {Key::S, "S"}, {Key::T, "T"}, {Key::U, "U"}, {Key::V, "V"}, {Key::W, "W"}, {Key::X, "X"},
            {Key::Y, "Y"}, {Key::Z, "Z"},
            
            {Key::D0, "0"}, {Key::D1, "1"}, {Key::D2, "2"}, {Key::D3, "3"}, {Key::D4, "4"},
            {Key::D5, "5"}, {Key::D6, "6"}, {Key::D7, "7"}, {Key::D8, "8"}, {Key::D9, "9"},
            
            {Key::LeftControl, "Ctrl"}, {Key::RightControl, "Ctrl"},
            {Key::LeftShift, "Shift"}, {Key::RightShift, "Shift"},
            {Key::LeftAlt, "Alt"}, {Key::RightAlt, "Alt"},
            
            {Key::Space, "Space"}, {Key::Enter, "Enter"}, {Key::Escape, "Escape"},
            {Key::Backspace, "Backspace"}, {Key::Tab, "Tab"},
            {Key::Up, "ArrowUp"}, {Key::Down, "ArrowDown"}, {Key::Left, "ArrowLeft"}, {Key::Right, "ArrowRight"}
        };
        
        auto it = keyNameMap.find(key);
        if (it != keyNameMap.end()) return it->second;
        
        return "Key_" + std::to_string((int)key);
    }

    void Input::EnableInputBuffer(bool enabled) { s_InputBufferEnabled = enabled; }
    void Input::SetInputBufferSize(size_t size) { s_InputBufferSize = size; }
    void Input::ClearInputBuffer() { while(!s_InputBuffer.empty()) s_InputBuffer.pop(); }
    
    std::vector<InputEvent> Input::GetInputBuffer()
    {
        std::vector<InputEvent> res;
        auto temp = s_InputBuffer;
        while (!temp.empty()) { res.push_back(temp.front()); temp.pop(); }
        return res;
    }

    void Input::StartRecording()
    {
        s_Recording = true;
        s_RecordedEvents.clear();
    }

    void Input::StopRecording() { s_Recording = false; }
    bool Input::IsRecording() { return s_Recording; }
    std::vector<InputEvent> Input::GetRecording() { return s_RecordedEvents; }

    void Input::StartReplay(const std::vector<InputEvent>& events)
    {
        s_Replaying = true;
        s_ReplayEvents = events;
        s_ReplayIndex = 0;
        s_ReplayStartTime = glfwGetTimerValue();
    }

    void Input::StopReplay() { s_Replaying = false; }
    bool Input::IsReplaying() { return s_Replaying; }

    void Input::RegisterCombo(const std::string& name, const std::vector<KeyCode>& keys, float timeWindow)
    {
        s_Combos[name] = { keys, timeWindow, std::vector<float>(keys.size(), 0.0f) };
    }

    bool Input::IsComboTriggered(const std::string& name)
    {
        if (s_Combos.find(name) == s_Combos.end()) return false;
        auto& combo = s_Combos[name];
        if (combo.Keys.empty()) return false;
        
        float currentTime = (float)glfwGetTime();
        if ((currentTime - combo.KeyPressTimes.back()) > combo.TimeWindow) return false;
        
        for (size_t i = 1; i < combo.Keys.size(); i++) {
            if (combo.KeyPressTimes[i] <= combo.KeyPressTimes[i-1]) return false;
            if ((combo.KeyPressTimes[i] - combo.KeyPressTimes[0]) > combo.TimeWindow) return false;
        }
        
        std::fill(combo.KeyPressTimes.begin(), combo.KeyPressTimes.end(), 0.0f);
        return true;
    }

    void Input::OnEvent(class Event& e) { }

    void Input::ProcessInputBuffer()
    {
        uint64_t currentTimestamp = (uint64_t)(glfwGetTime() * 1000.0);
        while (!s_InputBuffer.empty()) {
            if ((currentTimestamp - s_InputBuffer.front().Timestamp) > 2000) s_InputBuffer.pop();
            else break;
        }
        while (s_InputBuffer.size() > s_InputBufferSize) s_InputBuffer.pop();
    }

    void Input::ProcessReplay()
    {
        if (s_ReplayIndex >= s_ReplayEvents.size()) {
            StopReplay(); return;
        }
        
        uint64_t currentTimestamp = (uint64_t)(glfwGetTime() * 1000.0);
        uint64_t timeElapsed = currentTimestamp - s_ReplayStartTime;
        
        while (s_ReplayIndex < s_ReplayEvents.size())
        {
            auto& ev = s_ReplayEvents[s_ReplayIndex];
            uint64_t eventRelativeTime = ev.Timestamp - s_ReplayEvents[0].Timestamp;
            if (timeElapsed >= eventRelativeTime) {
                if (ev.EventType == InputEvent::Type::KeyPress) s_KeyStates[(KeyCode)ev.Code] = (ev.Value > 0.0f);
                else if (ev.EventType == InputEvent::Type::MouseMove) s_MousePosition = ev.Position;
                s_ReplayIndex++;
            }
            else break;
        }
    }

    void Input::CheckCombos()
    {
        float currentTime = (float)glfwGetTime();
        for (auto& kv : s_Combos) {
            auto& combo = kv.second;
            if (combo.Keys.empty()) continue;
            for (size_t i = 0; i < combo.Keys.size(); i++) {
                if (IsKeyDown(combo.Keys[i])) combo.KeyPressTimes[i] = currentTime;
            }
        }
    }

    bool Input::LoadConfig(const std::string& filepath)
    {
        CQ_CORE_INFO("Attempting to load Input Config from: {0}", filepath);

        std::ifstream file(filepath);
        if (!file.is_open()) 
        {
            CQ_CORE_ERROR("Failed to open Input Config file: {0}", filepath);
            return false;
        }
        std::stringstream text;
        text << file.rdbuf();
        LoadConfigFromString(text.str());
        CQ_CORE_INFO("Successfully loaded Input Config: {0}", filepath);
        return true;
    }

    void Input::ClearContexts()
    {
        s_Contexts.clear();
        s_ContextStack.clear();
        s_ActiveContext.clear();
    }

    InputContext* Input::GetContextMutable(const std::string& name)
    {
        auto it = s_Contexts.find(name);
        return it != s_Contexts.end() ? &it->second : nullptr;
    }

    std::string Input::SaveConfigToString()
    {
        std::ostringstream out;
        out << "# Conqueror's Engine - Input Configuration (Project Settings > Input)\n";
        std::vector<std::string> names;
        for (auto& [name, ctx] : s_Contexts)
            names.push_back(name);
        std::sort(names.begin(), names.end());
        for (const auto& name : names)
        {
            const auto& ctx = s_Contexts[name];
            if (name == "Default" && ctx.Actions.empty())
                continue;
            out << "Context:" << name << "\n";
            std::vector<std::string> actions;
            for (auto& [an, a] : ctx.Actions)
                actions.push_back(an);
            std::sort(actions.begin(), actions.end());
            for (const auto& an : actions)
            {
                const InputAction& a = ctx.Actions.at(an);
                if (a.IsChord && a.Keys.size() >= 2)
                    out << "BindChord:" << an << "," << a.Keys[0] << "," << a.Keys[1] << "\n";
                else
                    for (KeyCode k : a.Keys)
                        out << "BindAction:" << an << "," << k << "\n";
                for (MouseCode m : a.MouseButtons)
                    out << "BindMouseButton:" << an << "," << m << "\n";
                for (int b : a.GamepadButtons)
                    out << "BindGamepadButton:" << an << "," << b << "\n";
                for (int ax : a.GamepadAxes)
                    out << "BindGamepadAxis:" << an << "," << ax << "," << a.DeadZone << "\n";
                if (a.Keys.empty() && a.MouseButtons.empty() && a.GamepadButtons.empty() && a.GamepadAxes.empty())
                    out << "DeclareAction:" << an << "\n";
                if (a.Scale != 1.0f) out << "ActionScale:" << an << "," << a.Scale << "\n";
                if (a.PlayerIndex != 0) out << "ActionPlayerIndex:" << an << "," << a.PlayerIndex << "\n";
                if (a.RequiredHoldTime > 0.0f) out << "ActionHoldTime:" << an << "," << a.RequiredHoldTime << "\n";
            }
        }
        return out.str();
    }

    bool Input::LoadConfigFromString(const std::string& text)
    {
        std::istringstream file(text);
        std::string line;
        std::string currentContext = "Default";
        CreateContext(currentContext);

        while (std::getline(file, line))
        {
            if (line.empty() || line[0] == '#' || line[0] == ';') continue;
            
            std::istringstream iss(line);
            std::string command;
            if (std::getline(iss, command, ':'))
            {
                std::string value;
                std::getline(iss, value);
                
                if (command == "Context") {
                    currentContext = value;
                    CreateContext(currentContext);
                }
                else if (command == "BindAction") {
                    std::istringstream viss(value);
                    std::string actionName;
                    std::string keyStr;
                    if (std::getline(viss, actionName, ',') && std::getline(viss, keyStr)) {
                        int code = std::stoi(keyStr);
                        // Aynı eylemin birden çok satırı tuşları birleştirir (W ve Yukarı ok gibi)
                        auto& actions = s_Contexts[currentContext].Actions;
                        if (actions.count(actionName) && !actions[actionName].IsChord)
                            actions[actionName].Keys.push_back((KeyCode)code);
                        else
                            BindAction(currentContext, actionName, InputActionType::Button, { (KeyCode)code });
                    }
                }
                else if (command == "BindCombo") {
                    std::istringstream viss(value);
                    std::string comboName;
                    if (std::getline(viss, comboName, ',')) {
                        std::vector<KeyCode> keys;
                        std::string keyStr;
                        while(std::getline(viss, keyStr, ',')) {
                            keys.push_back((KeyCode)std::stoi(keyStr));
                        }
                        if (!keys.empty()) {
                            RegisterCombo(comboName, keys, 0.5f);
                        }
                    }
                }
                else if (command == "BindChord") {
                    std::istringstream viss(value);
                    std::string actionName, modStr, keyStr;
                    if (std::getline(viss, actionName, ',') && std::getline(viss, modStr, ',') && std::getline(viss, keyStr)) {
                        BindChord(currentContext, actionName, { (KeyCode)std::stoi(modStr) }, (KeyCode)std::stoi(keyStr));
                    }
                }
                else if (command == "BindMouseButton") {
                    std::istringstream viss(value);
                    std::string actionName, btnStr;
                    if (std::getline(viss, actionName, ',') && std::getline(viss, btnStr)) {
                        auto& actions = s_Contexts[currentContext].Actions;
                        if (actions.count(actionName))
                            actions[actionName].MouseButtons.push_back((MouseCode)std::stoi(btnStr));
                        else
                            BindMouseAction(currentContext, actionName, InputActionType::Button, { (MouseCode)std::stoi(btnStr) });
                    }
                }
                else if (command == "DeclareAction") {
                    if (!value.empty())
                        s_Contexts[currentContext].Actions[value].Name = value;
                }
                else if (command == "BindGamepadButton") {
                    std::istringstream viss(value);
                    std::string actionName, btnStr;
                    if (std::getline(viss, actionName, ',') && std::getline(viss, btnStr)) {
                        auto& actions = s_Contexts[currentContext].Actions;
                        if (actions.count(actionName))
                            actions[actionName].GamepadButtons.push_back(std::stoi(btnStr));
                        else
                            BindGamepadAction(currentContext, actionName, InputActionType::Button, { std::stoi(btnStr) }, {});
                    }
                }
                else if (command == "BindGamepadAxis") {
                    std::istringstream viss(value);
                    std::string actionName, axisStr, dzStr;
                    if (std::getline(viss, actionName, ',') && std::getline(viss, axisStr, ',') && std::getline(viss, dzStr)) {
                        auto& actions = s_Contexts[currentContext].Actions;
                        if (actions.count(actionName))
                            actions[actionName].GamepadAxes.push_back(std::stoi(axisStr));
                        else
                            BindGamepadAction(currentContext, actionName, InputActionType::Axis1D, {}, { std::stoi(axisStr) });
                        if (s_Contexts.find(currentContext) != s_Contexts.end()) {
                            s_Contexts[currentContext].Actions[actionName].DeadZone = std::stof(dzStr);
                        }
                    }
                }
                else if (command == "ActionScale") {
                    std::istringstream viss(value);
                    std::string actionName, scaleStr;
                    if (std::getline(viss, actionName, ',') && std::getline(viss, scaleStr)) {
                        if (s_Contexts.find(currentContext) != s_Contexts.end() && s_Contexts[currentContext].Actions.find(actionName) != s_Contexts[currentContext].Actions.end()) {
                            s_Contexts[currentContext].Actions[actionName].Scale = std::stof(scaleStr);
                        }
                    }
                }
                else if (command == "ActionPlayerIndex") {
                    std::istringstream viss(value);
                    std::string actionName, playerIdxStr;
                    if (std::getline(viss, actionName, ',') && std::getline(viss, playerIdxStr)) {
                        if (s_Contexts.find(currentContext) != s_Contexts.end() && s_Contexts[currentContext].Actions.find(actionName) != s_Contexts[currentContext].Actions.end()) {
                            s_Contexts[currentContext].Actions[actionName].PlayerIndex = std::stoi(playerIdxStr);
                        }
                    }
                }
                else if (command == "ActionHoldTime") {
                    std::istringstream viss(value);
                    std::string actionName, holdTimeStr;
                    if (std::getline(viss, actionName, ',') && std::getline(viss, holdTimeStr)) {
                        if (s_Contexts.find(currentContext) != s_Contexts.end() && s_Contexts[currentContext].Actions.find(actionName) != s_Contexts[currentContext].Actions.end()) {
                            s_Contexts[currentContext].Actions[actionName].RequiredHoldTime = std::stof(holdTimeStr);
                        }
                    }
                }
            }
        }
        return true;
    }
}
