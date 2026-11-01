#ifdef CQ_METAL_ENABLED

#include "MetalContext.h"
#include "MetalCore.h"
#include "MtlGL.h"
#include "Core/Logging/Log.h"

namespace Conqueror
{
    MetalContext::MetalContext(GLFWwindow* windowHandle)
        : m_WindowHandle(windowHandle)
    {
    }

    MetalContext::~MetalContext()
    {
        if (!m_Initialized)
            return;
        MtlGL::Shutdown();
        MTL::Core::Get().Shutdown();
    }

    bool MetalContext::Init()
    {
        std::string error;
        auto& core = MTL::Core::Get();
        if (!core.Init(m_WindowHandle, error))
        {
            CQ_CORE_ERROR("[Metal] Initialization failed: {0}", error);
            core.Shutdown();
            return false;
        }
        if (!MtlGL::Install(error))
        {
            CQ_CORE_ERROR("[Metal] GL translation layer failed: {0}", error);
            MtlGL::Shutdown();
            core.Shutdown();
            return false;
        }
        m_Initialized = true;
        CQ_CORE_INFO("Metal Info:");
        CQ_CORE_INFO("  Device: {0}", core.DeviceName());
        CQ_CORE_INFO("  Backend: GL 4.6 -> Metal translation (MtlGL)");
        return true;
    }

    void MetalContext::SwapBuffers()
    {
        if (m_Initialized)
            MtlGL::SwapBuffers();
    }

    void MetalContext::SetVSync(bool enabled)
    {
        if (m_Initialized)
            MTL::Core::Get().SetVSync(enabled);
    }
}

#endif
