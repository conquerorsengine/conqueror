#pragma once

#include "Core/Base/Base.h"
#include "Scene/Entity.h"

namespace Conqueror::Editor
{
    class AudioGraphPanel
    {
    public:
        AudioGraphPanel();
        ~AudioGraphPanel();

        void SetContext(class Scene* scene);
        void SetSelectedEntity(Entity entity);
        void OnImGuiRender();

    private:
        class Scene* m_Context = nullptr;
        Entity m_SelectedEntity;
    };
}
