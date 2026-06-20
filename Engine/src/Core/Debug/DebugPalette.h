#pragma once

#include "DebugPalette.h"
#include <glm/glm.hpp>
#include <algorithm>

namespace Conqueror
{
    struct CQ_API DebugPalette
    {
        static constexpr glm::vec4 White       = { 1.00f, 1.00f, 1.00f, 1.00f };
        static constexpr glm::vec4 Black       = { 0.00f, 0.00f, 0.00f, 1.00f };
        static constexpr glm::vec4 Red         = { 1.00f, 0.20f, 0.20f, 1.00f };
        static constexpr glm::vec4 Green       = { 0.20f, 1.00f, 0.30f, 1.00f };
        static constexpr glm::vec4 Blue        = { 0.25f, 0.55f, 1.00f, 1.00f };
        static constexpr glm::vec4 Yellow      = { 1.00f, 0.92f, 0.20f, 1.00f };
        static constexpr glm::vec4 Cyan        = { 0.20f, 0.95f, 0.95f, 1.00f };
        static constexpr glm::vec4 Magenta     = { 1.00f, 0.20f, 0.90f, 1.00f };
        static constexpr glm::vec4 Orange      = { 1.00f, 0.55f, 0.10f, 1.00f };
        static constexpr glm::vec4 Purple      = { 0.65f, 0.25f, 1.00f, 1.00f };
        static constexpr glm::vec4 Pink        = { 1.00f, 0.45f, 0.70f, 1.00f };
        static constexpr glm::vec4 Lime          = { 0.55f, 1.00f, 0.20f, 1.00f };
        static constexpr glm::vec4 Teal          = { 0.10f, 0.75f, 0.65f, 1.00f };
        static constexpr glm::vec4 Gray          = { 0.55f, 0.55f, 0.55f, 1.00f };
        static constexpr glm::vec4 LightGray     = { 0.75f, 0.75f, 0.75f, 1.00f };
        static constexpr glm::vec4 DarkGray      = { 0.25f, 0.25f, 0.25f, 1.00f };
        static constexpr glm::vec4 GridColor     = { 0.35f, 0.35f, 0.35f, 0.30f };
        static constexpr glm::vec4 GridMajor     = { 0.45f, 0.45f, 0.45f, 0.50f };
        static constexpr glm::vec4 Selection     = { 0.95f, 0.75f, 0.10f, 1.00f };
        static constexpr glm::vec4 Highlight     = { 0.20f, 0.85f, 1.00f, 1.00f };
        static constexpr glm::vec4 Warning         = { 1.00f, 0.70f, 0.10f, 1.00f };
        static constexpr glm::vec4 Error           = { 1.00f, 0.15f, 0.15f, 1.00f };
        static constexpr glm::vec4 Success         = { 0.15f, 0.90f, 0.35f, 1.00f };
        static constexpr glm::vec4 VelocityColor   = { 0.20f, 0.80f, 1.00f, 0.90f };
        static constexpr glm::vec4 ForceColor      = { 1.00f, 0.35f, 0.15f, 0.90f };
        static constexpr glm::vec4 NormalColor     = { 0.35f, 1.00f, 0.55f, 0.90f };
        static constexpr glm::vec4 ContactColor    = { 1.00f, 0.95f, 0.20f, 1.00f };
        static constexpr glm::vec4 BoundsColor     = { 0.55f, 0.85f, 1.00f, 0.75f };
        static constexpr glm::vec4 SolidBounds     = { 0.55f, 0.85f, 1.00f, 0.20f };
        static constexpr glm::vec4 FrustumColor    = { 0.90f, 0.90f, 0.20f, 0.85f };
        static constexpr glm::vec4 XAxis           = { 0.90f, 0.15f, 0.15f, 1.00f };
        static constexpr glm::vec4 YAxis           = { 0.20f, 0.90f, 0.20f, 1.00f };
        static constexpr glm::vec4 ZAxis           = { 0.20f, 0.45f, 1.00f, 1.00f };

        static glm::vec4 WithAlpha(const glm::vec4& color, float alpha)
        {
            return { color.r, color.g, color.b, alpha };
        }

        static glm::vec4 Lerp(const glm::vec4& a, const glm::vec4& b, float t)
        {
            return glm::mix(a, b, t);
        }

        static glm::vec4 Heatmap(float value)
        {
            value = std::max(0.0f, std::min(1.0f, value));
            if (value < 0.5f)
                return Lerp(Blue, Yellow, value * 2.0f);
            return Lerp(Yellow, Red, (value - 0.5f) * 2.0f);
        }

        static glm::vec4 Rainbow(float t)
        {
            t = std::max(0.0f, std::min(1.0f, t));
            const float hue = t * 6.0f;
            const int sector = static_cast<int>(hue) % 6;
            const float frac = hue - static_cast<float>(sector);
            switch (sector)
            {
                case 0: return { 1.0f, frac, 0.0f, 1.0f };
                case 1: return { 1.0f - frac, 1.0f, 0.0f, 1.0f };
                case 2: return { 0.0f, 1.0f, frac, 1.0f };
                case 3: return { 0.0f, 1.0f - frac, 1.0f, 1.0f };
                case 4: return { frac, 0.0f, 1.0f, 1.0f };
                default: return { 1.0f, 0.0f, 1.0f - frac, 1.0f };
            }
        }
    };
}
