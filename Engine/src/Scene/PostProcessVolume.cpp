#include "PostProcessVolume.h"
#include "Core/Console/ConsoleVariables.h"
#include "Scene/Scene.h"
#include "Scene/Entity.h"
#include "Scene/Components.h"
#include "Scene/ComponentRegistry.h"
#include "Scene/FullScreenPass.h"
#include "Core/Debug/DebugDraw.h"
#include "Core/Math/Math.h"

#include <IconsFontAwesome6.h>
#include <glm/gtc/matrix_transform.hpp>

#include "Core/Serialization/SceneSerializer.h"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <unordered_map>
#include <vector>

namespace Conqueror
{
    namespace
    {
        // Hacmin bu noktadaki etkisi (içeride 1, dışarıda BlendDistance boyunca 0'a iner)
        float Influence(Scene& scene, entt::entity e, const PostProcessVolumeComponent& v, const glm::vec3& p)
        {
            if (v.Shape == VolumeShape::Global)
                return 1.0f;
            glm::mat4 world = scene.ComputeWorldMatrix(e);
            float outside = 0.0f;
            if (v.Shape == VolumeShape::Box)
            {
                // Ölçekli kutu: yerel uzayda yarı boyutlara uzaklık, dünya ölçeğine çevrilir
                glm::vec3 pos, euler, scale;
                Math::DecomposeTransform(world, pos, euler, scale);
                glm::mat4 frame = glm::translate(glm::mat4(1.0f), pos) * glm::mat4_cast(glm::quat(euler));
                glm::vec3 local = glm::vec3(glm::inverse(frame) * glm::vec4(p, 1.0f));
                glm::vec3 half = glm::abs(v.Size * scale) * 0.5f;
                glm::vec3 d = glm::max(glm::abs(local) - half, glm::vec3(0.0f));
                outside = glm::length(d);
            }
            else
            {
                glm::vec3 pos, euler, scale;
                Math::DecomposeTransform(world, pos, euler, scale);
                float r = v.Radius * std::max({ std::fabs(scale.x), std::fabs(scale.y), std::fabs(scale.z) });
                outside = std::max(0.0f, glm::distance(p, pos) - r);
            }
            if (outside <= 0.0f)
                return 1.0f;
            if (v.BlendDistance <= 1e-4f)
                return 0.0f;
            return std::clamp(1.0f - outside / v.BlendDistance, 0.0f, 1.0f);
        }

        float Lerp(float a, float b, float t) { return a + (b - a) * t; }
        glm::vec3 Lerp(const glm::vec3& a, const glm::vec3& b, float t) { return a + (b - a) * t; }
    }

    namespace PostProcessVolumes
    {
        static PostProcessSettings ResolveVolumes(Scene& scene, const PostProcessSettings& base, const glm::vec3& cameraPosition);

