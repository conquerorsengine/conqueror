#include "Core/Base/Base.h"
#include "Core/Application.h"
#include "Core/Logging/Log.h"

#include <filesystem>

extern Conqueror::Application* Conqueror::CreateApplication(ApplicationCommandLineArgs args);

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

    CQ_CORE_INFO("Conqueror Engine Starting...");
    CQ_CORE_INFO("Working Directory: {0}", std::filesystem::current_path().string());

    auto app = Conqueror::CreateApplication({ argc, argv });
    app->Run();
    delete app;

    return 0;
}
