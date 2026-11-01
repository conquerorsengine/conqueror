#include "Splines.h"
#include "Core/PhysicsSystem/Physics3D/PhysicsWorld3D.h"
#include "Scene/Scene.h"
#include "Scene/Entity.h"
#include "Scene/Components.h"
#include "Scene/ComponentRegistry.h"
#include "Core/Debug/DebugDraw.h"
#include "Core/Serialization/PrefabSerializer.h"
#include "Core/Math/Math.h"
#include "Renderer/Utilities/Renderer3D/Mesh.h"

#include <IconsFontAwesome6.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <functional>

namespace Conqueror
{
    namespace
    {
        constexpr int kSamplesPerSegment = 24;

        // Centripetal Catmull-Rom (alfa 0.5): düğümlerden geçer, sivri köşe ve kendi üstüne dönme yapmaz
        glm::vec3 CatmullRom(const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& p2, const glm::vec3& p3, float t)
        {
            auto knot = [](float ti, const glm::vec3& a, const glm::vec3& b) {
                return ti + std::max(std::sqrt(glm::length(b - a)), 1e-4f);
            };
            float t0 = 0.0f, t1 = knot(t0, p0, p1), t2 = knot(t1, p1, p2), t3 = knot(t2, p2, p3);
            float tt = t1 + (t2 - t1) * t;
            glm::vec3 a1 = (t1 - tt) / (t1 - t0) * p0 + (tt - t0) / (t1 - t0) * p1;
            glm::vec3 a2 = (t2 - tt) / (t2 - t1) * p1 + (tt - t1) / (t2 - t1) * p2;
            glm::vec3 a3 = (t3 - tt) / (t3 - t2) * p2 + (tt - t2) / (t3 - t2) * p3;
            glm::vec3 b1 = (t2 - tt) / (t2 - t0) * a1 + (tt - t0) / (t2 - t0) * a2;
            glm::vec3 b2 = (t3 - tt) / (t3 - t1) * a2 + (tt - t1) / (t3 - t1) * a3;
            return (t2 - tt) / (t2 - t1) * b1 + (tt - t1) / (t2 - t1) * b2;
        }

        int SegmentCount(const SplineContainerComponent& s)
        {
            const int n = (int)s.Knots.size();
            if (n < 2) return 0;
            return s.Closed ? n : n - 1;
        }

        glm::vec3 Knot(const SplineContainerComponent& s, int i)
        {
            const int n = (int)s.Knots.size();
            if (s.Closed)
                return s.Knots[(size_t)(((i % n) + n) % n)];
            if (i < 0)
                return s.Knots[0] * 2.0f - s.Knots[1];          // uç noktada yansıtılmış hayali düğüm
            if (i >= n)
                return s.Knots[(size_t)n - 1] * 2.0f - s.Knots[(size_t)n - 2];
            return s.Knots[(size_t)i];
        }

        // Parametrik (segment, yerel t) nokta
        glm::vec3 SegmentPoint(const SplineContainerComponent& s, int seg, float t)
        {
            if (s.Mode == SplineTangentMode::Linear)
                return glm::mix(Knot(s, seg), Knot(s, seg + 1), t);
            return CatmullRom(Knot(s, seg - 1), Knot(s, seg), Knot(s, seg + 1), Knot(s, seg + 2), t);
        }

        // Yay uzunluğu tablosu: örnek noktaları ve kümülatif uzunluklar
        struct ArcTable
        {
            std::vector<glm::vec3> Points;
            std::vector<float> Cumulative;
            float Length = 0.0f;
        };

        void BuildTable(const SplineContainerComponent& s, ArcTable& table)
        {
            table.Points.clear();
            table.Cumulative.clear();
            table.Length = 0.0f;
            const int segs = SegmentCount(s);
            if (segs == 0)
            {
                if (!s.Knots.empty())
                {
                    table.Points.push_back(s.Knots[0]);
                    table.Cumulative.push_back(0.0f);
                }
                return;
            }
            for (int seg = 0; seg < segs; ++seg)
                for (int i = (seg == 0 ? 0 : 1); i <= kSamplesPerSegment; ++i)
                {
                    glm::vec3 p = SegmentPoint(s, seg, (float)i / kSamplesPerSegment);
                    if (!table.Points.empty())
                        table.Length += glm::distance(table.Points.back(), p);
                    table.Points.push_back(p);
                    table.Cumulative.push_back(table.Length);
                }
        }

