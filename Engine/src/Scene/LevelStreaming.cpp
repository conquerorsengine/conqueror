#include "LevelStreaming.h"
#include "Scene/Scene.h"
#include "Scene/Entity.h"
#include "Scene/Components.h"
#include "Scene/ComponentRegistry.h"
#include "Core/Serialization/SceneSerializer.h"
#include "Core/Serialization/PrefabSerializer.h"
#include "Core/Logging/Log.h"
#include "Scripting/ConquerorScript/CQSValue.h"

#include <IconsFontAwesome6.h>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <unordered_map>

namespace Conqueror
{
    namespace fs = std::filesystem;

    namespace
    {
        struct LevelTemplate
        {
            std::shared_ptr<Scene> Source;
            fs::file_time_type Time;
        };

        std::unordered_map<std::string, LevelTemplate>& Cache()
        {
            static std::unordered_map<std::string, LevelTemplate> cache;
            return cache;
        }

        std::string LevelNameOf(const std::string& path) { return fs::path(path).stem().string(); }

        Scene* LoadTemplate(const fs::path& file)
        {
            std::error_code ec;
            auto time = fs::last_write_time(file, ec);
            if (ec)
                return nullptr;
            const std::string key = fs::weakly_canonical(file, ec).string();
            auto& entry = Cache()[key];
            if (entry.Source && entry.Time == time)
                return entry.Source.get();
            auto scene = std::make_shared<Scene>();
            SceneSerializer serializer(scene);
            if (!serializer.Deserialize(file))
                return nullptr;
            entry.Source = scene;
            entry.Time = time;
            return entry.Source.get();
        }

        Entity FindLevelRoot(Scene& scene, const std::string& nameOrPath)
        {
            const std::string name = LevelNameOf(nameOrPath);
            auto view = scene.m_Registry.view<StreamedLevelComponent>();
            for (auto h : view)
            {
                auto& l = view.get<StreamedLevelComponent>(h);
                if (l.Source == nameOrPath || l.LevelName == nameOrPath || l.LevelName == name)
                    return Entity{ h, &scene };
            }
            return {};
        }

        bool IsInside(Scene& scene, entt::entity volume, const LevelStreamingVolumeComponent& v, const glm::vec3& p)
        {
            const glm::mat4& w = scene.m_Registry.get<TransformComponent>(volume).WorldTransform;
            glm::vec3 local = glm::vec3(glm::inverse(w) * glm::vec4(p, 1.0f));
            glm::vec3 half = v.Size * 0.5f;
            return std::abs(local.x) <= half.x && std::abs(local.y) <= half.y && std::abs(local.z) <= half.z;
        }
    }

    namespace LevelStreaming
    {
        uint32_t LoadAdditive(Scene& scene, const std::string& path, const glm::vec3& offset)
        {
            if (Entity existing = FindLevelRoot(scene, path))
                return (uint32_t)(entt::entity)existing;
            fs::path file = PrefabSerializer::Resolve(path);
            Scene* source = LoadTemplate(file);
            if (!source)
            {
                CQ_CORE_ERROR("[LevelStreaming] Cannot load {0}", path);
                return 0xFFFFFFFFu;
            }
            Entity root = scene.CreateEntity("[Level] " + LevelNameOf(path));
            auto& streamed = root.AddComponent<StreamedLevelComponent>();
            streamed.Source = PrefabSerializer::ToProjectPath(file);
            streamed.LevelName = LevelNameOf(path);
            root.GetComponent<TransformComponent>().Position = offset;
            // Kaynak sahnenin kök varlıkları (ebeveynsiz) yeni kökün altına kopyalanır
            std::vector<entt::entity> roots;
            source->m_Registry.view<IDComponent>().each([&](auto e, auto&) {
                if (!Entity{ e, source }.GetParent()) roots.push_back(e);
            });
            std::sort(roots.begin(), roots.end());
            std::unordered_map<uint64_t, uint64_t> uuidMap;
            std::vector<Entity> copies;
            for (auto e : roots)
                copies.push_back(scene.DuplicateEntityTree(Entity{ e, source }, "", root, &uuidMap));
            for (Entity c : copies)
                scene.RemapEntityReferences(c, uuidMap);
            scene.UpdateWorldTransforms();
            scene.OnEntityTreeSpawned(root);
            CQS::Value arg = CQS::Value::MakeString(streamed.LevelName);
            scene.InvokeScriptMethod((uint32_t)(entt::entity)root, "OnLevelLoaded", &arg, 1);
            CQ_CORE_INFO("[LevelStreaming] Loaded {0} ({1} root objects)", streamed.LevelName, copies.size());
            return (uint32_t)(entt::entity)root;
        }

