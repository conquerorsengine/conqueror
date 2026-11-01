#pragma once

#include "Core/Base/Base.h"

#include <glm/glm.hpp>

#include <memory>
#include <string>
#include <vector>

namespace Conqueror
{
    class Texture2D;

    // Kameraya bakan şerit çizici (Line Renderer, Trail Renderer). Noktalardan CPU'da üçgen
    // şeridi üretir; saydam geçişte (opak geometriden sonra) çizilir.
    class CQ_API RibbonRenderer
    {
    public:
        struct Point
        {
            glm::vec3 Position;
            float Width;
            glm::vec4 Color;
        };

        struct Params
        {
            bool AlignToView = true;          // false: ViewNormal'a dik (TransformZ hizalama)
            glm::vec3 ViewNormal{ 0, 0, 1 };
            bool TileTexture = false;         // UV.x = mesafe / genişlik
            bool Additive = false;
            bool Loop = false;
            float Emissive = 1.0f;
            std::shared_ptr<Texture2D> Texture;
        };

        static void Draw(const std::vector<Point>& points, const Params& params);
        static std::shared_ptr<Texture2D> LoadTexture(const std::string& path);
        static void Shutdown();
    };
}
