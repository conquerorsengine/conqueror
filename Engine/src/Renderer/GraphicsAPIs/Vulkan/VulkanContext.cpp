#ifdef CQ_VULKAN_ENABLED

#include "VulkanContext.h"
#include "VulkanCore.h"
#include "VkGL.h"
#include "Core/Logging/Log.h"

namespace Conqueror
{
    VulkanContext::VulkanContext(GLFWwindow* windowHandle)
        : m_WindowHandle(windowHandle)
    {
    }

    VulkanContext::~VulkanContext()
    {
        if (!m_Initialized)
            return;
        VkGL::Shutdown();
        VK::Core::Get().Shutdown();
    }

    bool VulkanContext::Init()
    {
        std::string error;
        auto& core = VK::Core::Get();
        if (!core.Init(m_WindowHandle, error))
        {
            CQ_CORE_ERROR("[Vulkan] Initialization failed: {0}", error);
            core.Shutdown();
            return false;
        }
        if (!VkGL::Install(error))
        {
            CQ_CORE_ERROR("[Vulkan] GL translation layer failed: {0}", error);
            VkGL::Shutdown();
            core.Shutdown();
            return false;
        }
        m_Initialized = true;
        CQ_CORE_INFO("Vulkan Info:");
        CQ_CORE_INFO("  Device: {0}", core.DeviceName());
        CQ_CORE_INFO("  Backend: GL 4.6 -> Vulkan 1.3 translation (VkGL)");
        return true;
    }

    void VulkanContext::SwapBuffers()
    {
        if (m_Initialized)
            VkGL::SwapBuffers();
    }

    void VulkanContext::SetVSync(bool enabled)
    {
        if (m_Initialized)
            VK::Core::Get().SetVSync(enabled);
    }
}

#endif
