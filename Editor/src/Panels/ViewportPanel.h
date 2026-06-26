#pragma once

#include "Core/Events/Event.h"
#include "Core/Time/Timestep.h"
#include "Renderer/RHI/Framebuffer.h"
#include "Scene/EditorCamera.h"
#include "Scene/Entity.h"

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <memory>

namespace Conqueror
{
    class Scene;
}

namespace Conqueror
{
    struct CanvasComponent;
    struct TransformComponent;
}

namespace Conqueror::Editor
{
    class ViewportPanel
    {
    public:
        ViewportPanel();
        ~ViewportPanel() = default;

        void SetContext(const std::shared_ptr<Scene>& scene) { m_Context = scene; }
        void SetSelectedEntity(Entity entity) { m_SelectedEntity = entity; }

        void OnUpdate(Timestep ts);
        void OnImGuiRender();
        void OnEvent(Event& e);

        glm::vec2 GetViewportSize() const { return m_ViewportSize; }
        const glm::vec2* GetViewportBounds() const { return m_ViewportBounds; }
        bool IsViewportHovered() const { return m_ViewportHovered; }
        std::shared_ptr<Framebuffer> GetFramebuffer() const { return m_Framebuffer; }
        EditorCamera& GetEditorCamera() { return m_EditorCamera; }

        int GetGizmoType() const { return m_GizmoType; }
        void SetGizmoType(int type) { m_GizmoType = type; }

    private:
        std::shared_ptr<Conqueror::Scene> m_Context;
        std::shared_ptr<Framebuffer> m_Framebuffer;
        glm::vec2 m_ViewportSize = { 0.0f, 0.0f };
        glm::vec2 m_ViewportBounds[2];

        EditorCamera m_EditorCamera;
        bool m_ViewportFocused = false;
        bool m_ViewportHovered = false;

        // Gizmo
        int m_GizmoType = 7; // ImGuizmo::OPERATION::TRANSLATE
        Entity m_SelectedEntity;
    };
}
