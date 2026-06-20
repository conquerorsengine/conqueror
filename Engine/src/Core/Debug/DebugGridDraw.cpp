#include "DebugGridDraw.h"
#include "DebugDraw.h"
#include "DebugPalette.h"
#include "DebugTimer.h"
#include "DebugSettings.h"

#include <cmath>

namespace Conqueror
{
    static void DrawAxisGrid(
        const glm::vec3& axisU,
        const glm::vec3& axisV,
        float size,
        float step,
        const glm::vec4& minorColor,
        const glm::vec4& majorColor,
        int majorEvery,
        float duration,
        bool depthTested)
    {
        if (!DebugSettings::Get().IsCategoryActive(DebugCategory::Grid))
            return;

        DebugTimer::Submit(duration, depthTested, DebugCategory::Grid, [=]()
        {
            int lineIndex = 0;
            for (float i = -size; i <= size + 0.001f; i += step)
            {
                const glm::vec4& color = (majorEvery > 0 && (lineIndex % majorEvery) == 0) ? majorColor : minorColor;
                DebugDraw::Line(-axisU * size + axisV * i, axisU * size + axisV * i, color, 0.0f, depthTested);
                DebugDraw::Line(axisU * i - axisV * size, axisU * i + axisV * size, color, 0.0f, depthTested);
                lineIndex++;
            }
        });
    }

    void DebugGridDraw::XY(float size, float step, const glm::vec4& minorColor, const glm::vec4& majorColor, int majorEvery, float duration, bool depthTested)
    {
        DrawAxisGrid(glm::vec3(1, 0, 0), glm::vec3(0, 1, 0), size, step, minorColor, majorColor, majorEvery, duration, depthTested);
    }

    void DebugGridDraw::XZ(float size, float step, const glm::vec4& minorColor, const glm::vec4& majorColor, int majorEvery, float duration, bool depthTested)
    {
        DrawAxisGrid(glm::vec3(1, 0, 0), glm::vec3(0, 0, 1), size, step, minorColor, majorColor, majorEvery, duration, depthTested);
    }

    void DebugGridDraw::YZ(float size, float step, const glm::vec4& minorColor, const glm::vec4& majorColor, int majorEvery, float duration, bool depthTested)
    {
        DrawAxisGrid(glm::vec3(0, 1, 0), glm::vec3(0, 0, 1), size, step, minorColor, majorColor, majorEvery, duration, depthTested);
    }

    void DebugGridDraw::InfiniteXZ(const glm::vec3& origin, float step, int lineCount, const glm::vec4& color, float duration, bool depthTested)
    {
        if (!DebugSettings::Get().IsCategoryActive(DebugCategory::Grid))
            return;

        glm::vec4 fadeColor = color;
        fadeColor.a = -color.a;

        DebugTimer::Submit(duration, depthTested, DebugCategory::Grid, [=]()
        {
            float extent = step * static_cast<float>(lineCount);
            for (int i = -lineCount; i <= lineCount; ++i)
            {
                float offset = static_cast<float>(i) * step;
                DebugDraw::Line(origin + glm::vec3(-extent, 0.0f, offset), origin + glm::vec3(extent, 0.0f, offset), fadeColor, 0.0f, depthTested);
                DebugDraw::Line(origin + glm::vec3(offset, 0.0f, -extent), origin + glm::vec3(offset, 0.0f, extent), fadeColor, 0.0f, depthTested);
            }
        });
    }

    void DebugGridDraw::InfiniteXY(const glm::vec3& origin, float step, int lineCount, const glm::vec4& color, float duration, bool depthTested)
    {
        if (!DebugSettings::Get().IsCategoryActive(DebugCategory::Grid))
            return;

        glm::vec4 fadeColor = color;
        fadeColor.a = -color.a;

        DebugTimer::Submit(duration, depthTested, DebugCategory::Grid, [=]()
        {
            float extent = step * static_cast<float>(lineCount);
            for (int i = -lineCount; i <= lineCount; ++i)
            {
                float offset = static_cast<float>(i) * step;
                DebugDraw::Line(origin + glm::vec3(-extent, offset, 0.0f), origin + glm::vec3(extent, offset, 0.0f), fadeColor, 0.0f, depthTested);
                DebugDraw::Line(origin + glm::vec3(offset, -extent, 0.0f), origin + glm::vec3(offset, extent, 0.0f), fadeColor, 0.0f, depthTested);
            }
        });
    }

    void DebugGridDraw::PolarXZ(const glm::vec3& center, float maxRadius, float radiusStep, int segments, const glm::vec4& color, float duration, bool depthTested)
    {
        if (!DebugSettings::Get().IsCategoryActive(DebugCategory::Grid))
            return;

        DebugTimer::Submit(duration, depthTested, DebugCategory::Grid, [=]()
        {
            for (float radius = radiusStep; radius <= maxRadius + 0.001f; radius += radiusStep)
            {
                glm::vec3 prev = center + glm::vec3(radius, 0.0f, 0.0f);
                for (int i = 1; i <= segments; ++i)
                {
                    float angle = (2.0f * 3.14159265359f * static_cast<float>(i)) / static_cast<float>(segments);
                    glm::vec3 point = center + glm::vec3(cosf(angle) * radius, 0.0f, sinf(angle) * radius);
                    DebugDraw::Line(prev, point, color, 0.0f, depthTested);
                    prev = point;
                }
            }

            for (int i = 0; i < segments; ++i)
            {
                float angle = (2.0f * 3.14159265359f * static_cast<float>(i)) / static_cast<float>(segments);
                glm::vec3 dir = glm::vec3(cosf(angle), 0.0f, sinf(angle));
                DebugDraw::Line(center, center + dir * maxRadius, DebugPalette::WithAlpha(color, 0.35f), 0.0f, depthTested);
            }
        });
    }
}
