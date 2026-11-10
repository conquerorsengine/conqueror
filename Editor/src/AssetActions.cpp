#include "Renderer/Renderer2D.h"
#include "Panels/MaterialEditorUI.h"
#include "AssetActions.h"
#include "EditorState.h"
#include "Scripting/DataAssets.h"
#include "Scene/Scene.h"
#include "Scene/Components.h"
#include "Grid/GridComponents.h"
#include "Scene/PostProcessVolume.h"
#include "Scene/Billboards.h"
#include "Core/Serialization/PrefabSerializer.h"
#include "Core/Serialization/SceneSerializer.h"
#include "Core/Logging/Log.h"
#include "Renderer/RHI/Texture.h"
#include "Renderer/Utilities/Renderer3D/ModelLoader.h"
#include "Scripting/ScriptRuntime.h"
#include "Core/Animation/PropertyAnimation.h"
#include "Panels/InspectorPanel.h"
#include "UndoRedo/EntitySnapshotCommand.h"
#include "UndoRedo/UndoRedoManager.h"

#include <algorithm>
#include <cctype>

namespace Conqueror::Editor::AssetActions
{
    namespace
    {
        namespace fs = std::filesystem;

        std::string Ext(const std::string& path)
        {
            std::string e = fs::path(path).extension().string();
            std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) { return (char)std::tolower(c); });
            return e;
        }

        bool IsModel(const std::string& e) { return e == ".gltf" || e == ".glb" || e == ".fbx" || e == ".obj" || e == ".dae"; }
        bool IsTexture(const std::string& e) { return e == ".png" || e == ".jpg" || e == ".jpeg" || e == ".tga" || e == ".bmp"; }
        bool IsAudio(const std::string& e) { return e == ".wav" || e == ".mp3" || e == ".ogg" || e == ".flac"; }

        void PlaceRoot(Entity e, Entity parent, const glm::vec3* worldPosition)
        {
            if (!parent && worldPosition)
                e.GetComponent<TransformComponent>().Position = *worldPosition;
        }
    }

    bool CanSpawn(const std::string& path)
    {
        const std::string e = Ext(path);
        return PrefabSerializer::IsPrefabPath(path) || IsModel(e) || IsTexture(e) || IsAudio(e) || Scripting::ScriptRuntimes::IsScriptFile(path);
    }

    bool IsAppliedToEntity(const std::string& path)
    {
        const std::string e = Ext(path);
        return e == ".cqmat" || e == ".cqphysmat" || e == ".cqanim";
    }

    bool ApplyToEntity(Entity entity, const std::string& path)
    {
        if (!entity)
            return false;
        const std::string e = Ext(path);
        if (e == ".cqmat" && entity.HasComponent<MeshRendererComponent>())
            return MaterialEditorUI::AssignMaterial(entity.GetComponent<MeshRendererComponent>(), path);
        if (e == ".cqlmparams" && entity.HasComponent<MeshRendererComponent>())
        {
            entity.GetComponent<MeshRendererComponent>().LightmapParameters = path;
            return true;
        }
        if (e == ".cqanim")
        {
            // Animation Clip bir nesneye bırakılınca: Animation bileşeni (yoksa) ve klip (varsayılan boşsa o, değilse listeye);
            // Ctrl+Z ile geri alınır
            const std::string clip = SceneSerializer::ToAssetPath(path);
            auto before = EntitySnapshotCommand::Capture(entity);
            if (!entity.HasComponent<AnimationPlayerComponent>())
                entity.AddComponent<AnimationPlayerComponent>();
            auto& a = entity.GetComponent<AnimationPlayerComponent>();
            if (a.Clip.empty() || a.Clip == clip)
                a.Clip = clip;
            else if (std::find(a.Clips.begin(), a.Clips.end(), clip) == a.Clips.end())
                a.Clips.push_back(clip);
            if (auto scene = InspectorPanel::s_SharedScene ? InspectorPanel::s_SharedScene() : nullptr; scene && scene.get() == entity.GetScene())
                UndoRedoManager::Get().Execute(std::make_shared<EntitySnapshotCommand>(scene, (uint64_t)entity.GetComponent<IDComponent>().ID, before,
                                                                                       EntitySnapshotCommand::Capture(entity), "Add Animation Clip"));
            return true;
        }
        if (e == ".cqflare")
        {
            if (!entity.HasComponent<LensFlareComponent>())
                entity.AddComponent<LensFlareComponent>();
            entity.GetComponent<LensFlareComponent>().Flare = SceneSerializer::ToAssetPath(path);
            return true;
        }
        if (e == ".cqvolume")
        {
            // Volume Profile: nesnedeki Post Process Volume'a atanır (yoksa global hacim eklenir)
            if (!entity.HasComponent<PostProcessVolumeComponent>())
                entity.AddComponent<PostProcessVolumeComponent>().Shape = VolumeShape::Global;
            entity.GetComponent<PostProcessVolumeComponent>().Profile = SceneSerializer::ToAssetPath(path);
            return true;
        }
        if (e == ".cqphysmat2d")
        {
            // Unity: Physics Material 2D bir nesneye bırakılınca Rigidbody 2D'ye (yoksa 2D çarpıştırıcılara) atanır
            if (entity.HasComponent<RigidBody2DComponent>()) { entity.GetComponent<RigidBody2DComponent>().PhysicsMaterial = path; return true; }
            bool any = false;
            if (entity.HasComponent<BoxCollider2DComponent>()) { entity.GetComponent<BoxCollider2DComponent>().PhysicsMaterial = path; any = true; }
            if (entity.HasComponent<CircleCollider2DComponent>()) { entity.GetComponent<CircleCollider2DComponent>().PhysicsMaterial = path; any = true; }
            if (entity.HasComponent<CapsuleCollider2DComponent>()) { entity.GetComponent<CapsuleCollider2DComponent>().PhysicsMaterial = path; any = true; }
            if (entity.HasComponent<PolygonCollider2DComponent>()) { entity.GetComponent<PolygonCollider2DComponent>().PhysicsMaterial = path; any = true; }
            if (entity.HasComponent<EdgeCollider2DComponent>()) { entity.GetComponent<EdgeCollider2DComponent>().PhysicsMaterial = path; any = true; }
            if (entity.HasComponent<CompositeCollider2DComponent>()) { entity.GetComponent<CompositeCollider2DComponent>().PhysicsMaterial = path; any = true; }
            if (entity.HasComponent<TilemapCollider2DComponent>()) { entity.GetComponent<TilemapCollider2DComponent>().PhysicsMaterial = path; any = true; }
            return any;
        }
        if (e == ".cqphysmat")
        {
            bool any = false;
            if (entity.HasComponent<BoxColliderComponent>()) { entity.GetComponent<BoxColliderComponent>().PhysicsMaterial = path; any = true; }
            if (entity.HasComponent<SphereColliderComponent>()) { entity.GetComponent<SphereColliderComponent>().PhysicsMaterial = path; any = true; }
            if (entity.HasComponent<CapsuleColliderComponent>()) { entity.GetComponent<CapsuleColliderComponent>().PhysicsMaterial = path; any = true; }
            if (entity.HasComponent<MeshColliderComponent>()) { entity.GetComponent<MeshColliderComponent>().PhysicsMaterial = path; any = true; }
            return any;
        }
        return false;
    }

    Entity SpawnFromAsset(Scene* scene, const std::string& path, Entity parent, const glm::vec3* worldPosition)
    {
        if (!scene || path.empty())
            return {};
        const std::string e = Ext(path);
        const std::string name = fs::path(path).stem().string();
        Entity created;

        if (PrefabSerializer::IsPrefabPath(path))
        {
            created = PrefabSerializer::Instantiate(*scene, path, parent);
            if (created)
                PlaceRoot(created, parent, worldPosition);
        }
        else if (IsModel(e))
        {
            created = scene->CreateEntity(name);
            // Model dosyası: Mesh Renderer'ın Mesh alanında (eski Model bileşeni Mesh Renderer'a katıldı)
            auto& model = created.AddComponent<MeshRendererComponent>();
            model.MaterialInstance = nullptr;
            model.MaterialPath.clear();
            model.ModelPath = SceneSerializer::ToAssetPath(path);
            model.ModelData = ModelLoader::Load(path);
            if (!model.ModelData)
                CQ_CORE_WARN("[Assets] Model could not be loaded: {0}", path);
        }
        else if (IsTexture(e))
        {
            created = scene->CreateEntity(name);
            auto& sprite = created.AddComponent<SpriteRendererComponent>();
            sprite.Texture = Texture2D::Create(path);
            Renderer2D::ApplySpriteImport(sprite);
        }
        else if (IsAudio(e))
        {
            created = scene->CreateEntity(name);
            created.AddComponent<AudioSourceComponent>().FilePath = SceneSerializer::ToAssetPath(path);
        }
        else if (Scripting::ScriptRuntimes::IsScriptFile(path))
        {
            // Data Asset sınıfı nesneye eklenmez (Unity: ScriptableObject betiği GameObject'e eklenemez)
            if (RefuseDataScript(path))
                return {};
            // Betik: hedef varlığa bileşen olarak eklenir (hedef yoksa betik adıyla yeni nesne)
            Entity target = parent ? parent : scene->CreateEntity(name);
            if (!target.HasComponent<ConquerorScriptComponent>())
                target.AddComponent<ConquerorScriptComponent>();
            auto& scripts = target.GetComponent<ConquerorScriptComponent>().Scripts;
            ConquerorScriptData data;
            data.ScriptPath = SceneSerializer::ToAssetPath(path);
            scripts.push_back(data);
            if (!parent)
                PlaceRoot(target, Entity{}, worldPosition);
            return target;
        }
        else
        {
            return {};
        }

        if (created && !PrefabSerializer::IsPrefabPath(path))
        {
            if (parent)
                scene->SetEntityParent(created, parent, false);
            else
                PlaceRoot(created, parent, worldPosition);
        }
        if (created)
            scene->UpdateWorldTransforms();
        return created;
    }

    void AddPrimitiveCollider(Entity entity)
    {
        if (!entity || !entity.HasComponent<MeshRendererComponent>())
            return;
        switch (entity.GetComponent<MeshRendererComponent>().Type)
        {
            case MeshType::Sphere: entity.AddComponent<SphereColliderComponent>(); break;
            case MeshType::Plane: entity.AddComponent<MeshColliderComponent>(); break;
            case MeshType::Cylinder: entity.AddComponent<MeshColliderComponent>().IsConvex = true; break;
            default: entity.AddComponent<BoxColliderComponent>(); break;
        }
    }

    bool RefuseDataScript(const std::string& scriptPath)
    {
        const std::string resolved = SceneSerializer::ResolveAssetPath(scriptPath);
        if (!DataAssets::IsDataOnlyScript(resolved))
            return false;
        const auto classes = DataAssets::DataClassesOf(resolved);
        const std::string cls = classes.empty() ? fs::path(resolved).stem().string() : classes.front();
        EditorState::Get().ShowMessage("Can't add script",
            "Can't add script behaviour '" + cls + "'. It is a Data Asset class: it holds data and cannot run on an object.\n\n"
            "Create assets from it with Create > Data Asset > " + cls + ", then load them in a script (LoadDataAsset) or drop them on an [Asset(\".cqdata\")] field.");
        return true;
    }

    fs::path CreatePrefab(Entity root, const fs::path& directory)
    {
        if (!root)
            return {};
        std::string base = root.GetComponent<TagComponent>().Tag;
        // Dosya adı için güvenli karakterler
        for (char& c : base)
            if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
                c = '_';
        if (base.empty())
            base = "Prefab";
        std::error_code ec;
        fs::create_directories(directory, ec);
        fs::path target = directory / (base + PrefabSerializer::Extension);
        for (int i = 1; fs::exists(target, ec) && i < 1000; ++i)
            target = directory / (base + " " + std::to_string(i) + PrefabSerializer::Extension);
        if (!PrefabSerializer::Serialize(root, target))
            return {};
        // Kaynak varlık artık bu prefab'ın örneği (Unity davranışı)
        if (!root.HasComponent<PrefabInstanceComponent>())
            root.AddComponent<PrefabInstanceComponent>();
        root.GetComponent<PrefabInstanceComponent>().Prefab = PrefabSerializer::ToProjectPath(target);
        return target;
    }
}
