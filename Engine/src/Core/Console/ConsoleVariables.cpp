#include "ConsoleVariables.h"
#include "Core/Input/InputGlyphs.h"
#include "UI/UISystem.h"
#include "Renderer/Utilities/Renderer3D/ClusterRenderer.h"
#include "Renderer/Utilities/Renderer3D/Renderer3D.h"
#include "Core/Application.h"
#include "Core/Audio/AudioEngine.h"
#include "Core/Logging/Log.h"
#include "Core/PhysicsSystem/PhysicsSettings.h"
#include "Core/PhysicsSystem/Physics3D/PhysicsWorld3D.h"
#include "Core/Project/Project.h"
#include "Core/Project/ProjectSettings.h"
#include "Core/Tiering/QualitySettings.h"
#include "Core/Time/TimeManager.h"
#include "Renderer/RenderSettings.h"
#include "Renderer/AntiAliasing.h"
#include "Scene/Scene.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>

namespace Conqueror
{
    namespace
    {
        struct CommandEntry
        {
            std::string Name, Description;
            ConsoleCommandFn Run;
        };

        struct State
        {
            std::map<std::string, ConsoleVariable> Variables;   // anahtar: küçük harf
            std::map<std::string, CommandEntry> Commands;
            std::function<Scene*()> SceneProvider;
            RenderOverrides Overrides;
            bool BuiltinsRegistered = false;
            std::recursive_mutex Mutex;
        };

        State& S()
        {
            static State s;
            return s;
        }

        std::string Lower(std::string s)
        {
            std::transform(s.begin(), s.end(), s.begin(), [](char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; });
            return s;
        }

        std::string Trim(const std::string& s)
        {
            size_t a = s.find_first_not_of(" \t\r\n");
            if (a == std::string::npos) return "";
            size_t b = s.find_last_not_of(" \t\r\n");
            return s.substr(a, b - a + 1);
        }

        std::vector<std::string> Split(const std::string& line)
        {
            std::vector<std::string> out;
            std::istringstream in(line);
            std::string token;
            while (in >> token)
                out.push_back(token);
            return out;
        }

        bool ParseBool(const std::string& v, bool& out)
        {
            std::string l = Lower(v);
            if (l == "1" || l == "true" || l == "on" || l == "yes") { out = true; return true; }
            if (l == "0" || l == "false" || l == "off" || l == "no") { out = false; return true; }
            return false;
        }

        bool ParseFloat(const std::string& v, float& out)
        {
            try { size_t used = 0; out = std::stof(v, &used); return used > 0; }
            catch (...) { return false; }
        }

        bool ParseInt(const std::string& v, int& out)
        {
            float f;
            if (!ParseFloat(v, f)) return false;
            out = (int)std::lround(f);
            return true;
        }

        std::string FloatText(float f)
        {
            std::ostringstream o;
            o << f;
            return o.str();
        }

        // ---- r.* override yardımcıları
        void AddBoolOverride(const char* name, const char* description, int RenderOverrides::* field, std::function<bool(Scene&)> sceneValue)
        {
            ConsoleVariable v;
            v.Name = name;
            v.Description = description;
            v.Type = CVarType::Bool;
            v.Get = [field, sceneValue]() -> std::string {
                int o = S().Overrides.*field;
                if (o >= 0) return o ? "1" : "0";
                Scene* scene = ConsoleSystem::GetScene();
                return scene ? (sceneValue(*scene) ? "1" : "0") : "1";
            };
            v.Set = [field](const std::string& text) {
                bool b;
                if (!ParseBool(text, b)) return false;
                S().Overrides.*field = b ? 1 : 0;
                return true;
            };
            v.Reset = [field]() { S().Overrides.*field = -1; };
            ConsoleSystem::RegisterVariable(std::move(v));
        }

        void AddIntOverride(const char* name, const char* description, int RenderOverrides::* field, std::function<int(Scene&)> sceneValue, int minValue, int maxValue)
        {
            ConsoleVariable v;
            v.Name = name;
            v.Description = description;
            v.Type = CVarType::Int;
            v.Get = [field, sceneValue]() -> std::string {
                int o = S().Overrides.*field;
                if (o >= 0) return std::to_string(o);
                Scene* scene = ConsoleSystem::GetScene();
                return scene ? std::to_string(sceneValue(*scene)) : "0";
            };
            v.Set = [field, minValue, maxValue](const std::string& text) {
                int i;
                if (!ParseInt(text, i)) return false;
                S().Overrides.*field = std::clamp(i, minValue, maxValue);
                return true;
            };
            v.Reset = [field]() { S().Overrides.*field = -1; };
            ConsoleSystem::RegisterVariable(std::move(v));
        }

