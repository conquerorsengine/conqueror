#pragma once

#include "Core/Base/Base.h"

#include <glm/glm.hpp>

#include <vector>

// 2D çokgen birleşimi (Composite Collider 2D):
//  - Tüm kaynaklar eksene hizalı dikdörtgense (karo haritası, kutular): koordinat sıkıştırmalı ızgarada KESİN birleşim
//  - Aksi halde hücre boyu "cellSize" olan ızgarada örnekleme + Douglas-Peucker sadeleştirme (eğimler düz kalır)
// Konturlarda dolu bölge her kenarın SOLUNDADIR: dış konturlar saat yönü tersine, delikler saat yönünde
// (Box2D zincirinin tek yönlü normali boş tarafa bakar).
namespace Conqueror::PolygonUnion
{
    struct Result
    {
        std::vector<std::vector<glm::vec2>> Loops;   // kapalı konturlar
        std::vector<glm::vec4> Boxes;                // "Polygons" modu: birleşimi örten dikdörtgenler (minX, minY, maxX, maxY)
        bool Exact = false;                          // dikdörtgen yolu kullanıldı
    };

    CQ_API Result Build(const std::vector<std::vector<glm::vec2>>& polygons, float cellSize, bool wantBoxes);
    CQ_API float SignedArea(const std::vector<glm::vec2>& loop);
}
