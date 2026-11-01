#pragma once

#include "Core/Base/Base.h"
#include "Grid/GridComponents.h"

#include <glm/glm.hpp>

#include <memory>
#include <string>
#include <vector>

namespace Conqueror
{
    class Scene;
    class Entity;
    class Texture2D;

    namespace GridSystem
    {
        // Hücre merkezi (ızgara düzleminde, ızgara yerel uzayı)
        CQ_API glm::vec2 CellToPlane(const GridComponent& grid, const glm::ivec2& cell);
        CQ_API glm::ivec2 PlaneToCell(const GridComponent& grid, const glm::vec2& point);
        // Düzlem <-> yerel 3B (XY: z=height, XZ: y=height)
        CQ_API glm::vec3 PlaneToLocal(const GridComponent& grid, const glm::vec2& point, float height = 0.0f);
        CQ_API glm::vec2 LocalToPlane(const GridComponent& grid, const glm::vec3& local);
        // Hücre köşeleri (düzlemde, saat yönü tersine): dikdörtgen/izometrik 4, altıgen 6
        CQ_API void CellCorners(const GridComponent& grid, const glm::ivec2& cell, std::vector<glm::vec2>& corners);

        CQ_API std::vector<glm::ivec2> Neighbors(const GridComponent& grid, const glm::ivec2& cell, bool diagonals = false);
        CQ_API int Distance(const GridComponent& grid, const glm::ivec2& a, const glm::ivec2& b);
        // A*: yürünebilir karolar üzerinden (boş hücre yürünemez); yol başlangıç ve hedefi içerir, yoksa boş
        CQ_API std::vector<glm::ivec2> FindPath(const GridComponent& grid, const TilemapComponent& map, const glm::ivec2& start,
                                                const glm::ivec2& goal, int maxIterations = 20000);

        // Karo haritasının ızgarası (kendisi ya da üst varlıklardan biri); yoksa varsayılan ızgara
        CQ_API const GridComponent& GridOf(Entity tilemapEntity, Entity* gridEntity = nullptr);
        // Dünya <-> hücre (karo haritası varlığının dönüşümüyle)
        CQ_API glm::vec3 CellToWorld(Entity tilemapEntity, const glm::ivec2& cell, float height = 0.0f);
        CQ_API glm::ivec2 WorldToCell(Entity tilemapEntity, const glm::vec3& world);
        // Işını ızgara düzlemiyle kesiştir
        CQ_API bool RaycastPlane(Entity tilemapEntity, const glm::vec3& origin, const glm::vec3& direction, glm::vec3& hit);
        // Hücre köşeleri dünya uzayında (düzenleyici önizlemesi); height: düzlemden yükseklik
        CQ_API void CellWorldCorners(Entity tilemapEntity, const glm::ivec2& cell, std::vector<glm::vec3>& out, float height = 0.0f);
        // Karo dokusu (önbellekli; boş yol = nullptr)
        CQ_API std::shared_ptr<Texture2D> TileTexture(const std::string& path);

        // Düzenleme yardımcıları (Version artırır)
        CQ_API void SetTile(TilemapComponent& map, const glm::ivec2& cell, int tile, const glm::vec4& color = glm::vec4(1.0f));
        CQ_API void ClearTile(TilemapComponent& map, const glm::ivec2& cell);
        CQ_API int GetTile(const TilemapComponent& map, const glm::ivec2& cell);
        CQ_API void BoxFill(TilemapComponent& map, glm::ivec2 a, glm::ivec2 b, int tile);
        CQ_API void FloodFill(const GridComponent& grid, TilemapComponent& map, const glm::ivec2& start, int tile, int limit = 4096);

        // Otomatik döşeme: hücrenin atlas indisi (karo Autotile değilse -1). Dikdörtgen / izometrik ızgara.
        CQ_API int AutotileIndex(const TilemapComponent& map, const glm::ivec2& cell);
        // Atlas düzeni (sütun, satır): 16 karo 4x4, blob 8x6
        CQ_API glm::ivec2 AutotileAtlasSize(int mode);
        // Sanatçı için şablon PNG: her atlas hücresi bağlı olmayan kenarlarında koyu kenarlık, iç köşelerde köşe parçası
        CQ_API bool WriteAutotileTemplate(int mode, int tilePixels, const std::string& path);

        // Çizim
        CQ_API void DrawTilemap2D(Scene& scene, Entity tilemapEntity);
        CQ_API void DrawTilemaps3D(Scene& scene);
        CQ_API void DrawEditorGizmos(Scene& scene, const std::vector<uint32_t>& selection);
        CQ_API void Shutdown();
    }
}
