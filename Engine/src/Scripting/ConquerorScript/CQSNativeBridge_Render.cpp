#include "CQSNativeBridge.h"
#include "CQSValue.h"
#include "Scripting/NativeRegistry.h"
#include "Scripting/ScriptContext.h"
#include "Scene/Scene.h"
#include "Scene/Entity.h"
#include "Scene/Components.h"
#include "Renderer/RenderSettings.h"
#include "Core/Console/ConsoleVariables.h"
#include "Core/Serialization/SceneSerializer.h"
#include "Renderer/RHI/Cubemap.h"
#include "Renderer/ComputeShaders.h"
#include "Renderer/RayTracingShaders.h"
#include "Scene/Signals.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

// Sahne görüntü ayarları (Unity RenderSettings / Volume karşılığı) ve kamera denetimi.
// RenderSettingSet("Post.BloomIntensity", 0.8), RenderSettingSet("Fog.Color", {r: 0.5, g: 0.6, b: 0.7}) ...
namespace Conqueror::CQS
{
    namespace
    {
        Scene* ActiveScene() { return ScriptContext::GetActiveScene(); }

        enum class Kind { Bool, Int, Float, Vec3, Enum };

        struct SettingDef
        {
            const char* Name;
            Kind K;
            void* (*Ptr)(RenderSettings&);
            std::vector<const char*> Options;   // Enum adları (değer = sıra)
        };

#define CQ_RS(name, kind, member, ...) SettingDef{ name, kind, [](RenderSettings& r) -> void* { return &(r.member); }, { __VA_ARGS__ } }

