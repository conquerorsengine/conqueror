#include "FrameRateController.h"
#include "Core/Logging/Log.h"
#include <thread>
#include <algorithm>
#include <numeric>
#include <cmath>

namespace Conqueror
{
    FrameRateController::FrameRateController()
        : m_TargetFPS(0), m_VSync(false), m_CurrentFPS(0.0f), m_AverageFPS(0.0f),
          m_FrameTime(0.0f), m_AverageFrameTime(0.0f), m_HistoryIndex(0)
    {
        m_LastFrameTime = std::chrono::high_resolution_clock::now();
        std::fill(std::begin(m_FrameTimeHistory), std::end(m_FrameTimeHistory), 0.0f);
    }

    void FrameRateController::SetTargetFPS(uint32_t fps)
    {
        m_TargetFPS = fps;
        if (fps > 0)
        {
            CQ_CORE_INFO("Target FPS set to: {0}", fps);
        }
        else
        {
            CQ_CORE_INFO("Target FPS: Unlimited");
        }
    }

    void FrameRateController::SetVSync(bool enabled)
    {
        m_VSync = enabled;
        CQ_CORE_INFO("VSync: {0}", enabled ? "Enabled" : "Disabled");
    }

    void FrameRateController::BeginFrame()
    {
        m_FrameStartTime = std::chrono::high_resolution_clock::now();
    }

    void FrameRateController::EndFrame()
    {
        // FPS hesapla
        UpdateFPS();
        
        // Target FPS için bekle (VSync kapalıysa)
        if (!m_VSync && m_TargetFPS > 0)
        {
            WaitForTargetFrameTime();
        }
        
        m_LastFrameTime = std::chrono::high_resolution_clock::now();
    }

    void FrameRateController::UpdateFPS()
    {
        auto now = std::chrono::high_resolution_clock::now();
        std::chrono::duration<float> elapsed = now - m_LastFrameTime;
        
        m_FrameTime = elapsed.count();
        m_CurrentFPS = 1.0f / m_FrameTime;
        
        // Frame time history
        m_FrameTimeHistory[m_HistoryIndex] = m_FrameTime;
        m_HistoryIndex = (m_HistoryIndex + 1) % HISTORY_SIZE;
        
        // Average hesapla
        float sum = std::accumulate(std::begin(m_FrameTimeHistory), std::end(m_FrameTimeHistory), 0.0f);
        m_AverageFrameTime = sum / HISTORY_SIZE;
        m_AverageFPS = 1.0f / m_AverageFrameTime;
    }

    void FrameRateController::WaitForTargetFrameTime()
    {
        float targetFrameTime = 1.0f / m_TargetFPS;
        
        auto now = std::chrono::high_resolution_clock::now();
        std::chrono::duration<float> elapsed = now - m_FrameStartTime;
        
        float remainingTime = targetFrameTime - elapsed.count();
        
        if (remainingTime > 0.0f)
        {
            // Spin-wait son 1ms için (daha hassas)
            if (remainingTime > 0.001f)
            {
                std::this_thread::sleep_for(
                    std::chrono::duration<float>(remainingTime - 0.001f)
                );
            }
            
            // Spin-wait
            while (true)
            {
                now = std::chrono::high_resolution_clock::now();
                elapsed = now - m_FrameStartTime;
                if (elapsed.count() >= targetFrameTime)
                    break;
            }
        }
    }

    float FrameRateController::GetFramePacingQuality() const
    {
        // Frame time variance hesapla (düşük variance = iyi pacing)
        float mean = m_AverageFrameTime;
        
        float variance = 0.0f;
        for (size_t i = 0; i < HISTORY_SIZE; ++i)
        {
            float diff = m_FrameTimeHistory[i] - mean;
            variance += diff * diff;
        }
        variance /= HISTORY_SIZE;
        
        float stddev = std::sqrt(variance);
        
        // Normalize (0-1, 1 = perfect)
        float quality = 1.0f - std::min(stddev / mean, 1.0f);
        return quality;
    }
}
