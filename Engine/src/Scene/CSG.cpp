#include "CSG.h"
#include "Scene/Scene.h"
#include "Scene/Entity.h"
#include "Scene/Components.h"
#include "Scene/ComponentRegistry.h"
#include "Core/PhysicsSystem/Physics3D/PhysicsWorld3D.h"
#include "Renderer/Utilities/Renderer3D/Mesh.h"

#include <IconsFontAwesome6.h>
#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <vector>

namespace Conqueror
{
    namespace
    {
        constexpr float kEps = 1e-5f;

        struct CVertex
        {
            glm::vec3 Pos{ 0.0f };
            glm::vec3 Normal{ 0.0f, 1.0f, 0.0f };
            glm::vec2 UV{ 0.0f };
            CVertex Lerp(const CVertex& o, float t) const
            {
                return { glm::mix(Pos, o.Pos, t), glm::normalize(glm::mix(Normal, o.Normal, t) + glm::vec3(1e-9f)), glm::mix(UV, o.UV, t) };
            }
        };

        struct CPlane
        {
            glm::vec3 N{ 0.0f, 1.0f, 0.0f };
            float W = 0.0f;
            static bool FromPoints(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, CPlane& out)
            {
                glm::vec3 n = glm::cross(b - a, c - a);
                const float len = glm::length(n);
                if (len < 1e-12f)
                    return false;
                out.N = n / len;
                out.W = glm::dot(out.N, a);
                return true;
            }
            void Flip() { N = -N; W = -W; }
        };

        struct CPolygon
        {
            std::vector<CVertex> V;
            CPlane Plane;
            bool Smooth = false;
            void Flip()
            {
                std::reverse(V.begin(), V.end());
                for (auto& v : V) v.Normal = -v.Normal;
                Plane.Flip();
            }
        };

        void Split(const CPlane& plane, const CPolygon& poly, std::vector<CPolygon>& coFront, std::vector<CPolygon>& coBack,
                   std::vector<CPolygon>& front, std::vector<CPolygon>& back)
        {
            enum { Coplanar = 0, Front = 1, Back = 2, Spanning = 3 };
            int polyType = 0;
            const size_t n = poly.V.size();
            std::vector<int> types(n);
            for (size_t i = 0; i < n; ++i)
            {
                const float t = glm::dot(plane.N, poly.V[i].Pos) - plane.W;
                types[i] = t < -kEps ? Back : t > kEps ? Front : Coplanar;
                polyType |= types[i];
            }
            switch (polyType)
            {
                case Coplanar: (glm::dot(plane.N, poly.Plane.N) > 0.0f ? coFront : coBack).push_back(poly); break;
                case Front: front.push_back(poly); break;
                case Back: back.push_back(poly); break;
                default:
                {
                    CPolygon f, b;
                    f.Plane = b.Plane = poly.Plane;
                    f.Smooth = b.Smooth = poly.Smooth;
                    for (size_t i = 0; i < n; ++i)
                    {
                        const size_t j = (i + 1) % n;
                        const int ti = types[i], tj = types[j];
                        const CVertex& vi = poly.V[i];
                        const CVertex& vj = poly.V[j];
                        if (ti != Back) f.V.push_back(vi);
                        if (ti != Front) b.V.push_back(vi);
                        if ((ti | tj) == Spanning)
                        {
                            const float t = (plane.W - glm::dot(plane.N, vi.Pos)) / glm::dot(plane.N, vj.Pos - vi.Pos);
                            const CVertex v = vi.Lerp(vj, t);
                            f.V.push_back(v);
                            b.V.push_back(v);
                        }
                    }
                    if (f.V.size() >= 3) front.push_back(std::move(f));
                    if (b.V.size() >= 3) back.push_back(std::move(b));
                }
            }
        }

        struct Node
        {
            bool HasPlane = false;
            CPlane Plane;
            std::unique_ptr<Node> Front, Back;
            std::vector<CPolygon> Polygons;

            void Invert()
            {
                for (auto& p : Polygons) p.Flip();
                Plane.Flip();
                if (Front) Front->Invert();
                if (Back) Back->Invert();
                std::swap(Front, Back);
            }

