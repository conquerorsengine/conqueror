#include "LauncherLayer.h"
#include "ProjectHub.h"
#include "Core/Logging/Log.h"
#include "Core/Application.h"

namespace Conqueror::Launcher
{
    static std::string s_PendingLaunchCommand;

    const std::string& GetPendingLaunchCommand() { return s_PendingLaunchCommand; }

    LauncherLayer::LauncherLayer()
        : Layer("LauncherLayer")
    {
    }

    void LauncherLayer::OnAttach()
    {
        CQ_CORE_INFO("LauncherLayer: Initializing...");
        
        try
        {
            CQ_CORE_INFO("LauncherLayer: Creating ProjectHub...");
            m_ProjectHub = std::make_unique<Editor::ProjectHub>();
            CQ_CORE_INFO("LauncherLayer: ProjectHub created successfully");
            
            // ProjectHub callback - proje seçilince
            m_ProjectHub->SetProjectOpenCallback([this](const Editor::ProjectInfo& project) {
                OnProjectSelected(project.Path);
            });
            
            CQ_CORE_INFO("LauncherLayer: Initialization complete");
        }
        catch (const std::exception& e)
        {
            CQ_CORE_ERROR("LauncherLayer: Failed to initialize: {0}", e.what());
            throw;
        }
    }

    void LauncherLayer::OnDetach()
    {
    }

    void LauncherLayer::OnUpdate(Timestep /*ts*/)
    {
        if (m_ProjectHub && m_ProjectHub->ShouldClose())
        {
            s_PendingLaunchCommand = m_ProjectHub->GetLaunchCommand();
            Application::Get().Close();
        }
    }

    void LauncherLayer::OnImGuiRender()
    {
        if (m_ProjectHub)
            m_ProjectHub->OnImGuiRender();
    }

    void LauncherLayer::OnEvent(Event& /*e*/)
    {
    }

    void LauncherLayer::OnProjectSelected(const std::filesystem::path& projectPath)
    {
        CQ_CORE_INFO("Project selected: {0}", projectPath.string());
    }
}
