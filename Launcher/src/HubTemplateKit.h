#pragma once

// Şablon sahnelerinin ortak yardımcıları (HubTemplates.cpp, HubTemplatesPro.cpp)

#include "Scene/Scene.h"
#include "Scene/Entity.h"
#include "Scene/Components.h"
#include "Core/Math/Math.h"
#include "Addons/AddonManager.h"
#include "Renderer/RHI/Texture.h"
#include "Renderer/RenderSettings.h"

#include <filesystem>
#include <fstream>
#include <string>

namespace Conqueror::Launcher::Kit
{
    namespace fs = std::filesystem;

    // size: metre cinsinden dünya boyutu. Motorun ilkel ağları 2 birimdir (küp -1..1, küre r=1), bu yüzden ölçek = boyut / 2.
    inline Entity MakeMesh(Scene& scene, const char* name, MeshType type, const glm::vec3& pos, const glm::vec3& size, const glm::vec4& color)
    {
        Entity e = scene.CreateEntity(name);
        auto& tc = e.GetComponent<TransformComponent>();
        tc.Position = pos;
        tc.Scale = size * 0.5f;
        auto& mr = e.AddComponent<MeshRendererComponent>();
        mr.Type = type;
        mr.Color = color;
        return e;
    }

    // Ada göre varlık (Scene::FindEntityByTag oyun etiketine bakar)
    inline Entity FindByName(Scene& scene, const std::string& name)
    {
        auto view = scene.m_Registry.view<TagComponent>();
        for (auto handle : view)
            if (view.get<TagComponent>(handle).Tag == name)
                return Entity{ handle, &scene };
        return {};
    }

    inline void MakeStatic(Entity e)
    {
        auto& rb = e.AddComponent<RigidbodyComponent>();
        rb.Type = RigidbodyComponent::BodyType::Static;
        e.AddComponent<BoxColliderComponent>();
    }

    // Statik kutu (görünür) — çarpıştırıcılı blok
    inline Entity MakeBlock(Scene& scene, const char* name, const glm::vec3& pos, const glm::vec3& size, const glm::vec4& color, float yawDeg = 0.0f,
                            float pitchDeg = 0.0f)
    {
        Entity e = MakeMesh(scene, name, MeshType::Cube, pos, size, color);
        e.GetComponent<TransformComponent>().Rotation = { glm::radians(pitchDeg), glm::radians(yawDeg), 0.0f };
        MakeStatic(e);
        return e;
    }

    inline Entity MakeCamera(Scene& scene, const glm::vec3& pos, const glm::vec3& lookAt, bool orthographic = false, float orthoSize = 6.0f,
                             const char* name = "Main Camera")
    {
        Entity cam = scene.CreateEntity(name);
        auto& tc = cam.GetComponent<TransformComponent>();
        tc.Position = pos;
        tc.Rotation = Math::EulerForForward(lookAt - pos);
        auto& cc = cam.AddComponent<CameraComponent>();
        cc.Primary = true;
        if (orthographic)
        {
            cc.Camera.SetProjectionType(SceneCamera::ProjectionType::Orthographic);
            cc.Camera.SetOrthographicSize(orthoSize);
        }
        return cam;
    }

    inline Entity MakeSun(Scene& scene, const glm::vec3& direction = glm::vec3(-0.4f, -1.0f, -0.3f), float intensity = 2.2f,
                          const glm::vec3& color = glm::vec3(1.0f, 0.97f, 0.92f))
    {
        Entity sun = scene.CreateEntity("Directional Light");
        auto& tc = sun.GetComponent<TransformComponent>();
        tc.Position = { 0.0f, 6.0f, 0.0f };
        tc.Rotation = Math::EulerForForward(direction);
        auto& dl = sun.AddComponent<DirectionalLightComponent>();
        dl.Intensity = intensity;
        dl.Color = color;
        return sun;
    }

