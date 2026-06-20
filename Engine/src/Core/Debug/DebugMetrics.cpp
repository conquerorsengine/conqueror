#include "DebugMetrics.h"

namespace Conqueror
{
    struct DebugMetricsData
    {
        RendererStats Stats;
    };

    static DebugMetricsData s_Metrics;

    void DebugMetrics::ResetFrame()
    {
        s_Metrics.Stats.DebugLineCount = 0;
        s_Metrics.Stats.DebugLineVertices = 0;
        s_Metrics.Stats.DebugSolidTriangles = 0;
        s_Metrics.Stats.DebugSolidVertices = 0;
        s_Metrics.Stats.DebugTextEntries = 0;
        s_Metrics.Stats.DebugTimedEntries = 0;
    }

    void DebugMetrics::RecordLineDraw(uint32_t lineCount, uint32_t vertexCount)
    {
        s_Metrics.Stats.DebugLineCount += lineCount;
        s_Metrics.Stats.DebugLineVertices += vertexCount;
    }

    void DebugMetrics::RecordSolidDraw(uint32_t triangleCount, uint32_t vertexCount)
    {
        s_Metrics.Stats.DebugSolidTriangles += triangleCount;
        s_Metrics.Stats.DebugSolidVertices += vertexCount;
        s_Metrics.Stats.DrawCalls++;
    }

    void DebugMetrics::RecordTextDraw(uint32_t entryCount)
    {
        s_Metrics.Stats.DebugTextEntries += entryCount;
    }

    void DebugMetrics::RecordTimedEntry(uint32_t count)
    {
        s_Metrics.Stats.DebugTimedEntries = count;
    }

    void DebugMetrics::SyncToRendererStats(RendererStats& stats)
    {
        stats.DebugLineCount = s_Metrics.Stats.DebugLineCount;
        stats.DebugLineVertices = s_Metrics.Stats.DebugLineVertices;
        stats.DebugSolidTriangles = s_Metrics.Stats.DebugSolidTriangles;
        stats.DebugSolidVertices = s_Metrics.Stats.DebugSolidVertices;
        stats.DebugTextEntries = s_Metrics.Stats.DebugTextEntries;
        stats.DebugTimedEntries = s_Metrics.Stats.DebugTimedEntries;
    }

    const RendererStats& DebugMetrics::GetDebugStats()
    {
        return s_Metrics.Stats;
    }
}