        void AddFloatOverride(const char* name, const char* description, float RenderOverrides::* field, std::function<float(Scene&)> sceneValue, float minValue, float maxValue)
        {
            ConsoleVariable v;
            v.Name = name;
            v.Description = description;
            v.Type = CVarType::Float;
            v.Get = [field, sceneValue]() -> std::string {
                float o = S().Overrides.*field;
                if (!std::isnan(o)) return FloatText(o);
                Scene* scene = ConsoleSystem::GetScene();
                return scene ? FloatText(sceneValue(*scene)) : "0";
            };
            v.Set = [field, minValue, maxValue](const std::string& text) {
                float f;
                if (!ParseFloat(text, f)) return false;
                S().Overrides.*field = std::clamp(f, minValue, maxValue);
                return true;
            };
            v.Reset = [field]() { S().Overrides.*field = NAN; };
            ConsoleSystem::RegisterVariable(std::move(v));
        }

        // Motor genelindeki değerler (getter / setter)
        void AddValue(const char* name, const char* description, CVarType type, std::function<std::string()> get, std::function<bool(const std::string&)> set)
        {
            ConsoleVariable v;
            v.Name = name;
            v.Description = description;
            v.Type = type;
            v.Get = std::move(get);
            v.Set = std::move(set);
            ConsoleSystem::RegisterVariable(std::move(v));
        }
    }

    void ConsoleSystem::RegisterVariable(ConsoleVariable variable)
    {
        std::lock_guard lock(S().Mutex);
        const std::string key = Lower(variable.Name);
        S().Variables[key] = std::move(variable);
    }

    void ConsoleSystem::RegisterCommand(const std::string& name, const std::string& description, ConsoleCommandFn run)
    {
        std::lock_guard lock(S().Mutex);
        S().Commands[Lower(name)] = CommandEntry{ name, description, std::move(run) };
    }

    ConsoleVariable* ConsoleSystem::FindVariable(const std::string& name)
    {
        RegisterBuiltins();
        auto it = S().Variables.find(Lower(name));
        return it != S().Variables.end() ? &it->second : nullptr;
    }

    bool ConsoleSystem::IsKnown(const std::string& line)
    {
        RegisterBuiltins();
        auto tokens = Split(line);
        if (tokens.empty())
            return false;
        const std::string first = Lower(tokens[0]);
        if (S().Variables.count(first))
            return true;
        if (!S().Commands.count(first))
            return false;
        // "stat" bir Unix komutu da: yalnızca bilinen istatistik adlarıyla (ya da tek başına) konsolundur
        if (first == "stat" && tokens.size() > 1)
        {
            static const char* kStats[] = { "fps", "unit", "none", "memory", "game", "physics", "rendering", "audio", "scene" };
            const std::string second = Lower(tokens[1]);
            return std::any_of(std::begin(kStats), std::end(kStats), [&](const char* s) { return second == s; });
        }
        return true;
    }

    bool ConsoleSystem::Execute(const std::string& rawLine, std::string& output)
    {
        RegisterBuiltins();
        std::lock_guard lock(S().Mutex);
        output.clear();
        std::string line = Trim(rawLine);
        // "r.Bloom=0" biçimi (ini) de kabul edilir
        if (size_t eq = line.find('='); eq != std::string::npos && line.find(' ') > eq)
            line = Trim(line.substr(0, eq)) + " " + Trim(line.substr(eq + 1));
        auto tokens = Split(line);
        if (tokens.empty())
            return true;
        const std::string key = Lower(tokens[0]);

        if (auto it = S().Variables.find(key); it != S().Variables.end())
        {
            ConsoleVariable& v = it->second;
            if (tokens.size() == 1)
            {
                output = v.Name + " = " + (v.Get ? v.Get() : std::string("?"));
                return true;
            }
            const std::string arg = line.substr(line.find(tokens[0]) + tokens[0].size());
            const std::string value = Trim(arg);
            if (value == "?")
            {
                output = v.Name + ": " + v.Description;
                return true;
            }
            if (Lower(value) == "default" || Lower(value) == "reset")
            {
                if (v.Reset) v.Reset();
                output = v.Name + " = " + (v.Get ? v.Get() : std::string("?")) + " (default)";
                return true;
            }
            if (!v.Set || !v.Set(value))
            {
                output = "Invalid value for " + v.Name + ": " + value;
                return false;
            }
            output = v.Name + " = " + (v.Get ? v.Get() : value);
            CQ_CORE_INFO("[Console] {0}", output);
            return true;
        }
        if (auto it = S().Commands.find(key); it != S().Commands.end())
        {
            std::vector<std::string> args(tokens.begin() + 1, tokens.end());
            output = it->second.Run ? it->second.Run(args) : std::string();
            return true;
        }
        output = "Unknown command: " + tokens[0] + " (type 'help' for the list)";
        return false;
    }

