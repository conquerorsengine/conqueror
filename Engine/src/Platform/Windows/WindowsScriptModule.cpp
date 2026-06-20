#include "WindowsScriptModule.h"

#ifdef CQ_PLATFORM_WINDOWS

#include "Core/Logging/Log.h"

namespace Conqueror
{
    WindowsScriptModule::~WindowsScriptModule()
    {
        Unload();
    }

    bool WindowsScriptModule::Load(const std::string& path)
    {
        if (m_ModuleHandle)
        {
            CQ_CORE_WARN("Module already loaded: {0}", path);
            return false;
        }

        m_ModuleHandle = LoadLibraryA(path.c_str());
        
        if (!m_ModuleHandle)
        {
            DWORD error = GetLastError();
            CQ_CORE_ERROR("Failed to load module: {0}, Error: {1}", path, error);
            return false;
        }

        CQ_CORE_INFO("Loaded script module: {0}", path);
        return true;
    }

    void WindowsScriptModule::Unload()
    {
        if (m_ModuleHandle)
        {
            FreeLibrary(m_ModuleHandle);
            m_ModuleHandle = nullptr;
            CQ_CORE_INFO("Unloaded script module");
        }
    }

    void* WindowsScriptModule::GetFunction(const std::string& name)
    {
        if (!m_ModuleHandle)
        {
            CQ_CORE_ERROR("Module not loaded");
            return nullptr;
        }

        void* func = (void*)GetProcAddress(m_ModuleHandle, name.c_str());
        
        if (!func)
        {
            CQ_CORE_ERROR("Function not found: {0}", name);
        }

        return func;
    }
}

#endif
