#pragma once

#include "Core/Base/Base.h"
#include "DebugSettings.h"
#include <glm/glm.hpp>
#include <functional>
#include <vector>

namespace Conqueror
{
    using DebugDrawCallback = std::function<void()>;

    struct CQ_API DebugTimedEntry
    {
        float Remaining = 0.0f;
        bool DepthTested = true;
        DebugCategory Category = DebugCategory::Wireframe;
        DebugDrawCallback Callback;
    };

    class CQ_API DebugTimer
    {
    public:
        static void Init();
        static void Shutdown();
        static void Update(float deltaTime);

        static void Submit(float duration, bool depthTested, DebugCategory category, DebugDrawCallback callback);
        static void FlushActive();
        static void Clear();

        static uint32_t GetActiveCount();
        static uint32_t GetTotalSubmitted();
    };
}
