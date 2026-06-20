#pragma once

#include "Core/Base/Base.h"
#include <cstdint>

namespace Conqueror
{
    enum class DebugCategory : uint32_t
    {
        None        = 0,
        Wireframe   = 1 << 0,
        Solid       = 1 << 1,
        Text        = 1 << 2,
        Gizmo       = 1 << 3,
        Camera      = 1 << 4,
        Physics     = 1 << 5,
        Grid        = 1 << 6,
        Path        = 1 << 7,
        All         = 0xFFFFFFFF
    };

    inline DebugCategory operator|(DebugCategory a, DebugCategory b)
    {
        return static_cast<DebugCategory>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
    }

    inline DebugCategory operator&(DebugCategory a, DebugCategory b)
    {
        return static_cast<DebugCategory>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
    }

    inline bool HasCategory(DebugCategory mask, DebugCategory category)
    {
        return (static_cast<uint32_t>(mask & category) != 0);
    }

    struct CQ_API DebugSettings
    {
        bool Enabled = true;
        bool DepthTestLines = true;
        bool DepthTestSolids = true;
        bool DepthTestText = false;
        float LineWidth = 2.0f;
        float DefaultDuration = 0.0f;
        uint32_t MaxLineVertices = 250000;
        uint32_t MaxSolidVertices = 100000;
        uint32_t MaxSolidIndices = 300000;
        uint32_t MaxTextEntries = 4096;
        uint32_t MaxTimedEntries = 512;
        uint32_t SphereSegments = 16;
        uint32_t CircleSegments = 32;
        DebugCategory ActiveCategories = DebugCategory::All;

        static DebugSettings& Get();
        static void Reset();

        bool IsCategoryActive(DebugCategory category) const;
        void EnableCategory(DebugCategory category);
        void DisableCategory(DebugCategory category);
        void SetCategory(DebugCategory category, bool enabled);
    };
}