    std::string ConsoleSystem::ExecuteText(const std::string& line)
    {
        std::string out;
        Execute(line, out);
        return out;
    }

    std::vector<std::string> ConsoleSystem::Complete(const std::string& prefix, size_t max)
    {
        RegisterBuiltins();
        std::vector<std::string> out;
        const std::string p = Lower(prefix);
        for (auto& [key, v] : S().Variables)
            if (key.rfind(p, 0) == 0 && out.size() < max) out.push_back(v.Name);
        for (auto& [key, c] : S().Commands)
            if (key.rfind(p, 0) == 0 && out.size() < max) out.push_back(c.Name);
        return out;
    }

    std::vector<std::string> ConsoleSystem::GetVariableNames()
    {
        RegisterBuiltins();
        std::vector<std::string> out;
        for (auto& [key, v] : S().Variables) out.push_back(v.Name);
        return out;
    }

    std::vector<std::string> ConsoleSystem::GetCommandNames()
    {
        RegisterBuiltins();
        std::vector<std::string> out;
        for (auto& [key, c] : S().Commands) out.push_back(c.Name);
        return out;
    }

    std::string ConsoleSystem::GetCommandDescription(const std::string& name)
    {
        RegisterBuiltins();
        for (auto& [key, c] : S().Commands)
            if (c.Name == name)
                return c.Description;
        return {};
    }

    int ConsoleSystem::ApplyFile(const std::filesystem::path& file)
    {
        std::ifstream in(file);
        if (!in)
            return 0;
        int applied = 0;
        std::string line;
        while (std::getline(in, line))
        {
            line = Trim(line);
            if (line.empty() || line[0] == ';' || line[0] == '#' || line[0] == '[' || line.rfind("//", 0) == 0)
                continue;
            std::string out;
            if (Execute(line, out))
                ++applied;
            else
                CQ_CORE_WARN("[Console] {0}: {1}", file.filename().string(), out);
        }
        if (applied > 0)
            CQ_CORE_INFO("[Console] {0}: {1} setting(s) applied", file.filename().string(), applied);
        return applied;
    }

    void ConsoleSystem::OnProjectLoaded(const std::filesystem::path& projectDirectory)
    {
        ClearOverrides();
        if (projectDirectory.empty())
            return;
        ApplyFile(projectDirectory / "Config" / "ConsoleVariables.ini");
    }

    void ConsoleSystem::SetSceneProvider(std::function<Scene*()> provider) { S().SceneProvider = std::move(provider); }
    Scene* ConsoleSystem::GetScene() { return S().SceneProvider ? S().SceneProvider() : nullptr; }

    RenderOverrides& ConsoleSystem::Overrides() { return S().Overrides; }
    void ConsoleSystem::ClearOverrides() { S().Overrides = RenderOverrides{}; }

    void ConsoleSystem::ApplyRenderOverrides(RenderSettings& rs)
    {
        const RenderOverrides& o = S().Overrides;
        if (o.Shadows >= 0) rs.Shadows.Enabled = o.Shadows != 0;
        if (o.ShadowResolution >= 0) rs.Shadows.Resolution = o.ShadowResolution;
        if (o.ShadowCascades >= 0) rs.Shadows.CascadeCount = o.ShadowCascades;
        if (!std::isnan(o.ShadowDistance)) rs.Shadows.Distance = o.ShadowDistance;
        if (o.Fog >= 0) rs.Fog.Enabled = o.Fog != 0;
        if (!std::isnan(o.FogDensity)) rs.Fog.Density = o.FogDensity;
        if (o.SkyAtmosphere >= 0)
        {
            if (o.SkyAtmosphere != 0) rs.Environment.Sky = SkyMode::Atmosphere;
            else if (rs.Environment.Sky == SkyMode::Atmosphere) rs.Environment.Sky = SkyMode::Auto;
        }
        ApplyPostOverrides(rs.Post);
    }

