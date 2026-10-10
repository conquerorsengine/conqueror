#pragma once

#include "Scene/Scene.h"
#include <memory>
#include <string>

namespace Conqueror::Editor
{
    class AIPanel
    {
    public:
        AIPanel();
        ~AIPanel() = default;

        void SetContext(const std::shared_ptr<Scene>& scene) { m_ActiveScene = scene; }
        void OnImGuiRender(bool* pOpen = nullptr);

    private:
        std::shared_ptr<Scene> m_ActiveScene;
        char m_InputBuffer[512] = "";
        char m_ApiKeyBuffer[256] = "";
        bool m_AutoScroll = true;
    };
}
