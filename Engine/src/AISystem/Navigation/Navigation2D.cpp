#include "Navigation2D.h"
#include "Scene/Scene.h"
#include "Scene/Entity.h"
#include "Scene/Components.h"
#include "Scene/PhysicsComponents.h"
#include "Scene/ComponentRegistry.h"
#include "Scene/GodotNodes.h"
#include "Grid/GridComponents.h"
#include "Grid/GridSystem.h"
#include "Core/Debug/DebugDraw.h"
#include "Core/Logging/Log.h"
#include "Core/PhysicsSystem/Physics2D/PhysicsWorld2D.h"

#include <IconsFontAwesome6.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <queue>
#include <unordered_map>

namespace Conqueror
{
    namespace
    {
        constexpr int kMaxCells = 4 * 1024 * 1024;

        struct LinkEdge
        {
            int A = -1, B = -1;
            float Cost = 1.0f;
            bool Bidirectional = true;
        };

        struct Grid
        {
            glm::vec2 Origin{ 0.0f };     // sol alt hücrenin köşesi
            float Cell = 0.1f;
            int W = 0, H = 0;
            std::vector<uint8_t> Walk;     // pişirilmiş (statik)
            std::vector<uint8_t> Dyn;      // bu karede Carve engellerin kapattığı
            std::vector<float> Cost;       // hücre başına yol maliyeti çarpanı
            std::vector<LinkEdge> Links;
            std::unordered_map<int, std::vector<int>> LinkFrom;   // hücre -> bağlantı indisleri
            uint64_t Signature = 0;
            bool Valid = false;
            float AgentRadius = 0.25f;

            int Index(int x, int y) const { return y * W + x; }
            bool InBounds(int x, int y) const { return x >= 0 && y >= 0 && x < W && y < H; }
            bool Open(int i) const { return Walk[(size_t)i] && !Dyn[(size_t)i]; }
            bool OpenXY(int x, int y) const { return InBounds(x, y) && Open(Index(x, y)); }
            glm::ivec2 CellOf(const glm::vec2& p) const
            {
                return { (int)std::floor((p.x - Origin.x) / Cell), (int)std::floor((p.y - Origin.y) / Cell) };
            }
            glm::vec2 Center(int x, int y) const { return Origin + (glm::vec2((float)x, (float)y) + 0.5f) * Cell; }
            glm::vec2 Center(int i) const { return Center(i % W, i / W); }
        };

        std::unordered_map<const Scene*, Grid>& Grids()
        {
            static std::unordered_map<const Scene*, Grid> s;
            return s;
        }

        glm::vec2 ToWorld(const glm::mat4& m, const glm::vec2& p)
        {
            const glm::vec4 w = m * glm::vec4(p, 0.0f, 1.0f);
            return { w.x, w.y };
        }

