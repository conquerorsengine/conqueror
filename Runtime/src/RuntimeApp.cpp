#include "Core/Application.h"
#include "Core/Project/Project.h"
#include "Core/Utils/Platform.h"
#include "Core/Project/ProjectSerializer.h"
#include "Core/Utils/DynamicLibrary.h"
#include "RuntimeLayer.h"
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <string>

namespace Conqueror::Runtime
{
    class RuntimeApp : public Application
    {
    public:
        RuntimeApp(const ApplicationSpecification& spec, const std::filesystem::path& projectPath)
            : Application(spec)
        {
            PushLayer(new RuntimeLayer(projectPath));
        }

        ~RuntimeApp()
        {
        }
    };

    namespace
    {
        // Proje klasörü: "--" ile başlamayan ilk argüman; yoksa oyunun kendi klasörü (çift tıklamada
        // çalışma dizini ev klasörü olabilir), o da yoksa çalışma dizini
        std::filesystem::path FindProjectPath(const ApplicationCommandLineArgs& args)
        {
            for (int i = 1; i < args.Count; ++i)
            {
                std::string a = args.Args[i] ? args.Args[i] : "";
                if (a.rfind("--", 0) == 0)
                {
                    // Değer alan seçenekler
                    if ((a == "--connect" || a == "--port") && i + 1 < args.Count)
                        ++i;
                    continue;
                }
                return a;
            }
            std::error_code ec;
            const auto exeDir = DynamicLibrary::ExecutableDirectory();
            for (const auto& entry : std::filesystem::directory_iterator(exeDir, ec))
                if (entry.path().extension() == ".cqproj")
                    return exeDir;
            return std::filesystem::current_path();
        }
    }
}

Conqueror::Application* Conqueror::CreateApplication(ApplicationCommandLineArgs args)
{
    ApplicationSpecification spec;
    spec.Name = "Conqueror Game";
    spec.CommandLineArgs = args;
    spec.WorkingDirectory = "";
    spec.Width = 1280;
    spec.Height = 720;

    const std::filesystem::path projectPath = Conqueror::Runtime::FindProjectPath(args);

    // Pencere başlığı ve boyutu proje ayarlarından (Player Settings)
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(projectPath, ec))
    {
        if (entry.path().extension() != ".cqproj")
            continue;
        auto project = std::make_shared<Project>();
        if (ProjectSerializer(project).Deserialize(entry.path()))
        {
            const auto& cfg = project->GetConfig();
            if (!cfg.Name.empty())
                spec.Name = cfg.Name;
            spec.Width = (uint32_t)cfg.Player.Width;
            spec.Height = (uint32_t)cfg.Player.Height;
            spec.GraphicsBackend = cfg.Player.GraphicsAPI;
#if !defined(_WIN32)
            // Pencere uygulama kimliği (Wayland app_id / X11 sınıfı): ürün adı (küçük harf); masaüstü simgesi bununla eşleşir
            if (!std::getenv("CQ_APP_ID") && !cfg.Name.empty())
            {
                std::string id;
                for (char c : cfg.Name)
                    if (std::isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.')
                        id += (char)std::tolower((unsigned char)c);
                if (!id.empty())
                    setenv("CQ_APP_ID", id.c_str(), 0);
            }
#endif
            // Platform ayarları (Project Settings > Player > Platform Settings); kullanıcının ortam değişkeni önceliklidir
#if defined(CQ_PLATFORM_QNX)
            if (!cfg.Player.QNX.FillDisplay)
                setenv("CQ_QNX_WINDOWED", "1", 0);
            if (!cfg.Player.QNX.ScreenAppID.empty())
                setenv("CQ_APP_ID", cfg.Player.QNX.ScreenAppID.c_str(), 0);
#elif defined(CQ_PLATFORM_LINUX) && !defined(CQ_PLATFORM_ANDROID)
    #if defined(CQ_PLATFORM_BSD)
            const int displayServer = cfg.Player.BSD.DisplayServer;
    #else
            const int displayServer = cfg.Player.Linux.DisplayServer;
    #endif
            if (displayServer == 1 || displayServer == 2)
                setenv("CQ_DISPLAY_SERVER", displayServer == 1 ? "wayland" : "x11", 0);
#endif
        }
        break;
    }
    // Steam Deck: ekranın kendi çözünürlüğü (1280x800, 16:10)
    if (Conqueror::Platform::IsSteamDeck())
    {
        spec.Width = 1280;
        spec.Height = 800;
    }

    return new Conqueror::Runtime::RuntimeApp(spec, projectPath);
}

// Masaüstü main() ve Android android_main() (AndroidMain.cpp) ortak gövdesi
int CQ_RuntimeMain(int argc, char** argv)
{
    Conqueror::Log::Init();
    CQ_CORE_INFO("Starting Conqueror Runtime...");

    auto app = Conqueror::CreateApplication({ argc, argv });
    app->Run();
    delete app;
    Conqueror::Log::Shutdown();   // son günlük satırları da dosyaya

    return 0;
}

#ifndef __ANDROID__
int main(int argc, char** argv)
{
    return CQ_RuntimeMain(argc, argv);
}
#endif
