#include "ProjectSettings.h"
#include "Core/Localization/Localization.h"
#include "Core/Audio/AudioEngine.h"
#include "Core/Logging/Log.h"
#include "Core/Input/Input.h"
#include "Core/Input/InputActionRuntime.h"
#include "Core/Utils/FileSystem.h"
#include "Scene/Scene.h"

#include <yaml-cpp/yaml.h>

#include <algorithm>

namespace Conqueror
{
    NavigationProjectSettings::NavigationProjectSettings()
    {
        for (size_t i = 0; i < Areas.size(); ++i)
            Areas[i] = { i == 0 ? "Walkable" : i == 1 ? "Not Walkable" : i == 2 ? "Jump" : std::string(), i == 2 ? 2.0f : 1.0f };
    }

    namespace
    {
        AudioProjectSettings s_Audio;
        QualityProjectSettings s_Quality;
        GraphicsProjectSettings s_Graphics;
        NavigationProjectSettings s_Navigation;
        EditorProjectSettings s_Editor;
        std::vector<ScriptOrderEntry> s_ScriptOrder;
        std::vector<AutoloadEntry> s_Autoloads;
        std::unordered_map<std::string, int> s_OrderCache;
        bool s_OrderCacheValid = false;

        template<typename T>
        void Read(const YAML::Node& n, const char* key, T& value)
        {
            if (n && n[key])
            {
                try { value = n[key].as<T>(); }
                catch (const YAML::Exception&) {}
            }
        }

        void WritePreset(YAML::Emitter& out, const QualityPreset& p)
        {
            out << YAML::BeginMap;
            out << YAML::Key << "ShadowResolution" << YAML::Value << p.ShadowResolution;
            out << YAML::Key << "ShadowCascades" << YAML::Value << p.ShadowCascades;
            out << YAML::Key << "ShadowDistance" << YAML::Value << p.ShadowDistance;
            out << YAML::Key << "EnableSSAO" << YAML::Value << p.EnableSSAO;
            out << YAML::Key << "EnableBloom" << YAML::Value << p.EnableBloom;
            out << YAML::Key << "AntiAliasing" << YAML::Value << p.AntiAliasing;
            out << YAML::Key << "LODBias" << YAML::Value << p.LODBias;
            out << YAML::Key << "TextureQuality" << YAML::Value << p.TextureQuality;
            out << YAML::Key << "AnisotropicFiltering" << YAML::Value << p.AnisotropicFiltering;
            out << YAML::Key << "MaxParticles" << YAML::Value << p.MaxParticles;
            out << YAML::EndMap;
        }

        void ReadPreset(const YAML::Node& n, QualityPreset& p)
        {
            if (!n || !n.IsMap())
                return;
            Read(n, "ShadowResolution", p.ShadowResolution);
            Read(n, "ShadowCascades", p.ShadowCascades);
            Read(n, "ShadowDistance", p.ShadowDistance);
            Read(n, "EnableSSAO", p.EnableSSAO);
            Read(n, "EnableBloom", p.EnableBloom);
            Read(n, "AntiAliasing", p.AntiAliasing);
            Read(n, "LODBias", p.LODBias);
            Read(n, "TextureQuality", p.TextureQuality);
            Read(n, "AnisotropicFiltering", p.AnisotropicFiltering);
            Read(n, "MaxParticles", p.MaxParticles);
        }
    }

    AudioProjectSettings& ProjectSettings::Audio() { return s_Audio; }
    QualityProjectSettings& ProjectSettings::Quality() { return s_Quality; }
    GraphicsProjectSettings& ProjectSettings::Graphics() { return s_Graphics; }
    NavigationProjectSettings& ProjectSettings::Navigation() { return s_Navigation; }
    EditorProjectSettings& ProjectSettings::Editor() { return s_Editor; }
    std::vector<ScriptOrderEntry>& ProjectSettings::ScriptOrder() { return s_ScriptOrder; }
    std::vector<AutoloadEntry>& ProjectSettings::Autoloads() { return s_Autoloads; }
    std::vector<std::string>& ProjectSettings::ScriptDefines()
    {
        static std::vector<std::string> s_Defines;
        return s_Defines;
    }
    std::vector<std::string>& ProjectSettings::CustomTags()
    {
        static std::vector<std::string> tags;
        return tags;
    }

