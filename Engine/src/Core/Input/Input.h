#pragma once

#include "Core/Base/Base.h"
#include "Core/KeyCodes.h"
#include "Core/MouseCodes.h"

#include <glm/glm.hpp>
#include <functional>
#include <unordered_map>
#include <vector>
#include <string>
#include <queue>

namespace Conqueror
{
    // Input action types
    enum class InputActionType
    {
        Button,  // Press/Release
        Axis1D,  // Single axis (-1 to 1)
        Axis2D   // Two axes (vec2)
    };

    // Input trigger state
    enum class InputTriggerState
    {
        None,
        Started,   // Just pressed
        Ongoing,   // Held down
        Completed, // Just released
        Canceled   // Interrupted
    };

    // Gamepad info
    struct GamepadInfo
    {
        int ID;
        std::string Name;
        bool Connected;
        int ButtonCount;
        int AxisCount;
    };

    // Input action definition
    struct InputAction
    {
        std::string Name;
        InputActionType Type;
        std::vector<KeyCode> Keys;
        std::vector<MouseCode> MouseButtons;
        std::vector<int> GamepadButtons;
        std::vector<int> GamepadAxes;
        float DeadZone = 0.2f;
        bool Consumed = false;
        bool IsChord = false;

        float Scale = 1.0f;
        int PlayerIndex = 0;
        float RequiredHoldTime = 0.0f;
        float PressStartTime = -1.0f;
    };

    // Input context (for switching input schemes)
    struct InputContext
    {
        std::string Name;
        std::unordered_map<std::string, InputAction> Actions;
        int Priority = 0;
        bool Enabled = true;
    };

    // Input event for recording/replay
    struct InputEvent
    {
        enum class Type { KeyPress, KeyRelease, MouseMove, MouseButton, GamepadButton, GamepadAxis };
        
        Type EventType;
        uint64_t Timestamp;
        int Code;
        float Value;
        glm::vec2 Position;
    };

    class CQ_API Input
    {
    public:
        // Basic input (legacy)
        static bool IsKeyPressed(KeyCode key);
        static bool IsKeyDown(KeyCode key);
        static bool IsKeyUp(KeyCode key);
        
        static bool IsMouseButtonPressed(MouseCode button);
        static bool IsMouseButtonDown(MouseCode button);
        static bool IsMouseButtonUp(MouseCode button);
        
        static glm::vec2 GetMousePosition();
        static glm::vec2 GetMouseDelta();
        static float GetMouseX();
        static float GetMouseY();
        static float GetMouseScrollDelta();
        
        // Gamepad support
        static bool IsGamepadConnected(int gamepadID = 0);
        static GamepadInfo GetGamepadInfo(int gamepadID = 0);
        static int GetConnectedGamepadCount();
        
        static bool IsGamepadButtonPressed(int button, int gamepadID = 0);
        static bool IsGamepadButtonDown(int button, int gamepadID = 0);
        static bool IsGamepadButtonUp(int button, int gamepadID = 0);
        
        static float GetGamepadAxis(int axis, int gamepadID = 0);
        static glm::vec2 GetGamepadLeftStick(int gamepadID = 0);
        static glm::vec2 GetGamepadRightStick(int gamepadID = 0);
        static float GetGamepadLeftTrigger(int gamepadID = 0);
        static float GetGamepadRightTrigger(int gamepadID = 0);
        
        // Vibration/Haptic feedback
        static void SetGamepadVibration(float leftMotor, float rightMotor, int gamepadID = 0);
        static void StopGamepadVibration(int gamepadID = 0);
        
        // Input action system
        static void CreateContext(const std::string& name, int priority = 0);
        static void RemoveContext(const std::string& name);
        static void SetContextEnabled(const std::string& name, bool enabled);
        
        // Context Stacking (Advanced Input Routing)
        static void PushContext(const std::string& name);
        static void PopContext();
        static void SwitchContext(const std::string& name); // Clears stack, pushes one
        
