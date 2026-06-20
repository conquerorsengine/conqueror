#include "Profiler.h"
#include "Core/Logging/Log.h"

namespace Conqueror
{
    void Profiler::BeginSession(const std::string& name)
    {
        m_SessionName = name;
        m_ActiveProfiles.clear();
        m_Results.clear();
        CQ_CORE_INFO("Profiler session started: {0}", name);
    }

    void Profiler::EndSession()
    {
        CQ_CORE_INFO("Profiler session ended: {0}", m_SessionName);
        m_SessionName.clear();
    }

    void Profiler::BeginProfile(const std::string& name)
    {
        if (!m_Enabled)
            return;

        auto& data = m_ActiveProfiles[name];
        data.StartTime = std::chrono::high_resolution_clock::now();
    }

    void Profiler::EndProfile(const std::string& name)
    {
        if (!m_Enabled)
            return;

        auto endTime = std::chrono::high_resolution_clock::now();
        
        auto it = m_ActiveProfiles.find(name);
        if (it == m_ActiveProfiles.end())
            return;

        auto& data = it->second;
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - data.StartTime).count() / 1000.0;

        data.CallCount++;
        data.TotalDuration += duration;
        data.MinDuration = std::min(data.MinDuration, duration);
        data.MaxDuration = std::max(data.MaxDuration, duration);

        // Update results
        ProfileResult& result = m_Results[name];
        result.Name = name;
        result.CallCount = data.CallCount;
        result.Duration = data.TotalDuration;
        result.MinDuration = data.MinDuration;
        result.MaxDuration = data.MaxDuration;
        result.AvgDuration = data.TotalDuration / data.CallCount;
    }

    void Profiler::Clear()
    {
        m_ActiveProfiles.clear();
        m_Results.clear();
    }
}
