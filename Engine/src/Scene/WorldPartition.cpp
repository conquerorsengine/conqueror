#include "WorldPartition.h"
#include "Scene/Scene.h"
#include "Scene/Entity.h"
#include "Scene/Components.h"
#include "Scene/ComponentRegistry.h"
#include "Scene/GameFramework.h"
#include "Scene/XRInteraction.h"
#include "Core/Debug/DebugDraw.h"
#include "Core/Logging/Log.h"

#include <IconsFontAwesome6.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <unordered_map>
#include <vector>

namespace Conqueror
{
    namespace
    {
        struct CellKey
        {
            int X = 0, Z = 0;
            bool operator<(const CellKey& o) const { return X != o.X ? X < o.X : Z < o.Z; }
        };

        struct Cell
        {
            std::vector<std::pair<entt::entity, bool>> Actors;   // varlık, oynatma başındaki etkinlik
            bool Loaded = true;
        };

        struct State
        {
            entt::entity Settings = entt::null;
            std::map<CellKey, Cell> Cells;
            float Timer = 0.0f;
        };

        std::unordered_map<const Scene*, State>& States()
        {
            static auto& m = *new std::unordered_map<const Scene*, State>();
            return m;
        }

        // Uzamsal yüklenmeyen nesneler: kamera, güneş, arayüz, oyun kuralları, oyuncu kaynakları
        bool AlwaysLoaded(Scene& scene, entt::entity e)
        {
            auto& r = scene.m_Registry;
            if (const auto* al = r.try_get<AlwaysLoadedComponent>(e); al && al->Enabled)
                return true;
            return r.any_of<WorldPartitionComponent, WorldPartitionStreamingSourceComponent, CameraComponent,
                            DirectionalLightComponent, CanvasComponent, GameModeComponent, PlayerStartComponent, XROriginComponent>(e);
        }
    }

    namespace WorldPartition
    {
        void Forget(Scene& scene) { States().erase(&scene); }

        void BeginPlay(Scene& scene)
        {
            auto& reg = scene.m_Registry;
            Forget(scene);
            entt::entity settings = entt::null;
            for (auto e : reg.view<WorldPartitionComponent>())
                if (scene.IsEntityActive(e) && reg.get<WorldPartitionComponent>(e).EnableStreaming) { settings = e; break; }
            if (settings == entt::null)
                return;
            auto& wp = reg.get<WorldPartitionComponent>(settings);
            State& st = States()[&scene];
            st.Settings = settings;
            const float cell = std::max(wp.CellSize, 1.0f);
            int actors = 0;
            scene.UpdateWorldTransforms();
            for (auto e : reg.view<TransformComponent>())
            {
                // Kök nesneler hücrelere (çocuklar ebeveyniyle birlikte gelir)
                if (scene.GetEntityParent(Entity{ e, &scene }) || AlwaysLoaded(scene, e))
                    continue;
                const glm::vec3 p = glm::vec3(reg.get<TransformComponent>(e).WorldTransform[3]);
                const CellKey key{ (int)std::floor(p.x / cell), (int)std::floor(p.z / cell) };
                const bool active = !reg.all_of<ActiveComponent>(e) || reg.get<ActiveComponent>(e).Active;
                st.Cells[key].Actors.push_back({ e, active });
                ++actors;
            }
            wp.Cells = (int)st.Cells.size();
            wp.StreamedActors = actors;
            st.Timer = 1e9f;   // ilk karede hemen değerlendir
            CQ_CORE_INFO("[World Partition] {0} actors in {1} cells ({2:.0f} m)", actors, st.Cells.size(), cell);
        }

        void Update(Scene& scene, float dt)
        {
            auto it = States().find(&scene);
            if (it == States().end())
                return;
            State& st = it->second;
            auto& reg = scene.m_Registry;
            if (!reg.valid(st.Settings) || !reg.all_of<WorldPartitionComponent>(st.Settings))
                return;
            auto& wp = reg.get<WorldPartitionComponent>(st.Settings);
            st.Timer += dt;
            if (st.Timer < std::max(wp.UpdateInterval, 0.0f))
                return;
            st.Timer = 0.0f;
            // Akış kaynakları (yoksa ana kamera)
            std::vector<glm::vec2> sources;
            for (auto e : reg.view<WorldPartitionStreamingSourceComponent>())
                if (scene.IsEntityActive(e) && reg.get<WorldPartitionStreamingSourceComponent>(e).Enabled)
                {
                    const glm::vec3 p = glm::vec3(scene.ComputeWorldMatrix(e)[3]);
                    sources.push_back({ p.x, p.z });
                }
            if (sources.empty())
                if (Entity cam = scene.GetPrimaryCameraEntity())
                {
                    const glm::vec3 p = glm::vec3(cam.GetComponent<TransformComponent>().WorldTransform[3]);
                    sources.push_back({ p.x, p.z });
                }
            const float cell = std::max(wp.CellSize, 1.0f), range = std::max(wp.LoadingRange, 0.0f);
            int loaded = 0;
            for (auto& [key, c] : st.Cells)
            {
                const glm::vec2 mn((float)key.X * cell, (float)key.Z * cell), mx = mn + glm::vec2(cell);
                bool want = sources.empty();
                for (const auto& s : sources)
                {
                    const glm::vec2 q = glm::clamp(s, mn, mx);
                    if (glm::distance(q, s) <= range) { want = true; break; }
                }
                if (want != c.Loaded)
                {
                    c.Loaded = want;
                    for (auto& [e, originallyActive] : c.Actors)
                    {
                        if (!reg.valid(e))
                            continue;
                        auto& ac = reg.all_of<ActiveComponent>(e) ? reg.get<ActiveComponent>(e) : reg.emplace<ActiveComponent>(e);
                        ac.Active = want && originallyActive;
                    }
                }
                loaded += c.Loaded ? 1 : 0;
            }
            wp.LoadedCells = loaded;
        }

