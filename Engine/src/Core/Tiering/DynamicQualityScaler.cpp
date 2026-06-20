#include "DynamicQualityScaler.h"
#include "Core/Logging/Log.h"
#include <algorithm>
#include <numeric>

namespace Conqueror
{
    bool DynamicQualityScaler::s_Enabled = false;
    float DynamicQualityScaler::s_TargetFPS = 60.0f;
    float DynamicQualityScaler::s_FPSThreshold = 5.0f;
    float DynamicQualityScaler::s_Aggressiveness = 0.5f;
    float DynamicQualityScaler::s_StabilizationTime = 3.0f;
    
    float DynamicQualityScaler::s_CurrentFPS = 60.0f;
    float DynamicQualityScaler::s_TimeSinceLastChange = 0.0f;
    bool DynamicQualityScaler::s_IsScaling = false;
    bool DynamicQualityScaler::s_ManualOverride = false;
    
    QualityLevel DynamicQualityScaler::s_BaseQuality = QualityLevel::High;
    
    float DynamicQualityScaler::s_FPSHistory[FPS_HISTORY_SIZE] = {0};
    size_t DynamicQualityScaler::s_FPSHistoryIndex = 0;

    void DynamicQualityScaler::Init()
    {
        s_BaseQuality = QualitySettings::GetQualityLevel();
        std::fill(std::begin(s_FPSHistory), std::end(s_FPSHistory), s_TargetFPS);
        
        CQ_CORE_INFO("DynamicQualityScaler initialized");
        CQ_CORE_INFO("  Target FPS: {0}", s_TargetFPS);
        CQ_CORE_INFO("  Base Quality: {0}", (int)s_BaseQuality);
    }

    void DynamicQualityScaler::Update(float deltaTime)
    {
        if (!s_Enabled || s_ManualOverride)
            return;
        
        // FPS hesapla
        s_CurrentFPS = 1.0f / deltaTime;
        UpdateFPSHistory(s_CurrentFPS);
        
        s_TimeSinceLastChange += deltaTime;
        
        // Stabilization period
        if (s_TimeSinceLastChange < s_StabilizationTime)
            return;
        
        float avgFPS = GetAverageFPS();
        float fpsDiff = s_TargetFPS - avgFPS;
        
        // FPS threshold kontrolü
        if (std::abs(fpsDiff) > s_FPSThreshold)
        {
            s_IsScaling = true;
            AdjustQuality();
            s_TimeSinceLastChange = 0.0f;
        }
        else
        {
            s_IsScaling = false;
        }
    }

    void DynamicQualityScaler::SetEnabled(bool enabled)
    {
        s_Enabled = enabled;
        CQ_CORE_INFO("Dynamic Quality Scaling: {0}", enabled ? "Enabled" : "Disabled");
    }

    void DynamicQualityScaler::SetTargetFPS(float fps)
    {
        s_TargetFPS = fps;
        CQ_CORE_INFO("Target FPS set to: {0}", fps);
    }

    void DynamicQualityScaler::SetFPSThreshold(float threshold)
    {
        s_FPSThreshold = threshold;
    }

    void DynamicQualityScaler::SetAggressiveness(float aggressiveness)
    {
        s_Aggressiveness = std::clamp(aggressiveness, 0.0f, 1.0f);
    }

    void DynamicQualityScaler::SetStabilizationTime(float time)
    {
        s_StabilizationTime = time;
    }

    void DynamicQualityScaler::ForceQualityLevel(QualityLevel level)
    {
        s_ManualOverride = true;
        QualitySettings::SetQualityLevel(level);
        CQ_CORE_INFO("Quality level manually overridden to: {0}", (int)level);
    }

    void DynamicQualityScaler::ReleaseOverride()
    {
        s_ManualOverride = false;
        CQ_CORE_INFO("Manual quality override released");
    }

    void DynamicQualityScaler::UpdateFPSHistory(float fps)
    {
        s_FPSHistory[s_FPSHistoryIndex] = fps;
        s_FPSHistoryIndex = (s_FPSHistoryIndex + 1) % FPS_HISTORY_SIZE;
    }

    float DynamicQualityScaler::GetAverageFPS()
    {
        float sum = std::accumulate(std::begin(s_FPSHistory), std::end(s_FPSHistory), 0.0f);
        return sum / FPS_HISTORY_SIZE;
    }

    void DynamicQualityScaler::AdjustQuality()
    {
        QualityLevel currentLevel = QualitySettings::GetQualityLevel();
        float avgFPS = GetAverageFPS();
        
        // FPS düşükse quality düşür
        if (avgFPS < s_TargetFPS - s_FPSThreshold)
        {
            if (currentLevel > QualityLevel::Low)
            {
                QualityLevel newLevel = static_cast<QualityLevel>(static_cast<int>(currentLevel) - 1);
                QualitySettings::SetQualityLevel(newLevel);
                CQ_CORE_WARN("Quality downgraded to: {0} (FPS: {1})", (int)newLevel, avgFPS);
            }
        }
        // FPS yüksekse quality artır
        else if (avgFPS > s_TargetFPS + s_FPSThreshold * 2.0f)
        {
            if (currentLevel < s_BaseQuality)
            {
                QualityLevel newLevel = static_cast<QualityLevel>(static_cast<int>(currentLevel) + 1);
                QualitySettings::SetQualityLevel(newLevel);
                CQ_CORE_INFO("Quality upgraded to: {0} (FPS: {1})", (int)newLevel, avgFPS);
            }
        }
    }
}
