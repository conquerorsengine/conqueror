#include "SpriteShape.h"
#include "Scene/Scene.h"
#include "Scene/Entity.h"
#include "Scene/Components.h"
#include "Scene/PhysicsComponents.h"
#include "Scene/ComponentRegistry.h"
#include "Core/Assets/SimpleAsset.h"
#include "Core/Debug/DebugDraw.h"
#include "Core/Math/Triangulate2D.h"
#include "Core/Serialization/SceneSerializer.h"
#include "Renderer/Renderer2D.h"
#include "Renderer/RHI/Texture.h"

#include <IconsFontAwesome6.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <unordered_map>

namespace Conqueror
{
    namespace
    {
        std::shared_ptr<Texture2D> TextureOf(const std::string& path)
        {
            static auto& cache = *new std::unordered_map<std::string, std::shared_ptr<Texture2D>>();
            if (path.empty())
                return nullptr;
            auto& t = cache[path];
            if (!t)
                t = Texture2D::Create(SceneSerializer::ResolveAssetPath(path));
            return t;
        }

        void HashF(size_t& h, float v) { h ^= std::hash<float>()(v) + 0x9e3779b9 + (h << 6) + (h >> 2); }

        glm::vec2 CatmullRom(const glm::vec2& p0, const glm::vec2& p1, const glm::vec2& p2, const glm::vec2& p3, float t)
        {
            const float t2 = t * t, t3 = t2 * t;
            return 0.5f * ((2.0f * p1) + (-p0 + p2) * t + (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 + (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3);
        }

        // Kenarın dış yüzünün açısı: 0 yukarı, 90 sağ, -90 sol, ±180 aşağı
        float EdgeAngle(const glm::vec2& outward)
        {
            return glm::degrees(std::atan2(outward.x, outward.y));
        }

        bool InRange(float a, float start, float end)
        {
            if (start <= end)
                return a >= start && a <= end;
            return a >= start || a <= end;   // ±180'i aşan aralık (ör. 135 .. -135)
        }
    }

    namespace SpriteShapes
    {
        const std::vector<glm::vec2>& Outline(SpriteShapeControllerComponent& c)
        {
            size_t h = 1469598103934665603ull;
            for (const auto& p : c.Points) { HashF(h, p.x); HashF(h, p.y); }
            HashF(h, c.Closed ? 1.0f : 0.0f);
            HashF(h, c.Smooth ? 1.0f : 0.0f);
            HashF(h, (float)c.Detail);
            if (h == c.CacheHash && (!c.Outline.empty() || c.Points.size() < 2))
                return c.Outline;
            c.CacheHash = h;
            c.Outline.clear();
            c.FillTriangles.clear();
            c.Area = 0.0f;
            const size_t n = c.Points.size();
            if (n < 2)
                return c.Outline;
            if (!c.Smooth || n < 3)
                c.Outline = c.Points;
            else
            {
                const int detail = std::clamp(c.Detail, 1, 64);
                const size_t segs = c.Closed ? n : n - 1;
                auto at = [&](long i) -> glm::vec2 {
                    if (c.Closed)
                        return c.Points[(size_t)((i % (long)n + (long)n) % (long)n)];
                    return c.Points[(size_t)std::clamp(i, 0l, (long)n - 1)];
                };
                for (size_t s = 0; s < segs; ++s)
                    for (int k = 0; k < detail; ++k)
                        c.Outline.push_back(CatmullRom(at((long)s - 1), at((long)s), at((long)s + 1), at((long)s + 2), (float)k / (float)detail));
                if (!c.Closed)
                    c.Outline.push_back(c.Points.back());
            }
            if (c.Closed && c.Outline.size() >= 3)
            {
                c.Area = Triangulate2D::SignedArea(c.Outline);
                Triangulate2D::Triangulate(c.Outline, c.FillTriangles);
            }
            return c.Outline;
        }

        void Update(Scene& scene)
        {
            auto view = scene.m_Registry.view<SpriteShapeControllerComponent>();
            for (auto e : view)
            {
                auto& c = view.get<SpriteShapeControllerComponent>(e);
                if (!c.UpdateCollider)
                    continue;
                const auto& outline = Outline(c);
                if (c.ColliderHash == c.CacheHash || outline.size() < 2)
                    continue;
                c.ColliderHash = c.CacheHash;
                Entity ent(e, &scene);
                if (ent.HasComponent<EdgeCollider2DComponent>())
                {
                    auto& ec = ent.GetComponent<EdgeCollider2DComponent>();
                    ec.Points = outline;
                    ec.Loop = c.Closed;
                }
                if (c.Closed && outline.size() >= 3 && ent.HasComponent<PolygonCollider2DComponent>())
                    ent.GetComponent<PolygonCollider2DComponent>().Points = outline;
            }
        }

        void Draw(Scene& scene, entt::entity entity)
        {
            auto* c = scene.m_Registry.try_get<SpriteShapeControllerComponent>(entity);
            if (!c)
                return;
            const glm::vec4 color = scene.m_Registry.all_of<SpriteShapeRendererComponent>(entity)
                                        ? scene.m_Registry.get<SpriteShapeRendererComponent>(entity).Color : glm::vec4(1.0f);
            const auto& outline = Outline(*c);
            if (outline.size() < 2)
                return;
            const glm::mat4 world = scene.ComputeWorldMatrix(entity);
            auto W = [&](const glm::vec2& p) { return glm::vec3(world * glm::vec4(p, 0.0f, 1.0f)); };

            SimpleAssets::Data profile;
            const bool hasProfile = !c->Profile.empty() && SimpleAssets::Load(c->Profile, profile);

            // Dolgu: kapalı şeklin üçgenleri, doku yerel konumla döşenir (dokunun Wrap Mode'u Repeat olmalı)
            if (c->Closed && !c->FillTriangles.empty())
            {
                std::shared_ptr<Texture2D> fill = hasProfile ? TextureOf(profile.Str("Fill Texture")) : nullptr;
                const float tiling = std::max(hasProfile ? profile.Float("Fill Tiling", 4.0f) : 4.0f, 0.01f);
                if (fill || !hasProfile)
                    for (size_t t = 0; t + 2 < c->FillTriangles.size(); t += 3)
                    {
                        const glm::vec2& a = outline[c->FillTriangles[t]];
                        const glm::vec2& b = outline[c->FillTriangles[t + 1]];
                        const glm::vec2& d = outline[c->FillTriangles[t + 2]];
                        const glm::vec3 pos[4] = { W(a), W(b), W(d), W(d) };
                        const glm::vec2 uvs[4] = { a / tiling, b / tiling, d / tiling, d / tiling };
                        Renderer2D::DrawQuadCorners(pos, uvs, fill, color);
                    }
            }
            if (!hasProfile)
                return;

            // Kenar: hattın iki yanında Edge Height kalınlığında şerit; kenar sprite'ı boy oranıyla tekrarlanır
            const float height = std::max(profile.Float("Edge Height", 0.5f), 0.0f);
            if (height <= 0.0f)
                return;
            const float offset = std::clamp(profile.Float("Edge Offset", 0.0f), -0.5f, 0.5f);
            const std::string defaultEdge = profile.Str("Edge Sprite");
            const auto& ranges = profile.Table("Angle Ranges");
            const size_t n = outline.size();
            const size_t segs = c->Closed ? n : n - 1;
            if (segs == 0)
                return;
            // Dış yön: kapalı şekilde dolgunun dışı, açık şekilde çizim yönünün solu (soldan sağa çizilen hat için yukarı)
            const bool flip = c->Closed && c->Area > 0.0f;
            std::vector<glm::vec2> segNormal(segs);
            for (size_t s = 0; s < segs; ++s)
            {
                glm::vec2 d = outline[(s + 1) % n] - outline[s];
                const float len = glm::length(d);
                d = len > 1e-6f ? d / len : glm::vec2(1.0f, 0.0f);
                glm::vec2 left(-d.y, d.x);
                segNormal[s] = flip ? -left : left;
            }
            // Köşe normalleri (birleşimde boşluk kalmasın: gönye)
            std::vector<glm::vec2> vNormal(n);
            for (size_t i = 0; i < n; ++i)
            {
                const bool hasPrev = c->Closed || i > 0, hasNext = c->Closed || i < segs;
                const glm::vec2 np = hasPrev ? segNormal[(i + segs - 1) % segs] : segNormal[0];
                const glm::vec2 nn = hasNext ? segNormal[i % segs] : segNormal[segs - 1];
                glm::vec2 m = np + nn;
                const float ml = glm::length(m);
                m = ml > 1e-5f ? m / ml : nn;
                const float cosHalf = std::max(glm::dot(m, nn), 0.35f);
                vNormal[i] = m / cosHalf;
            }
            const float inner = (offset - 0.5f) * height, outer = (offset + 0.5f) * height;
            float travelled = 0.0f;
            for (size_t s = 0; s < segs; ++s)
            {
                const glm::vec2 a = outline[s], b = outline[(s + 1) % n];
                const float len = glm::length(b - a);
                if (len < 1e-6f)
                    continue;
                // Açı aralığı -> sprite
                std::string sprite = defaultEdge;
                const float ang = EdgeAngle(segNormal[s]);
                for (const auto& row : ranges)
                    if (InRange(ang, SimpleAssets::CellFloat(row, 0, -45.0f), SimpleAssets::CellFloat(row, 1, 45.0f)) && !SimpleAssets::Cell(row, 2).empty())
                    {
                        sprite = SimpleAssets::Cell(row, 2);
                        break;
                    }
                auto tex = TextureOf(sprite);
                if (!tex)
                {
                    travelled += len;
                    continue;
                }
                const float tile = height * (tex->GetHeight() > 0 ? (float)tex->GetWidth() / (float)tex->GetHeight() : 1.0f);
                const glm::vec2 na = vNormal[s], nb = vNormal[(s + 1) % n];
                // Parçalar karo sınırlarında bölünür: her parçanın UV'si 0..1 içinde kalır (doku kenetli olabilir)
                float sPos = 0.0f;
                while (sPos < 1.0f - 1e-6f)
                {
                    const float uStart = (travelled + sPos * len) / tile;
                    const float tileEnd = (std::floor(uStart + 1e-5f) + 1.0f) * tile;
                    const float sNext = std::min(1.0f, (tileEnd - travelled) / len);
                    const float u0 = uStart - std::floor(uStart + 1e-5f);
                    const float u1 = u0 + (sNext - sPos) * len / tile;
                    const glm::vec2 p0 = glm::mix(a, b, sPos), p1 = glm::mix(a, b, sNext);
                    const glm::vec2 n0 = glm::mix(na, nb, sPos), n1 = glm::mix(na, nb, sNext);
                    const glm::vec3 pos[4] = { W(p0 + n0 * inner), W(p1 + n1 * inner), W(p1 + n1 * outer), W(p0 + n0 * outer) };
                    const glm::vec2 uvs[4] = { { u0, 0.0f }, { u1, 0.0f }, { u1, 1.0f }, { u0, 1.0f } };
                    Renderer2D::DrawQuadCorners(pos, uvs, tex, color);
                    sPos = sNext;
                }
                travelled += len;
            }
        }

        void DrawGizmo(Scene& scene, uint32_t entity)
        {
            const entt::entity e = (entt::entity)entity;
            auto* c = scene.m_Registry.valid(e) ? scene.m_Registry.try_get<SpriteShapeControllerComponent>(e) : nullptr;
            if (!c)
                return;
            const glm::mat4 world = scene.ComputeWorldMatrix(e);
            auto W = [&](const glm::vec2& p) { return glm::vec3(world * glm::vec4(p, 0.0f, 1.0f)); };
            const auto& outline = Outline(*c);
            const glm::vec4 lineCol(0.4f, 0.9f, 1.0f, 1.0f), pointCol(1.0f, 0.85f, 0.3f, 1.0f);
            for (size_t i = 0; i + 1 < outline.size() + (c->Closed ? 1 : 0) && outline.size() >= 2; ++i)
                DebugDraw::Line(W(outline[i]), W(outline[(i + 1) % outline.size()]), lineCol, 0.0f, false);
            for (const auto& p : c->Points)
                DebugDraw::Cross(W(p), 0.15f, pointCol, 0.0f, false);
        }
    }

    template<> void Scene::OnComponentAdded<SpriteShapeControllerComponent>(Entity, SpriteShapeControllerComponent&) {}
    template<> void Scene::OnComponentAdded<SpriteShapeRendererComponent>(Entity, SpriteShapeRendererComponent&) {}

    void RegisterSpriteShapeComponents()
    {
        const glm::vec4 kColor{ 0.5f, 0.9f, 0.6f, 1.0f };
        {
            using C = SpriteShapeRendererComponent;
            ComponentDef<C>("Sprite Shape Renderer", "SpriteShapeRendererComponent", "2D", ICON_FA_DRAW_POLYGON).Color(kColor)
                .Describe("Draws the shape of the Sprite Shape Controller: fill and edge sprites from its profile. Sorting uses the object's Layer.")
                .Field("Color", "Color", &C::Color, FieldKind::Color4);
        }
        {
            using C = SpriteShapeControllerComponent;
            ComponentDef<C>("Sprite Shape Controller", "SpriteShapeControllerComponent", "2D", ICON_FA_BEZIER_CURVE).Color(kColor)
                .Requires("SpriteShapeRendererComponent")
                .Describe("A 2D shape drawn through points: closed shapes are filled with the profile's fill texture, every edge gets "
                          "the edge sprite picked by its angle (grass on top, rock on the sides). Updates an Edge Collider 2D or "
                          "Polygon Collider 2D on this object to the same outline.")
                .Field("Profile", "Profile", &C::Profile, FieldKind::Asset).Filter(".cqspriteshape")
                .Field("Points", "Points", &C::Points).Speed(0.05f).Tooltip("Local positions of the shape's points.")
                .Field("Closed", "Closed", &C::Closed).Tooltip("Closed shapes are filled; open shapes only get edges.")
                .Field("Smooth", "Smooth", &C::Smooth).Tooltip("Curve through the points instead of straight lines.")
                .Field("Detail", "Detail", &C::Detail).Range(1.0f, 64.0f, 1.0f).VisibleIf([](const C& c) { return c.Smooth; })
                .Field("UpdateCollider", "Update Collider", &C::UpdateCollider)
                    .Tooltip("Keeps an Edge Collider 2D / Polygon Collider 2D on this object on the shape's outline.");
        }
    }
}
