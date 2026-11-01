#pragma once

// Çalışan sürecin yürütülebilir dosya yolu (POSIX): Linux / Android'de /proc/self/exe bağı, QNX'te /proc/self/exefile
// metin dosyası, macOS'ta _NSGetExecutablePath, FreeBSD / NetBSD / DragonFly'da sysctl (KERN_PROC_PATHNAME). OpenBSD
// yolu vermez (boş döner, çağıran çalışma klasörüne düşer). Windows GetModuleFileName kullanır.
#if !defined(_WIN32)

#include <unistd.h>
#if defined(__APPLE__)
    #include <mach-o/dyld.h>
#elif defined(__FreeBSD__) || defined(__NetBSD__) || defined(__DragonFly__)
    #include <sys/types.h>
    #include <sys/sysctl.h>
#endif

#include <cstdint>
#include <cstdio>
#include <string>

namespace Conqueror
{
    inline std::string SelfExecutablePath()
    {
        char buffer[4096];
#if defined(__APPLE__)
        uint32_t size = sizeof(buffer);
        return _NSGetExecutablePath(buffer, &size) == 0 ? std::string(buffer) : std::string();
#elif defined(__FreeBSD__) || defined(__DragonFly__)
        int mib[4] = { CTL_KERN, KERN_PROC, KERN_PROC_PATHNAME, -1 };
        size_t size = sizeof(buffer);
        return sysctl(mib, 4, buffer, &size, nullptr, 0) == 0 && size > 1 ? std::string(buffer) : std::string();
#elif defined(__NetBSD__)
        int mib[4] = { CTL_KERN, KERN_PROC_ARGS, -1, KERN_PROC_PATHNAME };
        size_t size = sizeof(buffer);
        return sysctl(mib, 4, buffer, &size, nullptr, 0) == 0 && size > 1 ? std::string(buffer) : std::string();
#elif defined(__OpenBSD__)
        (void)buffer;
        return {};
#elif defined(__QNX__)
        std::FILE* f = std::fopen("/proc/self/exefile", "r");
        if (!f)
            return {};
        const size_t n = std::fread(buffer, 1, sizeof(buffer) - 1, f);
        std::fclose(f);
        std::string path(buffer, n);
        while (!path.empty() && (path.back() == '\0' || path.back() == '\n'))
            path.pop_back();
        return path;
#else
        const ssize_t n = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
        return n > 0 ? std::string(buffer, (size_t)n) : std::string();
#endif
    }
}

#endif
