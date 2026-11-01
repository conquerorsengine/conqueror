#include "MultiMesh.h"
#include "Scene/Scene.h"
#include "Scene/Entity.h"
#include "Scene/Components.h"
#include "Scene/ComponentRegistry.h"
#include "Terrain/Terrain.h"
#include "Terrain/TerrainComponent.h"
#include "Renderer/Utilities/Renderer3D/Renderer3D.h"
#include "Renderer/Utilities/Renderer3D/Mesh.h"
#include "Renderer/Utilities/Renderer3D/Material.h"
#include "Renderer/Utilities/Renderer3D/ModelLoader.h"
#include "Renderer/RHI/Texture.h"
#include "Renderer/Renderer.h"
#include "Core/Serialization/SceneSerializer.h"

#include <IconsFontAwesome6.h>
#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/matrix_decompose.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <random>
#include <sstream>
#include <unordered_map>

namespace Conqueror
{
    namespace
    {
        constexpr int kMaxInstances = 1000000;

        // Örnek kümeleri (Unreal HISM): örnekler uzaysal (Morton) sırayla 64'lük kümelere ayrılır ve GPU tamponuna bu sırayla
        // yazılır. Kamera ya da gölge kademesi dışındaki kümeler çizilmez; ardışık görünür kümeler tek çağrıda birleşir.
        struct InstanceCluster
        {
            uint32_t First = 0, Count = 0;
            glm::vec3 Center{ 0.0f }, Extents{ 0.0f };   // bileşenin yerel uzayında kutu
        };
        constexpr uint32_t kClusterSize = 64;

        struct GPUBatch
        {
            GLuint Buffer = 0;
            uint64_t Version = 0;
            uint32_t Capacity = 0;
            uint32_t Count = 0;
            std::vector<InstanceCluster> Clusters;
        };
        // Sahne başına (oyun kopyası kendi tamponunu tutar) varlık -> örnek tamponu
        std::unordered_map<Scene*, std::unordered_map<uint32_t, GPUBatch>> s_Batches;
        uint64_t s_NextVersion = 0;

        uint64_t Mix(uint64_t h, uint64_t v) { return h ^ (v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2)); }
        uint64_t MixF(uint64_t h, float f)
        {
            uint32_t u;
            std::memcpy(&u, &f, sizeof(u));
            return Mix(h, u);
        }

        uint64_t GenerationSignature(const MultiMeshComponent& c, const glm::mat4& world)
        {
            uint64_t h = 1469598103934665603ull;
            h = Mix(h, (uint64_t)c.Mode);
            h = Mix(h, (uint64_t)(uint32_t)c.Count);
            h = Mix(h, (uint64_t)(uint32_t)c.Seed);
            for (int i = 0; i < 3; ++i)
            {
                h = MixF(h, c.Area[i]);
                h = MixF(h, c.GridCount[i]);
                h = MixF(h, c.Spacing[i]);
            }
            h = MixF(h, c.ScaleRange.x);
            h = MixF(h, c.ScaleRange.y);
            h = Mix(h, c.RandomYaw ? 1 : 0);
            h = MixF(h, c.ColorVariation);
            h = Mix(h, c.SnapToTerrain ? 1 : 0);
            // Terrain'e oturtmada sonuç dünya konumuna bağlıdır
            if (c.SnapToTerrain)
                for (int i = 0; i < 4; ++i)
                    for (int j = 0; j < 4; ++j)
                        h = MixF(h, world[i][j]);
            return h;
        }

        glm::mat4 Compose(const glm::vec3& position, float yawDegrees, float scale)
        {
            glm::mat4 m = glm::translate(glm::mat4(1.0f), position);
            if (yawDegrees != 0.0f)
                m = glm::rotate(m, glm::radians(yawDegrees), glm::vec3(0.0f, 1.0f, 0.0f));
            return glm::scale(m, glm::vec3(scale));
        }

