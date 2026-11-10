#include "ProjectSerializer.h"
#include "Core/PhysicsSystem/PhysicsSettings.h"
#include "ProjectSettings.h"
#include <algorithm>
#include <fstream>
#include <yaml-cpp/yaml.h>

namespace Conqueror {

    ProjectSerializer::ProjectSerializer(Ref<Project> project)
        : m_Project(project)
    {
    }

    bool ProjectSerializer::Serialize(const std::filesystem::path& filepath)
    {
        const auto& config = m_Project->GetConfig();

        YAML::Emitter out;
        out << YAML::BeginMap;
        out << YAML::Key << "Project" << YAML::Value << YAML::BeginMap;
        out << YAML::Key << "Name" << YAML::Value << config.Name;
        out << YAML::Key << "Organization" << YAML::Value << config.Organization;
        out << YAML::Key << "Version" << YAML::Value << config.Version;
        out << YAML::Key << "TargetPlatform" << YAML::Value << config.TargetPlatform;
        out << YAML::Key << "StartScene" << YAML::Value << config.StartScene;
        out << YAML::Key << "AssetDirectory" << YAML::Value << config.AssetDirectory;
        out << YAML::Key << "ScriptModulePath" << YAML::Value << config.ScriptModulePath;
        out << YAML::EndMap;

        const auto& player = config.Player;
        out << YAML::Key << "Player" << YAML::Value << YAML::BeginMap;
        out << YAML::Key << "Width" << YAML::Value << player.Width;
        out << YAML::Key << "Height" << YAML::Value << player.Height;
        out << YAML::Key << "FullscreenMode" << YAML::Value << player.FullscreenMode;
        out << YAML::Key << "Resizable" << YAML::Value << player.Resizable;
        out << YAML::Key << "VSync" << YAML::Value << player.VSync;
        out << YAML::Key << "RunInBackground" << YAML::Value << player.RunInBackground;
        out << YAML::Key << "Icon" << YAML::Value << player.Icon;
        out << YAML::Key << "GraphicsAPI" << YAML::Value << player.GraphicsAPI;
        out << YAML::Key << "Splash" << YAML::Value << YAML::BeginMap;
        out << YAML::Key << "Show" << YAML::Value << player.Splash.Show;
        out << YAML::Key << "Logo" << YAML::Value << (player.Splash.Logos.empty() ? player.Splash.Logo : player.Splash.Logos.front().Path);
        out << YAML::Key << "Logos" << YAML::Value << YAML::BeginSeq;
        for (const auto& logo : player.Splash.Logos)
            out << YAML::Flow << YAML::BeginMap << YAML::Key << "Path" << YAML::Value << logo.Path << YAML::Key << "Duration" << YAML::Value
                << logo.Duration << YAML::EndMap;
        out << YAML::EndSeq;
        out << YAML::Key << "ShowEngineLogo" << YAML::Value << player.Splash.ShowEngineLogo;
        out << YAML::Key << "DrawMode" << YAML::Value << player.Splash.DrawMode;
        out << YAML::Key << "Animation" << YAML::Value << player.Splash.Animation;
        out << YAML::Key << "LogoScale" << YAML::Value << player.Splash.LogoScale;
        out << YAML::Key << "BackgroundImage" << YAML::Value << player.Splash.BackgroundImage;
        out << YAML::Key << "Duration" << YAML::Value << player.Splash.Duration;
        out << YAML::Key << "Background" << YAML::Value << YAML::Flow << YAML::BeginSeq << player.Splash.BackgroundR << player.Splash.BackgroundG
            << player.Splash.BackgroundB << YAML::EndSeq;
        out << YAML::EndMap;
        // Platforma özel ayarlar
        out << YAML::Key << "macOS" << YAML::Value << YAML::BeginMap;
        out << YAML::Key << "BundleIdentifier" << YAML::Value << player.Mac.BundleIdentifier;
        out << YAML::Key << "BuildNumber" << YAML::Value << player.Mac.BuildNumber;
        out << YAML::Key << "MinimumVersion" << YAML::Value << player.Mac.MinimumVersion;
        out << YAML::Key << "Category" << YAML::Value << player.Mac.Category;
        out << YAML::Key << "CameraUsage" << YAML::Value << player.Mac.CameraUsage;
        out << YAML::Key << "MicrophoneUsage" << YAML::Value << player.Mac.MicrophoneUsage;
        out << YAML::EndMap;
        out << YAML::Key << "Linux" << YAML::Value << YAML::BeginMap;
        out << YAML::Key << "DisplayServer" << YAML::Value << player.Linux.DisplayServer;
        out << YAML::EndMap;
        out << YAML::Key << "BSD" << YAML::Value << YAML::BeginMap;
        out << YAML::Key << "DisplayServer" << YAML::Value << player.BSD.DisplayServer;
        out << YAML::EndMap;
        out << YAML::Key << "Android" << YAML::Value << YAML::BeginMap;
        out << YAML::Key << "PackageName" << YAML::Value << player.Android.PackageName;
        out << YAML::Key << "VersionCode" << YAML::Value << player.Android.VersionCode;
        out << YAML::Key << "Orientation" << YAML::Value << player.Android.Orientation;
        out << YAML::Key << "ARM64" << YAML::Value << player.Android.ARM64;
        out << YAML::Key << "X86_64" << YAML::Value << player.Android.X86_64;
        out << YAML::Key << "Keystore" << YAML::Value << player.Android.Keystore;
        out << YAML::Key << "KeyAlias" << YAML::Value << player.Android.KeyAlias;
        out << YAML::EndMap;
        out << YAML::Key << "iOS" << YAML::Value << YAML::BeginMap;
        out << YAML::Key << "BundleIdentifier" << YAML::Value << player.IOS.BundleIdentifier;
        out << YAML::Key << "BuildNumber" << YAML::Value << player.IOS.BuildNumber;
        out << YAML::Key << "TargetDevice" << YAML::Value << player.IOS.TargetDevice;
        out << YAML::Key << "MinimumVersion" << YAML::Value << player.IOS.MinimumVersion;
        out << YAML::Key << "Orientation" << YAML::Value << player.IOS.Orientation;
        out << YAML::Key << "TeamID" << YAML::Value << player.IOS.TeamID;
        out << YAML::Key << "CameraUsage" << YAML::Value << player.IOS.CameraUsage;
        out << YAML::Key << "RequiresARKit" << YAML::Value << player.IOS.RequiresARKit;
        out << YAML::Key << "StatusBarHidden" << YAML::Value << player.IOS.StatusBarHidden;
        out << YAML::EndMap;
        out << YAML::Key << "Web" << YAML::Value << YAML::BeginMap;
        out << YAML::Key << "GraphicsAPI" << YAML::Value << player.Web.GraphicsAPI;
        out << YAML::Key << "Template" << YAML::Value << player.Web.Template;
        out << YAML::Key << "Compression" << YAML::Value << player.Web.Compression;
        out << YAML::Key << "MemoryMB" << YAML::Value << player.Web.MemoryMB;
        out << YAML::Key << "Threads" << YAML::Value << player.Web.Threads;
        out << YAML::Key << "DataCaching" << YAML::Value << player.Web.DataCaching;
        out << YAML::EndMap;
        out << YAML::Key << "QNX" << YAML::Value << YAML::BeginMap;
        out << YAML::Key << "SDP" << YAML::Value << player.QNX.SDP;
        out << YAML::Key << "Architecture" << YAML::Value << player.QNX.Architecture;
        out << YAML::Key << "FillDisplay" << YAML::Value << player.QNX.FillDisplay;
        out << YAML::Key << "ScreenAppID" << YAML::Value << player.QNX.ScreenAppID;
        out << YAML::EndMap;
        out << YAML::EndMap;

        const auto& build = config.Build;
        out << YAML::Key << "Build" << YAML::Value << YAML::BeginMap;
        out << YAML::Key << "OutputDirectory" << YAML::Value << build.OutputDirectory;
        out << YAML::Key << "DevelopmentBuild" << YAML::Value << build.DevelopmentBuild;
        out << YAML::Key << "Scenes" << YAML::Value << YAML::BeginSeq;
        for (const auto& scene : build.Scenes)
        {
            out << YAML::BeginMap;
            out << YAML::Key << "Path" << YAML::Value << scene.Path;
            out << YAML::Key << "Enabled" << YAML::Value << scene.Enabled;
            out << YAML::EndMap;
        }
        out << YAML::EndSeq;
        out << YAML::EndMap;

        PhysicsSettings::Get().Serialize(out);
        ProjectSettings::Serialize(out);
        out << YAML::EndMap;

        std::ofstream fout(filepath);
        fout << out.c_str();
        return true;
    }

