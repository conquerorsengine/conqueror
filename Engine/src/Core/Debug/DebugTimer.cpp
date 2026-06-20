#include "DebugTimer.h"

#include <algorithm>

namespace Conqueror
{
    struct DebugTimerData
    {
        std::vector<DebugTimedEntry> Entries;
        uint32_t TotalSubmitted = 0;
    };

    static DebugTimerData* s_TimerData = nullptr;

    void DebugTimer::Init()
    {
        s_TimerData = new DebugTimerData();
    }

    void DebugTimer::Shutdown()
    {
        delete s_TimerData;
        s_TimerData = nullptr;
    }

    void DebugTimer::Update(float deltaTime)
    {
        if (!s_TimerData)
            return;

        for (auto& entry : s_TimerData->Entries)
        {
            if (entry.Remaining > 0.0f)
                entry.Remaining -= deltaTime;
        }

        s_TimerData->Entries.erase(
            std::remove_if(s_TimerData->Entries.begin(), s_TimerData->Entries.end(),
                [](const DebugTimedEntry& entry)
                {
                    return entry.Remaining <= 0.0f;
                }),
            s_TimerData->Entries.end());
    }

    void DebugTimer::Submit(float duration, bool depthTested, DebugCategory category, DebugDrawCallback callback)
    {
        if (!s_TimerData || !callback)
            return;

        if (duration <= 0.0f)
        {
            if (DebugSettings::Get().IsCategoryActive(category))
                callback();
            return;
        }

        const uint32_t maxEntries = DebugSettings::Get().MaxTimedEntries;
        if (s_TimerData->Entries.size() >= maxEntries)
            return;

        s_TimerData->TotalSubmitted++;
        s_TimerData->Entries.push_back({ duration, depthTested, category, std::move(callback) });
    }

    void DebugTimer::FlushActive()
    {
        if (!s_TimerData)
            return;

        auto& settings = DebugSettings::Get();
        for (const auto& entry : s_TimerData->Entries)
        {
            if (entry.Remaining <= 0.0f)
                continue;
            if (!settings.IsCategoryActive(entry.Category))
                continue;
            if (entry.Callback)
                entry.Callback();
        }
    }

    void DebugTimer::Clear()
    {
        if (!s_TimerData)
            return;
        s_TimerData->Entries.clear();
    }

    uint32_t DebugTimer::GetActiveCount()
    {
        return s_TimerData ? static_cast<uint32_t>(s_TimerData->Entries.size()) : 0;
    }

    uint32_t DebugTimer::GetTotalSubmitted()
    {
        return s_TimerData ? s_TimerData->TotalSubmitted : 0;
    }
}