        glm::vec3 SampleTable(const ArcTable& table, float t, glm::vec3* tangent)
        {
            if (table.Points.empty())
            {
                if (tangent) *tangent = glm::vec3(0, 0, -1);
                return glm::vec3(0.0f);
            }
            if (table.Points.size() == 1 || table.Length <= 1e-6f)
            {
                if (tangent) *tangent = glm::vec3(0, 0, -1);
                return table.Points.front();
            }
            float d = std::clamp(t, 0.0f, 1.0f) * table.Length;
            auto it = std::lower_bound(table.Cumulative.begin(), table.Cumulative.end(), d);
            size_t i = (size_t)std::clamp<std::ptrdiff_t>(it - table.Cumulative.begin(), 1, (std::ptrdiff_t)table.Points.size() - 1);
            float a = table.Cumulative[i - 1], b = table.Cumulative[i];
            float f = b > a ? (d - a) / (b - a) : 0.0f;
            glm::vec3 dir = table.Points[i] - table.Points[i - 1];
            if (tangent)
                *tangent = glm::length(dir) > 1e-7f ? glm::normalize(dir) : glm::vec3(0, 0, -1);
            return glm::mix(table.Points[i - 1], table.Points[i], f);
        }

        // Eğri değiştikçe tabloyu yeniden kur (düğüm verisinin özeti)
        size_t HashSpline(const SplineContainerComponent& s)
        {
            size_t h = std::hash<int>()((int)s.Mode * 2 + (s.Closed ? 1 : 0));
            for (const auto& k : s.Knots)
                for (int i = 0; i < 3; ++i)
                    h ^= std::hash<float>()(k[i]) + 0x9e3779b9 + (h << 6) + (h >> 2);
            return h;
        }

        const ArcTable& TableFor(const SplineContainerComponent& s)
        {
            struct Cached { size_t Hash = 0; ArcTable Table; };
            static std::unordered_map<const SplineContainerComponent*, Cached> s_Cache;
            if (s_Cache.size() > 4096)
                s_Cache.clear();
            auto& c = s_Cache[&s];
            size_t h = HashSpline(s);
            if (c.Hash != h || c.Table.Points.empty())
            {
                BuildTable(s, c.Table);
                c.Hash = h;
            }
            return c.Table;
        }

        uint32_t ResolveContainer(Scene& scene, uint32_t self, uint64_t uuid)
        {
            if (uuid)
            {
                Entity e = scene.FindEntityByUUID(uuid);
                return e && e.HasComponent<SplineContainerComponent>() ? (uint32_t)(entt::entity)e : 0xFFFFFFFFu;
            }
            Entity me{ (entt::entity)self, &scene };
            return me.HasComponent<SplineContainerComponent>() ? self : 0xFFFFFFFFu;
        }

        void SetWorldPose(Scene& scene, Entity e, const glm::vec3& position, const glm::quat* rotation)
        {
            scene.SetEntityWorldPose(e, position, rotation);
        }

