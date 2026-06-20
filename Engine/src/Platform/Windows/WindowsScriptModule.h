#pragma once

#include "Scripting/ScriptModule.h"

#ifdef CQ_PLATFORM_WINDOWS

#include <Windows.h>

namespace Conqueror
{
    class WindowsScriptModule : public ScriptModule
    {
    public:
        WindowsScriptModule() = default;
        virtual ~WindowsScriptModule() override;

        virtual bool Load(const std::string& path) override;
        virtual void Unload() override;
        virtual bool IsLoaded() const override { return m_ModuleHandle != nullptr; }
        virtual void* GetFunction(const std::string& name) override;

    private:
        HMODULE m_ModuleHandle = nullptr;
    };
}

#endif
