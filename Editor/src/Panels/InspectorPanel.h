#pragma once

#include "Scene/Entity.h"

namespace Conqueror::Editor
{
    class InspectorPanel
    {
    public:
        InspectorPanel() = default;
        ~InspectorPanel() = default;

        void OnImGuiRender();
        void SetSelectedEntity(Entity entity);
        Entity GetSelectedEntity() const { return m_SelectionContext; }
        void SetContext(class Scene* scene) 
        { 
            m_Context = scene; 
            // Scene değiştiğinde seçimi temizle
            m_SelectionContext = {};
        }

    private:
        void DrawComponents(Entity entity);
        
        template<typename T>
        void DrawAddComponentEntry(const std::string& entryName);

    private:
        Entity m_SelectionContext;
        class Scene* m_Context = nullptr;
    };
}
