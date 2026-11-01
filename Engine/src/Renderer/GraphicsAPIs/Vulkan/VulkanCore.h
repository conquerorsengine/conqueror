#pragma once

#ifdef CQ_VULKAN_ENABLED

#include "VulkanLoader.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

struct GLFWwindow;

namespace Conqueror::VK
{
    constexpr uint32_t kFramesInFlight = 2;

    // Kare başına kalıcı eşlenmiş halka arabellek (köşe / indeks / uniform verisi, yükleme)
    struct RingChunk
    {
        VkBuffer Buffer = VK_NULL_HANDLE;
        VkDeviceMemory Memory = VK_NULL_HANDLE;
        uint8_t* Mapped = nullptr;
        VkDeviceSize Size = 0;
        VkDeviceSize Offset = 0;
    };

    struct RingAlloc
    {
        VkBuffer Buffer = VK_NULL_HANDLE;
        VkDeviceSize Offset = 0;
        uint8_t* Ptr = nullptr;
    };

    // Vulkan 1.3 çekirdeğindeki işlevler: 1.2 sürücülerinde (MoltenVK dahil) KHR/EXT karşılıklarından yüklenir
    struct Dispatch
    {
        PFN_vkCmdBeginRendering CmdBeginRendering = nullptr;
        PFN_vkCmdEndRendering CmdEndRendering = nullptr;
        PFN_vkCmdSetCullMode CmdSetCullMode = nullptr;
        PFN_vkCmdSetFrontFace CmdSetFrontFace = nullptr;
        PFN_vkCmdSetDepthTestEnable CmdSetDepthTestEnable = nullptr;
        PFN_vkCmdSetDepthWriteEnable CmdSetDepthWriteEnable = nullptr;
        PFN_vkCmdSetDepthCompareOp CmdSetDepthCompareOp = nullptr;
        PFN_vkCmdSetStencilTestEnable CmdSetStencilTestEnable = nullptr;
        PFN_vkCmdSetStencilOp CmdSetStencilOp = nullptr;
        PFN_vkCmdSetDepthBiasEnable CmdSetDepthBiasEnable = nullptr;   // yoksa boru hattına gömülür
        PFN_vkResetQueryPool ResetQueryPool = nullptr;
    };
    extern Dispatch g_Fn;

    struct Frame
    {
        VkCommandBuffer Cmd = VK_NULL_HANDLE;
        VkFence Fence = VK_NULL_HANDLE;
        VkSemaphore ImageAvailable = VK_NULL_HANDLE;
        std::vector<RingChunk> Chunks;          // ilk parça kalıcı, taşmalar kare bitince silinir
        std::vector<std::function<void()>> Deletions;
        uint64_t Serial = 0;                    // bu kareyi başlatan küresel sayaç
    };

    // Vulkan cihazı + takas zinciri + kareler. GL çeviri katmanı (VkGL) bunun üstünde çalışır.
    class Core
    {
    public:
        static Core& Get();

        bool Init(GLFWwindow* window, std::string& error);
        void Shutdown();
        bool IsInitialized() const { return m_Device != VK_NULL_HANDLE; }

        // Kare akışı
        VkCommandBuffer BeginFrameIfNeeded();
        VkCommandBuffer Cmd() { return BeginFrameIfNeeded(); }
        bool InFrame() const { return m_InFrame; }
        // Komutları gönder ve GPU'nun bitirmesini bekle (geri okuma için); kayıt yeni komut arabelleğiyle sürer
        void FlushAndWait();
        // Kare sonu: src görüntüsünü takas zincirine (dikey çevrilmiş) kopyala ve sun
        void Present(VkImage src, uint32_t srcWidth, uint32_t srcHeight);
        void SetVSync(bool enabled);
        void OnResize();

        RingAlloc AllocRing(VkDeviceSize size, VkDeviceSize alignment);
        void DeferDelete(std::function<void()> fn);
        uint64_t FrameSerial() const { return m_Serial; }            // gönderim (batch) sayacı
        uint64_t CompletedSerial() const { return m_CompletedSerial; }
        uint64_t FrameNumber() const { return m_FrameNumber; }       // BeginFrame başına artar
        uint32_t FrameSlot() const { return m_FrameIndex; }

