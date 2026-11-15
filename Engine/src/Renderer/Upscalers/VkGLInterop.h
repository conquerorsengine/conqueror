#pragma once

#include "Core/Base/Base.h"

#ifdef CQ_VULKAN_ENABLED

#include "Renderer/GraphicsAPIs/Vulkan/VulkanLoader.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

// OpenGL <-> Vulkan görüntü paylaşımı: OpenGL ile çizen motorda yalnız Vulkan / Direct3D'de çalışan üretici yükselticileri (NVIDIA DLSS,
// Intel XeSS) için. GL ile aynı GPU'da (aygıt UUID'si eşleşir) ayrı bir Vulkan aygıtı açılır; görüntüler Vulkan'da dışa aktarılabilir
// bellekle oluşturulup GL'e içe aktarılır (GL_EXT_memory_object), iki API dışa aktarılan semaforlarla sıralanır (GL_EXT_semaphore).
// Linux: dosya tanıtıcıları (OPAQUE_FD), Windows: NT tanıtıcıları (OPAQUE_WIN32).
namespace Conqueror::VkGLInterop
{
    struct Image
    {
        VkImage Handle = VK_NULL_HANDLE;
        VkDeviceMemory Memory = VK_NULL_HANDLE;
        VkImageView View = VK_NULL_HANDLE;
        VkFormat Format = VK_FORMAT_UNDEFINED;
        uint32_t Width = 0, Height = 0;
        uint32_t GLTexture = 0;   // aynı belleği kullanan GL dokusu
        uint32_t GLMemory = 0;    // GL bellek nesnesi
    };

    // Yükselticinin istediği ek Vulkan uzantıları (ör. NVSDK_NGX_VULKAN_RequiredExtensions)
    struct Requirements
    {
        std::vector<std::string> InstanceExtensions;
        std::vector<std::string> DeviceExtensions;
        uint32_t ApiVersion = 0;               // 0: Vulkan 1.2
        void* FeatureChain = nullptr;          // vkCreateDevice pNext zinciri (ör. xessVKGetRequiredDeviceFeatures)
        // Fiziksel aygıt seçildikten sonra, aygıt oluşturulmadan önce: aygıta bağlı uzantılar / özellikler (XeSS bunları aygıta göre verir)
        std::function<bool(VkInstance, VkPhysicalDevice, Requirements&, std::string& error)> BeforeDevice;
    };

    // GL'in kullandığı GPU'nun Vulkan karşılığı + paylaşım (yalnız OpenGL arka ucunda; tek örnek)
    class Device
    {
    public:
        static Device& Get();

        // GL bağlamı geçerliyken çağrılır. Uzantılar yoksa (GL_EXT_memory_object / semaphore, Vulkan sürücüsü) false + neden
        bool Init(const Requirements& extra, std::string& error);
        void Shutdown();
        bool IsReady() const { return m_Device != VK_NULL_HANDLE; }

        bool CreateImage(Image& image, uint32_t width, uint32_t height, VkFormat format, uint32_t glInternalFormat, std::string& error);
        void DestroyImage(Image& image);

        // GL'in yazdığı görüntüler Vulkan'a geçer, record komutları kaydeder (görüntüler VK_IMAGE_LAYOUT_GENERAL), sonra GL bekler
        bool Execute(const std::vector<Image*>& images, const std::function<bool(VkCommandBuffer)>& record, std::string& error);
        // Bekleyen Vulkan işinin bitmesini bekler (yeniden boyutlandırma / kapanış)
        void WaitIdle();
        // GL derinlik dokusu (D24S8 / D32F) -> paylaşımlı R32F görüntü (yükselticiler derinliği renk görüntüsü olarak okur; Vulkan'da
        // D24S8 her GPU'da yok). GL durumu (program, çerçeve arabelleği, görüntü alanı, VAO) korunur
        void CopyDepth(uint32_t glDepthTexture, Image& destination);

        VkInstance Instance() const { return m_Instance; }
        VkPhysicalDevice Physical() const { return m_Physical; }
        VkDevice Handle() const { return m_Device; }
        VkQueue Queue() const { return m_Queue; }
        uint32_t QueueFamily() const { return m_QueueFamily; }
        const std::string& Name() const { return m_Name; }

    private:
        bool CreateSemaphorePair(VkSemaphore& vk, uint32_t& gl, std::string& error);
        bool Transition(Image& image, std::string& error);

        VkInstance m_Instance = VK_NULL_HANDLE;
        VkPhysicalDevice m_Physical = VK_NULL_HANDLE;
        VkDevice m_Device = VK_NULL_HANDLE;
        VkQueue m_Queue = VK_NULL_HANDLE;
        uint32_t m_QueueFamily = 0;
        VkCommandPool m_Pool = VK_NULL_HANDLE;
        static constexpr int kFrames = 2;
        VkCommandBuffer m_Commands[kFrames] = {};
        VkFence m_Fences[kFrames] = {};
        int m_Frame = 0;
        VkSemaphore m_VkReady = VK_NULL_HANDLE, m_VkDone = VK_NULL_HANDLE;   // GL -> Vulkan, Vulkan -> GL
        uint32_t m_GLReady = 0, m_GLDone = 0;
        std::string m_Name;
        VkPhysicalDeviceMemoryProperties m_MemoryProps{};
    };

    // Paylaşımı uçtan uca sınar (DLSS / XeSS olmadan, her GPU'da): GL dokusuna desen yazılır, Vulkan iki katına büyütür, GL geri okur
    CQ_API bool SelfTest(std::string& report);
}

#endif