    void ConsoleSystem::ApplyPostOverrides(PostProcessSettings& p)
    {
        const RenderOverrides& o = S().Overrides;
        if (o.PostProcess >= 0) p.Enabled = o.PostProcess != 0;
        if (o.Bloom >= 0) p.Bloom = o.Bloom != 0;
        if (o.SSAO >= 0) p.AmbientOcclusion = o.SSAO != 0;
        if (o.AntiAliasing >= 0) p.AntiAliasing = (AntiAliasingMode)o.AntiAliasing;
        if (o.Upscaler >= 0) p.Upscaler = (UpscalerMode)o.Upscaler;
        if (!std::isnan(o.ScreenPercentage))
        {
            p.UpscaleQuality = UpscaleQualityMode::Custom;
            p.UpscaleRenderScale = std::clamp(o.ScreenPercentage / 100.0f, 0.33f, 1.0f);
        }
        // Eski numaralar (r.AntiAliasing 20 = FSR ...): TAA + Upscaling karşılığı
        AntiAliasing::MigrateLegacy(p);
        if (o.Tonemapper >= 0) p.Tonemap = (Tonemapper)o.Tonemapper;
        if (o.Vignette >= 0) p.Vignette = o.Vignette != 0;
        if (o.FilmGrain >= 0) p.FilmGrain = o.FilmGrain != 0;
        if (o.ChromaticAberration >= 0) p.ChromaticAberration = o.ChromaticAberration != 0;
        if (o.Sharpen >= 0) p.Sharpen = o.Sharpen != 0;
        if (o.ColorGrading >= 0) p.ColorGrading = o.ColorGrading != 0;
        if (!std::isnan(o.BloomIntensity)) p.BloomIntensity = o.BloomIntensity;
        if (!std::isnan(o.Exposure)) p.Exposure = o.Exposure;
        if (!std::isnan(o.Saturation)) p.Saturation = o.Saturation;
        if (!std::isnan(o.Contrast)) p.Contrast = o.Contrast;
        if (o.SSR >= 0) p.ScreenSpaceReflections = o.SSR != 0;
        if (o.VolumetricFog >= 0) p.VolumetricFog = o.VolumetricFog != 0;
        if (o.VolumetricClouds >= 0) p.VolumetricClouds = o.VolumetricClouds != 0;
        if (o.DepthOfField >= 0) p.DepthOfField = o.DepthOfField != 0;
        if (o.MotionBlur >= 0) p.MotionBlur = o.MotionBlur != 0;
        if (o.AutoExposure >= 0) p.AutoExposure = o.AutoExposure != 0;
        if (o.SSGI >= 0) p.ScreenSpaceGI = o.SSGI != 0;
        if (!std::isnan(o.SSGIIntensity)) p.SSGIIntensity = o.SSGIIntensity;
        if (o.RTGI >= 0) p.RayTracedGI = o.RTGI != 0;
        if (!std::isnan(o.RTGIIntensity)) p.RTGIIntensity = o.RTGIIntensity;
        if (o.RTGIRays >= 0) p.RTGIRaysPerProbe = o.RTGIRays;
        if (o.TemporalStability >= 0) p.TemporalStability = o.TemporalStability != 0;
    }

