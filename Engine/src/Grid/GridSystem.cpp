#include "GridSystem.h"
#include "Scene/Scene.h"
#include "Scene/Entity.h"
#include "Scene/Components.h"
#include "Renderer/Renderer2D.h"
#include "Renderer/RHI/Texture.h"
#include "Renderer/Utilities/Renderer3D/Renderer3D.h"
#include "Renderer/Utilities/Renderer3D/Mesh.h"
#include "Renderer/Utilities/Renderer3D/Material.h"
#include "Core/Debug/DebugDraw.h"
#include "Core/Serialization/SceneSerializer.h"
#include "stb_image_write.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <map>
#include <queue>
#include <unordered_map>
#include <unordered_set>

namespace Conqueror::GridSystem
{
    namespace
    {
        constexpr float kSqrt3Half = 0.86602540378f;

        glm::vec2 Pitch(const GridComponent& g)
        {
            return glm::max(g.CellSize + g.CellGap, glm::vec2(1e-4f));
        }

        bool IsHex(const GridComponent& g) { return g.Layout == GridLayout::HexagonPointyTop || g.Layout == GridLayout::HexagonFlatTop; }

        // Altıgen kafesini düzenli yapan ölçek (komşular eşit uzaklıkta)
        glm::vec2 RegularScale(const GridComponent& g)
        {
            glm::vec2 p = Pitch(g);
            if (g.Layout == GridLayout::HexagonPointyTop) return { 1.0f / p.x, kSqrt3Half / (0.75f * p.y) };
            if (g.Layout == GridLayout::HexagonFlatTop) return { kSqrt3Half / (0.75f * p.x), 1.0f / p.y };
            return { 1.0f / p.x, 1.0f / p.y };
        }

        glm::ivec2 Axial(const GridComponent& g, const glm::ivec2& c)
        {
            if (g.Layout == GridLayout::HexagonPointyTop)
                return { c.x - (c.y - (c.y & 1)) / 2, c.y };
            return { c.x, c.y - (c.x - (c.x & 1)) / 2 };
        }

        glm::mat4 WorldMatrixOf(Entity e)
        {
            glm::mat4 m = e.GetComponent<TransformComponent>().GetTransform();
            for (Entity p = e.GetParent(); p; p = p.GetParent())
                m = p.GetComponent<TransformComponent>().GetTransform() * m;
            return m;
        }

        // GPU kaynak önbellekleri (Shutdown'da GL bağlamı kapanmadan boşaltılır)
        struct Caches
        {
            std::unordered_map<std::string, std::shared_ptr<Texture2D>> Textures;
            std::map<std::tuple<int, int, int, int>, std::shared_ptr<Mesh>> Prisms;
            std::unordered_map<std::string, std::shared_ptr<Material>> Materials;
        };
        Caches s_Caches;

        std::shared_ptr<Texture2D> TextureFor(const std::string& path)
        {
            if (path.empty())
                return nullptr;
            auto it = s_Caches.Textures.find(path);
            if (it != s_Caches.Textures.end())
                return it->second;
            // Proje göreli yollar ("Assets/...") proje dizinine göre çözülür
            auto tex = Texture2D::Create(SceneSerializer::ResolveAssetPath(path));
            if (tex && !tex->GetRendererID())
                tex = nullptr;
            s_Caches.Textures[path] = tex;
            return tex;
        }
    }

    // ════════════════════════════════════════
    //  Matematik
    // ════════════════════════════════════════
    glm::vec2 CellToPlane(const GridComponent& g, const glm::ivec2& c)
    {
        glm::vec2 p = Pitch(g);
        switch (g.Layout)
        {
            case GridLayout::HexagonPointyTop: return { p.x * ((float)c.x + 0.5f * (float)(c.y & 1)), (float)c.y * 0.75f * p.y };
            case GridLayout::HexagonFlatTop: return { (float)c.x * 0.75f * p.x, p.y * ((float)c.y + 0.5f * (float)(c.x & 1)) };
            case GridLayout::Isometric: return { (float)(c.x - c.y) * p.x * 0.5f, (float)(c.x + c.y) * p.y * 0.5f };
            default: return { (float)c.x * p.x, (float)c.y * p.y };
        }
    }

