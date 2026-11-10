#include "SplashLayout.h"

#include <algorithm>
#include <cmath>

namespace Conqueror::Splash
{
    std::vector<Step> BuildSteps(const SplashSettings& s)
    {
        std::vector<Step> steps;
        std::vector<const SplashLogoEntry*> logos;
        for (const auto& l : s.Logos)
            if (!l.Path.empty())
                logos.push_back(&l);
        const bool combined = s.DrawMode != SplashSettings::Sequential && s.ShowEngineLogo;
        for (const auto* l : logos)
        {
            Step step;
            step.Duration = std::clamp(l->Duration, 0.5f, 10.0f);
            step.Items.push_back({ l->Path, false });
            if (combined)
                step.Items.push_back({ {}, true });
            steps.push_back(std::move(step));
        }
        // Sıralı kipte (ya da proje logosu yoksa) motor logosu kendi adımında, en sonda
        if ((s.ShowEngineLogo && (!combined || logos.empty())) || steps.empty())
        {
            Step step;
            step.Duration = std::clamp(s.Duration, 0.5f, 10.0f);
            step.Items.push_back({ {}, true });
            steps.push_back(std::move(step));
        }
        return steps;
    }

    float TotalDuration(const std::vector<Step>& steps)
    {
        float total = 0.0f;
        for (const auto& s : steps)
            total += s.Duration;
        return total;
    }

    void CoverRect(float imageAspect, const glm::vec2& window, glm::vec2& center, glm::vec2& size)
    {
        center = window * 0.5f;
        if (imageAspect <= 0.0f)
        {
            size = window;
            return;
        }
        const float windowAspect = window.x / std::max(window.y, 1.0f);
        size = imageAspect > windowAspect ? glm::vec2(window.y * imageAspect, window.y) : glm::vec2(window.x, window.x / imageAspect);
    }

    void Layout(const SplashSettings& s, const std::vector<Step>& steps, float time, const glm::vec2& window,
                const std::function<float(const Item&)>& aspectOf, std::vector<DrawItem>& out)
    {
        out.clear();
        if (steps.empty() || window.x <= 0.0f || window.y <= 0.0f)
            return;
        // Şimdiki adım ve adımın içindeki zaman
        size_t index = 0;
        float local = std::max(time, 0.0f);
        while (index + 1 < steps.size() && local >= steps[index].Duration)
        {
            local -= steps[index].Duration;
            ++index;
        }
        const Step& step = steps[index];
        local = std::min(local, step.Duration);
        // 0.45 s belirme, bekleme, 0.45 s kaybolma (yumuşak eğri)
        const float fade = std::min(0.45f, step.Duration * 0.3f);
        float alpha = std::clamp(std::min(local / fade, (step.Duration - local) / fade), 0.0f, 1.0f);
        alpha = alpha * alpha * (3.0f - 2.0f * alpha);
        // Dolly: logo adım boyunca %6 büyür
        const float zoom = s.Animation == SplashSettings::Dolly ? 1.0f + 0.06f * (local / std::max(step.Duration, 0.01f)) : 1.0f;
        const float scale = std::clamp(s.LogoScale, 0.25f, 3.0f) * zoom;

        auto sizeFor = [&](const Item& item, float heightFraction, float maxWidthFraction) {
            const float aspect = aspectOf(item);
            if (aspect <= 0.0f)
                return glm::vec2(0.0f);
            glm::vec2 size(window.y * heightFraction * scale * aspect, window.y * heightFraction * scale);
            const float maxW = window.x * maxWidthFraction;
            if (size.x > maxW)
                size *= maxW / size.x;
            return size;
        };

        if (step.Items.size() == 1)
        {
            const Item& item = step.Items[0];
            const glm::vec2 size = sizeFor(item, item.EngineLogo ? 0.26f : 0.34f, 0.8f);
            if (size.x > 0.0f)
                out.push_back({ &item, window * 0.5f, size, alpha });
            return;
        }

        // Proje logosu + motor logosu (Unity "Unity Logo Below"; burada altında / üstünde / solunda / sağında)
        const Item& logo = step.Items[0];
        const Item& engine = step.Items[1];
        const bool horizontal = s.DrawMode == SplashSettings::EngineLeft || s.DrawMode == SplashSettings::EngineRight;
        glm::vec2 logoSize = sizeFor(logo, 0.30f, horizontal ? 0.5f : 0.8f);
        glm::vec2 engineSize = sizeFor(engine, 0.12f, horizontal ? 0.3f : 0.6f);
        const float gap = window.y * 0.05f;
        const glm::vec2 c = window * 0.5f;
        glm::vec2 logoCenter = c, engineCenter = c;
        if (horizontal)
        {
            const float total = logoSize.x + gap + engineSize.x;
            const float left = c.x - total * 0.5f;
            if (s.DrawMode == SplashSettings::EngineLeft)
            {
                engineCenter.x = left + engineSize.x * 0.5f;
                logoCenter.x = left + engineSize.x + gap + logoSize.x * 0.5f;
            }
            else
            {
                logoCenter.x = left + logoSize.x * 0.5f;
                engineCenter.x = left + logoSize.x + gap + engineSize.x * 0.5f;
            }
        }
        else
        {
            const float total = logoSize.y + gap + engineSize.y;
            const float top = c.y - total * 0.5f;
            if (s.DrawMode == SplashSettings::EngineAbove)
            {
                engineCenter.y = top + engineSize.y * 0.5f;
                logoCenter.y = top + engineSize.y + gap + logoSize.y * 0.5f;
            }
            else
            {
                logoCenter.y = top + logoSize.y * 0.5f;
                engineCenter.y = top + logoSize.y + gap + engineSize.y * 0.5f;
            }
        }
        if (logoSize.x > 0.0f)
            out.push_back({ &logo, logoCenter, logoSize, alpha });
        if (engineSize.x > 0.0f)
            out.push_back({ &engine, engineCenter, engineSize, alpha });
    }
}
