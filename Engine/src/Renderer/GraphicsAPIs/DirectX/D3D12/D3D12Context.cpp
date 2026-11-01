#ifdef CQ_D3D12_ENABLED

#include "D3D12Context.h"
#include "D3D12Core.h"
#include "D12GL.h"
#include "Core/Logging/Log.h"

namespace Conqueror
{
    D3D12Context::D3D12Context(GLFWwindow* windowHandle)
        : m_WindowHandle(windowHandle)
    {
    }

    D3D12Context::~D3D12Context()
    {
        if (!m_Initialized)
            return;
        D12GL::Shutdown();
        DX12::Core::Get().Shutdown();
    }

    bool D3D12Context::Init()
    {
        std::string error;
        auto& core = DX12::Core::Get();
        if (!core.Init(m_WindowHandle, error))
        {
            CQ_CORE_ERROR("[D3D12] Initialization failed: {0}", error);
            core.Shutdown();
            return false;
        }
        if (!D12GL::Install(error))
        {
            CQ_CORE_ERROR("[D3D12] GL translation layer failed: {0}", error);
            D12GL::Shutdown();
            core.Shutdown();
            return false;
        }
        m_Initialized = true;
        CQ_CORE_INFO("Direct3D 12 Info:");
        CQ_CORE_INFO("  Device: {0}", core.DeviceName());
        CQ_CORE_INFO("  Backend: GL 4.6 -> Direct3D 12 translation (D12GL)");
        return true;
    }

    void D3D12Context::SwapBuffers()
    {
        if (m_Initialized)
            D12GL::SwapBuffers();
    }

    void D3D12Context::SetVSync(bool enabled)
    {
        if (m_Initialized)
            DX12::Core::Get().SetVSync(enabled);
    }
}

#endif
