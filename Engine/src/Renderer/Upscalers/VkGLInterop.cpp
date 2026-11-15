#include "VkGLInterop.h"

#ifdef CQ_VULKAN_ENABLED

#include "Core/Logging/Log.h"
#include "Renderer/RHI/RendererAPI.h"

#include <glad/glad.h>

#if defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
    #include <vulkan/vulkan_win32.h>
#else
    #include <unistd.h>
#endif

#include <algorithm>
#include <cstring>

namespace Conqueror::VkGLInterop
{
    namespace
    {
        // Yükleyici tablosunda olmayan işlevler (yalnız bu katman kullanır)
        PFN_vkGetPhysicalDeviceProperties2 s_GetProperties2 = nullptr;
#if defined(_WIN32)
        PFN_vkGetMemoryWin32HandleKHR s_GetMemoryHandle = nullptr;
        PFN_vkGetSemaphoreWin32HandleKHR s_GetSemaphoreHandle = nullptr;
        constexpr VkExternalMemoryHandleTypeFlagBits kMemoryHandle = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT;
        constexpr VkExternalSemaphoreHandleTypeFlagBits kSemaphoreHandle = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT;
        const char* kMemoryExtension = VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME;
        const char* kSemaphoreExtension = VK_KHR_EXTERNAL_SEMAPHORE_WIN32_EXTENSION_NAME;
#else
        PFN_vkGetMemoryFdKHR s_GetMemoryFd = nullptr;
        PFN_vkGetSemaphoreFdKHR s_GetSemaphoreFd = nullptr;
        constexpr VkExternalMemoryHandleTypeFlagBits kMemoryHandle = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT;
        constexpr VkExternalSemaphoreHandleTypeFlagBits kSemaphoreHandle = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;
        const char* kMemoryExtension = VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME;
        const char* kSemaphoreExtension = VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME;
#endif

        bool HasGLInterop()
        {
#if defined(_WIN32)
            return GLAD_GL_EXT_memory_object && GLAD_GL_EXT_memory_object_win32 && GLAD_GL_EXT_semaphore && GLAD_GL_EXT_semaphore_win32 &&
                   glImportMemoryWin32HandleEXT && glImportSemaphoreWin32HandleEXT && glTextureStorageMem2DEXT;
#else
            return GLAD_GL_EXT_memory_object && GLAD_GL_EXT_memory_object_fd && GLAD_GL_EXT_semaphore && GLAD_GL_EXT_semaphore_fd &&
                   glImportMemoryFdEXT && glImportSemaphoreFdEXT && glTextureStorageMem2DEXT;
#endif
        }

        bool HasExtension(const std::vector<VkExtensionProperties>& list, const char* name)
        {
            for (const auto& e : list)
                if (std::strcmp(e.extensionName, name) == 0)
                    return true;
            return false;
        }
    }

    Device& Device::Get()
    {
        static Device s;
        return s;
    }

