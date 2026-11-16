#include "LoadingProgress.h"

#include <algorithm>

namespace Conqueror::LoadingProgress
{
    namespace
    {
        Handler& H()
        {
            static auto& h = *new Handler();
            return h;
        }
        float s_RangeBegin = 0.0f, s_RangeEnd = 1.0f;
        bool s_InReport = false;
    }

    void SetHandler(Handler handler)
    {
        H() = std::move(handler);
        s_RangeBegin = 0.0f;
        s_RangeEnd = 1.0f;
    }

    bool Active() { return (bool)H(); }

    void Report(const std::string& step, const std::string& detail, float fraction)
    {
        // Dinleyici çizerken yeniden çağrılırsa (çizimin yüklediği bir doku) atlanır
        if (!H() || s_InReport)
            return;
        s_InReport = true;
        H()(step, detail, fraction);
        s_InReport = false;
    }

    void SetRange(float begin, float end)
    {
        s_RangeBegin = std::clamp(begin, 0.0f, 1.0f);
        s_RangeEnd = std::clamp(end, s_RangeBegin, 1.0f);
    }

    void ReportPart(const std::string& step, const std::string& detail, float partFraction)
    {
        if (!H())
            return;
        Report(step, detail, s_RangeBegin + (s_RangeEnd - s_RangeBegin) * std::clamp(partFraction, 0.0f, 1.0f));
    }
}
