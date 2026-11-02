#include "Fracture.h"
#include "Scene/Scene.h"
#include "Scene/Entity.h"
#include "Scene/Components.h"
#include "Scene/ComponentRegistry.h"
#include "Scene/VertexPaint.h"
#include "Core/Math/Math.h"
#include "Core/Logging/Log.h"
#include "Core/PhysicsSystem/Physics3D/PhysicsWorld3D.h"
#include "Renderer/Utilities/Renderer3D/Mesh.h"
#include "Renderer/Utilities/Renderer3D/ModelLoader.h"

#include <IconsFontAwesome6.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <random>
#include <tuple>
#include <unordered_map>

namespace Conqueror
{
    namespace
    {
        // Çokyüzlü köşesi: konum + kaynak öznitelikleri (kırpmada doğrusal karışır)
        struct PV
        {
            glm::dvec3 P{ 0.0 };
            glm::vec3 N{ 0.0f, 1.0f, 0.0f };
            glm::vec2 UV{ 0.0f };
        };

        struct Face
        {
            std::vector<PV> Points;   // dışarıdan bakınca saat yönü tersine
            bool Interior = false;
        };

        using Poly = std::vector<Face>;

        PV Lerp(const PV& a, const PV& b, double t)
        {
            PV r;
            r.P = a.P + (b.P - a.P) * t;
            r.N = glm::normalize(glm::mix(a.N, b.N, (float)t) + glm::vec3(1e-6f));
            r.UV = glm::mix(a.UV, b.UV, (float)t);
            return r;
        }

        glm::vec2 PlanarUV(const glm::dvec3& p, const glm::dvec3& n)
        {
            const glm::dvec3 a = glm::abs(n);
            if (a.x >= a.y && a.x >= a.z) return glm::vec2((float)p.z, (float)p.y) * 0.5f;
            if (a.y >= a.z) return glm::vec2((float)p.x, (float)p.z) * 0.5f;
            return glm::vec2((float)p.x, (float)p.y) * 0.5f;
        }

        // Düzlemle kırp: n·x + d <= 0 tarafı kalır; kesit kapak yüzü (iç yüz) olarak eklenir
        void Clip(Poly& poly, const glm::dvec3& n, double d, double eps)
        {
            Poly out;
            out.reserve(poly.size() + 1);
            std::vector<PV> cut;
            for (Face& f : poly)
            {
                const size_t count = f.Points.size();
                if (count < 3)
                    continue;
                Face nf;
                nf.Interior = f.Interior;
                for (size_t i = 0; i < count; ++i)
                {
                    const PV& a = f.Points[i];
                    const PV& b = f.Points[(i + 1) % count];
                    const double da = glm::dot(n, a.P) + d;
                    const double db = glm::dot(n, b.P) + d;
                    if (da <= eps)
                        nf.Points.push_back(a);
                    if ((da < -eps && db > eps) || (da > eps && db < -eps))
                    {
                        PV x = Lerp(a, b, da / (da - db));
                        nf.Points.push_back(x);
                        cut.push_back(x);
                    }
                    else if (std::abs(da) <= eps)
                        cut.push_back(a);
                }
                if (nf.Points.size() >= 3)
                    out.push_back(std::move(nf));
            }
            // Kapak: kesit noktaları düzlem içinde açıya göre sıralı (dış normal = +n)
            if (cut.size() >= 3)
            {
                glm::dvec3 c(0.0);
                for (auto& p : cut) c += p.P;
                c /= (double)cut.size();
                glm::dvec3 u = glm::normalize(std::abs(n.x) < 0.9 ? glm::cross(n, glm::dvec3(1, 0, 0)) : glm::cross(n, glm::dvec3(0, 1, 0)));
                glm::dvec3 v = glm::cross(n, u);
                std::sort(cut.begin(), cut.end(), [&](const PV& a, const PV& b) {
                    return std::atan2(glm::dot(a.P - c, v), glm::dot(a.P - c, u)) < std::atan2(glm::dot(b.P - c, v), glm::dot(b.P - c, u));
                });
                Face cap;
                cap.Interior = true;
                for (auto& p : cut)
                {
                    if (!cap.Points.empty() && glm::distance(cap.Points.back().P, p.P) < eps * 4.0)
                        continue;
                    PV q = p;
                    q.N = glm::vec3(n);
                    q.UV = PlanarUV(p.P, n);
                    cap.Points.push_back(q);
                }
                if (cap.Points.size() >= 3 && glm::distance(cap.Points.front().P, cap.Points.back().P) < eps * 4.0)
                    cap.Points.pop_back();
                if (cap.Points.size() >= 3)
                    out.push_back(std::move(cap));
            }
            poly = std::move(out);
        }

