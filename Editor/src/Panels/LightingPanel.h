#pragma once

#include "Scene/Scene.h"
#include <memory>

namespace Conqueror::Editor
{
    class LightingPanel
    {
    public:
        LightingPanel() = default;
        ~LightingPanel() = default;

        void OnImGuiRender();
        void SetContext(std::shared_ptr<Scene> scene) { m_Context = scene; }

    private:
        std::shared_ptr<Scene> m_Context;
        int m_SkyboxResolutionIndex = 4; // 0=512, 1=1024, 2=2048, 3=4096, 4=Original
    };
}
