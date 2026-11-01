#include "ScenePicking.h"
#include "Scene/Billboards.h"
#include "Renderer/Renderer2D.h"
#include "Terrain/Terrain.h"
#include "Scene.h"
#include "Components.h"
#include "Renderer/Utilities/Renderer3D/Renderer3D.h"
#include "Renderer/Utilities/Renderer3D/ModelLoader.h"
#include "Renderer/Utilities/Renderer3D/SkinnedMesh.h"
#include "Renderer/RHI/Texture.h"

#include <algorithm>
#include <limits>
#include <mutex>
#include <unordered_map>

namespace Conqueror
{
    namespace
    {
        struct Bounds { glm::vec3 Min{ 0.0f }, Max{ 0.0f }; };

        // Mesh nesnesi başına önbellek (meshler değişmez)
        std::unordered_map<const void*, Bounds> s_MeshBounds;
        std::mutex s_BoundsMutex;

        template<typename V>
        Bounds ComputeBounds(const std::vector<V>& vertices)
        {
            Bounds b;
            if (vertices.empty())
                return b;
            b.Min = glm::vec3(std::numeric_limits<float>::max());
            b.Max = glm::vec3(-std::numeric_limits<float>::max());
            for (const auto& v : vertices)
            {
                b.Min = glm::min(b.Min, v.Position);
                b.Max = glm::max(b.Max, v.Position);
            }
            return b;
        }

        Bounds MeshBounds(const std::shared_ptr<Mesh>& mesh)
        {
            std::lock_guard<std::mutex> lock(s_BoundsMutex);
            auto it = s_MeshBounds.find(mesh.get());
            if (it != s_MeshBounds.end())
                return it->second;
            Bounds b = ComputeBounds(mesh->GetVertices());
            s_MeshBounds[mesh.get()] = b;
            return b;
        }

        Bounds SkinnedBounds(const std::shared_ptr<SkinnedMesh>& mesh)
        {
            std::lock_guard<std::mutex> lock(s_BoundsMutex);
            auto it = s_MeshBounds.find(mesh.get());
            if (it != s_MeshBounds.end())
                return it->second;
            Bounds b = ComputeBounds(mesh->GetVertices());
            s_MeshBounds[mesh.get()] = b;
            return b;
        }

        // Yerel uzayda ışın-AABB (slab); t ışın parametresi
        bool RayAABB(const glm::vec3& o, const glm::vec3& d, const glm::vec3& mn, const glm::vec3& mx, float& tOut)
        {
            float tmin = 0.0f, tmax = std::numeric_limits<float>::max();
            for (int i = 0; i < 3; ++i)
            {
                if (std::abs(d[i]) < 1e-9f)
                {
                    if (o[i] < mn[i] || o[i] > mx[i])
                        return false;
                    continue;
                }
                float inv = 1.0f / d[i];
                float t1 = (mn[i] - o[i]) * inv;
                float t2 = (mx[i] - o[i]) * inv;
                if (t1 > t2) std::swap(t1, t2);
                tmin = std::max(tmin, t1);
                tmax = std::min(tmax, t2);
                if (tmin > tmax)
                    return false;
            }
            tOut = tmin;
            return true;
        }

        bool RaySphere(const glm::vec3& o, const glm::vec3& d, const glm::vec3& c, float r, float& t)
        {
            glm::vec3 oc = o - c;
            float b = glm::dot(oc, d);
            float cc = glm::dot(oc, oc) - r * r;
            float disc = b * b - cc;
            if (disc < 0.0f)
                return false;
            float s = std::sqrt(disc);
            t = -b - s;
            if (t < 0.0f) t = -b + s;
            return t >= 0.0f;
        }
    }

