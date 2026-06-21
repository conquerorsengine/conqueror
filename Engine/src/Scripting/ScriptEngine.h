#pragma once

#include "ScriptModule.h"
#include "ScriptableEntity.h"
#include <unordered_map>
#include <string>
#include <memory>

namespace Conqueror
{
    // Script modüllerini yükler ve yönetir
    class CQ_API ScriptEngine
    {
    public:
        static void Init();
        static void Shutdown();

        // Script modülü yükle
        static bool LoadModule(const std::string& moduleName, const std::string& path);
        
        // Script modülü kaldır
        static void UnloadModule(const std::string& moduleName);
        
        // Modül yüklü mü?
        static bool IsModuleLoaded(const std::string& moduleName);
        
        // Script instance oluştur
        static ScriptableEntity* CreateScriptInstance(const std::string& moduleName, const std::string& className);

        // Modülün export ettiği Create* class adlarını döndür
        static std::vector<std::string> GetModuleClassNames(const std::string& moduleName);

    private:
        static std::unordered_map<std::string, std::unique_ptr<ScriptModule>> s_Modules;
    };
}
