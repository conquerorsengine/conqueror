#pragma once

#include "Core/Base/Base.h"
#include "QualitySettings.h"

namespace Conqueror
{
    // Runtime'da dinamik quality scaling
    class CQ_API DynamicQualityScaler
    {
    public:
        static void Init();
        static void Update(float deltaTime);
        
        // Enable/Disable
        static void SetEnabled(bool enabled);
        static bool IsEnabled() { return s_Enabled; }
        
        // Target FPS
        static void SetTargetFPS(float fps);
        static float GetTargetFPS() { return s_TargetFPS; }
        
        // FPS threshold (ne kadar düşerse scaling başlar)
        static void SetFPSThreshold(float threshold);
        static float GetFPSThreshold() { return s_FPSThreshold; }
        
        // Scaling aggressiveness (0-1, 1 = çok agresif)
        static void SetAggressiveness(float aggressiveness);
        static float GetAggressiveness() { return s_Aggressiveness; }
        
        // Stabilization time (quality değişikliği sonrası bekleme süresi)
        static void SetStabilizationTime(float time);
        static float GetStabilizationTime() { return s_StabilizationTime; }
        
        // Current state
        static float GetCurrentFPS() { return s_CurrentFPS; }
        static bool IsScaling() { return s_IsScaling; }
        
        // Manual override
        static void ForceQualityLevel(QualityLevel level);
        static void ReleaseOverride();
        
    private:
        static bool s_Enabled;
        static float s_TargetFPS;
        static float s_FPSThreshold;
        static float s_Aggressiveness;
        static float s_StabilizationTime;
        
        static float s_CurrentFPS;
        static float s_TimeSinceLastChange;
        static bool s_IsScaling;
        static bool s_ManualOverride;
        
        static QualityLevel s_BaseQuality;
        
        // FPS history (smoothing için)
        static constexpr size_t FPS_HISTORY_SIZE = 60;
        static float s_FPSHistory[FPS_HISTORY_SIZE];
        static size_t s_FPSHistoryIndex;
        
        static void UpdateFPSHistory(float fps);
        static float GetAverageFPS();
        static void AdjustQuality();
    };
}