        bool SnapDown(Scene& scene, const glm::vec3& worldPos, glm::vec3& outHit)
        {
            bool found = false;
            float best = 1e30f;
            auto view = scene.m_Registry.view<TransformComponent, TerrainComponent>();
            for (auto e : view)
            {
                auto [tc, terrain] = view.get<TransformComponent, TerrainComponent>(e);
                glm::vec3 hit;
                const glm::vec3 origin(worldPos.x, worldPos.y + 10000.0f, worldPos.z);
                if (TerrainSystem::Raycast(terrain, tc.WorldTransform, origin, glm::vec3(0.0f, -1.0f, 0.0f), hit, 20000.0f))
                {
                    const float d = std::abs(hit.y - worldPos.y);
                    if (d < best)
                    {
                        best = d;
                        outHit = hit;
                        found = true;
                    }
                }
            }
            return found;
        }

        void Generate(Scene& scene, MultiMeshComponent& c, const glm::mat4& world)
        {
            c.Transforms.clear();
            c.Colors.clear();
            std::mt19937 rng((uint32_t)c.Seed * 2654435761u + 977u);
            std::uniform_real_distribution<float> U(0.0f, 1.0f);
            const glm::mat4 inverseWorld = glm::inverse(world);
            auto emit = [&](glm::vec3 p) {
                const float yaw = c.RandomYaw ? U(rng) * 360.0f : 0.0f;
                const float scale = glm::mix(c.ScaleRange.x, c.ScaleRange.y, U(rng));
                glm::vec4 color(1.0f);
                const float b = U(rng), t = U(rng);
                if (c.ColorVariation > 0.0f)
                {
                    // Parlaklık + hafif sıcak/soğuk ton oynaması (çimen / yaprak doğallığı)
                    const float bright = 1.0f + (b * 2.0f - 1.0f) * c.ColorVariation;
                    const float tone = (t * 2.0f - 1.0f) * c.ColorVariation * 0.5f;
                    color = glm::vec4(glm::max(glm::vec3(bright + tone, bright, bright - tone), glm::vec3(0.0f)), 1.0f);
                }
                if (c.SnapToTerrain)
                {
                    glm::vec3 hit;
                    if (SnapDown(scene, glm::vec3(world * glm::vec4(p, 1.0f)), hit))
                        p = glm::vec3(inverseWorld * glm::vec4(hit, 1.0f));
                }
                c.Transforms.push_back(Compose(p, yaw, scale));
                c.Colors.push_back(color);
            };
            if (c.Mode == MultiMeshMode::Scatter)
            {
                const int n = std::clamp(c.Count, 0, kMaxInstances);
                c.Transforms.reserve((size_t)n);
                c.Colors.reserve((size_t)n);
                for (int i = 0; i < n; ++i)
                {
                    const float x = U(rng), y = U(rng), z = U(rng);
                    emit(glm::vec3((x - 0.5f) * c.Area.x, (y - 0.5f) * c.Area.y, (z - 0.5f) * c.Area.z));
                }
            }
            else if (c.Mode == MultiMeshMode::Grid)
            {
                const int nx = std::max(1, (int)std::lround(c.GridCount.x));
                const int ny = std::max(1, (int)std::lround(c.GridCount.y));
                const int nz = std::max(1, (int)std::lround(c.GridCount.z));
                const long long total = std::min<long long>((long long)nx * ny * nz, kMaxInstances);
                c.Transforms.reserve((size_t)total);
                c.Colors.reserve((size_t)total);
                long long made = 0;
                // x ve z merkezli, y tabandan yukarı istiflenir
                for (int iy = 0; iy < ny && made < total; ++iy)
                    for (int iz = 0; iz < nz && made < total; ++iz)
                        for (int ix = 0; ix < nx && made < total; ++ix, ++made)
                            emit(glm::vec3((ix - (nx - 1) * 0.5f) * c.Spacing.x, iy * c.Spacing.y, (iz - (nz - 1) * 0.5f) * c.Spacing.z));
            }
        }

        // Manual: "px py pz qx qy qz qw sx sy sz r g b a;..."
        void ParseInstanceData(MultiMeshComponent& c)
        {
            c.Transforms.clear();
            c.Colors.clear();
            std::stringstream all(c.InstanceData);
            std::string item;
            while (std::getline(all, item, ';'))
            {
                float v[14] = { 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1 };
                std::stringstream ss(item);
                int k = 0;
                while (k < 14 && (ss >> v[k]))
                    ++k;
                if (k < 3)
                    continue;
                glm::mat4 m = glm::translate(glm::mat4(1.0f), glm::vec3(v[0], v[1], v[2]));
                m *= glm::mat4_cast(glm::normalize(glm::quat(v[6], v[3], v[4], v[5])));
                m = glm::scale(m, glm::vec3(v[7], v[8], v[9]));
                c.Transforms.push_back(m);
                c.Colors.push_back(glm::vec4(v[10], v[11], v[12], v[13]));
            }
        }

