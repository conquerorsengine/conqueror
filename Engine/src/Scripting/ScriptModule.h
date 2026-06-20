#pragma once

#include "Core/Base/Base.h"
#include <string>
#include <memory>

namespace Conqueror
{
    class ScriptableEntity;

    // DLL/SO modül yönetimi için interface
    class ScriptModule
    {
    public:
        virtual ~ScriptModule() = default;

        // Modülü yükle
        virtual bool Load(const std::string& path) = 0;
        
        // Modülü kaldır
        virtual void Unload() = 0;
        
        // Modül yüklü mü?
        virtual bool IsLoaded() const = 0;
        
        // Fonksiyon pointer'ı al
        virtual void* GetFunction(const std::string& name) = 0;

        // Platform-specific modül oluştur
        static std::unique_ptr<ScriptModule> Create();
    };
}