    bool ScenePicking::GetLocalBounds(Entity entity, glm::vec3& outMin, glm::vec3& outMax)
    {
        bool any = false;
        glm::vec3 mn(std::numeric_limits<float>::max()), mx(-std::numeric_limits<float>::max());
        auto merge = [&](const Bounds& b)
        {
            mn = glm::min(mn, b.Min);
            mx = glm::max(mx, b.Max);
            any = true;
        };

        if (entity.HasComponent<MeshRendererComponent>())
        {
            auto& mr = entity.GetComponent<MeshRendererComponent>();
            std::shared_ptr<Mesh> mesh = mr.CustomMesh;   // prosedürel ağ (kumaş, Spline Extrude) çizildiği gibi seçilir
            if (!mesh)
            {
                switch (mr.Type)
                {
                    case MeshType::Sphere: mesh = Renderer3D::GetSphereMesh(); break;
                    case MeshType::Plane: mesh = Renderer3D::GetPlaneMesh(); break;
                    case MeshType::Cylinder: mesh = Renderer3D::GetCylinderMesh(); break;
                    default: mesh = Renderer3D::GetCubeMesh(); break;
                }
            }
            if (mesh)
            {
                Bounds b = MeshBounds(mesh);
                // Düzlem sonsuz ince: seçilebilsin diye biraz kalınlaştır
                if (b.Max.y - b.Min.y < 1e-3f) { b.Min.y -= 0.01f; b.Max.y += 0.01f; }
                merge(b);
            }
        }
        if (entity.HasComponent<RectTransformComponent>())
        {
            // UI öğesi: WorldTransform pivot merkezli (UISystem::UpdateEditorTransforms)
            const auto& rt = entity.GetComponent<RectTransformComponent>();
            glm::vec2 size(rt.ComputedRect.z, rt.ComputedRect.w);
            outMin = glm::vec3(-size * rt.Pivot, -0.5f);
            outMax = glm::vec3(size * (glm::vec2(1.0f) - rt.Pivot), 0.5f);
            return size.x > 0.0f && size.y > 0.0f;
        }
        if (entity.HasComponent<TerrainComponent>())
        {
            Bounds b;
            TerrainSystem::GetLocalBounds(entity.GetComponent<TerrainComponent>(), b.Min, b.Max);
            merge(b);
        }
        {
            Bounds b;
            if (Billboards::GetLocalBounds(*entity.GetScene(), (uint32_t)entity, b.Min, b.Max))
                merge(b);
        }
        if (entity.HasComponent<ModelComponent>())
        {
            auto& mc = entity.GetComponent<ModelComponent>();
            if (mc.ModelData)
            {
                for (auto& m : mc.ModelData->Meshes)
                    if (m) merge(MeshBounds(m));
                for (auto& m : mc.ModelData->SkinnedMeshes)
                    if (m) merge(SkinnedBounds(m));
            }
        }
        if (entity.HasComponent<SpriteRendererComponent>())
        {
            auto& sr = entity.GetComponent<SpriteRendererComponent>();
            glm::vec2 mn(-0.5f), mx(0.5f);
            Renderer2D::GetSpriteLocalBounds(sr, mn, mx);
            merge({ glm::vec3(mn, -0.01f), glm::vec3(mx, 0.01f) });
        }
        if (entity.HasComponent<ImageComponent>())
        {
            auto& img = entity.GetComponent<ImageComponent>();
            glm::vec2 half(0.5f);
            if (img.Texture && img.Texture->GetWidth() > 0)
                half = glm::vec2((float)img.Texture->GetWidth(), (float)img.Texture->GetHeight()) / 200.0f;
            merge({ glm::vec3(-half, -0.01f), glm::vec3(half, 0.01f) });
        }
        if (entity.HasComponent<TextRendererComponent>() && !any)
        {
            auto& text = entity.GetComponent<TextRendererComponent>();
            float h = std::max(text.FontSize / 100.0f, 0.1f);
            float w = h * 0.55f * (float)std::max<size_t>(text.Text.size(), 1);
            merge({ glm::vec3(0.0f, -h * 0.25f, -0.01f), glm::vec3(w, h, 0.01f) });
        }

        if (!any)
            return false;
        outMin = mn;
        outMax = mx;
        return true;
    }

