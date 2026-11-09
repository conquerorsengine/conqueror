#include "Foliage.h"

#include "Core/Assets/Graph/NodeAsset.h"
#include "Core/Assets/SimpleAsset.h"
#include "Core/Logging/Log.h"
#include "Core/Serialization/PrefabSerializer.h"
#include "Core/Serialization/SceneSerializer.h"
#include "Scene/ComponentRegistry.h"
#include "Scene/Components.h"
#include "Scene/Entity.h"
#include "Scene/MultiMesh.h"
#include "Scene/Scene.h"
#include "Terrain/Terrain.h"
#include "Terrain/TerrainComponent.h"

#include <IconsFontAwesome6.h>
#include <yaml-cpp/yaml.h>

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <map>
#include <random>
#include <vector>

namespace Conqueror
{
    template<> void Scene::OnComponentAdded<FoliageVolumeComponent>(Entity, FoliageVolumeComponent&) {}

    namespace Foliage
    {
        namespace
        {
            constexpr const char* kPrefix = "[Generated] ";

            struct Surface
            {
                bool Found = false;
                float Height = 0.0f;
                float Slope = 0.0f;     // derece
                glm::vec3 Normal{ 0.0f, 1.0f, 0.0f };
                glm::vec4 Splat{ 0.0f };
                bool HasSplat = false;
            };

            Surface QuerySurface(Scene& scene, float x, float z, float fallback)
            {
                Surface best;
                best.Height = fallback;
                auto view = scene.m_Registry.view<TransformComponent, TerrainComponent>();
                for (auto e : view)
                {
                    auto& tc = view.get<TransformComponent>(e);
                    auto& terrain = view.get<TerrainComponent>(e);
                    const glm::mat4 inv = glm::inverse(tc.WorldTransform);
                    const glm::vec3 local = glm::vec3(inv * glm::vec4(x, 0.0f, z, 1.0f));
                    if (local.x < 0.0f || local.z < 0.0f || local.x > terrain.Width || local.z > terrain.Length)
                        continue;
                    const float h = TerrainSystem::GetHeightLocal(terrain, local.x, local.z);
                    const float d = std::max(terrain.Width / (float)std::max(terrain.Resolution - 1, 1), 0.05f);
                    const float hx = TerrainSystem::GetHeightLocal(terrain, std::min(local.x + d, terrain.Width), local.z);
                    const float hz = TerrainSystem::GetHeightLocal(terrain, local.x, std::min(local.z + d, terrain.Length));
                    const glm::vec3 normal = glm::normalize(glm::vec3(h - hx, d, h - hz));
                    best.Found = true;
                    best.Height = (tc.WorldTransform * glm::vec4(local.x, h, local.z, 1.0f)).y;
                    best.Slope = glm::degrees(std::acos(std::clamp(normal.y, 0.0f, 1.0f)));
                    best.Normal = normal;
                    if (!terrain.Splat.empty())
                    {
                        const int r = std::max(terrain.SplatResolution, 1);
                        const int ix = std::clamp((int)(local.x / terrain.Width * (float)r), 0, r - 1);
                        const int iz = std::clamp((int)(local.z / terrain.Length * (float)r), 0, r - 1);
                        const size_t i = (size_t)iz * (size_t)r * 4 + (size_t)ix * 4;
                        if (i + 3 < terrain.Splat.size())
                        {
                            best.Splat = glm::vec4(terrain.Splat[i], terrain.Splat[i + 1], terrain.Splat[i + 2], terrain.Splat[i + 3]) / 255.0f;
                            best.HasSplat = true;
                        }
                    }
                    break;
                }
                return best;
            }

            struct Instance
            {
                glm::vec3 Position{ 0.0f };
                float Yaw = 0.0f;
                float Scale = 1.0f;
                glm::vec4 Color{ 1.0f };
            };

            struct Batch
            {
                std::string Mesh;
                std::vector<Instance> Items;
            };