    bool& ProjectSettings::CustomInput()
    {
        static bool custom = false;
        return custom;
    }

    std::string& ProjectSettings::ProjectWideActions()
    {
        static std::string path;
        return path;
    }

    int ProjectSettings::GetScriptOrder(const std::string& className)
    {
        if (s_ScriptOrder.empty() || className.empty())
            return 0;
        if (!s_OrderCacheValid)
        {
            s_OrderCache.clear();
            for (auto& e : s_ScriptOrder)
                s_OrderCache[e.ClassName] = e.Order;
            s_OrderCacheValid = true;
        }
        auto it = s_OrderCache.find(className);
        return it != s_OrderCache.end() ? it->second : 0;
    }

    void ProjectSettings::InvalidateScriptOrderCache() { s_OrderCacheValid = false; }

    void ProjectSettings::ResetToDefaults()
    {
        s_Audio = {};
        s_Quality = {};
        s_Graphics = {};
        s_Navigation = {};
        s_Editor = {};
        s_ScriptOrder.clear();
        s_Autoloads.clear();
        ScriptDefines().clear();
        s_OrderCacheValid = false;
        CustomTags().clear();
        ProjectWideActions().clear();
        Localization::ResetToDefaults();
    }

    void ProjectSettings::Serialize(YAML::Emitter& out)
    {
        out << YAML::Key << "Audio" << YAML::Value << YAML::BeginMap;
        out << YAML::Key << "GlobalVolume" << YAML::Value << s_Audio.GlobalVolume;
        out << YAML::Key << "DopplerFactor" << YAML::Value << s_Audio.DopplerFactor;
        out << YAML::Key << "RolloffScale" << YAML::Value << s_Audio.RolloffScale;
        out << YAML::Key << "DisableAudio" << YAML::Value << s_Audio.DisableAudio;
        out << YAML::EndMap;

        out << YAML::Key << "Quality" << YAML::Value << YAML::BeginMap;
        out << YAML::Key << "Level" << YAML::Value << s_Quality.Level;
        out << YAML::Key << "TargetFrameRate" << YAML::Value << s_Quality.TargetFrameRate;
        out << YAML::Key << "Custom" << YAML::Value;
        WritePreset(out, s_Quality.Custom);
        {
            const auto& ts = s_Quality.Streaming;
            out << YAML::Key << "TextureStreaming" << YAML::Value << YAML::BeginMap;
            out << YAML::Key << "Enabled" << YAML::Value << ts.Enabled;
            out << YAML::Key << "AddAllCameras" << YAML::Value << ts.AddAllCameras;
            out << YAML::Key << "MemoryBudgetMB" << YAML::Value << ts.MemoryBudgetMB;
            out << YAML::Key << "RenderersPerFrame" << YAML::Value << ts.RenderersPerFrame;
            out << YAML::Key << "MaxLevelReduction" << YAML::Value << ts.MaxLevelReduction;
            out << YAML::Key << "MaxIORequests" << YAML::Value << ts.MaxIORequests;
            out << YAML::EndMap;
        }
        out << YAML::EndMap;

        out << YAML::Key << "Graphics" << YAML::Value << YAML::BeginMap;
        out << YAML::Key << "DefaultTonemapper" << YAML::Value << s_Graphics.DefaultTonemapper;
        out << YAML::Key << "DefaultSky" << YAML::Value << s_Graphics.DefaultSky;
        out << YAML::Key << "PreloadedShaders" << YAML::Value << YAML::BeginSeq;
        for (const auto& p : s_Graphics.PreloadedShaders) out << p;
        out << YAML::EndSeq;
        out << YAML::EndMap;

        out << YAML::Key << "Navigation" << YAML::Value << YAML::BeginMap;
        out << YAML::Key << "Areas" << YAML::Value << YAML::BeginSeq;
        for (size_t i = 0; i < s_Navigation.Areas.size(); ++i)
        {
            const auto& a = s_Navigation.Areas[i];
            if (a.Name.empty() && a.Cost == 1.0f)
                continue;
            out << YAML::Flow << YAML::BeginMap << YAML::Key << "Index" << YAML::Value << (int)i << YAML::Key << "Name" << YAML::Value << a.Name
                << YAML::Key << "Cost" << YAML::Value << a.Cost << YAML::EndMap;
        }
        out << YAML::EndSeq;
        out << YAML::EndMap;

        out << YAML::Key << "Editor" << YAML::Value << YAML::BeginMap;
        out << YAML::Key << "DefaultBehaviorMode" << YAML::Value << s_Editor.DefaultBehaviorMode;
        out << YAML::Key << "NewSceneWithCameraAndLight" << YAML::Value << s_Editor.NewSceneWithCameraAndLight;
        out << YAML::Key << "AutoSaveMinutes" << YAML::Value << s_Editor.AutoSaveMinutes;
        out << YAML::Key << "ExternalScriptEditor" << YAML::Value << s_Editor.ExternalScriptEditor;
        out << YAML::EndMap;

        out << YAML::Key << "ScriptExecutionOrder" << YAML::Value << YAML::BeginSeq;
        for (const auto& e : s_ScriptOrder)
            out << YAML::Flow << YAML::BeginMap << YAML::Key << "Class" << YAML::Value << e.ClassName << YAML::Key << "Order" << YAML::Value << e.Order << YAML::EndMap;
        out << YAML::EndSeq;
        if (!s_Autoloads.empty())
        {
            out << YAML::Key << "Autoload" << YAML::Value << YAML::BeginSeq;
            for (const auto& a : s_Autoloads)
                out << YAML::Flow << YAML::BeginMap << YAML::Key << "Name" << YAML::Value << a.Name << YAML::Key << "Path" << YAML::Value << a.Path
                    << YAML::Key << "Enabled" << YAML::Value << a.Enabled << YAML::EndMap;
            out << YAML::EndSeq;
        }

        out << YAML::Key << "Tags" << YAML::Value << YAML::Flow << CustomTags();
        out << YAML::Key << "ScriptDefines" << YAML::Value << YAML::Flow << ScriptDefines();

        if (CustomInput())
            out << YAML::Key << "InputConfig" << YAML::Value << YAML::Literal << Input::SaveConfigToString();
        if (!ProjectWideActions().empty())
            out << YAML::Key << "ProjectWideActions" << YAML::Value << ProjectWideActions();
        Localization::Serialize(out);
    }

