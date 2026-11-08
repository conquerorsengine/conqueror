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
        // Yıkıcı .cpp'de: ProjectHub orada tam tür (unique_ptr eksik türü silemez, libc++ bunu hata sayar)
        ~LauncherLayer() override;

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
