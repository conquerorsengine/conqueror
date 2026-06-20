#pragma once

#include "Core/Base/Base.h"
#include <chrono>
#include <vector>
#include <string>

namespace Conqueror
{
    // Lap time tracking için stopwatch
    class CQ_API Stopwatch
    {
    public:
        struct Lap
        {
            std::string Name;
            double Time; // seconds
            uint32_t Index;
        };

        Stopwatch();
        
        void Start();
        void Stop();
        void Reset();
        void Restart();
        
        // Lap kaydetme
        void RecordLap(const std::string& name = "");
        void ClearLaps();
        
        bool IsRunning() const { return m_Running; }
        double GetElapsedSeconds() const;
        double GetElapsedMilliseconds() const;
        
        // Lap bilgileri
        const std::vector<Lap>& GetLaps() const { return m_Laps; }
        size_t GetLapCount() const { return m_Laps.size(); }
        double GetAverageLapTime() const;
        double GetFastestLapTime() const;
        double GetSlowestLapTime() const;
        
        // Son lap'tan bu yana geçen süre
        double GetCurrentLapTime() const;
        
    private:
        using TimePoint = std::chrono::high_resolution_clock::time_point;
        
        TimePoint m_StartTime;
        TimePoint m_StopTime;
        TimePoint m_LastLapTime;
        bool m_Running;
        std::vector<Lap> m_Laps;
    };
}
