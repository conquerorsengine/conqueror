#include "GameViewModes.h"

#include "Core/Input/GameInput.h"
#include "Core/Logging/Log.h"
#include "Renderer/Utilities/Renderer3D/Renderer3D.h"

#include <chrono>

namespace Conqueror::GameViewModes
{
    namespace
    {
        Mode s_Mode = Mode::Lit;
        std::chrono::steady_clock::time_point s_Changed{};
    }

    Mode Current() { return s_Mode; }

    void Set(Mode mode)
    {
        if (mode == s_Mode)
            return;
        s_Mode = mode;
        s_Changed = std::chrono::steady_clock::now();
        CQ_CORE_INFO("[Game] View mode: {0}", Name(mode));
    }

    const char* Name(Mode mode)
    {
        switch (mode)
        {
            case Mode::Wireframe: return "Wireframe";
            case Mode::Unlit: return "Unlit";
            case Mode::LightingOnly: return "Lighting Only";
            default: return "Lit";
        }
    }

    bool HandleHotkeys()
    {
        // Oyunun gördüğü girdi (editörde yalnız Game görünümü odaktayken, derlenmiş oyunda pencereden)
        if (GameInput::IsKeyHeld(Key::LeftShift) || GameInput::IsKeyHeld(Key::RightShift) || GameInput::IsKeyHeld(Key::LeftControl)
            || GameInput::IsKeyHeld(Key::RightControl) || GameInput::IsKeyHeld(Key::LeftAlt) || GameInput::IsKeyHeld(Key::RightAlt))
            return false;   // Shift+F1 fareyi bırakır; değiştirici tuşlu kısayollar oyunundur
        const Mode before = s_Mode;
        if (GameInput::IsKeyDown(Key::F1)) Set(Mode::Wireframe);
        else if (GameInput::IsKeyDown(Key::F2)) Set(Mode::Unlit);
        else if (GameInput::IsKeyDown(Key::F3)) Set(Mode::Lit);
        else if (GameInput::IsKeyDown(Key::F4)) Set(Mode::LightingOnly);
        return s_Mode != before;
    }

    void BeginScene()
    {
        Renderer3D::DebugView view = Renderer3D::DebugView::Shaded;
        if (s_Mode == Mode::Unlit) view = Renderer3D::DebugView::Unlit;
        else if (s_Mode == Mode::LightingOnly) view = Renderer3D::DebugView::LightingOnly;
        Renderer3D::SetDebugView(view);
        Renderer3D::SetWireframe(s_Mode == Mode::Wireframe);
    }

    void EndScene()
    {
        if (s_Mode == Mode::Lit)
            return;
        Renderer3D::SetDebugView(Renderer3D::DebugView::Shaded);
        Renderer3D::SetWireframe(false);
    }

    bool PostEffectsAllowed() { return s_Mode == Mode::Lit || s_Mode == Mode::Wireframe; }

    std::string Banner()
    {
        if (s_Changed == std::chrono::steady_clock::time_point{})
            return {};
        const float age = std::chrono::duration<float>(std::chrono::steady_clock::now() - s_Changed).count();
        if (age > 2.0f)
            return {};
        static const char* keys[] = { "F3", "F1", "F2", "F4" };
        return std::string("View Mode: ") + Name(s_Mode) + " (" + keys[(int)s_Mode] + ")";
    }
}
