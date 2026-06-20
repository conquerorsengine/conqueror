#pragma once

#include "Core/Base/Base.h"
#include "Timestep.h"

namespace Conqueror
{
    // Global time management (time scale, pause, etc.)
    class CQ_API TimeManager
    {
    public:
        static void Init();
        static void Update(float deltaTime);
        
        // Time scale (slow-motion/fast-forward)
        static void SetTimeScale(float scale);
        static float GetTimeScale() { return s_TimeScale; }
        
        // Pause/Resume
        static void Pause();
        static void Resume();
        static bool IsPaused() { return s_Paused; }
        
        // Scaled time (affected by time scale and pause)
        static float GetDeltaTime() { return s_ScaledDeltaTime; }
        static float GetUnscaledDeltaTime() { return s_UnscaledDeltaTime; }
        
        // Total time since start
        static float GetTime() { return s_Time; }
        static float GetUnscaledTime() { return s_UnscaledTime; }
        
        // Frame count
        static uint64_t GetFrameCount() { return s_FrameCount; }
        
        // Fixed timestep (physics)
        static void SetFixedTimestep(float timestep);
        static float GetFixedTimestep() { return s_FixedTimestep; }
        
        // Maximum allowed delta time (prevent spiral of death)
        static void SetMaxDeltaTime(float maxDelta);
        static float GetMaxDeltaTime() { return s_MaxDeltaTime; }
        
        // Smooth delta time (exponential moving average)
        static float GetSmoothDeltaTime() { return s_SmoothDeltaTime; }
        static void SetDeltaSmoothingFactor(float factor);
        
    private:
        static float s_TimeScale;
        static bool s_Paused;
        
        static float s_ScaledDeltaTime;
        static float s_UnscaledDeltaTime;
        
        static float s_Time;
        static float s_UnscaledTime;
        
        static uint64_t s_FrameCount;
        
        static float s_FixedTimestep;
        static float s_MaxDeltaTime;
        
        static float s_SmoothDeltaTime;
        static float s_DeltaSmoothingFactor;
    };
}
