#pragma once

#include "Core/Base/Base.h"
#include "Core/KeyCodes.h"
#include "Core/MouseCodes.h"

#include <glm/glm.hpp>

namespace Conqueror
{
    // Oyun betiklerinin gördüğü girdi. Editörde her kare beslenir (Feed): klavye/fare yalnızca oyun
    // görünümü odaktayken / fare üzerindeyken oyuna gider (Unity gibi), konumlar viewport sınırlarıyla
    // aynı ekran uzayındadır ve otomasyonla enjekte edilen girdi de görünür. Beslenmezse (bağımsız oyuncu)
    // doğrudan Input (GLFW) kullanılır.
    class CQ_API GameInput
    {
    public:
        static constexpr int KeyCount = 512;
        static constexpr int ButtonCount = 8;

        struct Frame
        {
            bool Keys[KeyCount] = {};          // GLFW tuş kodu dizinli
            bool Buttons[ButtonCount] = {};
            glm::vec2 MousePosition{ 0.0f };   // ekran uzayı (viewport sınırlarıyla aynı)
            float Scroll = 0.0f;
            bool KeyboardFocus = false;        // tuşlar oyuna gidiyor mu
            bool MouseHover = false;           // yeni tıklamalar oyuna gidiyor mu
        };

        // Editör: kare başına bir kez (sahne güncellemesinden önce)
        static void Feed(const Frame& frame);
        // ImGui girdisinden besle (editör oyun görünümü, derlenmiş oyun; otomasyon girdisi de böyle ulaşır)
        static void FeedFromImGui(bool keyboardFocus, bool mouseHover);
        // Beslemeyi bırak (bağımsız oyuncu davranışına dön)
        static void StopFeeding();
        // ImGui karesi sırasında (NewFrame - EndFrame arası) çağrılır: tek karelik girdileri (tekerlek) saklar
        static void CaptureImGuiFrameInput();
        // Sonraki karelerde fare farkı sıfır (konum sıçraması oyuna gitmesin)
        static void SuppressMouseDelta(int frames = 1);
        // Şu an basılı fare tuşları bırakılana kadar oyuna gitmez (imleci geri kilitleyen tık silahı ateşlemesin)
        static void ConsumeHeldButtons();
        static bool IsFed();

        // ImGuiKey -> GLFW tuş kodu (-1: eşleşme yok); tuş atama arayüzleri için
        static int KeyCodeFromImGuiKey(int imguiKey);
        static bool IsKeyHeld(KeyCode key);
        static bool IsKeyDown(KeyCode key);       // bu karede basıldı
        static bool IsKeyUp(KeyCode key);         // bu karede bırakıldı
        static bool IsMouseHeld(MouseCode button);
        static bool IsMouseDown(MouseCode button);
        static bool IsMouseUp(MouseCode button);
        static glm::vec2 GetMousePosition();
        static glm::vec2 GetMouseDelta();
        static float GetScroll();
    };
}
