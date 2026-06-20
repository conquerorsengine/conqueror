#pragma once

#include "Core/Base/Base.h"
#include <chrono>

namespace Conqueror
{
    // FPS limiting ve frame pacing
    class CQ_API FrameRateController
    {
    public:
        FrameRateController();
        
        // Target FPS ayarla (0 = unlimited)
        void SetTargetFPS(uint32_t fps);
        uint32_t GetTargetFPS() const { return m_TargetFPS; }
        
        // VSync
        void SetVSync(bool enabled);
        bool IsVSyncEnabled() const { return m_VSync; }
        
        // Frame başlangıcında çağrılır
        void BeginFrame();
        
        // Frame sonunda çağrılır (gerekirse bekler)
        void EndFrame();
        
        // Gerçek FPS
        float GetCurrentFPS() const { return m_CurrentFPS; }
        float GetAverageFPS() const { return m_AverageFPS; }
        
        // Frame time
        float GetFrameTime() const { return m_FrameTime; }
        float GetAverageFrameTime() const { return m_AverageFrameTime; }
        
        // Frame pacing quality (0-1, 1 = perfect)
        float GetFramePacingQuality() const;
        
    private:
        using TimePoint = std::chrono::high_resolution_clock::time_point;
        
        uint32_t m_TargetFPS;
        bool m_VSync;
        
        TimePoint m_FrameStartTime;
        TimePoint m_LastFrameTime;
        
        float m_CurrentFPS;
        float m_AverageFPS;
        
        float m_FrameTime;
        float m_AverageFrameTime;
        
        // Frame time history (son 60 frame)
        static constexpr size_t HISTORY_SIZE = 60;
        float m_FrameTimeHistory[HISTORY_SIZE];
        size_t m_HistoryIndex;
        
        void UpdateFPS();
        void WaitForTargetFrameTime();
    };
}
