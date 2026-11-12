#include "Project.h"
#include "Core/Localization/Localization.h"
#include "Core/Console/ConsoleVariables.h"
#include "Scripting/ScriptEngine.h"
#include "Core/Audio/AudioEngine.h"
#include "Core/Logging/Log.h"
#include "Core/Debug/CrashHandler.h"
#include "Core/Utils/PlayerPrefs.h"

namespace Conqueror {

    Project::Project()
    {
    }

    Project::~Project()
    {
    }

    void Project::ReloadScriptEngine()
    {
        ScriptEngine::Shutdown();
        ScriptEngine::Init();
        // Load project assembly using m_Config.ScriptModulePath later
    }

    std::filesystem::path Project::GetActiveProjectFile()
    {
        if (!s_ActiveProject)
            return {};
        const auto& dir = s_ActiveProject->m_ProjectDirectory;
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(dir, ec))
            if (entry.path().extension() == ".cqproj")
                return entry.path();
        return dir / (s_ActiveProject->m_Config.Name + ".cqproj");
    }

    void Project::SetActive(Ref<Project> project)
    {
        if (s_ActiveProject)
        {
            if (s_AssetManager)
                s_AssetManager->Shutdown();
            s_AssetManager = nullptr;
            ScriptEngine::Shutdown();
        }

        s_ActiveProject = project;
        if (s_ActiveProject)
        {
            if (!s_ActiveProject->m_ProjectDirectory.empty())
                CrashHandler::SetReportDirectory((s_ActiveProject->m_ProjectDirectory / "crash_dumps").string());
            s_AssetManager = CreateRef<EditorAssetManager>();
            
            ScriptEngine::Init();
            ConsoleSystem::OnProjectLoaded(s_ActiveProject->m_ProjectDirectory);
            Localization::Reload();   // çeviri dosyaları (proje göreli)
            
            // AudioEngine could be notified here
        }
    }

    void Project::SetActiveRuntime(Ref<Project> project, Ref<AssetPack> assetPack)
    {
        if (s_ActiveProject)
        {
            if (s_AssetManager)
                s_AssetManager->Shutdown();
            s_AssetManager = nullptr;
            ScriptEngine::Shutdown();
        }

        s_ActiveProject = project;
        if (s_ActiveProject)
        {
            // Derlenmiş oyun klasörü yazılamayabilir: kullanıcı veri klasörü
            CrashHandler::SetReportDirectory(PlayerPrefs::GetPersistentDataPath() + "/crash_dumps");
            s_AssetManager = CreateRef<RuntimeAssetManager>();
            if (assetPack)
                GetRuntimeAssetManager()->SetAssetPack(assetPack);

            ScriptEngine::Init();
            ConsoleSystem::OnProjectLoaded(s_ActiveProject->m_ProjectDirectory);
            Localization::Reload();
        }
    }
}
