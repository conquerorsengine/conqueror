#pragma once

#include "Core/Debug/RendererStats.h"

namespace Conqueror::Editor
{
    class StatsPanel
    {
    public:
        StatsPanel() = default;
        ~StatsPanel() = default;

        void OnImGuiRender();
        void SetStats(const RendererStats& stats);
        void SetFPS(float fps, float frameTime) { m_FPS = fps; m_FrameTime = frameTime; }

    private:
        RendererStats m_Stats;
        float m_FPS = 0.0f;
        float m_FrameTime = 0.0f;
    };
}
