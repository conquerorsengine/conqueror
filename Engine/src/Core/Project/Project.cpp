#include "Project.h"
#include "Scripting/ScriptEngine.h"
#include "Core/Audio/AudioEngine.h"
#include "Core/Logging/Log.h"

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
            s_AssetManager = CreateRef<EditorAssetManager>();
            
            ScriptEngine::Init();
            
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
            s_AssetManager = CreateRef<RuntimeAssetManager>();
            if (assetPack)
                GetRuntimeAssetManager()->SetAssetPack(assetPack);

            ScriptEngine::Init();
        }
    }
}
