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
#elif defined(__APPLE__) || defined(__MACH__)
    #include <TargetConditionals.h>
    #if TARGET_IPHONE_SIMULATOR == 1
        #error "IOS simulator is not supported!"
    #elif TARGET_OS_IPHONE == 1
        #define CQ_PLATFORM_IOS
        #error "IOS is not supported!"
    #elif TARGET_OS_MAC == 1
        #define CQ_PLATFORM_MACOS
        #error "MacOS is not supported!"
    #else
        #error "Unknown Apple platform!"
    #endif
#elif defined(__ANDROID__)
    #define CQ_PLATFORM_ANDROID
    #error "Android is not supported!"
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
#elif defined(CQ_PLATFORM_LINUX)
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
    #error Conqueror only supports Windows and Linux!
#endif

// Debug break
#ifdef CQ_DEBUG
    #if defined(CQ_PLATFORM_WINDOWS)
        #define CQ_DEBUGBREAK() __debugbreak()
    #elif defined(CQ_PLATFORM_LINUX)
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