            void Emit(Scene& scene, Entity volume, std::map<std::string, Batch>& batches, const std::string& prefabOut)
            {
                (void)prefabOut;
                FoliageVolumeComponent& c = volume.GetComponent<FoliageVolumeComponent>();
                const glm::mat4 world = volume.GetComponent<TransformComponent>().WorldTransform;
                const glm::mat4 inverse = glm::inverse(world);
                for (auto& [mesh, batch] : batches)
                {
                    if (batch.Items.empty())
                        continue;
                    Entity child = scene.CreateEntity(kPrefix + std::filesystem::path(mesh).stem().string());
                    child.SetParent(volume);
                    auto& mm = child.AddComponent<MultiMeshComponent>();
                    mm.Source = MultiMeshSource::Model;
                    mm.ModelPath = mesh;
                    mm.Mode = MultiMeshMode::Manual;
                    std::string data;
                    data.reserve(batch.Items.size() * 48);
                    char line[160];
                    for (const auto& it : batch.Items)
                    {
                        const glm::vec3 p = glm::vec3(inverse * glm::vec4(it.Position, 1.0f));
                        std::snprintf(line, sizeof(line), "%g %g %g %g %g %g %g %g %g;", p.x, p.y, p.z, it.Yaw, it.Scale, it.Color.r, it.Color.g, it.Color.b, it.Color.a);
                        data += line;
                    }
                    mm.InstanceData = std::move(data);
                    mm.Count = (int)batch.Items.size();
                    c.Instances += (int)batch.Items.size();
                }
            }

            void GenerateFromSpawner(Scene& scene, Entity volume, std::map<std::string, Batch>& batches)
            {
                const auto& c = volume.GetComponent<FoliageVolumeComponent>();
                SimpleAssets::Data spawner;
                if (c.Spawner.empty() || !SimpleAssets::Load(c.Spawner, spawner))
                    return;
                const glm::mat4 world = volume.GetComponent<TransformComponent>().WorldTransform;
                const glm::vec3 center = glm::vec3(world[3]);
                std::mt19937 rng((uint32_t)(c.Seed + spawner.Int("Random Seed", 1)));
                std::uniform_real_distribution<float> U(0.0f, 1.0f);
                for (const auto& row : spawner.Table("Foliage Types"))
                {
                    SimpleAssets::Data type;
                    if (!SimpleAssets::Load(SimpleAssets::Cell(row, 0), type) || type.Str("Mesh").empty())
                        continue;
                    const float density = type.Float("Density", 10.0f) * SimpleAssets::CellFloat(row, 1, 1.0f);
                    const int count = std::min((int)(density * c.Size.x * c.Size.z / 100.0f), 200000);
                    const float scaleMin = type.Float("Scale Min", 0.8f), scaleMax = type.Float("Scale Max", 1.2f);
                    const float slopeMax = type.Float("Slope Max", 45.0f), slopeMin = type.Float("Slope Min", 0.0f);
                    auto& batch = batches[type.Str("Mesh")];
                    batch.Mesh = type.Str("Mesh");
                    for (int i = 0; i < count; ++i)
                    {
                        const float x = center.x + (U(rng) - 0.5f) * c.Size.x, z = center.z + (U(rng) - 0.5f) * c.Size.z;
                        const Surface s = QuerySurface(scene, x, z, center.y);
                        if (s.Slope > slopeMax || s.Slope < slopeMin)
                            continue;
                        Instance it;
                        it.Position = glm::vec3(x, s.Height, z);
                        it.Yaw = type.Bool("Random Yaw", true) ? U(rng) * 360.0f : 0.0f;
                        it.Scale = scaleMin + (scaleMax - scaleMin) * U(rng);
                        batch.Items.push_back(it);
                    }
                }
            }

            void GenerateGrass(Scene& scene, Entity volume, std::map<std::string, Batch>& batches)
            {
                const auto& c = volume.GetComponent<FoliageVolumeComponent>();
                SimpleAssets::Data grass;
                if (c.GrassType.empty() || !SimpleAssets::Load(c.GrassType, grass))
                    return;
                const glm::vec3 center = glm::vec3(volume.GetComponent<TransformComponent>().WorldTransform[3]);
                std::mt19937 rng((uint32_t)c.Seed + 7919u);
                std::uniform_real_distribution<float> U(0.0f, 1.0f);
                const int layer = std::clamp(grass.Int("Terrain Layer", 0), 0, 3);
                for (const auto& row : grass.Table("Varieties"))
                {
                    const std::string mesh = SimpleAssets::Cell(row, 0);
                    if (mesh.empty())
                        continue;
                    const float density = SimpleAssets::CellFloat(row, 1, 100.0f);
                    const int count = std::min((int)(density * c.Size.x * c.Size.z / 100.0f), 200000);
                    const float scaleMin = SimpleAssets::CellFloat(row, 2, 0.8f), scaleMax = SimpleAssets::CellFloat(row, 3, 1.2f);
                    auto& batch = batches[mesh];
                    batch.Mesh = mesh;
                    for (int i = 0; i < count; ++i)
                    {
                        const float x = center.x + (U(rng) - 0.5f) * c.Size.x, z = center.z + (U(rng) - 0.5f) * c.Size.z;
                        const Surface s = QuerySurface(scene, x, z, center.y);
                        // Çim yalnızca seçili katmanın boyandığı yerde
                        if (!s.Found || !s.HasSplat || s.Splat[layer] < 0.5f)
                            continue;
                        Instance it;
                        it.Position = glm::vec3(x, s.Height, z);
                        it.Yaw = U(rng) * 360.0f;
                        it.Scale = scaleMin + (scaleMax - scaleMin) * U(rng);
                        batch.Items.push_back(it);
                    }
                }
            }

