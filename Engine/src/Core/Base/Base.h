#pragma once

#include <memory>

// Platform detection
#ifdef _WIN32
    #ifdef _WIN64
        #ifndef CQ_PLATFORM_WINDOWS
            #define CQ_PLATFORM_WINDOWS
        #endif
    #else
        #error "x86 Builds are not supported!"
    #endif
#elif defined(__APPLE__)   // __MACH__ değil: GNU/Hurd da GNU Mach çekirdeği üzerinde __MACH__ tanımlar
    #include <TargetConditionals.h>
    #if TARGET_IPHONE_SIMULATOR == 1
        #error "IOS simulator is not supported!"
    #elif TARGET_OS_IPHONE == 1
        #define CQ_PLATFORM_IOS
        #error "IOS is not supported!"
    #elif TARGET_OS_MAC == 1
        // macOS: POSIX yolları + Vulkan arka ucu (MoltenVK -> Metal); motor statik bağlanır
        #define CQ_PLATFORM_MACOS
    #else
        #error "Unknown Apple platform!"
    #endif
#elif defined(__ANDROID__)
    // Android: Linux çekirdeği + bionic libc. POSIX yolları Linux ile ortak; pencere, girdi ve EGL bağlamı
    // Platform/Android katmanında (NativeActivity), grafik OpenGL ES 3.x
    #define CQ_PLATFORM_ANDROID
    #define CQ_PLATFORM_LINUX
#elif defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) || defined(__DragonFly__)
    // BSD (FreeBSD, OpenBSD, NetBSD, DragonFly): POSIX + GLFW (X11 / Wayland), Linux ile aynı yollar. Linux'a özgü
    // /proc ve sysinfo kullanımları CQ_PLATFORM_BSD ile ayrıca korunur.
    #define CQ_PLATFORM_BSD
    #define CQ_PLATFORM_LINUX
#elif defined(__QNX__)
    // QNX Neutrino (SDP 7.1 / 8.0, otomotiv): POSIX. Pencere, girdi ve EGL bağlamı Platform/QNX katmanında (Screen),
    // grafik OpenGL ES 3.x. Linux'a özgü /proc ve sysinfo kullanımları ayrıca CQ_PLATFORM_QNX ile korunur.
    #define CQ_PLATFORM_QNX
    #define CQ_PLATFORM_LINUX
#elif defined(__gnu_hurd__) || defined(__GNU__)
    // GNU/Hurd (Debian GNU/Hurd): glibc + POSIX + GLFW X11 (grafik: Mesa yazılım OpenGL). Linux yolları ortak; Linux'a özgü
    // sysinfo ve /proc ayrıntıları ile tanımsız PATH_MAX CQ_PLATFORM_HURD ile ayrıca korunur.
    #define CQ_PLATFORM_HURD
    #define CQ_PLATFORM_LINUX
#elif defined(__linux__)
    #define CQ_PLATFORM_LINUX
#else
    #error "Unknown platform!"
#endif

// DLL support
#ifdef CQ_PLATFORM_WINDOWS
    #if CQ_DYNAMIC_LINK
        #ifdef CQ_BUILD_DLL
            #define CQ_API __declspec(dllexport)
        #else
            #define CQ_API __declspec(dllimport)
        #endif
    #else
        #define CQ_API
    #endif
#elif defined(CQ_PLATFORM_LINUX) || defined(CQ_PLATFORM_MACOS)
    #if CQ_DYNAMIC_LINK
        #ifdef CQ_BUILD_DLL
            #define CQ_API __attribute__((visibility("default")))
        #else
            #define CQ_API
        #endif
    #else
        #define CQ_API
    #endif
#else
    #error Conqueror only supports Windows, Linux and macOS!
#endif

// Debug break
#ifdef CQ_DEBUG
    #if defined(CQ_PLATFORM_WINDOWS)
        #define CQ_DEBUGBREAK() __debugbreak()
    #elif defined(CQ_PLATFORM_LINUX) || defined(CQ_PLATFORM_MACOS)
        #include <signal.h>
        #define CQ_DEBUGBREAK() raise(SIGTRAP)
    #else
        #error "Platform doesn't support debugbreak yet!"
    #endif
    #define CQ_ENABLE_ASSERTS
#else
    #define CQ_DEBUGBREAK()
#endif

#define CQ_EXPAND_MACRO(x) x
#define CQ_STRINGIFY_MACRO(x) #x

// Bit operations
#define BIT(x) (1 << x)

// Bind event function
#define CQ_BIND_EVENT_FN(fn) [this](auto&&... args) -> decltype(auto) { return this->fn(std::forward<decltype(args)>(args)...); }

namespace Conqueror
{
    template<typename T>
    using Scope = std::unique_ptr<T>;
    template<typename T, typename ... Args>
    constexpr Scope<T> CreateScope(Args&& ... args)
    {
        return std::make_unique<T>(std::forward<Args>(args)...);
    }

    template<typename T>
    using Ref = std::shared_ptr<T>;
    template<typename T, typename ... Args>
    constexpr Ref<T> CreateRef(Args&& ... args)
    {
        return std::make_shared<T>(std::forward<Args>(args)...);
    }
}

#include "Core/Base/Assert.h"