        // Boru ağı: eğri boyunca halkalar (paralel taşınan çerçeve ile burulmasız)
        std::shared_ptr<Mesh> BuildTube(const ArcTable& table, const SplineExtrudeComponent& ex, float length)
        {
            std::vector<Vertex> vertices;
            std::vector<uint32_t> indices;
            const int sides = std::clamp(ex.Sides, 3, 64);
            const float start = std::clamp(ex.RangeStart, 0.0f, 1.0f);
            const float range = std::clamp(ex.Range, start, 1.0f) - start;
            const int rings = std::max(2, (int)std::ceil(length * range * std::max(ex.SegmentsPerUnit, 0.1f)) + 1);
            if (length * range <= 1e-4f || table.Points.size() < 2)
                return nullptr;
            glm::vec3 prevT(0.0f), normal(0.0f);
            for (int r = 0; r < rings; ++r)
            {
                float t = start + range * (float)r / (float)(rings - 1);
                glm::vec3 tangent;
                glm::vec3 c = SampleTable(table, t, &tangent);
                if (r == 0)
                {
                    glm::vec3 ref = std::fabs(tangent.y) < 0.95f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
                    normal = glm::normalize(glm::cross(glm::cross(tangent, ref), tangent));
                }
                else
                {
                    // Paralel taşıma: önceki normali yeni teğete göre döndür
                    glm::vec3 axis = glm::cross(prevT, tangent);
                    float s = glm::length(axis);
                    if (s > 1e-6f)
                    {
                        float ang = std::atan2(s, glm::dot(prevT, tangent));
                        normal = glm::angleAxis(ang, axis / s) * normal;
                    }
                    normal = glm::normalize(normal - tangent * glm::dot(normal, tangent));
                }
                prevT = tangent;
                glm::vec3 binormal = glm::cross(tangent, normal);
                for (int i = 0; i <= sides; ++i)
                {
                    float a = 6.2831853f * (float)i / (float)sides;
                    glm::vec3 n = normal * std::cos(a) + binormal * std::sin(a);
                    Vertex v{};
                    v.Position = c + n * ex.Radius;
                    v.Normal = n;
                    v.Tangent = tangent;
                    v.TexCoords = glm::vec2((float)i / (float)sides, t * length / std::max(ex.Radius * 6.2831853f, 1e-3f));
                    v.TexCoords2 = v.TexCoords;
                    vertices.push_back(v);
                }
            }
            const uint32_t stride = (uint32_t)sides + 1;
            for (int r = 0; r + 1 < rings; ++r)
                for (int i = 0; i < sides; ++i)
                {
                    uint32_t a = (uint32_t)r * stride + (uint32_t)i, b = a + stride;
                    indices.insert(indices.end(), { a, b, a + 1, a + 1, b, b + 1 });
                }
            if (ex.Capped)
            {
                for (int end = 0; end < 2; ++end)
                {
                    int r = end == 0 ? 0 : rings - 1;
                    glm::vec3 tangent;
                    glm::vec3 c = SampleTable(table, start + range * (float)r / (float)(rings - 1), &tangent);
                    glm::vec3 n = end == 0 ? -tangent : tangent;
                    uint32_t center = (uint32_t)vertices.size();
                    Vertex cv{};
                    cv.Position = c; cv.Normal = n; cv.Tangent = glm::vec3(1, 0, 0); cv.TexCoords = cv.TexCoords2 = glm::vec2(0.5f);
                    vertices.push_back(cv);
                    uint32_t first = (uint32_t)vertices.size();
                    for (int i = 0; i <= sides; ++i)
                    {
                        Vertex v = vertices[(size_t)r * stride + (size_t)i];
                        v.Normal = n;
                        v.TexCoords = v.TexCoords2 = glm::vec2(0.5f) + 0.5f * glm::vec2(std::cos(6.2831853f * i / sides), std::sin(6.2831853f * i / sides));
                        vertices.push_back(v);
                    }
                    for (int i = 0; i < sides; ++i)
                    {
                        if (end == 0) indices.insert(indices.end(), { center, first + (uint32_t)i + 1, first + (uint32_t)i });
                        else indices.insert(indices.end(), { center, first + (uint32_t)i, first + (uint32_t)i + 1 });
                    }
                }
            }
            return std::make_shared<Mesh>(vertices, indices);
        }
    }

    namespace Splines
    {
        glm::vec3 EvaluateLocal(const SplineContainerComponent& spline, float t, glm::vec3* tangent)
        {
            return SampleTable(TableFor(spline), t, tangent);
        }

        float LocalLength(const SplineContainerComponent& spline)
        {
            return TableFor(spline).Length;
        }

        bool EvaluateWorld(Scene& scene, uint32_t containerEntity, float t, glm::vec3& position, glm::vec3* tangent)
        {
            if (!scene.m_Registry.valid((entt::entity)containerEntity) || !scene.m_Registry.all_of<SplineContainerComponent>((entt::entity)containerEntity))
                return false;
            const auto& s = scene.m_Registry.get<SplineContainerComponent>((entt::entity)containerEntity);
            glm::vec3 lt;
            glm::vec3 lp = EvaluateLocal(s, t, &lt);
            glm::mat4 w = scene.ComputeWorldMatrix((entt::entity)containerEntity);
            position = glm::vec3(w * glm::vec4(lp, 1.0f));
            if (tangent)
            {
                glm::vec3 wt = glm::mat3(w) * lt;
                *tangent = glm::length(wt) > 1e-7f ? glm::normalize(wt) : glm::vec3(0, 0, -1);
            }
            return true;
        }

