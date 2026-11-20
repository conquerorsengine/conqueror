#pragma once

#include "Core/Project/EditorInstalls.h"
#include "Renderer/RHI/Texture.h"

#include <atomic>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <set>
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
        std::string EditorVersion;   // projeyi son kaydeden editörün sürümü (.cqproj Project/EditorVersion; eski projelerde boş)
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
        enum class Page : int { Projects = 0, NewProject, Learn, Addons, Settings, Installs };

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

        // ---- Installs (47, Unity Hub "Installs"): HubInstalls.cpp
        struct ReleasesFetch
        {
            std::atomic<bool> Done{ false };
            std::vector<Installs::Release> Releases;
            std::string Error;
        };
        struct HubJob
        {
            std::shared_ptr<Installs::Job> Job;
            std::string Label;
            bool Reported = false;
        };
        void DrawInstallsPage();
        void DrawInstallRow(const Installs::Install& install, int index);
        void DrawInstallEditorModal();
        void DrawModulePicker(const Installs::Release& release, const Installs::Install* existing, std::set<std::string>& picked);
        void DrawAddModulesModal();
        void ReloadInstalls();
        void FetchReleases(bool force);
        void PollInstalls();
        bool StartInstall(const Installs::Release& release, const std::vector<std::string>& modules, std::string& error);
        bool StartAddModules(int installIndex, const std::vector<std::string>& modules, std::string& error);
        void LocateInstall();
        bool LocateInstallAt(const std::filesystem::path& dir, std::string& error);
        void CheckConquerorXUpdates();
        const Installs::Release* FindRelease(const Installs::Install& install) const;
        const Installs::Requirement& CachedRequirement(const std::string& id, const std::filesystem::path& installDir);
        int ProjectsUsing(const std::string& version) const;
        std::filesystem::path EditorForProject(const ProjectInfo& project) const;
        void RegisterInstallsAutomation();

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
        std::filesystem::path m_OpenWithEditor;   // projenin bağlam menüsünden "Open with" ile seçilen editör (bir kez)
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
        // İpuçları yalnızca fare Hub açıldıktan sonra gerçekten hareket ettiyse açılır: açılışta imleç zaten bir projenin
        // üstünde duruyorsa kutu kendiliğinden çıkmaz (Unity Hub / tarayıcılar gibi)
        bool m_PointerMoved = false;
        int m_TipCard = -1;          // 284: üstünde durulan proje kartı ve ne zamandan beri (ipucu 0.6 sn sonra)
        double m_TipSince = 0.0;
        static constexpr double kCardTipDelay = 0.6;
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
        std::string m_CreatedName;      // son "Create Project" ile oluşturulan ad (pencere kapanırken gösterilir)
        bool m_NewNameEdited = false;   // kullanıcı adı elle yazdı: otomatik numaralama kutudaki adı değiştirmez
        int m_NewNameRefresh = 0;       // konumdaki boş ad birkaç karede bir yeniden bakılır (dosya sistemi her karede taranmasın)
        std::string m_NewLocation;
        int m_PhysicsBackend = 0;
        bool m_InstallAddons = true;

        // Öğren
        int m_LearnTopic = 0;
        std::string m_ApiSearch;

        std::shared_ptr<Texture2D> m_LogoTexture;
        std::filesystem::path m_ConfigFile;

        // Installs
        std::vector<Installs::Install> m_Installs;
        int m_InstallsTab = 0;                     // 0 All, 1 Official releases, 2 Pre-releases
        std::string m_InstallsSearch;
        std::shared_ptr<ReleasesFetch> m_ReleasesFetch;
        std::vector<Installs::Release> m_Releases;
        bool m_ReleasesLoaded = false;
        std::string m_ReleasesError;
        bool m_OpenInstallModal = false;
        int m_InstallModalTab = 0;                 // 0 Official, 1 Pre-releases, 2 ConquerorX, 3 Archive
        int m_InstallPicked = -1;                  // m_Releases içindeki sürüm (modül adımı)
        std::set<std::string> m_PickedModules;
        int m_AddModulesFor = -1;                  // m_Installs içindeki kurulum (Add modules penceresi)
        bool m_OpenAddModulesModal = false;
        std::vector<HubJob> m_Jobs;
        std::map<std::string, Installs::Requirement> m_RequirementCache;
        bool m_ConquerorXChecked = false;
        std::string m_InstallsStatus;
        bool m_InstallsStatusError = false;

        // Şablon kart resimleri: Resources/Templates/Thumbnails/<id>.jpg (Tools/Templates/make_thumbnails.py), yoksa degrade + ikon
        Texture2D* TemplateThumbnail(const std::string& id);
        std::unordered_map<std::string, std::shared_ptr<Texture2D>> m_TemplateThumbs;
    };
}
