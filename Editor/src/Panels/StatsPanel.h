#pragma once

#include "Core/Debug/RendererStats.h"

#include <imgui.h>
#include <array>

namespace Conqueror::Editor
{
    // İstatistikler: Scene/Game görünümlerinin sağ üst köşesine sabitlenmiş,
    // ">" ile katlanabilen bir katman olarak çizilir (ayrı yüzen pencere değil).
    class StatsPanel
    {
    public:
        bool IsShowingFPS() const { return m_Ready && m_ShownFPS > 0.0f; }
        float GetShownFPS() const { return m_ShownFPS; }
        StatsPanel() = default;
        ~StatsPanel() = default;

        // Geriye dönük uyumluluk: ayrı pencere
        void OnImGuiRender();

        // Mevcut pencerenin içinde sabit katman. anchor: alignRight ise sağ üst,
        // değilse sol üst köşe. expanded: katlanma durumu (görünüm başına ayrı)
        void DrawOverlay(const char* id, const ImVec2& anchor, bool alignRight, bool& expanded, float maxHeight = 0.0f);

        void SetStats(const RendererStats& stats);
        void SetFPS(float fps, float frameTime);
        void SetEntityCount(uint32_t count) { m_EntityCount = count; }

    private:
        void DrawContents();

        RendererStats m_Stats;
        float m_FPS = 0.0f;
        float m_FrameTime = 0.0f;
        // Başlıkta gösterilen: çeyrek saniyelik ortalama; açılıştaki yükleme kareleri sayılmaz (Stats ilk anda kırmızı 10 FPS gösteriyordu)
        float m_ShownFPS = 0.0f, m_ShownFrameTime = 0.0f;
        float m_AccumMs = 0.0f, m_SinceStartMs = 0.0f;
        int m_AccumFrames = 0, m_GoodFrames = 0;
        float m_GoodMs = 0.0f, m_PrevFrameMs = 0.0f;   // açılış: art arda düzenli karelerin toplamı / önceki kare
        bool m_Ready = false;
        uint32_t m_EntityCount = 0;

        std::array<float, 120> m_FrameHistory{};
        int m_HistoryOffset = 0;
    };
}