        const std::vector<SettingDef>& Settings()
        {
            static const std::vector<SettingDef> s = {
                CQ_RS("Shadows.Enabled", Kind::Bool, Shadows.Enabled),
                CQ_RS("Shadows.Quality", Kind::Enum, Shadows.Quality, "Hard", "Soft", "VerySoft"),
                CQ_RS("Shadows.Resolution", Kind::Int, Shadows.Resolution),
                CQ_RS("Shadows.Distance", Kind::Float, Shadows.Distance),
                CQ_RS("Shadows.CascadeCount", Kind::Int, Shadows.CascadeCount),
                CQ_RS("Shadows.Strength", Kind::Float, Shadows.Strength),
                CQ_RS("Shadows.Softness", Kind::Float, Shadows.Softness),
                CQ_RS("Shadows.Color", Kind::Vec3, Shadows.Color),
                CQ_RS("Fog.Enabled", Kind::Bool, Fog.Enabled),
                CQ_RS("Fog.Mode", Kind::Enum, Fog.Mode, "Linear", "Exponential", "ExponentialSquared"),
                CQ_RS("Fog.Color", Kind::Vec3, Fog.Color),
                CQ_RS("Fog.Density", Kind::Float, Fog.Density),
                CQ_RS("Fog.Start", Kind::Float, Fog.Start),
                CQ_RS("Fog.End", Kind::Float, Fog.End),
                CQ_RS("Fog.HeightFalloff", Kind::Float, Fog.HeightFalloff),
                CQ_RS("Fog.BaseHeight", Kind::Float, Fog.BaseHeight),
                CQ_RS("Post.Enabled", Kind::Bool, Post.Enabled),
                CQ_RS("Post.Tonemap", Kind::Enum, Post.Tonemap, "None", "Neutral", "ACES", "AgX", "Reinhard"),
                CQ_RS("Post.Exposure", Kind::Float, Post.Exposure),
                CQ_RS("Post.Bloom", Kind::Bool, Post.Bloom),
                CQ_RS("Post.BloomThreshold", Kind::Float, Post.BloomThreshold),
                CQ_RS("Post.BloomKnee", Kind::Float, Post.BloomKnee),
                CQ_RS("Post.BloomIntensity", Kind::Float, Post.BloomIntensity),
                CQ_RS("Post.BloomScatter", Kind::Float, Post.BloomScatter),
                CQ_RS("Post.BloomTint", Kind::Vec3, Post.BloomTint),
                CQ_RS("Post.AmbientOcclusion", Kind::Bool, Post.AmbientOcclusion),
                CQ_RS("Post.AORadius", Kind::Float, Post.AORadius),
                CQ_RS("Post.AOIntensity", Kind::Float, Post.AOIntensity),
                CQ_RS("Post.AOPower", Kind::Float, Post.AOPower),
                CQ_RS("Post.ColorGrading", Kind::Bool, Post.ColorGrading),
                CQ_RS("Post.Contrast", Kind::Float, Post.Contrast),
                CQ_RS("Post.Saturation", Kind::Float, Post.Saturation),
                CQ_RS("Post.Temperature", Kind::Float, Post.Temperature),
                CQ_RS("Post.Tint", Kind::Float, Post.Tint),
                CQ_RS("Post.Lift", Kind::Vec3, Post.Lift),
                CQ_RS("Post.Gamma", Kind::Vec3, Post.Gamma),
                CQ_RS("Post.Gain", Kind::Vec3, Post.Gain),
                CQ_RS("Post.Vignette", Kind::Bool, Post.Vignette),
                CQ_RS("Post.VignetteIntensity", Kind::Float, Post.VignetteIntensity),
                CQ_RS("Post.VignetteSmoothness", Kind::Float, Post.VignetteSmoothness),
                CQ_RS("Post.VignetteColor", Kind::Vec3, Post.VignetteColor),
                CQ_RS("Post.ChromaticAberration", Kind::Bool, Post.ChromaticAberration),
                CQ_RS("Post.ChromaticAberrationIntensity", Kind::Float, Post.ChromaticAberrationIntensity),
                CQ_RS("Post.FilmGrain", Kind::Bool, Post.FilmGrain),
                CQ_RS("Post.FilmGrainIntensity", Kind::Float, Post.FilmGrainIntensity),
                CQ_RS("Post.Sharpen", Kind::Bool, Post.Sharpen),
                CQ_RS("Post.SharpenAmount", Kind::Float, Post.SharpenAmount),
                CQ_RS("Post.AntiAliasing", Kind::Enum, Post.AntiAliasing, "None", "FXAA", "SMAA", "TAA", "TAA_FXAA", "MSAA", "CSAA", "SSAA", "FSAA", "EQAA",
                      "MLAA", "CMAA", "TXAA", "TSAA", "TAAU", "SMAA_T2X", "TSRAA", "TSR", "DLSS", "DLAA", "FSR", "XeSS", "PSSR", "MetalFX", "NIS", "CAS",
                      "FSRAA", "XeSSAA", "OGSSAA", "SGSSAA", "RGSS", "TSSAA", "DEAA", "GBAA", "SRAA"),
                CQ_RS("Post.MSAASamples", Kind::Int, Post.MSAASamples),
                CQ_RS("Post.SSAAScale", Kind::Float, Post.SSAAScale),
                CQ_RS("Post.UpscaleRenderScale", Kind::Float, Post.UpscaleRenderScale),
                CQ_RS("Post.UpscaleSharpness", Kind::Float, Post.UpscaleSharpness),
                CQ_RS("Post.EQAASamples", Kind::Int, Post.EQAASamples),
                CQ_RS("Post.TAAFeedback", Kind::Float, Post.TAAFeedback),
                CQ_RS("Post.TAASharpness", Kind::Float, Post.TAASharpness),
                CQ_RS("Post.ScreenSpaceReflections", Kind::Bool, Post.ScreenSpaceReflections),
                CQ_RS("Post.SSRIntensity", Kind::Float, Post.SSRIntensity),
                CQ_RS("Post.SSRMaxDistance", Kind::Float, Post.SSRMaxDistance),
                CQ_RS("Post.SSRThickness", Kind::Float, Post.SSRThickness),
                CQ_RS("Post.SSRMaxRoughness", Kind::Float, Post.SSRMaxRoughness),
                CQ_RS("Post.ScreenSpaceGI", Kind::Bool, Post.ScreenSpaceGI),
                CQ_RS("Post.SSGIIntensity", Kind::Float, Post.SSGIIntensity),
                CQ_RS("Post.SSGIRadius", Kind::Float, Post.SSGIRadius),
                CQ_RS("Post.SSGIRays", Kind::Int, Post.SSGIRays),
                CQ_RS("Post.VolumetricFog", Kind::Bool, Post.VolumetricFog),
                CQ_RS("Post.VolumetricFogDensity", Kind::Float, Post.VolumetricFogDensity),
                CQ_RS("Post.VolumetricFogHeightFalloff", Kind::Float, Post.VolumetricFogHeightFalloff),
                CQ_RS("Post.VolumetricFogBaseHeight", Kind::Float, Post.VolumetricFogBaseHeight),
                CQ_RS("Post.VolumetricFogAnisotropy", Kind::Float, Post.VolumetricFogAnisotropy),
                CQ_RS("Post.VolumetricFogIntensity", Kind::Float, Post.VolumetricFogIntensity),
                CQ_RS("Post.VolumetricFogDistance", Kind::Float, Post.VolumetricFogDistance),
                CQ_RS("Post.VolumetricFogAlbedo", Kind::Vec3, Post.VolumetricFogAlbedo),
                CQ_RS("Post.VolumetricFogAmbient", Kind::Float, Post.VolumetricFogAmbient),
                CQ_RS("Post.VolumetricClouds", Kind::Bool, Post.VolumetricClouds),
                CQ_RS("Post.CloudCoverage", Kind::Float, Post.CloudCoverage),
                CQ_RS("Post.CloudDensity", Kind::Float, Post.CloudDensity),
                CQ_RS("Post.CloudBottom", Kind::Float, Post.CloudBottom),
                CQ_RS("Post.CloudThickness", Kind::Float, Post.CloudThickness),
                CQ_RS("Post.CloudScale", Kind::Float, Post.CloudScale),
                CQ_RS("Post.CloudWindSpeed", Kind::Float, Post.CloudWindSpeed),
                CQ_RS("Post.CloudWindDirection", Kind::Float, Post.CloudWindDirection),
                CQ_RS("Post.CloudColor", Kind::Vec3, Post.CloudColor),
                CQ_RS("Post.DepthOfField", Kind::Bool, Post.DepthOfField),
                CQ_RS("Post.FocusDistance", Kind::Float, Post.FocusDistance),
                CQ_RS("Post.Aperture", Kind::Float, Post.Aperture),
                CQ_RS("Post.FocalLength", Kind::Float, Post.FocalLength),
                CQ_RS("Post.DOFMaxBlur", Kind::Float, Post.DOFMaxBlur),
                CQ_RS("Post.MotionBlur", Kind::Bool, Post.MotionBlur),
                CQ_RS("Post.MotionBlurIntensity", Kind::Float, Post.MotionBlurIntensity),
                CQ_RS("Post.MotionBlurMax", Kind::Float, Post.MotionBlurMax),
                CQ_RS("Post.AutoExposure", Kind::Bool, Post.AutoExposure),
                CQ_RS("Post.AutoExposureMin", Kind::Float, Post.AutoExposureMin),
                CQ_RS("Post.AutoExposureMax", Kind::Float, Post.AutoExposureMax),
                CQ_RS("Post.AutoExposureSpeedUp", Kind::Float, Post.AutoExposureSpeedUp),
                CQ_RS("Post.AutoExposureSpeedDown", Kind::Float, Post.AutoExposureSpeedDown),
                CQ_RS("Post.AutoExposureCompensation", Kind::Float, Post.AutoExposureCompensation),
                CQ_RS("Environment.Sky", Kind::Enum, Environment.Sky, "Auto", "Procedural", "SolidColor", "Atmosphere"),
                CQ_RS("Environment.AtmosphereRayleigh", Kind::Float, Environment.AtmosphereRayleigh),
                CQ_RS("Environment.AtmosphereMie", Kind::Float, Environment.AtmosphereMie),
                CQ_RS("Environment.AtmosphereMieAnisotropy", Kind::Float, Environment.AtmosphereMieAnisotropy),
                CQ_RS("Environment.AtmosphereOzone", Kind::Float, Environment.AtmosphereOzone),
                CQ_RS("Environment.AtmosphereSunDisk", Kind::Float, Environment.AtmosphereSunDisk),
                CQ_RS("Environment.AtmosphereIntensity", Kind::Float, Environment.AtmosphereIntensity),
                CQ_RS("Environment.AtmosphereGroundAlbedo", Kind::Vec3, Environment.AtmosphereGroundAlbedo),
                CQ_RS("Environment.BackgroundColor", Kind::Vec3, Environment.BackgroundColor),
                CQ_RS("Environment.ImageBasedLighting", Kind::Bool, Environment.ImageBasedLighting),
                CQ_RS("Environment.ReflectionIntensity", Kind::Float, Environment.ReflectionIntensity),
                CQ_RS("Environment.SkyboxIntensity", Kind::Float, Environment.SkyboxIntensity),
            };
            return s;
        }
#undef CQ_RS