        float WorldLength(Scene& scene, uint32_t containerEntity)
        {
            if (!scene.m_Registry.valid((entt::entity)containerEntity) || !scene.m_Registry.all_of<SplineContainerComponent>((entt::entity)containerEntity))
                return 0.0f;
            const auto& table = TableFor(scene.m_Registry.get<SplineContainerComponent>((entt::entity)containerEntity));
            glm::mat4 w = scene.ComputeWorldMatrix((entt::entity)containerEntity);
            float len = 0.0f;
            for (size_t i = 1; i < table.Points.size(); ++i)
                len += glm::distance(glm::vec3(w * glm::vec4(table.Points[i - 1], 1.0f)), glm::vec3(w * glm::vec4(table.Points[i], 1.0f)));
            return len;
        }

        float NearestT(Scene& scene, uint32_t containerEntity, const glm::vec3& point)
        {
            if (!scene.m_Registry.valid((entt::entity)containerEntity) || !scene.m_Registry.all_of<SplineContainerComponent>((entt::entity)containerEntity))
                return 0.0f;
            const auto& table = TableFor(scene.m_Registry.get<SplineContainerComponent>((entt::entity)containerEntity));
            if (table.Points.size() < 2 || table.Length <= 1e-6f)
                return 0.0f;
            glm::vec3 lp = glm::vec3(glm::inverse(scene.ComputeWorldMatrix((entt::entity)containerEntity)) * glm::vec4(point, 1.0f));
            float best = 1e30f, bestT = 0.0f;
            for (size_t i = 1; i < table.Points.size(); ++i)
            {
                glm::vec3 a = table.Points[i - 1], b = table.Points[i], ab = b - a;
                float f = glm::dot(ab, ab) > 0.0f ? std::clamp(glm::dot(lp - a, ab) / glm::dot(ab, ab), 0.0f, 1.0f) : 0.0f;
                float d = glm::distance(lp, a + ab * f);
                if (d < best)
                {
                    best = d;
                    bestT = (table.Cumulative[i - 1] + (table.Cumulative[i] - table.Cumulative[i - 1]) * f) / table.Length;
                }
            }
            return bestT;
        }

        // Profil ağı: kesit eğri boyunca, yatay çerçevede (sağ = teğet x dünya yukarısı). Kenarlar sert (her yüzün kendi
        // köşeleri); sarım yüz normaline göre seçilir. UV: kesit koordinatı (Profile.z) ve eğri boyunca mesafe / UVLength.
        std::shared_ptr<Mesh> BuildProfile(const ArcTable& table, const SplineExtrudeComponent& ex, float length, bool closedSpline)
        {
            const float r0 = std::clamp(ex.RangeStart, 0.0f, 1.0f);
            const float r1 = std::clamp(ex.Range, r0, 1.0f);
            const float span = length * (r1 - r0);
            if (span <= 1e-4f || table.Points.size() < 2 || ex.Profile.size() < 2)
                return nullptr;
            const int rings = std::max(2, (int)std::ceil(span * std::max(ex.SegmentsPerUnit, 0.01f)) + 1);
            // Kapalı eğride doku tekrarı tam sayıya yuvarlanır: başlangıçta dikiş olmaz
            float uvLen = std::max(ex.UVLength, 1e-3f);
            if (closedSpline)
                uvLen = length / std::max(1.0f, std::round(length / uvLen));
            struct Ring { glm::vec3 C, T, R, U; float Dist; };
            std::vector<Ring> frames((size_t)rings);
            for (int r = 0; r < rings; ++r)
            {
                const float t = r0 + (r1 - r0) * (float)r / (float)(rings - 1);
                glm::vec3 tangent;
                glm::vec3 c = SampleTable(table, t, &tangent);
                glm::vec3 right = glm::cross(tangent, glm::vec3(0.0f, 1.0f, 0.0f));
                right = glm::length(right) > 1e-5f ? glm::normalize(right) : glm::vec3(1.0f, 0.0f, 0.0f);
                glm::vec3 up = glm::normalize(glm::cross(right, tangent));
                frames[(size_t)r] = { c, tangent, right, up, t * length };
            }
            std::vector<Vertex> vertices;
            std::vector<uint32_t> indices;
            for (size_t e = 0; e + 1 < ex.Profile.size(); ++e)
            {
                const glm::vec3 a = ex.Profile[e], b = ex.Profile[e + 1];
                const glm::vec2 d(b.x - a.x, b.y - a.y);
                if (glm::length(d) < 1e-5f)
                    continue;   // yalnız doku koordinatı değişimi
                const glm::vec2 n2 = glm::normalize(glm::vec2(-d.y, d.x));   // gidiş yönünün solu
                const uint32_t base = (uint32_t)vertices.size();
                for (const Ring& f : frames)
                {
                    const glm::vec3 n = glm::normalize(f.R * n2.x + f.U * n2.y);
                    const glm::vec3 across = glm::normalize(f.R * d.x + f.U * d.y);
                    for (int k = 0; k < 2; ++k)
                    {
                        const glm::vec3& q = k == 0 ? a : b;
                        Vertex v{};
                        v.Position = f.C + f.R * q.x + f.U * q.y;
                        v.Normal = n;
                        const float along = f.Dist / uvLen;
                        v.TexCoords = ex.SwapUV ? glm::vec2(along, q.z) : glm::vec2(q.z, along);
                        v.Tangent = ex.SwapUV ? f.T : across;     // +u yönü (normal haritası)
                        v.TexCoords2 = v.TexCoords;
                        vertices.push_back(v);
                    }
                }
                for (int r = 0; r + 1 < rings; ++r)
                {
                    uint32_t i0 = base + (uint32_t)r * 2, i1 = i0 + 1, i2 = i0 + 2, i3 = i0 + 3;
                    // Sarım: üçgenin geometrik normali yüz normaliyle aynı yönde olsun (arka yüz ayıklama)
                    const glm::vec3 g = glm::cross(vertices[i1].Position - vertices[i0].Position, vertices[i2].Position - vertices[i0].Position);
                    if (glm::dot(g, vertices[i0].Normal) >= 0.0f)
                        indices.insert(indices.end(), { i0, i1, i2, i1, i3, i2 });
                    else
                        indices.insert(indices.end(), { i0, i2, i1, i1, i2, i3 });
                }
            }
            if (indices.empty())
                return nullptr;
            return std::make_shared<Mesh>(vertices, indices);
        }