        void DrawGizmo(Scene& scene, uint32_t entity)
        {
            const entt::entity e = (entt::entity)entity;
            auto& reg = scene.m_Registry;
            if (!reg.valid(e) || !reg.all_of<WorldPartitionComponent>(e))
                return;
            const auto& wp = reg.get<WorldPartitionComponent>(e);
            const float cell = std::max(wp.CellSize, 1.0f);
            // Kök nesnelerin kapsadığı alanın ızgarası
            glm::vec2 mn(1e30f), mx(-1e30f);
            for (auto h : reg.view<TransformComponent>())
            {
                if (scene.GetEntityParent(Entity{ h, &scene }) || AlwaysLoaded(scene, h))
                    continue;
                const glm::vec3 p = glm::vec3(reg.get<TransformComponent>(h).WorldTransform[3]);
                mn = glm::min(mn, glm::vec2(p.x, p.z));
                mx = glm::max(mx, glm::vec2(p.x, p.z));
            }
            if (mn.x > mx.x)
                return;
            const int x0 = (int)std::floor(mn.x / cell), x1 = (int)std::floor(mx.x / cell) + 1;
            const int z0 = (int)std::floor(mn.y / cell), z1 = (int)std::floor(mx.y / cell) + 1;
            if ((x1 - x0) * (z1 - z0) > 4096)
                return;
            const float y = reg.get<TransformComponent>(e).WorldTransform[3][1];
            const glm::vec4 col(0.4f, 0.75f, 1.0f, 0.8f);
            for (int x = x0; x <= x1; ++x)
                DebugDraw::Line({ x * cell, y, z0 * cell }, { x * cell, y, z1 * cell }, col, 0.0f, false);
            for (int z = z0; z <= z1; ++z)
                DebugDraw::Line({ x0 * cell, y, z * cell }, { x1 * cell, y, z * cell }, col, 0.0f, false);
        }
    }

    template<> void Scene::OnComponentAdded<WorldPartitionComponent>(Entity, WorldPartitionComponent&) {}
    template<> void Scene::OnComponentAdded<WorldPartitionStreamingSourceComponent>(Entity, WorldPartitionStreamingSourceComponent&) {}
    template<> void Scene::OnComponentAdded<AlwaysLoadedComponent>(Entity, AlwaysLoadedComponent&) {}

    void RegisterWorldPartitionComponents()
    {
        const glm::vec4 kColor{ 0.4f, 0.75f, 1.0f, 1.0f };
        {
            using C = WorldPartitionComponent;
            ComponentDef<C>("World Partition", "WorldPartitionComponent", "World", ICON_FA_TABLE_CELLS).Color(kColor)
                .Describe("Splits the world into a grid (Unreal World Partition): in play, cells within Loading Range of a Streaming Source "
                          "(or the main camera) are loaded and the rest are unloaded, so big open worlds stay in one scene.")
                .Field("EnableStreaming", "Enable Streaming", &C::EnableStreaming)
                .Field("CellSize", "Cell Size", &C::CellSize).Range(1.0f, 100000.0f, 1.0f)
                .Field("LoadingRange", "Loading Range", &C::LoadingRange).Range(0.0f, 100000.0f, 1.0f)
                .Field("UpdateInterval", "Update Interval", &C::UpdateInterval).Range(0.0f, 10.0f, 0.01f)
                .Field("Cells", "Cells", &C::Cells).ReadOnly().Header("Runtime")
                .Field("LoadedCells", "Loaded Cells", &C::LoadedCells).ReadOnly()
                .Field("StreamedActors", "Streamed Actors", &C::StreamedActors).ReadOnly();
        }
        {
            using C = WorldPartitionStreamingSourceComponent;
            ComponentDef<C>("World Partition Streaming Source", "WorldPartitionStreamingSourceComponent", "World", ICON_FA_TOWER_BROADCAST).Color(kColor)
                .Describe("World Partition loads the cells around this object (the player, a vehicle). Without any, the main camera is used.")
                .Field("Enabled", "Enabled", &C::Enabled);
        }
        ComponentDef<AlwaysLoadedComponent>("Always Loaded", "AlwaysLoadedComponent", "World", ICON_FA_THUMBTACK).Color(kColor)
            .Describe("World Partition never unloads this object (Unreal: Is Spatially Loaded off).")
            .Field("Enabled", "Enabled", &AlwaysLoadedComponent::Enabled);
    }
}
