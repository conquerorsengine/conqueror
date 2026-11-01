#include "GameInput.h"
#include "Input.h"

#include <algorithm>
#include <cmath>

namespace Conqueror
{
    namespace
    {
        struct State
        {
            bool Fed = false;
            GameInput::Frame Current;
            GameInput::Frame Previous;
        };

        State& S()
        {
            static State s;
            return s;
        }

        int s_Quiet = 0;    // fare farkı bastırılacak kare sayısı (imleç kilidi geçişi, geçersiz konumdan dönüş)
        int s_ConsumeFrames = 0;                              // ConsumeHeldButtons: bu kadar kare içinde basılı görülenler
                                                              // (tık GLFW'de ImGui'den bir kare önce görülebilir)
        bool s_Consumed[GameInput::ButtonCount] = {};         // bırakılana kadar oyuna gitmeyen tuşlar

        bool ValidKey(KeyCode key) { return (int)key >= 0 && (int)key < GameInput::KeyCount; }
        bool ValidButton(MouseCode b) { return (int)b >= 0 && (int)b < GameInput::ButtonCount; }
    }

    void GameInput::Feed(const Frame& frame)
    {
        auto& s = S();
        s.Previous = s.Fed ? s.Current : frame;
        s.Current = frame;
        // İmleç kilitlendi / bırakıldı: bu karede fare hareketi yok
        // (GLFW kilitten sonraki ilk olayda imleci merkeze alabilir: iki kare)
        static uint32_t s_Epoch = 0;
        if (Input::GetCursorModeEpoch() != s_Epoch)
        {
            s_Epoch = Input::GetCursorModeEpoch();
            s_Quiet = 2;
        }
        if (s_Quiet > 0)
        {
            --s_Quiet;
            s.Previous.MousePosition = frame.MousePosition;
        }
        // Odak yoksa tuşlar oyuna gitmez; fare yalnızca görünüm üzerindeyken başlayan basışları alır
        if (!frame.KeyboardFocus)
            for (bool& k : s.Current.Keys) k = false;
        for (int b = 0; b < ButtonCount; ++b)
        {
            const bool startedOutside = frame.Buttons[b] && !s.Previous.Buttons[b] && !frame.MouseHover;
            if (startedOutside)
                s.Current.Buttons[b] = false;
        }
        if (!frame.MouseHover)
            s.Current.Scroll = 0.0f;
        // Geri kilitleyen tık (ve onunla basılı tutulanlar) bırakılana kadar oyuna gitmez
        for (int b = 0; b < ButtonCount; ++b)
        {
            if (s_ConsumeFrames > 0 && frame.Buttons[b])
                s_Consumed[b] = true;
            else if (!frame.Buttons[b])
                s_Consumed[b] = false;
            if (s_Consumed[b])
                s.Current.Buttons[b] = false;
        }
        if (s_ConsumeFrames > 0)
            --s_ConsumeFrames;
        // Oyunun istediği imleç kilidi kullanıcı tarafından bırakıldı (Esc): oyun fareyi görmez; serbest imleç kamerayı
        // döndürmez, editörde yapılan tıklamalar ateş etmez
        if (Input::IsCursorReleased())
        {
            s.Previous.MousePosition = frame.MousePosition;
            for (bool& b : s.Current.Buttons) b = false;
            s.Current.Scroll = 0.0f;
        }
        s.Fed = true;
    }

    void GameInput::ConsumeHeldButtons()
    {
        s_ConsumeFrames = 4;
    }

    void GameInput::SuppressMouseDelta(int frames)
    {
        s_Quiet = std::max(s_Quiet, frames);
    }

    void GameInput::StopFeeding()
    {
        S().Fed = false;
    }

    bool GameInput::IsFed()
    {
        return S().Fed;
    }

    bool GameInput::IsKeyHeld(KeyCode key)
    {
        auto& s = S();
        if (!s.Fed) return Input::IsKeyPressed(key);
        return ValidKey(key) && s.Current.Keys[(int)key];
    }

    bool GameInput::IsKeyDown(KeyCode key)
    {
        auto& s = S();
        if (!s.Fed) return Input::IsKeyDown(key);
        return ValidKey(key) && s.Current.Keys[(int)key] && !s.Previous.Keys[(int)key];
    }

    bool GameInput::IsKeyUp(KeyCode key)
    {
        auto& s = S();
        if (!s.Fed) return Input::IsKeyUp(key);
        return ValidKey(key) && !s.Current.Keys[(int)key] && s.Previous.Keys[(int)key];
    }

    bool GameInput::IsMouseHeld(MouseCode button)
    {
        auto& s = S();
        if (!s.Fed) return Input::IsMouseButtonPressed(button);
        return ValidButton(button) && s.Current.Buttons[(int)button];
    }

    bool GameInput::IsMouseDown(MouseCode button)
    {
        auto& s = S();
        if (!s.Fed) return Input::IsMouseButtonDown(button);
        return ValidButton(button) && s.Current.Buttons[(int)button] && !s.Previous.Buttons[(int)button];
    }

    bool GameInput::IsMouseUp(MouseCode button)
    {
        auto& s = S();
        if (!s.Fed) return Input::IsMouseButtonUp(button);
        return ValidButton(button) && !s.Current.Buttons[(int)button] && s.Previous.Buttons[(int)button];
    }

    glm::vec2 GameInput::GetMousePosition()
    {
        auto& s = S();
        return s.Fed ? s.Current.MousePosition : Input::GetMousePosition();
    }

    glm::vec2 GameInput::GetMouseDelta()
    {
        auto& s = S();
        glm::vec2 d = s.Fed ? s.Current.MousePosition - s.Previous.MousePosition : Input::GetMouseDelta();
        // Sonsuz / anlamsız fark (geçersiz imleç konumu) oyuna hiç gitmez
        if (!std::isfinite(d.x) || !std::isfinite(d.y) || std::fabs(d.x) > 1.0e5f || std::fabs(d.y) > 1.0e5f)
            return glm::vec2(0.0f);
        return d;
    }

    float GameInput::GetScroll()
    {
        auto& s = S();
        return s.Fed ? s.Current.Scroll : Input::GetMouseScrollDelta();
    }
}
