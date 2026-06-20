#include "TimeManager.h"
#include "Core/Logging/Log.h"
#include <algorithm>

namespace Conqueror
{
    float TimeManager::s_TimeScale = 1.0f;
    bool TimeManager::s_Paused = false;
    
    float TimeManager::s_ScaledDeltaTime = 0.0f;
    float TimeManager::s_UnscaledDeltaTime = 0.0f;
    
    float TimeManager::s_Time = 0.0f;
    float TimeManager::s_UnscaledTime = 0.0f;
    
    uint64_t TimeManager::s_FrameCount = 0;
    
    float TimeManager::s_FixedTimestep = 0.02f; // 50 FPS
    float TimeManager::s_MaxDeltaTime = 0.1f;   // 10 FPS minimum
    
    float TimeManager::s_SmoothDeltaTime = 0.0f;
    float TimeManager::s_DeltaSmoothingFactor = 0.1f;

    void TimeManager::Init()
    {
        s_TimeScale = 1.0f;
        s_Paused = false;
        s_ScaledDeltaTime = 0.0f;
        s_UnscaledDeltaTime = 0.0f;
        s_Time = 0.0f;
        s_UnscaledTime = 0.0f;
        s_FrameCount = 0;
        s_SmoothDeltaTime = 0.0f;
        
        CQ_CORE_INFO("TimeManager initialized");
    }

    void TimeManager::Update(float deltaTime)
    {
        // Clamp delta time
        s_UnscaledDeltaTime = std::min(deltaTime, s_MaxDeltaTime);
        
        // Apply time scale and pause
        if (s_Paused)
        {
            s_ScaledDeltaTime = 0.0f;
        }
        else
        {
            s_ScaledDeltaTime = s_UnscaledDeltaTime * s_TimeScale;
        }
        
        // Update total time
        s_Time += s_ScaledDeltaTime;
        s_UnscaledTime += s_UnscaledDeltaTime;
        
        // Smooth delta time (exponential moving average)
        if (s_SmoothDeltaTime == 0.0f)
        {
            s_SmoothDeltaTime = s_ScaledDeltaTime;
        }
        else
        {
            s_SmoothDeltaTime = s_SmoothDeltaTime * (1.0f - s_DeltaSmoothingFactor) + 
                                s_ScaledDeltaTime * s_DeltaSmoothingFactor;
        }
        
        s_FrameCount++;
    }

    void TimeManager::SetTimeScale(float scale)
    {
        s_TimeScale = std::max(0.0f, scale);
        CQ_CORE_INFO("Time scale set to: {0}", s_TimeScale);
    }

    void TimeManager::Pause()
    {
        s_Paused = true;
        CQ_CORE_INFO("Time paused");
    }

    void TimeManager::Resume()
    {
        s_Paused = false;
        CQ_CORE_INFO("Time resumed");
    }

    void TimeManager::SetFixedTimestep(float timestep)
    {
        s_FixedTimestep = timestep;
        CQ_CORE_INFO("Fixed timestep set to: {0}s ({1} FPS)", timestep, 1.0f / timestep);
    }

    void TimeManager::SetMaxDeltaTime(float maxDelta)
    {
        s_MaxDeltaTime = maxDelta;
        CQ_CORE_INFO("Max delta time set to: {0}s", maxDelta);
    }

    void TimeManager::SetDeltaSmoothingFactor(float factor)
    {
        s_DeltaSmoothingFactor = std::clamp(factor, 0.0f, 1.0f);
    }
}
