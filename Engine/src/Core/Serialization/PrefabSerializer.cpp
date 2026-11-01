#include "PrefabSerializer.h"
#include "SceneSerializer.h"
#include "Scene/Components.h"
#include "Core/Project/Project.h"
#include "Core/Logging/Log.h"

#include "Core/Math/Math.h"

#include <algorithm>
#include <fstream>
#include <unordered_map>

namespace Conqueror
{
    namespace
    {
        namespace fs = std::filesystem;

        // Önbellekli şablon: prefab dosyasından yüklenmiş tek köklü sahne
        struct PrefabTemplate
        {
            std::shared_ptr<Scene> TemplateScene;
            entt::entity Root = entt::null;
            fs::file_time_type Time;
        };

        std::unordered_map<std::string, PrefabTemplate>& Templates()
        {
            static std::unordered_map<std::string, PrefabTemplate> s_Templates;
            return s_Templates;
        }

        PrefabTemplate* LoadTemplate(const fs::path& path)
        {
            std::error_code ec;
            auto time = fs::last_write_time(path, ec);
            if (ec)
            {
                CQ_CORE_ERROR("[Prefab] Not found: {0}", path.string());
                return nullptr;
            }
            const std::string key = fs::weakly_canonical(path, ec).string();
            auto& cache = Templates();
            auto it = cache.find(key);
            if (it != cache.end() && it->second.Time == time && it->second.TemplateScene)
                return &it->second;

            PrefabTemplate t;
            t.TemplateScene = std::make_shared<Scene>();
            t.Time = time;
            SceneSerializer serializer(t.TemplateScene);
            if (!serializer.Deserialize(path))
            {
                CQ_CORE_ERROR("[Prefab] Failed to load {0}", path.string());
                return nullptr;
            }
            t.TemplateScene->m_Registry.view<IDComponent>().each([&](auto e, auto&) {
                if (t.Root == entt::null && !Entity{ e, t.TemplateScene.get() }.GetParent())
                    t.Root = e;
            });
            if (t.Root == entt::null)
            {
                CQ_CORE_ERROR("[Prefab] {0} contains no entities", path.string());
                return nullptr;
            }
            cache[key] = std::move(t);
            return &cache[key];
        }

        void MarkInstance(Entity root, const std::string& prefabPath)
        {
            if (!root.HasComponent<PrefabInstanceComponent>())
                root.AddComponent<PrefabInstanceComponent>();
            root.GetComponent<PrefabInstanceComponent>().Prefab = prefabPath;
        }
    }

    bool PrefabSerializer::IsPrefabPath(const std::string& path)
    {
        return fs::path(path).extension() == Extension;
    }

    fs::path PrefabSerializer::Resolve(const std::string& path)
    {
        fs::path p(SceneSerializer::ResolveAssetPath(path));
        if (p.is_absolute())
            return p;
        std::error_code ec;
        fs::path projectDir = Project::GetActiveProjectDirectory();
        if (!projectDir.empty())
        {
            if (fs::exists(projectDir / p, ec))
                return projectDir / p;
            if (fs::exists(projectDir / "Assets" / p, ec))
                return projectDir / "Assets" / p;
        }
        if (fs::exists(p, ec))
            return fs::absolute(p, ec);
        // Henüz yok (ör. yeni kaydedilecek): proje göreli
        return projectDir.empty() ? fs::absolute(p, ec) : projectDir / p;
    }

    std::string PrefabSerializer::ToProjectPath(const fs::path& path)
    {
        std::error_code ec;
        fs::path abs = fs::weakly_canonical(fs::absolute(path, ec), ec);
        std::string portable = SceneSerializer::ToAssetPath(abs.string());
        std::replace(portable.begin(), portable.end(), '\\', '/');
        return portable;
    }

    void PrefabSerializer::ClearCache()
    {
        Templates().clear();
    }

