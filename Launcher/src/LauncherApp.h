#pragma once

#include "Core/Application.h"
#include "LauncherLayer.h"

namespace Conqueror::Launcher
{
    class LauncherApp : public Application
    {
    public:
        LauncherApp(const ApplicationSpecification& spec);
        ~LauncherApp();

        const std::string& GetLaunchCommand() const { return m_LaunchCommand; }

    private:
        std::string m_LaunchCommand;
    };
}
