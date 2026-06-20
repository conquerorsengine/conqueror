#include "Input.h"
#include "Core/Application.h"
#include "Core/Logging/Log.h"
#include <fstream>
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
    bool Input::IsGamepadConnected(int gamepadID)
    {
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

    bool Input::IsGamepadButtonPressed(int button, int gamepadID)
    {
        if (!IsGamepadConnected(gamepadID))
            return false;
            
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

    float Input::GetGamepadLeftTrigger(int gamepadID)
    {
        return GetGamepadAxis(4, gamepadID);
    }

    float Input::GetGamepadRightTrigger(int gamepadID)
    {
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

    void Input::SetCursorMode(int mode)
    {
        auto* window = static_cast<GLFWwindow*>(Application::Get().GetWindow().GetNativeWindow());
        glfwSetInputMode(window, GLFW_CURSOR, mode);
    }

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
        s_MouseScrollDelta = 0.0f;
        
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
                else if (command == "BindGamepadButton") {
                    std::istringstream viss(value);
                    std::string actionName, btnStr;
                    if (std::getline(viss, actionName, ',') && std::getline(viss, btnStr)) {
                        BindGamepadAction(currentContext, actionName, InputActionType::Button, { std::stoi(btnStr) }, {});
                    }
                }
                else if (command == "BindGamepadAxis") {
                    std::istringstream viss(value);
                    std::string actionName, axisStr, dzStr;
                    if (std::getline(viss, actionName, ',') && std::getline(viss, axisStr, ',') && std::getline(viss, dzStr)) {
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
        CQ_CORE_INFO("Successfully loaded Input Config: {0}", filepath);
        return true;
    }
}
