#pragma once

#include "Core/Base/Base.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// Mesh Paint (Unreal "Vertex Color Painting"): nesneye özel köşe renkleri. Veri bileşende kalır (base64 RGBA8,
// sahneyle kaydedilir), GPU'da örnek başına renk tamponu olarak 6. öznitelikten okunur; PBR malzemelerinde
// albedo ile çarpılır, Shader Graph'te "Vertex Color" düğümü kullanır. Boyanan nesne LOD0 ile çizilir.
namespace Conqueror
{
    class Scene;
    class Entity;
    class Mesh;

    struct VertexPaintRuntime;

    struct VertexPaintComponent
    {
        bool Enabled = true;
        std::string Data;                 // "vp1|n0,n1,...|base64(RGBA8 ...)" (mesh başına köşe sayısı)

        // Çalışma zamanı (kopyalanmaz)
        std::shared_ptr<VertexPaintRuntime> Runtime;
        uint32_t Revision = 1;            // Data değişince artar

        VertexPaintComponent() = default;
        VertexPaintComponent(const VertexPaintComponent& o) : Enabled(o.Enabled), Data(o.Data), Revision(o.Revision + 1) {}
        VertexPaintComponent& operator=(const VertexPaintComponent& o)
        {
            Enabled = o.Enabled;
            Data = o.Data;
            Runtime.reset();
            Revision = o.Revision + 1;
            return *this;
        }
    };

    namespace VertexPaint
    {
        // Varlığın boyanabilir meshleri (Model'in statik meshleri ya da Mesh Renderer meshi)
        CQ_API std::vector<std::shared_ptr<Mesh>> MeshesOf(Entity entity);
        // Çizim öncesi: GPU tamponlarını hazırla; mesh sırasına göre tampon kimlikleri (boşsa boyama yok)
        CQ_API const std::vector<uint32_t>* Prepare(Entity entity);
        // Dünya uzayında küre fırça: merkez etrafındaki köşeleri renge doğru karıştır (yumuşak kenar); boyanan köşe sayısı
        CQ_API int PaintSphere(Entity entity, const glm::vec3& worldCenter, float radius, const glm::vec4& color, float strength, float falloff = 0.5f,
                               uint32_t channelMask = 0xF);
        CQ_API void Fill(Entity entity, const glm::vec4& color);
        // Bir meshin tüm köşe renklerini doğrudan yaz (prosedürel meshler: kırılma parçalarının iç yüzleri)
        CQ_API void SetColors(Entity entity, int meshIndex, const std::vector<glm::vec4>& colors);
        CQ_API glm::vec4 GetColor(Entity entity, int meshIndex, int vertex);
        // Nesnenin gerçek üçgenleriyle ışın kesişimi (fırça için); uzaklık
        CQ_API bool Raycast(Entity entity, const glm::vec3& origin, const glm::vec3& direction, float& outDistance, glm::vec3& outNormal);
        // Çalışma zamanı GPU kaynaklarını bırak (sahne kapanırken)
        CQ_API void ReleaseRuntime(VertexPaintComponent& component);
    }

    void RegisterVertexPaintComponents();
}
