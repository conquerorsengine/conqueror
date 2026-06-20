#pragma once

#include "RendererStats.h"
#include <cstdint>

namespace Conqueror
{
    class CQ_API DebugMetrics
    {
    public:
        static void ResetFrame();
        static void RecordLineDraw(uint32_t lineCount, uint32_t vertexCount);
        static void RecordSolidDraw(uint32_t triangleCount, uint32_t vertexCount);
        static void RecordTextDraw(uint32_t entryCount);
        static void RecordTimedEntry(uint32_t count);

        static void SyncToRendererStats(RendererStats& stats);
        static const RendererStats& GetDebugStats();
    };
}