        uint32_t FindMemoryType(uint32_t typeBits, VkMemoryPropertyFlags props) const;
        bool CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags props, VkBuffer& buffer, VkDeviceMemory& memory, void** mapped);
        bool CreateImage(const VkImageCreateInfo& info, VkImage& image, VkDeviceMemory& memory);

        // Görüntü bariyeri: GENERAL düzeni tüm kaynaklar için; erişim sıralaması kaba (tüm komutlar)
        void FullBarrier(VkCommandBuffer cmd);
        void TransitionToGeneral(VkCommandBuffer cmd, VkImage image, VkImageAspectFlags aspect, uint32_t levels, uint32_t layers, VkImageLayout from);

        VkInstance Instance() const { return m_Instance; }
        VkPhysicalDevice Physical() const { return m_Physical; }
        VkDevice Device() const { return m_Device; }
        VkQueue Queue() const { return m_Queue; }
        uint32_t QueueFamily() const { return m_QueueFamily; }
        const VkPhysicalDeviceProperties& Properties() const { return m_Props; }
        const VkPhysicalDeviceFeatures& Features() const { return m_EnabledFeatures; }
        VkFormat DepthStencilFormat() const { return m_DepthStencilFormat; }
        bool SupportsD24S8() const { return m_D24S8; }
        VkExtent2D SwapExtent() const { return m_SwapExtent; }
        uint32_t FramebufferWidth() const;
        uint32_t FramebufferHeight() const;
        const std::string& DeviceName() const { return m_DeviceName; }
        VkPipelineCache PipelineCache() const { return m_PipelineCache; }
        uint32_t ApiVersion() const { return m_ApiVersion; }
        bool HasDynamicDepthBiasEnable() const { return g_Fn.CmdSetDepthBiasEnable != nullptr; }
        bool IsPortability() const { return m_Portability; }


    private:
        bool CreateSwapchain(std::string& error);
        void DestroySwapchain();
        void BeginFrame();
        void RetireFrame(Frame& f);

        GLFWwindow* m_Window = nullptr;
        VkInstance m_Instance = VK_NULL_HANDLE;
        VkDebugUtilsMessengerEXT m_Messenger = VK_NULL_HANDLE;
        VkSurfaceKHR m_Surface = VK_NULL_HANDLE;
        VkPhysicalDevice m_Physical = VK_NULL_HANDLE;
        VkDevice m_Device = VK_NULL_HANDLE;
        VkQueue m_Queue = VK_NULL_HANDLE;
        uint32_t m_QueueFamily = 0;
        VkPhysicalDeviceProperties m_Props{};
        VkPhysicalDeviceMemoryProperties m_MemProps{};
        VkPhysicalDeviceFeatures m_EnabledFeatures{};
        VkFormat m_DepthStencilFormat = VK_FORMAT_D32_SFLOAT_S8_UINT;
        uint32_t m_ApiVersion = 0;
        bool m_Portability = false;
        bool m_D24S8 = false;
        std::string m_DeviceName;
        VkCommandPool m_Pool = VK_NULL_HANDLE;
        VkPipelineCache m_PipelineCache = VK_NULL_HANDLE;

        VkSwapchainKHR m_Swapchain = VK_NULL_HANDLE;
        VkFormat m_SwapFormat = VK_FORMAT_B8G8R8A8_UNORM;
        VkExtent2D m_SwapExtent{ 0, 0 };
        std::vector<VkImage> m_SwapImages;
        std::vector<VkSemaphore> m_RenderFinished;   // takas görüntüsü başına
        bool m_VSync = true;
        bool m_SwapchainDirty = false;

        Frame m_Frames[kFramesInFlight];
        uint32_t m_FrameIndex = 0;
        bool m_InFrame = false;
        uint64_t m_Serial = 0;
        uint64_t m_CompletedSerial = 0;
        uint64_t m_FrameNumber = 0;
    };
}

#endif