        void WriteInstanceData(MultiMeshComponent& c)
        {
            std::string out;
            out.reserve(c.Transforms.size() * 64);
            char buf[256];
            for (size_t i = 0; i < c.Transforms.size(); ++i)
            {
                glm::vec3 scale, translation, skew;
                glm::vec4 perspective;
                glm::quat rotation;
                if (!glm::decompose(c.Transforms[i], scale, rotation, translation, skew, perspective))
                {
                    translation = glm::vec3(c.Transforms[i][3]);
                    rotation = glm::quat(1, 0, 0, 0);
                    scale = glm::vec3(1.0f);
                }
                const glm::vec4 col = i < c.Colors.size() ? c.Colors[i] : glm::vec4(1.0f);
                std::snprintf(buf, sizeof(buf), "%s%.4g %.4g %.4g %.4g %.4g %.4g %.4g %.4g %.4g %.4g %.3g %.3g %.3g %.3g", i ? ";" : "",
                              translation.x, translation.y, translation.z, rotation.x, rotation.y, rotation.z, rotation.w,
                              scale.x, scale.y, scale.z, col.r, col.g, col.b, col.a);
                out += buf;
            }
            c.InstanceData = std::move(out);
        }

        float SourceRadius(const MultiMeshComponent& c, glm::vec3& center)
        {
            center = glm::vec3(0.0f);
            if (c.Source == MultiMeshSource::Model)
            {
                float r = 0.0f;
                if (c.RuntimeModel)
                    for (const auto& m : c.RuntimeModel->Meshes)
                        if (m)
                            r = std::max(r, glm::length(m->GetBoundsCenter()) + m->GetBoundsRadius());
                return std::max(r, 0.5f);
            }
            std::shared_ptr<Mesh> mesh;
            switch (c.Primitive)
            {
                case 1: mesh = Renderer3D::GetSphereMesh(); break;
                case 2: mesh = Renderer3D::GetPlaneMesh(); break;
                case 3: mesh = Renderer3D::GetCylinderMesh(); break;
                default: mesh = Renderer3D::GetCubeMesh(); break;
            }
            if (!mesh)
                return 1.0f;
            center = mesh->GetBoundsCenter();
            return std::max(mesh->GetBoundsRadius(), 0.01f);
        }

        void ComputeBounds(MultiMeshComponent& c)
        {
            glm::vec3 center;
            const float radius = SourceRadius(c, center);
            glm::vec3 mn(1e30f), mx(-1e30f);
            for (const glm::mat4& m : c.Transforms)
            {
                const float s = std::max({ glm::length(glm::vec3(m[0])), glm::length(glm::vec3(m[1])), glm::length(glm::vec3(m[2])) });
                const glm::vec3 p = glm::vec3(m * glm::vec4(center, 1.0f));
                mn = glm::min(mn, p - glm::vec3(radius * s));
                mx = glm::max(mx, p + glm::vec3(radius * s));
            }
            if (c.Transforms.empty())
                mn = mx = glm::vec3(0.0f);
            c.BoundsMin = mn;
            c.BoundsMax = mx;
        }

        // 10 bitlik tamsayının bitlerini 3 aralıkla yay (Morton kodu)
        uint32_t Spread10(uint32_t v)
        {
            v &= 1023u;
            v = (v | (v << 16)) & 0x030000FFu;
            v = (v | (v << 8)) & 0x0300F00Fu;
            v = (v | (v << 4)) & 0x030C30C3u;
            v = (v | (v << 2)) & 0x09249249u;
            return v;
        }

