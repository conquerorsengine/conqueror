#include <string>

#include "TestRunnerPanel.h"

#include "Scripting/TestRunner.h"
#include "UI/EditorUI.h"

#include <IconsFontAwesome6.h>
#include <imgui.h>

#include <filesystem>

namespace Conqueror::Editor
{
    using namespace Scripting;

    void TestRunnerPanel::OnImGuiRender()
    {
        if (!m_IsOpen)
            return;
        ImGui::SetNextWindowSize(ImVec2(620, 460), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin(ICON_FA_FLASK_VIAL "  Test Runner###TestRunner", &m_IsOpen))
        {
            ImGui::End();
            return;
        }
        if (m_FirstDraw)
        {
            TestRunner::Refresh();
            m_FirstDraw = false;
        }
        if (ImGui::Button(ICON_FA_PLAY "  Run Edit Mode"))
            TestRunner::RunEditMode();
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_GAMEPAD "  Run Play Mode"))
        {
            TestRunner::QueuePlayMode();
            if (OnEnterPlay)
                OnEnterPlay();
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_ROTATE "  Refresh"))
            TestRunner::Refresh();
        ImGui::SameLine();
        const int passed = TestRunner::CountWithStatus(TestRunner::Status::Passed);
        const int failed = TestRunner::CountWithStatus(TestRunner::Status::Failed);
        ImGui::TextColored(ImVec4(0.4f, 0.85f, 0.45f, 1.0f), "%d passed", passed);
        ImGui::SameLine();
        ImGui::TextColored(failed ? ImVec4(0.95f, 0.4f, 0.4f, 1.0f) : ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "%d failed", failed);
        ImGui::Separator();
        ImGui::BeginChild("##tests");
        auto& tests = TestRunner::Tests();
        if (tests.empty())
            ImGui::TextDisabled("No tests found. Create one with Create > Scripting > Test Script.");
        std::string group;
        bool open = false;
        for (auto& t : tests)
        {
            if (t.Script != group)
            {
                if (open)
                    ImGui::TreePop();
                group = t.Script;
                open = ImGui::TreeNodeEx(std::filesystem::path(group).filename().string().c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth);
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && OnOpenScript)
                    OnOpenScript(group);
            }
            if (!open)
                continue;
            ImGui::PushID((t.Script + t.Method).c_str());
            ImVec4 color(0.6f, 0.6f, 0.6f, 1.0f);
            const char* icon = ICON_FA_CIRCLE;
            if (t.State == TestRunner::Status::Passed) { color = ImVec4(0.4f, 0.85f, 0.45f, 1.0f); icon = ICON_FA_CIRCLE_CHECK; }
            else if (t.State == TestRunner::Status::Failed) { color = ImVec4(0.95f, 0.4f, 0.4f, 1.0f); icon = ICON_FA_CIRCLE_XMARK; }
            else if (t.State == TestRunner::Status::Running) { color = ImVec4(0.95f, 0.8f, 0.35f, 1.0f); icon = ICON_FA_HOURGLASS_HALF; }
            ImGui::TextColored(color, "%s", icon);
            ImGui::SameLine();
            ImGui::TextUnformatted(t.Method.c_str());
            ImGui::SameLine();
            ImGui::TextDisabled(t.PlayMode ? "Play Mode" : "Edit Mode");
            if (t.Seconds > 0.0)
            {
                ImGui::SameLine();
                ImGui::TextDisabled("%.3f s", t.Seconds);
            }
            ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorStartPos().x - 44.0f);
            if (ImGui::SmallButton(ICON_FA_PLAY))
            {
                if (t.PlayMode)
                {
                    TestRunner::QueuePlayMode(t.Method);
                    if (OnEnterPlay)
                        OnEnterPlay();
                }
                else
                    TestRunner::RunEditMode(t.Method);
            }
            for (const auto& message : t.Messages)
            {
                ImGui::Indent();
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.95f, 0.55f, 0.5f, 1.0f));
                ImGui::TextWrapped("%s", message.c_str());
                ImGui::PopStyleColor();
                ImGui::Unindent();
            }
            ImGui::PopID();
        }
        if (open)
            ImGui::TreePop();
        ImGui::EndChild();
        ImGui::End();
    }
}
