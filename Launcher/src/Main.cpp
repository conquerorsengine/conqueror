#include "LauncherApp.h"
#include "LauncherLayer.h"

#include "Core/Base/Base.h"
#include "Core/Application.h"
#include "Core/Logging/Log.h"

#include <filesystem>
#include <cstdlib>

namespace Conqueror::Launcher
{
    LauncherApp::LauncherApp(const ApplicationSpecification& spec)
        : Application(spec)
    {
        PushLayer(new LauncherLayer());
    }

    LauncherApp::~LauncherApp()
    {
    }
}

Conqueror::Application* Conqueror::CreateApplication(ApplicationCommandLineArgs args)
{
    ApplicationSpecification spec;
    spec.Name = "Conqueror Launcher";
    spec.CommandLineArgs = args;
    spec.WorkingDirectory = "";
    spec.Width = 1280;
    spec.Height = 720;

    return new Conqueror::Launcher::LauncherApp(spec);
}

int main(int argc, char** argv)
{
    // Working directory'yi executable'ın bulunduğu yere ayarla
    std::filesystem::path exePath = argv[0];
    std::filesystem::path exeDir = exePath.parent_path();
    
    if (!exeDir.empty())
    {
        std::filesystem::current_path(exeDir);
    }

    Conqueror::Log::Init();

    CQ_CORE_INFO("Conqueror Launcher Starting...");
    CQ_CORE_INFO("Working Directory: {0}", std::filesystem::current_path().string());

    auto app = Conqueror::CreateApplication({ argc, argv });
    app->Run();
    delete app;

    const auto& launchCmd = Conqueror::Launcher::GetPendingLaunchCommand();
    if (!launchCmd.empty())
    {
        CQ_CORE_INFO("Launching engine: {0}", launchCmd);
        std::system(launchCmd.c_str());
    }

    return 0;
}