        void Upload(GPUBatch& batch, const MultiMeshComponent& c)
        {
            const uint32_t count = (uint32_t)c.Transforms.size();
            // Örneklerin yerel sınır küreleri ve uzaysal sıra
            glm::vec3 srcCenter;
            const float srcRadius = SourceRadius(c, srcCenter);
            static thread_local std::vector<glm::vec4> s_Spheres;      // xyz merkez, w yarıçap
            static thread_local std::vector<std::pair<uint32_t, uint32_t>> s_Order;   // Morton kodu, örnek
            s_Spheres.resize(count);
            s_Order.resize(count);
            const glm::vec3 extent = glm::max(c.BoundsMax - c.BoundsMin, glm::vec3(1e-6f));
            for (uint32_t i = 0; i < count; ++i)
            {
                const glm::mat4& m = c.Transforms[i];
                const float scale = std::max({ glm::length(glm::vec3(m[0])), glm::length(glm::vec3(m[1])), glm::length(glm::vec3(m[2])) });
                const glm::vec3 p = glm::vec3(m * glm::vec4(srcCenter, 1.0f));
                s_Spheres[i] = glm::vec4(p, srcRadius * scale);
                const glm::vec3 q = glm::clamp((p - c.BoundsMin) / extent, glm::vec3(0.0f), glm::vec3(1.0f)) * 1023.0f;
                s_Order[i] = { Spread10((uint32_t)q.x) | (Spread10((uint32_t)q.y) << 1) | (Spread10((uint32_t)q.z) << 2), i };
            }
            std::sort(s_Order.begin(), s_Order.end());

            static thread_local std::vector<float> s_Data;
            s_Data.resize((size_t)count * 20);
            for (uint32_t k = 0; k < count; ++k)
            {
                const uint32_t i = s_Order[k].second;
                std::memcpy(&s_Data[(size_t)k * 20], &c.Transforms[i][0][0], sizeof(float) * 16);
                const glm::vec4 col = i < c.Colors.size() ? c.Colors[i] : glm::vec4(1.0f);
                std::memcpy(&s_Data[(size_t)k * 20 + 16], &col[0], sizeof(float) * 4);
            }
            batch.Clusters.clear();
            for (uint32_t first = 0; first < count; first += kClusterSize)
            {
                const uint32_t n = std::min(kClusterSize, count - first);
                glm::vec3 mn(std::numeric_limits<float>::max()), mx(-std::numeric_limits<float>::max());
                for (uint32_t k = first; k < first + n; ++k)
                {
                    const glm::vec4& sphere = s_Spheres[s_Order[k].second];
                    mn = glm::min(mn, glm::vec3(sphere) - glm::vec3(sphere.w));
                    mx = glm::max(mx, glm::vec3(sphere) + glm::vec3(sphere.w));
                }
                batch.Clusters.push_back({ first, n, (mn + mx) * 0.5f, (mx - mn) * 0.5f });
            }
            if (!batch.Buffer)
                glGenBuffers(1, &batch.Buffer);
            glBindBuffer(GL_ARRAY_BUFFER, batch.Buffer);
            const GLsizeiptr bytes = (GLsizeiptr)std::max<size_t>(s_Data.size() * sizeof(float), 80);
            if (count > batch.Capacity || !batch.Capacity)
            {
                glBufferData(GL_ARRAY_BUFFER, bytes, s_Data.empty() ? nullptr : s_Data.data(), GL_DYNAMIC_DRAW);
                batch.Capacity = std::max<uint32_t>(count, 1);
            }
            else if (!s_Data.empty())
                glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)(s_Data.size() * sizeof(float)), s_Data.data());
            glBindBuffer(GL_ARRAY_BUFFER, 0);
            batch.Count = count;
            batch.Version = c.Version;
        }

        void ReleaseBatch(GPUBatch& batch)
        {
            if (batch.Buffer)
                glDeleteBuffers(1, &batch.Buffer);
            batch = {};
        }

        void ResolveResources(MultiMeshComponent& c)
        {
            if (c.Source == MultiMeshSource::Model)
            {
                if (c.LoadedModel != c.ModelPath)
                {
                    c.LoadedModel = c.ModelPath;
                    c.RuntimeModel = c.ModelPath.empty() ? nullptr : ModelLoader::Load(SceneSerializer::ResolveAssetPath(c.ModelPath));
                }
            }
            else if (c.LoadedTexture != c.Texture)
            {
                c.LoadedTexture = c.Texture;
                c.RuntimeTexture = c.Texture.empty() ? nullptr : Texture2D::Create(SceneSerializer::ResolveAssetPath(c.Texture));
            }
        }

        std::shared_ptr<Mesh> PrimitiveMesh(int primitive)
        {
            switch (primitive)
            {
                case 1: return Renderer3D::GetSphereMesh();
                case 2: return Renderer3D::GetPlaneMesh();
                case 3: return Renderer3D::GetCylinderMesh();
                default: return Renderer3D::GetCubeMesh();
            }
        }

        bool BoundsVisible(const glm::mat4& world, const glm::vec3& mn, const glm::vec3& mx, const glm::vec4 planes[6])
        {
            // Yerel AABB'nin dünya AABB'si (8 köşe), sonra düzlem testi
            glm::vec3 wmn(1e30f), wmx(-1e30f);
            for (int i = 0; i < 8; ++i)
            {
                const glm::vec3 corner((i & 1) ? mx.x : mn.x, (i & 2) ? mx.y : mn.y, (i & 4) ? mx.z : mn.z);
                const glm::vec3 w = glm::vec3(world * glm::vec4(corner, 1.0f));
                wmn = glm::min(wmn, w);
                wmx = glm::max(wmx, w);
            }
            for (int p = 0; p < 6; ++p)
            {
                const glm::vec3 n(planes[p]);
                const glm::vec3 positive(n.x >= 0 ? wmx.x : wmn.x, n.y >= 0 ? wmx.y : wmn.y, n.z >= 0 ? wmx.z : wmn.z);
                if (glm::dot(n, positive) + planes[p].w < 0.0f)
                    return false;
            }
            return true;
        }

        float DistanceToBounds(const glm::mat4& world, const glm::vec3& mn, const glm::vec3& mx, const glm::vec3& point)
        {
            const glm::vec3 local = glm::vec3(glm::inverse(world) * glm::vec4(point, 1.0f));
            const glm::vec3 closest = glm::clamp(local, mn, mx);
            return glm::length(glm::vec3(world * glm::vec4(closest, 1.0f)) - point);
        }

        // Düzlemlerin içindeki kümeler ardışık aralıklara birleştirilir (ilk örnek, sayı); düzlem yoksa tüm tampon
        void VisibleRanges(const GPUBatch& batch, const glm::mat4& world, const glm::vec4* planes,
                           std::vector<std::pair<uint32_t, uint32_t>>& out)
        {
            out.clear();
            if (!planes || batch.Clusters.size() <= 1)
            {
                out.push_back({ 0u, batch.Count });
                return;
            }
            for (const auto& cl : batch.Clusters)
            {
                glm::vec3 center, extents;
                Renderer3D::TransformBox(world, cl.Center, cl.Extents, center, extents);
                if (!Renderer3D::BoxInFrustum(planes, center, extents))
                    continue;
                if (!out.empty() && out.back().first + out.back().second == cl.First)
                    out.back().second += cl.Count;
                else
                    out.push_back({ cl.First, cl.Count });
            }
        }
    }

    namespace MultiMesh
    {
        void MarkChanged(MultiMeshComponent& c)
        {
            c.Version = ++s_NextVersion;
            c.Instances = (int)c.Transforms.size();
            ComputeBounds(c);
        }

        static void EnsureManual(MultiMeshComponent& c)
        {
            if (c.Mode != MultiMeshMode::Manual)
            {
                c.Mode = MultiMeshMode::Manual;
                c.Signature = 0;
            }
            c.ManualDirty = true;
        }

        int AddInstance(MultiMeshComponent& c, const glm::mat4& local, const glm::vec4& color)
        {
            if ((int)c.Transforms.size() >= kMaxInstances)
                return -1;
            EnsureManual(c);
            c.Colors.resize(c.Transforms.size(), glm::vec4(1.0f));
            c.Transforms.push_back(local);
            c.Colors.push_back(color);
            MarkChanged(c);
            return (int)c.Transforms.size() - 1;
        }

        bool SetInstance(MultiMeshComponent& c, int index, const glm::mat4& local)
        {
            if (index < 0 || index >= (int)c.Transforms.size())
                return false;
            EnsureManual(c);
            c.Transforms[(size_t)index] = local;
            MarkChanged(c);
            return true;
        }

        bool SetInstanceColor(MultiMeshComponent& c, int index, const glm::vec4& color)
        {
            if (index < 0 || index >= (int)c.Transforms.size())
                return false;
            EnsureManual(c);
            c.Colors.resize(c.Transforms.size(), glm::vec4(1.0f));
            c.Colors[(size_t)index] = color;
            MarkChanged(c);
            return true;
        }

        void SetCount(MultiMeshComponent& c, int count)
        {
            count = std::clamp(count, 0, kMaxInstances);
            EnsureManual(c);
            c.Transforms.resize((size_t)count, glm::mat4(1.0f));
            c.Colors.resize((size_t)count, glm::vec4(1.0f));
            MarkChanged(c);
        }

        void Clear(MultiMeshComponent& c)
        {
            SetCount(c, 0);
        }

        void Update(Scene& scene)
        {
            auto& batches = s_Batches[&scene];
            auto view = scene.m_Registry.view<TransformComponent, MultiMeshComponent>();
            for (auto e : view)
            {
                auto [tc, c] = view.get<TransformComponent, MultiMeshComponent>(e);
                ResolveResources(c);
                if (c.Mode == MultiMeshMode::Manual)
                {
                    // Kayıttan yüklenen Manual veri bir kez çözülür; betik değişiklikleri kayda geri yazılır (düzenleyici)
                    if (c.Version == 0)
                    {
                        ParseInstanceData(c);
                        MarkChanged(c);
                    }
                    if (c.ManualDirty && !scene.IsRuntimeScene())
                        WriteInstanceData(c);
                    c.ManualDirty = false;
                }
                else
                {
                    const uint64_t sig = GenerationSignature(c, tc.WorldTransform);
                    if (sig != c.Signature || c.Version == 0)
                    {
                        Generate(scene, c, tc.WorldTransform);
                        c.Signature = sig;
                        MarkChanged(c);
                    }
                }
                GPUBatch& batch = batches[(uint32_t)e];
                if (batch.Version != c.Version)
                    Upload(batch, c);
            }
            // Silinen varlıkların tamponları
            for (auto it = batches.begin(); it != batches.end();)
            {
                const auto e = (entt::entity)it->first;
                if (!scene.m_Registry.valid(e) || !scene.m_Registry.all_of<MultiMeshComponent>(e))
                {
                    ReleaseBatch(it->second);
                    it = batches.erase(it);
                }
                else
                    ++it;
            }
        }

        void Render(Scene& scene)
        {
            auto sceneIt = s_Batches.find(&scene);
            if (sceneIt == s_Batches.end())
                return;
            const RenderCamera& cam = Renderer3D::GetCamera();
            glm::vec4 planes[6];
            Renderer3D::ExtractFrustum(cam.Projection * cam.View, planes);
            auto view = scene.m_Registry.view<TransformComponent, MultiMeshComponent>();
            for (auto e : view)
            {
                auto [tc, c] = view.get<TransformComponent, MultiMeshComponent>(e);
                c.DrawCalls = 0;
                if (!scene.IsEntityActive(e) || c.Transforms.empty())
                    continue;
                auto batchIt = sceneIt->second.find((uint32_t)e);
                if (batchIt == sceneIt->second.end() || !batchIt->second.Buffer || batchIt->second.Version != c.Version)
                    continue;
                const GPUBatch& batch = batchIt->second;
                const glm::mat4& world = tc.WorldTransform;
                if (!BoundsVisible(world, c.BoundsMin, c.BoundsMax, planes))
                    continue;
                if (c.CullDistance > 0.0f && DistanceToBounds(world, c.BoundsMin, c.BoundsMax, cam.Position) > c.CullDistance)
                    continue;

                // Görünür örnek kümeleri (Shader Graph malzemesi örnek başına çizilir: orada her örnek ayrıca kırpılır)
                static thread_local std::vector<std::pair<uint32_t, uint32_t>> s_Ranges;
                VisibleRanges(batch, world, Renderer3D::GetFrustumCulling() ? planes : nullptr, s_Ranges);
                {
                    uint32_t visible = 0;
                    for (const auto& r : s_Ranges)
                        visible += r.second;
                    Renderer::GetStats().CulledObjects += batch.Count - std::min(visible, batch.Count);
                }
                auto drawRanges = [&](const std::shared_ptr<Mesh>& mesh, const std::shared_ptr<Material>& material, bool doubleSided) {
                    if (material && material->MaterialShader && material->MaterialShader->IsValid())
                    {
                        Renderer3D::DrawMeshInstanced(mesh, world, batch.Buffer, batch.Count, material, &c.Transforms, doubleSided);
                        c.DrawCalls++;
                        return;
                    }
                    for (const auto& [first, count] : s_Ranges)
                    {
                        Renderer3D::DrawMeshInstanced(mesh, world, batch.Buffer, count, material, &c.Transforms, doubleSided, first);
                        c.DrawCalls++;
                    }
                };

                Renderer3D::SetReceiveShadows(c.ReceiveShadows);
                if (c.Source == MultiMeshSource::Model)
                {
                    if (!c.RuntimeModel)
                        continue;
                    const auto& model = *c.RuntimeModel;
                    for (size_t i = 0; i < model.Meshes.size(); ++i)
                    {
                        auto material = i < model.Materials.size() ? model.Materials[i] : nullptr;
                        drawRanges(model.Meshes[i], material, false);
                    }
                }
                else
                {
                    if (!c.RuntimeMaterial)
                        c.RuntimeMaterial = std::make_shared<Material>();
                    Material& m = *c.RuntimeMaterial;
                    m.Albedo = glm::vec3(c.Color);
                    m.Metallic = c.Metallic;
                    m.Roughness = c.Roughness;
                    m.AlbedoMap = c.RuntimeTexture;
                    m.Opacity = c.Color.a;
                    m.Rendering = c.Color.a < 0.999f ? Material::RenderMode::Transparent : Material::RenderMode::Opaque;
                    drawRanges(PrimitiveMesh(c.Primitive), c.RuntimeMaterial, c.Primitive == 2);
                }
            }
            Renderer3D::SetReceiveShadows(true);
        }

        void ForEachShadowBatch(Scene& scene, const glm::vec4* planes,
                                const std::function<void(const Mesh&, const glm::mat4&, uint32_t, uint32_t, uint32_t, bool)>& fn)
        {
            auto sceneIt = s_Batches.find(&scene);
            if (sceneIt == s_Batches.end())
                return;
            auto view = scene.m_Registry.view<TransformComponent, MultiMeshComponent>();
            for (auto e : view)
            {
                auto [tc, c] = view.get<TransformComponent, MultiMeshComponent>(e);
                if (!c.CastShadows || c.Transforms.empty() || !scene.IsEntityActive(e) || c.Color.a < 0.999f)
                    continue;
                auto batchIt = sceneIt->second.find((uint32_t)e);
                if (batchIt == sceneIt->second.end() || !batchIt->second.Buffer || batchIt->second.Version != c.Version)
                    continue;
                const GPUBatch& batch = batchIt->second;
                // Kademenin ışık hacmindeki örnek kümeleri
                static thread_local std::vector<std::pair<uint32_t, uint32_t>> s_Ranges;
                VisibleRanges(batch, tc.WorldTransform, planes, s_Ranges);
                for (const auto& [first, count] : s_Ranges)
                {
                    if (c.Source == MultiMeshSource::Model)
                    {
                        if (c.RuntimeModel)
                            for (const auto& mesh : c.RuntimeModel->Meshes)
                                if (mesh && mesh->GetVertexArray())
                                    fn(*mesh, tc.WorldTransform, batch.Buffer, first, count, false);
                    }
                    else if (auto mesh = PrimitiveMesh(c.Primitive); mesh && mesh->GetVertexArray())
                        fn(*mesh, tc.WorldTransform, batch.Buffer, first, count, c.Primitive == 2);
                }
            }
        }

        void ForgetScene(Scene* scene)
        {
            auto it = s_Batches.find(scene);
            if (it == s_Batches.end())
                return;
            for (auto& [id, batch] : it->second)
                ReleaseBatch(batch);
            s_Batches.erase(it);
        }

        void ClearCache()
        {
            for (auto& [scene, batches] : s_Batches)
                for (auto& [id, batch] : batches)
                    ReleaseBatch(batch);
            s_Batches.clear();
        }
    }

    void RegisterMultiMeshComponents()
    {
        using C = MultiMeshComponent;
        ComponentDef<C>("Multi Mesh", "MultiMeshComponent", "Rendering", ICON_FA_CUBES_STACKED)
            .Describe("Godot-style MultiMesh: thousands of copies of one mesh (grass, trees, rocks, crowds) drawn with GPU instancing "
                      "in a single draw call. Scatter them in an area (optionally onto terrain), lay them out on a grid, or place them "
                      "one by one from scripts (MultiMeshAdd / MultiMeshSetTransform).")
            .Field("Source", "Source", &C::Source).Options({ "Primitive", "Model" })
            .Field("Primitive", "Primitive", &C::Primitive, FieldKind::Enum).Options({ "Cube", "Sphere", "Plane", "Cylinder" })
                .VisibleIf([](const C& c) { return c.Source == MultiMeshSource::Primitive; })
            .Field("ModelPath", "Model", &C::ModelPath, FieldKind::Asset).Filter(".obj;.fbx;.gltf;.glb;.dae;.3ds;.blend")
                .VisibleIf([](const C& c) { return c.Source == MultiMeshSource::Model; })
            .Field("Color", "Color", &C::Color, FieldKind::Color4).VisibleIf([](const C& c) { return c.Source == MultiMeshSource::Primitive; })
            .Field("Metallic", "Metallic", &C::Metallic).Range(0.0f, 1.0f, 0.01f).VisibleIf([](const C& c) { return c.Source == MultiMeshSource::Primitive; })
            .Field("Roughness", "Roughness", &C::Roughness).Range(0.0f, 1.0f, 0.01f).VisibleIf([](const C& c) { return c.Source == MultiMeshSource::Primitive; })
            .Field("Texture", "Texture", &C::Texture, FieldKind::Asset).Filter(".png;.jpg;.jpeg;.tga;.bmp")
                .VisibleIf([](const C& c) { return c.Source == MultiMeshSource::Primitive; })
            .Field("Mode", "Mode", &C::Mode).Options({ "Scatter", "Grid", "Manual" }).Tooltip("Manual: instances are placed from scripts (MultiMeshAdd / MultiMeshSetTransform) and saved with the scene").Header("Placement")
            .Field("Count", "Count", &C::Count).Range(0.0f, (float)kMaxInstances, 1.0f).VisibleIf([](const C& c) { return c.Mode == MultiMeshMode::Scatter; })
            .Field("Area", "Area", &C::Area).Tooltip("Local box the instances are scattered in (centered)")
                .VisibleIf([](const C& c) { return c.Mode == MultiMeshMode::Scatter; })
            .Field("GridCount", "Grid Count", &C::GridCount).VisibleIf([](const C& c) { return c.Mode == MultiMeshMode::Grid; })
            .Field("Spacing", "Spacing", &C::Spacing).VisibleIf([](const C& c) { return c.Mode == MultiMeshMode::Grid; })
            .Field("Seed", "Seed", &C::Seed).VisibleIf([](const C& c) { return c.Mode != MultiMeshMode::Manual; })
            .Field("ScaleRange", "Scale Range", &C::ScaleRange).VisibleIf([](const C& c) { return c.Mode != MultiMeshMode::Manual; })
            .Field("RandomYaw", "Random Rotation", &C::RandomYaw).VisibleIf([](const C& c) { return c.Mode != MultiMeshMode::Manual; })
            .Field("ColorVariation", "Color Variation", &C::ColorVariation).Range(0.0f, 1.0f, 0.01f)
                .VisibleIf([](const C& c) { return c.Mode != MultiMeshMode::Manual; })
            .Field("SnapToTerrain", "Snap To Terrain", &C::SnapToTerrain).Tooltip("Drop every instance onto the terrain surface below/above it")
                .VisibleIf([](const C& c) { return c.Mode != MultiMeshMode::Manual; })
            .Field("InstanceData", "Instance Data", &C::InstanceData).VisibleIf([](const C&) { return false; })
            .Field("CastShadows", "Cast Shadows", &C::CastShadows).Header("Rendering")
            .Field("ReceiveShadows", "Receive Shadows", &C::ReceiveShadows)
            .Field("CullDistance", "Cull Distance", &C::CullDistance).Range(0.0f, 100000.0f, 1.0f).Tooltip("Hide the whole batch beyond this camera distance (0 = never)")
            .Field("Instances", "Instances", &C::Instances).ReadOnly()
            .Field("DrawCalls", "Draw Calls", &C::DrawCalls).ReadOnly();
    }
}