        std::string Lower(std::string s)
        {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
            return s;
        }

        const SettingDef* FindSetting(const std::string& name)
        {
            const std::string key = Lower(name);
            for (const auto& d : Settings())
            {
                if (Lower(d.Name) == key)
                    return &d;
                // "PostProcess.X" de kabul edilir
                if (key.rfind("postprocess.", 0) == 0 && Lower(d.Name) == "post." + key.substr(12))
                    return &d;
            }
            return nullptr;
        }

        bool ReadVec3(const Value& v, glm::vec3& out)
        {
            if (v.IsMap())
            {
                auto& m = v.AsMap()->Entries;
                const char* rgb[3] = { "r", "g", "b" };
                const char* xyz[3] = { "x", "y", "z" };
                for (int i = 0; i < 3; ++i)
                {
                    auto it = m.find(rgb[i]);
                    if (it == m.end()) it = m.find(xyz[i]);
                    if (it != m.end()) out[i] = (float)it->second.ToNumber();
                }
                return true;
            }
            if (v.IsList())
            {
                auto& l = v.AsList()->Elements;
                for (int i = 0; i < 3 && i < (int)l.size(); ++i)
                    out[i] = (float)l[(size_t)i].ToNumber();
                return true;
            }
            if (v.IsNumber())
            {
                out = glm::vec3((float)v.ToNumber());
                return true;
            }
            return false;
        }

