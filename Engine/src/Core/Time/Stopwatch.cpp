#include "Stopwatch.h"
#include <algorithm>
#include <numeric>

namespace Conqueror
{
    Stopwatch::Stopwatch()
        : m_Running(false)
    {
    }

    void Stopwatch::Start()
    {
        if (!m_Running)
        {
            m_StartTime = std::chrono::high_resolution_clock::now();
            m_LastLapTime = m_StartTime;
            m_Running = true;
        }
    }

    void Stopwatch::Stop()
    {
        if (m_Running)
        {
            m_StopTime = std::chrono::high_resolution_clock::now();
            m_Running = false;
        }
    }

    void Stopwatch::Reset()
    {
        m_StartTime = std::chrono::high_resolution_clock::now();
        m_StopTime = m_StartTime;
        m_LastLapTime = m_StartTime;
        m_Laps.clear();
    }

    void Stopwatch::Restart()
    {
        Reset();
        Start();
    }

    void Stopwatch::RecordLap(const std::string& name)
    {
        if (!m_Running)
            return;

        auto now = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> elapsed = now - m_LastLapTime;
        
        Lap lap;
        lap.Name = name.empty() ? "Lap " + std::to_string(m_Laps.size() + 1) : name;
        lap.Time = elapsed.count();
        lap.Index = static_cast<uint32_t>(m_Laps.size());
        
        m_Laps.push_back(lap);
        m_LastLapTime = now;
    }

    void Stopwatch::ClearLaps()
    {
        m_Laps.clear();
        m_LastLapTime = std::chrono::high_resolution_clock::now();
    }

    double Stopwatch::GetElapsedSeconds() const
    {
        TimePoint endTime = m_Running ? std::chrono::high_resolution_clock::now() : m_StopTime;
        std::chrono::duration<double> elapsed = endTime - m_StartTime;
        return elapsed.count();
    }

    double Stopwatch::GetElapsedMilliseconds() const
    {
        return GetElapsedSeconds() * 1000.0;
    }

    double Stopwatch::GetAverageLapTime() const
    {
        if (m_Laps.empty())
            return 0.0;
        
        double sum = std::accumulate(m_Laps.begin(), m_Laps.end(), 0.0,
            [](double acc, const Lap& lap) { return acc + lap.Time; });
        
        return sum / m_Laps.size();
    }

    double Stopwatch::GetFastestLapTime() const
    {
        if (m_Laps.empty())
            return 0.0;
        
        auto it = std::min_element(m_Laps.begin(), m_Laps.end(),
            [](const Lap& a, const Lap& b) { return a.Time < b.Time; });
        
        return it->Time;
    }

    double Stopwatch::GetSlowestLapTime() const
    {
        if (m_Laps.empty())
            return 0.0;
        
        auto it = std::max_element(m_Laps.begin(), m_Laps.end(),
            [](const Lap& a, const Lap& b) { return a.Time < b.Time; });
        
        return it->Time;
    }

    double Stopwatch::GetCurrentLapTime() const
    {
        if (!m_Running)
            return 0.0;
        
        auto now = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double> elapsed = now - m_LastLapTime;
        return elapsed.count();
    }
}
