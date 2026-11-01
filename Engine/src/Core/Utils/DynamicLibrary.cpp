#include "DynamicLibrary.h"

#ifdef _WIN32
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
#else
    #include <dlfcn.h>
    #include <unistd.h>
    #ifdef __APPLE__
        #include <mach-o/dyld.h>
    #endif
    #include "Core/Utils/SelfExecutable.h"
#endif

namespace Conqueror
{
    DynamicLibrary::~DynamicLibrary()
    {
        Close();
    }

    DynamicLibrary::DynamicLibrary(DynamicLibrary&& other) noexcept
        : m_Handle(other.m_Handle), m_Path(std::move(other.m_Path))
    {
        other.m_Handle = nullptr;
    }

    DynamicLibrary& DynamicLibrary::operator=(DynamicLibrary&& other) noexcept
    {
        if (this != &other)
        {
            Close();
            m_Handle = other.m_Handle;
            m_Path = std::move(other.m_Path);
            other.m_Handle = nullptr;
        }
        return *this;
    }

    bool DynamicLibrary::Open(const std::filesystem::path& path, std::string* error)
    {
        Close();
#ifdef _WIN32
        m_Handle = (void*)LoadLibraryW(path.wstring().c_str());
        if (!m_Handle && error)
            *error = "LoadLibrary failed (" + std::to_string(GetLastError()) + "): " + path.string();
#else
        m_Handle = dlopen(path.string().c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!m_Handle && error)
        {
            const char* e = dlerror();
            *error = e ? e : ("dlopen failed: " + path.string());
        }
#endif
        if (m_Handle)
            m_Path = path;
        return m_Handle != nullptr;
    }

    void DynamicLibrary::Close()
    {
        if (!m_Handle)
            return;
#ifdef _WIN32
        FreeLibrary((HMODULE)m_Handle);
#else
        dlclose(m_Handle);
#endif
        m_Handle = nullptr;
        m_Path.clear();
    }

    void* DynamicLibrary::Symbol(const char* name) const
    {
        if (!m_Handle)
            return nullptr;
#ifdef _WIN32
        return (void*)GetProcAddress((HMODULE)m_Handle, name);
#else
        return dlsym(m_Handle, name);
#endif
    }

    std::string DynamicLibrary::PlatformFileName(const std::string& baseName)
    {
#ifdef _WIN32
        return baseName + ".dll";
#elif defined(__APPLE__)
        return "lib" + baseName + ".dylib";
#else
        return "lib" + baseName + ".so";
#endif
    }

    std::filesystem::path DynamicLibrary::ExecutableDirectory()
    {
#ifdef _WIN32
        wchar_t buffer[MAX_PATH];
        DWORD n = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
        return n ? std::filesystem::path(std::wstring(buffer, n)).parent_path() : std::filesystem::current_path();
#elif defined(__APPLE__)
        char buffer[4096];
        uint32_t size = sizeof(buffer);
        if (_NSGetExecutablePath(buffer, &size) == 0)
            return std::filesystem::weakly_canonical(buffer).parent_path();
        return std::filesystem::current_path();
#else
        const std::string exe = SelfExecutablePath();
        return exe.empty() ? std::filesystem::current_path() : std::filesystem::path(exe).parent_path();
#endif
    }

    bool DynamicLibrary::OpenFirst(DynamicLibrary& lib, const std::vector<std::filesystem::path>& candidates, std::string* error)
    {
        std::string lastError;
        for (const auto& c : candidates)
        {
            if (c.empty())
                continue;
            std::error_code ec;
            // Mutlak yollar varsa denenir; yalın adlar sistem aramasına bırakılır
            if (c.is_absolute() && !std::filesystem::exists(c, ec))
                continue;
            if (lib.Open(c, &lastError))
                return true;
        }
        if (error)
            *error = lastError.empty() ? "library not found" : lastError;
        return false;
    }
}
