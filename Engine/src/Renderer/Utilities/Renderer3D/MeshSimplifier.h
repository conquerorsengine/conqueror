#pragma once

#include "Core/Base/Base.h"
#include "Mesh.h"

#include <cstdint>
#include <vector>

// Mesh sadeleştirme (Garland-Heckbert quadric error metric, yarım kenar çöktürme). Konumlar kaynakla aynı kalır,
// köşe öznitelikleri (UV, normal) kopyalanır; UV dikişleri ve açık kenarlar ceza düzlemleriyle korunur,
// üçgen ters dönmesi ve manifold bozan çöktürmeler reddedilir. Otomatik LOD ve küme (Nanite) hiyerarşisi kullanır.
namespace Conqueror::MeshSimplifier
{
    struct Options
    {
        float TargetRatio = 0.5f;      // kalan üçgen oranı (0..1)
        uint32_t TargetTriangles = 0;  // > 0 ise oran yerine
        float MaxError = 1e30f;        // sınır kutusu köşegenine göre bağıl hata üst sınırı (ör. 0.01)
        bool LockBorders = false;      // açık kenar köşeleri hiç taşınmaz (kümeler arası dikişsiz LOD için)
        float SeamWeight = 1.0f;       // UV / normal dikişlerini koruma ağırlığı
    };

    struct Result
    {
        std::vector<Vertex> Vertices;
        std::vector<uint32_t> Indices;
        float Error = 0.0f;            // ulaşılan en büyük bağıl hata
    };

    CQ_API Result Simplify(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices, const Options& options = {});
}
