#pragma once

#include "Core/Base/Base.h"
#include "Core/Assets/AssetManager/AssetManager.h"
#include "Core/Assets/AssetManager/EditorAssetManager.h"
#include "Core/Assets/AssetManager/RuntimeAssetManager.h"

#include <string>
#include <filesystem>
#include <memory>

namespace Conqueror {

    struct ProjectConfig
    {
        std::string Name = "Untitled";
        std::string Organization = "MyCompany";
        std::string Version = "1.0.0";
        std::string TargetPlatform = "Linux"; // "Linux" or "Windows"
        
        std::string StartScene;
        std::string AssetDirectory;
        std::string ScriptModulePath;
    };

    class Project
    {
    public:
        Project();
        ~Project();

        const ProjectConfig& GetConfig() const { return m_Config; }
        ProjectConfig& GetConfig() { return m_Config; }

        static Ref<Project> GetActive() { return s_ActiveProject; }
        static void SetActive(Ref<Project> project);
        static void SetActiveRuntime(Ref<Project> project, Ref<AssetPack> assetPack = nullptr);
        
        static void ReloadScriptEngine();

        std::filesystem::path GetProjectDirectory() const { return m_ProjectDirectory; }
        void SetProjectDirectory(const std::filesystem::path& path) { m_ProjectDirectory = path; }

        static std::filesystem::path GetActiveProjectDirectory()
        {
            if (s_ActiveProject)
                return s_ActiveProject->m_ProjectDirectory;
            return "";
        }

        static std::filesystem::path GetAssetDirectory()
        {
            if (s_ActiveProject)
                return GetActiveProjectDirectory() / s_ActiveProject->m_Config.AssetDirectory;
            return "";
        }

        static Ref<AssetManager> GetAssetManager() { return s_AssetManager; }
        static Ref<RuntimeAssetManager> GetRuntimeAssetManager() { return std::static_pointer_cast<RuntimeAssetManager>(s_AssetManager); }

    private:
        ProjectConfig m_Config;
        std::filesystem::path m_ProjectDirectory;

        inline static Ref<Project> s_ActiveProject;
        inline static Ref<AssetManager> s_AssetManager;
    };
}