        struct CacheKey
        {
            const Mesh* Source;
            int Count;
            uint32_t Seed;
            bool operator<(const CacheKey& o) const { return std::tie(Source, Count, Seed) < std::tie(o.Source, o.Count, o.Seed); }
        };

        struct CacheEntry
        {
            std::weak_ptr<Mesh> Source;
            glm::vec3 Interior{ 0.0f };
            std::vector<Fracture::Piece> Pieces;
        };

        std::map<CacheKey, CacheEntry>& Cache()
        {
            static std::map<CacheKey, CacheEntry> cache;
            return cache;
        }

        struct PendingBreak
        {
            uint32_t Entity;
            glm::vec3 Point;
            float Impulse;
        };

        struct SpawnedPiece
        {
            uint32_t Entity;
            float Remaining;
            glm::vec3 Scale;
        };

        struct SceneState
        {
            std::vector<PendingBreak> Pending;
            std::vector<SpawnedPiece> Pieces;
        };

        std::unordered_map<Scene*, SceneState>& States()
        {
            static std::unordered_map<Scene*, SceneState> states;
            return states;
        }
    }

    namespace Fracture
    {
        const std::vector<Piece>& BuildPieces(const std::shared_ptr<Mesh>& source, int count, uint32_t seed, const glm::vec3& interiorColor)
        {
            static const std::vector<Piece> s_Empty;
            if (!source)
                return s_Empty;
            count = std::clamp(count, 2, 64);
            CacheKey key{ source.get(), count, seed };
            auto& cache = Cache();
            auto it = cache.find(key);
            if (it != cache.end() && !it->second.Source.expired() && it->second.Interior == interiorColor)
                return it->second.Pieces;

            const auto& verts = source->GetVertices();
            const auto& idx = source->GetIndices();
            CacheEntry entry;
            entry.Source = source;
            entry.Interior = interiorColor;
            if (verts.empty() || idx.size() < 3)
                return (cache[key] = std::move(entry)).Pieces;

            glm::dvec3 bmin(1e30), bmax(-1e30);
            for (const auto& v : verts)
            {
                bmin = glm::min(bmin, glm::dvec3(v.Position));
                bmax = glm::max(bmax, glm::dvec3(v.Position));
            }
            const double diag = std::max(glm::length(bmax - bmin), 1e-6);
            const double eps = diag * 1e-6;

            // Kaynak çokyüzlü: mesh konveksse kendi üçgenleri (UV / normal korunur), değilse sınır kutusu
            std::vector<std::pair<glm::dvec3, double>> planes;
            Poly base;
            bool convex = true;
            for (size_t i = 0; i + 2 < idx.size(); i += 3)
            {
                const glm::dvec3 a(verts[idx[i]].Position), b(verts[idx[i + 1]].Position), c(verts[idx[i + 2]].Position);
                glm::dvec3 n = glm::cross(b - a, c - a);
                const double len = glm::length(n);
                if (len < 1e-14 * diag * diag)
                    continue;
                n /= len;
                const double d = -glm::dot(n, a);
                for (const auto& v : verts)
                    if (glm::dot(n, glm::dvec3(v.Position)) + d > diag * 1e-3)
                    {
                        convex = false;
                        break;
                    }
                if (!convex)
                    break;
                planes.push_back({ n, d });
                Face f;
                for (int k = 0; k < 3; ++k)
                {
                    const Vertex& sv = verts[idx[i + k]];
                    f.Points.push_back({ glm::dvec3(sv.Position), sv.Normal, sv.TexCoords });
                }
                base.push_back(std::move(f));
            }
            if (!convex || base.size() < 4)
            {
                base.clear();
                planes.clear();
                const glm::dvec3 corner[8] = {
                    { bmin.x, bmin.y, bmin.z }, { bmax.x, bmin.y, bmin.z }, { bmax.x, bmax.y, bmin.z }, { bmin.x, bmax.y, bmin.z },
                    { bmin.x, bmin.y, bmax.z }, { bmax.x, bmin.y, bmax.z }, { bmax.x, bmax.y, bmax.z }, { bmin.x, bmax.y, bmax.z } };
                const int faces[6][4] = { { 0, 3, 2, 1 }, { 4, 5, 6, 7 }, { 0, 1, 5, 4 }, { 3, 7, 6, 2 }, { 0, 4, 7, 3 }, { 1, 2, 6, 5 } };
                for (auto& fi : faces)
                {
                    Face f;
                    const glm::dvec3 n = glm::normalize(glm::cross(corner[fi[1]] - corner[fi[0]], corner[fi[2]] - corner[fi[0]]));
                    for (int k = 0; k < 4; ++k)
                        f.Points.push_back({ corner[fi[k]], glm::vec3(n), PlanarUV(corner[fi[k]], n) });
                    planes.push_back({ n, -glm::dot(n, corner[fi[0]]) });
                    base.push_back(std::move(f));
                }
            }

            // Voronoi tohumları: çokyüzlünün içinde (tekrarlanabilir rastgele)
            std::mt19937 rng(seed);
            std::uniform_real_distribution<double> ux(bmin.x, bmax.x), uy(bmin.y, bmax.y), uz(bmin.z, bmax.z);
            std::vector<glm::dvec3> sites;
            for (int attempt = 0; attempt < count * 400 && (int)sites.size() < count; ++attempt)
            {
                const glm::dvec3 p(ux(rng), uy(rng), uz(rng));
                bool inside = true;
                for (auto& [n, d] : planes)
                    if (glm::dot(n, p) + d > -diag * 0.02)
                    {
                        inside = false;
                        break;
                    }
                if (inside)
                    sites.push_back(p);
            }

            for (size_t i = 0; i < sites.size(); ++i)
            {
                Poly cell = base;
                for (size_t j = 0; j < sites.size() && !cell.empty(); ++j)
                {
                    if (i == j)
                        continue;
                    const glm::dvec3 n = glm::normalize(sites[j] - sites[i]);
                    const glm::dvec3 mid = (sites[i] + sites[j]) * 0.5;
                    Clip(cell, n, -glm::dot(n, mid), eps);
                }
                if (cell.size() < 4)
                    continue;
                // Üçgenle (yelpaze), hacim ve ağırlık merkezi
                std::vector<Vertex> mv;
                std::vector<uint32_t> mi;
                Piece piece;
                double volume = 0.0;
                glm::dvec3 centroid(0.0);
                const glm::vec4 inner(interiorColor, 1.0f);
                for (const Face& f : cell)
                {
                    const uint32_t start = (uint32_t)mv.size();
                    const glm::dvec3 fn = glm::normalize(glm::cross(f.Points[1].P - f.Points[0].P, f.Points[2].P - f.Points[0].P) + glm::dvec3(1e-12));
                    for (const PV& p : f.Points)
                    {
                        Vertex v{};
                        v.Position = glm::vec3(p.P);
                        v.Normal = f.Interior ? glm::vec3(fn) : p.N;
                        v.TexCoords = p.UV;
                        mv.push_back(v);
                        piece.Colors.push_back(f.Interior ? inner : glm::vec4(1.0f));
                    }
                    for (size_t k = 1; k + 1 < f.Points.size(); ++k)
                    {
                        mi.push_back(start);
                        mi.push_back(start + (uint32_t)k);
                        mi.push_back(start + (uint32_t)k + 1);
                        const glm::dvec3 a = f.Points[0].P, b = f.Points[k].P, c = f.Points[k + 1].P;
                        const double tv = glm::dot(a, glm::cross(b, c)) / 6.0;
                        volume += tv;
                        centroid += tv * (a + b + c) / 4.0;
                    }
                }
                if (volume <= diag * diag * diag * 1e-7 || mi.size() < 12)
                    continue;
                piece.Volume = (float)volume;
                piece.Center = glm::vec3(centroid / volume);
                piece.PieceMesh = std::make_shared<Mesh>(mv, mi);
                entry.Pieces.push_back(std::move(piece));
            }
            CQ_CORE_INFO("[Fracture] {0} pieces ({1} source)", entry.Pieces.size(), convex ? "convex mesh" : "bounding box");
            return (cache[key] = std::move(entry)).Pieces;
        }