        void Update(Scene& scene, float dt, bool runtime)
        {
            auto& reg = scene.m_Registry;

            // ── Spline Extrude: eğri ya da ayarlar değişince ağ yeniden kurulur (düzenleyicide de) ──
            for (auto h : reg.view<SplineExtrudeComponent, MeshRendererComponent>())
            {
                auto& ex = reg.get<SplineExtrudeComponent>(h);
                uint32_t c = ResolveContainer(scene, (uint32_t)h, ex.Container);
                auto& mr = reg.get<MeshRendererComponent>(h);
                if (c == 0xFFFFFFFFu)
                {
                    mr.CustomMesh.reset();
                    ex.BuiltHash = 0;
                    continue;
                }
                const auto& spline = reg.get<SplineContainerComponent>((entt::entity)c);
                // Ağ, eğrinin yerel uzayında üretilir; eğri başka bir varlıktaysa bu varlığa göre dönüştürülür
                glm::mat4 toLocal(1.0f);
                if (c != (uint32_t)h)
                    toLocal = glm::inverse(scene.ComputeWorldMatrix(h)) * scene.ComputeWorldMatrix((entt::entity)c);
                size_t hash = HashSpline(spline);
                auto mix = [&](float v) { hash ^= std::hash<float>()(v) + 0x9e3779b9 + (hash << 6) + (hash >> 2); };
                mix(ex.Radius); mix((float)ex.Sides); mix(ex.SegmentsPerUnit); mix(ex.Capped ? 1.0f : 0.0f); mix(ex.Range);
                mix((float)ex.Shape); mix(ex.RangeStart); mix(ex.UVLength); mix(ex.SwapUV ? 1.0f : 0.0f);
                for (const auto& pp : ex.Profile) { mix(pp.x); mix(pp.y); mix(pp.z); }
                for (int i = 0; i < 4; ++i) for (int j = 0; j < 4; ++j) mix(toLocal[i][j]);
                if (hash == ex.BuiltHash && mr.CustomMesh)
                    continue;
                ex.BuiltHash = hash;
                ArcTable table = TableFor(spline);
                if (c != (uint32_t)h)
                {
                    for (auto& p : table.Points)
                        p = glm::vec3(toLocal * glm::vec4(p, 1.0f));
                    float len = 0.0f;
                    for (size_t i = 0; i < table.Points.size(); ++i)
                    {
                        if (i) len += glm::distance(table.Points[i - 1], table.Points[i]);
                        table.Cumulative[i] = len;
                    }
                    table.Length = len;
                }
                if (ex.Shape == SplineExtrudeShape::Profile)
                    mr.CustomMesh = BuildProfile(table, ex, table.Length, spline.Closed);
                else
                    mr.CustomMesh = BuildTube(table, ex, table.Length);
                // Ağ değişti: bu varlıkta Mesh Collider varsa fizik gövdesi yeni ağla yeniden kurulsun (yol üstünde sürüş)
                if (reg.all_of<MeshColliderComponent>(h))
                    if (PhysicsWorld3D* physics = scene.GetPhysicsWorld3D())
                        physics->MarkDirty(Entity{ h, &scene });
            }

            if (!runtime)
                return;

            // ── Spline Animate ──
            for (auto h : reg.view<TransformComponent, SplineAnimateComponent>())
            {
                if (!scene.IsEntityActive(h)) continue;
                auto& an = reg.get<SplineAnimateComponent>(h);
                uint32_t c = ResolveContainer(scene, (uint32_t)h, an.Container);
                if (c == 0xFFFFFFFFu || c == (uint32_t)h) continue;   // kendi eğrisinde hareket edemez
                if (!an.Started)
                {
                    an.Started = true;
                    an.Playing = an.PlayOnAwake;
                    an.NormalizedTime = std::clamp(an.StartOffset, 0.0f, 1.0f);
                    an.Direction = 1;
                }
                if (an.Playing)
                {
                    float length = WorldLength(scene, c);
                    float rate = an.UseDuration ? 1.0f / std::max(an.Duration, 1e-3f) : (length > 1e-5f ? an.Speed / length : 0.0f);
                    an.NormalizedTime += rate * dt * (float)an.Direction;
                    if (an.NormalizedTime > 1.0f || an.NormalizedTime < 0.0f)
                    {
                        switch (an.Loop)
                        {
                            case SplineLoopMode::Once:
                                an.NormalizedTime = std::clamp(an.NormalizedTime, 0.0f, 1.0f);
                                an.Playing = false;
                                break;
                            case SplineLoopMode::Loop:
                                an.NormalizedTime -= std::floor(an.NormalizedTime);
                                break;
                            case SplineLoopMode::PingPong:
                                an.Direction = -an.Direction;
                                an.NormalizedTime = an.NormalizedTime > 1.0f ? 2.0f - an.NormalizedTime : -an.NormalizedTime;
                                break;
                        }
                    }
                }
                glm::vec3 pos, tangent;
                if (!EvaluateWorld(scene, c, an.NormalizedTime, pos, &tangent))
                    continue;
                Entity e{ h, &scene };
                if (an.AlignToPath)
                {
                    glm::vec3 fwd = tangent * (float)an.Direction;
                    glm::quat rot = glm::quatLookAt(glm::normalize(fwd), glm::vec3(0, 1, 0));
                    SetWorldPose(scene, e, pos, &rot);
                }
                else
                    SetWorldPose(scene, e, pos, nullptr);
            }

            // ── Spline Instantiate: oyun başında yol boyunca kopyalar ──
            for (auto h : reg.view<TransformComponent, SplineInstantiateComponent>())
            {
                auto& si = reg.get<SplineInstantiateComponent>(h);
                if (si.Spawned || !scene.IsEntityActive(h)) continue;
                si.Spawned = true;
                uint32_t c = ResolveContainer(scene, (uint32_t)h, si.Container);
                if (c == 0xFFFFFFFFu) continue;
                Entity templ = si.Template ? scene.FindEntityByUUID(si.Template) : Entity{};
                if (si.Prefab.empty() && !templ) continue;
                float length = WorldLength(scene, c);
                int count = std::clamp((int)std::floor(length / std::max(si.Spacing, 0.01f)) + 1, 1, 4096);
                Entity parent{ h, &scene };
                for (int i = 0; i < count; ++i)
                {
                    float t = count > 1 ? (float)i / (float)(count - 1) : 0.0f;
                    glm::vec3 pos, tangent;
                    EvaluateWorld(scene, c, t, pos, &tangent);
                    Entity inst = !si.Prefab.empty() ? PrefabSerializer::Instantiate(scene, si.Prefab, parent)
                                                     : scene.DuplicateEntity(templ, templ.GetComponent<TagComponent>().Tag + " " + std::to_string(i));
                    if (!inst) break;
                    if (!si.Prefab.empty() || inst.GetParent() != parent)
                        scene.SetEntityParent(inst, parent, false);
                    glm::quat rot = glm::quatLookAt(glm::normalize(tangent), glm::vec3(0, 1, 0));
                    SetWorldPose(scene, inst, pos + rot * si.PositionOffset, si.AlignToPath ? &rot : nullptr);
                }
                scene.UpdateWorldTransforms();
            }
        }

