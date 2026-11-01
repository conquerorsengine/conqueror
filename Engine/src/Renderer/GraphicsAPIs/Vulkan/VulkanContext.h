#pragma once

#ifdef CQ_VULKAN_ENABLED

#include "Renderer/RHI/GraphicsContext.h"

struct GLFWwindow;

namespace Conqueror
{
    // Vulkan arka ucu: cihaz + takas zinciri (VK::Core) ve GL çeviri katmanı (VkGL)
    class VulkanContext : public GraphicsContext
    {
    public:
        explicit VulkanContext(GLFWwindow* windowHandle);
        ~VulkanContext() override;

        bool Init() override;
        void SwapBuffers() override;
        void SetVSync(bool enabled) override;
        const char* GetName() const override { return "Vulkan"; }

    private:
        GLFWwindow* m_WindowHandle = nullptr;
        bool m_Initialized = false;
    };
}

#endif
