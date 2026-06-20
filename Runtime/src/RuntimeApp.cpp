#include "Core/Application.h"
#include "RuntimeLayer.h"
#include <filesystem>

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
}

Conqueror::Application* Conqueror::CreateApplication(ApplicationCommandLineArgs args)
{
    ApplicationSpecification spec;
    spec.Name = "Conqueror Game";
    spec.CommandLineArgs = args;
    spec.WorkingDirectory = "";
    spec.Width = 1280;
    spec.Height = 720;

    std::filesystem::path projectPath = std::filesystem::current_path();
    if (args.Count > 1)
    {
        projectPath = args.Args[1];
    }

    return new Conqueror::Runtime::RuntimeApp(spec, projectPath);
}

int main(int argc, char** argv)
{
    Conqueror::Log::Init();
    CQ_CORE_INFO("Starting Conqueror Runtime...");

    auto app = Conqueror::CreateApplication({ argc, argv });
    app->Run();
    delete app;

    return 0;
}
