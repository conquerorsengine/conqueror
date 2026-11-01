#include "Panels/MaterialEditorUI.h"
#include "AssetActions.h"
#include "Scene/Scene.h"
#include "Scene/Components.h"
#include "Core/Serialization/PrefabSerializer.h"
#include "Core/Serialization/SceneSerializer.h"
#include "Core/Logging/Log.h"
#include "Renderer/RHI/Texture.h"
#include "Renderer/Utilities/Renderer3D/ModelLoader.h"
#include "Scripting/ScriptRuntime.h"

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
        return e == ".cqmat" || e == ".cqphysmat";
    }

    bool ApplyToEntity(Entity entity, const std::string& path)
    {
        if (!entity)
            return false;
        const std::string e = Ext(path);
        if (e == ".cqmat" && entity.HasComponent<MeshRendererComponent>())
            return MaterialEditorUI::AssignMaterial(entity.GetComponent<MeshRendererComponent>(), path);
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
            auto& model = created.AddComponent<ModelComponent>();
            model.FilePath = SceneSerializer::ToAssetPath(path);
            model.ModelData = ModelLoader::Load(path);
            if (!model.ModelData)
                CQ_CORE_WARN("[Assets] Model could not be loaded: {0}", path);
        }
        else if (IsTexture(e))
        {
            created = scene->CreateEntity(name);
            created.AddComponent<SpriteRendererComponent>().Texture = Texture2D::Create(path);
        }
        else if (IsAudio(e))
        {
            created = scene->CreateEntity(name);
            created.AddComponent<AudioSourceComponent>().FilePath = SceneSerializer::ToAssetPath(path);
        }
        else if (Scripting::ScriptRuntimes::IsScriptFile(path))
        {
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
