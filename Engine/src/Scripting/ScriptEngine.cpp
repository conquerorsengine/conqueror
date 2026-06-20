#include "ScriptEngine.h"
#include "Core/Logging/Log.h"
#include "ConquerorScript/CQSEngine.h"

namespace Conqueror
{
    std::unordered_map<std::string, std::unique_ptr<ScriptModule>> ScriptEngine::s_Modules;

    void ScriptEngine::Init()
    {
        CQ_CORE_INFO("ScriptEngine::Init - Starting initialization");
        CQS::CQSEngine::Init();
        CQ_CORE_INFO("ScriptEngine initialized");
    }

    void ScriptEngine::Shutdown()
    {
        CQ_CORE_INFO("ScriptEngine::Shutdown - Starting shutdown");
        CQS::CQSEngine::Shutdown();
        CQ_CORE_INFO("ScriptEngine::Shutdown - Clearing {0} modules", s_Modules.size());
        s_Modules.clear();
        CQ_CORE_INFO("ScriptEngine shutdown");
    }


    bool ScriptEngine::LoadModule(const std::string& moduleName, const std::string& path)
    {
        CQ_CORE_INFO("ScriptEngine::LoadModule - Module name: {0}, Path: {1}", moduleName, path);
        
        if (s_Modules.find(moduleName) != s_Modules.end())
        {
            CQ_CORE_WARN("Module already loaded: {0}", moduleName);
            return false;
        }

        CQ_CORE_INFO("ScriptEngine::LoadModule - Creating ScriptModule...");
        auto module = ScriptModule::Create();
        
        if (!module)
        {
            CQ_CORE_ERROR("ScriptEngine::LoadModule - Failed to create ScriptModule!");
            return false;
        }
        
        CQ_CORE_INFO("ScriptEngine::LoadModule - ScriptModule created, calling Load...");
        if (!module->Load(path))
        {
            CQ_CORE_ERROR("ScriptEngine::LoadModule - module->Load() returned false for: {0}", moduleName);
            return false;
        }

        CQ_CORE_INFO("ScriptEngine::LoadModule - Load successful, storing module");
        s_Modules[moduleName] = std::move(module);
        CQ_CORE_INFO("Module loaded: {0}", moduleName);
        return true;
    }

    void ScriptEngine::UnloadModule(const std::string& moduleName)
    {
        CQ_CORE_INFO("ScriptEngine::UnloadModule - Module: {0}", moduleName);
        
        auto it = s_Modules.find(moduleName);
        if (it != s_Modules.end())
        {
            CQ_CORE_INFO("ScriptEngine::UnloadModule - Module found, unloading...");
            it->second->Unload();
            s_Modules.erase(it);
            CQ_CORE_INFO("Module unloaded: {0}", moduleName);
        }
        else
        {
            CQ_CORE_WARN("ScriptEngine::UnloadModule - Module not found: {0}", moduleName);
        }
    }

    bool ScriptEngine::IsModuleLoaded(const std::string& moduleName)
    {
        bool loaded = s_Modules.find(moduleName) != s_Modules.end();
        CQ_CORE_INFO("ScriptEngine::IsModuleLoaded - Module: {0}, Loaded: {1}", moduleName, loaded);
        return loaded;
    }

    ScriptableEntity* ScriptEngine::CreateScriptInstance(const std::string& moduleName, const std::string& className)
    {
        CQ_CORE_INFO("ScriptEngine::CreateScriptInstance - Module: {0}, Class: {1}", moduleName, className);
        
        auto it = s_Modules.find(moduleName);
        if (it == s_Modules.end())
        {
            CQ_CORE_ERROR("ScriptEngine::CreateScriptInstance - Module not loaded: {0}", moduleName);
            return nullptr;
        }

        CQ_CORE_INFO("ScriptEngine::CreateScriptInstance - Module found in map");
        
        // Factory fonksiyonu adı: Create<ClassName>
        std::string factoryName = "Create" + className;
        CQ_CORE_INFO("ScriptEngine::CreateScriptInstance - Factory function name: {0}", factoryName);
        
        // DLL'den factory fonksiyonunu al
        typedef ScriptableEntity* (*CreateFunc)();
        CQ_CORE_INFO("ScriptEngine::CreateScriptInstance - Calling GetFunction...");
        CreateFunc createFunc = (CreateFunc)it->second->GetFunction(factoryName);
        
        if (!createFunc)
        {
            CQ_CORE_ERROR("ScriptEngine::CreateScriptInstance - Factory function not found: {0}", factoryName);
            return nullptr;
        }

        CQ_CORE_INFO("ScriptEngine::CreateScriptInstance - Factory function found, calling it...");
        // Script instance oluştur
        ScriptableEntity* instance = createFunc();
        CQ_CORE_INFO("ScriptEngine::CreateScriptInstance - Instance created: {0}", (void*)instance);
        CQ_CORE_INFO("Created script instance: {0}::{1}", moduleName, className);
        return instance;
    }
}