            std::vector<CPolygon> ClipPolygons(const std::vector<CPolygon>& polys) const
            {
                if (!HasPlane)
                    return polys;
                std::vector<CPolygon> f, b;
                for (const auto& p : polys)
                    Split(Plane, p, f, b, f, b);
                if (Front) f = Front->ClipPolygons(f);
                if (Back) b = Back->ClipPolygons(b);
                else b.clear();
                f.insert(f.end(), std::make_move_iterator(b.begin()), std::make_move_iterator(b.end()));
                return f;
            }

            void ClipTo(const Node& bsp)
            {
                Polygons = bsp.ClipPolygons(Polygons);
                if (Front) Front->ClipTo(bsp);
                if (Back) Back->ClipTo(bsp);
            }

            void All(std::vector<CPolygon>& out) const
            {
                out.insert(out.end(), Polygons.begin(), Polygons.end());
                if (Front) Front->All(out);
                if (Back) Back->All(out);
            }

            void Build(const std::vector<CPolygon>& polys, int depth = 0)
            {
                if (polys.empty())
                    return;
                if (!HasPlane)
                {
                    Plane = polys[0].Plane;
                    HasPlane = true;
                }
                std::vector<CPolygon> f, b;
                for (const auto& p : polys)
                    Split(Plane, p, Polygons, Polygons, f, b);
                if (depth > 4000)
                {
                    // Aşırı derinlik: kalanlar bu düğümde kalır (yığın taşmasın)
                    Polygons.insert(Polygons.end(), f.begin(), f.end());
                    Polygons.insert(Polygons.end(), b.begin(), b.end());
                    return;
                }
                if (!f.empty()) { if (!Front) Front = std::make_unique<Node>(); Front->Build(f, depth + 1); }
                if (!b.empty()) { if (!Back) Back = std::make_unique<Node>(); Back->Build(b, depth + 1); }
            }
        };

        std::vector<CPolygon> Union(const std::vector<CPolygon>& pa, const std::vector<CPolygon>& pb)
        {
            Node a, b;
            a.Build(pa);
            b.Build(pb);
            a.ClipTo(b);
            b.ClipTo(a);
            b.Invert();
            b.ClipTo(a);
            b.Invert();
            std::vector<CPolygon> bp;
            b.All(bp);
            a.Build(bp);
            std::vector<CPolygon> out;
            a.All(out);
            return out;
        }

        std::vector<CPolygon> Subtract(const std::vector<CPolygon>& pa, const std::vector<CPolygon>& pb)
        {
            Node a, b;
            a.Build(pa);
            b.Build(pb);
            a.Invert();
            a.ClipTo(b);
            b.ClipTo(a);
            b.Invert();
            b.ClipTo(a);
            b.Invert();
            std::vector<CPolygon> bp;
            b.All(bp);
            a.Build(bp);
            a.Invert();
            std::vector<CPolygon> out;
            a.All(out);
            return out;
        }

        std::vector<CPolygon> Intersect(const std::vector<CPolygon>& pa, const std::vector<CPolygon>& pb)
        {
            Node a, b;
            a.Build(pa);
            b.Build(pb);
            a.Invert();
            b.ClipTo(a);
            b.Invert();
            a.ClipTo(b);
            b.ClipTo(a);
            std::vector<CPolygon> bp;
            b.All(bp);
            a.Build(bp);
            a.Invert();
            std::vector<CPolygon> out;
            a.All(out);
            return out;
        }

        void AddPolygon(std::vector<CPolygon>& out, std::vector<CVertex> verts, bool smooth)
        {
            CPolygon p;
            if (verts.size() < 3 || !CPlane::FromPoints(verts[0].Pos, verts[1].Pos, verts[2].Pos, p.Plane))
                return;
            if (!smooth)
                for (auto& v : verts) v.Normal = p.Plane.N;
            p.V = std::move(verts);
            p.Smooth = smooth;
            out.push_back(std::move(p));
        }