        int Break(Scene& scene, uint32_t entityId, const glm::vec3& worldPoint, float impulse)
        {
            auto& reg = scene.m_Registry;
            const entt::entity h = (entt::entity)entityId;
            if (!reg.valid(h) || !reg.all_of<FractureComponent>(h))
                return 0;
            Entity src{ h, &scene };
            auto& fc = src.GetComponent<FractureComponent>();
            if (fc.Broken)
                return 0;
            const auto meshes = VertexPaint::MeshesOf(src);
            if (meshes.empty())
                return 0;
            const auto& pieces = BuildPieces(meshes[0], fc.Pieces, (uint32_t)fc.Seed, fc.InteriorColor);
            if (pieces.empty())
                return 0;
            fc.Broken = true;

            const glm::mat4 world = src.GetComponent<TransformComponent>().WorldTransform;
            glm::vec3 position, euler, scale;
            Math::DecomposeTransform(world, position, euler, scale);
            const std::string name = src.GetComponent<TagComponent>().Tag + " Piece";
            PhysicsWorld3D* physics = scene.GetPhysicsWorld3D();

            // Kaynağın görünümü ve kütlesi
            MeshRendererComponent look;
            if (src.HasComponent<MeshRendererComponent>())
                look = src.GetComponent<MeshRendererComponent>();
            else if (src.HasComponent<ModelComponent>() && src.GetComponent<ModelComponent>().ModelData)
            {
                auto& model = *src.GetComponent<ModelComponent>().ModelData;
                if (!model.Materials.empty())
                    look.MaterialInstance = model.Materials[0];
            }
            float totalVolume = 0.0f;
            for (const auto& p : pieces) totalVolume += p.Volume;
            float totalMass = std::max(totalVolume * std::abs(scale.x * scale.y * scale.z) * 400.0f, 1.0f);
            glm::vec3 baseVelocity(0.0f);
            if (src.HasComponent<RigidbodyComponent>())
            {
                totalMass = std::max(src.GetComponent<RigidbodyComponent>().Mass, 0.01f);
                if (fc.InheritVelocity && physics)
                    baseVelocity = physics->GetLinearVelocity(src);
            }
            const float explosion = fc.ExplosionSpeed + std::min(impulse * 0.02f, 6.0f);
            const float lifetime = fc.PieceLifetime;
            const glm::vec3 interior = fc.InteriorColor;

            auto& state = States()[&scene];
            int spawned = 0;
            for (const auto& p : pieces)
            {
                Entity e = scene.CreateEntity(name);
                auto& tc = e.GetComponent<TransformComponent>();
                tc.Position = position;
                tc.Rotation = euler;
                tc.Scale = scale;
                auto& mr = e.AddComponent<MeshRendererComponent>();
                mr = look;
                mr.CustomMesh = p.PieceMesh;
                auto& rb = e.AddComponent<RigidbodyComponent>();
                rb.Type = RigidbodyComponent::BodyType::Dynamic;
                rb.Mass = std::max(totalMass * (totalVolume > 0.0f ? p.Volume / totalVolume : 1.0f / (float)pieces.size()), 0.01f);
                rb.RuntimeBody = nullptr;
                auto& mc = e.AddComponent<MeshColliderComponent>();
                mc.IsConvex = true;
                mc.RuntimeShape = nullptr;
                VertexPaint::SetColors(e, 0, p.Colors);
                (void)interior;
                scene.UpdateWorldTransforms();
                if (physics)
                {
                    physics->CreateBody(e);
                    const glm::vec3 center = glm::vec3(world * glm::vec4(p.Center, 1.0f));
                    glm::vec3 away = center - worldPoint;
                    away = glm::dot(away, away) > 1e-8f ? glm::normalize(away) : glm::vec3(0.0f, 1.0f, 0.0f);
                    physics->SetLinearVelocity(e, baseVelocity + away * explosion);
                }
                state.Pieces.push_back({ (uint32_t)(entt::entity)e, lifetime, scale });
                ++spawned;
            }
            scene.DestroyEntity(src);
            return spawned;
        }