    void ConsoleSystem::RegisterBuiltins()
    {
        {
            std::lock_guard lock(S().Mutex);
            if (S().BuiltinsRegistered)
                return;
            S().BuiltinsRegistered = true;
        }
        InputGlyphs::RegisterConsoleVariables();
        using R = RenderOverrides;
        // ---- Çizim (r.*): sahnenin ayarı üzerine oturum geçerliliğinde
        AddBoolOverride("r.Shadows", "Realtime shadows on/off", &R::Shadows, [](Scene& s) { return s.GetRenderSettings().Shadows.Enabled; });
        AddIntOverride("r.Shadows.Resolution", "Shadow atlas width in pixels (512..8192)", &R::ShadowResolution, [](Scene& s) { return s.GetRenderSettings().Shadows.Resolution; }, 256, 8192);
        AddIntOverride("r.Shadows.Cascades", "Directional light shadow cascades (1..4)", &R::ShadowCascades, [](Scene& s) { return s.GetRenderSettings().Shadows.CascadeCount; }, 1, 4);
        AddFloatOverride("r.Shadows.Distance", "Shadow draw distance in meters", &R::ShadowDistance, [](Scene& s) { return s.GetRenderSettings().Shadows.Distance; }, 1.0f, 5000.0f);
        AddBoolOverride("r.Fog", "Scene fog on/off", &R::Fog, [](Scene& s) { return s.GetRenderSettings().Fog.Enabled; });
        AddFloatOverride("r.Fog.Density", "Exponential fog density", &R::FogDensity, [](Scene& s) { return s.GetRenderSettings().Fog.Density; }, 0.0f, 10.0f);
        AddBoolOverride("r.PostProcess", "Whole post processing stack on/off", &R::PostProcess, [](Scene& s) { return s.GetRenderSettings().Post.Enabled; });
        AddBoolOverride("r.Bloom", "Bloom on/off", &R::Bloom, [](Scene& s) { return s.GetRenderSettings().Post.Bloom; });
        AddFloatOverride("r.Bloom.Intensity", "Bloom intensity", &R::BloomIntensity, [](Scene& s) { return s.GetRenderSettings().Post.BloomIntensity; }, 0.0f, 20.0f);
        AddBoolOverride("r.SSAO", "Screen space ambient occlusion on/off", &R::SSAO, [](Scene& s) { return s.GetRenderSettings().Post.AmbientOcclusion; });
        AddIntOverride("r.TemporalStability", "Temporal stability for single-frame AA methods: 1 on (the method's result is accumulated over sub-pixel jittered frames like TAA; SMAA T2x averages only flickering pixels), 0 off",
                       &R::TemporalStability, [](Scene& s) { return s.GetRenderSettings().Post.TemporalStability ? 1 : 0; }, 0, 1);
        AddIntOverride("r.TAADebug", "TAA debug view: 0 off, 1 per-pixel history path (red moving, green settled, cyan calm, blue trusted, yellow clamped, magenta sky, white no history)", &R::TAADebug, [](Scene&) { return 0; }, 0, 1);
        AddIntOverride("r.AntiAliasing", "Anti-aliasing method: 0 off, 1 FXAA, 2 SMAA, 3 TAA, 4 TAA+FXAA, 5 MSAA, 6 CSAA, 7 SSAA, 8 FSAA, 9 EQAA, 10 MLAA, 11 CMAA, "
                       "12 TXAA, 13 TSAA, 15 SMAA T2x, 16 TSRAA, 19 DLAA, 26 FSR AA, 27 XeSS AA, 28 OGSSAA, 29 SGSSAA, 30 RGSS, 31 TSSAA, 32 DEAA, 33 GBAA, 34 SRAA "
                       "(a method this system cannot run falls back to the closest one; old upscaler numbers 14, 17, 18, 20-25 map to TAA + r.Upscaler)",
                       &R::AntiAliasing, [](Scene& s) { return (int)s.GetRenderSettings().Post.AntiAliasing; }, 0, (int)AntiAliasingMode::Count - 1);
        AddIntOverride("r.Upscaler", "Upscaler: 0 off (native), 1 FSR 1, 2 CAS, 3 NIS, 4 TAAU, 5 TSR, 6 DLSS, 7 XeSS, 8 MetalFX, 9 PSSR, 10 FSR 2 "
                       "(an upscaler this system cannot run falls back to FSR 1)",
                       &R::Upscaler, [](Scene& s) { return (int)s.GetRenderSettings().Post.Upscaler; }, 0, (int)UpscalerMode::Count - 1);
        AddFloatOverride("r.ScreenPercentage", "Upscaler internal resolution in percent (33 .. 100); sets the quality mode to Custom",
                         &R::ScreenPercentage, [](Scene& s) {
                             const auto& p = s.GetRenderSettings().Post;
                             return 100.0f * AntiAliasing::GetQualityModeScale(p.UpscaleQuality, p.UpscaleRenderScale);
                         }, 33.0f, 100.0f);
        AddBoolOverride("r.SSR", "Screen space reflections on/off", &R::SSR, [](Scene& s) { return s.GetRenderSettings().Post.ScreenSpaceReflections; });
        AddBoolOverride("r.VolumetricFog", "Volumetric fog (shadowed light shafts) on/off", &R::VolumetricFog, [](Scene& s) { return s.GetRenderSettings().Post.VolumetricFog; });
        AddBoolOverride("r.VolumetricClouds", "Volumetric clouds on/off", &R::VolumetricClouds, [](Scene& s) { return s.GetRenderSettings().Post.VolumetricClouds; });
        AddBoolOverride("r.DepthOfField", "Depth of field on/off", &R::DepthOfField, [](Scene& s) { return s.GetRenderSettings().Post.DepthOfField; });
        AddBoolOverride("r.MotionBlur", "Camera motion blur on/off", &R::MotionBlur, [](Scene& s) { return s.GetRenderSettings().Post.MotionBlur; });
        AddBoolOverride("r.AutoExposure", "Eye adaptation (auto exposure) on/off", &R::AutoExposure, [](Scene& s) { return s.GetRenderSettings().Post.AutoExposure; });
        AddBoolOverride("r.SSGI", "Screen space global illumination (Lumen-style screen traces) on/off", &R::SSGI, [](Scene& s) { return s.GetRenderSettings().Post.ScreenSpaceGI; });
        AddFloatOverride("r.SSGI.Intensity", "Screen space GI bounce strength", &R::SSGIIntensity, [](Scene& s) { return s.GetRenderSettings().Post.SSGIIntensity; }, 0.0f, 8.0f);
        AddBoolOverride("r.RTGI", "Ray traced global illumination (Lumen-style probes traced against the scene) on/off", &R::RTGI, [](Scene& s) { return s.GetRenderSettings().Post.RayTracedGI; });
        AddFloatOverride("r.RTGI.Intensity", "Ray traced GI indirect lighting intensity", &R::RTGIIntensity, [](Scene& s) { return s.GetRenderSettings().Post.RTGIIntensity; }, 0.0f, 8.0f);
        AddIntOverride("r.RTGI.Rays", "Ray traced GI rays per probe (32..256)", &R::RTGIRays, [](Scene& s) { return s.GetRenderSettings().Post.RTGIRaysPerProbe; }, 32, 256);
        AddBoolOverride("r.SkyAtmosphere", "Physically based sky atmosphere on/off", &R::SkyAtmosphere, [](Scene& s) { return s.GetRenderSettings().Environment.Sky == SkyMode::Atmosphere; });
        AddIntOverride("r.Tonemapper", "0 none, 1 Neutral, 2 ACES, 3 AgX, 4 Reinhard", &R::Tonemapper, [](Scene& s) { return (int)s.GetRenderSettings().Post.Tonemap; }, 0, 4);
        AddFloatOverride("r.Exposure", "Exposure in EV", &R::Exposure, [](Scene& s) { return s.GetRenderSettings().Post.Exposure; }, -16.0f, 16.0f);
        AddFloatOverride("r.Saturation", "Color grading saturation", &R::Saturation, [](Scene& s) { return s.GetRenderSettings().Post.Saturation; }, 0.0f, 4.0f);
        AddFloatOverride("r.Contrast", "Color grading contrast", &R::Contrast, [](Scene& s) { return s.GetRenderSettings().Post.Contrast; }, 0.0f, 4.0f);
        AddBoolOverride("r.ColorGrading", "Color grading on/off", &R::ColorGrading, [](Scene& s) { return s.GetRenderSettings().Post.ColorGrading; });
        AddBoolOverride("r.Vignette", "Vignette on/off", &R::Vignette, [](Scene& s) { return s.GetRenderSettings().Post.Vignette; });
        AddBoolOverride("r.FilmGrain", "Film grain on/off", &R::FilmGrain, [](Scene& s) { return s.GetRenderSettings().Post.FilmGrain; });
        AddBoolOverride("r.ChromaticAberration", "Chromatic aberration on/off", &R::ChromaticAberration, [](Scene& s) { return s.GetRenderSettings().Post.ChromaticAberration; });
        AddBoolOverride("r.Sharpen", "Sharpen filter on/off", &R::Sharpen, [](Scene& s) { return s.GetRenderSettings().Post.Sharpen; });

        AddValue("r.ForceLOD", "Force a mesh LOD level on every Auto LOD mesh (-1 = automatic by screen size)", CVarType::Int,
            []() { return std::to_string(Renderer3D::GetForcedLOD()); },
            [](const std::string& t) { int i; if (!ParseInt(t, i)) return false; Renderer3D::SetForcedLOD(std::clamp(i, -1, 4)); return true; });
        AddValue("r.FrustumCulling", "Skip meshes outside the camera view and outside each shadow cascade (0 = draw everything, for debugging)",
            CVarType::Bool,
            []() { return Renderer3D::GetFrustumCulling() ? std::string("1") : std::string("0"); },
            [](const std::string& t) { bool b; if (!ParseBool(t, b)) return false; Renderer3D::SetFrustumCulling(b); return true; });
        AddValue("r.Nanite", "Virtual Geometry: 1 GPU-driven clusters (LOD, frustum and occlusion culling on the GPU, instances batched), 0 CPU selection",
            CVarType::Bool,
            []() { return ClusterRenderer::GetEnabled() ? std::string("1") : std::string("0"); },
            [](const std::string& t) { bool b; if (!ParseBool(t, b)) return false; ClusterRenderer::SetEnabled(b); return true; });
        AddValue("r.Nanite.Occlusion", "Virtual Geometry two-pass occlusion culling with a depth pyramid (HZB); 0 draws every cluster in view",
            CVarType::Bool,
            []() { return ClusterRenderer::GetOcclusionCulling() ? std::string("1") : std::string("0"); },
            [](const std::string& t) { bool b; if (!ParseBool(t, b)) return false; ClusterRenderer::SetOcclusionCulling(b); return true; });
        AddValue("r.OpaqueSort", "Opaque draw order: 1 front to back, alpha-tested (cutout) after opaque (default); 0 submission order",
            CVarType::Int,
            []() { return std::to_string(Renderer3D::GetOpaqueSorting()); },
            [](const std::string& t) { int i; if (!ParseInt(t, i) || i < 0 || i > 1) return false; Renderer3D::SetOpaqueSorting(i); return true; });

        // ---- Motor geneli
        AddValue("ui.OnScreenKeyboard", "On-screen keyboard for text fields: 0 auto (gamepad / Steam Deck / touch), 1 always, 2 off", CVarType::Int,
            []() { return std::to_string(UISystem::GetOnScreenKeyboardMode()); },
            [](const std::string& t) { int i; if (!ParseInt(t, i) || i < 0 || i > 2) return false; UISystem::SetOnScreenKeyboardMode(i); return true; });
        AddValue("r.Quality", "Quality level: 0 Low, 1 Medium, 2 High, 3 Ultra", CVarType::Int,
            []() { return std::to_string((int)QualitySettings::GetQualityLevel()); },
            [](const std::string& t) { int i; if (!ParseInt(t, i)) return false; QualitySettings::SetQualityLevel((QualityLevel)std::clamp(i, 0, 3)); return true; });
        AddValue("r.VSync", "Vertical sync on/off", CVarType::Bool,
            []() { return Application::Exists() && Application::Get().GetWindow().IsVSync() ? std::string("1") : std::string("0"); },
            [](const std::string& t) { bool b; if (!ParseBool(t, b) || !Application::Exists()) return false; Application::Get().GetWindow().SetVSync(b); return true; });
        AddValue("t.MaxFPS", "Frame rate limit (0 = unlimited)", CVarType::Int,
            []() { return std::to_string(ProjectSettings::Quality().TargetFrameRate); },
            [](const std::string& t) { int i; if (!ParseInt(t, i)) return false; ProjectSettings::Quality().TargetFrameRate = std::max(0, i); return true; });
        AddValue("t.TimeScale", "Game time scale (1 = normal speed)", CVarType::Float,
            []() { return FloatText(TimeManager::GetTimeScale()); },
            [](const std::string& t) { float f; if (!ParseFloat(t, f)) return false; TimeManager::SetTimeScale(std::max(0.0f, f)); return true; });
        AddValue("au.MasterVolume", "Master audio volume (0..1+)", CVarType::Float,
            []() { return FloatText(AudioEngine::IsInitialized() ? AudioEngine::Get().GetMasterVolume() : 1.0f); },
            [](const std::string& t) { float f; if (!ParseFloat(t, f) || !AudioEngine::IsInitialized()) return false; AudioEngine::Get().SetMasterVolume(std::max(0.0f, f)); return true; });
        AddValue("p.GravityY", "Vertical gravity in m/s^2 (3D physics)", CVarType::Float,
            []() { return FloatText(PhysicsSettings::Get().Gravity.y); },
            [](const std::string& t) {
                float f;
                if (!ParseFloat(t, f)) return false;
                PhysicsSettings::Get().Gravity.y = f;
                if (Scene* scene = ConsoleSystem::GetScene(); scene && scene->GetPhysicsWorld3D())
                    scene->GetPhysicsWorld3D()->SetGravity(PhysicsSettings::Get().Gravity);
                return true;
            });
        AddValue("ai.ShowNavMesh", "Draw the baked navigation mesh", CVarType::Bool,
            []() { Scene* s = ConsoleSystem::GetScene(); return s && s->IsShowingNavMesh() ? std::string("1") : std::string("0"); },
            [](const std::string& t) { bool b; Scene* s = ConsoleSystem::GetScene(); if (!ParseBool(t, b) || !s) return false; s->SetShowNavMesh(b); return true; });

        // ---- Komutlar
        RegisterCommand("help", "List console variables and commands (optional prefix): help r.", [](const std::vector<std::string>& args) {
            const std::string prefix = args.empty() ? std::string() : Lower(args[0]);
            std::string out;
            for (auto& [key, v] : S().Variables)
                if (key.rfind(prefix, 0) == 0) out += v.Name + " = " + (v.Get ? v.Get() : "") + "   " + v.Description + "\n";
            for (auto& [key, c] : S().Commands)
                if (key.rfind(prefix, 0) == 0) out += c.Name + "   " + c.Description + "\n";
            if (!out.empty() && out.back() == '\n') out.pop_back();
            return out.empty() ? std::string("No match.") : out;
        });
        RegisterCommand("slomo", "Set the game time scale: slomo 0.5", [](const std::vector<std::string>& args) {
            float f = 1.0f;
            if (!args.empty() && !ParseFloat(args[0], f)) return std::string("Usage: slomo <scale>");
            TimeManager::SetTimeScale(std::max(0.0f, f));
            return "Time scale = " + FloatText(TimeManager::GetTimeScale());
        });
        RegisterCommand("stat", "Show statistics: stat fps | unit | none", [](const std::vector<std::string>& args) {
            const float dt = std::max(1e-6f, TimeManager::GetUnscaledDeltaTime());
            const std::string what = args.empty() ? "fps" : Lower(args[0]);
            if (what == "none") return std::string("Stats hidden.");
            std::ostringstream o;
            o.setf(std::ios::fixed);
            o.precision(1);
            o << "FPS " << (1.0f / dt) << "   Frame " << dt * 1000.0f << " ms   Time scale " << TimeManager::GetTimeScale();
            return o.str();
        });
        RegisterCommand("dumpcvars", "List every console variable with its value (optional prefix)", [](const std::vector<std::string>& args) {
            const std::string prefix = args.empty() ? std::string() : Lower(args[0]);
            std::string out;
            for (auto& [key, v] : S().Variables)
                if (key.rfind(prefix, 0) == 0) out += v.Name + " = " + (v.Get ? v.Get() : "") + "\n";
            if (!out.empty()) out.pop_back();
            return out;
        });
        RegisterCommand("r.ResetOverrides", "Return every r.* variable to the scene's own settings", [](const std::vector<std::string>&) {
            ConsoleSystem::ClearOverrides();
            return std::string("Render overrides cleared.");
        });
        RegisterCommand("exec", "Run a file of console lines: exec Config/Low.ini", [](const std::vector<std::string>& args) {
            if (args.empty()) return std::string("Usage: exec <file>");
            std::filesystem::path p(args[0]);
            std::error_code ec;
            if (p.is_relative() && !std::filesystem::exists(p, ec) && !Project::GetActiveProjectDirectory().empty())
                p = Project::GetActiveProjectDirectory() / p;
            int n = ConsoleSystem::ApplyFile(p);
            return std::to_string(n) + " line(s) applied from " + p.string();
        });
    }
}