        static void BindAction(const std::string& contextName, const std::string& actionName, 
                               InputActionType type, const std::vector<KeyCode>& keys);
        static void BindChord(const std::string& contextName, const std::string& actionName, 
                              const std::vector<KeyCode>& modifiers, KeyCode key);
        static void BindMouseAction(const std::string& contextName, const std::string& actionName, 
                               InputActionType type, const std::vector<MouseCode>& mouseButtons);
        static void BindGamepadAction(const std::string& contextName, const std::string& actionName, 
                                      InputActionType type, const std::vector<int>& buttons, const std::vector<int>& axes);
        
        static bool GetActionButton(const std::string& actionName);
        static InputTriggerState GetActionState(const std::string& actionName);
        static float GetActionAxis1D(const std::string& actionName);
        static glm::vec2 GetActionAxis2D(const std::string& actionName);
        
        static void ConsumeAction(const std::string& actionName);
        
        // Input Map File Parsing
        static bool LoadConfig(const std::string& filepath);

        // UI Helpers
        static KeyCode GetAnyKeyPressed();
        static std::string GetKeyName(KeyCode key);
        
        // Input buffering (for fighting games, etc.)
        static void EnableInputBuffer(bool enabled);
        static void SetInputBufferSize(size_t size);
        static void ClearInputBuffer();
        static std::vector<InputEvent> GetInputBuffer();
        
        // Input recording/replay
        static void StartRecording();
        static void StopRecording();
        static bool IsRecording();
        static std::vector<InputEvent> GetRecording();
        
        static void StartReplay(const std::vector<InputEvent>& events);
        static void StopReplay();
        static bool IsReplaying();
        
        // Key combo detection
        static void RegisterCombo(const std::string& name, const std::vector<KeyCode>& keys, float timeWindow = 0.5f);
        static bool IsComboTriggered(const std::string& name);
        
        // Dead zone configuration
        static void SetDeadZone(float deadZone);
        static float GetDeadZone();
        
        // Cursor control
        static void SetCursorMode(int mode); // GLFW_CURSOR_NORMAL, GLFW_CURSOR_HIDDEN, GLFW_CURSOR_DISABLED
        static void SetCursorPosition(float x, float y);
        
        // Internal update (called by Application)
        static void Update();
        static void OnEvent(class Event& e);
        
    private:
        // State tracking
        static std::unordered_map<KeyCode, bool> s_KeyStates;
        static std::unordered_map<KeyCode, bool> s_PreviousKeyStates;
        static std::unordered_map<MouseCode, bool> s_MouseButtonStates;
        static std::unordered_map<MouseCode, bool> s_PreviousMouseButtonStates;
        
        static glm::vec2 s_MousePosition;
        static glm::vec2 s_PreviousMousePosition;
        static float s_MouseScrollDelta;
        
        // Gamepad state
        struct GamepadState
        {
            bool Connected;
            std::string Name;
            std::vector<bool> ButtonStates;
            std::vector<bool> PreviousButtonStates;
            std::vector<float> AxisStates;
        };
        static std::unordered_map<int, GamepadState> s_GamepadStates;
        
        // Input contexts
        static std::unordered_map<std::string, InputContext> s_Contexts;
        static std::string s_ActiveContext;
        static std::vector<std::string> s_ContextStack;
        
        // Input buffer
        static bool s_InputBufferEnabled;
        static size_t s_InputBufferSize;
        static std::queue<InputEvent> s_InputBuffer;
        
        // Recording/Replay
        static bool s_Recording;
        static bool s_Replaying;
        static std::vector<InputEvent> s_RecordedEvents;
        static std::vector<InputEvent> s_ReplayEvents;
        static size_t s_ReplayIndex;
        static uint64_t s_ReplayStartTime;
        
        // Combo detection
        struct ComboState
        {
            std::vector<KeyCode> Keys;
            float TimeWindow;
            std::vector<float> KeyPressTimes;
        };
        static std::unordered_map<std::string, ComboState> s_Combos;
        
        static float s_DeadZone;
        
        // Helper functions
        static void UpdateGamepads();
        static void ProcessInputBuffer();
        static void ProcessReplay();
        static void CheckCombos();
        static float ApplyDeadZone(float value, float deadZone);
    };
}