    bool ScenePicking::GetWorldBounds(Entity entity, glm::vec3& outMin, glm::vec3& outMax)
    {
        glm::vec3 mn, mx;
        const auto& tc = entity.GetComponent<TransformComponent>();
        if (!GetLocalBounds(entity, mn, mx))
        {
            glm::vec3 p = glm::vec3(tc.WorldTransform[3]);
            outMin = p - glm::vec3(0.5f);
            outMax = p + glm::vec3(0.5f);
            return false;
        }
        glm::vec3 wmn(std::numeric_limits<float>::max()), wmx(-std::numeric_limits<float>::max());
        for (int i = 0; i < 8; ++i)
        {
            glm::vec3 corner((i & 1) ? mx.x : mn.x, (i & 2) ? mx.y : mn.y, (i & 4) ? mx.z : mn.z);
            glm::vec3 w = glm::vec3(tc.WorldTransform * glm::vec4(corner, 1.0f));
            wmn = glm::min(wmn, w);
            wmx = glm::max(wmx, w);
        }
        outMin = wmn;
        outMax = wmx;
        return true;
    }

    bool ScenePicking::Raycast(Scene& scene, const glm::vec3& origin, const glm::vec3& direction, PickResult& out, bool includeIconOnly)
    {
        glm::vec3 dir = glm::normalize(direction);
        float best = std::numeric_limits<float>::max();
        bool found = false;

        auto view = scene.m_Registry.view<TransformComponent>();
        for (auto e : view)
        {
            if (!scene.IsEntityActive(e))
                continue;
            Entity entity{ e, &scene };
            const auto& tc = view.get<TransformComponent>(e);

            // Terrain: yükseklik alanına tam ışın testi
            if (entity.HasComponent<TerrainComponent>())
            {
                glm::vec3 hit;
                if (TerrainSystem::Raycast(entity.GetComponent<TerrainComponent>(), tc.WorldTransform, origin, dir, hit))
                {
                    float dist = glm::length(hit - origin);
                    if (dist < best)
                    {
                        best = dist;
                        out.Hit = entity;
                        out.Distance = dist;
                        out.Point = hit;
                        found = true;
                    }
                }
                continue;
            }

            glm::vec3 mn, mx;
            if (GetLocalBounds(entity, mn, mx))
            {
                glm::mat4 inv = glm::inverse(tc.WorldTransform);
                glm::vec3 lo = glm::vec3(inv * glm::vec4(origin, 1.0f));
                glm::vec3 ld = glm::vec3(inv * glm::vec4(dir, 0.0f));
                float t;
                if (RayAABB(lo, ld, mn, mx, t))
                {
                    glm::vec3 hitWorld = glm::vec3(tc.WorldTransform * glm::vec4(lo + ld * t, 1.0f));
                    float dist = glm::length(hitWorld - origin);
                    if (dist < best)
                    {
                        best = dist;
                        out.Hit = entity;
                        out.Distance = dist;
                        out.Point = hitWorld;
                        found = true;
                    }
                }
            }
            else if (includeIconOnly)
            {
                bool iconEntity = entity.HasComponent<DirectionalLightComponent>() || entity.HasComponent<PointLightComponent>() ||
                                  entity.HasComponent<SpotLightComponent>() || entity.HasComponent<CameraComponent>() ||
                                  entity.HasComponent<AudioSourceComponent>() || entity.HasComponent<ReflectionProbeComponent>() ||
                                  entity.HasComponent<ParticleSystemComponent>();
                if (!iconEntity)
                    continue;
                glm::vec3 center = glm::vec3(tc.WorldTransform[3]);
                float radius = std::max(0.25f, glm::length(center - origin) * 0.02f);
                float t;
                if (RaySphere(origin, dir, center, radius, t) && t < best)
                {
                    best = t;
                    out.Hit = entity;
                    out.Distance = t;
                    out.Point = origin + dir * t;
                    found = true;
                }
            }
        }
        return found;
    }
}
