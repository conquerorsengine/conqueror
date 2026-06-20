#pragma once

#include <memory>

namespace Conqueror
{
    class Scene;
}

namespace Conqueror::Editor
{
    class ProjectSettingsPanel
    {
    public:
        ProjectSettingsPanel() = default;
        ~ProjectSettingsPanel() = default;

        void OnImGuiRender();
        void SetContext(std::shared_ptr<Scene> context) { m_Context = context; }

    private:
        std::shared_ptr<Scene> m_Context;
    };
}