    void ProjectSettings::Deserialize(const YAML::Node& root)
    {
        ResetToDefaults();
        if (auto a = root["Audio"])
        {
            Read(a, "GlobalVolume", s_Audio.GlobalVolume);
            Read(a, "DopplerFactor", s_Audio.DopplerFactor);
            Read(a, "RolloffScale", s_Audio.RolloffScale);
            Read(a, "DisableAudio", s_Audio.DisableAudio);
        }
        if (auto q = root["Quality"])
        {
            Read(q, "Level", s_Quality.Level);
            Read(q, "TargetFrameRate", s_Quality.TargetFrameRate);
            ReadPreset(q["Custom"], s_Quality.Custom);
            s_Quality.Level = std::clamp(s_Quality.Level, 0, (int)QualityLevel::Custom);
            if (auto t = q["TextureStreaming"])
            {
                auto& ts = s_Quality.Streaming;
                Read(t, "Enabled", ts.Enabled);
                Read(t, "AddAllCameras", ts.AddAllCameras);
                Read(t, "MemoryBudgetMB", ts.MemoryBudgetMB);
                Read(t, "RenderersPerFrame", ts.RenderersPerFrame);
                Read(t, "MaxLevelReduction", ts.MaxLevelReduction);
                Read(t, "MaxIORequests", ts.MaxIORequests);
                ts.MemoryBudgetMB = std::max(ts.MemoryBudgetMB, 1.0f);
                ts.RenderersPerFrame = std::clamp(ts.RenderersPerFrame, 1, 1 << 20);
                ts.MaxLevelReduction = std::clamp(ts.MaxLevelReduction, 0, 7);
                ts.MaxIORequests = std::clamp(ts.MaxIORequests, 1, 1 << 16);
            }
        }
        if (auto g = root["Graphics"])
        {
            Read(g, "DefaultTonemapper", s_Graphics.DefaultTonemapper);
            Read(g, "DefaultSky", s_Graphics.DefaultSky);
            s_Graphics.PreloadedShaders.clear();
            if (auto p = g["PreloadedShaders"]; p && p.IsSequence())
                for (const auto& x : p) s_Graphics.PreloadedShaders.push_back(x.as<std::string>());
        }
        if (auto n = root["Navigation"]; n && n["Areas"] && n["Areas"].IsSequence())
            for (const auto& a : n["Areas"])
            {
                int index = -1;
                Read(a, "Index", index);
                if (index < 0 || index >= (int)s_Navigation.Areas.size())
                    continue;
                Read(a, "Name", s_Navigation.Areas[(size_t)index].Name);
                Read(a, "Cost", s_Navigation.Areas[(size_t)index].Cost);
            }
        if (auto e = root["Editor"])
        {
            Read(e, "DefaultBehaviorMode", s_Editor.DefaultBehaviorMode);
            Read(e, "NewSceneWithCameraAndLight", s_Editor.NewSceneWithCameraAndLight);
            Read(e, "AutoSaveMinutes", s_Editor.AutoSaveMinutes);
            Read(e, "ExternalScriptEditor", s_Editor.ExternalScriptEditor);
        }
        // Giriş haritası: projede varsa onu, yoksa motorun varsayılanını yükle (proje değişince sıfırlanır)
        if (auto in = root["InputConfig"]; in && in.IsScalar())
        {
            Input::ClearContexts();
            Input::LoadConfigFromString(in.as<std::string>());
            CustomInput() = true;
        }
        else
        {
            if (CustomInput())
            {
                Input::ClearContexts();
                Input::LoadConfig(FileSystem::ResolveResourcePath("Engine/src/Core/Input/default.cqinput"));
            }
            CustomInput() = false;
        }
        if (auto pw = root["ProjectWideActions"]; pw && pw.IsScalar())
            ProjectWideActions() = pw.as<std::string>();
        if (auto d = root["ScriptDefines"]; d && d.IsSequence())
            for (const auto& symbol : d)
            {
                try { ScriptDefines().push_back(symbol.as<std::string>()); }
                catch (const YAML::Exception&) {}
            }
        if (auto t = root["Tags"]; t && t.IsSequence())
            for (const auto& tag : t)
            {
                try { CustomTags().push_back(tag.as<std::string>()); }
                catch (const YAML::Exception&) {}
            }
        if (auto o = root["ScriptExecutionOrder"]; o && o.IsSequence())
            for (const auto& e : o)
            {
                ScriptOrderEntry entry;
                Read(e, "Class", entry.ClassName);
                Read(e, "Order", entry.Order);
                if (!entry.ClassName.empty())
                    s_ScriptOrder.push_back(entry);
            }
        s_OrderCacheValid = false;
        s_Autoloads.clear();
        if (auto al = root["Autoload"]; al && al.IsSequence())
            for (const auto& e : al)
            {
                AutoloadEntry entry;
                Read(e, "Name", entry.Name);
                Read(e, "Path", entry.Path);
                Read(e, "Enabled", entry.Enabled);
                if (!entry.Name.empty() || !entry.Path.empty())
                    s_Autoloads.push_back(entry);
            }
        Localization::Deserialize(root);
    }

    void ProjectSettings::Apply()
    {
        // Kalite
        if (s_Quality.Level == (int)QualityLevel::Custom)
            QualitySettings::SetCustomPreset(s_Quality.Custom);
        else
            QualitySettings::SetQualityLevel((QualityLevel)s_Quality.Level);
        // Ses
        if (AudioEngine::IsInitialized())
            AudioEngine::Get().SetMasterVolume(s_Audio.DisableAudio ? 0.0f : std::clamp(s_Audio.GlobalVolume, 0.0f, 4.0f));
        Scene::SetCustomTags(CustomTags());
        s_OrderCacheValid = false;
        InputActionRuntime::SetProjectWideAsset(ProjectWideActions());
    }
}
