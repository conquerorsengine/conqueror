#ifdef CQ_WEBGPU_ENABLED

#include "WebGPUApi.h"

#include <cstdlib>
#include <vector>

#if !defined(__EMSCRIPTEN__)
    #if defined(_WIN32)
        #ifndef WIN32_LEAN_AND_MEAN
            #define WIN32_LEAN_AND_MEAN
        #endif
        #include <windows.h>
    #else
        #include <dlfcn.h>
    #endif
    #include <filesystem>
    #include "Core/Utils/DynamicLibrary.h"
#endif

namespace Conqueror::WGPU
{
    namespace
    {
        Procs s_Procs;
        std::string s_Library;
        bool s_Loaded = false;
    }

    Procs& W() { return s_Procs; }
    const std::string& LibraryName() { return s_Library; }

    bool Load(std::string& error)
    {
        if (s_Loaded)
            return true;
#if defined(__EMSCRIPTEN__)
#define CQ_WGPU_BIND(name) s_Procs.name = &wgpu##name;
        CQ_WGPU_PROCS(CQ_WGPU_BIND)
#undef CQ_WGPU_BIND
        s_Library = "browser WebGPU";
        s_Loaded = true;
        (void)error;
        return true;
#else
        std::vector<std::string> candidates;
        if (const char* env = std::getenv("CQ_WEBGPU_LIBRARY"); env && *env)
            candidates.push_back(env);
    #if defined(_WIN32)
        const char* names[] = { "webgpu_dawn.dll", "wgpu_native.dll" };
    #elif defined(__APPLE__)
        const char* names[] = { "libwebgpu_dawn.dylib", "libwgpu_native.dylib" };
    #else
        const char* names[] = { "libwebgpu_dawn.so", "libwgpu_native.so" };
    #endif
        // Önce oyunun / editörün yanı (lib/ dahil), sonra sistem arama yolu
        const std::filesystem::path exeDir = DynamicLibrary::ExecutableDirectory();
        for (const char* n : names)
        {
            candidates.push_back((exeDir / n).string());
            candidates.push_back((exeDir / "lib" / n).string());
        }
        for (const char* n : names)
            candidates.push_back(n);

        for (const auto& c : candidates)
        {
    #if defined(_WIN32)
            HMODULE lib = LoadLibraryA(c.c_str());
            auto sym = [&](const char* s) { return lib ? (void*)GetProcAddress(lib, s) : nullptr; };
    #else
            void* lib = dlopen(c.c_str(), RTLD_NOW | RTLD_LOCAL);
            auto sym = [&](const char* s) { return lib ? dlsym(lib, s) : nullptr; };
    #endif
            if (!lib)
                continue;
            bool ok = true;
            std::string missing;
#define CQ_WGPU_LOAD(name) \
            s_Procs.name = reinterpret_cast<WGPUProc##name>(sym("wgpu" #name)); \
            if (!s_Procs.name) { ok = false; missing = "wgpu" #name; }
            CQ_WGPU_PROCS(CQ_WGPU_LOAD)
#undef CQ_WGPU_LOAD
            if (!ok)
            {
                error = c + " has no " + missing + " (older webgpu.h?)";
                s_Procs = Procs{};
                continue;
            }
            s_Library = c;
            s_Loaded = true;
            return true;
        }
        if (error.empty())
            error = "no WebGPU implementation found (put libwebgpu_dawn / libwgpu_native next to the game or set CQ_WEBGPU_LIBRARY)";
        return false;
#endif
    }
}

#endif
