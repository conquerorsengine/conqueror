#pragma once

#include "Core/Base/Base.h"
#include <chrono>
#include <string>

namespace Conqueror
{
    // Sistem saati ve zaman ölçümü
    class CQ_API Clock
    {
    public:
        using TimePoint = std::chrono::high_resolution_clock::time_point;
        using Duration = std::chrono::duration<double>;
        
        Clock();
        
        // Stopwatch fonksiyonları
        void Start();
        void Stop();
        void Reset();
        double GetElapsedSeconds() const;
        double GetElapsedMilliseconds() const;
        
        // Sistem zamanı
        static std::string GetCurrentTime(); // HH:MM:SS
        static std::string GetCurrentDate(); // YYYY-MM-DD
        static std::string GetCurrentDateTime(); // YYYY-MM-DD HH:MM:SS
        static uint64_t GetTimestamp(); // Unix timestamp (seconds)
        static uint64_t GetTimestampMs(); // Unix timestamp (milliseconds)
        
    private:
        TimePoint m_StartTime;
        TimePoint m_StopTime;
        bool m_Running;
    };
}