        PostProcessSettings Resolve(Scene& scene, const PostProcessSettings& base, const glm::vec3& cameraPosition)
        {
            PostProcessSettings out = ResolveVolumes(scene, base, cameraPosition);
            // Full Screen Pass bileşenleri (Order sırasıyla)
            {
                std::vector<std::pair<int, FullScreenPassEntry>> passes;
                auto view = scene.m_Registry.view<FullScreenPassComponent>();
                for (auto e : view)
                {
                    const auto& p = view.get<FullScreenPassComponent>(e);
                    if (!scene.IsEntityActive(e) || p.Shader.empty() || p.Intensity <= 0.0f)
                        continue;
                    passes.push_back({ p.Order, { p.Shader, std::clamp(p.Intensity, 0.0f, 1.0f), p.Injection == FullScreenPassInjection::AfterPostProcess } });
                }
                std::stable_sort(passes.begin(), passes.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
                out.FullScreenPasses.clear();
                for (auto& p : passes)
                    out.FullScreenPasses.push_back(p.second);
            }
            ConsoleSystem::ApplyPostOverrides(out);   // r.* konsol değişkenleri en son uygulanır
            return out;
        }

        static PostProcessSettings ResolveVolumes(Scene& scene, const PostProcessSettings& base, const glm::vec3& cameraPosition)
        {
            PostProcessSettings out = base;
            auto view = scene.m_Registry.view<PostProcessVolumeComponent>();
            if (view.empty())
                return out;
            std::vector<entt::entity> volumes(view.begin(), view.end());
            std::stable_sort(volumes.begin(), volumes.end(), [&](entt::entity a, entt::entity b) {
                return view.get<PostProcessVolumeComponent>(a).Priority < view.get<PostProcessVolumeComponent>(b).Priority;
            });
            for (entt::entity e : volumes)
            {
                auto& vol = view.get<PostProcessVolumeComponent>(e);
                // Profil kullanan hacim: efekt ayarları paylaşılan .cqvolume dosyasından
                PostProcessVolumeComponent fromProfile;
                const bool useProfile = !vol.Profile.empty() && (fromProfile = vol, VolumeProfiles::Load(vol.Profile, fromProfile));
                const PostProcessVolumeComponent& v = useProfile ? fromProfile : vol;
                float w = scene.IsEntityActive(e) ? std::clamp(v.Weight, 0.0f, 1.0f) * Influence(scene, e, v, cameraPosition) : 0.0f;
                vol.CurrentInfluence = w;
                if (w <= 0.0f)
                    continue;
                if (v.OverrideExposure)
                    out.Exposure = Lerp(out.Exposure, v.Exposure, w);
                if (v.OverrideBloom)
                {
                    // Kapalı bloom açılırken yoğunluk sıfırdan büyür (ani patlama olmaz)
                    float from = out.Bloom ? out.BloomIntensity : 0.0f;
                    out.Bloom = true;
                    out.BloomIntensity = Lerp(from, v.BloomIntensity, w);
                    out.BloomThreshold = Lerp(out.BloomThreshold, v.BloomThreshold, w);
                    out.BloomTint = Lerp(out.BloomTint, v.BloomTint, w);
                }
                if (v.OverrideColorGrading)
                {
                    out.ColorGrading = true;
                    out.Contrast = Lerp(out.Contrast, v.Contrast, w);
                    out.Saturation = Lerp(out.Saturation, v.Saturation, w);
                    out.Temperature = Lerp(out.Temperature, v.Temperature, w);
                    out.Tint = Lerp(out.Tint, v.Tint, w);
                    out.Gain = Lerp(out.Gain, out.Gain * v.ColorFilter, w);
                }
                if (v.OverrideVignette)
                {
                    float from = out.Vignette ? out.VignetteIntensity : 0.0f;
                    out.Vignette = true;
                    out.VignetteIntensity = Lerp(from, v.VignetteIntensity, w);
                    out.VignetteSmoothness = Lerp(out.VignetteSmoothness, v.VignetteSmoothness, w);
                    out.VignetteColor = Lerp(out.VignetteColor, v.VignetteColor, w);
                }
                if (v.OverrideChromaticAberration)
                {
                    float from = out.ChromaticAberration ? out.ChromaticAberrationIntensity : 0.0f;
                    out.ChromaticAberration = true;
                    out.ChromaticAberrationIntensity = Lerp(from, v.ChromaticAberrationIntensity, w);
                }
                if (v.OverrideFilmGrain)
                {
                    float from = out.FilmGrain ? out.FilmGrainIntensity : 0.0f;
                    out.FilmGrain = true;
                    out.FilmGrainIntensity = Lerp(from, v.FilmGrainIntensity, w);
                }
                if (v.OverrideAmbientOcclusion)
                {
                    float from = out.AmbientOcclusion ? out.AOIntensity : 0.0f;
                    out.AmbientOcclusion = true;
                    out.AOIntensity = Lerp(from, v.AOIntensity, w);
                }
                if (v.OverrideDepthOfField)
                {
                    // Kapalı DOF açılırken bulanıklık kısık diyaframdan (f/32, neredeyse her şey net) büyür
                    const float fromAperture = out.DepthOfField ? out.Aperture : 32.0f;
                    out.DepthOfField = true;
                    out.FocusDistance = Lerp(out.FocusDistance, v.FocusDistance, w);
                    out.Aperture = Lerp(fromAperture, v.Aperture, w);
                    out.FocalLength = Lerp(out.FocalLength, v.FocalLength, w);
                }
            }
            return out;
        }

        void DrawGizmo(Scene& scene, uint32_t entity)
        {
            entt::entity e = (entt::entity)entity;
            if (!scene.m_Registry.valid(e) || !scene.m_Registry.all_of<PostProcessVolumeComponent>(e))
                return;
            const auto& v = scene.m_Registry.get<PostProcessVolumeComponent>(e);
            const glm::vec4 kColor{ 0.55f, 0.75f, 1.0f, 1.0f };
            const glm::vec4 kBlend{ 0.55f, 0.75f, 1.0f, 0.35f };
            glm::vec3 pos, euler, scale;
            Math::DecomposeTransform(scene.ComputeWorldMatrix(e), pos, euler, scale);
            glm::mat4 frame = glm::translate(glm::mat4(1.0f), pos) * glm::mat4_cast(glm::quat(euler));
            if (v.Shape == VolumeShape::Box)
            {
                glm::vec3 size = glm::abs(v.Size * scale);
                DebugDraw::Box(frame * glm::scale(glm::mat4(1.0f), size), kColor, 0.0f, false);
                if (v.BlendDistance > 0.0f)
                    DebugDraw::Box(frame * glm::scale(glm::mat4(1.0f), size + glm::vec3(v.BlendDistance * 2.0f)), kBlend, 0.0f, false);
            }
            else if (v.Shape == VolumeShape::Sphere)
            {
                float r = v.Radius * std::max({ std::fabs(scale.x), std::fabs(scale.y), std::fabs(scale.z) });
                DebugDraw::Sphere(pos, r, kColor, 0.0f, false);
                if (v.BlendDistance > 0.0f)
                    DebugDraw::Sphere(pos, r + v.BlendDistance, kBlend, 0.0f, false);
            }
        }
    }

