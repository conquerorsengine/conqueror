#include "SceneManager.h"
#include "Core/Project/Project.h"

#include <algorithm>
#include <cctype>

namespace Conqueror
{
    namespace
    {
        struct State
        {
            std::filesystem::path Pending;
            bool HasPending = false;
            bool QuitRequested = false;
            std::filesystem::path Active;
            std::string Error;
        };

        State& S()
        {
            static State s;
            return s;
        }

        std::string Lower(std::string s)
        {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
            return s;
        }

        std::filesystem::path ProjectDir()
        {
            return Project::GetActiveProjectDirectory();
        }
    }

    std::vector<std::string> SceneManager::GetBuildScenes()
    {
        auto project = Project::GetActive();
        return project ? project->GetConfig().GetBuildScenes() : std::vector<std::string>{};
    }

    int SceneManager::GetSceneCount()
    {
        return (int)GetBuildScenes().size();
    }

    std::filesystem::path SceneManager::Resolve(const std::string& nameOrPath)
    {
        std::error_code ec;
        if (nameOrPath.empty())
            return {};
        const std::filesystem::path dir = ProjectDir();
        std::filesystem::path asPath(nameOrPath);
        if (asPath.extension() == ".cqscene")
        {
            if (asPath.is_absolute())
                return std::filesystem::exists(asPath, ec) ? asPath : std::filesystem::path{};
            for (const auto& base : { dir, dir / "Assets", dir / "Assets" / "Scenes" })
                if (std::filesystem::exists(base / asPath, ec))
                    return base / asPath;
            return {};
        }
        // Ad: önce derleme listesi, sonra Assets altında arama
        const std::string key = Lower(nameOrPath);
        for (const auto& rel : GetBuildScenes())
            if (Lower(std::filesystem::path(rel).stem().string()) == key)
                return dir / rel;
        if (!dir.empty() && std::filesystem::is_directory(dir / "Assets", ec))
        {
            for (auto it = std::filesystem::recursive_directory_iterator(dir / "Assets", std::filesystem::directory_options::skip_permission_denied, ec);
                 it != std::filesystem::recursive_directory_iterator(); it.increment(ec))
            {
                if (ec)
                    break;
                const auto& p = it->path();
                if (p.extension() == ".cqscene" && Lower(p.stem().string()) == key)
                    return p;
            }
        }
        return {};
    }

    bool SceneManager::RequestLoad(const std::string& nameOrPath)
    {
        auto path = Resolve(nameOrPath);
        if (path.empty())
        {
            S().Error = "Scene '" + nameOrPath + "' not found (add it to Build Settings or use a path under Assets)";
            return false;
        }
        S().Pending = path;
        S().HasPending = true;
        return true;
    }

    bool SceneManager::RequestLoadIndex(int index)
    {
        const auto scenes = GetBuildScenes();
        if (index < 0 || index >= (int)scenes.size())
        {
            S().Error = "Scene index " + std::to_string(index) + " is outside Build Settings (" + std::to_string(scenes.size()) + " scenes)";
            return false;
        }
        S().Pending = ProjectDir() / scenes[(size_t)index];
        S().HasPending = true;
        return true;
    }

    bool SceneManager::RequestReload()
    {
        if (S().Active.empty())
        {
            S().Error = "No active scene to reload";
            return false;
        }
        S().Pending = S().Active;
        S().HasPending = true;
        return true;
    }

    bool SceneManager::HasPendingLoad() { return S().HasPending; }

    std::filesystem::path SceneManager::TakePendingLoad()
    {
        S().HasPending = false;
        return std::move(S().Pending);
    }

    void SceneManager::RequestQuit() { S().QuitRequested = true; }

    bool SceneManager::TakeQuitRequest()
    {
        const bool q = S().QuitRequested;
        S().QuitRequested = false;
        return q;
    }

    void SceneManager::SetActiveScenePath(const std::filesystem::path& path) { S().Active = path; }
    const std::filesystem::path& SceneManager::GetActiveScenePath() { return S().Active; }

    std::string SceneManager::GetActiveSceneName()
    {
        return S().Active.empty() ? std::string() : S().Active.stem().string();
    }

    int SceneManager::GetActiveSceneIndex()
    {
        if (S().Active.empty())
            return -1;
        std::error_code ec;
        const auto scenes = GetBuildScenes();
        const auto dir = ProjectDir();
        for (size_t i = 0; i < scenes.size(); ++i)
            if (std::filesystem::equivalent(dir / scenes[i], S().Active, ec))
                return (int)i;
        return -1;
    }

    const std::string& SceneManager::GetLastError() { return S().Error; }

    void SceneManager::Reset()
    {
        S().HasPending = false;
        S().Pending.clear();
        S().QuitRequested = false;
    }
}
