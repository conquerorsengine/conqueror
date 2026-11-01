#include "RendererAPI.h"
#include "Core/Base/Base.h"
#include "Renderer/GraphicsAPIs/OpenGL/OpenGLRendererAPI.h"

#include <glad/glad.h>

#ifdef _WIN32
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
#endif

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>

namespace Conqueror
{
    RendererAPI::API RendererAPI::s_API = RendererAPI::API::OpenGL;
    RendererAPI::Backend RendererAPI::s_Backend = RendererAPI::Backend::OpenGL;

    namespace
    {
        // Windows derlemesi Wine/Proton altında mı (ntdll Wine'a özgü işlevi dışa aktarır)
        bool RunningUnderWine()
        {
#ifdef _WIN32
            HMODULE ntdll = GetModuleHandleA("ntdll.dll");
            return ntdll && GetProcAddress(ntdll, "wine_get_version") != nullptr;
#else
            return false;
#endif
        }
    }

    std::string RendererAPI::GetDeviceDescription()
    {
        const GLubyte* renderer = glGetString ? glGetString(GL_RENDERER) : nullptr;
        return renderer ? std::string((const char*)renderer) : std::string();
    }

    const char* RendererAPI::GetBackendName(Backend backend)
    {
        switch (backend)
        {
            case Backend::Vulkan: return "Vulkan";
            case Backend::Direct3D11: return "Direct3D 11";
            case Backend::Direct3D12: return "Direct3D 12";
            case Backend::Metal: return "Metal";
            default: return "OpenGL";
        }
    }

    bool RendererAPI::IsBackendCompiled(Backend backend)
    {
        switch (backend)
        {
            case Backend::OpenGL: return true;
#ifdef CQ_VULKAN_ENABLED
            case Backend::Vulkan: return true;
#endif
#ifdef CQ_D3D11_ENABLED
            case Backend::Direct3D11: return true;
#endif
#ifdef CQ_D3D12_ENABLED
            case Backend::Direct3D12: return true;
#endif
#ifdef CQ_METAL_ENABLED
            case Backend::Metal: return true;
#endif
            default: return false;
        }
    }

    bool RendererAPI::ParseBackend(const std::string& name, Backend& out)
    {
        std::string n = name;
        std::transform(n.begin(), n.end(), n.begin(), [](unsigned char ch) { return (char)std::tolower(ch); });
        if (n == "vulkan" || n == "vk")
        {
            out = Backend::Vulkan;
            return true;
        }
        if (n == "opengl" || n == "gl")
        {
            out = Backend::OpenGL;
            return true;
        }
        if (n == "metal" || n == "mtl")
        {
            out = Backend::Metal;
            return true;
        }
        if (n == "direct3d12" || n == "direct3d 12" || n == "d3d12" || n == "dx12")
        {
            out = Backend::Direct3D12;
            return true;
        }
        if (n == "direct3d11" || n == "direct3d 11" || n == "d3d11" || n == "dx11" || n == "directx" || n == "d3d" || n == "dx")
        {
            out = Backend::Direct3D11;
            return true;
        }
        return false;   // "auto" ya da bilinmeyen
    }

    RendererAPI::Backend RendererAPI::ResolveBackend(int argc, char** argv, const std::string& requested)
    {
        Backend chosen = Backend::OpenGL;
        bool decided = false;
        for (int i = 1; i < argc && argv && !decided; ++i)
        {
            const char* a = argv[i];
            if (!a)
                continue;
            if (std::strcmp(a, "--vulkan") == 0)
                chosen = Backend::Vulkan, decided = true;
            else if (std::strcmp(a, "--opengl") == 0)
                chosen = Backend::OpenGL, decided = true;
            else if (std::strcmp(a, "--metal") == 0)
                chosen = Backend::Metal, decided = true;
            else if (std::strcmp(a, "--d3d12") == 0 || std::strcmp(a, "--dx12") == 0)
                chosen = Backend::Direct3D12, decided = true;
            else if (std::strcmp(a, "--d3d11") == 0 || std::strcmp(a, "--dx11") == 0 || std::strcmp(a, "--directx") == 0)
                chosen = Backend::Direct3D11, decided = true;
            else if (std::strcmp(a, "--renderer") == 0 && i + 1 < argc && argv[i + 1])
                decided = ParseBackend(argv[i + 1], chosen);
            else if (std::strncmp(a, "--renderer=", 11) == 0)
                decided = ParseBackend(a + 11, chosen);
        }
        if (!decided)
            if (const char* env = std::getenv("CQ_RENDERER"))
                decided = ParseBackend(env, chosen);
        if (!decided)
            decided = ParseBackend(requested, chosen);
        // "Auto": platformun yerel API'si — Windows: Direct3D 11, macOS: Metal (yoksa Vulkan / MoltenVK);
        // Linux'ta en olgun yol OpenGL (Vulkan isteğe bağlı). Başlatılamazsa pencere OpenGL'e düşer.
        if (!decided)
        {
#if defined(_WIN32) && defined(CQ_D3D11_ENABLED)
            chosen = Backend::Direct3D11;
    #ifdef CQ_VULKAN_ENABLED
            // Wine'ın HLSL derleyicisi (vkd3d) çok yavaş (açılış dakikalar sürer); Wine'da Vulkan daha hızlı
            if (RunningUnderWine())
                chosen = Backend::Vulkan;
    #endif
#elif defined(__APPLE__) && defined(CQ_METAL_ENABLED)
            chosen = Backend::Metal;
#elif defined(__APPLE__) && defined(CQ_VULKAN_ENABLED)
            chosen = Backend::Vulkan;
#else
            chosen = Backend::OpenGL;
#endif
        }
        if (!IsBackendCompiled(chosen))
            chosen = Backend::OpenGL;
        return chosen;
    }

    std::unique_ptr<RendererAPI> RendererAPI::Create()
    {
        switch (s_API)
        {
            case RendererAPI::API::None:    CQ_CORE_ASSERT(false, "RendererAPI::None is currently not supported!"); return nullptr;
            case RendererAPI::API::OpenGL:  return std::make_unique<OpenGLRendererAPI>();
        }

        CQ_CORE_ASSERT(false, "Unknown RendererAPI!");
        return nullptr;
    }
}