    bool ProjectSerializer::Deserialize(const std::filesystem::path& filepath)
    {
        YAML::Node data;
        try
        {
            data = YAML::LoadFile(filepath.string());
        }
        catch (YAML::ParserException e)
        {
            return false;
        }

        auto projectNode = data["Project"];
        if (!projectNode || !projectNode.IsMap())
            return false;

        auto& config = m_Project->GetConfig();
        if (projectNode["Name"])
            config.Name = projectNode["Name"].as<std::string>();
        
        if (projectNode["Organization"])
            config.Organization = projectNode["Organization"].as<std::string>();
        if (projectNode["Version"])
            config.Version = projectNode["Version"].as<std::string>();
        if (projectNode["TargetPlatform"])
            config.TargetPlatform = projectNode["TargetPlatform"].as<std::string>();
        if (projectNode["StartScene"])
            config.StartScene = projectNode["StartScene"].as<std::string>();
        if (projectNode["AssetDirectory"])
            config.AssetDirectory = projectNode["AssetDirectory"].as<std::string>();
        if (projectNode["ScriptModulePath"])
            config.ScriptModulePath = projectNode["ScriptModulePath"].as<std::string>();

        // Oyuncu (pencere) ayarları
        config.Player = PlayerSettings{};
        if (auto player = data["Player"]; player && player.IsMap())
        {
            auto& p = config.Player;
            try
            {
                if (player["Width"]) p.Width = std::max(160, player["Width"].as<int>());
                if (player["Height"]) p.Height = std::max(120, player["Height"].as<int>());
                if (player["FullscreenMode"]) p.FullscreenMode = std::clamp(player["FullscreenMode"].as<int>(), 0, 2);
                if (player["Resizable"]) p.Resizable = player["Resizable"].as<bool>();
                if (player["VSync"]) p.VSync = player["VSync"].as<bool>();
                if (player["RunInBackground"]) p.RunInBackground = player["RunInBackground"].as<bool>();
                if (player["Icon"]) p.Icon = player["Icon"].as<std::string>();
                if (player["GraphicsAPI"]) p.GraphicsAPI = player["GraphicsAPI"].as<std::string>();
            }
            catch (const YAML::Exception&) {}
            // Platforma özel ayarlar: her alan ayrı okunur (eksik / bozuk alan ötekileri silmez)
            auto readStr = [](const YAML::Node& n, const char* key, std::string& v) {
                try { if (n[key]) v = n[key].as<std::string>(); } catch (const YAML::Exception&) {}
            };
            auto readInt = [](const YAML::Node& n, const char* key, int& v, int lo, int hi) {
                try { if (n[key]) v = std::clamp(n[key].as<int>(), lo, hi); } catch (const YAML::Exception&) {}
            };
            auto readBool = [](const YAML::Node& n, const char* key, bool& v) {
                try { if (n[key]) v = n[key].as<bool>(); } catch (const YAML::Exception&) {}
            };
            if (auto n = player["Splash"]; n && n.IsMap())
            {
                readBool(n, "Show", p.Splash.Show);
                readStr(n, "Logo", p.Splash.Logo);
                readBool(n, "ShowEngineLogo", p.Splash.ShowEngineLogo);
                try { if (n["Duration"]) p.Splash.Duration = std::clamp(n["Duration"].as<float>(), 0.5f, 10.0f); } catch (const YAML::Exception&) {}
                readInt(n, "DrawMode", p.Splash.DrawMode, 0, 4);
                readInt(n, "Animation", p.Splash.Animation, 0, 1);
                try { if (n["LogoScale"]) p.Splash.LogoScale = std::clamp(n["LogoScale"].as<float>(), 0.25f, 3.0f); } catch (const YAML::Exception&) {}
                readStr(n, "BackgroundImage", p.Splash.BackgroundImage);
                p.Splash.Logos.clear();
                try
                {
                    if (auto logos = n["Logos"]; logos && logos.IsSequence())
                        for (const auto& l : logos)
                        {
                            SplashLogoEntry e;
                            if (l["Path"]) e.Path = l["Path"].as<std::string>();
                            if (l["Duration"]) e.Duration = std::clamp(l["Duration"].as<float>(), 0.5f, 10.0f);
                            if (!e.Path.empty())
                                p.Splash.Logos.push_back(e);
                        }
                }
                catch (const YAML::Exception&) {}
                // Eski projeler: tek logo alanı
                if (p.Splash.Logos.empty() && !p.Splash.Logo.empty())
                    p.Splash.Logos.push_back({ p.Splash.Logo, p.Splash.Duration });
                try
                {
                    if (auto bg = n["Background"]; bg && bg.IsSequence() && bg.size() == 3)
                    {
                        p.Splash.BackgroundR = std::clamp(bg[0].as<float>(), 0.0f, 1.0f);
                        p.Splash.BackgroundG = std::clamp(bg[1].as<float>(), 0.0f, 1.0f);
                        p.Splash.BackgroundB = std::clamp(bg[2].as<float>(), 0.0f, 1.0f);
                    }
                }
                catch (const YAML::Exception&) {}
            }
            if (auto n = player["macOS"]; n && n.IsMap())
            {
                readStr(n, "BundleIdentifier", p.Mac.BundleIdentifier);
                readStr(n, "BuildNumber", p.Mac.BuildNumber);
                readStr(n, "MinimumVersion", p.Mac.MinimumVersion);
                readStr(n, "Category", p.Mac.Category);
                readStr(n, "CameraUsage", p.Mac.CameraUsage);
                readStr(n, "MicrophoneUsage", p.Mac.MicrophoneUsage);
            }
            if (auto n = player["Linux"]; n && n.IsMap())
                readInt(n, "DisplayServer", p.Linux.DisplayServer, 0, 2);
            if (auto n = player["BSD"]; n && n.IsMap())
                readInt(n, "DisplayServer", p.BSD.DisplayServer, 0, 2);
            if (auto n = player["Android"]; n && n.IsMap())
            {
                readStr(n, "PackageName", p.Android.PackageName);
                readInt(n, "VersionCode", p.Android.VersionCode, 1, 2100000000);
                readInt(n, "Orientation", p.Android.Orientation, 0, 4);
                readBool(n, "ARM64", p.Android.ARM64);
                readBool(n, "X86_64", p.Android.X86_64);
                readStr(n, "Keystore", p.Android.Keystore);
                readStr(n, "KeyAlias", p.Android.KeyAlias);
            }
            if (auto n = player["iOS"]; n && n.IsMap())
            {
                readStr(n, "BundleIdentifier", p.IOS.BundleIdentifier);
                readStr(n, "BuildNumber", p.IOS.BuildNumber);
                readInt(n, "TargetDevice", p.IOS.TargetDevice, 0, 2);
                readStr(n, "MinimumVersion", p.IOS.MinimumVersion);
                readInt(n, "Orientation", p.IOS.Orientation, 0, 2);
                readStr(n, "TeamID", p.IOS.TeamID);
                readStr(n, "CameraUsage", p.IOS.CameraUsage);
                readBool(n, "RequiresARKit", p.IOS.RequiresARKit);
                readBool(n, "StatusBarHidden", p.IOS.StatusBarHidden);
            }
            if (auto n = player["Web"]; n && n.IsMap())
            {
                readInt(n, "GraphicsAPI", p.Web.GraphicsAPI, 0, 1);
                readInt(n, "Template", p.Web.Template, 0, 2);
                readInt(n, "Compression", p.Web.Compression, 0, 2);
                readInt(n, "MemoryMB", p.Web.MemoryMB, 32, 4096);
                readBool(n, "Threads", p.Web.Threads);
                readBool(n, "DataCaching", p.Web.DataCaching);
            }
            if (auto n = player["QNX"]; n && n.IsMap())
            {
                readInt(n, "SDP", p.QNX.SDP, 0, 1);
                readInt(n, "Architecture", p.QNX.Architecture, 0, 1);
                readBool(n, "FillDisplay", p.QNX.FillDisplay);
                readStr(n, "ScreenAppID", p.QNX.ScreenAppID);
            }
        }

        // Derleme ayarları (sahne listesi)
        config.Build = BuildSettings{};
        if (auto build = data["Build"]; build && build.IsMap())
        {
            try
            {
                if (build["OutputDirectory"]) config.Build.OutputDirectory = build["OutputDirectory"].as<std::string>();
                if (build["DevelopmentBuild"]) config.Build.DevelopmentBuild = build["DevelopmentBuild"].as<bool>();
                if (auto scenes = build["Scenes"]; scenes && scenes.IsSequence())
                    for (const auto& s : scenes)
                    {
                        BuildSceneEntry e;
                        if (s["Path"]) e.Path = s["Path"].as<std::string>();
                        if (s["Enabled"]) e.Enabled = s["Enabled"].as<bool>();
                        if (!e.Path.empty())
                            config.Build.Scenes.push_back(e);
                    }
            }
            catch (const YAML::Exception&) {}
        }

        // Proje fizik ayarları (yoksa varsayılanlar)
        PhysicsSettings::Get().Deserialize(data["Physics"]);
        // Ses, kalite, grafik, gezinme, editör ve betik sırası ayarları
        ProjectSettings::Deserialize(data);
        ProjectSettings::Apply();

        // Set the root project directory to the directory containing this .cqproj file
        m_Project->SetProjectDirectory(filepath.parent_path());

        return true;
    }
}