        Value MakeColor(const glm::vec3& c)
        {
            auto* m = new CQSMapObject();
            m->Entries["r"] = Value::MakeFloat(c.r);
            m->Entries["g"] = Value::MakeFloat(c.g);
            m->Entries["b"] = Value::MakeFloat(c.b);
            return Value::MakeObject(m);
        }

        Entity EntityFromId(int64_t id)
        {
            Scene* scene = ActiveScene();
            if (!scene || id < 0 || id >= (int64_t)ScriptContext::NullEntity)
                return {};
            entt::entity h = (entt::entity)(uint32_t)id;
            return scene->m_Registry.valid(h) ? Entity{ h, scene } : Entity{};
        }

        // Varlık argümanı: kimlik (sayı) ya da ad (metin). Ad bulunamazsa boş varlık (eskiden metin 0'a
        // çevrilip 0 numaralı varlık değişiyordu).
        Entity EntityFromValue(const Value& v)
        {
            if (!v.IsString())
                return EntityFromId((int64_t)v.ToNumber());
            Scene* scene = ScriptContext::GetActiveScene();
            if (!scene)
                return {};
            const std::string& name = v.AsString()->Value;
            auto view = scene->m_Registry.view<TagComponent>();
            for (auto h : view)
                if (view.get<TagComponent>(h).Tag == name)
                    return Entity{ h, scene };
            return {};
        }

        // Hedef kamera: kimlik / ad / yoksa sahnenin birincil kamerası
        CameraComponent* CameraOf(int argc, Value* args)
        {
            Scene* scene = ActiveScene();
            if (!scene)
                return nullptr;
            Entity e;
            if (argc > 0 && args[0].IsNumber() && args[0].ToNumber() >= 0)
                e = EntityFromValue(args[0]);
            else if (argc > 0 && args[0].IsString())
            {
                auto view = scene->m_Registry.view<TagComponent>();
                for (auto h : view)
                    if (view.get<TagComponent>(h).Tag == args[0].AsString()->Value)
                    {
                        e = Entity{ h, scene };
                        break;
                    }
            }
            else
            {
                auto view = scene->m_Registry.view<CameraComponent>();
                for (auto h : view)
                    if (view.get<CameraComponent>(h).Primary)
                        return &view.get<CameraComponent>(h);
            }
            return e && e.HasComponent<CameraComponent>() ? &e.GetComponent<CameraComponent>() : nullptr;
        }

