#pragma once

#include "Scene/Scene.h"
#include "Scene/Entity.h"

#include <functional>
#include <memory>
#include <string>

namespace Conqueror::Editor
{
    class HierarchyPanel
    {
    public:
        HierarchyPanel(const std::shared_ptr<Scene>& scene);
        ~HierarchyPanel() = default;

        void OnImGuiRender();
        void SetContext(const std::shared_ptr<Scene>& scene);
        void SetSelectionChangedCallback(std::function<void(Entity)> callback) { m_SelectionChangedCallback = callback; }

        Entity GetSelectedEntity() const { return m_SelectionContext; }
        void SetSelectedEntity(Entity entity);
        void StartRename(Entity entity);

    private:
        void DrawEntityNode(Entity entity);
        void DrawComponents(Entity entity);
        bool IsDescendant(Entity parent, Entity potentialDescendant);

    private:
        std::shared_ptr<Scene> m_Context;
        Entity m_SelectionContext;
        std::function<void(Entity)> m_SelectionChangedCallback;

        // Rename state
        bool m_IsRenaming = false;
        Entity m_RenameEntity;
        char m_RenameBuffer[256] = "";
        int m_RenameFrame = 0;
    };
}