            struct PcgPoint
            {
                glm::vec3 Position{ 0.0f };
                float Yaw = 0.0f, Scale = 1.0f, Density = 1.0f;
            };

            float Hash2D(float x, float y, int seed)
            {
                const float v = std::sin(x * 127.1f + y * 311.7f + (float)seed * 74.7f) * 43758.5453f;
                return v - std::floor(v);
            }

            float ValueNoise(float x, float y, int seed)
            {
                const float ix = std::floor(x), iy = std::floor(y);
                const float fx = x - ix, fy = y - iy;
                const float u = fx * fx * (3.0f - 2.0f * fx), v = fy * fy * (3.0f - 2.0f * fy);
                return glm::mix(glm::mix(Hash2D(ix, iy, seed), Hash2D(ix + 1.0f, iy, seed), u),
                                glm::mix(Hash2D(ix, iy + 1.0f, seed), Hash2D(ix + 1.0f, iy + 1.0f, seed), u), v);
            }

            std::vector<PcgPoint> EvaluateNode(Scene& scene, Entity volume, const NodeAssets::File& file, int id, std::mt19937& rng, int depth)
            {
                const auto* node = file.Find(id);
                if (!node || depth > 64)
                    return {};
                const auto& c = volume.GetComponent<FoliageVolumeComponent>();
                const glm::vec3 center = glm::vec3(volume.GetComponent<TransformComponent>().WorldTransform[3]);
                std::uniform_real_distribution<float> U(0.0f, 1.0f);
                std::vector<PcgPoint> in;
                if (node->Type != "sampler")
                    for (const auto* link : file.In(id))
                    {
                        auto part = EvaluateNode(scene, volume, file, link->From, rng, depth + 1);
                        in.insert(in.end(), part.begin(), part.end());
                    }
                std::vector<PcgPoint> out;
                if (node->Type == "sampler")
                {
                    const float spacing = std::max(0.05f, node->Float("Spacing", 2.0f));
                    const float jitter = node->Float("Jitter", 0.5f);
                    const float slopeMax = node->Float("Slope Max", 60.0f);
                    for (float z = -c.Size.z * 0.5f; z < c.Size.z * 0.5f; z += spacing)
                        for (float x = -c.Size.x * 0.5f; x < c.Size.x * 0.5f; x += spacing)
                        {
                            PcgPoint p;
                            const float wx = center.x + x + (U(rng) - 0.5f) * spacing * jitter, wz = center.z + z + (U(rng) - 0.5f) * spacing * jitter;
                            const Surface s = QuerySurface(scene, wx, wz, center.y);
                            if (s.Slope > slopeMax)
                                continue;
                            p.Position = glm::vec3(wx, s.Height, wz);
                            out.push_back(p);
                        }
                }
                else if (node->Type == "noise")
                {
                    const float scale = std::max(0.001f, node->Float("Scale", 10.0f));
                    const int seed = node->Int("Seed", 0);
                    out = in;
                    for (auto& p : out)
                        p.Density *= ValueNoise(p.Position.x / scale, p.Position.z / scale, seed);
                }
                else if (node->Type == "filter")
                {
                    const float low = node->Float("Min", 0.5f), high = node->Float("Max", 1.0f);
                    for (const auto& p : in)
                        if (p.Density >= low && p.Density <= high)
                            out.push_back(p);
                }
                else if (node->Type == "random")
                {
                    const float keep = node->Float("Keep", 0.5f);
                    for (const auto& p : in)
                        if (U(rng) <= keep)
                            out.push_back(p);
                }
                else if (node->Type == "transform")
                {
                    out = in;
                    const float scaleMin = node->Float("Scale Min", 1.0f), scaleMax = node->Float("Scale Max", 1.0f);
                    const float offset = node->Float("Height Offset", 0.0f);
                    const bool yaw = node->Bool("Random Yaw", true);
                    for (auto& p : out)
                    {
                        p.Scale *= scaleMin + (scaleMax - scaleMin) * U(rng);
                        p.Position.y += offset;
                        if (yaw)
                            p.Yaw = U(rng) * 360.0f;
                    }
                }
                else
                    out = in;
                return out;
            }

