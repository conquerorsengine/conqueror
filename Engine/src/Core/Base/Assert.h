#pragma once

#include "Core/Base/Base.h"
#include "Core/Logging/Log.h"

#ifdef CQ_ENABLE_ASSERTS
    #define CQ_INTERNAL_ASSERT_IMPL(type, check, msg, ...) { if(!(check)) { CQ##type##ERROR(msg, __VA_ARGS__); CQ_DEBUGBREAK(); } }
    #define CQ_INTERNAL_ASSERT_WITH_MSG(type, check, ...) CQ_INTERNAL_ASSERT_IMPL(type, check, "Assertion failed: {0}", __VA_ARGS__)
    #define CQ_INTERNAL_ASSERT_NO_MSG(type, check) CQ_INTERNAL_ASSERT_IMPL(type, check, "Assertion '{0}' failed at {1}:{2}", CQ_STRINGIFY_MACRO(check), std::filesystem::path(__FILE__).filename().string(), __LINE__)

    #define CQ_INTERNAL_ASSERT_GET_MACRO_NAME(arg1, arg2, macro, ...) macro
    #define CQ_INTERNAL_ASSERT_GET_MACRO(...) CQ_EXPAND_MACRO( CQ_INTERNAL_ASSERT_GET_MACRO_NAME(__VA_ARGS__, CQ_INTERNAL_ASSERT_WITH_MSG, CQ_INTERNAL_ASSERT_NO_MSG) )

    #define CQ_ASSERT(...) CQ_EXPAND_MACRO( CQ_INTERNAL_ASSERT_GET_MACRO(__VA_ARGS__)(_CLIENT_, __VA_ARGS__) )
    #define CQ_CORE_ASSERT(...) CQ_EXPAND_MACRO( CQ_INTERNAL_ASSERT_GET_MACRO(__VA_ARGS__)(_CORE_, __VA_ARGS__) )
#else
    #define CQ_ASSERT(...)
    #define CQ_CORE_ASSERT(...)
#endif
