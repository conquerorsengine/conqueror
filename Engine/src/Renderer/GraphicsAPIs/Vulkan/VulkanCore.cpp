#ifdef CQ_VULKAN_ENABLED

#include "VulkanCore.h"
#include "Core/Logging/Log.h"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>

namespace Conqueror::VK
{
    Dispatch g_Fn;

    namespace
    {
        constexpr VkDeviceSize kRingChunkSize = 32ull * 1024 * 1024;

        VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT,
                                                     const VkDebugUtilsMessengerCallbackDataEXT* data, void*)
        {
            if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
                CQ_CORE_ERROR("[Vulkan] {0}", data->pMessage ? data->pMessage : "");
            else if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
                CQ_CORE_WARN("[Vulkan] {0}", data->pMessage ? data->pMessage : "");
            return VK_FALSE;
        }

        bool HasLayer(const char* name)
        {
            uint32_t count = 0;
            vkEnumerateInstanceLayerProperties(&count, nullptr);
            std::vector<VkLayerProperties> layers(count);
            vkEnumerateInstanceLayerProperties(&count, layers.data());
            for (const auto& l : layers)
                if (std::strcmp(l.layerName, name) == 0)
                    return true;
            return false;
        }

        bool HasInstanceExtension(const char* name)
        {
            uint32_t count = 0;
            vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
            std::vector<VkExtensionProperties> exts(count);
            vkEnumerateInstanceExtensionProperties(nullptr, &count, exts.data());
            for (const auto& e : exts)
                if (std::strcmp(e.extensionName, name) == 0)
                    return true;
            return false;
        }

        bool HasDeviceExtension(VkPhysicalDevice dev, const char* name)
        {
            uint32_t count = 0;
            vkEnumerateDeviceExtensionProperties(dev, nullptr, &count, nullptr);
            std::vector<VkExtensionProperties> exts(count);
            vkEnumerateDeviceExtensionProperties(dev, nullptr, &count, exts.data());
            for (const auto& e : exts)
                if (std::strcmp(e.extensionName, name) == 0)
                    return true;
            return false;
        }

        std::string Lower(std::string s)
        {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
            return s;
        }