    namespace VolumeProfiles
    {
        bool IsProfileField(const std::string& n)
        {
            return n != "Shape" && n != "Size" && n != "Radius" && n != "BlendDistance" && n != "Weight" && n != "Priority" &&
                   n != "Profile" && n != "CurrentInfluence";
        }

        bool Load(const std::string& path, PostProcessVolumeComponent& values)
        {
            struct Cached { std::filesystem::file_time_type Time; std::vector<std::pair<std::string, std::string>> Fields; };
            static std::unordered_map<std::string, Cached> cache;
            const std::string file = SceneSerializer::ResolveAssetPath(path);
            std::error_code ec;
            const auto t = std::filesystem::last_write_time(file, ec);
            if (ec)
                return false;
            auto it = cache.find(file);
            if (it == cache.end() || it->second.Time != t)
            {
                try
                {
                    YAML::Node root = YAML::LoadFile(file);
                    YAML::Node n = root["VolumeProfile"];
                    if (!n || !n.IsMap())
                        return false;
                    Cached c{ t, {} };
                    for (auto kv = n.begin(); kv != n.end(); ++kv)
                        c.Fields.emplace_back(kv->first.as<std::string>(), kv->second.IsScalar() ? kv->second.as<std::string>() : YAML::Dump(kv->second));
                    it = cache.insert_or_assign(file, std::move(c)).first;
                }
                catch (...) { return false; }
            }
            const ComponentInfo* info = ComponentRegistry::Find("PostProcessVolumeComponent");
            if (!info)
                return false;
            for (const auto& [name, text] : it->second.Fields)
                if (const ComponentField* f = ComponentRegistry::FindField(*info, name); f && IsProfileField(name))
                    ComponentRegistry::FieldFromString(*f, &values, text);
            return true;
        }

        bool Save(const std::string& path, const PostProcessVolumeComponent& values)
        {
            const ComponentInfo* info = ComponentRegistry::Find("PostProcessVolumeComponent");
            if (!info)
                return false;
            YAML::Emitter out;
            out << YAML::BeginMap << YAML::Key << "VolumeProfile" << YAML::Value << YAML::BeginMap;
            for (const auto& f : info->Fields)
                if (IsProfileField(f.Name) && !f.ReadOnly)
                    out << YAML::Key << f.Name << YAML::Value << ComponentRegistry::FieldToString(f, const_cast<PostProcessVolumeComponent*>(&values));
            out << YAML::EndMap << YAML::EndMap;
            std::ofstream file(SceneSerializer::ResolveAssetPath(path), std::ios::trunc);
            if (!file)
                return false;
            file << out.c_str() << "\n";
            return true;
        }
    }

