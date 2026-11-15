#ifdef CQ_WEBGPU_ENABLED

#include "WebGPUContext.h"
#include "WgpuGL.h"
#include "Core/Logging/Log.h"

namespace Conqueror
{
    WebGPUContext::WebGPUContext(GLFWwindow* windowHandle)
        : m_WindowHandle(windowHandle)
    {
    }

    WebGPUContext::~WebGPUContext()
    {
        if (!m_Initialized)
            return;
        WgpuGL::Shutdown();
        WgpuGL::ShutdownDevice();
    }

    bool WebGPUContext::Init()
    {
        std::string error;
        if (!WgpuGL::InitDevice(m_WindowHandle, error))
        {
            CQ_CORE_ERROR("[WebGPU] Initialization failed: {0}", error);
            WgpuGL::ShutdownDevice();
            return false;
        }
        if (!WgpuGL::Install(error))
        {
            CQ_CORE_ERROR("[WebGPU] GL translation layer failed: {0}", error);
            WgpuGL::Shutdown();
            WgpuGL::ShutdownDevice();
            return false;
        }
        m_Initialized = true;
        CQ_CORE_INFO("WebGPU Info:");
        CQ_CORE_INFO("  Device: {0}", WgpuGL::DeviceName());
        CQ_CORE_INFO("  Implementation: {0}", WGPU::LibraryName());
        CQ_CORE_INFO("  Backend: GL 4.6 -> WebGPU translation (WgpuGL), shaders SPIR-V -> WGSL (Tint)");
        return true;
    }

    void WebGPUContext::SwapBuffers()
    {
        if (m_Initialized)
            WgpuGL::SwapBuffers();
    }

    void WebGPUContext::SetVSync(bool enabled)
    {
        if (m_Initialized)
            WgpuGL::SetVSync(enabled);
    }
}

#endif