        bool Unload(Scene& scene, const std::string& nameOrPath)
        {
            Entity root = FindLevelRoot(scene, nameOrPath);
            if (!root)
                return false;
            const std::string name = root.GetComponent<StreamedLevelComponent>().LevelName;
            scene.DestroyEntity(root);
            CQ_CORE_INFO("[LevelStreaming] Unloaded {0}", name);
            return true;
        }

        bool IsLoaded(Scene& scene, const std::string& nameOrPath) { return (bool)FindLevelRoot(scene, nameOrPath); }

        std::vector<std::string> GetLoaded(Scene& scene)
        {
            std::vector<std::string> out;
            auto view = scene.m_Registry.view<StreamedLevelComponent>();
            for (auto h : view)
                out.push_back(view.get<StreamedLevelComponent>(h).LevelName);
            return out;
        }

        void Update(Scene& scene, float dt)
        {
            auto view = scene.m_Registry.view<LevelStreamingVolumeComponent>();
            if (view.empty())
                return;
            // Etkinleştiriciler: etiketli varlıklar ya da (etiket boşsa) birincil kamera
            std::vector<entt::entity> volumes(view.begin(), view.end());
            for (auto h : volumes)
            {
                if (!scene.m_Registry.valid(h) || !scene.IsEntityActive(h))
                    continue;
                auto& v = scene.m_Registry.get<LevelStreamingVolumeComponent>(h);
                if (v.Level.empty())
                    continue;
                bool inside = false;
                if (v.ActivatorTag.empty())
                {
                    if (Entity cam = scene.GetPrimaryCameraEntity())
                        inside = IsInside(scene, h, v, glm::vec3(cam.GetComponent<TransformComponent>().WorldTransform[3]));
                }
                else
                {
                    auto tags = scene.m_Registry.view<TagComponent, TransformComponent>();
                    for (auto e : tags)
                    {
                        if (tags.get<TagComponent>(e).GameTag != v.ActivatorTag || !scene.IsEntityActive(e))
                            continue;
                        if (IsInside(scene, h, v, glm::vec3(tags.get<TransformComponent>(e).WorldTransform[3]))) { inside = true; break; }
                    }
                }
                v.Inside = inside;
                const bool loaded = IsLoaded(scene, v.Level);
                if (inside)
                {
                    v.OutsideTimer = 0.0f;
                    if (!loaded)
                    {
                        const std::string level = v.Level;
                        const glm::vec3 offset = v.Offset;
                        LoadAdditive(scene, level, offset);
                    }
                }
                else if (loaded && v.UnloadWhenOutside)
                {
                    v.OutsideTimer += dt;
                    if (v.OutsideTimer >= v.UnloadDelay)
                        Unload(scene, v.Level);
                }
                if (scene.m_Registry.valid(h))
                    scene.m_Registry.get<LevelStreamingVolumeComponent>(h).Loaded = IsLoaded(scene, scene.m_Registry.get<LevelStreamingVolumeComponent>(h).Level);
            }
        }

        void ClearCache() { Cache().clear(); }
    }

    void RegisterLevelStreamingComponents()
    {
        ComponentDef<StreamedLevelComponent>("Streamed Level", "StreamedLevelComponent", "World", ICON_FA_LAYER_GROUP)
            .Describe("Root of a level loaded additively (LevelLoadAdditive / Level Streaming Volume). Deleting this object unloads the level.")
            .Field("Source", "Source", &StreamedLevelComponent::Source).ReadOnly()
            .Field("LevelName", "Level", &StreamedLevelComponent::LevelName).ReadOnly();
        using V = LevelStreamingVolumeComponent;
        ComponentDef<V>("Level Streaming Volume", "LevelStreamingVolumeComponent", "World", ICON_FA_CUBES_STACKED)
            .Describe("Loads a scene additively when an activator (objects with the tag, or the main camera) enters the box and unloads it after leaving (Unreal level streaming).")
            .Field("Level", "Level", &V::Level, FieldKind::Asset).Filter(".cqscene")
            .Field("Size", "Size", &V::Size)
            .Field("ActivatorTag", "Activator Tag", &V::ActivatorTag).Tooltip("Game tag of the objects that stream the level in (empty = main camera)")
            .Field("UnloadWhenOutside", "Unload When Outside", &V::UnloadWhenOutside)
            .Field("UnloadDelay", "Unload Delay", &V::UnloadDelay).Range(0.0f, 60.0f, 0.05f)
            .Field("Offset", "Level Offset", &V::Offset)
            .Field("Inside", "Inside", &V::Inside).ReadOnly()
            .Field("Loaded", "Loaded", &V::Loaded).ReadOnly();
    }
}