        bool IsWayland()
        {
#if defined(GLFW_PLATFORM_WAYLAND)
            return glfwGetPlatform() == GLFW_PLATFORM_WAYLAND;
#else
            return false;
#endif
        }
    }

    Core& Core::Get()
    {
        static Core s;
        return s;
    }

    uint32_t Core::FindMemoryType(uint32_t typeBits, VkMemoryPropertyFlags props) const
    {
        for (uint32_t i = 0; i < m_MemProps.memoryTypeCount; ++i)
            if ((typeBits & (1u << i)) && (m_MemProps.memoryTypes[i].propertyFlags & props) == props)
                return i;
        return UINT32_MAX;
    }

    bool Core::CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags props, VkBuffer& buffer, VkDeviceMemory& memory, void** mapped)
    {
        VkBufferCreateInfo bi{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        bi.size = std::max<VkDeviceSize>(size, 16);
        bi.usage = usage;
        bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(m_Device, &bi, nullptr, &buffer) != VK_SUCCESS)
            return false;
        VkMemoryRequirements req;
        vkGetBufferMemoryRequirements(m_Device, buffer, &req);
        VkMemoryAllocateInfo ai{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        ai.allocationSize = req.size;
        ai.memoryTypeIndex = FindMemoryType(req.memoryTypeBits, props);
        if (ai.memoryTypeIndex == UINT32_MAX || vkAllocateMemory(m_Device, &ai, nullptr, &memory) != VK_SUCCESS)
        {
            vkDestroyBuffer(m_Device, buffer, nullptr);
            buffer = VK_NULL_HANDLE;
            return false;
        }
        vkBindBufferMemory(m_Device, buffer, memory, 0);
        if (mapped)
            vkMapMemory(m_Device, memory, 0, VK_WHOLE_SIZE, 0, mapped);
        return true;
    }

    bool Core::CreateImage(const VkImageCreateInfo& info, VkImage& image, VkDeviceMemory& memory)
    {
        if (vkCreateImage(m_Device, &info, nullptr, &image) != VK_SUCCESS)
            return false;
        VkMemoryRequirements req;
        vkGetImageMemoryRequirements(m_Device, image, &req);
        VkMemoryAllocateInfo ai{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        ai.allocationSize = req.size;
        ai.memoryTypeIndex = FindMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (ai.memoryTypeIndex == UINT32_MAX)
            ai.memoryTypeIndex = FindMemoryType(req.memoryTypeBits, 0);
        if (vkAllocateMemory(m_Device, &ai, nullptr, &memory) != VK_SUCCESS)
        {
            vkDestroyImage(m_Device, image, nullptr);
            image = VK_NULL_HANDLE;
            return false;
        }
        vkBindImageMemory(m_Device, image, memory, 0);
        return true;
    }

    bool Core::Init(GLFWwindow* window, std::string& error)
    {
        m_Window = window;
        // Yükleyici çalışma anında açılır: Vulkan'sız sistemde motor açılır ve OpenGL'e düşer
        if (!OpenRuntime(error))
            return false;
        if (!glfwVulkanSupported())
        {
            error = "No Vulkan loader / driver found";
            return false;
        }

        // ── Örnek ──
        uint32_t glfwCount = 0;
        const char** glfwExts = glfwGetRequiredInstanceExtensions(&glfwCount);
        std::vector<const char*> exts(glfwExts, glfwExts + glfwCount);
        std::vector<const char*> layers;
        const char* validationEnv = std::getenv("CQ_VK_VALIDATION");
        const bool validation = validationEnv && validationEnv[0] == '1';
        if (validation)
        {
            if (HasLayer("VK_LAYER_KHRONOS_validation"))
            {
                layers.push_back("VK_LAYER_KHRONOS_validation");
                exts.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
            }
            else
                CQ_CORE_WARN("[Vulkan] Validation layer requested but not installed");
        }
        // Yükleyici sürümü: 1.3 tercih edilir, 1.2 + uzantılar da kabul (MoltenVK / eski sürücüler)
        uint32_t loaderVersion = VK_API_VERSION_1_0;
        if (auto enumerate = (PFN_vkEnumerateInstanceVersion)vkGetInstanceProcAddr(nullptr, "vkEnumerateInstanceVersion"))
            enumerate(&loaderVersion);
        if (loaderVersion < VK_API_VERSION_1_2)
        {
            error = "Vulkan 1.2 or newer is required";
            return false;
        }
        VkApplicationInfo app{ VK_STRUCTURE_TYPE_APPLICATION_INFO };
        app.pApplicationName = "Conqueror Engine";
        app.pEngineName = "Conqueror Engine";
        app.apiVersion = loaderVersion >= VK_API_VERSION_1_3 ? VK_API_VERSION_1_3 : VK_API_VERSION_1_2;
        VkInstanceCreateInfo ici{ VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
        // macOS: MoltenVK taşınabilirlik sürücüsü yalnızca açıkça istenirse listelenir
        if (HasInstanceExtension(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME))
        {
            exts.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
            ici.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
        }
        ici.pApplicationInfo = &app;
        ici.enabledExtensionCount = (uint32_t)exts.size();
        ici.ppEnabledExtensionNames = exts.data();
        ici.enabledLayerCount = (uint32_t)layers.size();
        ici.ppEnabledLayerNames = layers.data();
        if (vkCreateInstance(&ici, nullptr, &m_Instance) != VK_SUCCESS)
        {
            error = "vkCreateInstance failed";
            return false;
        }
        LoadInstanceFunctions(m_Instance);
        if (!layers.empty())
        {
            auto create = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(m_Instance, "vkCreateDebugUtilsMessengerEXT");
            VkDebugUtilsMessengerCreateInfoEXT mci{ VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT };
            mci.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
            mci.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                              VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
            mci.pfnUserCallback = DebugCallback;
            if (create)
                create(m_Instance, &mci, nullptr, &m_Messenger);
        }
        if (glfwCreateWindowSurface(m_Instance, window, nullptr, &m_Surface) != VK_SUCCESS)
        {
            error = "Cannot create a Vulkan surface for the window";
            return false;
        }

        // ── Fiziksel cihaz ──
        uint32_t devCount = 0;
        vkEnumeratePhysicalDevices(m_Instance, &devCount, nullptr);
        std::vector<VkPhysicalDevice> devices(devCount);
        vkEnumeratePhysicalDevices(m_Instance, &devCount, devices.data());
        const char* wanted = std::getenv("CQ_VK_DEVICE");
        int bestScore = -1;
        for (VkPhysicalDevice dev : devices)
        {
            VkPhysicalDeviceProperties props;
            vkGetPhysicalDeviceProperties(dev, &props);
            if (props.apiVersion < VK_API_VERSION_1_2 || !HasDeviceExtension(dev, VK_KHR_SWAPCHAIN_EXTENSION_NAME))
                continue;
            // 1.2 cihazı: dinamik render + genişletilmiş dinamik durum uzantıları şart
            if ((props.apiVersion < VK_API_VERSION_1_3 || app.apiVersion < VK_API_VERSION_1_3) &&
                (!HasDeviceExtension(dev, VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME) || !HasDeviceExtension(dev, VK_EXT_EXTENDED_DYNAMIC_STATE_EXTENSION_NAME)))
                continue;
            uint32_t qCount = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(dev, &qCount, nullptr);
            std::vector<VkQueueFamilyProperties> qprops(qCount);
            vkGetPhysicalDeviceQueueFamilyProperties(dev, &qCount, qprops.data());
            int family = -1;
            for (uint32_t i = 0; i < qCount; ++i)
            {
                VkBool32 present = VK_FALSE;
                vkGetPhysicalDeviceSurfaceSupportKHR(dev, i, m_Surface, &present);
                if ((qprops[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && present)
                {
                    family = (int)i;
                    break;
                }
            }
            if (family < 0)
                continue;
            int score = props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 1000 : props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 500 :
                        props.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU ? 10 : 100;
            if (wanted && Lower(props.deviceName).find(Lower(wanted)) != std::string::npos)
                score += 100000;
            if (score > bestScore)
            {
                bestScore = score;
                m_Physical = dev;
                m_QueueFamily = (uint32_t)family;
            }
        }
        if (!m_Physical)
        {
            error = "No GPU with Vulkan 1.3 and presentation support";
            return false;
        }
        vkGetPhysicalDeviceProperties(m_Physical, &m_Props);
        vkGetPhysicalDeviceMemoryProperties(m_Physical, &m_MemProps);
        m_DeviceName = m_Props.deviceName;

        // ── Mantıksal cihaz ──
        VkPhysicalDeviceFeatures supported;
        vkGetPhysicalDeviceFeatures(m_Physical, &supported);
        m_EnabledFeatures = {};
        m_EnabledFeatures.fillModeNonSolid = supported.fillModeNonSolid;
        m_EnabledFeatures.wideLines = supported.wideLines;
        m_EnabledFeatures.samplerAnisotropy = supported.samplerAnisotropy;
        m_EnabledFeatures.independentBlend = supported.independentBlend;
        m_EnabledFeatures.depthClamp = supported.depthClamp;
        m_EnabledFeatures.depthBiasClamp = supported.depthBiasClamp;
        m_EnabledFeatures.largePoints = supported.largePoints;
        m_EnabledFeatures.shaderClipDistance = supported.shaderClipDistance;
        m_EnabledFeatures.imageCubeArray = supported.imageCubeArray;

        m_ApiVersion = std::min(m_Props.apiVersion, app.apiVersion);
        const bool core13 = m_ApiVersion >= VK_API_VERSION_1_3;
        std::vector<const char*> devExts = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
        m_Portability = HasDeviceExtension(m_Physical, "VK_KHR_portability_subset");
        if (m_Portability)
            devExts.push_back("VK_KHR_portability_subset");   // MoltenVK: etkinleştirilmesi zorunlu

        VkPhysicalDeviceVulkan13Features f13{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
        f13.dynamicRendering = VK_TRUE;
        VkPhysicalDeviceDynamicRenderingFeaturesKHR fdr{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR };
        fdr.dynamicRendering = VK_TRUE;
        VkPhysicalDeviceExtendedDynamicStateFeaturesEXT feds{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT };
        feds.extendedDynamicState = VK_TRUE;
        VkPhysicalDeviceExtendedDynamicState2FeaturesEXT feds2{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_2_FEATURES_EXT };
        feds2.extendedDynamicState2 = VK_TRUE;
        VkPhysicalDeviceVulkan12Features f12{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES };
        f12.hostQueryReset = VK_TRUE;   // zaman damgası sorguları (profilleyici) ana bilgisayardan sıfırlanır
        bool dynamicDepthBias = core13;
        if (core13)
            f12.pNext = &f13;
        else
        {
            devExts.push_back(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
            devExts.push_back(VK_EXT_EXTENDED_DYNAMIC_STATE_EXTENSION_NAME);
            f12.pNext = &fdr;
            fdr.pNext = &feds;
            if (HasDeviceExtension(m_Physical, VK_EXT_EXTENDED_DYNAMIC_STATE_2_EXTENSION_NAME))
            {
                devExts.push_back(VK_EXT_EXTENDED_DYNAMIC_STATE_2_EXTENSION_NAME);
                feds.pNext = &feds2;
                dynamicDepthBias = true;
            }
        }
        VkPhysicalDeviceFeatures2 f2{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2 };
        f2.features = m_EnabledFeatures;
        f2.pNext = &f12;

        const float priority = 1.0f;
        VkDeviceQueueCreateInfo qci{ VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
        qci.queueFamilyIndex = m_QueueFamily;
        qci.queueCount = 1;
        qci.pQueuePriorities = &priority;
        VkDeviceCreateInfo dci{ VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
        dci.pNext = &f2;
        dci.queueCreateInfoCount = 1;
        dci.pQueueCreateInfos = &qci;
        dci.enabledExtensionCount = (uint32_t)devExts.size();
        dci.ppEnabledExtensionNames = devExts.data();
        if (vkCreateDevice(m_Physical, &dci, nullptr, &m_Device) != VK_SUCCESS)
        {
            error = "vkCreateDevice failed";
            return false;
        }
        LoadDeviceFunctions(m_Device);
        vkGetDeviceQueue(m_Device, m_QueueFamily, 0, &m_Queue);

        // 1.3 çekirdek adı, yoksa uzantı adı
        auto load = [&](const char* coreName, const char* extName) -> PFN_vkVoidFunction {
            PFN_vkVoidFunction f = core13 ? vkGetDeviceProcAddr(m_Device, coreName) : nullptr;
            if (!f && extName)
                f = vkGetDeviceProcAddr(m_Device, extName);
            return f;
        };
        g_Fn.CmdBeginRendering = (PFN_vkCmdBeginRendering)load("vkCmdBeginRendering", "vkCmdBeginRenderingKHR");
        g_Fn.CmdEndRendering = (PFN_vkCmdEndRendering)load("vkCmdEndRendering", "vkCmdEndRenderingKHR");
        g_Fn.CmdSetCullMode = (PFN_vkCmdSetCullMode)load("vkCmdSetCullMode", "vkCmdSetCullModeEXT");
        g_Fn.CmdSetFrontFace = (PFN_vkCmdSetFrontFace)load("vkCmdSetFrontFace", "vkCmdSetFrontFaceEXT");
        g_Fn.CmdSetDepthTestEnable = (PFN_vkCmdSetDepthTestEnable)load("vkCmdSetDepthTestEnable", "vkCmdSetDepthTestEnableEXT");
        g_Fn.CmdSetDepthWriteEnable = (PFN_vkCmdSetDepthWriteEnable)load("vkCmdSetDepthWriteEnable", "vkCmdSetDepthWriteEnableEXT");
        g_Fn.CmdSetDepthCompareOp = (PFN_vkCmdSetDepthCompareOp)load("vkCmdSetDepthCompareOp", "vkCmdSetDepthCompareOpEXT");
        g_Fn.CmdSetStencilTestEnable = (PFN_vkCmdSetStencilTestEnable)load("vkCmdSetStencilTestEnable", "vkCmdSetStencilTestEnableEXT");
        g_Fn.CmdSetStencilOp = (PFN_vkCmdSetStencilOp)load("vkCmdSetStencilOp", "vkCmdSetStencilOpEXT");
        g_Fn.CmdSetDepthBiasEnable = dynamicDepthBias ? (PFN_vkCmdSetDepthBiasEnable)load("vkCmdSetDepthBiasEnable", "vkCmdSetDepthBiasEnableEXT") : nullptr;
        g_Fn.ResetQueryPool = (PFN_vkResetQueryPool)vkGetDeviceProcAddr(m_Device, "vkResetQueryPool");
        if (!g_Fn.ResetQueryPool)
            g_Fn.ResetQueryPool = (PFN_vkResetQueryPool)vkGetDeviceProcAddr(m_Device, "vkResetQueryPoolEXT");
        if (!g_Fn.CmdBeginRendering || !g_Fn.CmdEndRendering || !g_Fn.CmdSetCullMode || !g_Fn.CmdSetStencilOp)
        {
            error = "Required Vulkan entry points are missing (dynamic rendering / extended dynamic state)";
            return false;
        }

        // Derinlik-şablon biçimi: AMD D24S8 desteklemez
        VkFormatProperties fp;
        vkGetPhysicalDeviceFormatProperties(m_Physical, VK_FORMAT_D24_UNORM_S8_UINT, &fp);
        m_D24S8 = (fp.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0;
        m_DepthStencilFormat = m_D24S8 ? VK_FORMAT_D24_UNORM_S8_UINT : VK_FORMAT_D32_SFLOAT_S8_UINT;

        VkCommandPoolCreateInfo pci{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
        pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        pci.queueFamilyIndex = m_QueueFamily;
        vkCreateCommandPool(m_Device, &pci, nullptr, &m_Pool);

        VkPipelineCacheCreateInfo cci{ VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO };
        vkCreatePipelineCache(m_Device, &cci, nullptr, &m_PipelineCache);

        for (auto& f : m_Frames)
        {
            VkCommandBufferAllocateInfo ai{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
            ai.commandPool = m_Pool;
            ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            ai.commandBufferCount = 1;
            vkAllocateCommandBuffers(m_Device, &ai, &f.Cmd);
            VkFenceCreateInfo fi{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
            fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
            vkCreateFence(m_Device, &fi, nullptr, &f.Fence);
            VkSemaphoreCreateInfo si{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
            vkCreateSemaphore(m_Device, &si, nullptr, &f.ImageAvailable);
            RingChunk chunk;
            chunk.Size = kRingChunkSize;
            void* mapped = nullptr;
            CreateBuffer(chunk.Size,
                         VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT |
                             VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, chunk.Buffer, chunk.Memory, &mapped);
            chunk.Mapped = (uint8_t*)mapped;
            f.Chunks.push_back(chunk);
        }

        if (!CreateSwapchain(error))
            return false;
        CQ_CORE_INFO("[Vulkan] Device: {0} (API {1}.{2}.{3}, driver {4}){5}{6}", m_DeviceName, VK_API_VERSION_MAJOR(m_Props.apiVersion),
                     VK_API_VERSION_MINOR(m_Props.apiVersion), VK_API_VERSION_PATCH(m_Props.apiVersion), m_Props.driverVersion,
                     core13 ? "" : " [1.2 + extensions]", m_Portability ? " [portability]" : "");
        if (!m_Messenger && validation)
            CQ_CORE_WARN("[Vulkan] Validation requested but the debug messenger is unavailable");
        else if (m_Messenger)
            CQ_CORE_INFO("[Vulkan] Validation layer enabled");
        return true;
    }

    uint32_t Core::FramebufferWidth() const
    {
        int w = 0, h = 0;
        glfwGetFramebufferSize(m_Window, &w, &h);
        return (uint32_t)std::max(w, 1);
    }

    uint32_t Core::FramebufferHeight() const
    {
        int w = 0, h = 0;
        glfwGetFramebufferSize(m_Window, &w, &h);
        return (uint32_t)std::max(h, 1);
    }

    bool Core::CreateSwapchain(std::string& error)
    {
        VkSurfaceCapabilitiesKHR caps;
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_Physical, m_Surface, &caps);
        uint32_t formatCount = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(m_Physical, m_Surface, &formatCount, nullptr);
        std::vector<VkSurfaceFormatKHR> formats(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(m_Physical, m_Surface, &formatCount, formats.data());
        // Motor çıktısı zaten sRGB kodlu (GL varsayılan çerçeve arabelleği gibi): UNORM takas biçimi
        VkSurfaceFormatKHR chosen = formats.empty() ? VkSurfaceFormatKHR{ VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR } : formats[0];
        for (const auto& f : formats)
            if ((f.format == VK_FORMAT_B8G8R8A8_UNORM || f.format == VK_FORMAT_R8G8B8A8_UNORM) && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            {
                chosen = f;
                break;
            }
        m_SwapFormat = chosen.format;

        uint32_t modeCount = 0;
        vkGetPhysicalDeviceSurfacePresentModesKHR(m_Physical, m_Surface, &modeCount, nullptr);
        std::vector<VkPresentModeKHR> modes(modeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(m_Physical, m_Surface, &modeCount, modes.data());
        auto has = [&](VkPresentModeKHR m) { return std::find(modes.begin(), modes.end(), m) != modes.end(); };
        VkPresentModeKHR mode = VK_PRESENT_MODE_FIFO_KHR;
        // Wayland: görünmeyen pencerede FIFO sonsuza dek bekleyebilir; pencere yazılımla kare sınırlar
        if (!m_VSync || IsWayland())
            mode = has(VK_PRESENT_MODE_MAILBOX_KHR) ? VK_PRESENT_MODE_MAILBOX_KHR : has(VK_PRESENT_MODE_IMMEDIATE_KHR) ? VK_PRESENT_MODE_IMMEDIATE_KHR
                                                                                                                       : VK_PRESENT_MODE_FIFO_KHR;

        VkExtent2D extent = caps.currentExtent;
        if (extent.width == UINT32_MAX)
        {
            extent.width = std::clamp(FramebufferWidth(), caps.minImageExtent.width, caps.maxImageExtent.width);
            extent.height = std::clamp(FramebufferHeight(), caps.minImageExtent.height, caps.maxImageExtent.height);
        }
        if (extent.width == 0 || extent.height == 0)
        {
            m_SwapExtent = extent;
            return true;   // simge durumunda: sonra yeniden denenir
        }
        uint32_t imageCount = std::max(caps.minImageCount + 1, 2u);
        if (caps.maxImageCount > 0)
            imageCount = std::min(imageCount, caps.maxImageCount);

        VkSwapchainCreateInfoKHR sci{ VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR };
        sci.surface = m_Surface;
        sci.minImageCount = imageCount;
        sci.imageFormat = chosen.format;
        sci.imageColorSpace = chosen.colorSpace;
        sci.imageExtent = extent;
        sci.imageArrayLayers = 1;
        sci.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        sci.preTransform = (caps.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR) ? VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR : caps.currentTransform;
        sci.compositeAlpha = (caps.supportedCompositeAlpha & VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR) ? VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR
                                                                                                : VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
        sci.presentMode = mode;
        sci.clipped = VK_TRUE;
        sci.oldSwapchain = m_Swapchain;
        VkSwapchainKHR newSwap = VK_NULL_HANDLE;
        if (vkCreateSwapchainKHR(m_Device, &sci, nullptr, &newSwap) != VK_SUCCESS)
        {
            error = "vkCreateSwapchainKHR failed";
            return false;
        }
        DestroySwapchain();
        m_Swapchain = newSwap;
        m_SwapExtent = extent;
        uint32_t count = 0;
        vkGetSwapchainImagesKHR(m_Device, m_Swapchain, &count, nullptr);
        m_SwapImages.resize(count);
        vkGetSwapchainImagesKHR(m_Device, m_Swapchain, &count, m_SwapImages.data());
        m_RenderFinished.resize(count);
        for (auto& s : m_RenderFinished)
        {
            VkSemaphoreCreateInfo si{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
            vkCreateSemaphore(m_Device, &si, nullptr, &s);
        }
        m_SwapchainDirty = false;
        return true;
    }

    void Core::DestroySwapchain()
    {
        for (auto s : m_RenderFinished)
            vkDestroySemaphore(m_Device, s, nullptr);
        m_RenderFinished.clear();
        m_SwapImages.clear();
        if (m_Swapchain)
            vkDestroySwapchainKHR(m_Device, m_Swapchain, nullptr);
        m_Swapchain = VK_NULL_HANDLE;
    }

    void Core::Shutdown()
    {
        if (!m_Device)
        {
            // Yarım kalmış başlatma: yüzey ve örnek yine de bırakılır
            if (m_Surface)
                vkDestroySurfaceKHR(m_Instance, m_Surface, nullptr);
            m_Surface = VK_NULL_HANDLE;
            if (m_Messenger)
            {
                auto destroy = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(m_Instance, "vkDestroyDebugUtilsMessengerEXT");
                if (destroy)
                    destroy(m_Instance, m_Messenger, nullptr);
                m_Messenger = VK_NULL_HANDLE;
            }
            if (m_Instance)
                vkDestroyInstance(m_Instance, nullptr);
            m_Instance = VK_NULL_HANDLE;
            m_Physical = VK_NULL_HANDLE;
            return;
        }
        vkDeviceWaitIdle(m_Device);
        for (auto& f : m_Frames)
        {
            RetireFrame(f);
            for (auto& c : f.Chunks)
            {
                vkDestroyBuffer(m_Device, c.Buffer, nullptr);
                vkFreeMemory(m_Device, c.Memory, nullptr);
            }
            f.Chunks.clear();
            vkDestroyFence(m_Device, f.Fence, nullptr);
            vkDestroySemaphore(m_Device, f.ImageAvailable, nullptr);
        }
        DestroySwapchain();
        if (m_PipelineCache)
            vkDestroyPipelineCache(m_Device, m_PipelineCache, nullptr);
        vkDestroyCommandPool(m_Device, m_Pool, nullptr);
        vkDestroyDevice(m_Device, nullptr);
        m_Device = VK_NULL_HANDLE;
        if (m_Surface)
            vkDestroySurfaceKHR(m_Instance, m_Surface, nullptr);
        m_Surface = VK_NULL_HANDLE;
        if (m_Messenger)
        {
            auto destroy = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(m_Instance, "vkDestroyDebugUtilsMessengerEXT");
            if (destroy)
                destroy(m_Instance, m_Messenger, nullptr);
            m_Messenger = VK_NULL_HANDLE;
        }
        vkDestroyInstance(m_Instance, nullptr);
        m_Instance = VK_NULL_HANDLE;
        m_Physical = VK_NULL_HANDLE;
        m_InFrame = false;
    }

    void Core::RetireFrame(Frame& f)
    {
        for (auto& fn : f.Deletions)
            fn();
        f.Deletions.clear();
        // Taşma parçaları silinir, ilk parça sıfırlanır
        while (f.Chunks.size() > 1)
        {
            auto& c = f.Chunks.back();
            vkDestroyBuffer(m_Device, c.Buffer, nullptr);
            vkFreeMemory(m_Device, c.Memory, nullptr);
            f.Chunks.pop_back();
        }
        if (!f.Chunks.empty())
            f.Chunks[0].Offset = 0;
    }

    void Core::BeginFrame()
    {
        Frame& f = m_Frames[m_FrameIndex];
        vkWaitForFences(m_Device, 1, &f.Fence, VK_TRUE, UINT64_MAX);
        m_CompletedSerial = std::max(m_CompletedSerial, f.Serial);
        RetireFrame(f);
        vkResetCommandBuffer(f.Cmd, 0);
        VkCommandBufferBeginInfo bi{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(f.Cmd, &bi);
        f.Serial = ++m_Serial;
        ++m_FrameNumber;
        m_InFrame = true;
    }

    VkCommandBuffer Core::BeginFrameIfNeeded()
    {
        if (!m_InFrame)
            BeginFrame();
        return m_Frames[m_FrameIndex].Cmd;
    }

    void Core::FlushAndWait()
    {
        if (!m_InFrame)
            return;
        Frame& f = m_Frames[m_FrameIndex];
        vkEndCommandBuffer(f.Cmd);
        vkResetFences(m_Device, 1, &f.Fence);
        VkSubmitInfo si{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
        si.commandBufferCount = 1;
        si.pCommandBuffers = &f.Cmd;
        vkQueueSubmit(m_Queue, 1, &si, f.Fence);
        vkWaitForFences(m_Device, 1, &f.Fence, VK_TRUE, UINT64_MAX);
        m_CompletedSerial = std::max(m_CompletedSerial, f.Serial);
        // Silinmeyi bekleyen nesneler artık güvenle bırakılabilir; halka verisi korunur (aynı karede kullanılabilir)
        for (auto& fn : f.Deletions)
            fn();
        f.Deletions.clear();
        vkResetCommandBuffer(f.Cmd, 0);
        VkCommandBufferBeginInfo bi{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(f.Cmd, &bi);
        f.Serial = ++m_Serial;
    }

    RingAlloc Core::AllocRing(VkDeviceSize size, VkDeviceSize alignment)
    {
        BeginFrameIfNeeded();
        Frame& f = m_Frames[m_FrameIndex];
        alignment = std::max<VkDeviceSize>(alignment, 16);
        auto& last = f.Chunks.back();
        VkDeviceSize aligned = (last.Offset + alignment - 1) / alignment * alignment;
        if (aligned + size > last.Size)
        {
            RingChunk chunk;
            chunk.Size = std::max(kRingChunkSize, size * 2);
            void* mapped = nullptr;
            CreateBuffer(chunk.Size,
                         VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT |
                             VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, chunk.Buffer, chunk.Memory, &mapped);
            chunk.Mapped = (uint8_t*)mapped;
            f.Chunks.push_back(chunk);
            aligned = 0;
        }
        auto& c = f.Chunks.back();
        RingAlloc a{ c.Buffer, aligned, c.Mapped + aligned };
        c.Offset = aligned + size;
        return a;
    }

    void Core::DeferDelete(std::function<void()> fn)
    {
        if (!m_Device)
            return;
        if (!m_InFrame)
        {
            // Kayıt dışında: önceki karelerin bitmesini bekle (nadiren: kapanış / yükleme)
            vkDeviceWaitIdle(m_Device);
            fn();
            return;
        }
        m_Frames[m_FrameIndex].Deletions.push_back(std::move(fn));
    }

    void Core::FullBarrier(VkCommandBuffer cmd)
    {
        VkMemoryBarrier mb{ VK_STRUCTURE_TYPE_MEMORY_BARRIER };
        mb.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
        mb.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &mb, 0, nullptr, 0, nullptr);
    }

    void Core::TransitionToGeneral(VkCommandBuffer cmd, VkImage image, VkImageAspectFlags aspect, uint32_t levels, uint32_t layers, VkImageLayout from)
    {
        VkImageMemoryBarrier b{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        b.oldLayout = from;
        b.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = image;
        b.subresourceRange = { aspect, 0, levels, 0, layers };
        b.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
        b.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
    }

    void Core::SetVSync(bool enabled)
    {
        if (m_VSync == enabled)
            return;
        m_VSync = enabled;
        m_SwapchainDirty = true;
    }

    void Core::OnResize()
    {
        m_SwapchainDirty = true;
    }

    void Core::Present(VkImage src, uint32_t srcWidth, uint32_t srcHeight)
    {
        VkCommandBuffer cmd = BeginFrameIfNeeded();
        Frame& f = m_Frames[m_FrameIndex];

        if (m_SwapchainDirty || m_SwapExtent.width != FramebufferWidth() || m_SwapExtent.height != FramebufferHeight())
        {
            // Bekleyen komutları gönder, sonra takas zincirini yeniden kur
            FlushAndWait();
            vkDeviceWaitIdle(m_Device);
            std::string err;
            if (!CreateSwapchain(err))
                CQ_CORE_ERROR("[Vulkan] {0}", err);
            cmd = f.Cmd;
        }

        uint32_t imageIndex = UINT32_MAX;
        bool acquired = false;
        if (m_Swapchain && m_SwapExtent.width > 0)
        {
            const VkResult r = vkAcquireNextImageKHR(m_Device, m_Swapchain, UINT64_MAX, f.ImageAvailable, VK_NULL_HANDLE, &imageIndex);
            acquired = r == VK_SUCCESS || r == VK_SUBOPTIMAL_KHR;
            if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR)
                m_SwapchainDirty = true;
        }

        if (acquired)
        {
            VkImage dst = m_SwapImages[imageIndex];
            FullBarrier(cmd);
            VkImageMemoryBarrier b{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
            b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.image = dst;
            b.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
            b.srcAccessMask = 0;
            b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
            // GL yönü (satır 0 = alt) -> ekran (satır 0 = üst): dikey çevir
            VkImageBlit blit{};
            blit.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
            blit.srcOffsets[0] = { 0, (int32_t)srcHeight, 0 };
            blit.srcOffsets[1] = { (int32_t)srcWidth, 0, 1 };
            blit.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
            blit.dstOffsets[0] = { 0, 0, 0 };
            blit.dstOffsets[1] = { (int32_t)m_SwapExtent.width, (int32_t)m_SwapExtent.height, 1 };
            const bool same = srcWidth == m_SwapExtent.width && srcHeight == m_SwapExtent.height;
            vkCmdBlitImage(cmd, src, VK_IMAGE_LAYOUT_GENERAL, dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, same ? VK_FILTER_NEAREST : VK_FILTER_LINEAR);
            b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            b.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
            b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            b.dstAccessMask = 0;
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
        }

        vkEndCommandBuffer(cmd);
        vkResetFences(m_Device, 1, &f.Fence);
        VkSubmitInfo si{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
        si.commandBufferCount = 1;
        si.pCommandBuffers = &cmd;
        VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        if (acquired)
        {
            si.waitSemaphoreCount = 1;
            si.pWaitSemaphores = &f.ImageAvailable;
            si.pWaitDstStageMask = &waitStage;
            si.signalSemaphoreCount = 1;
            si.pSignalSemaphores = &m_RenderFinished[imageIndex];
        }
        vkQueueSubmit(m_Queue, 1, &si, f.Fence);
        if (acquired)
        {
            VkPresentInfoKHR pi{ VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
            pi.waitSemaphoreCount = 1;
            pi.pWaitSemaphores = &m_RenderFinished[imageIndex];
            pi.swapchainCount = 1;
            pi.pSwapchains = &m_Swapchain;
            pi.pImageIndices = &imageIndex;
            const VkResult r = vkQueuePresentKHR(m_Queue, &pi);
            if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR)
                m_SwapchainDirty = true;
        }
        m_InFrame = false;
        m_FrameIndex = (m_FrameIndex + 1) % kFramesInFlight;
    }
}

#endif
