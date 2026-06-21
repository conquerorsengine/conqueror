#pragma once

#include "Scripting/ScriptModule.h"
#include <dlfcn.h>
#include <string>
#include <vector>

namespace Conqueror
{
    class LinuxScriptModule : public ScriptModule
    {
    public:
        LinuxScriptModule() = default;
        virtual ~LinuxScriptModule() override;

        virtual bool Load(const std::string& path) override;
        virtual void Unload() override;
        virtual bool IsLoaded() const override { return m_ModuleHandle != nullptr; }
        virtual void* GetFunction(const std::string& name) override;
        virtual std::vector<std::string> GetExportedClassNames() override;

    private:
        void* m_ModuleHandle = nullptr;
        std::string m_LoadPath;
    };
}