        // Şekil poligonları (yerel uzay, dışa bakan yüzler saat yönü tersine)
        std::vector<CPolygon> ShapePolygons(const CSGShapeComponent& c)
        {
            std::vector<CPolygon> out;
            const float pi = glm::pi<float>();
            const int sides = std::clamp(c.Sides, 3, 128);
            switch (c.Shape)
            {
                case CSGShapeType::Box:
                {
                    const glm::vec3 h = glm::abs(c.Size) * 0.5f;
                    const glm::vec3 p[8] = { { -h.x, -h.y, -h.z }, { h.x, -h.y, -h.z }, { h.x, h.y, -h.z }, { -h.x, h.y, -h.z },
                                             { -h.x, -h.y, h.z }, { h.x, -h.y, h.z }, { h.x, h.y, h.z }, { -h.x, h.y, h.z } };
                    const int faces[6][4] = { { 0, 3, 2, 1 }, { 4, 5, 6, 7 }, { 0, 1, 5, 4 }, { 3, 7, 6, 2 }, { 0, 4, 7, 3 }, { 1, 2, 6, 5 } };
                    const glm::vec2 uv[4] = { { 0, 0 }, { 0, 1 }, { 1, 1 }, { 1, 0 } };
                    for (auto& f : faces)
                    {
                        std::vector<CVertex> v;
                        for (int k = 0; k < 4; ++k)
                            v.push_back({ p[f[k]], glm::vec3(0.0f), uv[k] });
                        AddPolygon(out, v, false);
                    }
                    break;
                }
                case CSGShapeType::Sphere:
                {
                    const int stacks = std::max(sides / 2, 2);
                    auto vertex = [&](int i, int j) {
                        const float theta = (float)i / (float)sides * 2.0f * pi;
                        const float phi = (float)j / (float)stacks * pi;
                        const glm::vec3 dir(std::cos(theta) * std::sin(phi), std::cos(phi), std::sin(theta) * std::sin(phi));
                        return CVertex{ dir * c.Radius, dir, glm::vec2((float)i / (float)sides, (float)j / (float)stacks) };
                    };
                    for (int i = 0; i < sides; ++i)
                        for (int j = 0; j < stacks; ++j)
                        {
                            std::vector<CVertex> v;
                            v.push_back(vertex(i, j));
                            if (j > 0) v.push_back(vertex(i + 1, j));
                            if (j < stacks - 1) v.push_back(vertex(i + 1, j + 1));
                            v.push_back(vertex(i, j + 1));
                            AddPolygon(out, v, c.SmoothFaces);
                        }
                    break;
                }
                case CSGShapeType::Cylinder:
                case CSGShapeType::Cone:
                {
                    const bool cone = c.Shape == CSGShapeType::Cone;
                    const float hh = c.Height * 0.5f;
                    std::vector<CVertex> top, bottom;
                    for (int i = 0; i < sides; ++i)
                    {
                        const float a0 = (float)i / (float)sides * 2.0f * pi, a1 = (float)(i + 1) / (float)sides * 2.0f * pi;
                        const glm::vec3 d0(std::cos(a0), 0.0f, std::sin(a0)), d1(std::cos(a1), 0.0f, std::sin(a1));
                        const glm::vec2 u0((float)i / (float)sides, 0.0f), u1((float)(i + 1) / (float)sides, 0.0f);
                        if (cone)
                        {
                            std::vector<CVertex> v = { { d0 * c.Radius - glm::vec3(0, hh, 0), d0, u0 }, { glm::vec3(0, hh, 0), glm::vec3(0, 1, 0), glm::vec2(u0.x, 1.0f) },
                                                       { d1 * c.Radius - glm::vec3(0, hh, 0), d1, u1 } };
                            AddPolygon(out, v, c.SmoothFaces);
                        }
                        else
                        {
                            std::vector<CVertex> v = { { d0 * c.Radius - glm::vec3(0, hh, 0), d0, u0 }, { d0 * c.Radius + glm::vec3(0, hh, 0), d0, glm::vec2(u0.x, 1.0f) },
                                                       { d1 * c.Radius + glm::vec3(0, hh, 0), d1, glm::vec2(u1.x, 1.0f) }, { d1 * c.Radius - glm::vec3(0, hh, 0), d1, u1 } };
                            AddPolygon(out, v, c.SmoothFaces);
                            top.push_back({ d0 * c.Radius + glm::vec3(0, hh, 0), glm::vec3(0, 1, 0), glm::vec2(d0.x, d0.z) * 0.5f + 0.5f });
                        }
                        bottom.push_back({ d0 * c.Radius - glm::vec3(0, hh, 0), glm::vec3(0, -1, 0), glm::vec2(d0.x, d0.z) * 0.5f + 0.5f });
                    }
                    if (!cone)
                    {
                        std::reverse(top.begin(), top.end());
                        AddPolygon(out, top, false);
                    }
                    AddPolygon(out, bottom, false);
                    break;
                }
            }
            return out;
        }

