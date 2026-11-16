#pragma once

#include "Scene/Entity.h"
#include "Scene/Scene.h"

#include <deque>
#include <functional>
#include <memory>
#include <string>

// UI Debugger (199, Unreal Widget Reflector / Unity UI Toolkit Debugger gibi): oyun oynarken (ve düzenlerken) canlı UI ağacı: her öğenin
// türü, hesaplanan dikdörtgeni, görünürlüğü, ışın hedefi, etkileşim durumu (üzerinde / basılı / seçili / etkileşimli), klavye-gamepad
// seçimi ve son UI olayları (tık, değer değişimi). Satıra tıklamak öğeyi Hierarchy / Inspector'da seçer; "Only hovered" fare altındaki
// zinciri gösterir.
namespace Conqueror::Editor
{
    class UIDebuggerPanel
    {
    public:
        void SetContext(const std::shared_ptr<Scene>& scene) { m_Context = scene; }
        void OnImGuiRender(bool* open);
        // Her karenin UI olayları (oyun sırasında GetEvents temizlenmeden önce)
        void CollectEvents();
        std::function<void(Entity)> OnSelect;
        size_t EventCount() const { return m_Events.size(); }
        const std::deque<std::string>& Events() const { return m_Events; }

    private:
        void DrawElement(Entity e, int depth);
        std::shared_ptr<Scene> m_Context;
        std::deque<std::string> m_Events;
        bool m_OnlyHovered = false;
        char m_Filter[64] = {};
    };
}
