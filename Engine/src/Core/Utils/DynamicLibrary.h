#pragma once

#include "Core/Base/Base.h"

#include <filesystem>
#include <string>
#include <vector>

namespace Conqueror
{
    // Platformdan bağımsız paylaşımlı kitaplık yükleyici (dlopen / LoadLibrary)
    class CQ_API DynamicLibrary
    {
    public:
        DynamicLibrary() = default;
        ~DynamicLibrary();
        DynamicLibrary(const DynamicLibrary&) = delete;
        DynamicLibrary& operator=(const DynamicLibrary&) = delete;
        DynamicLibrary(DynamicLibrary&& other) noexcept;
        DynamicLibrary& operator=(DynamicLibrary&& other) noexcept;

        bool Open(const std::filesystem::path& path, std::string* error = nullptr);
        void Close();
        bool IsOpen() const { return m_Handle != nullptr; }
        void* Symbol(const char* name) const;
        template<typename T>
        T Get(const char* name) const { return reinterpret_cast<T>(Symbol(name)); }
        const std::filesystem::path& GetPath() const { return m_Path; }

        // "mujoco" -> libmujoco.so / mujoco.dll / libmujoco.dylib
        static std::string PlatformFileName(const std::string& baseName);
        // Çalıştırılabilir dosyanın klasörü
        static std::filesystem::path ExecutableDirectory();
        // Adayları sırayla dener; ilk açılanı döndürür
        static bool OpenFirst(DynamicLibrary& lib, const std::vector<std::filesystem::path>& candidates, std::string* error = nullptr);

    private:
        void* m_Handle = nullptr;
        std::filesystem::path m_Path;
    };
}