    inline Entity MakeSprite(Scene& scene, const char* name, const char* texture, const glm::vec3& pos, const glm::vec3& scale, const glm::vec4& color)
    {
        Entity e = scene.CreateEntity(name);
        auto& tc = e.GetComponent<TransformComponent>();
        tc.Position = pos;
        tc.Scale = scale;
        auto& sr = e.AddComponent<SpriteRendererComponent>();
        sr.Texture = Texture2D::Create(texture);
        sr.Color = color;
        return e;
    }

    // Model (glb/fbx/obj): proje göreli yol, Y ekseni etrafında dönüş, tekdüze ölçek
    inline Entity MakeModel(Scene& scene, const char* name, const std::string& projectRelativePath, const glm::vec3& pos, float yawDeg = 0.0f, float scale = 1.0f)
    {
        Entity e = scene.CreateEntity(name);
        auto& tc = e.GetComponent<TransformComponent>();
        tc.Position = pos;
        tc.Rotation = { 0.0f, glm::radians(yawDeg), 0.0f };
        tc.Scale = glm::vec3(scale);
        e.AddComponent<ModelComponent>().FilePath = projectRelativePath;
        return e;
    }

    // Unity 2D şablonları gibi: düz arka plan rengi, parlama yok (sprite renkleri olduğu gibi görünür)
    inline void Setup2DRendering(Scene& scene, const glm::vec3& background)
    {
        auto& rs = scene.GetRenderSettings();
        rs.Environment.Sky = SkyMode::SolidColor;
        rs.Environment.BackgroundColor = background;
        rs.Post.Bloom = false;
        rs.Post.AmbientOcclusion = false;
    }

    // Unreal şablonlarının "yüksek kalite" görünümü: fiziksel gökyüzü, yumuşak gölgeler, SSAO, ACES, TAA
    inline void SetupHighQuality(Scene& scene, bool reflections = false, bool globalIllumination = false)
    {
        auto& rs = scene.GetRenderSettings();
        rs.Environment.Sky = SkyMode::Atmosphere;
        rs.Shadows.Quality = ShadowQuality::VerySoft;
        rs.Shadows.CascadeCount = 3;
        rs.Shadows.Distance = 120.0f;
        rs.Post.Tonemap = Tonemapper::ACES;
        rs.Post.AntiAliasing = AntiAliasingMode::TAA;
        rs.Post.AmbientOcclusion = true;
        rs.Post.Bloom = true;
        rs.Post.BloomIntensity = 0.35f;
        rs.Post.ScreenSpaceReflections = reflections;
        rs.Post.ScreenSpaceGI = globalIllumination;
    }

    inline bool WriteText(const fs::path& path, const char* text)
    {
        std::error_code ec;
        fs::create_directories(path.parent_path(), ec);
        std::ofstream f(path, std::ios::binary);
        if (!f.is_open())
            return false;
        f << text;
        return true;
    }

    // Motor kaynağını (Resources/...) projeye kopyala
    inline bool CopyResource(const fs::path& projectDir, const std::string& resource, const std::string& projectRelative)
    {
        std::error_code ec;
        const fs::path dst = projectDir / projectRelative;
        fs::create_directories(dst.parent_path(), ec);
        fs::copy_file(fs::path("Resources") / resource, dst, fs::copy_options::overwrite_existing, ec);
        return !ec;
    }

    inline void AddScript(Entity e, const std::string& projectRelativePath, const std::string& className)
    {
        auto& sc = e.HasComponent<ConquerorScriptComponent>() ? e.GetComponent<ConquerorScriptComponent>() : e.AddComponent<ConquerorScriptComponent>();
        ConquerorScriptData data;
        data.ScriptPath = projectRelativePath;
        data.ClassName = className;
        sc.Scripts.push_back(data);
    }

    inline bool InstallAddon(const std::string& folder, const fs::path& projectDir, std::string& error)
    {
        std::error_code ec;
        fs::path src = AddonManager::GetCatalogDirectory() / folder;
        if (!fs::is_directory(src, ec))
        {
            error = "Addon not found in catalog: " + folder;
            return false;
        }
        fs::path dst = projectDir / "Packages" / folder;
        fs::copy(src, dst, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
        if (ec)
        {
            error = "Cannot copy addon " + folder + ": " + ec.message();
            return false;
        }
        return true;
    }
}
