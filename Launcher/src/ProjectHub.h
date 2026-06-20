#pragma once

#include <string>
#include <vector>
#include <filesystem>
#include <functional>

namespace Conqueror::Editor
{
    enum class ProjectTemplate
    {
        Empty = 0,
        Template2D,
        Template3D
    };

    enum class HubView
    {
        Projects = 0,
        Settings
    };

    struct ProjectInfo
    {
        std::string Name;
        std::filesystem::path Path;
        std::string LastOpened;
        
        ProjectInfo() = default;
        ProjectInfo(const std::string& name, const std::filesystem::path& path, const std::string& lastOpened = "")
            : Name(name), Path(path), LastOpened(lastOpened) {}
    };

    class ProjectHub
    {
    public:
        ProjectHub();
        ~ProjectHub() = default;

        void OnImGuiRender();
        
        bool ShouldClose() const { return m_ShouldClose; }
        bool HasProjectSelected() const { return m_ProjectSelected; }
        const ProjectInfo& GetSelectedProject() const { return m_SelectedProject; }
        const std::string& GetLaunchCommand() const { return m_LaunchCommand; }

        void SetProjectOpenCallback(std::function<void(const ProjectInfo&)> callback) 
        { 
            m_ProjectOpenCallback = callback; 
        }

    private:
        void RenderProjectsHome();
        void RenderNewProjectPanel();
        void RenderRecentProjectsPanel();
        void RenderSettings();
        
        void CreateNewProject(const std::string& name, const std::filesystem::path& path, ProjectTemplate templateType);
        void OpenProject(const ProjectInfo& project);
        
        void LoadRecentProjects();
        void SaveRecentProjects();
        void AddToRecentProjects(const ProjectInfo& project);
        
        void SetupProjectTemplate(const std::filesystem::path& projectPath, ProjectTemplate templateType);
        void CreateEmptyTemplate(const std::filesystem::path& projectPath);
        void Create2DTemplate(const std::filesystem::path& projectPath);
        void Create3DTemplate(const std::filesystem::path& projectPath);

    private:
        bool m_ShouldClose = false;
        bool m_ProjectSelected = false;
        ProjectInfo m_SelectedProject;

        // UI State
        HubView m_CurrentView = HubView::Projects;
        bool m_ShowNewProject = false;

        // New project data
        char m_ProjectNameBuffer[256] = "NewProject";
        char m_ProjectPathBuffer[512] = "";
        int m_SelectedTemplate = 0;

        // Recent projects
        std::vector<ProjectInfo> m_RecentProjects;
        std::filesystem::path m_RecentProjectsFile;

        // Callback
        std::function<void(const ProjectInfo&)> m_ProjectOpenCallback;

        // Launch command (post-shutdown)
        std::string m_LaunchCommand;

        // Logo texture
        uint32_t m_LogoTexture = 0;

        // Template textures
        uint32_t m_3DTemplateTexture = 0;
        uint32_t m_2DTemplateTexture = 0;
        uint32_t m_EmptyTemplateTexture = 0;
    };
}
