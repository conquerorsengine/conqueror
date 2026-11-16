#include "EditorApp.h"
#include "EditorLayer.h"
#include "Core/Project/Project.h"
#include "Core/Project/ProjectSerializer.h"

#include "Core/Base/Version.h"
#include "Core/Project/EditorInstalls.h"
#include "Core/Utils/FileSystem.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>

namespace Conqueror::Editor
{
    EditorApp::EditorApp(const ApplicationSpecification& spec)
        : Application(spec)
    {
        // Pencere / görev çubuğu simgesi (X11, Windows; Wayland'de uygulama kimliği + .desktop dosyası)
        GetWindow().SetIcon(FileSystem::ResolveResourcePath("Resources/Logo/AppIcon/conqueror_256.png"));
        // Bu kurulumun modüllerindeki araçlar (paketlenmiş JDK, .NET, Zig, Rust ...) PATH'e (47)
        Installs::ApplyModuleToolPaths(std::filesystem::current_path());
        PushLayer(new EditorLayer());
    }

    EditorApp::~EditorApp()
    {
    }
}

Conqueror::Application* Conqueror::CreateApplication(ApplicationCommandLineArgs args)
{
    // --version: pencere açmadan sürümü yazar (Hub'ın Locate'i ve betikler için) (47)
    for (int i = 1; i < args.Count; ++i)
        if (std::strcmp(args.Args[i], "--version") == 0)
        {
            std::printf("Conqueror's Engine %s\n", CQ_ENGINE_VERSION);
            std::fflush(stdout);
            std::exit(0);
        }
    ApplicationSpecification spec;
    spec.Name = "Conqueror Engine Editor";
    spec.CommandLineArgs = args;
    spec.WorkingDirectory = "";  // Empty - use exe directory
    spec.Width = 1920;
    spec.Height = 1080;
    spec.Maximized = true;   // Unity / Unreal editörü gibi tüm ekran (görev çubuğu hariç); düzen ilk karede son boyutla kurulur (259)
    spec.StartHidden = true;   // proje yüklenirken yalnız ayrı açılış penceresi görünür; editör ilk karesinde gösterilir (270)

    // Projenin grafik API ayarı (Player Settings > Rendering) pencere oluşturulmadan önce okunur
    if (args.Count > 1 && args[1])
    {
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(args[1], ec))
        {
            if (entry.path().extension() != ".cqproj")
                continue;
            auto project = std::make_shared<Conqueror::Project>();
            if (Conqueror::ProjectSerializer(project).Deserialize(entry.path()))
                spec.GraphicsBackend = project->GetConfig().Player.GraphicsAPI;
            break;
        }
    }

    return new Conqueror::Editor::EditorApp(spec);
}