        float Num(int argc, Value* args, int i, float def)
        {
            return i < argc && args[i].IsNumber() ? (float)args[i].ToNumber() : def;
        }

        // Lighting penceresinin ortam ışığı / gökyüzü değerleri (RenderSettings dışında, Scene üyeleri)
        const std::vector<std::string>& LightingKeys()
        {
            static const std::vector<std::string> keys = {
                "Lighting.AmbientSource", "Lighting.AmbientIntensity", "Lighting.AmbientColor", "Lighting.AmbientSkyColor",
                "Lighting.AmbientEquatorColor", "Lighting.AmbientGroundColor", "Lighting.SkyboxExposure", "Lighting.SkyboxRotation",
                "Lighting.SkyboxTint" };
            return keys;
        }

        bool SetLighting(Scene& s, const std::string& key, const Value& v)
        {
            glm::vec3 c;
            if (key == "Lighting.AmbientSource") { s.SetEnvironmentLightingSource((Scene::EnvironmentLightingSource)std::clamp((int)v.ToNumber(), 0, 2)); return true; }
            if (key == "Lighting.AmbientIntensity") { s.SetAmbientIntensity((float)v.ToNumber()); return true; }
            if (key == "Lighting.SkyboxExposure") { s.SetSkyboxExposure((float)v.ToNumber()); return true; }
            if (key == "Lighting.SkyboxRotation") { s.SetSkyboxRotation((float)v.ToNumber()); return true; }
            if (!ReadVec3(v, c)) return false;
            if (key == "Lighting.AmbientColor") { s.SetAmbientColor(c); return true; }
            if (key == "Lighting.AmbientSkyColor") { s.SetAmbientSkyColor(c); return true; }
            if (key == "Lighting.AmbientEquatorColor") { s.SetAmbientEquatorColor(c); return true; }
            if (key == "Lighting.AmbientGroundColor") { s.SetAmbientGroundColor(c); return true; }
            if (key == "Lighting.SkyboxTint") { s.SetSkyboxTint(c); return true; }
            return false;
        }

        Value GetLighting(Scene& s, const std::string& key)
        {
            if (key == "Lighting.AmbientSource") return Value::MakeInt((int)s.GetEnvironmentLightingSource());
            if (key == "Lighting.AmbientIntensity") return Value::MakeFloat(s.GetAmbientIntensity());
            if (key == "Lighting.SkyboxExposure") return Value::MakeFloat(s.GetSkyboxExposure());
            if (key == "Lighting.SkyboxRotation") return Value::MakeFloat(s.GetSkyboxRotation());
            if (key == "Lighting.AmbientColor") return MakeColor(s.GetAmbientColor());
            if (key == "Lighting.AmbientSkyColor") return MakeColor(s.GetAmbientSkyColor());
            if (key == "Lighting.AmbientEquatorColor") return MakeColor(s.GetAmbientEquatorColor());
            if (key == "Lighting.AmbientGroundColor") return MakeColor(s.GetAmbientGroundColor());
            if (key == "Lighting.SkyboxTint") return MakeColor(s.GetSkyboxTint());
            return Value::MakeNull();
        }
    }

