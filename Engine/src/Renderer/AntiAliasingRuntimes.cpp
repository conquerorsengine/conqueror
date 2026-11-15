// Üretici yükselticilerinin çalışma zamanı kütüphaneleri bu bilgisayarda var mı (AntiAliasing::Caps): kartın ve sürücünün kendisine
// sorulur, sabit listeden değil. windows.h ayrı dosyada (GL başlıklarıyla ve min / max makrolarıyla karışmasın).
#include "Core/Base/Base.h"

#include <filesystem>

#if defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
#endif

namespace Conqueror::AntiAliasing::Detail
{
    // NVIDIA NGX: DLSS'i yükleyen çalışma zamanı NVIDIA sürücüsüyle kurulur. Windows: sürücü yolunu
    // HKLM\SOFTWARE\NVIDIA Corporation\Global\NGXCore\FullPath'e yazar; Linux: libnvidia-ngx.so.1 (sürücü 470 ve sonrası)
    bool FindNGXRuntime()
    {
#if defined(_WIN32)
        char path[1024] = {};
        DWORD size = sizeof(path);
        return RegGetValueA(HKEY_LOCAL_MACHINE, "SOFTWARE\\NVIDIA Corporation\\Global\\NGXCore", "FullPath", RRF_RT_REG_SZ, nullptr, path, &size) ==
               ERROR_SUCCESS;
#elif defined(__linux__) && !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
        const char* dirs[] = { "/usr/lib/x86_64-linux-gnu", "/usr/lib/aarch64-linux-gnu", "/usr/lib64", "/usr/lib", "/usr/lib/nvidia",
                               "/usr/lib64/nvidia", "/run/opengl-driver/lib" };
        std::error_code ec;
        for (const char* d : dirs)
            if (std::filesystem::exists(std::filesystem::path(d) / "libnvidia-ngx.so.1", ec))
                return true;
        return false;
#else
        return false;
#endif
    }

    // Intel XeSS: oyunla birlikte dağıtılan libxess.dll (yalnız Windows): oyun klasöründe ya da DLL arama yolunda
    bool FindXeSSRuntime()
    {
#if defined(_WIN32)
        char found[MAX_PATH] = {};
        return SearchPathA(nullptr, "libxess.dll", nullptr, MAX_PATH, found, nullptr) > 0;
#else
        return false;
#endif
    }
}