    bool PrefabSerializer::Serialize(Entity root, const fs::path& filepath)
    {
        if (!root)
            return false;
        // Geçici sahneye kopyala: yalnızca alt ağaç, kök ebeveynsiz; kökün dünya dönüşümü yerel olarak saklanır
        auto temp = std::make_shared<Scene>();
        std::unordered_map<uint64_t, uint64_t> uuidMap;
        Entity copy = temp->DuplicateEntityTree(root, "", Entity{}, &uuidMap);
        temp->RemapEntityReferences(copy, uuidMap);
        if (root.GetParent())
        {
            auto& tc = copy.GetComponent<TransformComponent>();
            Math::DecomposeTransform(root.GetComponent<TransformComponent>().WorldTransform, tc.Position, tc.Rotation, tc.Scale);
        }
        // Şablon kökü kendine referans vermez (iç içe prefab'lar çocuklarda işaretli kalır)
        if (copy.HasComponent<PrefabInstanceComponent>())
            copy.RemoveComponent<PrefabInstanceComponent>();

        std::error_code ec;
        if (filepath.has_parent_path())
            fs::create_directories(filepath.parent_path(), ec);
        std::ofstream out(filepath, std::ios::binary);
        if (!out.is_open())
        {
            CQ_CORE_ERROR("[Prefab] Cannot write {0}", filepath.string());
            return false;
        }
        SceneSerializer serializer(temp);
        serializer.SerializeToStream(out);
        out.close();
        // Aynı yoldaki eski şablon geçersiz
        Templates().erase(fs::weakly_canonical(filepath, ec).string());
        CQ_CORE_INFO("[Prefab] Saved {0}", filepath.filename().string());
        return true;
    }

    Entity PrefabSerializer::Instantiate(Scene& scene, const std::string& path, Entity parent)
    {
        fs::path resolved = Resolve(path);
        PrefabTemplate* t = LoadTemplate(resolved);
        if (!t)
            return {};
        Entity source{ t->Root, t->TemplateScene.get() };
        std::unordered_map<uint64_t, uint64_t> uuidMap;
        Entity root = scene.DuplicateEntityTree(source, "", parent, &uuidMap);
        scene.RemapEntityReferences(root, uuidMap);
        MarkInstance(root, ToProjectPath(resolved));
        scene.UpdateWorldTransforms();
        scene.OnEntityTreeSpawned(root);
        return root;
    }

    Entity PrefabSerializer::Deserialize(const std::shared_ptr<Scene>& scene, const fs::path& filepath, Entity parent)
    {
        return scene ? Instantiate(*scene, filepath.string(), parent) : Entity{};
    }

    Entity PrefabSerializer::FindInstanceRoot(Entity entity)
    {
        for (Entity e = entity; e; e = e.GetParent())
            if (e.HasComponent<PrefabInstanceComponent>())
                return e;
        return {};
    }

    std::string PrefabSerializer::GetPrefabPath(Entity entity)
    {
        Entity root = FindInstanceRoot(entity);
        return root ? root.GetComponent<PrefabInstanceComponent>().Prefab : std::string();
    }

    bool PrefabSerializer::ApplyInstance(Entity instanceRoot)
    {
        Entity root = FindInstanceRoot(instanceRoot);
        if (!root)
            return false;
        const std::string path = root.GetComponent<PrefabInstanceComponent>().Prefab;
        return Serialize(root, Resolve(path));
    }

    Entity PrefabSerializer::RevertInstance(Entity instanceRoot)
    {
        Entity root = FindInstanceRoot(instanceRoot);
        if (!root)
            return {};
        Scene* scene = root.GetScene();
        const std::string path = root.GetComponent<PrefabInstanceComponent>().Prefab;
        const TransformComponent transform = root.GetComponent<TransformComponent>();
        const std::string name = root.GetComponent<TagComponent>().Tag;
        Entity parent = root.GetParent();
        Entity fresh = Instantiate(*scene, path, parent);
        if (!fresh)
            return {};
        // Örnek düzeyindeki yerleşim korunur: konum, dönüş, ölçek, isim
        auto& tc = fresh.GetComponent<TransformComponent>();
        tc.Position = transform.Position;
        tc.Rotation = transform.Rotation;
        tc.Scale = transform.Scale;
        fresh.GetComponent<TagComponent>().Tag = name;
        scene->DestroyEntity(root);
        scene->UpdateWorldTransforms();
        return fresh;
    }
}