        void Transform(std::vector<CPolygon>& polys, const glm::mat4& m)
        {
            const glm::mat3 nm = glm::transpose(glm::inverse(glm::mat3(m)));
            const bool mirrored = glm::determinant(glm::mat3(m)) < 0.0f;
            for (auto& p : polys)
            {
                for (auto& v : p.V)
                {
                    v.Pos = glm::vec3(m * glm::vec4(v.Pos, 1.0f));
                    v.Normal = glm::normalize(nm * v.Normal);
                }
                if (mirrored)
                    std::reverse(p.V.begin(), p.V.end());
                CPlane::FromPoints(p.V[0].Pos, p.V[1].Pos, p.V[2].Pos, p.Plane);
            }
        }

        bool IsCSG(Entity e) { return e && e.HasComponent<CSGShapeComponent>(); }

        // Düğüm ve CSG çocukları (hiyerarşi sırasıyla) kökün yerel uzayında
        std::vector<CPolygon> Evaluate(Entity e, const glm::mat4& rootInv)
        {
            const auto& c = e.GetComponent<CSGShapeComponent>();
            std::vector<CPolygon> result = ShapePolygons(c);
            Transform(result, rootInv * e.GetComponent<TransformComponent>().WorldTransform);
            for (Entity child : e.GetChildren())
            {
                if (!IsCSG(child) || !child.GetScene()->IsEntityActive((entt::entity)child))
                    continue;
                const std::vector<CPolygon> other = Evaluate(child, rootInv);
                switch (child.GetComponent<CSGShapeComponent>().Operation)
                {
                    case CSGOperation::Union: result = Union(result, other); break;
                    case CSGOperation::Subtraction: result = Subtract(result, other); break;
                    case CSGOperation::Intersection: result = Intersect(result, other); break;
                }
            }
            return result;
        }

        void HashBytes(uint64_t& h, const void* p, size_t n)
        {
            const auto* b = (const unsigned char*)p;
            for (size_t i = 0; i < n; ++i) { h ^= b[i]; h *= 1099511628211ull; }
        }

        // rel: köke göre dönüşüm, yerel dönüşümlerin çarpımı (dünya matrislerinden değil: kök döndükçe / taşındıkça
        // rootInv * world kayan nokta gürültüsüyle her karede değişip CSG'yi her karede yeniden kurduruyordu)
        void Signature(Entity e, const glm::mat4& rel, uint64_t& h)
        {
            const auto& c = e.GetComponent<CSGShapeComponent>();
            for (int col = 0; col < 4; ++col)
                for (int row = 0; row < 4; ++row)
                {
                    const int64_t q = (int64_t)std::llround((double)rel[col][row] * 100000.0);
                    HashBytes(h, &q, sizeof(q));
                }
            HashBytes(h, &c.Shape, sizeof(c.Shape));
            HashBytes(h, &c.Operation, sizeof(c.Operation));
            HashBytes(h, &c.Size, sizeof(c.Size));
            HashBytes(h, &c.Radius, sizeof(float));
            HashBytes(h, &c.Height, sizeof(float));
            HashBytes(h, &c.Sides, sizeof(int));
            HashBytes(h, &c.SmoothFaces, sizeof(bool));
            HashBytes(h, &c.UseCollision, sizeof(bool));
            for (Entity child : e.GetChildren())
                if (IsCSG(child))
                {
                    const bool active = child.GetScene()->IsEntityActive((entt::entity)child);
                    HashBytes(h, &active, sizeof(active));
                    if (active)
                        Signature(child, rel * child.GetComponent<TransformComponent>().GetTransform(), h);
                }
        }
    }