        void OnContact(Scene& scene, const Physics3D::ContactEvent& ev)
        {
            if (ev.Type != Physics3D::ContactEvent::Enter || ev.Trigger)
                return;
            auto& reg = scene.m_Registry;
            for (uint32_t id : { ev.EntityA, ev.EntityB })
            {
                if (id == Physics3D::NoEntity || !reg.valid((entt::entity)id) || !reg.all_of<FractureComponent>((entt::entity)id))
                    continue;
                const auto& fc = reg.get<FractureComponent>((entt::entity)id);
                if (fc.Broken || fc.BreakImpulse <= 0.0f || ev.Impulse < fc.BreakImpulse)
                    continue;
                States()[&scene].Pending.push_back({ id, ev.Point, ev.Impulse });
            }
        }

        void Update(Scene& scene, float dt)
        {
            auto it = States().find(&scene);
            if (it == States().end())
                return;
            SceneState& state = it->second;
            if (!state.Pending.empty())
            {
                auto pending = std::move(state.Pending);
                state.Pending.clear();
                for (const auto& b : pending)
                    Break(scene, b.Entity, b.Point, b.Impulse);
            }
            // Ömür: son saniyede küçülerek kaybolur
            for (size_t i = 0; i < state.Pieces.size();)
            {
                SpawnedPiece& p = state.Pieces[i];
                const entt::entity h = (entt::entity)p.Entity;
                if (!scene.m_Registry.valid(h))
                {
                    state.Pieces.erase(state.Pieces.begin() + (long)i);
                    continue;
                }
                if (p.Remaining <= 0.0f && p.Remaining > -1.0f)   // kalıcı parçalar (ömür 0) -1'e ayarlanır
                {
                    p.Remaining = -2.0f;
                }
                if (p.Remaining > 0.0f)
                {
                    p.Remaining -= dt;
                    if (p.Remaining < 1.0f)
                        scene.m_Registry.get<TransformComponent>(h).Scale = p.Scale * std::max(p.Remaining, 0.01f);
                    if (p.Remaining <= 0.0f)
                    {
                        scene.DestroyEntity(Entity{ h, &scene });
                        state.Pieces.erase(state.Pieces.begin() + (long)i);
                        continue;
                    }
                }
                ++i;
            }
        }

