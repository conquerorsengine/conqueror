#pragma once

#include "Core/Layer.h"
#include <memory>
#include <filesystem>

namespace Conqueror
{
    namespace Editor { class ProjectHub; }
}

namespace Conqueror::Launcher
{
    const std::string& GetPendingLaunchCommand();

    class LauncherLayer : public Layer
    {
    public:
        LauncherLayer();
        virtual ~LauncherLayer() = default;

        void OnAttach() override;
        void OnDetach() override;
        void OnUpdate(Timestep ts) override;
        void OnImGuiRender() override;
        void OnEvent(Event& e) override;

    private:
        void OnProjectSelected(const std::filesystem::path& projectPath);

    private:
        std::unique_ptr<Editor::ProjectHub> m_ProjectHub;
    };
}
