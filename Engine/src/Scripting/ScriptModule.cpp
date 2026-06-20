#include "ScriptModule.h"
#include "Core/Base/Base.h"

#ifdef CQ_PLATFORM_WINDOWS
    #include "Platform/Windows/WindowsScriptModule.h"
#endif

#ifndef CQ_PLATFORM_WINDOWS
    #include "Platform/Linux/LinuxScriptModule.h"
#endif

namespace Conqueror
{
    std::unique_ptr<ScriptModule> ScriptModule::Create()
    {
        CQ_CORE_INFO("ScriptModule::Create - Creating platform-specific module");
        
        #ifdef CQ_PLATFORM_WINDOWS
            CQ_CORE_INFO("ScriptModule::Create - Platform: Windows");
            return std::make_unique<WindowsScriptModule>();
        #else
            CQ_CORE_INFO("ScriptModule::Create - Platform: Linux");
            return std::make_unique<LinuxScriptModule>();
        #endif
    }
}
