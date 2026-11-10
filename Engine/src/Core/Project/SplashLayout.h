#pragma once

#include "Core/Base/Base.h"
#include "Core/Project/Project.h"

#include <glm/glm.hpp>

#include <functional>
#include <string>
#include <vector>

// Açılış ekranı yerleşimi (Player Settings > Splash Image): derlenmiş oyun (RuntimeLayer) ve editördeki önizleme aynı hesabı
// kullanır. Adımlar sırayla gösterilir; her adımda bir ya da iki logo (proje logosu + yanında / üstünde / altında motor logosu).
namespace Conqueror::Splash
{
    struct Item
    {
        std::string Image;           // proje göreli yol (motor logosunda boş)
        bool EngineLogo = false;
    };

    struct Step
    {
        std::vector<Item> Items;     // 1: tek logo, 2: proje logosu + motor logosu
        float Duration = 2.0f;
    };

    struct DrawItem
    {
        const Item* Source = nullptr;
        glm::vec2 Center{ 0.0f };    // piksel, sol üst köken
        glm::vec2 Size{ 0.0f };
        float Alpha = 1.0f;
    };

    CQ_API std::vector<Step> BuildSteps(const SplashSettings& settings);
    CQ_API float TotalDuration(const std::vector<Step>& steps);
    // aspectOf: resmin en / boy oranı (yüklenemiyorsa 0: çizilmez)
    CQ_API void Layout(const SplashSettings& settings, const std::vector<Step>& steps, float time, const glm::vec2& window,
                       const std::function<float(const Item&)>& aspectOf, std::vector<DrawItem>& out);
    // Arka plan resmi: ekranı kaplayan (taşanı kırpılan) dikdörtgen
    CQ_API void CoverRect(float imageAspect, const glm::vec2& window, glm::vec2& center, glm::vec2& size);
}