    void RegisterRenderNatives(Scripting::NativeRegistry* vm)
    {
        // Compute Shader (Unity ComputeShader.SetTexture / SetFloat / SetInt / SetVector / Dispatch)
        vm->DefineNative("ComputeSetTexture", [](int argc, Value* args) -> Value {
            if (argc < 3 || !args[0].IsString() || !args[1].IsString() || !args[2].IsString()) return Value::MakeBool(false);
            return Value::MakeBool(ComputeShaders::SetTexture(args[0].AsString()->Value, args[1].AsString()->Value, args[2].AsString()->Value));
        }, 3);
        vm->DefineNative("ComputeSetFloat", [](int argc, Value* args) -> Value {
            if (argc < 3 || !args[0].IsString() || !args[1].IsString()) return Value::MakeBool(false);
            return Value::MakeBool(ComputeShaders::SetFloat(args[0].AsString()->Value, args[1].AsString()->Value, (float)args[2].ToNumber()));
        }, 3);
        vm->DefineNative("ComputeSetInt", [](int argc, Value* args) -> Value {
            if (argc < 3 || !args[0].IsString() || !args[1].IsString()) return Value::MakeBool(false);
            return Value::MakeBool(ComputeShaders::SetInt(args[0].AsString()->Value, args[1].AsString()->Value, (int)args[2].ToNumber()));
        }, 3);
        vm->DefineNative("ComputeSetVector", [](int argc, Value* args) -> Value {
            if (argc < 3 || !args[0].IsString() || !args[1].IsString()) return Value::MakeBool(false);
            float v[4] = { 0, 0, 0, 0 };
            if (args[2].IsMap())
            {
                auto& m = args[2].AsMap()->Entries;
                const char* keys[4][2] = { { "x", "r" }, { "y", "g" }, { "z", "b" }, { "w", "a" } };
                for (int i = 0; i < 4; ++i)
                    for (const char* k : keys[i])
                        if (auto it = m.find(k); it != m.end()) v[i] = (float)it->second.ToNumber();
            }
            else if (args[2].IsList())
            {
                auto& l = args[2].AsList()->Elements;
                for (int i = 0; i < 4 && i < (int)l.size(); ++i) v[i] = (float)l[(size_t)i].ToNumber();
            }
            else
                v[0] = (float)args[2].ToNumber();
            return Value::MakeBool(ComputeShaders::SetVector(args[0].AsString()->Value, args[1].AsString()->Value, glm::vec4(v[0], v[1], v[2], v[3])));
        }, 3);
        // Signal (Unity Signal asset): SignalSend(entity, "Assets/Door Open.cqsignal") -> çalışan tepki sayısı (Signal Receiver)
        vm->DefineNative("SignalSend", [](int argc, Value* args) -> Value {
            Scene* scene = ActiveScene();
            if (!scene || argc < 2 || !args[1].IsString()) return Value::MakeInt(0);
            Entity e = EntityFromValue(args[0]);
            if (!e) return Value::MakeInt(0);
            return Value::MakeInt(SignalAssets::Send(*scene, (uint32_t)(entt::entity)e, args[1].AsString()->Value));
        }, 2);
        // Ray Tracing Shader: RayTracingDispatch("Assets/X.cqraytrace", "Assets/Result.cqrt") -> bool (etkin sahne, birincil kamera)
        vm->DefineNative("RayTracingDispatch", [](int argc, Value* args) -> Value {
            Scene* scene = ActiveScene();
            if (!scene || argc < 2 || !args[0].IsString() || !args[1].IsString()) return Value::MakeBool(false);
            return Value::MakeBool(RayTracingShaders::Dispatch(*scene, args[0].AsString()->Value, args[1].AsString()->Value));
        }, 2);
        vm->DefineNative("ComputeDispatch", [](int argc, Value* args) -> Value {
            if (argc < 2 || !args[0].IsString()) return Value::MakeBool(false);
            const int x = (int)args[1].ToNumber(), y = argc > 2 ? (int)args[2].ToNumber() : 1, z = argc > 3 ? (int)args[3].ToNumber() : 1;
            return Value::MakeBool(ComputeShaders::Dispatch(args[0].AsString()->Value, x, y, z));
        }, -1);
        // SceneSetSkybox("Assets/Sky/studio.hdr") -> bool: sahnenin HDR gökyüzü (arka plan + görüntü tabanlı aydınlatma
        // ve yansımalar; Environment.Sky = Auto iken). Boş yol kaldırır. Sahneyle birlikte kaydedilir.
        vm->DefineNative("SceneSetSkybox", [](int argc, Value* args) -> Value {
            Scene* scene = ActiveScene();
            if (!scene || argc < 1 || !args[0].IsString()) return Value::MakeBool(false);
            const std::string path = args[0].AsString()->Value;
            if (path.empty())
            {
                scene->SetSkybox(nullptr);
                return Value::MakeBool(true);
            }
            const std::string full = SceneSerializer::ResolveAssetPath(path);
            auto cube = Cubemap::CreateFromEquirectangular(full, 512);
            if (!cube || !cube->GetRendererID()) return Value::MakeBool(false);
            scene->SetSkybox(cube);
            return Value::MakeBool(true);
        }, 1);
        vm->DefineNative("RenderSettingSet", [](int argc, Value* args) -> Value {
            Scene* scene = ActiveScene();
            if (scene && argc >= 2 && args[0].IsString() && args[0].AsString()->Value.rfind("Lighting.", 0) == 0)
                return Value::MakeBool(SetLighting(*scene, args[0].AsString()->Value, args[1]));
            const SettingDef* d = argc >= 2 && args[0].IsString() ? FindSetting(args[0].AsString()->Value) : nullptr;
            if (!scene || !d) return Value::MakeBool(false);
            void* p = d->Ptr(scene->GetRenderSettings());
            const Value& v = args[1];
            switch (d->K)
            {
                case Kind::Bool: *(bool*)p = v.IsBool() ? v.AsBool() : v.ToNumber() != 0.0; return Value::MakeBool(true);
                case Kind::Int: *(int*)p = (int)v.ToNumber(); return Value::MakeBool(true);
                case Kind::Float: *(float*)p = (float)v.ToNumber(); return Value::MakeBool(true);
                case Kind::Vec3: return Value::MakeBool(ReadVec3(v, *(glm::vec3*)p));
                case Kind::Enum:
                {
                    if (v.IsString())
                    {
                        const std::string want = Lower(v.AsString()->Value);
                        for (size_t i = 0; i < d->Options.size(); ++i)
                            if (Lower(d->Options[i]) == want)
                            {
                                *(int*)p = (int)i;
                                return Value::MakeBool(true);
                            }
                        return Value::MakeBool(false);
                    }
                    const int i = (int)v.ToNumber();
                    if (i < 0 || i >= (int)d->Options.size()) return Value::MakeBool(false);
                    *(int*)p = i;
                    return Value::MakeBool(true);
                }
            }
            return Value::MakeBool(false);
        }, 2);
        vm->DefineNative("RenderSettingGet", [](int argc, Value* args) -> Value {
            Scene* scene = ActiveScene();
            if (scene && argc >= 1 && args[0].IsString() && args[0].AsString()->Value.rfind("Lighting.", 0) == 0)
                return GetLighting(*scene, args[0].AsString()->Value);
            const SettingDef* d = argc >= 1 && args[0].IsString() ? FindSetting(args[0].AsString()->Value) : nullptr;
            if (!scene || !d) return Value::MakeNull();
            void* p = d->Ptr(scene->GetRenderSettings());
            switch (d->K)
            {
                case Kind::Bool: return Value::MakeBool(*(bool*)p);
                case Kind::Int: return Value::MakeInt(*(int*)p);
                case Kind::Float: return Value::MakeFloat(*(float*)p);
                case Kind::Vec3: return MakeColor(*(glm::vec3*)p);
                case Kind::Enum:
                {
                    const int i = *(int*)p;
                    return Value::MakeString(i >= 0 && i < (int)d->Options.size() ? d->Options[(size_t)i] : "");
                }
            }
            return Value::MakeNull();
        }, 1);
        vm->DefineNative("RenderSettingNames", [](int, Value*) -> Value {
            auto* list = new CQSListObject();
            for (const auto& d : Settings())
                list->Elements.push_back(Value::MakeString(d.Name));
            for (const auto& k : LightingKeys())
                list->Elements.push_back(Value::MakeString(k));
            return Value::MakeObject(list);
        }, 0);

        // ---- Kamera (hedef: kimlik, ad ya da birincil kamera)
        vm->DefineNative("CameraSetOrthographic", [](int argc, Value* args) -> Value {
            // CameraSetOrthographic(size) birincil kamera; CameraSetOrthographic(target, size)
            const bool targeted = argc >= 2;
            CameraComponent* cam = CameraOf(targeted ? argc : 0, args);
            if (!cam) return Value::MakeBool(false);
            auto& c = cam->Camera;
            c.SetOrthographic(std::max(0.01f, Num(argc, args, targeted ? 1 : 0, c.GetOrthographicSize())), c.GetOrthographicNearClip(), c.GetOrthographicFarClip());
            return Value::MakeBool(true);
        }, -1);
        vm->DefineNative("CameraSetPerspective", [](int argc, Value* args) -> Value {
            // CameraSetPerspective(fovDegrees) birincil; CameraSetPerspective(target, fovDegrees, near, far)
            const bool targeted = argc >= 2 || (argc == 1 && args[0].IsString());
            CameraComponent* cam = CameraOf(targeted ? argc : 0, args);
            if (!cam) return Value::MakeBool(false);
            auto& c = cam->Camera;
            const int o = targeted ? 1 : 0;
            const float fov = Num(argc, args, o, glm::degrees(c.GetPerspectiveVerticalFOV()));
            c.SetPerspective(glm::radians(std::clamp(fov, 1.0f, 179.0f)), Num(argc, args, o + 1, c.GetPerspectiveNearClip()), Num(argc, args, o + 2, c.GetPerspectiveFarClip()));
            return Value::MakeBool(true);
        }, -1);
        vm->DefineNative("CameraGetOrthographicSize", [](int argc, Value* args) -> Value {
            CameraComponent* cam = CameraOf(argc, args);
            return Value::MakeFloat(cam ? cam->Camera.GetOrthographicSize() : 0.0f);
        }, -1);
        vm->DefineNative("CameraIsOrthographic", [](int argc, Value* args) -> Value {
            CameraComponent* cam = CameraOf(argc, args);
            return Value::MakeBool(cam && cam->Camera.GetProjectionType() == SceneCamera::ProjectionType::Orthographic);
        }, -1);
        vm->DefineNative("CameraSetPrimary", [](int argc, Value* args) -> Value {
            Scene* scene = ActiveScene();
            CameraComponent* cam = argc > 0 ? CameraOf(argc, args) : nullptr;
            if (!scene || !cam) return Value::MakeBool(false);
            auto view = scene->m_Registry.view<CameraComponent>();
            for (auto h : view)
                view.get<CameraComponent>(h).Primary = &view.get<CameraComponent>(h) == cam;
            return Value::MakeBool(true);
        }, 1);
        // Konsol değişkenleri: CVarGet("r.Bloom") -> "1", CVarSet("t.MaxFPS", 60) -> bool, ConsoleCommand("stat fps") -> çıktı
        vm->DefineNative("CVarGet", [](int argc, Value* args) -> Value {
            ConsoleVariable* v = argc > 0 ? ConsoleSystem::FindVariable(args[0].ToString()) : nullptr;
            return v && v->Get ? Value::MakeString(v->Get()) : Value::MakeNull();
        }, 1);
        vm->DefineNative("CVarSet", [](int argc, Value* args) -> Value {
            ConsoleVariable* v = argc > 1 ? ConsoleSystem::FindVariable(args[0].ToString()) : nullptr;
            if (!v || !v->Set) return Value::MakeBool(false);
            std::string text = args[1].IsBool() ? (args[1].AsBool() ? "1" : "0") : args[1].ToString();
            return Value::MakeBool(v->Set(text));
        }, 2);
        vm->DefineNative("ConsoleCommand", [](int argc, Value* args) -> Value {
            if (argc < 1) return Value::MakeString("");
            return Value::MakeString(ConsoleSystem::ExecuteText(args[0].ToString()));
        }, 1);
        // GetMainCamera() -> entity | null: Primary kamera (Unity Camera.main); yoksa ilk kamera
        vm->DefineNative("GetMainCamera", [](int, Value*) -> Value {
            Scene* scene = ActiveScene();
            if (!scene) return Value::MakeNull();
            auto view = scene->m_Registry.view<CameraComponent>();
            entt::entity first = entt::null;
            for (auto h : view)
            {
                if (!scene->IsEntityActive(h))
                    continue;
                if (view.get<CameraComponent>(h).Primary)
                    return Value::MakeInt((int64_t)(uint32_t)h);
                if (first == entt::null)
                    first = h;
            }
            return first != entt::null ? Value::MakeInt((int64_t)(uint32_t)first) : Value::MakeNull();
        }, 0);
    }
}