        void DrawGizmo(Scene& scene, uint32_t entity, bool selected)
        {
            auto& reg = scene.m_Registry;
            if (!reg.valid((entt::entity)entity) || !reg.all_of<SplineContainerComponent>((entt::entity)entity))
                return;
            const auto& s = reg.get<SplineContainerComponent>((entt::entity)entity);
            if (!selected && !s.ShowGizmo)
                return;
            const auto& table = TableFor(s);
            glm::mat4 w = scene.ComputeWorldMatrix((entt::entity)entity);
            const glm::vec4 kCurve = selected ? glm::vec4(0.35f, 0.85f, 1.0f, 1.0f) : glm::vec4(0.35f, 0.85f, 1.0f, 0.55f);
            for (size_t i = 1; i < table.Points.size(); ++i)
                DebugDraw::Line(glm::vec3(w * glm::vec4(table.Points[i - 1], 1.0f)), glm::vec3(w * glm::vec4(table.Points[i], 1.0f)), kCurve, 0.0f, false);
            if (selected)
                for (size_t i = 0; i < s.Knots.size(); ++i)
                    DebugDraw::Sphere(glm::vec3(w * glm::vec4(s.Knots[i], 1.0f)), 0.12f, i == 0 ? glm::vec4(0.4f, 1.0f, 0.4f, 1.0f) : glm::vec4(1.0f, 0.85f, 0.3f, 1.0f), 0.0f, false);
        }