    bool Device::Init(const Requirements& extra, std::string& error)
    {
        if (IsReady())
            return true;
        if (RendererAPI::GetBackend() != RendererAPI::Backend::OpenGL)
        {
            error = "OpenGL <-> Vulkan sharing is used only with the OpenGL backend";
            return false;
        }
        if (!HasGLInterop())
        {
            error = "The OpenGL driver has no GL_EXT_memory_object / GL_EXT_semaphore (needed to share images with Vulkan)";
            return false;
        }
        if (!VK::OpenRuntime(error))
            return false;

        // GL'in GPU'su: aygıt UUID'si
        uint8_t glUUID[GL_UUID_SIZE_EXT] = {};
        glGetUnsignedBytei_vEXT(GL_DEVICE_UUID_EXT, 0, glUUID);

        std::vector<const char*> instanceExt;
        for (const auto& e : extra.InstanceExtensions)
            instanceExt.push_back(e.c_str());
        VkApplicationInfo app{ VK_STRUCTURE_TYPE_APPLICATION_INFO };
        app.pApplicationName = "Conqueror Engine";
        app.pEngineName = "Conqueror Engine";
        app.apiVersion = std::max<uint32_t>(VK_API_VERSION_1_2, extra.ApiVersion);
        VkInstanceCreateInfo ici{ VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
        ici.pApplicationInfo = &app;
        ici.enabledExtensionCount = (uint32_t)instanceExt.size();
        ici.ppEnabledExtensionNames = instanceExt.data();
        if (vkCreateInstance(&ici, nullptr, &m_Instance) != VK_SUCCESS)
        {
            error = "vkCreateInstance failed";
            return false;
        }
        VK::LoadInstanceFunctions(m_Instance);
        s_GetProperties2 = (PFN_vkGetPhysicalDeviceProperties2)cq_vkGetInstanceProcAddr(m_Instance, "vkGetPhysicalDeviceProperties2");

        uint32_t count = 0;
        vkEnumeratePhysicalDevices(m_Instance, &count, nullptr);
        std::vector<VkPhysicalDevice> devices(count);
        vkEnumeratePhysicalDevices(m_Instance, &count, devices.data());
        for (VkPhysicalDevice pd : devices)
        {
            VkPhysicalDeviceIDProperties id{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES };
            VkPhysicalDeviceProperties2 props{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
            props.pNext = &id;
            if (s_GetProperties2)
                s_GetProperties2(pd, &props);
            if (std::memcmp(id.deviceUUID, glUUID, VK_UUID_SIZE) == 0)
            {
                m_Physical = pd;
                m_Name = props.properties.deviceName;
                break;
            }
        }
        if (!m_Physical)
        {
            error = "No Vulkan device matches the OpenGL GPU (device UUID)";
            Shutdown();
            return false;
        }

        Requirements req = extra;
        if (req.BeforeDevice && !req.BeforeDevice(m_Instance, m_Physical, req, error))
        {
            Shutdown();
            return false;
        }
        uint32_t extCount = 0;
        vkEnumerateDeviceExtensionProperties(m_Physical, nullptr, &extCount, nullptr);
        std::vector<VkExtensionProperties> available(extCount);
        vkEnumerateDeviceExtensionProperties(m_Physical, nullptr, &extCount, available.data());
        std::vector<const char*> deviceExt = { kMemoryExtension, kSemaphoreExtension };
        for (const auto& e : req.DeviceExtensions)
            if (std::find_if(deviceExt.begin(), deviceExt.end(), [&](const char* n) { return e == n; }) == deviceExt.end())
                deviceExt.push_back(e.c_str());
        for (const char* e : deviceExt)
            if (!HasExtension(available, e))
            {
                error = std::string("The Vulkan driver lacks ") + e;
                Shutdown();
                return false;
            }

        uint32_t familyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(m_Physical, &familyCount, nullptr);
        std::vector<VkQueueFamilyProperties> families(familyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(m_Physical, &familyCount, families.data());
        bool found = false;
        for (uint32_t i = 0; i < familyCount && !found; ++i)
            if ((families[i].queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) == (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT))
                m_QueueFamily = i, found = true;
        const float priority = 1.0f;
        VkDeviceQueueCreateInfo qci{ VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
        qci.queueFamilyIndex = m_QueueFamily;
        qci.queueCount = 1;
        qci.pQueuePriorities = &priority;
        VkDeviceCreateInfo dci{ VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
        dci.pNext = req.FeatureChain;
        dci.queueCreateInfoCount = 1;
        dci.pQueueCreateInfos = &qci;
        dci.enabledExtensionCount = (uint32_t)deviceExt.size();
        dci.ppEnabledExtensionNames = deviceExt.data();
        if (vkCreateDevice(m_Physical, &dci, nullptr, &m_Device) != VK_SUCCESS)
        {
            error = "vkCreateDevice failed";
            Shutdown();
            return false;
        }
        VK::LoadDeviceFunctions(m_Device);
#if defined(_WIN32)
        s_GetMemoryHandle = (PFN_vkGetMemoryWin32HandleKHR)vkGetDeviceProcAddr(m_Device, "vkGetMemoryWin32HandleKHR");
        s_GetSemaphoreHandle = (PFN_vkGetSemaphoreWin32HandleKHR)vkGetDeviceProcAddr(m_Device, "vkGetSemaphoreWin32HandleKHR");
        if (!s_GetMemoryHandle || !s_GetSemaphoreHandle)
#else
        s_GetMemoryFd = (PFN_vkGetMemoryFdKHR)vkGetDeviceProcAddr(m_Device, "vkGetMemoryFdKHR");
        s_GetSemaphoreFd = (PFN_vkGetSemaphoreFdKHR)vkGetDeviceProcAddr(m_Device, "vkGetSemaphoreFdKHR");
        if (!s_GetMemoryFd || !s_GetSemaphoreFd)
#endif
        {
            error = "External memory functions missing in the Vulkan driver";
            Shutdown();
            return false;
        }
        vkGetDeviceQueue(m_Device, m_QueueFamily, 0, &m_Queue);
        vkGetPhysicalDeviceMemoryProperties(m_Physical, &m_MemoryProps);

        VkCommandPoolCreateInfo pci{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
        pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        pci.queueFamilyIndex = m_QueueFamily;
        vkCreateCommandPool(m_Device, &pci, nullptr, &m_Pool);
        VkCommandBufferAllocateInfo cai{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
        cai.commandPool = m_Pool;
        cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cai.commandBufferCount = kFrames;
        vkAllocateCommandBuffers(m_Device, &cai, m_Commands);
        for (VkFence& f : m_Fences)
        {
            VkFenceCreateInfo fci{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
            fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
            vkCreateFence(m_Device, &fci, nullptr, &f);
        }
        if (!CreateSemaphorePair(m_VkReady, m_GLReady, error) || !CreateSemaphorePair(m_VkDone, m_GLDone, error))
        {
            Shutdown();
            return false;
        }
        CQ_CORE_INFO("[VkGLInterop] OpenGL <-> Vulkan sharing on {0}", m_Name);
        return true;
    }

    bool Device::CreateSemaphorePair(VkSemaphore& vk, uint32_t& gl, std::string& error)
    {
        VkExportSemaphoreCreateInfo export_{ VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO };
        export_.handleTypes = kSemaphoreHandle;
        VkSemaphoreCreateInfo sci{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
        sci.pNext = &export_;
        if (vkCreateSemaphore(m_Device, &sci, nullptr, &vk) != VK_SUCCESS)
        {
            error = "Cannot create an exportable Vulkan semaphore";
            return false;
        }
        glGenSemaphoresEXT(1, &gl);
#if defined(_WIN32)
        VkSemaphoreGetWin32HandleInfoKHR gi{ VK_STRUCTURE_TYPE_SEMAPHORE_GET_WIN32_HANDLE_INFO_KHR };
        gi.semaphore = vk;
        gi.handleType = kSemaphoreHandle;
        HANDLE handle = nullptr;
        if (s_GetSemaphoreHandle(m_Device, &gi, &handle) != VK_SUCCESS)
        {
            error = "Cannot export the Vulkan semaphore";
            return false;
        }
        glImportSemaphoreWin32HandleEXT(gl, GL_HANDLE_TYPE_OPAQUE_WIN32_EXT, handle);
        CloseHandle(handle);
#else
        VkSemaphoreGetFdInfoKHR gi{ VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR };
        gi.semaphore = vk;
        gi.handleType = kSemaphoreHandle;
        int fd = -1;
        if (s_GetSemaphoreFd(m_Device, &gi, &fd) != VK_SUCCESS)
        {
            error = "Cannot export the Vulkan semaphore";
            return false;
        }
        glImportSemaphoreFdEXT(gl, GL_HANDLE_TYPE_OPAQUE_FD_EXT, fd);   // tanıtıcının sahibi artık GL
#endif
        return glIsSemaphoreEXT(gl) == GL_TRUE;
    }

    bool Device::CreateImage(Image& image, uint32_t width, uint32_t height, VkFormat format, uint32_t glInternalFormat, std::string& error)
    {
        DestroyImage(image);
        VkExternalMemoryImageCreateInfo external{ VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO };
        external.handleTypes = kMemoryHandle;
        VkImageCreateInfo ii{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
        ii.pNext = &external;
        ii.imageType = VK_IMAGE_TYPE_2D;
        ii.format = format;
        ii.extent = { width, height, 1 };
        ii.mipLevels = 1;
        ii.arrayLayers = 1;
        ii.samples = VK_SAMPLE_COUNT_1_BIT;
        ii.tiling = VK_IMAGE_TILING_OPTIMAL;
        ii.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (vkCreateImage(m_Device, &ii, nullptr, &image.Handle) != VK_SUCCESS)
        {
            error = "vkCreateImage (shared) failed";
            return false;
        }
        VkMemoryRequirements req{};
        vkGetImageMemoryRequirements(m_Device, image.Handle, &req);
        uint32_t typeIndex = UINT32_MAX;
        for (uint32_t i = 0; i < m_MemoryProps.memoryTypeCount; ++i)
            if ((req.memoryTypeBits & (1u << i)) && (m_MemoryProps.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
            {
                typeIndex = i;
                break;
            }
        VkMemoryDedicatedAllocateInfo dedicated{ VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO };
        dedicated.image = image.Handle;
        VkExportMemoryAllocateInfo exportInfo{ VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO };
        exportInfo.pNext = &dedicated;
        exportInfo.handleTypes = kMemoryHandle;
        VkMemoryAllocateInfo mai{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        mai.pNext = &exportInfo;
        mai.allocationSize = req.size;
        mai.memoryTypeIndex = typeIndex;
        if (typeIndex == UINT32_MAX || vkAllocateMemory(m_Device, &mai, nullptr, &image.Memory) != VK_SUCCESS)
        {
            error = "Cannot allocate exportable Vulkan image memory";
            DestroyImage(image);
            return false;
        }
        vkBindImageMemory(m_Device, image.Handle, image.Memory, 0);
        VkImageViewCreateInfo vi{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        vi.image = image.Handle;
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = format;
        vi.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        vkCreateImageView(m_Device, &vi, nullptr, &image.View);
        image.Format = format;
        image.Width = width;
        image.Height = height;

        // GL: aynı bellek (adanmış nesne) + aynı biçimde doku
        glCreateMemoryObjectsEXT(1, &image.GLMemory);
        const GLint isDedicated = GL_TRUE;
        glMemoryObjectParameterivEXT(image.GLMemory, GL_DEDICATED_MEMORY_OBJECT_EXT, &isDedicated);
#if defined(_WIN32)
        VkMemoryGetWin32HandleInfoKHR gi{ VK_STRUCTURE_TYPE_MEMORY_GET_WIN32_HANDLE_INFO_KHR };
        gi.memory = image.Memory;
        gi.handleType = kMemoryHandle;
        HANDLE handle = nullptr;
        if (s_GetMemoryHandle(m_Device, &gi, &handle) != VK_SUCCESS)
        {
            error = "Cannot export the Vulkan image memory";
            DestroyImage(image);
            return false;
        }
        glImportMemoryWin32HandleEXT(image.GLMemory, req.size, GL_HANDLE_TYPE_OPAQUE_WIN32_EXT, handle);
        CloseHandle(handle);
#else
        VkMemoryGetFdInfoKHR gi{ VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR };
        gi.memory = image.Memory;
        gi.handleType = kMemoryHandle;
        int fd = -1;
        if (s_GetMemoryFd(m_Device, &gi, &fd) != VK_SUCCESS)
        {
            error = "Cannot export the Vulkan image memory";
            DestroyImage(image);
            return false;
        }
        glImportMemoryFdEXT(image.GLMemory, req.size, GL_HANDLE_TYPE_OPAQUE_FD_EXT, fd);
#endif
        glCreateTextures(GL_TEXTURE_2D, 1, &image.GLTexture);
        glTextureStorageMem2DEXT(image.GLTexture, 1, (GLenum)glInternalFormat, (GLsizei)width, (GLsizei)height, image.GLMemory, 0);
        glTextureParameteri(image.GLTexture, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTextureParameteri(image.GLTexture, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTextureParameteri(image.GLTexture, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTextureParameteri(image.GLTexture, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        if (glGetError() != GL_NO_ERROR)
        {
            error = "The OpenGL driver could not import the Vulkan image";
            DestroyImage(image);
            return false;
        }
        return Transition(image, error);
    }

    // İlk kullanımdan önce GENERAL düzenine (içerik henüz yok); ondan sonra her iki API de GENERAL'da kullanır
    bool Device::Transition(Image& image, std::string& error)
    {
        const int f = m_Frame;
        vkWaitForFences(m_Device, 1, &m_Fences[f], VK_TRUE, UINT64_MAX);
        vkResetFences(m_Device, 1, &m_Fences[f]);
        VkCommandBuffer cb = m_Commands[f];
        vkResetCommandBuffer(cb, 0);
        VkCommandBufferBeginInfo bi{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cb, &bi);
        VkImageMemoryBarrier b{ VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
        b.srcAccessMask = 0;
        b.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        b.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = image.Handle;
        b.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
        vkEndCommandBuffer(cb);
        VkSubmitInfo si{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
        si.commandBufferCount = 1;
        si.pCommandBuffers = &cb;
        const VkResult r = vkQueueSubmit(m_Queue, 1, &si, m_Fences[f]);
        vkWaitForFences(m_Device, 1, &m_Fences[f], VK_TRUE, UINT64_MAX);
        if (r != VK_SUCCESS)
        {
            error = "Vulkan image transition failed";
            return false;
        }
        return true;
    }

    void Device::DestroyImage(Image& image)
    {
        if (image.GLTexture)
            glDeleteTextures(1, &image.GLTexture);
        if (image.GLMemory)
            glDeleteMemoryObjectsEXT(1, &image.GLMemory);
        if (m_Device)
        {
            if (image.View)
                vkDestroyImageView(m_Device, image.View, nullptr);
            if (image.Handle)
                vkDestroyImage(m_Device, image.Handle, nullptr);
            if (image.Memory)
                vkFreeMemory(m_Device, image.Memory, nullptr);
        }
        image = Image{};
    }

    bool Device::Execute(const std::vector<Image*>& images, const std::function<bool(VkCommandBuffer)>& record, std::string& error)
    {
        if (!IsReady())
        {
            error = "Vulkan sharing is not initialized";
            return false;
        }
        std::vector<GLuint> textures;
        std::vector<GLenum> layouts;
        for (Image* img : images)
            if (img && img->GLTexture)
            {
                textures.push_back(img->GLTexture);
                layouts.push_back(GL_LAYOUT_GENERAL_EXT);
            }
        // GL: buraya kadarki yazmalar bitince "hazır"
        glSignalSemaphoreEXT(m_GLReady, 0, nullptr, (GLuint)textures.size(), textures.data(), layouts.data());
        glFlush();

        const int f = m_Frame;
        m_Frame = (m_Frame + 1) % kFrames;
        vkWaitForFences(m_Device, 1, &m_Fences[f], VK_TRUE, UINT64_MAX);
        vkResetFences(m_Device, 1, &m_Fences[f]);
        VkCommandBuffer cb = m_Commands[f];
        vkResetCommandBuffer(cb, 0);
        VkCommandBufferBeginInfo bi{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cb, &bi);
        const bool ok = record(cb);
        // Vulkan yazmaları GL okumadan önce görünür olsun
        VkMemoryBarrier mb{ VK_STRUCTURE_TYPE_MEMORY_BARRIER };
        mb.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
        mb.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 1, &mb, 0, nullptr, 0, nullptr);
        vkEndCommandBuffer(cb);
        const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        VkSubmitInfo si{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
        si.waitSemaphoreCount = 1;
        si.pWaitSemaphores = &m_VkReady;
        si.pWaitDstStageMask = &waitStage;
        si.commandBufferCount = 1;
        si.pCommandBuffers = &cb;
        si.signalSemaphoreCount = 1;
        si.pSignalSemaphores = &m_VkDone;
        if (vkQueueSubmit(m_Queue, 1, &si, m_Fences[f]) != VK_SUCCESS)
        {
            error = "vkQueueSubmit failed";
            return false;
        }
        // GL: Vulkan bitirene kadar bu görüntülere dokunmaz
        glWaitSemaphoreEXT(m_GLDone, 0, nullptr, (GLuint)textures.size(), textures.data(), layouts.data());
        if (!ok)
            error = "Recording the Vulkan commands failed";
        return ok;
    }

    void Device::WaitIdle()
    {
        if (m_Device)
            vkDeviceWaitIdle(m_Device);
    }

    void Device::CopyDepth(uint32_t glDepthTexture, Image& destination)
    {
        static GLuint s_Program = 0, s_Framebuffer = 0, s_VAO = 0;
        if (!s_Program)
        {
            const char* vsSrc = "#version 450 core\nvoid main(){ vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2); gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0); }\n";
            const char* fsSrc = "#version 450 core\nlayout(location = 0) out float o_Depth;\nlayout(binding = 0) uniform sampler2D u_Depth;\n"
                                "void main(){ o_Depth = texelFetch(u_Depth, ivec2(gl_FragCoord.xy), 0).r; }\n";
            auto compile = [](GLenum type, const char* src) {
                GLuint sh = glCreateShader(type);
                glShaderSource(sh, 1, &src, nullptr);
                glCompileShader(sh);
                return sh;
            };
            const GLuint vs = compile(GL_VERTEX_SHADER, vsSrc), fs = compile(GL_FRAGMENT_SHADER, fsSrc);
            s_Program = glCreateProgram();
            glAttachShader(s_Program, vs);
            glAttachShader(s_Program, fs);
            glLinkProgram(s_Program);
            glDeleteShader(vs);
            glDeleteShader(fs);
            glCreateFramebuffers(1, &s_Framebuffer);
            glCreateVertexArrays(1, &s_VAO);
        }
        GLint prevProgram = 0, prevFramebuffer = 0, prevVAO = 0, prevViewport[4] = {};
        glGetIntegerv(GL_CURRENT_PROGRAM, &prevProgram);
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevFramebuffer);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVAO);
        glGetIntegerv(GL_VIEWPORT, prevViewport);
        const GLboolean depthTest = glIsEnabled(GL_DEPTH_TEST), blend = glIsEnabled(GL_BLEND);
        glNamedFramebufferTexture(s_Framebuffer, GL_COLOR_ATTACHMENT0, destination.GLTexture, 0);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, s_Framebuffer);
        glViewport(0, 0, (GLsizei)destination.Width, (GLsizei)destination.Height);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);
        glUseProgram(s_Program);
        glBindTextureUnit(0, glDepthTexture);
        glBindVertexArray(s_VAO);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindVertexArray((GLuint)prevVAO);
        glUseProgram((GLuint)prevProgram);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)prevFramebuffer);
        glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
        if (depthTest)
            glEnable(GL_DEPTH_TEST);
        if (blend)
            glEnable(GL_BLEND);
    }

    void Device::Shutdown()
    {
        if (m_Device)
        {
            vkDeviceWaitIdle(m_Device);
            for (VkFence& f : m_Fences)
                if (f)
                    vkDestroyFence(m_Device, f, nullptr), f = VK_NULL_HANDLE;
            if (m_Pool)
                vkDestroyCommandPool(m_Device, m_Pool, nullptr);
            if (m_VkReady)
                vkDestroySemaphore(m_Device, m_VkReady, nullptr);
            if (m_VkDone)
                vkDestroySemaphore(m_Device, m_VkDone, nullptr);
            vkDestroyDevice(m_Device, nullptr);
        }
        if (m_GLReady)
            glDeleteSemaphoresEXT(1, &m_GLReady);
        if (m_GLDone)
            glDeleteSemaphoresEXT(1, &m_GLDone);
        if (m_Instance)
            vkDestroyInstance(m_Instance, nullptr);
        m_Pool = VK_NULL_HANDLE;
        m_VkReady = m_VkDone = VK_NULL_HANDLE;
        m_GLReady = m_GLDone = 0;
        m_Device = VK_NULL_HANDLE;
        m_Physical = VK_NULL_HANDLE;
        m_Instance = VK_NULL_HANDLE;
        m_Queue = VK_NULL_HANDLE;
    }

    bool SelfTest(std::string& report)
    {
        Device& dev = Device::Get();
        std::string error;
        if (!dev.Init({}, error))
        {
            report = "FAIL: " + error;
            return false;
        }
        Image in, out;
        if (!dev.CreateImage(in, 64, 64, VK_FORMAT_R8G8B8A8_UNORM, GL_RGBA8, error) ||
            !dev.CreateImage(out, 128, 128, VK_FORMAT_R8G8B8A8_UNORM, GL_RGBA8, error))
        {
            dev.DestroyImage(in);
            report = "FAIL: " + error;
            return false;
        }
        // GL yazar: dört çeyrek dört renk (kırmızı, yeşil, mavi, beyaz)
        const uint32_t colors[4] = { 0xff0000ffu, 0xff00ff00u, 0xffff0000u, 0xffffffffu };
        std::vector<uint32_t> pixels(64 * 64);
        for (uint32_t y = 0; y < 64; ++y)
            for (uint32_t x = 0; x < 64; ++x)
                pixels[y * 64 + x] = colors[(y >= 32 ? 2 : 0) + (x >= 32 ? 1 : 0)];
        glTextureSubImage2D(in.GLTexture, 0, 0, 0, 64, 64, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        // Vulkan büyütür (GL'in yazdığını okuyup GL'in okuyacağı görüntüye yazar)
        const bool ok = dev.Execute({ &in, &out }, [&](VkCommandBuffer cb) {
            VkImageBlit blit{};
            blit.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
            blit.srcOffsets[1] = { 64, 64, 1 };
            blit.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
            blit.dstOffsets[1] = { 128, 128, 1 };
            vkCmdBlitImage(cb, in.Handle, VK_IMAGE_LAYOUT_GENERAL, out.Handle, VK_IMAGE_LAYOUT_GENERAL, 1, &blit, VK_FILTER_NEAREST);
            return true;
        }, error);
        std::vector<uint32_t> result(128 * 128, 0);
        if (ok)
            glGetTextureImage(out.GLTexture, 0, GL_RGBA, GL_UNSIGNED_BYTE, (GLsizei)(result.size() * 4), result.data());
        int matches = 0;
        const int probes[4][2] = { { 32, 32 }, { 96, 32 }, { 32, 96 }, { 96, 96 } };
        for (int i = 0; i < 4; ++i)
            if (result[(size_t)probes[i][1] * 128 + (size_t)probes[i][0]] == colors[i])
                ++matches;
        dev.DestroyImage(in);
        dev.DestroyImage(out);
        report = std::string(matches == 4 ? "PASS" : "FAIL") + ": OpenGL -> Vulkan -> OpenGL on " + dev.Name() + " (" + std::to_string(matches) +
                 " / 4 colors" + (ok ? "" : ", " + error) + ")";
        return matches == 4;
    }
}

#endif