        void ForgetScene(Scene* scene)
        {
            States().erase(scene);
        }

        void ClearCache()
        {
            Cache().clear();
            States().clear();
        }
    }

    void RegisterFractureComponents()
    {
        using C = FractureComponent;
        ComponentDef<C>("Fracture", "FractureComponent", "Physics", ICON_FA_BURST)
            .Describe("Breaks the object into Voronoi pieces on a strong hit or from scripts (FractureBreak), like Unreal's Chaos Geometry Collection. "
                      "Pieces get convex colliders, mass by volume and fly away from the impact point.")
            .Field("Pieces", "Pieces", &C::Pieces).Range(2.0f, 64.0f, 1.0f)
            .Field("Seed", "Seed", &C::Seed).Range(0.0f, 100000.0f, 1.0f)
            .Field("BreakImpulse", "Break Impulse", &C::BreakImpulse).Range(0.0f, 100000.0f, 0.1f).Tooltip("Collision impulse that breaks it (0 = only scripts)")
            .Field("ExplosionSpeed", "Explosion Speed", &C::ExplosionSpeed).Range(0.0f, 100.0f, 0.05f)
            .Field("PieceLifetime", "Piece Lifetime", &C::PieceLifetime).Range(0.0f, 600.0f, 0.1f).Tooltip("Seconds before pieces shrink away (0 = keep)")
            .Field("InteriorColor", "Interior Color", &C::InteriorColor, FieldKind::Color3)
            .Field("InheritVelocity", "Inherit Velocity", &C::InheritVelocity)
            .Field("Broken", "Broken", &C::Broken).ReadOnly();
    }
}
