#include "RuntimeLayer.h"
#include "Core/Project/Project.h"
#include "Core/Project/ProjectSerializer.h"
#include "Core/Serialization/SceneSerializer.h"
#include "Core/Logging/Log.h"
#include "Core/Application.h"
#include "Renderer/Renderer.h"
#include "Renderer/Renderer2D.h"
#include "Renderer/Utilities/Renderer3D/Renderer3D.h"
#include "Renderer/RHI/RenderCommand.h"

namespace Conqueror::Runtime
{
    RuntimeLayer::RuntimeLayer(const std::filesystem::path& projectPath)
        : Layer("RuntimeLayer"), m_ProjectPath(projectPath)
    {
    }

    void RuntimeLayer::OnAttach()
    {
        if (!LoadProject())
        {
            CQ_CORE_ERROR("Failed to load project at runtime!");
            Application::Get().Close();
        }
        else
        {
            uint32_t width = Application::Get().GetWindow().GetWidth();
            uint32_t height = Application::Get().GetWindow().GetHeight();
            RenderCommand::SetViewport(0, 0, width, height);
        }
    }

    void RuntimeLayer::OnDetach()
    {
        // Stop audio, cleanup if necessary
    }

    void RuntimeLayer::OnUpdate(Timestep ts)
    {
        if (m_ActiveScene)
        {
            RenderCommand::SetClearColor({ 0.1f, 0.1f, 0.1f, 1.0f });
            RenderCommand::Clear();
            m_ActiveScene->OnUpdateRuntime(ts);
        }
    }

    void RuntimeLayer::OnEvent(Event& e)
    {
        // Handle global events (e.g. window resize) if needed
    }

    bool RuntimeLayer::LoadProject()
    {
        auto project = std::make_shared<Project>();
        ProjectSerializer serializer(project);
        
        // Find .cqproj file in m_ProjectPath
        std::filesystem::path projectFilePath;
        for (const auto& entry : std::filesystem::directory_iterator(m_ProjectPath))
        {
            if (entry.path().extension() == ".cqproj" || entry.path().extension() == ".cqproject")
            {
                projectFilePath = entry.path();
                break;
            }
        }
        
        if (projectFilePath.empty() || !serializer.Deserialize(projectFilePath))
        {
            CQ_CORE_ERROR("Could not find or deserialize project in {0}", m_ProjectPath.string());
            return false;
        }

        Project::SetActive(project);

        std::filesystem::path startScenePath = project->GetProjectDirectory() / project->GetConfig().StartScene;
        
        if (!std::filesystem::exists(startScenePath))
        {
            CQ_CORE_ERROR("Start scene missing: {0}", startScenePath.string());
            return false;
        }

        m_ActiveScene = std::make_shared<Scene>();
        SceneSerializer sceneSerializer(m_ActiveScene);
        if (!sceneSerializer.Deserialize(startScenePath))
        {
            CQ_CORE_ERROR("Failed to load start scene: {0}", startScenePath.string());
            return false;
        }

        CQ_CORE_INFO("Project loaded successfully, starting simulation.");
        
        // Ensure resize is handled initially
        m_ActiveScene->OnViewportResize(Application::Get().GetWindow().GetWidth(), Application::Get().GetWindow().GetHeight());

        return true;
    }
}
