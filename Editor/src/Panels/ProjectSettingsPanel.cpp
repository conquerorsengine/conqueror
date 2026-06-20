#include "ProjectSettingsPanel.h"
#include "Scene/Scene.h"
#include "Core/Logging/Log.h"
#include "Core/Project/Project.h"
#include "Core/Project/ProjectSerializer.h"

#include <imgui.h>

namespace Conqueror::Editor
{
    void ProjectSettingsPanel::OnImGuiRender()
    {
        ImGui::Begin("Project Settings");

        auto project = Project::GetActive();
        if (!project)
        {
            ImGui::Text("No active project");
            ImGui::End();
            return;
        }

        auto& config = project->GetConfig();
        
        ImGui::Text("General Settings");
        ImGui::Separator();

        char buffer[256];
        memset(buffer, 0, sizeof(buffer));
        strcpy(buffer, config.Name.c_str());
        if (ImGui::InputText("Project Name", buffer, sizeof(buffer)))
        {
            config.Name = std::string(buffer);
        }

        memset(buffer, 0, sizeof(buffer));
        strcpy(buffer, config.Organization.c_str());
        if (ImGui::InputText("Organization", buffer, sizeof(buffer)))
        {
            config.Organization = std::string(buffer);
        }

        memset(buffer, 0, sizeof(buffer));
        strcpy(buffer, config.Version.c_str());
        if (ImGui::InputText("Version", buffer, sizeof(buffer)))
        {
            config.Version = std::string(buffer);
        }

        ImGui::Spacing();
        ImGui::Text("Build Settings");
        ImGui::Separator();

        const char* platforms[] = { "Linux", "Windows" };
        int current_platform = config.TargetPlatform == "Windows" ? 1 : 0;
        if (ImGui::Combo("Target Platform", &current_platform, platforms, IM_ARRAYSIZE(platforms)))
        {
            config.TargetPlatform = platforms[current_platform];
        }

        ImGui::End();
    }
}
