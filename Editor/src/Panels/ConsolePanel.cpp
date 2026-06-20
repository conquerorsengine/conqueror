#include "ConsolePanel.h"

#include <imgui.h>

namespace Conqueror::Editor
{
    void ConsolePanel::OnImGuiRender()
    {
        ImGui::Begin("Console");

        // Toolbar
        if (ImGui::Button("Clear"))
            Clear();

        ImGui::SameLine();
        ImGui::Checkbox("Auto Scroll", &m_AutoScroll);

        ImGui::SameLine();
        ImGui::Checkbox("Info", &m_ShowInfo);

        ImGui::SameLine();
        ImGui::Checkbox("Warnings", &m_ShowWarnings);

        ImGui::SameLine();
        ImGui::Checkbox("Errors", &m_ShowErrors);

        ImGui::Separator();

        // Messages
        ImGui::BeginChild("ScrollingRegion", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);

        for (const auto& message : m_Messages)
        {
            bool show = false;
            ImVec4 color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);

            switch (message.Type)
            {
                case ConsoleMessageType::Info:
                    show = m_ShowInfo;
                    color = ImVec4(0.8f, 0.8f, 0.8f, 1.0f);
                    break;
                case ConsoleMessageType::Warning:
                    show = m_ShowWarnings;
                    color = ImVec4(1.0f, 0.8f, 0.0f, 1.0f);
                    break;
                case ConsoleMessageType::Error:
                    show = m_ShowErrors;
                    color = ImVec4(1.0f, 0.2f, 0.2f, 1.0f);
                    break;
            }

            if (show)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, color);
                
                if (message.Count > 1)
                    ImGui::TextUnformatted((message.Message + " (" + std::to_string(message.Count) + ")").c_str());
                else
                    ImGui::TextUnformatted(message.Message.c_str());
                
                ImGui::PopStyleColor();
            }
        }

        if (m_AutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
            ImGui::SetScrollHereY(1.0f);

        ImGui::EndChild();
        ImGui::End();
    }

    void ConsolePanel::AddMessage(const std::string& message, ConsoleMessageType type)
    {
        // Check if last message is the same
        if (!m_Messages.empty() && m_Messages.back().Message == message && m_Messages.back().Type == type)
        {
            m_Messages.back().Count++;
        }
        else
        {
            m_Messages.emplace_back(message, type);
        }
    }

    void ConsolePanel::AddMessageFromLog(const std::string& message, int level)
    {
        // spdlog level'ını ConsoleMessageType'a çevir
        // 0=trace, 1=debug, 2=info, 3=warn, 4=error, 5=critical
        ConsoleMessageType type = ConsoleMessageType::Info;
        
        if (level >= 4) // error veya critical
            type = ConsoleMessageType::Error;
        else if (level == 3) // warn
            type = ConsoleMessageType::Warning;
        else // trace, debug, info
            type = ConsoleMessageType::Info;

        AddMessage(message, type);
    }

    void ConsolePanel::Clear()
    {
        m_Messages.clear();
    }
}
