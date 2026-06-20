#include "Clock.h"
#include <sstream>
#include <iomanip>
#include <ctime>

namespace Conqueror
{
    Clock::Clock()
        : m_Running(false)
    {
    }

    void Clock::Start()
    {
        m_StartTime = std::chrono::high_resolution_clock::now();
        m_Running = true;
    }

    void Clock::Stop()
    {
        m_StopTime = std::chrono::high_resolution_clock::now();
        m_Running = false;
    }

    void Clock::Reset()
    {
        m_StartTime = std::chrono::high_resolution_clock::now();
        m_StopTime = m_StartTime;
    }

    double Clock::GetElapsedSeconds() const
    {
        TimePoint endTime = m_Running ? std::chrono::high_resolution_clock::now() : m_StopTime;
        Duration elapsed = endTime - m_StartTime;
        return elapsed.count();
    }

    double Clock::GetElapsedMilliseconds() const
    {
        return GetElapsedSeconds() * 1000.0;
    }

    std::string Clock::GetCurrentTime()
    {
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        std::tm tm = *std::localtime(&time);
        
        std::ostringstream oss;
        oss << std::put_time(&tm, "%H:%M:%S");
        return oss.str();
    }

    std::string Clock::GetCurrentDate()
    {
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        std::tm tm = *std::localtime(&time);
        
        std::ostringstream oss;
        oss << std::put_time(&tm, "%Y-%m-%d");
        return oss.str();
    }

    std::string Clock::GetCurrentDateTime()
    {
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        std::tm tm = *std::localtime(&time);
        
        std::ostringstream oss;
        oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
        return oss.str();
    }

    uint64_t Clock::GetTimestamp()
    {
        auto now = std::chrono::system_clock::now();
        auto duration = now.time_since_epoch();
        return std::chrono::duration_cast<std::chrono::seconds>(duration).count();
    }

    uint64_t Clock::GetTimestampMs()
    {
        auto now = std::chrono::system_clock::now();
        auto duration = now.time_since_epoch();
        return std::chrono::duration_cast<std::chrono::milliseconds>(duration).count();
    }
}
