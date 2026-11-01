#ifdef CQ_VULKAN_ENABLED

#include "VulkanLoader.h"

#ifdef _WIN32
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
#else
    #include <dlfcn.h>
#endif

#define CQ_VK_DEFINE(name) PFN_##name cq_##name = nullptr;
PFN_vkGetInstanceProcAddr cq_vkGetInstanceProcAddr = nullptr;
CQ_VK_GLOBAL_FUNCTIONS(CQ_VK_DEFINE)
CQ_VK_INSTANCE_FUNCTIONS(CQ_VK_DEFINE)
CQ_VK_DEVICE_FUNCTIONS(CQ_VK_DEFINE)
#undef CQ_VK_DEFINE

namespace Conqueror::VK
{
    namespace
    {
        void* s_Library = nullptr;

        void* OpenLibrary()
        {
#if defined(_WIN32)
            return (void*)::LoadLibraryA("vulkan-1.dll");
#elif defined(__APPLE__)
            // Vulkan SDK yükleyicisi, ya da doğrudan MoltenVK (uygulama paketinin Frameworks/ klasöründe olabilir)
            const char* names[] = { "libvulkan.1.dylib", "libvulkan.dylib", "libMoltenVK.dylib", "@executable_path/../Frameworks/libMoltenVK.dylib" };
            for (const char* n : names)
                if (void* h = dlopen(n, RTLD_NOW | RTLD_LOCAL))
                    return h;
            return nullptr;
#else
            void* h = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
            return h ? h : dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
#endif
        }

        void* Symbol(void* lib, const char* name)
        {
#if defined(_WIN32)
            return (void*)::GetProcAddress((HMODULE)lib, name);
#else
            return dlsym(lib, name);
#endif
        }
    }

    bool OpenRuntime(std::string& error)
    {
        if (vkGetInstanceProcAddr)
            return true;
        s_Library = OpenLibrary();
        if (!s_Library)
        {
            error = "Vulkan runtime (loader) not found";
            return false;
        }
        vkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)Symbol(s_Library, "vkGetInstanceProcAddr");
        if (!vkGetInstanceProcAddr)
        {
            error = "vkGetInstanceProcAddr missing from the Vulkan loader";
            CloseRuntime();
            return false;
        }
#define CQ_VK_LOAD_GLOBAL(name) cq_##name = (PFN_##name)cq_vkGetInstanceProcAddr(nullptr, #name);
        CQ_VK_GLOBAL_FUNCTIONS(CQ_VK_LOAD_GLOBAL)
#undef CQ_VK_LOAD_GLOBAL
        if (!vkCreateInstance)
        {
            error = "Vulkan loader is unusable (vkCreateInstance missing)";
            CloseRuntime();
            return false;
        }
        return true;
    }

    void LoadInstanceFunctions(VkInstance instance)
    {
#define CQ_VK_LOAD_INSTANCE(name) cq_##name = (PFN_##name)cq_vkGetInstanceProcAddr(instance, #name);
        CQ_VK_INSTANCE_FUNCTIONS(CQ_VK_LOAD_INSTANCE)
        // Cihaz işlevleri önce yükleyici sıçramalarıyla (cihaz oluşturulunca doğrudan göstericilerle değiştirilir)
        CQ_VK_DEVICE_FUNCTIONS(CQ_VK_LOAD_INSTANCE)
#undef CQ_VK_LOAD_INSTANCE
    }

    void LoadDeviceFunctions(VkDevice device)
    {
        if (!vkGetDeviceProcAddr)
            return;
#define CQ_VK_LOAD_DEVICE(name) if (auto fn = (PFN_##name)cq_vkGetDeviceProcAddr(device, #name)) cq_##name = fn;
        CQ_VK_DEVICE_FUNCTIONS(CQ_VK_LOAD_DEVICE)
#undef CQ_VK_LOAD_DEVICE
    }

    void CloseRuntime()
    {
        if (!s_Library)
            return;
#if defined(_WIN32)
        ::FreeLibrary((HMODULE)s_Library);
#else
        dlclose(s_Library);
#endif
        s_Library = nullptr;
        vkGetInstanceProcAddr = nullptr;
#define CQ_VK_RESET(name) cq_##name = nullptr;
        CQ_VK_GLOBAL_FUNCTIONS(CQ_VK_RESET)
        CQ_VK_INSTANCE_FUNCTIONS(CQ_VK_RESET)
        CQ_VK_DEVICE_FUNCTIONS(CQ_VK_RESET)
#undef CQ_VK_RESET
    }
}

#endif