        bool PointInPolygon(const std::vector<glm::vec2>& poly, const glm::vec2& p)
        {
            bool inside = false;
            for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++)
            {
                const glm::vec2& a = poly[i];
                const glm::vec2& b = poly[j];
                if (((a.y > p.y) != (b.y > p.y)) && (p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y + 1e-12f) + a.x))
                    inside = !inside;
            }
            return inside;
        }

        float SegmentDistance(const glm::vec2& p, const glm::vec2& a, const glm::vec2& b)
        {
            const glm::vec2 ab = b - a;
            const float t = glm::clamp(glm::dot(p - a, ab) / std::max(glm::dot(ab, ab), 1e-12f), 0.0f, 1.0f);
            return glm::length(p - (a + ab * t));
        }

        uint64_t Mix(uint64_t h, float v)
        {
            uint32_t bits;
            std::memcpy(&bits, &v, sizeof(bits));
            h ^= bits + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
            return h;
        }

        bool StaticBody(Scene& scene, entt::entity e)
        {
            auto* rb = scene.m_Registry.try_get<RigidBody2DComponent>(e);
            return !rb || rb->Type == RigidBody2DComponent::BodyType::Static;
        }

        // Bölgelerin ve statik engellerin imzası (değişince yeniden pişirilir)
        uint64_t Signature(Scene& scene)
        {
            uint64_t h = 1469598103934665603ull;
            auto mixMatrix = [&](entt::entity e) {
                const glm::mat4 m = scene.ComputeWorldMatrix(e);
                for (int c : { 0, 1, 3 })
                    for (int r = 0; r < 2; ++r)
                        h = Mix(h, m[c][r]);
            };
            for (auto [e, r] : scene.m_Registry.view<NavigationRegion2DComponent>().each())
            {
                if (!r.Enabled || !scene.IsEntityActive(e))
                    continue;
                h = Mix(h, r.CellSize);
                h = Mix(h, r.AgentRadius);
                h = Mix(h, r.TravelCost);
                h = Mix(h, r.ParseColliders ? 1.0f : 0.0f);
                for (const auto& p : r.Outline) { h = Mix(h, p.x); h = Mix(h, p.y); }
                mixMatrix(e);
            }
            for (auto [e, c] : scene.m_Registry.view<BoxCollider2DComponent>().each()) { mixMatrix(e); h = Mix(h, c.Size.x + c.Size.y + c.Offset.x + c.Offset.y); }
            for (auto [e, c] : scene.m_Registry.view<CircleCollider2DComponent>().each()) { mixMatrix(e); h = Mix(h, c.Radius + c.Offset.x + c.Offset.y); }
            for (auto [e, c] : scene.m_Registry.view<CapsuleCollider2DComponent>().each()) { mixMatrix(e); h = Mix(h, c.Size.x + c.Size.y); }
            for (auto [e, c] : scene.m_Registry.view<PolygonCollider2DComponent>().each()) { mixMatrix(e); h = Mix(h, (float)c.Points.size()); for (auto& p : c.Points) h = Mix(h, p.x + 3.0f * p.y); }
            for (auto [e, t] : scene.m_Registry.view<TilemapComponent>().each()) { mixMatrix(e); h = Mix(h, (float)t.Version); h = Mix(h, (float)t.Cells.size()); }
            for (auto [e, l] : scene.m_Registry.view<NavigationLink2DComponent>().each())
            {
                mixMatrix(e);
                h = Mix(h, l.StartPosition.x + 7.0f * l.StartPosition.y + 13.0f * l.EndPosition.x + 17.0f * l.EndPosition.y);
                h = Mix(h, (l.Enabled ? 1.0f : 0.0f) + (l.Bidirectional ? 2.0f : 0.0f) + 4.0f * l.TravelCost);
            }
            return h;
        }

        // Kapalı (engel) hücreler işaretlenir: kontrol fonksiyonu dünya noktasının engel içinde olup olmadığını söyler
        void MarkShape(Grid& g, std::vector<uint8_t>& blocked, const glm::vec2& mn, const glm::vec2& mx, const std::function<bool(const glm::vec2&)>& inside)
        {
            const glm::ivec2 a = glm::max(g.CellOf(mn), glm::ivec2(0));
            const glm::ivec2 b = glm::min(g.CellOf(mx), glm::ivec2(g.W - 1, g.H - 1));
            for (int y = a.y; y <= b.y; ++y)
                for (int x = a.x; x <= b.x; ++x)
                    if (inside(g.Center(x, y)))
                        blocked[(size_t)g.Index(x, y)] = 1;
        }

        void BakeGrid(Scene& scene, Grid& g)
        {
            struct Region { std::vector<glm::vec2> Poly; glm::vec2 Min, Max; float Cost; bool Parse; };
            std::vector<Region> regions;
            float cell = 1e9f, radius = 0.0f;
            glm::vec2 mn(1e30f), mx(-1e30f);
            for (auto [e, r] : scene.m_Registry.view<NavigationRegion2DComponent>().each())
            {
                r.Baked = false;
                r.WalkableCells = 0;
                if (!r.Enabled || r.Outline.size() < 3 || !scene.IsEntityActive(e))
                    continue;
                const glm::mat4 m = scene.ComputeWorldMatrix(e);
                Region reg;
                reg.Min = glm::vec2(1e30f);
                reg.Max = glm::vec2(-1e30f);
                for (const auto& p : r.Outline)
                {
                    const glm::vec2 w = ToWorld(m, p);
                    reg.Poly.push_back(w);
                    reg.Min = glm::min(reg.Min, w);
                    reg.Max = glm::max(reg.Max, w);
                }
                reg.Cost = std::max(r.TravelCost, 0.01f);
                reg.Parse = r.ParseColliders;
                regions.push_back(reg);
                cell = std::min(cell, std::max(r.CellSize, 0.02f));
                radius = std::max(radius, std::max(r.AgentRadius, 0.0f));
                mn = glm::min(mn, reg.Min);
                mx = glm::max(mx, reg.Max);
            }
            g = Grid{};
            if (regions.empty())
                return;
            // Çok büyük bölgede hücre büyütülür
            while ((double)((mx.x - mn.x) / cell + 2.0) * ((mx.y - mn.y) / cell + 2.0) > kMaxCells)
                cell *= 1.5f;
            g.Cell = cell;
            g.AgentRadius = radius;
            g.Origin = mn - glm::vec2(cell);
            g.W = (int)std::ceil((mx.x - mn.x) / cell) + 2;
            g.H = (int)std::ceil((mx.y - mn.y) / cell) + 2;
            const size_t n = (size_t)g.W * (size_t)g.H;
            g.Walk.assign(n, 0);
            g.Dyn.assign(n, 0);
            g.Cost.assign(n, 1.0f);

            // 1) Bölgelerin içi (ilk kapsayan bölgenin maliyeti)
            std::vector<uint8_t> inside(n, 0);
            for (const Region& reg : regions)
                MarkShape(g, inside, reg.Min, reg.Max, [&](const glm::vec2& p) { return PointInPolygon(reg.Poly, p); });
            for (size_t i = 0; i < n; ++i)
                if (inside[i])
                    for (const Region& reg : regions)
                        if (PointInPolygon(reg.Poly, g.Center((int)i)))
                        {
                            g.Cost[i] = reg.Cost;
                            break;
                        }

            // 2) Engeller: bölge içindeki statik çarpıştırıcılar ve yürünemez / katı karolar
            const bool parse = std::any_of(regions.begin(), regions.end(), [](const Region& r) { return r.Parse; });
            std::vector<uint8_t> blocked(n, 0);
            if (parse)
            {
                for (auto [e, c] : scene.m_Registry.view<BoxCollider2DComponent>().each())
                {
                    if (c.IsTrigger || !StaticBody(scene, e) || !scene.IsEntityActive(e))
                        continue;
                    const glm::mat4 m = scene.ComputeWorldMatrix(e);
                    const glm::mat4 inv = glm::inverse(m);
                    glm::vec2 bmn(1e30f), bmx(-1e30f);
                    for (int k = 0; k < 4; ++k)
                    {
                        const glm::vec2 corner = c.Offset + glm::vec2((k & 1) ? 0.5f : -0.5f, (k & 2) ? 0.5f : -0.5f) * c.Size;
                        const glm::vec2 w = ToWorld(m, corner);
                        bmn = glm::min(bmn, w);
                        bmx = glm::max(bmx, w);
                    }
                    const glm::vec2 half = c.Size * 0.5f + glm::vec2(c.EdgeRadius);
                    MarkShape(g, blocked, bmn, bmx, [&](const glm::vec2& p) {
                        const glm::vec2 l = ToWorld(inv, p) - c.Offset;
                        return std::abs(l.x) <= half.x && std::abs(l.y) <= half.y;
                    });
                }
                for (auto [e, c] : scene.m_Registry.view<CircleCollider2DComponent>().each())
                {
                    if (c.IsTrigger || !StaticBody(scene, e) || !scene.IsEntityActive(e))
                        continue;
                    const glm::mat4 m = scene.ComputeWorldMatrix(e);
                    const glm::vec2 center = ToWorld(m, c.Offset);
                    const float r = c.Radius * std::max(glm::length(glm::vec2(m[0])), glm::length(glm::vec2(m[1])));
                    MarkShape(g, blocked, center - glm::vec2(r), center + glm::vec2(r),
                              [&](const glm::vec2& p) { return glm::length(p - center) <= r; });
                }
                for (auto [e, c] : scene.m_Registry.view<CapsuleCollider2DComponent>().each())
                {
                    if (c.IsTrigger || !StaticBody(scene, e) || !scene.IsEntityActive(e))
                        continue;
                    const glm::mat4 m = scene.ComputeWorldMatrix(e);
                    const glm::mat4 inv = glm::inverse(m);
                    const bool vertical = c.Direction == 0;
                    const float r = (vertical ? c.Size.x : c.Size.y) * 0.5f;
                    const float halfLen = std::max((vertical ? c.Size.y : c.Size.x) * 0.5f - r, 0.0f);
                    const glm::vec2 axis = vertical ? glm::vec2(0, halfLen) : glm::vec2(halfLen, 0);
                    glm::vec2 bmn(1e30f), bmx(-1e30f);
                    for (int k = 0; k < 4; ++k)
                    {
                        const glm::vec2 corner = c.Offset + glm::vec2((k & 1) ? 0.5f : -0.5f, (k & 2) ? 0.5f : -0.5f) * c.Size;
                        const glm::vec2 w = ToWorld(m, corner);
                        bmn = glm::min(bmn, w);
                        bmx = glm::max(bmx, w);
                    }
                    MarkShape(g, blocked, bmn, bmx, [&](const glm::vec2& p) {
                        const glm::vec2 l = ToWorld(inv, p) - c.Offset;
                        return SegmentDistance(l, -axis, axis) <= r;
                    });
                }
                for (auto [e, c] : scene.m_Registry.view<PolygonCollider2DComponent>().each())
                {
                    if (c.IsTrigger || !StaticBody(scene, e) || !scene.IsEntityActive(e) || c.Points.size() < 3)
                        continue;
                    const glm::mat4 m = scene.ComputeWorldMatrix(e);
                    auto addPath = [&](const std::vector<glm::vec2>& pts) {
                        if (pts.size() < 3)
                            return;
                        std::vector<glm::vec2> poly;
                        glm::vec2 bmn(1e30f), bmx(-1e30f);
                        for (const auto& p : pts)
                        {
                            const glm::vec2 w = ToWorld(m, p + c.Offset);
                            poly.push_back(w);
                            bmn = glm::min(bmn, w);
                            bmx = glm::max(bmx, w);
                        }
                        MarkShape(g, blocked, bmn, bmx, [&](const glm::vec2& p) { return PointInPolygon(poly, p); });
                    };
                    addPath(c.Points);
                    for (const auto& extra : c.ExtraPaths)
                        addPath(extra);
                }
                for (auto [e, c] : scene.m_Registry.view<EdgeCollider2DComponent>().each())
                {
                    if (!StaticBody(scene, e) || !scene.IsEntityActive(e) || c.Points.size() < 2)
                        continue;
                    const glm::mat4 m = scene.ComputeWorldMatrix(e);
                    const float thick = std::max(c.EdgeRadius, g.Cell * 0.5f);
                    for (size_t i = 0; i + 1 < c.Points.size(); ++i)
                    {
                        const glm::vec2 a = ToWorld(m, c.Points[i] + c.Offset), b = ToWorld(m, c.Points[i + 1] + c.Offset);
                        MarkShape(g, blocked, glm::min(a, b) - glm::vec2(thick), glm::max(a, b) + glm::vec2(thick),
                                  [&](const glm::vec2& p) { return SegmentDistance(p, a, b) <= thick; });
                    }
                }
                // Karo haritaları (XY ızgarası): yürünemez karolar ve Tilemap Collider 2D'li haritanın katı karoları
                for (auto [e, map] : scene.m_Registry.view<TilemapComponent>().each())
                {
                    Entity te{ e, &scene };
                    if (!scene.IsEntityActive(e) || GridSystem::GridOf(te).Plane != GridPlane::XY)
                        continue;
                    const bool collider = scene.m_Registry.all_of<TilemapCollider2DComponent>(e);
                    std::vector<glm::vec3> corners;
                    for (const auto& [key, cellData] : map.Cells)
                    {
                        if (cellData.Tile < 0 || cellData.Tile >= (int)map.Palette.size())
                            continue;
                        const TileDefinition& def = map.Palette[(size_t)cellData.Tile];
                        if (def.Walkable && !(collider && def.Solid))
                            continue;
                        GridSystem::CellWorldCorners(te, GridKeyToCell(key), corners);
                        if (corners.size() < 3)
                            continue;
                        std::vector<glm::vec2> poly;
                        glm::vec2 bmn(1e30f), bmx(-1e30f);
                        for (const auto& c3 : corners)
                        {
                            poly.push_back({ c3.x, c3.y });
                            bmn = glm::min(bmn, poly.back());
                            bmx = glm::max(bmx, poly.back());
                        }
                        MarkShape(g, blocked, bmn, bmx, [&](const glm::vec2& p) { return PointInPolygon(poly, p); });
                    }
                }
            }

            // 3) Ajan yarıçapı: bölge dışına ya da engele radius'tan yakın hücreler kapanır (öklid uzaklık dönüşümü, iki geçiş)
            std::vector<float> dist(n, 1e9f);
            for (size_t i = 0; i < n; ++i)
                if (!inside[i] || blocked[i])
                    dist[i] = 0.0f;
            const float d1 = 1.0f, d2 = 1.41421356f;
            for (int y = 0; y < g.H; ++y)
                for (int x = 0; x < g.W; ++x)
                {
                    float& d = dist[(size_t)g.Index(x, y)];
                    if (x > 0) d = std::min(d, dist[(size_t)g.Index(x - 1, y)] + d1);
                    if (y > 0) d = std::min(d, dist[(size_t)g.Index(x, y - 1)] + d1);
                    if (x > 0 && y > 0) d = std::min(d, dist[(size_t)g.Index(x - 1, y - 1)] + d2);
                    if (x + 1 < g.W && y > 0) d = std::min(d, dist[(size_t)g.Index(x + 1, y - 1)] + d2);
                }
            for (int y = g.H - 1; y >= 0; --y)
                for (int x = g.W - 1; x >= 0; --x)
                {
                    float& d = dist[(size_t)g.Index(x, y)];
                    if (x + 1 < g.W) d = std::min(d, dist[(size_t)g.Index(x + 1, y)] + d1);
                    if (y + 1 < g.H) d = std::min(d, dist[(size_t)g.Index(x, y + 1)] + d1);
                    if (x + 1 < g.W && y + 1 < g.H) d = std::min(d, dist[(size_t)g.Index(x + 1, y + 1)] + d2);
                    if (x > 0 && y + 1 < g.H) d = std::min(d, dist[(size_t)g.Index(x - 1, y + 1)] + d2);
                }
            // Hücre merkezinden engele uzaklık ~ (dist - 0.5) hücre
            const float need = radius / cell + 0.5f;
            int walkable = 0;
            for (size_t i = 0; i < n; ++i)
            {
                g.Walk[i] = (inside[i] && !blocked[i] && dist[i] >= need) ? 1 : 0;
                walkable += g.Walk[i];
            }

            // 4) Bağlantılar (Navigation Link 2D): uçlar en yakın yürünebilir hücreye
            auto nearestOpen = [&](const glm::vec2& p, float maxDist) -> int {
                const glm::ivec2 c = g.CellOf(p);
                const int reach = (int)std::ceil(maxDist / g.Cell);
                int best = -1;
                float bestD = 1e30f;
                for (int dy = -reach; dy <= reach; ++dy)
                    for (int dx = -reach; dx <= reach; ++dx)
                    {
                        const int x = c.x + dx, y = c.y + dy;
                        if (!g.InBounds(x, y) || !g.Walk[(size_t)g.Index(x, y)])
                            continue;
                        const float d = glm::length(g.Center(x, y) - p);
                        if (d < bestD) { bestD = d; best = g.Index(x, y); }
                    }
                return bestD <= maxDist ? best : -1;
            };
            for (auto [e, l] : scene.m_Registry.view<NavigationLink2DComponent>().each())
            {
                if (!l.Enabled || !scene.IsEntityActive(e))
                    continue;
                const glm::mat4 m = scene.ComputeWorldMatrix(e);
                LinkEdge link;
                link.A = nearestOpen(ToWorld(m, l.StartPosition), 1.0f);
                link.B = nearestOpen(ToWorld(m, l.EndPosition), 1.0f);
                link.Bidirectional = l.Bidirectional;
                link.Cost = std::max(l.TravelCost, 0.01f);
                if (link.A < 0 || link.B < 0 || link.A == link.B)
                    continue;
                const int idx = (int)g.Links.size();
                g.Links.push_back(link);
                g.LinkFrom[link.A].push_back(idx);
                if (link.Bidirectional)
                    g.LinkFrom[link.B].push_back(idx);
            }

            for (auto [e, r] : scene.m_Registry.view<NavigationRegion2DComponent>().each())
            {
                if (!r.Enabled || r.Outline.size() < 3)
                    continue;
                r.Baked = true;
                r.WalkableCells = walkable;
            }
            g.Valid = walkable > 0;
        }

        Grid* GetGrid(Scene& scene, bool rebakeIfChanged)
        {
            Grid& g = Grids()[&scene];
            if (rebakeIfChanged || !g.Valid)
            {
                const uint64_t sig = Signature(scene);
                if (sig != g.Signature || (!g.Valid && g.W == 0))
                {
                    BakeGrid(scene, g);
                    g.Signature = sig;
                }
            }
            return g.Valid ? &g : nullptr;
        }

        // Carve engeller (bu kare): yarıçap + ajan yarıçapı kadar kapatır
        void UpdateDynamic(Scene& scene, Grid& g)
        {
            std::fill(g.Dyn.begin(), g.Dyn.end(), 0);
            for (auto [e, o] : scene.m_Registry.view<NavigationObstacle2DComponent>().each())
            {
                if (!o.CarveNavigation || !scene.IsEntityActive(e))
                    continue;
                const glm::mat4 m = scene.ComputeWorldMatrix(e);
                const float r = g.AgentRadius;
                if (o.Size.x > 0.0f && o.Size.y > 0.0f)
                {
                    const glm::mat4 inv = glm::inverse(m);
                    const glm::vec2 half = o.Size * 0.5f;
                    const float scale = std::max(glm::length(glm::vec2(m[0])), 1e-4f);
                    glm::vec2 bmn(1e30f), bmx(-1e30f);
                    for (int k = 0; k < 4; ++k)
                    {
                        const glm::vec2 w = ToWorld(m, glm::vec2((k & 1) ? half.x : -half.x, (k & 2) ? half.y : -half.y));
                        bmn = glm::min(bmn, w);
                        bmx = glm::max(bmx, w);
                    }
                    MarkShape(g, g.Dyn, bmn - glm::vec2(r), bmx + glm::vec2(r), [&](const glm::vec2& p) {
                        const glm::vec2 l = ToWorld(inv, p);
                        const glm::vec2 d = glm::max(glm::abs(l) - half, glm::vec2(0.0f));
                        return glm::length(d) * scale <= r;
                    });
                }
                else
                {
                    const glm::vec2 c = ToWorld(m, glm::vec2(0.0f));
                    const float rr = o.Radius * std::max(glm::length(glm::vec2(m[0])), glm::length(glm::vec2(m[1]))) + r;
                    MarkShape(g, g.Dyn, c - glm::vec2(rr), c + glm::vec2(rr), [&](const glm::vec2& p) { return glm::length(p - c) <= rr; });
                }
            }
        }

        int NearestOpen(const Grid& g, const glm::vec2& p, float maxDist)
        {
            const glm::ivec2 c = g.CellOf(p);
            if (g.OpenXY(c.x, c.y))
                return g.Index(c.x, c.y);
            const int reach = std::max(1, (int)std::ceil(maxDist / g.Cell));
            int best = -1;
            float bestD = 1e30f;
            for (int ring = 1; ring <= reach; ++ring)
            {
                for (int dy = -ring; dy <= ring; ++dy)
                    for (int dx = -ring; dx <= ring; ++dx)
                    {
                        if (std::abs(dx) != ring && std::abs(dy) != ring)
                            continue;
                        const int x = c.x + dx, y = c.y + dy;
                        if (!g.OpenXY(x, y))
                            continue;
                        const float d = glm::length(g.Center(x, y) - p);
                        if (d < bestD) { bestD = d; best = g.Index(x, y); }
                    }
                // Bulunduktan sonra bir halka daha (daha yakın çapraz hücre olabilir)
                if (best >= 0 && ring * g.Cell > bestD + g.Cell)
                    break;
            }
            return bestD <= maxDist + g.Cell ? best : -1;
        }

        // İki hücre merkezi arasındaki doğru açık hücrelerden mi geçiyor (süper örtü, köşe kesmeden)
        bool LineOfSight(const Grid& g, int from, int to)
        {
            int x0 = from % g.W, y0 = from / g.W, x1 = to % g.W, y1 = to / g.W;
            const int dx = std::abs(x1 - x0), dy = std::abs(y1 - y0);
            const int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
            int err = dx - dy;
            int x = x0, y = y0;
            for (int guard = 0; guard < 2 * (dx + dy) + 2; ++guard)
            {
                if (!g.OpenXY(x, y))
                    return false;
                if (x == x1 && y == y1)
                    return true;
                const int e2 = 2 * err;
                if (e2 > -dy && e2 < dx)
                {
                    // Çapraz adım: iki yan hücre de açık olmalı (duvar köşesinden geçmesin)
                    if (!g.OpenXY(x + sx, y) || !g.OpenXY(x, y + sy))
                        return false;
                    err -= dy;
                    x += sx;
                    err += dx;
                    y += sy;
                }
                else if (e2 > -dy)
                {
                    err -= dy;
                    x += sx;
                }
                else
                {
                    err += dx;
                    y += sy;
                }
            }
            return false;
        }

        bool FindPathGrid(const Grid& g, const glm::vec2& start, const glm::vec2& end, std::vector<glm::vec2>& out, bool& complete)
        {
            out.clear();
            complete = false;
            const int s = NearestOpen(g, start, 3.0f);
            if (s < 0)
                return false;
            int goal = NearestOpen(g, end, 3.0f);
            const size_t n = g.Walk.size();
            std::vector<float> gScore(n, std::numeric_limits<float>::infinity());
            std::vector<int> parent(n, -1);
            std::vector<uint8_t> closed(n, 0), viaLink(n, 0);
            const glm::ivec2 goalCell = goal >= 0 ? glm::ivec2(goal % g.W, goal / g.W) : g.CellOf(end);
            auto heuristic = [&](int i) {
                const int dx = std::abs(i % g.W - goalCell.x), dy = std::abs(i / g.W - goalCell.y);
                return (float)(std::max(dx, dy) - std::min(dx, dy)) + 1.41421356f * (float)std::min(dx, dy);
            };
            using Item = std::pair<float, int>;
            std::priority_queue<Item, std::vector<Item>, std::greater<Item>> open;
            gScore[(size_t)s] = 0.0f;
            open.push({ heuristic(s), s });
            int best = s;
            float bestH = heuristic(s);
            static const int kDX[8] = { 1, -1, 0, 0, 1, 1, -1, -1 };
            static const int kDY[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };
            int found = -1;
            while (!open.empty())
            {
                const int cur = open.top().second;
                open.pop();
                if (closed[(size_t)cur])
                    continue;
                closed[(size_t)cur] = 1;
                const float h = heuristic(cur);
                if (h < bestH) { bestH = h; best = cur; }
                if (cur == goal) { found = cur; break; }
                const int cx = cur % g.W, cy = cur / g.W;
                for (int k = 0; k < 8; ++k)
                {
                    const int nx = cx + kDX[k], ny = cy + kDY[k];
                    if (!g.OpenXY(nx, ny))
                        continue;
                    if (k >= 4 && (!g.OpenXY(cx + kDX[k], cy) || !g.OpenXY(cx, cy + kDY[k])))
                        continue;   // köşe kesme yok
                    const int ni = g.Index(nx, ny);
                    if (closed[(size_t)ni])
                        continue;
                    const float step = (k >= 4 ? 1.41421356f : 1.0f) * 0.5f * (g.Cost[(size_t)cur] + g.Cost[(size_t)ni]);
                    const float tentative = gScore[(size_t)cur] + step;
                    if (tentative < gScore[(size_t)ni])
                    {
                        gScore[(size_t)ni] = tentative;
                        parent[(size_t)ni] = cur;
                        viaLink[(size_t)ni] = 0;
                        open.push({ tentative + heuristic(ni), ni });
                    }
                }
                if (auto it = g.LinkFrom.find(cur); it != g.LinkFrom.end())
                    for (int li : it->second)
                    {
                        const LinkEdge& l = g.Links[(size_t)li];
                        const int other = l.A == cur ? l.B : l.A;
                        if (other < 0 || !g.Open(other) || closed[(size_t)other])
                            continue;
                        const float len = glm::length(g.Center(l.A) - g.Center(l.B)) / g.Cell;
                        const float tentative = gScore[(size_t)cur] + len * l.Cost;
                        if (tentative < gScore[(size_t)other])
                        {
                            gScore[(size_t)other] = tentative;
                            parent[(size_t)other] = cur;
                            viaLink[(size_t)other] = 1;
                            open.push({ tentative + heuristic(other), other });
                        }
                    }
            }
            complete = found >= 0;
            const int last = complete ? found : best;
            std::vector<int> cells;
            for (int c = last; c >= 0; c = parent[(size_t)c])
                cells.push_back(c);
            std::reverse(cells.begin(), cells.end());
            // Köşelere indirgeme: görüş hattı; bağlantı adımları korunur
            std::vector<int> corners;
            corners.push_back(cells.front());
            size_t i = 0;
            while (i + 1 < cells.size())
            {
                size_t j = i + 1;
                if (!viaLink[(size_t)cells[j]])
                    while (j + 1 < cells.size() && !viaLink[(size_t)cells[j + 1]] && LineOfSight(g, cells[i], cells[j + 1]))
                        ++j;
                corners.push_back(cells[j]);
                i = j;
            }
            out.push_back(start);
            for (size_t k = 1; k < corners.size(); ++k)
                out.push_back(g.Center(corners[k]));
            if (complete && !out.empty())
            {
                // Son nokta: hedef açık hücredeyse kendisi
                const glm::ivec2 ec = g.CellOf(end);
                if (g.OpenXY(ec.x, ec.y))
                    out.back() = end;
            }
            if (out.size() == 1)
                out.push_back(complete ? end : out.front());
            return true;
        }

        float PathLength(const std::vector<glm::vec2>& path)
        {
            float len = 0.0f;
            for (size_t i = 1; i < path.size(); ++i)
                len += glm::length(path[i] - path[i - 1]);
            return len;
        }

        glm::vec2 WorldPos2D(Scene& scene, entt::entity e)
        {
            const glm::mat4 m = scene.ComputeWorldMatrix(e);
            return { m[3].x, m[3].y };
        }

        void MoveEntity(Scene& scene, entt::entity e, const glm::vec2& velocity, float dt)
        {
            Entity ent{ e, &scene };
            if (auto* cb = scene.m_Registry.try_get<CharacterBody2DComponent>(e))
            {
                cb->Velocity = velocity;
                GodotNodes::MoveAndSlide2D(scene, (uint32_t)e, dt);
                return;
            }
            if (auto* rb = scene.m_Registry.try_get<RigidBody2DComponent>(e); rb && rb->Type != RigidBody2DComponent::BodyType::Static && scene.GetPhysicsWorld2D())
            {
                scene.GetPhysicsWorld2D()->SetLinearVelocity(ent, velocity);
                return;
            }
            auto& tc = ent.GetComponent<TransformComponent>();
            glm::vec3 delta(velocity * dt, 0.0f);
            if (Entity parent = ent.GetParent())
            {
                const glm::mat4 inv = glm::inverse(scene.ComputeWorldMatrix(parent));
                delta = glm::vec3(inv * glm::vec4(delta, 0.0f));
            }
            tc.Position += delta;
        }
    }

    namespace Navigation2D
    {
        void Bake(Scene& scene)
        {
            Grid& g = Grids()[&scene];
            BakeGrid(scene, g);
            g.Signature = Signature(scene);
        }

        bool IsBaked(Scene& scene) { return GetGrid(scene, false) != nullptr; }

        bool FindPath(Scene& scene, const glm::vec2& start, const glm::vec2& end, std::vector<glm::vec2>& outPath, bool* complete)
        {
            Grid* g = GetGrid(scene, !scene.IsRuntimeScene());
            outPath.clear();
            bool full = false;
            const bool ok = g && FindPathGrid(*g, start, end, outPath, full);
            if (complete)
                *complete = full;
            return ok;
        }

        bool IsWalkable(Scene& scene, const glm::vec2& point)
        {
            Grid* g = GetGrid(scene, !scene.IsRuntimeScene());
            if (!g)
                return false;
            const glm::ivec2 c = g->CellOf(point);
            return g->OpenXY(c.x, c.y);
        }

        bool ClosestPoint(Scene& scene, const glm::vec2& point, glm::vec2& out, float maxDistance)
        {
            Grid* g = GetGrid(scene, !scene.IsRuntimeScene());
            if (!g)
                return false;
            const int i = NearestOpen(*g, point, maxDistance);
            if (i < 0)
                return false;
            const glm::ivec2 c = g->CellOf(point);
            out = g->OpenXY(c.x, c.y) ? point : g->Center(i);
            return true;
        }

        void OnRuntimeStart(Scene& scene)
        {
            if (scene.m_Registry.view<NavigationRegion2DComponent>().empty())
                return;
            Bake(scene);
            for (auto [e, a] : scene.m_Registry.view<NavigationAgent2DComponent>().each())
            {
                a.Path.clear();
                a.PathIndex = 0;
                a.PathTarget = glm::vec2(1e30f);
                a.Finished = !a.HasTarget;
            }
        }

        void ForgetScene(Scene* scene) { Grids().erase(scene); }

        bool SetTarget(Scene& scene, uint32_t entity, const glm::vec2& target)
        {
            auto* a = scene.m_Registry.try_get<NavigationAgent2DComponent>((entt::entity)entity);
            if (!a)
                return false;
            a->TargetPosition = target;
            a->HasTarget = true;
            a->Finished = false;
            a->RepathTimer = 0.0f;
            a->PathTarget = glm::vec2(1e30f);
            return true;
        }

        void Update(Scene& scene, float dt)
        {
            auto view = scene.m_Registry.view<NavigationAgent2DComponent>();
            if (view.empty() || dt <= 0.0f)
                return;
            Grid* g = GetGrid(scene, false);
            if (!g)
                return;
            UpdateDynamic(scene, *g);
            // Kaçınma için konumlar
            struct Body { entt::entity E; glm::vec2 P; float R; };
            std::vector<Body> bodies;
            for (auto [e, a] : view.each())
                if (scene.IsEntityActive(e) && a.AvoidanceEnabled)
                    bodies.push_back({ e, WorldPos2D(scene, e), a.Radius });
            for (auto [e, o] : scene.m_Registry.view<NavigationObstacle2DComponent>().each())
                if (scene.IsEntityActive(e) && o.AvoidanceEnabled)
                {
                    const float r = o.Size.x > 0.0f && o.Size.y > 0.0f ? 0.5f * glm::length(o.Size) : o.Radius;
                    bodies.push_back({ e, WorldPos2D(scene, e), r });
                }

            for (auto [e, a] : view.each())
            {
                if (!scene.IsEntityActive(e) || scene.IsProcessDisabled(e))
                    continue;
                const glm::vec2 pos = WorldPos2D(scene, e);
                a.Velocity = glm::vec2(0.0f);
                if (!a.HasTarget)
                {
                    a.Finished = true;
                    a.NextPathPosition = pos;
                    continue;
                }
                // Yol: hedef değişince ya da aralıkla (Carve engeller hareket ediyor olabilir)
                a.RepathTimer -= dt;
                const bool targetMoved = glm::length(a.TargetPosition - a.PathTarget) > g->Cell;
                if (a.Path.empty() || targetMoved || (a.RepathTimer <= 0.0f && !a.Finished))
                {
                    bool complete = false;
                    std::vector<glm::vec2> path;
                    if (FindPathGrid(*g, pos, a.TargetPosition, path, complete))
                    {
                        a.Path = path;
                        a.PathIndex = path.size() > 1 ? 1 : 0;
                        a.Reachable = complete;
                        a.PathLength = PathLength(path);
                        if (targetMoved)
                            a.Finished = false;
                    }
                    else
                    {
                        a.Path.clear();
                        a.Reachable = false;
                    }
                    a.PathTarget = a.TargetPosition;
                    a.RepathTimer = std::max(a.RepathInterval, 0.02f);
                }
                if (a.Path.empty() || a.Finished)
                {
                    a.NextPathPosition = pos;
                    continue;
                }
                // Yol noktalarını geç
                while (a.PathIndex < (int)a.Path.size() - 1 && glm::length(a.Path[(size_t)a.PathIndex] - pos) <= a.PathDesiredDistance)
                    ++a.PathIndex;
                const glm::vec2 next = a.Path[(size_t)std::min(a.PathIndex, (int)a.Path.size() - 1)];
                a.NextPathPosition = next;
                const bool last = a.PathIndex >= (int)a.Path.size() - 1;
                const float toNext = glm::length(next - pos);
                if (last && toNext <= a.TargetDesiredDistance)
                {
                    a.Finished = true;
                    a.Velocity = glm::vec2(0.0f);
                    if (a.AutoMove)
                        MoveEntity(scene, e, glm::vec2(0.0f), dt);
                    continue;
                }
                glm::vec2 desired = toNext > 1e-5f ? (next - pos) / toNext * std::min(a.MaxSpeed, toNext / dt) : glm::vec2(0.0f);
                // Kaçınma: yakın ajan / engelden uzaklaşan bileşen (hedefe giden hız korunur, yön hafif sapar)
                if (a.AvoidanceEnabled)
                {
                    glm::vec2 push(0.0f);
                    for (const Body& b : bodies)
                    {
                        if (b.E == e)
                            continue;
                        const glm::vec2 d = pos - b.P;
                        const float dist = glm::length(d);
                        const float reach = a.Radius + b.R + a.MaxSpeed * 0.25f;
                        if (dist < 1e-4f || dist > reach)
                            continue;
                        push += d / dist * (1.0f - dist / reach);
                    }
                    if (glm::dot(push, push) > 0.0f)
                    {
                        glm::vec2 steered = desired + push * a.MaxSpeed;
                        const float len = glm::length(steered);
                        if (len > a.MaxSpeed)
                            steered *= a.MaxSpeed / len;
                        const glm::ivec2 c = g->CellOf(pos + steered * dt);
                        if (g->OpenXY(c.x, c.y))
                            desired = steered;
                    }
                }
                a.Velocity = desired;
                if (a.AutoMove)
                    MoveEntity(scene, e, desired, dt);
            }
        }

        void DrawEditorGizmos(Scene& scene, const std::vector<uint32_t>& selection)
        {
            auto selected = [&](entt::entity e) {
                return std::find(selection.begin(), selection.end(), (uint32_t)e) != selection.end();
            };
            const glm::vec4 kOutline(0.25f, 0.65f, 1.0f, 1.0f);
            const glm::vec4 kArea(0.2f, 0.55f, 1.0f, 0.28f);
            const glm::vec4 kLink(1.0f, 0.75f, 0.2f, 1.0f);
            bool anyRegionSelected = false;
            for (auto [e, r] : scene.m_Registry.view<NavigationRegion2DComponent>().each())
            {
                if (r.Outline.size() < 2 || !scene.IsEntityActive(e))
                    continue;
                const glm::mat4 m = scene.ComputeWorldMatrix(e);
                for (size_t i = 0; i < r.Outline.size(); ++i)
                {
                    const glm::vec2 a = ToWorld(m, r.Outline[i]), b = ToWorld(m, r.Outline[(i + 1) % r.Outline.size()]);
                    DebugDraw::Line({ a, 0.0f }, { b, 0.0f }, kOutline, 0.0f, false);
                }
                anyRegionSelected = anyRegionSelected || selected(e);
            }
            for (auto [e, l] : scene.m_Registry.view<NavigationLink2DComponent>().each())
            {
                const glm::mat4 m = scene.ComputeWorldMatrix(e);
                const glm::vec2 a = ToWorld(m, l.StartPosition), b = ToWorld(m, l.EndPosition);
                DebugDraw::Line({ a, 0.0f }, { b, 0.0f }, kLink, 0.0f, false);
                DebugDraw::Circle({ a, 0.0f }, { 0, 0, 1 }, 0.12f, kLink, 0.0f, false);
                DebugDraw::Circle({ b, 0.0f }, { 0, 0, 1 }, 0.12f, kLink, 0.0f, false);
            }
            // Seçili bölge: yürünebilir alan (satır satır birleşik kutular)
            if (anyRegionSelected)
                if (Grid* g = GetGrid(scene, true))
                {
                    for (int y = 0; y < g->H; ++y)
                    {
                        int x = 0;
                        while (x < g->W)
                        {
                            if (!g->Walk[(size_t)g->Index(x, y)]) { ++x; continue; }
                            int x1 = x;
                            while (x1 + 1 < g->W && g->Walk[(size_t)g->Index(x1 + 1, y)])
                                ++x1;
                            const glm::vec2 a = g->Origin + glm::vec2((float)x, (float)y) * g->Cell;
                            const glm::vec2 b = g->Origin + glm::vec2((float)(x1 + 1), (float)(y + 1)) * g->Cell;
                            glm::mat4 box(1.0f);
                            box[0][0] = b.x - a.x;
                            box[1][1] = b.y - a.y;
                            box[2][2] = 0.001f;
                            box[3] = glm::vec4((a + b) * 0.5f, 0.0f, 1.0f);
                            DebugDraw::SolidBox(box, kArea, 0.0f, false);
                            x = x1 + 1;
                        }
                    }
                }
            // Seçili ajan: yolu
            for (auto [e, a] : scene.m_Registry.view<NavigationAgent2DComponent>().each())
            {
                if (!selected(e) || a.Path.size() < 2)
                    continue;
                for (size_t i = 1; i < a.Path.size(); ++i)
                    DebugDraw::Line({ a.Path[i - 1], 0.0f }, { a.Path[i], 0.0f }, glm::vec4(1.0f, 0.9f, 0.3f, 1.0f), 0.0f, false);
            }
        }
    }

    void RegisterNavigation2DComponents()
    {
        {
            using C = NavigationRegion2DComponent;
            ComponentDef<C>("Navigation Region 2D", "NavigationRegion2DComponent", "Navigation", ICON_FA_DRAW_POLYGON)
                .Describe("Godot NavigationRegion2D: the outline polygon is the walkable area on the XY plane. Static 2D colliders and "
                          "solid / not walkable tiles inside it are cut out, everything stays Agent Radius away from walls. Overlapping "
                          "or touching regions join into one area. Select it to see the walkable area.")
                .Field("Outline", "Outline", &C::Outline).Speed(0.05f).Tooltip("Local corners of the walkable area.")
                .Field("CellSize", "Cell Size", &C::CellSize).Speed(0.005f).Tooltip("Smaller = more exact paths near walls, slower to bake.")
                .Field("AgentRadius", "Agent Radius", &C::AgentRadius).Speed(0.01f)
                .Field("ParseColliders", "Parse Colliders", &C::ParseColliders).Tooltip("Cut out static 2D colliders and blocking tiles.")
                .Field("TravelCost", "Travel Cost", &C::TravelCost).Speed(0.05f).Tooltip("Paths avoid regions with a higher cost.")
                .Field("Enabled", "Enabled", &C::Enabled)
                .Field("WalkableCells", "Walkable Cells", &C::WalkableCells).ReadOnly()
                .Field("Baked", "Baked", &C::Baked).ReadOnly();
        }
        {
            using C = NavigationAgent2DComponent;
            ComponentDef<C>("Navigation Agent 2D", "NavigationAgent2DComponent", "Navigation", ICON_FA_PERSON_WALKING)
                .Describe("Godot NavigationAgent2D: finds a path to Target Position over the Navigation Region 2D areas and follows it "
                          "(Auto Move; with a Character Body 2D or Rigidbody 2D it moves them). Agents keep away from each other and from "
                          "avoidance obstacles; an unreachable target leads to the closest point.")
                .Field("TargetPosition", "Target Position", &C::TargetPosition).Speed(0.05f)
                .Field("HasTarget", "Has Target", &C::HasTarget).Tooltip("Off: the agent stands still.")
                .Field("MaxSpeed", "Max Speed", &C::MaxSpeed).Speed(0.05f)
                .Field("PathDesiredDistance", "Path Desired Distance", &C::PathDesiredDistance).Speed(0.01f)
                .Field("TargetDesiredDistance", "Target Desired Distance", &C::TargetDesiredDistance).Speed(0.01f)
                .Field("Radius", "Radius", &C::Radius).Speed(0.01f)
                .Field("AvoidanceEnabled", "Avoidance", &C::AvoidanceEnabled)
                .Field("AutoMove", "Auto Move", &C::AutoMove).Tooltip("Off: only computes the path; scripts read NextPathPosition and move it.")
                .Field("RepathInterval", "Repath Interval", &C::RepathInterval).Speed(0.01f)
                .Field("Finished", "Finished", &C::Finished).ReadOnly()
                .Field("Reachable", "Reachable", &C::Reachable).ReadOnly()
                .Field("NextPathPosition", "Next Path Position", &C::NextPathPosition).ReadOnly()
                .Field("Velocity", "Velocity", &C::Velocity).ReadOnly()
                .Field("PathLength", "Path Length", &C::PathLength).ReadOnly();
        }
        {
            using C = NavigationObstacle2DComponent;
            ComponentDef<C>("Navigation Obstacle 2D", "NavigationObstacle2DComponent", "Navigation", ICON_FA_BAN)
                .Describe("Godot NavigationObstacle2D: a moving circle (or box when Size is set) that cuts the walkable area (Carve) "
                          "and that agents steer around (Avoidance).")
                .Field("Radius", "Radius", &C::Radius).Speed(0.01f)
                .Field("Size", "Size", &C::Size).Speed(0.05f).Tooltip("Box size; 0 = circle with Radius.")
                .Field("CarveNavigation", "Carve Navigation", &C::CarveNavigation)
                .Field("AvoidanceEnabled", "Avoidance", &C::AvoidanceEnabled);
        }
        {
            using C = NavigationLink2DComponent;
            ComponentDef<C>("Navigation Link 2D", "NavigationLink2DComponent", "Navigation", ICON_FA_LINK)
                .Describe("Godot NavigationLink2D: joins two points of the walkable area (a gap to jump, a ladder, a teleporter) so "
                          "paths may use it.")
                .Field("StartPosition", "Start Position", &C::StartPosition).Speed(0.05f)
                .Field("EndPosition", "End Position", &C::EndPosition).Speed(0.05f)
                .Field("Bidirectional", "Bidirectional", &C::Bidirectional)
                .Field("TravelCost", "Travel Cost", &C::TravelCost).Speed(0.05f)
                .Field("Enabled", "Enabled", &C::Enabled);
        }
    }
}
