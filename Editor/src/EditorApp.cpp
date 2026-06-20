#include "EditorApp.h"
#include "EditorLayer.h"

namespace Conqueror::Editor
{
    EditorApp::EditorApp(const ApplicationSpecification& spec)
        : Application(spec)
    {
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

    return new Conqueror::Editor::EditorApp(spec);
}
