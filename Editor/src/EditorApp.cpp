#include "EditorApp.h"
#include "EditorLayer.h"
#include "Core/Project/Project.h"
#include "Core/Project/ProjectSerializer.h"

#include "Core/Utils/FileSystem.h"

#include <filesystem>

namespace Conqueror::Editor
{
    EditorApp::EditorApp(const ApplicationSpecification& spec)
        : Application(spec)
    {
        // Pencere / görev çubuğu simgesi (X11, Windows; Wayland'de uygulama kimliği + .desktop dosyası)
        GetWindow().SetIcon(FileSystem::ResolveResourcePath("Resources/Logo/AppIcon/conqueror_256.png"));
        PushLayer(new EditorLayer());
    }

    EditorApp::~EditorApp()
    {
    }
}

Conqueror::Application* Conqueror::CreateApplication(ApplicationCommandLineArgs args)
{
    ApplicationSpecification spec;
    spec.Name = "Conqueror Engine Editor";
    spec.CommandLineArgs = args;
    spec.WorkingDirectory = "";  // Empty - use exe directory
    spec.Width = 1920;
    spec.Height = 1080;

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
