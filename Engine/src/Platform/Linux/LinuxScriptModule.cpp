#include "LinuxScriptModule.h"
#include "Core/Logging/Log.h"

namespace Conqueror
{
    LinuxScriptModule::~LinuxScriptModule()
    {
        Unload();
    }

    bool LinuxScriptModule::Load(const std::string& path)
    {
        CQ_CORE_INFO("LinuxScriptModule::Load - Attempting to load: {0}", path);
        
        if (m_ModuleHandle)
        {
            CQ_CORE_WARN("Module already loaded: {0}", path);
            return false;
        }

        CQ_CORE_INFO("LinuxScriptModule::Load - Calling dlopen with RTLD_NOW | RTLD_GLOBAL");
        m_ModuleHandle = dlopen(path.c_str(), RTLD_NOW | RTLD_GLOBAL);
        
        if (!m_ModuleHandle)
        {
            const char* error = dlerror();
            CQ_CORE_ERROR("LinuxScriptModule::Load - dlopen FAILED for: {0}", path);
            CQ_CORE_ERROR("LinuxScriptModule::Load - dlerror: {0}", error ? error : "Unknown");
            return false;
        }

        CQ_CORE_INFO("LinuxScriptModule::Load - dlopen SUCCESS, handle: {0}", (void*)m_ModuleHandle);
        CQ_CORE_INFO("Loaded script module: {0}", path);
        return true;
    }

    void LinuxScriptModule::Unload()
    {
        if (m_ModuleHandle)
        {
            CQ_CORE_INFO("LinuxScriptModule::Unload - Calling dlclose, handle: {0}", (void*)m_ModuleHandle);
            dlclose(m_ModuleHandle);
            m_ModuleHandle = nullptr;
            CQ_CORE_INFO("Unloaded script module");
        }
    }

    void* LinuxScriptModule::GetFunction(const std::string& name)
    {
        CQ_CORE_INFO("LinuxScriptModule::GetFunction - Looking for function: {0}", name);
        
        if (!m_ModuleHandle)
        {
            CQ_CORE_ERROR("LinuxScriptModule::GetFunction - Module not loaded!");
            return nullptr;
        }

        CQ_CORE_INFO("LinuxScriptModule::GetFunction - Module handle valid: {0}", (void*)m_ModuleHandle);
        
        dlerror(); // Clear error
        CQ_CORE_INFO("LinuxScriptModule::GetFunction - Calling dlsym for: {0}", name);
        void* func = dlsym(m_ModuleHandle, name.c_str());
        
        const char* error = dlerror();
        if (error)
        {
            CQ_CORE_ERROR("LinuxScriptModule::GetFunction - dlsym FAILED for: {0}", name);
            CQ_CORE_ERROR("LinuxScriptModule::GetFunction - dlerror: {0}", error);
            return nullptr;
        }

        CQ_CORE_INFO("LinuxScriptModule::GetFunction - dlsym SUCCESS, function pointer: {0}", func);
        return func;
    }
}