    void RegisterPostProcessVolumeComponents()
    {
        using V = PostProcessVolumeComponent;
        ComponentDef<V>("Post Process Volume", "PostProcessVolumeComponent", "Rendering", ICON_FA_CUBES_STACKED).Color({ 0.62f, 0.72f, 1.0f, 1.0f })
            .Describe("Changes post-processing while the camera is inside (Unity Volume / Unreal Post Process Volume). Global volumes apply everywhere.")
            .Field("Shape", "Mode", &V::Shape).Options({ "Global", "Box", "Sphere" })
            .Field("Size", "Size", &V::Size).Speed(0.05f).VisibleIf([](const V& v) { return v.Shape == VolumeShape::Box; })
            .Field("Radius", "Radius", &V::Radius).Range(0.0f, 100000.0f, 0.05f).VisibleIf([](const V& v) { return v.Shape == VolumeShape::Sphere; })
            .Field("BlendDistance", "Blend Distance", &V::BlendDistance).Range(0.0f, 1000.0f, 0.05f).VisibleIf([](const V& v) { return v.Shape != VolumeShape::Global; })
            .Field("Weight", "Weight", &V::Weight).Range(0.0f, 1.0f, 0.01f)
            .Field("Priority", "Priority", &V::Priority).Range(-1000.0f, 1000.0f, 1.0f)
            .Field("Profile", "Profile", &V::Profile, FieldKind::Asset).Filter(".cqvolume")
                .Tooltip("Volume Profile asset: the overrides come from this file and are shared by every volume that uses it. "
                         "Empty = the overrides below.")
            .Field("OverrideExposure", "Exposure", &V::OverrideExposure).Header("Overrides")
            .Field("Exposure", "Post Exposure (EV)", &V::Exposure).Range(-10.0f, 10.0f, 0.05f).VisibleIf([](const V& v) { return v.OverrideExposure; })
            .Field("OverrideBloom", "Bloom", &V::OverrideBloom)
            .Field("BloomIntensity", "Bloom Intensity", &V::BloomIntensity).Range(0.0f, 20.0f, 0.01f).VisibleIf([](const V& v) { return v.OverrideBloom; })
            .Field("BloomThreshold", "Bloom Threshold", &V::BloomThreshold).Range(0.0f, 10.0f, 0.01f).VisibleIf([](const V& v) { return v.OverrideBloom; })
            .Field("BloomTint", "Bloom Tint", &V::BloomTint, FieldKind::Color3).VisibleIf([](const V& v) { return v.OverrideBloom; })
            .Field("OverrideColorGrading", "Color Adjustments", &V::OverrideColorGrading)
            .Field("Contrast", "Contrast", &V::Contrast).Range(0.0f, 3.0f, 0.01f).VisibleIf([](const V& v) { return v.OverrideColorGrading; })
            .Field("Saturation", "Saturation", &V::Saturation).Range(0.0f, 3.0f, 0.01f).VisibleIf([](const V& v) { return v.OverrideColorGrading; })
            .Field("Temperature", "Temperature", &V::Temperature).Range(-1.0f, 1.0f, 0.01f).VisibleIf([](const V& v) { return v.OverrideColorGrading; })
            .Field("Tint", "Tint", &V::Tint).Range(-1.0f, 1.0f, 0.01f).VisibleIf([](const V& v) { return v.OverrideColorGrading; })
            .Field("ColorFilter", "Color Filter", &V::ColorFilter, FieldKind::Color3).VisibleIf([](const V& v) { return v.OverrideColorGrading; })
            .Field("OverrideVignette", "Vignette", &V::OverrideVignette)
            .Field("VignetteIntensity", "Vignette Intensity", &V::VignetteIntensity).Range(0.0f, 1.0f, 0.01f).VisibleIf([](const V& v) { return v.OverrideVignette; })
            .Field("VignetteSmoothness", "Vignette Smoothness", &V::VignetteSmoothness).Range(0.01f, 1.0f, 0.01f).VisibleIf([](const V& v) { return v.OverrideVignette; })
            .Field("VignetteColor", "Vignette Color", &V::VignetteColor, FieldKind::Color3).VisibleIf([](const V& v) { return v.OverrideVignette; })
            .Field("OverrideChromaticAberration", "Chromatic Aberration", &V::OverrideChromaticAberration)
            .Field("ChromaticAberrationIntensity", "Aberration Intensity", &V::ChromaticAberrationIntensity).Range(0.0f, 1.0f, 0.01f)
                .VisibleIf([](const V& v) { return v.OverrideChromaticAberration; })
            .Field("OverrideFilmGrain", "Film Grain", &V::OverrideFilmGrain)
            .Field("FilmGrainIntensity", "Grain Intensity", &V::FilmGrainIntensity).Range(0.0f, 1.0f, 0.01f).VisibleIf([](const V& v) { return v.OverrideFilmGrain; })
            .Field("OverrideAmbientOcclusion", "Ambient Occlusion", &V::OverrideAmbientOcclusion)
            .Field("AOIntensity", "AO Intensity", &V::AOIntensity).Range(0.0f, 4.0f, 0.01f).VisibleIf([](const V& v) { return v.OverrideAmbientOcclusion; })
            .Field("OverrideDepthOfField", "Depth Of Field", &V::OverrideDepthOfField)
            .Field("FocusDistance", "Focus Distance", &V::FocusDistance).Range(0.1f, 1000.0f, 0.05f).VisibleIf([](const V& v) { return v.OverrideDepthOfField; })
            .Field("Aperture", "Aperture (f-stop)", &V::Aperture).Range(0.7f, 32.0f, 0.05f).VisibleIf([](const V& v) { return v.OverrideDepthOfField; })
            .Field("FocalLength", "Focal Length (mm)", &V::FocalLength).Range(8.0f, 300.0f, 0.5f).VisibleIf([](const V& v) { return v.OverrideDepthOfField; })
            .Field("CurrentInfluence", "Current Influence", &V::CurrentInfluence).ReadOnly().Header("Runtime");
        // Profil kullanılırken hacmin kendi efekt alanları gizlenir (profil düzenlenir)
        if (ComponentInfo* info = ComponentRegistry::Find("PostProcessVolumeComponent"))
            for (auto& f : info->Fields)
                if (VolumeProfiles::IsProfileField(f.Name))
                {
                    auto old = f.VisibleIf;
                    f.VisibleIf = [old](const void* c) { return static_cast<const V*>(c)->Profile.empty() && (!old || old(c)); };
                }
    }
}
