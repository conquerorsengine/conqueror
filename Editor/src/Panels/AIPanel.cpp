#include "AIPanel.h"
#include "Core/Base/Base.h"
#include "Core/AI/AIManager.h"
#include <imgui.h>
#include <cstring>

namespace Conqueror::Editor
{
    AIPanel::AIPanel()
    {
    }

    void AIPanel::OnImGuiRender(bool* pOpen)
    {
        if (pOpen && !*pOpen) return;

        ImGui::SetNextWindowSize(ImVec2(400, 600), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("AI Assistant", pOpen))
        {
            ImGui::End();
            return;
        }

        auto& aiManager = AI::AIManager::Get();
        auto provider = aiManager.GetProvider();

        if (!aiManager.IsLoggedIn())
        {
            ImGui::Spacing();

            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.45f, 0.85f, 1.0f));
            if (ImGui::Button(" Antigravity ", ImVec2(-1, 38)))
            {
                aiManager.StartOAuthServer();

                std::string oauthUrl = "https://accounts.google.com/o/oauth2/v2/auth?"
                    "client_id=1071006060591-tmhssin2h21lcre235vtolojh4g403ep.apps.googleusercontent.com"
                    "&redirect_uri=http%3A%2F%2Flocalhost%3A51121%2Foauth-callback"
                    "&response_type=code"
                    "&scope=https%3A%2F%2Fwww.googleapis.com%2Fauth%2Fcloud-platform%20https%3A%2F%2Fwww.googleapis.com%2Fauth%2Fuserinfo.email%20https%3A%2F%2Fwww.googleapis.com%2Fauth%2Fuserinfo.profile%20https%3A%2F%2Fwww.googleapis.com%2Fauth%2Fcclog%20https%3A%2F%2Fwww.googleapis.com%2Fauth%2Fexperimentsandconfigs"
                    "&access_type=offline&prompt=consent";

#ifdef CQ_PLATFORM_LINUX
                std::string cmd = "xdg-open \"" + oauthUrl + "\" &";
                system(cmd.c_str());
#elif defined(CQ_PLATFORM_WINDOWS)
                std::string cmd = "start \"" + oauthUrl + "\"";
                system(cmd.c_str());
#else
                std::string cmd = "open \"" + oauthUrl + "\"";
                system(cmd.c_str());
#endif
            }
            ImGui::PopStyleColor();

            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.45f, 0.85f, 1.0f));
            if (ImGui::Button(" GitHub Copilot ", ImVec2(-1, 38)))
            {
                aiManager.StartGitHubOAuthServer();
            }
            ImGui::PopStyleColor();
            ImGui::Separator();

            const std::string& authMessage = aiManager.GetLastAuthMessage();
            if (!authMessage.empty())
            {
                ImGui::Spacing();
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.95f, 0.95f, 1.0f));
                ImGui::TextWrapped("%s", authMessage.c_str());
                ImGui::PopStyleColor();
            }
        }
        else
        {
            // Model Selector Dropdown & Logout
            if (provider)
            {
                auto models = provider->GetAvailableModels();
                std::string currentModel = provider->GetSelectedModel();

                if (models.empty())
                {
                    ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "Loading available models...");
                }

                ImGui::SetNextItemWidth(ImGui::GetWindowWidth() - 90);
                if (ImGui::BeginCombo("##ModelSelect", currentModel.c_str()))
                {
                    for (const auto& model : models)
                    {
                        bool isSelected = (currentModel == model);
                        if (ImGui::Selectable(model.c_str(), isSelected))
                        {
                            provider->SetSelectedModel(model);
                        }
                        if (isSelected) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }

                ImGui::SameLine();
                if (ImGui::Button("Logout", ImVec2(75, 0)))
                {
                    aiManager.Logout();
                }
            }

            ImGui::Separator();

            // Chat History Area
            float footerHeight = ImGui::GetFrameHeightWithSpacing() + 35.0f;
            ImGui::BeginChild("ChatHistoryArea", ImVec2(0, -footerHeight), true, ImGuiWindowFlags_None);

            const auto& history = aiManager.GetHistory();
            float chatWidth = ImGui::GetContentRegionAvail().x;

            for (size_t i = 0; i < history.size(); ++i)
            {
                const auto& msg = history[i];
                if (msg.Role == "system" || msg.Content.empty() || msg.Content == "...") continue;

                ImGui::Spacing();

                // Calculate text size to fit bubble dynamically
                float maxBubbleWidth = chatWidth * 0.85f;
                ImVec2 textSize = ImGui::CalcTextSize(msg.Content.c_str(), nullptr, false, maxBubbleWidth - 16.0f);
                float bubbleWidth = std::min(textSize.x + 20.0f, maxBubbleWidth);

                if (msg.Role == "user")
                {
                    // Right-align user bubbles
                    float indent = chatWidth - bubbleWidth;
                    if (indent > 0) ImGui::Indent(indent);

                    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.18f, 0.28f, 0.42f, 0.85f));
                    ImGui::BeginChild((std::string("UB_") + std::to_string(i)).c_str(),
                        ImVec2(bubbleWidth, 0),
                        ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders,
                        ImGuiWindowFlags_NoScrollbar);

                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
                    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + bubbleWidth - 16.0f);
                    ImGui::TextWrapped("%s", msg.Content.c_str());
                    ImGui::PopTextWrapPos();
                    ImGui::PopStyleColor();

                    ImGui::EndChild();
                    ImGui::PopStyleColor();

                    if (indent > 0) ImGui::Unindent(indent);
                }
                else if (msg.Role == "assistant")
                {
                    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.15f, 0.17f, 0.22f, 0.85f));
                    ImGui::BeginChild((std::string("AB_") + std::to_string(i)).c_str(),
                        ImVec2(bubbleWidth, 0),
                        ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_Borders,
                        ImGuiWindowFlags_NoScrollbar);

                    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
                    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + bubbleWidth - 16.0f);
                    ImGui::TextWrapped("%s", msg.DisplayContent.empty() ? msg.Content.c_str() : msg.DisplayContent.c_str());
                    ImGui::PopTextWrapPos();
                    ImGui::PopStyleColor();

                    ImGui::EndChild();
                    ImGui::PopStyleColor();
                }
            }

            // Show loading indicator while waiting for response
            if (!history.empty() && history.back().Role == "assistant" && history.back().Content == "...")
            {
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Thinking...");
            }

            // Execution Tool Logs
            const auto& toolLogs = aiManager.GetToolLogs();
            if (!toolLogs.empty())
            {
                ImGui::Separator();
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Executions:");
                for (const auto& log : toolLogs)
                {
                    ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f), "[OK] %s", log.c_str());
                }
            }

            // Auto-scroll to bottom
            if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 20.0f)
                ImGui::SetScrollHereY(1.0f);

            ImGui::EndChild();

            // Message Input Line
            ImGui::Separator();
            bool reclaimFocus = false;
            ImGuiInputTextFlags inputFlags = ImGuiInputTextFlags_EnterReturnsTrue;
            
            ImGui::SetNextItemWidth(ImGui::GetWindowWidth() - 90);
            if (ImGui::InputText("##MessageInput", m_InputBuffer, sizeof(m_InputBuffer), inputFlags))
            {
                std::string userMsg = m_InputBuffer;
                m_InputBuffer[0] = '\0';

                aiManager.SendUserMessage(m_ActiveScene.get(), userMsg, [](const std::string&) {});
                reclaimFocus = true;
            }

            ImGui::SameLine();
            if (ImGui::Button("Send", ImVec2(75, 0)))
            {
                std::string userMsg = m_InputBuffer;
                m_InputBuffer[0] = '\0';

                aiManager.SendUserMessage(m_ActiveScene.get(), userMsg, [](const std::string&) {});
                reclaimFocus = true;
            }

            if (reclaimFocus) ImGui::SetKeyboardFocusHere(-1);
        }

        ImGui::End();
    }
}
