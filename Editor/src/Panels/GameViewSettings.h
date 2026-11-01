#pragma once

#include <glm/glm.hpp>
#include <algorithm>
#include <string>
#include <vector>

namespace Conqueror::Editor
{
    // Oyun görünümü çözünürlüğü: Game paneli, viewport çözünürlük seçicisi ve
    // Canvas çerçevesi aynı ayarı kullanır.
    struct GameResolutionPreset
    {
        std::string Name;
        int Width = 0;       // 0: serbest/oran
        int Height = 0;
        float Aspect = 0.0f; // >0 ise yalnızca en-boy oranı
    };

    class GameViewSettings
    {
    public:
        static GameViewSettings& Get()
        {
            static GameViewSettings s;
            return s;
        }

        std::vector<GameResolutionPreset> Presets = {
            { "Free Aspect", 0, 0, 0.0f },
            { "16:9 Aspect", 0, 0, 16.0f / 9.0f },
            { "16:10 Aspect", 0, 0, 16.0f / 10.0f },
            { "4:3 Aspect", 0, 0, 4.0f / 3.0f },
            { "21:9 Aspect", 0, 0, 21.0f / 9.0f },
            { "1280x720 (HD)", 1280, 720, 0.0f },
            { "1920x1080 (Full HD)", 1920, 1080, 0.0f },
            { "2560x1440 (QHD)", 2560, 1440, 0.0f },
            { "3840x2160 (4K UHD)", 3840, 2160, 0.0f },
            { "1080x1920 (Portrait)", 1080, 1920, 0.0f },
            { "2340x1080 (Phone)", 2340, 1080, 0.0f },
            { "1170x2532 (Phone Portrait)", 1170, 2532, 0.0f },
            { "2048x1536 (Tablet)", 2048, 1536, 0.0f },
            { "1280x800 (Steam Deck)", 1280, 800, 0.0f },
        };
        int Selected = 0;
        float Scale = 1.0f;        // Game panelinde yakınlaştırma
        bool MaximizeOnPlay = false;
        bool MuteAudio = false;
        bool ShowStats = false;
        bool VSync = true;
        glm::vec2 LastGamePanelSize{ 1920.0f, 1080.0f }; // serbest oran için

        const GameResolutionPreset& Current() const { return Presets[(size_t)std::clamp(Selected, 0, (int)Presets.size() - 1)]; }

        // Panel boyutu verildiğinde render çözünürlüğü
        glm::ivec2 RenderSize(const glm::vec2& panel) const
        {
            const auto& p = Current();
            if (p.Width > 0 && p.Height > 0)
                return { p.Width, p.Height };
            if (p.Aspect > 0.0f)
            {
                float w = panel.x, h = panel.x / p.Aspect;
                if (h > panel.y) { h = panel.y; w = h * p.Aspect; }
                return { std::max(1, (int)w), std::max(1, (int)h) };
            }
            return { std::max(1, (int)panel.x), std::max(1, (int)panel.y) };
        }

        // En-boy oranı (serbestse verilen panel oranı)
        float AspectRatio(const glm::vec2& panel) const
        {
            const auto& p = Current();
            if (p.Width > 0 && p.Height > 0) return (float)p.Width / (float)p.Height;
            if (p.Aspect > 0.0f) return p.Aspect;
            return panel.y > 0.0f ? panel.x / panel.y : 16.0f / 9.0f;
        }
    };
}
