#pragma once

#include "Core/Base/Base.h"
#include <string>
#include <chrono>
#include <unordered_map>
#include <vector>

namespace Conqueror
{
    // Performance profiling data
    struct ProfileResult
    {
        std::string Name;
        double Duration; // milliseconds
        uint64_t CallCount;
        double MinDuration;
        double MaxDuration;
        double AvgDuration;
    };

    // Profiler singleton
    class CQ_API Profiler
    {
    public:
        static Profiler& Get()
        {
            static Profiler instance;
            return instance;
        }

        void BeginSession(const std::string& name);
        void EndSession();

        void BeginProfile(const std::string& name);
        void EndProfile(const std::string& name);

        const std::unordered_map<std::string, ProfileResult>& GetResults() const { return m_Results; }
        void Clear();

        void SetEnabled(bool enabled) { m_Enabled = enabled; }
        bool IsEnabled() const { return m_Enabled; }

    private:
        Profiler() = default;
        ~Profiler() = default;
        Profiler(const Profiler&) = delete;
        Profiler& operator=(const Profiler&) = delete;

        struct ProfileData
        {
            std::chrono::time_point<std::chrono::high_resolution_clock> StartTime;
            uint64_t CallCount = 0;
            double TotalDuration = 0.0;
            double MinDuration = std::numeric_limits<double>::max();
            double MaxDuration = 0.0;
        };

        std::string m_SessionName;
        std::unordered_map<std::string, ProfileData> m_ActiveProfiles;
        std::unordered_map<std::string, ProfileResult> m_Results;
        bool m_Enabled = true;
    };

    // RAII scope profiler
    class CQ_API ScopedProfiler
    {
    public:
        ScopedProfiler(const std::string& name)
            : m_Name(name)
        {
            Profiler::Get().BeginProfile(m_Name);
        }

        ~ScopedProfiler()
        {
            Profiler::Get().EndProfile(m_Name);
        }

    private:
        std::string m_Name;
    };
}

// Profiling macros (single definition site — Log.h must not redefine these)
#ifndef CQ_PROFILE_SCOPE
#ifdef CQ_ENABLE_PROFILING
    #define CQ_PROFILE_BEGIN_SESSION(name) ::Conqueror::Profiler::Get().BeginSession(name)
    #define CQ_PROFILE_END_SESSION() ::Conqueror::Profiler::Get().EndSession()
    #define CQ_PROFILE_SCOPE(name) ::Conqueror::ScopedProfiler __profiler_##__LINE__(name)
    #define CQ_PROFILE_FUNCTION() CQ_PROFILE_SCOPE(__FUNCTION__)
#else
    #define CQ_PROFILE_BEGIN_SESSION(name)
    #define CQ_PROFILE_END_SESSION()
    #define CQ_PROFILE_SCOPE(name)
    #define CQ_PROFILE_FUNCTION()
#endif
#endif
