#pragma once

#include "Core/Events/Event.h"
#include "Core/Time/Timestep.h"
#include "Renderer/RHI/Framebuffer.h"
#include "Renderer/PostProcess.h"
#include "UI/UISystem.h"

#include <glm/glm.hpp>
#include <memory>
#include <algorithm>

namespace Conqueror
{
    class Scene;
}

namespace Conqueror::Editor
{
    class StatsPanel;

    // Game panel - oyun runtime görünümü (editör araçları yok)
    class GamePanel
    {
    public:
        GamePanel();
        ~GamePanel() = default;

        void SetContext(const std::shared_ptr<Scene>& scene) { m_Context = scene; }

        void OnUpdate(Timestep ts, bool isPlaying);
        void OnImGuiRender();
        void OnEvent(Event& e);

        glm::vec2 GetViewportSize() const { return m_ViewportSize; }
        void RequestScreenshot() { m_ScreenshotRequested = true; }
        std::shared_ptr<Framebuffer> GetFramebuffer() const { return m_Framebuffer; }
        bool HasCamera() const { return m_HasCamera; }
        void SetStatsPanel(StatsPanel* stats) { m_StatsPanel = stats; }
        // Oynatma sırasında "Maximize On Play" için
        void SetPlaying(bool playing) { m_Playing = playing; }
        // Pencere kapalı (OnImGuiRender çağrılmıyor): düzenleme modunda çizilmez
        void MarkHidden() { m_TabVisible = false; }
        bool IsMaximized() const;
        bool IsViewportFocused() const { return m_ViewportFocused; }
        bool IsViewportHovered() const { return m_ViewportHovered; }
        // Oyun görüntüsünün ortası (ImGui ekran koordinatı)
        glm::vec2 GetViewportCenter() const { return (m_ViewportBounds[0] + m_ViewportBounds[1]) * 0.5f; }
        // Oyun render pikseli (sol-alt köken) -> ImGui ekran koordinatı
        glm::vec2 RenderToScreen(const glm::vec2& renderPx) const
        {
            glm::vec2 size = m_ViewportBounds[1] - m_ViewportBounds[0];
            return { m_ViewportBounds[0].x + renderPx.x / std::max(m_ViewportSize.x, 1.0f) * size.x,
                     m_ViewportBounds[1].y - renderPx.y / std::max(m_ViewportSize.y, 1.0f) * size.y };
        }

    private:
        std::shared_ptr<Scene> m_Context;
        std::shared_ptr<Framebuffer> m_Framebuffer;
        glm::vec2 m_ViewportSize = { 0.0f, 0.0f };
        glm::vec2 m_ViewportBounds[2]{};

        bool m_ViewportFocused = false;
        bool m_ViewportHovered = false;
        bool m_HasCamera = true;   // yoksa görünümde "kamera yok" uyarısı
        bool m_HasOverlayUI = false;

        PostProcessStack m_PostStack;
        bool m_ScreenshotRequested = false;

        void DrawToolbar();

        StatsPanel* m_StatsPanel = nullptr;
        bool m_StatsExpanded = true;
        bool m_Playing = false;
        // Son ImGui karesinde sekme görünür müydü: düzenleme modunda gizli Game sekmesi çizilmez (Unity gibi; sahne
        // görünümüyle aynı sekme grubundayken editörün GPU yükü iki katına çıkıyordu). Oyunda güncelleme sürer.
        bool m_TabVisible = true;
        bool m_WasMuted = false;
        float m_SavedVolume = 1.0f;
        UIInputState m_UIInput;          // ImGui karesinde toplanır, sonraki OnUpdate'te tüketilir
    };
}
