#pragma once

#include "Core/Events/Event.h"
#include "Core/Time/Timestep.h"
#include "Renderer/RHI/Framebuffer.h"

#include <glm/glm.hpp>
#include <memory>

namespace Conqueror
{
    class Scene;
}

namespace Conqueror::Editor
{
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
        std::shared_ptr<Framebuffer> GetFramebuffer() const { return m_Framebuffer; }

    private:
        std::shared_ptr<Scene> m_Context;
        std::shared_ptr<Framebuffer> m_Framebuffer;
        glm::vec2 m_ViewportSize = { 0.0f, 0.0f };
        glm::vec2 m_ViewportBounds[2];

        bool m_ViewportFocused = false;
        bool m_ViewportHovered = false;
    };
}