    namespace CSG
    {
        int Rebuild(Scene& scene, uint32_t rootId)
        {
            Entity root{ (entt::entity)rootId, &scene };
            if (!IsCSG(root))
                return 0;
            const glm::mat4 rootInv = glm::inverse(root.GetComponent<TransformComponent>().WorldTransform);
            const std::vector<CPolygon> polys = Evaluate(root, rootInv);
            std::vector<Vertex> verts;
            std::vector<uint32_t> idx;
            for (const auto& p : polys)
            {
                const uint32_t base = (uint32_t)verts.size();
                for (const auto& v : p.V)
                {
                    Vertex out{};
                    out.Position = v.Pos;
                    out.Normal = p.Smooth ? v.Normal : p.Plane.N;
                    out.TexCoords = v.UV;
                    verts.push_back(out);
                }
                for (size_t k = 1; k + 1 < p.V.size(); ++k)
                {
                    idx.push_back(base);
                    idx.push_back(base + (uint32_t)k);
                    idx.push_back(base + (uint32_t)k + 1);
                }
            }
            auto& c = root.GetComponent<CSGShapeComponent>();
            if (!root.HasComponent<MeshRendererComponent>())
                root.AddComponent<MeshRendererComponent>();
            auto& mr = root.GetComponent<MeshRendererComponent>();
            mr.CustomMesh = idx.empty() ? nullptr : std::make_shared<Mesh>(verts, idx);
            c.Triangles = (int)(idx.size() / 3);
            if (c.UseCollision && !idx.empty())
            {
                if (!root.HasComponent<MeshColliderComponent>())
                    root.AddComponent<MeshColliderComponent>();
                if (PhysicsWorld3D* physics = scene.GetPhysicsWorld3D())
                    physics->MarkDirty(root);
            }
            return c.Triangles;
        }

        void Update(Scene& scene)
        {
            auto view = scene.m_Registry.view<CSGShapeComponent>();
            for (auto h : view)
            {
                Entity e{ h, &scene };
                if (IsCSG(e.GetParent()))
                    continue;   // yalnızca kökler
                uint64_t sig = 1469598103934665603ull;
                Signature(e, glm::mat4(1.0f), sig);
                auto& c = view.get<CSGShapeComponent>(h);
                if (sig == c.BuiltSignature && e.HasComponent<MeshRendererComponent>() && e.GetComponent<MeshRendererComponent>().CustomMesh)
                    continue;
                Rebuild(scene, (uint32_t)h);
                if (scene.m_Registry.valid(h))
                    scene.m_Registry.get<CSGShapeComponent>(h).BuiltSignature = sig;
            }
        }
    }

    void RegisterCSGComponents()
    {
        using C = CSGShapeComponent;
        ComponentDef<C>("CSG Shape", "CSGShapeComponent", "Rendering", ICON_FA_SHAPES)
            .Describe("Godot-style constructive solid geometry: child CSG shapes are combined with this one (union / subtraction / intersection) into a "
                      "single mesh for quick level blockouts. The top CSG object renders the result and gets a static collider.")
            .Field("Shape", "Shape", &C::Shape).Options({ "Box", "Sphere", "Cylinder", "Cone" })
            .Field("Operation", "Operation", &C::Operation).Options({ "Union", "Subtraction", "Intersection" }).Tooltip("How this shape combines with its parent CSG shape")
            .Field("Size", "Size", &C::Size).VisibleIf([](const C& c) { return c.Shape == CSGShapeType::Box; })
            .Field("Radius", "Radius", &C::Radius).Range(0.01f, 1000.0f, 0.01f).VisibleIf([](const C& c) { return c.Shape != CSGShapeType::Box; })
            .Field("Height", "Height", &C::Height).Range(0.01f, 1000.0f, 0.01f).VisibleIf([](const C& c) { return c.Shape == CSGShapeType::Cylinder || c.Shape == CSGShapeType::Cone; })
            .Field("Sides", "Sides", &C::Sides).Range(3.0f, 128.0f, 1.0f).VisibleIf([](const C& c) { return c.Shape != CSGShapeType::Box; })
            .Field("SmoothFaces", "Smooth Faces", &C::SmoothFaces).VisibleIf([](const C& c) { return c.Shape != CSGShapeType::Box; })
            .Field("UseCollision", "Use Collision", &C::UseCollision)
            .Field("Triangles", "Triangles", &C::Triangles).ReadOnly();
    }
}