        void ResetRuntime(Scene& scene)
        {
            for (auto h : scene.m_Registry.view<SplineAnimateComponent>())
                scene.m_Registry.get<SplineAnimateComponent>(h).Started = false;
            for (auto h : scene.m_Registry.view<SplineInstantiateComponent>())
                scene.m_Registry.get<SplineInstantiateComponent>(h).Spawned = false;
        }
    }

    void RegisterSplineComponents()
    {
        const glm::vec4 kSplineColor{ 0.35f, 0.85f, 1.0f, 1.0f };

        ComponentDef<SplineContainerComponent>("Spline Container", "SplineContainerComponent", "Splines", ICON_FA_BEZIER_CURVE).Color(kSplineColor)
            .Describe("Smooth curve through knots (paths, rails, roads, camera tracks). Knots are in local space.")
            .Field("Knots", "Knots", &SplineContainerComponent::Knots).Speed(0.05f)
            .Field("Closed", "Closed", &SplineContainerComponent::Closed)
            .Field("Mode", "Tangent Mode", &SplineContainerComponent::Mode).Options({ "Catmull-Rom (smooth)", "Linear" })
            .Field("ShowGizmo", "Show Gizmo", &SplineContainerComponent::ShowGizmo)
                .Tooltip("Draw the curve even when the entity is not selected.");

        ComponentDef<SplineAnimateComponent>("Spline Animate", "SplineAnimateComponent", "Splines", ICON_FA_ROUTE).Color(kSplineColor)
            .Describe("Moves this entity along a spline at constant speed.")
            .Field("Container", "Spline", &SplineAnimateComponent::Container)
                .Tooltip("Entity with a Spline Container.")
            .Field("UseDuration", "Use Duration", &SplineAnimateComponent::UseDuration)
            .Field("Speed", "Speed", &SplineAnimateComponent::Speed).Range(0.0f, 10000.0f, 0.05f).Format("%.2f m/s")
                .VisibleIf([](const SplineAnimateComponent& c) { return !c.UseDuration; })
            .Field("Duration", "Duration", &SplineAnimateComponent::Duration).Range(0.01f, 100000.0f, 0.05f).Format("%.2f s")
                .VisibleIf([](const SplineAnimateComponent& c) { return c.UseDuration; })
            .Field("Loop", "Loop Mode", &SplineAnimateComponent::Loop).Options({ "Once", "Loop", "Ping Pong" })
            .Field("AlignToPath", "Align To Path", &SplineAnimateComponent::AlignToPath)
            .Field("PlayOnAwake", "Play On Awake", &SplineAnimateComponent::PlayOnAwake)
            .Field("StartOffset", "Start Offset", &SplineAnimateComponent::StartOffset).Range(0.0f, 1.0f, 0.005f)
            .Field("NormalizedTime", "Normalized Time", &SplineAnimateComponent::NormalizedTime).ReadOnly().Header("Runtime")
            .Field("Playing", "Playing", &SplineAnimateComponent::Playing).ReadOnly();

        ComponentDef<SplineExtrudeComponent>("Spline Extrude", "SplineExtrudeComponent", "Splines", ICON_FA_GRIP_LINES).Color(kSplineColor)
            .Requires("MeshRendererComponent")
            .Describe("Builds a mesh along a spline: a tube (pipes, cables) or your own cross-section profile (roads, kerbs, barriers, "
                      "rails) kept level with the world. Uses this entity's Mesh Renderer material; add a Mesh Collider to drive or walk on it.")
            .Field("Container", "Spline", &SplineExtrudeComponent::Container)
                .Tooltip("Entity with a Spline Container (empty = this entity).")
            .Field("Shape", "Shape", &SplineExtrudeComponent::Shape).Options({ "Tube", "Profile" })
            .Field("Radius", "Radius", &SplineExtrudeComponent::Radius).Range(0.001f, 1000.0f, 0.005f)
                .VisibleIf([](const SplineExtrudeComponent& c) { return c.Shape == SplineExtrudeShape::Tube; })
            .Field("Sides", "Sides", &SplineExtrudeComponent::Sides).Range(3.0f, 64.0f, 1.0f)
                .VisibleIf([](const SplineExtrudeComponent& c) { return c.Shape == SplineExtrudeShape::Tube; })
            .Field("Capped", "Capped", &SplineExtrudeComponent::Capped)
                .VisibleIf([](const SplineExtrudeComponent& c) { return c.Shape == SplineExtrudeShape::Tube; })
            .Field("Profile", "Profile", &SplineExtrudeComponent::Profile)
                .Tooltip("Cross-section points: x = right, y = up (metres), z = texture coordinate across. The visible side is on the "
                         "left of the point order (left to right for a road, clockwise for solid shapes).")
                .VisibleIf([](const SplineExtrudeComponent& c) { return c.Shape == SplineExtrudeShape::Profile; })
            .Field("UVLength", "UV Length", &SplineExtrudeComponent::UVLength).Range(0.01f, 10000.0f, 0.05f).Format("%.2f m")
                .Tooltip("Metres per texture repeat along the spline (rounded to fit a closed spline).")
                .VisibleIf([](const SplineExtrudeComponent& c) { return c.Shape == SplineExtrudeShape::Profile; })
            .Field("SwapUV", "Swap UV", &SplineExtrudeComponent::SwapUV)
                .Tooltip("Texture u runs along the spline and v across the profile (signs, barrier stripes).")
                .VisibleIf([](const SplineExtrudeComponent& c) { return c.Shape == SplineExtrudeShape::Profile; })
            .Field("SegmentsPerUnit", "Segments Per Unit", &SplineExtrudeComponent::SegmentsPerUnit).Range(0.01f, 100.0f, 0.05f)
            .Field("RangeStart", "Range Start", &SplineExtrudeComponent::RangeStart).Range(0.0f, 1.0f, 0.005f)
                .Tooltip("Where the mesh starts along the spline (0..1). Split a long road into chunks for culling.")
            .Field("Range", "Range End", &SplineExtrudeComponent::Range).Range(0.0f, 1.0f, 0.005f)
                .Tooltip("Where the mesh ends along the spline (animate it for growing paths).");

        ComponentDef<SplineInstantiateComponent>("Spline Instantiate", "SplineInstantiateComponent", "Splines", ICON_FA_ELLIPSIS).Color(kSplineColor)
            .Describe("At play start, places copies of a prefab (or a template entity) along a spline.")
            .Field("Container", "Spline", &SplineInstantiateComponent::Container)
            .Field("Prefab", "Prefab", &SplineInstantiateComponent::Prefab, FieldKind::Asset).Filter(".cqprefab")
            .Field("Template", "Template Entity", &SplineInstantiateComponent::Template)
                .Tooltip("Used when no prefab is set.")
            .Field("Spacing", "Spacing", &SplineInstantiateComponent::Spacing).Range(0.01f, 10000.0f, 0.05f).Format("%.2f m")
            .Field("AlignToPath", "Align To Path", &SplineInstantiateComponent::AlignToPath)
            .Field("PositionOffset", "Position Offset", &SplineInstantiateComponent::PositionOffset).Speed(0.01f);
    }
}
