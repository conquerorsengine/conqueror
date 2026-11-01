#include "GameInput.h"
#include "Input.h"
#include "Core/Automation/Automation.h"

#include <imgui.h>

#include <utility>

namespace Conqueror
{
    namespace
    {
        // ImGuiKey -> GLFW tuş kodu (KeyCode)
        const std::pair<ImGuiKey, int> kKeys[] = {
            { ImGuiKey_Space, 32 }, { ImGuiKey_Apostrophe, 39 }, { ImGuiKey_Comma, 44 }, { ImGuiKey_Minus, 45 }, { ImGuiKey_Period, 46 },
            { ImGuiKey_Slash, 47 }, { ImGuiKey_Semicolon, 59 }, { ImGuiKey_Equal, 61 }, { ImGuiKey_LeftBracket, 91 }, { ImGuiKey_Backslash, 92 },
            { ImGuiKey_RightBracket, 93 }, { ImGuiKey_GraveAccent, 96 }, { ImGuiKey_Escape, 256 }, { ImGuiKey_Enter, 257 }, { ImGuiKey_Tab, 258 },
            { ImGuiKey_Backspace, 259 }, { ImGuiKey_Insert, 260 }, { ImGuiKey_Delete, 261 }, { ImGuiKey_RightArrow, 262 }, { ImGuiKey_LeftArrow, 263 },
            { ImGuiKey_DownArrow, 264 }, { ImGuiKey_UpArrow, 265 }, { ImGuiKey_PageUp, 266 }, { ImGuiKey_PageDown, 267 }, { ImGuiKey_Home, 268 },
            { ImGuiKey_End, 269 }, { ImGuiKey_CapsLock, 280 }, { ImGuiKey_ScrollLock, 281 }, { ImGuiKey_NumLock, 282 }, { ImGuiKey_PrintScreen, 283 },
            { ImGuiKey_Pause, 284 }, { ImGuiKey_Keypad0, 320 }, { ImGuiKey_Keypad1, 321 }, { ImGuiKey_Keypad2, 322 }, { ImGuiKey_Keypad3, 323 },
            { ImGuiKey_Keypad4, 324 }, { ImGuiKey_Keypad5, 325 }, { ImGuiKey_Keypad6, 326 }, { ImGuiKey_Keypad7, 327 }, { ImGuiKey_Keypad8, 328 },
            { ImGuiKey_Keypad9, 329 }, { ImGuiKey_KeypadDecimal, 330 }, { ImGuiKey_KeypadDivide, 331 }, { ImGuiKey_KeypadMultiply, 332 },
            { ImGuiKey_KeypadSubtract, 333 }, { ImGuiKey_KeypadAdd, 334 }, { ImGuiKey_KeypadEnter, 335 }, { ImGuiKey_KeypadEqual, 336 },
            { ImGuiKey_LeftShift, 340 }, { ImGuiKey_LeftCtrl, 341 }, { ImGuiKey_LeftAlt, 342 }, { ImGuiKey_LeftSuper, 343 },
            { ImGuiKey_RightShift, 344 }, { ImGuiKey_RightCtrl, 345 }, { ImGuiKey_RightAlt, 346 }, { ImGuiKey_RightSuper, 347 }, { ImGuiKey_Menu, 348 },
        };
    }

    int GameInput::KeyCodeFromImGuiKey(int key)
    {
        if (key >= ImGuiKey_A && key <= ImGuiKey_Z) return 65 + (key - ImGuiKey_A);
        if (key >= ImGuiKey_0 && key <= ImGuiKey_9) return 48 + (key - ImGuiKey_0);
        if (key >= ImGuiKey_F1 && key <= ImGuiKey_F12) return 290 + (key - ImGuiKey_F1);
        for (auto& [imguiKey, glfwKey] : kKeys)
            if (imguiKey == key) return glfwKey;
        return -1;
    }

    namespace
    {
        float s_ImGuiWheel = 0.0f;   // ImGui karesi içinde biriken tekerlek (EndFrame sıfırlar, Feed karenin dışında çalışır)
    }

    void GameInput::CaptureImGuiFrameInput()
    {
        if (ImGui::GetCurrentContext())
            s_ImGuiWheel += ImGui::GetIO().MouseWheel;
    }

    void GameInput::FeedFromImGui(bool keyboardFocus, bool mouseHover)
    {
        if (!ImGui::GetCurrentContext())
            return;
        ImGuiIO& io = ImGui::GetIO();
        Frame f;
        // Geçersiz konum (-FLT_MAX: imleç pencereden çıktı / kilit geçişi) son geçerli konumla değişir; yoksa tek karelik
        // devasa fare farkı kameranın açısını sonsuza fırlatır (sonra yatay bakış hiç çalışmaz)
        static ImVec2 s_LastValid(0.0f, 0.0f);
        static bool s_WasInvalid = false;
        // İmleç oyuna kilitliyken konum doğrudan GLFW'den (sınırsız sanal imleç): ImGui'nin konumu kilitte geçersiz olabilir
        // ya da pencere / görünüm sınırına takılır (fare donar). Otomasyon fareyi ImGui üzerinden sürer, o zaman ImGui kullanılır.
        if (Input::GetAppliedCursorMode() == 2 && !Automation::IsActive())
        {
            const glm::vec2 p = Input::GetMousePosition();
            s_LastValid = ImVec2(p.x, p.y);
            s_WasInvalid = false;
        }
        else if (ImGui::IsMousePosValid(&io.MousePos))
        {
            // Geçersizlikten dönünce konum başka yerde olabilir: o karenin farkı atılır
            if (s_WasInvalid)
                SuppressMouseDelta(1);
            s_WasInvalid = false;
            s_LastValid = io.MousePos;
        }
        else
            s_WasInvalid = true;
        f.MousePosition = { s_LastValid.x, s_LastValid.y };
        f.KeyboardFocus = keyboardFocus && !io.WantTextInput;
        f.MouseHover = mouseHover;
        for (int b = 0; b < 5 && b < ButtonCount; ++b)
            f.Buttons[b] = io.MouseDown[b];
        f.Scroll = s_ImGuiWheel;
        s_ImGuiWheel = 0.0f;
        // Kilitli imleçte tekerlek de doğrudan pencereden (ImGui fare kapalıyken tekerleği bildirmeyebilir)
        if (Input::GetAppliedCursorMode() == 2 && !Automation::IsActive())
            f.Scroll = Input::GetMouseScrollDelta();
        for (int k = 0; k < 26; ++k)
            f.Keys[65 + k] = ImGui::IsKeyDown((ImGuiKey)(ImGuiKey_A + k));
        for (int k = 0; k < 10; ++k)
            f.Keys[48 + k] = ImGui::IsKeyDown((ImGuiKey)(ImGuiKey_0 + k));
        for (int k = 0; k < 12; ++k)
            f.Keys[290 + k] = ImGui::IsKeyDown((ImGuiKey)(ImGuiKey_F1 + k));
        for (auto& [imguiKey, glfwKey] : kKeys)
            f.Keys[glfwKey] = ImGui::IsKeyDown(imguiKey);
        Feed(f);
    }
}
