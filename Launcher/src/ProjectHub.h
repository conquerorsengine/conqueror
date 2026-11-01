#pragma once

#include "Renderer/RHI/Texture.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Conqueror::Editor
{
    struct ProjectInfo
    {
        std::string Name;
        std::filesystem::path Path;
        std::string LastOpened;      // "YYYY-MM-DD HH:MM"
        bool Favorite = false;

        // Çalışma zamanı
        bool Missing = false;
        std::string StartScene;
        std::shared_ptr<Texture2D> Thumbnail;
        bool ThumbnailTried = false;

        ProjectInfo() = default;
        ProjectInfo(const std::string& name, const std::filesystem::path& path, const std::string& lastOpened = "")
            : Name(name), Path(path), LastOpened(lastOpened) {}
    };

    // Conqueror Hub (Unity Hub benzeri): projeler, şablondan yeni proje, öğrenme / betik API başvurusu,
    // addon kataloğu ve ayarlar. Ayarlar kullanıcı yapılandırma klasöründe (CQ_HUB_CONFIG_DIR ile değiştirilebilir).
    class ProjectHub
    {
    public:
        ProjectHub();
        ~ProjectHub();

        void OnImGuiRender();

        bool ShouldClose() const { return m_ShouldClose; }
        bool HasProjectSelected() const { return m_ProjectSelected; }
        const ProjectInfo& GetSelectedProject() const { return m_SelectedProject; }
        const std::string& GetLaunchCommand() const { return m_LaunchCommand; }

        void SetProjectOpenCallback(std::function<void(const ProjectInfo&)> callback) { m_ProjectOpenCallback = std::move(callback); }

    private:
        enum class Page : int { Projects = 0, NewProject, Learn, Addons, Settings };

        void DrawSidebar(float width);
        void DrawProjectsPage();
        void DrawProjectCard(ProjectInfo& project, int index, float width, float height);
        void DrawProjectRow(ProjectInfo& project, int index);
        void DrawProjectContextMenu(ProjectInfo& project, int index);
        void DrawNewProjectPage();
        void DrawLearnPage();
        void DrawAddonsPage();
        void DrawSettingsPage();
        void DrawHeader(const char* title, const char* subtitle);

        void OpenProject(const ProjectInfo& project);
        void AddExistingProject();
        bool CreateFromTemplate(const std::string& templateId, const std::string& name, const std::filesystem::path& location, bool open);

        void LoadConfig();
        void SaveConfig();
        void RefreshProjectStates();
        void AddToRecentProjects(const ProjectInfo& project);
        std::vector<int> VisibleProjects() const;
        void RegisterAutomation();

        static std::filesystem::path ConfigDirectory();
        static std::string NowString();
        static std::string RelativeTime(const std::string& stamp);

    private:
        bool m_ShouldClose = false;
        bool m_ProjectSelected = false;
        ProjectInfo m_SelectedProject;
        std::function<void(const ProjectInfo&)> m_ProjectOpenCallback;
        std::string m_LaunchCommand;

        Page m_Page = Page::Projects;
        std::vector<ProjectInfo> m_Projects;
        std::string m_Search;
        int m_SortMode = 0;          // 0 son açılan, 1 ad, 2 yol
        bool m_GridView = true;
        bool m_ShowMissing = true;
        float m_UIScale = 1.0f;
        std::string m_DefaultLocation;
        int m_PendingRemove = -1;
        std::string m_Status;        // alt bilgi / hata satırı
        bool m_StatusIsError = false;

        // Yeni proje
        std::string m_TemplateId = "basic3d";
        std::string m_Industry = "Games";   // Yeni Proje: seçili sektör (Unreal gibi)
        std::string m_NewName = "My Game";
        std::string m_NewLocation;
        int m_PhysicsBackend = 0;
        bool m_InstallAddons = true;

        // Öğren
        int m_LearnTopic = 0;
        std::string m_ApiSearch;

        std::shared_ptr<Texture2D> m_LogoTexture;
        std::filesystem::path m_ConfigFile;

        // Şablon kart resimleri: Resources/Templates/Thumbnails/<id>.jpg (Tools/Templates/make_thumbnails.py), yoksa degrade + ikon
        Texture2D* TemplateThumbnail(const std::string& id);
        std::unordered_map<std::string, std::shared_ptr<Texture2D>> m_TemplateThumbs;
    };
}
