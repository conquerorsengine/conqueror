#ifdef CQ_D3D11_ENABLED

#include "D3D11Context.h"
#include "D3D11Core.h"
#include "DxGL.h"
#include "Core/Logging/Log.h"

namespace Conqueror
{
    D3D11Context::D3D11Context(GLFWwindow* windowHandle)
        : m_WindowHandle(windowHandle)
    {
    }

    D3D11Context::~D3D11Context()
    {
        if (!m_Initialized)
            return;
        DxGL::Shutdown();
        DX::Core::Get().Shutdown();
    }

    bool D3D11Context::Init()
    {
        std::string error;
        auto& core = DX::Core::Get();
        if (!core.Init(m_WindowHandle, error))
        {
            CQ_CORE_ERROR("[D3D11] Initialization failed: {0}", error);
            core.Shutdown();
            return false;
        }
        if (!DxGL::Install(error))
        {
            CQ_CORE_ERROR("[D3D11] GL translation layer failed: {0}", error);
            DxGL::Shutdown();
            core.Shutdown();
            return false;
        }
        m_Initialized = true;
        CQ_CORE_INFO("Direct3D 11 Info:");
        CQ_CORE_INFO("  Device: {0}", core.DeviceName());
        CQ_CORE_INFO("  Backend: GL 4.6 -> Direct3D 11 translation (DxGL)");
        return true;
    }

    void D3D11Context::SwapBuffers()
    {
        if (m_Initialized)
            DxGL::SwapBuffers();
    }

    void D3D11Context::SetVSync(bool enabled)
    {
        if (m_Initialized)
            DX::Core::Get().SetVSync(enabled);
    }
}

#endif