    glm::ivec2 PlaneToCell(const GridComponent& g, const glm::vec2& pt)
    {
        glm::vec2 p = Pitch(g);
        switch (g.Layout)
        {
            case GridLayout::Isometric:
            {
                float a = pt.x / (p.x * 0.5f), b = pt.y / (p.y * 0.5f);
                return { (int)std::lround((a + b) * 0.5f), (int)std::lround((b - a) * 0.5f) };
            }
            case GridLayout::HexagonPointyTop:
            case GridLayout::HexagonFlatTop:
            {
                glm::ivec2 guess;
                if (g.Layout == GridLayout::HexagonPointyTop)
                {
                    guess.y = (int)std::lround(pt.y / (0.75f * p.y));
                    guess.x = (int)std::lround(pt.x / p.x - 0.5f * (float)(guess.y & 1));
                }
                else
                {
                    guess.x = (int)std::lround(pt.x / (0.75f * p.x));
                    guess.y = (int)std::lround(pt.y / p.y - 0.5f * (float)(guess.x & 1));
                }
                // En yakın merkez (düzenli kafes ölçüsünde) = altıgen hücre
                glm::vec2 s = RegularScale(g);
                glm::ivec2 best = guess;
                float bestDist = 1e30f;
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx)
                    {
                        glm::ivec2 c = guess + glm::ivec2(dx, dy);
                        glm::vec2 d = (CellToPlane(g, c) - pt) * s;
                        float dist = glm::dot(d, d);
                        if (dist < bestDist) { bestDist = dist; best = c; }
                    }
                return best;
            }
            default:
                return { (int)std::lround(pt.x / p.x), (int)std::lround(pt.y / p.y) };
        }
    }

    glm::vec3 PlaneToLocal(const GridComponent& g, const glm::vec2& pt, float height)
    {
        return g.Plane == GridPlane::XZ ? glm::vec3(pt.x, height, -pt.y) : glm::vec3(pt.x, pt.y, height);
    }

    glm::vec2 LocalToPlane(const GridComponent& g, const glm::vec3& local)
    {
        return g.Plane == GridPlane::XZ ? glm::vec2(local.x, -local.z) : glm::vec2(local.x, local.y);
    }

    void CellCorners(const GridComponent& g, const glm::ivec2& cell, std::vector<glm::vec2>& out)
    {
        out.clear();
        glm::vec2 c = CellToPlane(g, cell);
        glm::vec2 s = glm::max(g.CellSize, glm::vec2(1e-4f));
        switch (g.Layout)
        {
            case GridLayout::HexagonPointyTop:
                out = { c + glm::vec2(0, s.y * 0.5f), c + glm::vec2(-s.x * 0.5f, s.y * 0.25f), c + glm::vec2(-s.x * 0.5f, -s.y * 0.25f),
                        c + glm::vec2(0, -s.y * 0.5f), c + glm::vec2(s.x * 0.5f, -s.y * 0.25f), c + glm::vec2(s.x * 0.5f, s.y * 0.25f) };
                break;
            case GridLayout::HexagonFlatTop:
                out = { c + glm::vec2(s.x * 0.5f, 0), c + glm::vec2(s.x * 0.25f, s.y * 0.5f), c + glm::vec2(-s.x * 0.25f, s.y * 0.5f),
                        c + glm::vec2(-s.x * 0.5f, 0), c + glm::vec2(-s.x * 0.25f, -s.y * 0.5f), c + glm::vec2(s.x * 0.25f, -s.y * 0.5f) };
                break;
            case GridLayout::Isometric:
                out = { c + glm::vec2(s.x * 0.5f, 0), c + glm::vec2(0, s.y * 0.5f), c + glm::vec2(-s.x * 0.5f, 0), c + glm::vec2(0, -s.y * 0.5f) };
                break;
            default:
                out = { c + glm::vec2(s.x * 0.5f, -s.y * 0.5f), c + glm::vec2(s.x * 0.5f, s.y * 0.5f), c + glm::vec2(-s.x * 0.5f, s.y * 0.5f),
                        c + glm::vec2(-s.x * 0.5f, -s.y * 0.5f) };
                break;
        }
    }

    std::vector<glm::ivec2> Neighbors(const GridComponent& g, const glm::ivec2& c, bool diagonals)
    {
        std::vector<glm::ivec2> out;
        if (g.Layout == GridLayout::HexagonPointyTop)
        {
            static const glm::ivec2 even[6] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { -1, 1 }, { 0, -1 }, { -1, -1 } };
            static const glm::ivec2 odd[6] = { { 1, 0 }, { -1, 0 }, { 1, 1 }, { 0, 1 }, { 1, -1 }, { 0, -1 } };
            const glm::ivec2* t = (c.y & 1) ? odd : even;
            for (int i = 0; i < 6; ++i) out.push_back(c + t[i]);
        }
        else if (g.Layout == GridLayout::HexagonFlatTop)
        {
            static const glm::ivec2 even[6] = { { 0, 1 }, { 0, -1 }, { 1, 0 }, { 1, -1 }, { -1, 0 }, { -1, -1 } };
            static const glm::ivec2 odd[6] = { { 0, 1 }, { 0, -1 }, { 1, 0 }, { 1, 1 }, { -1, 0 }, { -1, 1 } };
            const glm::ivec2* t = (c.x & 1) ? odd : even;
            for (int i = 0; i < 6; ++i) out.push_back(c + t[i]);
        }
        else
        {
            out = { c + glm::ivec2(1, 0), c + glm::ivec2(-1, 0), c + glm::ivec2(0, 1), c + glm::ivec2(0, -1) };
            if (diagonals)
            {
                out.push_back(c + glm::ivec2(1, 1));
                out.push_back(c + glm::ivec2(-1, 1));
                out.push_back(c + glm::ivec2(1, -1));
                out.push_back(c + glm::ivec2(-1, -1));
            }
        }
        return out;
    }

    int Distance(const GridComponent& g, const glm::ivec2& a, const glm::ivec2& b)
    {
        if (IsHex(g))
        {
            glm::ivec2 qa = Axial(g, a), qb = Axial(g, b);
            int dq = qa.x - qb.x, dr = qa.y - qb.y;
            return (std::abs(dq) + std::abs(dr) + std::abs(dq + dr)) / 2;
        }
        return std::abs(a.x - b.x) + std::abs(a.y - b.y);
    }

    std::vector<glm::ivec2> FindPath(const GridComponent& g, const TilemapComponent& map, const glm::ivec2& start, const glm::ivec2& goal, int maxIterations)
    {
        auto walkable = [&](const glm::ivec2& c, int& cost) {
            auto it = map.Cells.find(GridKey(c.x, c.y));
            if (it == map.Cells.end()) return false;
            int t = it->second.Tile;
            if (t < 0 || t >= (int)map.Palette.size() || !map.Palette[(size_t)t].Walkable) return false;
            cost = std::max(1, map.Palette[(size_t)t].Cost);
            return true;
        };
        int goalCost = 1;
        if (!walkable(goal, goalCost))
            return {};
        if (start == goal)
            return { start };
        struct Node { int F, G; uint64_t Key; };
        auto cmp = [](const Node& a, const Node& b) { return a.F > b.F || (a.F == b.F && a.G < b.G); };
        std::priority_queue<Node, std::vector<Node>, decltype(cmp)> open(cmp);
        std::unordered_map<uint64_t, int> gScore;
        std::unordered_map<uint64_t, uint64_t> cameFrom;
        uint64_t startKey = GridKey(start.x, start.y), goalKey = GridKey(goal.x, goal.y);
        gScore[startKey] = 0;
        open.push({ Distance(g, start, goal), 0, startKey });
        int iterations = 0;
        while (!open.empty() && iterations++ < maxIterations)
        {
            Node cur = open.top();
            open.pop();
            if (cur.Key == goalKey)
                break;
            auto gs = gScore.find(cur.Key);
            if (gs != gScore.end() && cur.G > gs->second)
                continue;
            glm::ivec2 c = GridKeyToCell(cur.Key);
            for (const glm::ivec2& n : Neighbors(g, c, false))
            {
                int cost = 1;
                if (!walkable(n, cost)) continue;
                uint64_t nk = GridKey(n.x, n.y);
                int ng = cur.G + cost;
                auto it = gScore.find(nk);
                if (it != gScore.end() && it->second <= ng) continue;
                gScore[nk] = ng;
                cameFrom[nk] = cur.Key;
                open.push({ ng + Distance(g, n, goal), ng, nk });
            }
        }
        if (!cameFrom.count(goalKey))
            return {};
        std::vector<glm::ivec2> path;
        for (uint64_t k = goalKey;; k = cameFrom[k])
        {
            path.push_back(GridKeyToCell(k));
            if (k == startKey || !cameFrom.count(k))
                break;
        }
        std::reverse(path.begin(), path.end());
        return path;
    }

    // ════════════════════════════════════════
    //  Varlık yardımcıları
    // ════════════════════════════════════════
    const GridComponent& GridOf(Entity e, Entity* gridEntity)
    {
        static const GridComponent s_Default;
        for (Entity p = e; p; p = p.GetParent())
            if (p.HasComponent<GridComponent>())
            {
                if (gridEntity) *gridEntity = p;
                return p.GetComponent<GridComponent>();
            }
        if (gridEntity) *gridEntity = Entity{};
        return s_Default;
    }

    glm::vec3 CellToWorld(Entity e, const glm::ivec2& cell, float height)
    {
        const GridComponent& g = GridOf(e);
        return glm::vec3(WorldMatrixOf(e) * glm::vec4(PlaneToLocal(g, CellToPlane(g, cell), height), 1.0f));
    }

    glm::ivec2 WorldToCell(Entity e, const glm::vec3& world)
    {
        const GridComponent& g = GridOf(e);
        glm::vec3 local = glm::vec3(glm::inverse(WorldMatrixOf(e)) * glm::vec4(world, 1.0f));
        return PlaneToCell(g, LocalToPlane(g, local));
    }

    bool RaycastPlane(Entity e, const glm::vec3& origin, const glm::vec3& direction, glm::vec3& hit)
    {
        const GridComponent& g = GridOf(e);
        glm::mat4 world = WorldMatrixOf(e);
        glm::mat4 inv = glm::inverse(world);
        glm::vec3 o = glm::vec3(inv * glm::vec4(origin, 1.0f));
        glm::vec3 d = glm::vec3(inv * glm::vec4(direction, 0.0f));
        int axis = g.Plane == GridPlane::XZ ? 1 : 2;
        if (std::fabs(d[axis]) < 1e-7f)
            return false;
        float t = -o[axis] / d[axis];
        if (t < 0.0f)
            return false;
        hit = glm::vec3(world * glm::vec4(o + d * t, 1.0f));
        return true;
    }

    void CellWorldCorners(Entity e, const glm::ivec2& cell, std::vector<glm::vec3>& out, float height)
    {
        const GridComponent& g = GridOf(e);
        glm::mat4 world = WorldMatrixOf(e);
        std::vector<glm::vec2> corners;
        CellCorners(g, cell, corners);
        out.clear();
        for (auto& p : corners)
            out.push_back(glm::vec3(world * glm::vec4(PlaneToLocal(g, p, height), 1.0f)));
    }

    std::shared_ptr<Texture2D> TileTexture(const std::string& path)
    {
        return TextureFor(path);
    }

    void SetTile(TilemapComponent& map, const glm::ivec2& cell, int tile, const glm::vec4& color)
    {
        if (tile < 0)
        {
            ClearTile(map, cell);
            return;
        }
        TileCell& c = map.Cells[GridKey(cell.x, cell.y)];
        c.Tile = tile;
        c.Color = color;
        map.Version++;
    }

    void ClearTile(TilemapComponent& map, const glm::ivec2& cell)
    {
        if (map.Cells.erase(GridKey(cell.x, cell.y)))
            map.Version++;
    }

    int GetTile(const TilemapComponent& map, const glm::ivec2& cell)
    {
        auto it = map.Cells.find(GridKey(cell.x, cell.y));
        return it == map.Cells.end() ? -1 : it->second.Tile;
    }

    void BoxFill(TilemapComponent& map, glm::ivec2 a, glm::ivec2 b, int tile)
    {
        glm::ivec2 mn = glm::min(a, b), mx = glm::max(a, b);
        for (int y = mn.y; y <= mx.y; ++y)
            for (int x = mn.x; x <= mx.x; ++x)
            {
                if (tile < 0) map.Cells.erase(GridKey(x, y));
                else map.Cells[GridKey(x, y)] = TileCell{ tile, glm::vec4(1.0f), 0.0f };
            }
        map.Version++;
    }

    void FloodFill(const GridComponent& g, TilemapComponent& map, const glm::ivec2& start, int tile, int limit)
    {
        int target = GetTile(map, start);
        if (target == tile)
            return;
        std::vector<glm::ivec2> stack{ start };
        std::unordered_set<uint64_t> seen;
        int count = 0;
        while (!stack.empty() && count < limit)
        {
            glm::ivec2 c = stack.back();
            stack.pop_back();
            uint64_t k = GridKey(c.x, c.y);
            if (!seen.insert(k).second || GetTile(map, c) != target)
                continue;
            if (tile < 0) map.Cells.erase(k);
            else map.Cells[k] = TileCell{ tile, glm::vec4(1.0f), 0.0f };
            ++count;
            // Boş alanı doldururken sınırsız yayılmayı önle: başlangıca uzaklık sınırı
            for (auto& n : Neighbors(g, c, false))
                if (target >= 0 || Distance(g, n, start) <= 24)
                    stack.push_back(n);
        }
        map.Version++;
    }

    // ════════════════════════════════════════
    //  Otomatik döşeme (autotile / terrain)
    // ════════════════════════════════════════
    namespace
    {
        // 8 komşu bitleri: K=1 KD=2 D=4 GD=8 G=16 GB=32 B=64 KB=128 (K = +y)
        constexpr int kN = 1, kNE = 2, kE = 4, kSE = 8, kS = 16, kSW = 32, kW = 64, kNW = 128;

        // Köşe yalnızca iki komşu kenarı da bağlıysa sayılır (47 benzersiz desen)
        int ReduceBlob(int m)
        {
            int r = m & (kN | kE | kS | kW);
            if ((m & kNE) && (m & kN) && (m & kE)) r |= kNE;
            if ((m & kSE) && (m & kS) && (m & kE)) r |= kSE;
            if ((m & kSW) && (m & kS) && (m & kW)) r |= kSW;
            if ((m & kNW) && (m & kN) && (m & kW)) r |= kNW;
            return r;
        }

        // 256 desen -> 0..46 atlas indisi (benzersiz indirgenmiş desenler artan sırada); ters tablo şablon için
        const std::array<int, 256>& BlobTable(std::vector<int>* canonicalOut = nullptr)
        {
            static std::array<int, 256> s_Table{};
            static std::vector<int> s_Canonical;
            if (s_Canonical.empty())
            {
                for (int m = 0; m < 256; ++m)
                {
                    const int r = ReduceBlob(m);
                    if (std::find(s_Canonical.begin(), s_Canonical.end(), r) == s_Canonical.end())
                        s_Canonical.push_back(r);
                }
                std::sort(s_Canonical.begin(), s_Canonical.end());
                for (int m = 0; m < 256; ++m)
                    s_Table[(size_t)m] = (int)(std::find(s_Canonical.begin(), s_Canonical.end(), ReduceBlob(m)) - s_Canonical.begin());
            }
            if (canonicalOut)
                *canonicalOut = s_Canonical;
            return s_Table;
        }

        bool Connects(const TilemapComponent& map, const TileDefinition& def, int tile, const glm::ivec2& cell)
        {
            auto it = map.Cells.find(GridKey(cell.x, cell.y));
            if (it == map.Cells.end() || it->second.Tile < 0 || it->second.Tile >= (int)map.Palette.size())
                return false;
            if (it->second.Tile == tile)
                return true;
            return def.Terrain >= 0 && map.Palette[(size_t)it->second.Tile].Terrain == def.Terrain;
        }
    }

    glm::ivec2 AutotileAtlasSize(int mode)
    {
        return mode == 2 ? glm::ivec2(8, 6) : glm::ivec2(4, 4);
    }

    int AutotileIndex(const TilemapComponent& map, const glm::ivec2& cell)
    {
        auto it = map.Cells.find(GridKey(cell.x, cell.y));
        if (it == map.Cells.end() || it->second.Tile < 0 || it->second.Tile >= (int)map.Palette.size())
            return -1;
        const int tile = it->second.Tile;
        const TileDefinition& def = map.Palette[(size_t)tile];
        if (def.Autotile == 0)
            return -1;
        auto at = [&](int dx, int dy) { return Connects(map, def, tile, cell + glm::ivec2(dx, dy)); };
        if (def.Autotile == 1)
            return (at(0, 1) ? 1 : 0) | (at(1, 0) ? 2 : 0) | (at(0, -1) ? 4 : 0) | (at(-1, 0) ? 8 : 0);
        int m = 0;
        if (at(0, 1)) m |= kN;
        if (at(1, 1)) m |= kNE;
        if (at(1, 0)) m |= kE;
        if (at(1, -1)) m |= kSE;
        if (at(0, -1)) m |= kS;
        if (at(-1, -1)) m |= kSW;
        if (at(-1, 0)) m |= kW;
        if (at(-1, 1)) m |= kNW;
        return BlobTable()[(size_t)m];
    }

    bool WriteAutotileTemplate(int mode, int tilePixels, const std::string& path)
    {
        mode = mode == 2 ? 2 : 1;
        const int t = std::clamp(tilePixels, 8, 256);
        const glm::ivec2 atlas = AutotileAtlasSize(mode);
        const int w = atlas.x * t, h = atlas.y * t;
        std::vector<uint8_t> px((size_t)w * h * 4, 0);
        std::vector<int> canonical;
        BlobTable(&canonical);
        const int slots = mode == 2 ? (int)canonical.size() : 16;
        const int border = std::max(2, t / 6);
        const uint8_t fill[4] = { 112, 184, 92, 255 }, edge[4] = { 58, 104, 46, 255 };
        for (int slot = 0; slot < slots; ++slot)
        {
            int m;
            if (mode == 2)
                m = canonical[(size_t)slot];
            else
                m = ((slot & 1) ? kN : 0) | ((slot & 2) ? kE : 0) | ((slot & 4) ? kS : 0) | ((slot & 8) ? kW : 0);
            const int ox = (slot % atlas.x) * t, oy = (slot / atlas.x) * t;   // görüntüde üstten satır
            for (int y = 0; y < t; ++y)
                for (int x = 0; x < t; ++x)
                {
                    // Görüntü satırı yukarıdan aşağı; yerel "kuzey" = üst kenar
                    const bool north = y < border, south = y >= t - border, west = x < border, east = x >= t - border;
                    bool dark = (north && !(m & kN)) || (south && !(m & kS)) || (west && !(m & kW)) || (east && !(m & kE));
                    // İç köşe: iki kenar bağlı ama çapraz bağlı değil
                    if (mode == 2)
                        dark = dark || (north && east && (m & kN) && (m & kE) && !(m & kNE)) || (south && east && (m & kS) && (m & kE) && !(m & kSE))
                                    || (south && west && (m & kS) && (m & kW) && !(m & kSW)) || (north && west && (m & kN) && (m & kW) && !(m & kNW));
                    uint8_t* p = &px[((size_t)(oy + y) * w + (size_t)(ox + x)) * 4];
                    const uint8_t* c = dark ? edge : fill;
                    p[0] = c[0]; p[1] = c[1]; p[2] = c[2]; p[3] = c[3];
                }
        }
        std::error_code ec;
        std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
        return stbi_write_png(path.c_str(), w, h, 4, px.data(), w * 4) != 0;
    }

    // ════════════════════════════════════════
    //  Çizim
    // ════════════════════════════════════════
    void DrawTilemap2D(Scene& scene, Entity e)
    {
        (void)scene;
        auto& map = e.GetComponent<TilemapComponent>();
        if (map.Cells.empty())
            return;
        const GridComponent& g = GridOf(e);
        glm::mat4 world = WorldMatrixOf(e);
        auto toWorld = [&](const glm::vec2& p) { return glm::vec3(world * glm::vec4(PlaneToLocal(g, p, 0.0f), 1.0f)); };
        std::vector<glm::vec2> corners;
        auto white = Renderer2D::GetWhiteTexture();
        const float outline = std::min(g.CellSize.x, g.CellSize.y) * 0.04f;
        for (const auto& [key, cell] : map.Cells)
        {
            if (cell.Tile < 0 || cell.Tile >= (int)map.Palette.size())
                continue;
            const TileDefinition& def = map.Palette[(size_t)cell.Tile];
            glm::vec4 color = def.Color * cell.Color * map.Tint;
            auto tex = def.Texture.empty() ? white : TextureFor(def.Texture);
            if (!tex) tex = white;
            glm::ivec2 c = GridKeyToCell(key);
            CellCorners(g, c, corners);
            // Doku koordinatları: hücre sınır kutusuna göre
            glm::vec2 mn(1e30f), mx(-1e30f);
            for (auto& p : corners) { mn = glm::min(mn, p); mx = glm::max(mx, p); }
            glm::vec2 uvMin(0.0f), uvSize(1.0f);
            // Otomatik döşeme: atlas hücresi (satır 0 görüntünün üstü; doku dikey çevrilerek yüklenir)
            if (def.Autotile != 0 && corners.size() == 4)
            {
                const int index = AutotileIndex(map, c);
                const glm::ivec2 atlas = AutotileAtlasSize(def.Autotile);
                if (index >= 0)
                {
                    uvSize = glm::vec2(1.0f / (float)atlas.x, 1.0f / (float)atlas.y);
                    uvMin = glm::vec2((float)(index % atlas.x) * uvSize.x, 1.0f - (float)(index / atlas.x + 1) * uvSize.y);
                }
            }
            auto uvOf = [&](const glm::vec2& p) { return uvMin + (p - mn) / glm::max(mx - mn, glm::vec2(1e-5f)) * uvSize; };
            if (corners.size() == 4)
            {
                glm::vec3 pos[4] = { toWorld(corners[3]), toWorld(corners[0]), toWorld(corners[1]), toWorld(corners[2]) };
                glm::vec2 uv[4] = { uvOf(corners[3]), uvOf(corners[0]), uvOf(corners[1]), uvOf(corners[2]) };
                Renderer2D::DrawQuadCorners(pos, uv, tex, color);
            }
            else if (corners.size() == 6)
            {
                // Altıgen = iki dörtgen (0-1-2-3 ve 3-4-5-0)
                glm::vec3 a[4] = { toWorld(corners[0]), toWorld(corners[1]), toWorld(corners[2]), toWorld(corners[3]) };
                glm::vec2 au[4] = { uvOf(corners[0]), uvOf(corners[1]), uvOf(corners[2]), uvOf(corners[3]) };
                glm::vec3 b[4] = { toWorld(corners[3]), toWorld(corners[4]), toWorld(corners[5]), toWorld(corners[0]) };
                glm::vec2 bu[4] = { uvOf(corners[3]), uvOf(corners[4]), uvOf(corners[5]), uvOf(corners[0]) };
                Renderer2D::DrawQuadCorners(a, au, tex, color);
                Renderer2D::DrawQuadCorners(b, bu, tex, color);
            }
            // Otomatik döşenen karolarda kenarları atlas çizer
            if (map.Outline && outline > 0.0f && def.Autotile == 0)
            {
                glm::vec2 center = CellToPlane(g, c);
                glm::vec4 edge(color.r * 0.55f, color.g * 0.55f, color.b * 0.55f, color.a);
                for (size_t i = 0; i < corners.size(); ++i)
                {
                    glm::vec2 p0 = corners[i], p1 = corners[(i + 1) % corners.size()];
                    glm::vec2 in0 = p0 + glm::normalize(center - p0) * outline, in1 = p1 + glm::normalize(center - p1) * outline;
                    glm::vec3 q[4] = { toWorld(p0), toWorld(p1), toWorld(in1), toWorld(in0) };
                    glm::vec2 uv[4] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
                    Renderer2D::DrawQuadCorners(q, uv, white, edge);
                }
            }
        }
    }

    namespace
    {
        // Izgara düzenine göre birim yükseklikte prizma (taban düzlemde, +yükseklik ekseni boyunca)
        std::shared_ptr<Mesh> PrismMesh(const GridComponent& g)
        {
            auto& cache = s_Caches.Prisms;
            auto key = std::make_tuple((int)g.Layout, (int)g.Plane, (int)std::lround(g.CellSize.x * 1000.0f), (int)std::lround(g.CellSize.y * 1000.0f));
            auto it = cache.find(key);
            if (it != cache.end())
                return it->second;
            std::vector<glm::vec2> c;
            GridComponent origin = g;
            CellCorners(origin, { 0, 0 }, c);
            std::vector<Vertex> verts;
            std::vector<uint32_t> idx;
            auto P = [&](const glm::vec2& p, float h) { return PlaneToLocal(g, p, h); };
            const glm::vec3 up = P(glm::vec2(0.0f), 1.0f) - P(glm::vec2(0.0f), 0.0f);
            auto add = [&](const glm::vec3& pos, const glm::vec3& n, const glm::vec2& uv) {
                Vertex v{};
                v.Position = pos;
                v.Normal = n;
                v.Tangent = glm::abs(n.y) < 0.9f ? glm::normalize(glm::cross(glm::vec3(0, 1, 0), n)) : glm::vec3(1, 0, 0);
                v.TexCoords = uv;
                v.TexCoords2 = uv;
                verts.push_back(v);
                return (uint32_t)verts.size() - 1;
            };
            glm::vec2 mn(1e30f), mx(-1e30f);
            for (auto& p : c) { mn = glm::min(mn, p); mx = glm::max(mx, p); }
            auto uvOf = [&](const glm::vec2& p) { return (p - mn) / glm::max(mx - mn, glm::vec2(1e-5f)); };
            const size_t n = c.size();
            // Üst ve alt yüzler (yelpaze)
            uint32_t topCenter = add(P(glm::vec2(0.0f), 1.0f), up, { 0.5f, 0.5f });
            uint32_t topStart = (uint32_t)verts.size();
            for (auto& p : c) add(P(p, 1.0f), up, uvOf(p));
            uint32_t botCenter = add(P(glm::vec2(0.0f), 0.0f), -up, { 0.5f, 0.5f });
            uint32_t botStart = (uint32_t)verts.size();
            for (auto& p : c) add(P(p, 0.0f), -up, uvOf(p));
            // Köşe sırası düzlemde saat yönü tersine. Her iki düzlemde de (XY: z yukarı, XZ: y yukarı, düzlem y = -z)
            // yukarıdan bakınca sıra yine saat yönü tersi kalır: cross(a - c, b - c) = +yukarı -> sargı çevrilmez.
            for (size_t i = 0; i < n; ++i)
            {
                uint32_t a = topStart + (uint32_t)i, b = topStart + (uint32_t)((i + 1) % n);
                idx.push_back(topCenter); idx.push_back(a); idx.push_back(b);
                uint32_t ba = botStart + (uint32_t)i, bb = botStart + (uint32_t)((i + 1) % n);
                idx.push_back(botCenter); idx.push_back(bb); idx.push_back(ba);
            }
            // Yan yüzler
            for (size_t i = 0; i < n; ++i)
            {
                glm::vec2 p0 = c[i], p1 = c[(i + 1) % n];
                glm::vec2 e = p1 - p0;
                glm::vec2 out2(e.y, -e.x);       // saat yönü tersi sıralamada dış normal
                glm::vec3 normal = glm::normalize(P(out2, 0.0f) - P(glm::vec2(0.0f), 0.0f));
                float len = glm::length(e);
                uint32_t v0 = add(P(p0, 0.0f), normal, { 0.0f, 0.0f });
                uint32_t v1 = add(P(p1, 0.0f), normal, { len, 0.0f });
                uint32_t v2 = add(P(p1, 1.0f), normal, { len, 1.0f });
                uint32_t v3 = add(P(p0, 1.0f), normal, { 0.0f, 1.0f });
                idx.insert(idx.end(), { v0, v1, v2, v0, v2, v3 });
            }
            auto mesh = std::make_shared<Mesh>(verts, idx);
            cache[key] = mesh;
            return mesh;
        }

        std::shared_ptr<Material> MaterialFor(const TileDefinition& def, const glm::vec4& color)
        {
            auto& cache = s_Caches.Materials;
            char key[160];
            std::snprintf(key, sizeof(key), "%d_%d_%d_%d|", (int)(color.r * 255), (int)(color.g * 255), (int)(color.b * 255), (int)(color.a * 255));
            std::string k = key + def.Texture;
            auto it = cache.find(k);
            if (it != cache.end())
                return it->second;
            auto m = std::make_shared<Material>();
            m->Albedo = glm::vec3(color);
            m->Roughness = 0.7f;
            m->Metallic = 0.0f;
            m->AlbedoMap = TextureFor(def.Texture);
            m->Opacity = color.a;      // saydam karo (ör. su, alfa 0: görünmez, altındaki deniz düzlemi görünür)
            if (color.a < 0.999f)
                m->Rendering = Material::RenderMode::Transparent;
            if (cache.size() > 2048)
                cache.clear();
            cache[k] = m;
            return m;
        }
    }

    void DrawTilemaps3D(Scene& scene)
    {
        auto view = scene.m_Registry.view<TransformComponent, TilemapComponent>();
        for (auto h : view)
        {
            auto& map = view.get<TilemapComponent>(h);
            if (!map.Render3D || map.Cells.empty() || !scene.IsEntityActive(h))
                continue;
            Entity e{ h, &scene };
            const GridComponent& g = GridOf(e);
            auto mesh = PrismMesh(g);
            glm::mat4 world = WorldMatrixOf(e);
            for (const auto& [key, cell] : map.Cells)
            {
                if (cell.Tile < 0 || cell.Tile >= (int)map.Palette.size())
                    continue;
                const TileDefinition& def = map.Palette[(size_t)cell.Tile];
                glm::ivec2 c = GridKeyToCell(key);
                glm::vec3 base = PlaneToLocal(g, CellToPlane(g, c), cell.Elevation);
                float height = std::max(0.01f, def.Height);
                // Hücre kenarları: karolar arasında ince ayrım (tahta görünümü)
                const float inset = map.Outline ? 0.94f : 1.0f;
                glm::vec3 scale = g.Plane == GridPlane::XZ ? glm::vec3(inset, height, inset) : glm::vec3(inset, inset, height);
                glm::mat4 m = world * glm::translate(glm::mat4(1.0f), base) * glm::scale(glm::mat4(1.0f), scale);
                Renderer3D::DrawMesh(mesh, m, MaterialFor(def, def.Color * cell.Color * map.Tint));
            }
        }
    }

    void DrawEditorGizmos(Scene& scene, const std::vector<uint32_t>& selection)
    {
        std::vector<glm::vec2> corners;
        std::unordered_set<uint32_t> drawn;
        for (uint32_t id : selection)
        {
            entt::entity h = (entt::entity)id;
            if (!scene.m_Registry.valid(h))
                continue;
            Entity e{ h, &scene };
            Entity gridEntity;
            const GridComponent& g = GridOf(e, &gridEntity);
            if (!gridEntity || !g.ShowGridInEditor || !drawn.insert((uint32_t)gridEntity).second)
                continue;
            if (!e.HasComponent<GridComponent>() && !e.HasComponent<TilemapComponent>())
                continue;
            glm::mat4 world = WorldMatrixOf(e.HasComponent<TilemapComponent>() ? e : gridEntity);
            auto toWorld = [&](const glm::vec2& p) { return glm::vec3(world * glm::vec4(PlaneToLocal(g, p, 0.001f), 1.0f)); };
            const glm::vec4 col(0.45f, 0.8f, 1.0f, 0.35f);
            // 3D karolar ızgarayı örtsün (derinlik testi); 2D'de her zaman görünür
            const bool depthTested = e.HasComponent<TilemapComponent>() ? e.GetComponent<TilemapComponent>().Render3D : g.Plane == GridPlane::XZ;
            const int r = std::clamp(g.EditorGridExtent, 1, 64);
            for (int y = -r; y <= r; ++y)
                for (int x = -r; x <= r; ++x)
                {
                    CellCorners(g, { x, y }, corners);
                    for (size_t i = 0; i < corners.size(); ++i)
                        DebugDraw::Line(toWorld(corners[i]), toWorld(corners[(i + 1) % corners.size()]), col, 0.0f, depthTested);
                }
        }
    }

    void Shutdown()
    {
        s_Caches.Textures.clear();
        s_Caches.Prisms.clear();
        s_Caches.Materials.clear();
    }
}