            void GenerateGraph(Scene& scene, Entity volume, std::map<std::string, Batch>& batches)
            {
                auto& c = volume.GetComponent<FoliageVolumeComponent>();
                NodeAssets::File file;
                if (c.Graph.empty() || !NodeAssets::Load(c.Graph, file))
                    return;
                std::mt19937 rng((uint32_t)c.Seed + 104729u);
                for (const auto& node : file.Nodes)
                {
                    if (node.Type != "mesh" && node.Type != "prefab")
                        continue;
                    std::vector<PcgPoint> points;
                    for (const auto* link : file.In(node.Id))
                    {
                        auto part = EvaluateNode(scene, volume, file, link->From, rng, 1);
                        points.insert(points.end(), part.begin(), part.end());
                    }
                    if (node.Type == "mesh")
                    {
                        const std::string mesh = node.Str("Mesh");
                        if (mesh.empty())
                            continue;
                        auto& batch = batches[mesh];
                        batch.Mesh = mesh;
                        for (const auto& p : points)
                        {
                            Instance it;
                            it.Position = p.Position;
                            it.Yaw = p.Yaw;
                            it.Scale = p.Scale;
                            batch.Items.push_back(it);
                        }
                    }
                    else
                    {
                        const std::string prefab = node.Str("Prefab");
                        if (prefab.empty())
                            continue;
                        const int limit = std::min<int>((int)points.size(), 2000);
                        for (int i = 0; i < limit; ++i)
                        {
                            Entity e = PrefabSerializer::Instantiate(scene, SceneSerializer::ResolveAssetPath(prefab), volume);
                            if (!e)
                                break;
                            auto& tag = e.GetComponent<TagComponent>();
                            tag.Tag = kPrefix + tag.Tag;
                            auto& tc = e.GetComponent<TransformComponent>();
                            const glm::mat4 inverse = glm::inverse(volume.GetComponent<TransformComponent>().WorldTransform);
                            tc.Position = glm::vec3(inverse * glm::vec4(points[(size_t)i].Position, 1.0f));
                            tc.Rotation.y = glm::radians(points[(size_t)i].Yaw);
                            tc.Scale = glm::vec3(points[(size_t)i].Scale);
                            ++c.Instances;
                        }
                    }
                }
            }
        }

        void Clear(Scene& scene, Entity volume)
        {
            auto children = volume.GetChildren();
            for (Entity child : children)
                if (child.GetComponent<TagComponent>().Tag.rfind(kPrefix, 0) == 0)
                    scene.DestroyEntity(child);
            volume.GetComponent<FoliageVolumeComponent>().Instances = 0;
        }

        void Generate(Scene& scene, Entity volume)
        {
            Clear(scene, volume);
            std::map<std::string, Batch> batches;
            GenerateFromSpawner(scene, volume, batches);
            GenerateGrass(scene, volume, batches);
            GenerateGraph(scene, volume, batches);
            Emit(scene, volume, batches, "");
        }

        void Update(Scene& scene)
        {
            std::vector<entt::entity> pending;
            auto view = scene.m_Registry.view<FoliageVolumeComponent>();
            for (auto e : view)
            {
                auto& c = view.get<FoliageVolumeComponent>(e);
                if (c.GenerateOnPlay && !c.PlayGenerated)
                {
                    c.PlayGenerated = true;
                    pending.push_back(e);
                }
            }
            for (auto e : pending)
                Generate(scene, Entity{ e, &scene });
        }

        void RegisterComponents()
        {
            using C = FoliageVolumeComponent;
            ComponentDef<C>("Foliage Volume", "FoliageVolumeComponent", "Rendering", ICON_FA_TREE)
                .Describe("Generates foliage inside its box from a Procedural Foliage Spawner, a Landscape Grass Type and / or a PCG Graph. Sits on the Terrain when there is one.")
                .Field("Spawner", "Foliage Spawner", &C::Spawner, FieldKind::Asset).Filter(".cqfoliagespawner")
                .Field("GrassType", "Grass Type", &C::GrassType, FieldKind::Asset).Filter(".cqgrasstype")
                .Field("Graph", "PCG Graph", &C::Graph, FieldKind::Asset).Filter(".cqpcg")
                .Field("Size", "Size", &C::Size)
                .Field("Seed", "Seed", &C::Seed)
                .Field("GenerateOnPlay", "Generate On Play", &C::GenerateOnPlay)
                .Action("Generate", [](Entity e, C&) { Generate(*e.GetScene(), e); })
                .Action("Clear", [](Entity e, C&) { Clear(*e.GetScene(), e); })
                .Field("Instances", "Instances", &C::Instances).ReadOnly();
        }
    }
}
