#include "ConsolePanel.h"
#include "Core/Console/ConsoleVariables.h"

#include <sstream>
#include "Scripting/ConquerorScript/CQSEngine.h"
#include "UI/EditorUI.h"
#include "Core/Automation/Automation.h"
#include "Core/Project/Project.h"
#include "Core/Utils/FileSystem.h"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace Conqueror::Editor
{
    namespace
    {
        constexpr ImU32 kDefaultColor = IM_COL32(214, 216, 222, 255);
        constexpr ImU32 kPromptColor = IM_COL32(110, 180, 255, 255);
        constexpr ImU32 kErrorColor = IM_COL32(255, 110, 110, 255);
        constexpr ImU32 kDimColor = IM_COL32(140, 145, 155, 255);
        constexpr ImU32 kSuccessColor = IM_COL32(120, 220, 140, 255);

        ImU32 AnsiColor(int code)
        {
            static const ImU32 normal[8] = {
                IM_COL32(40, 40, 40, 255), IM_COL32(230, 90, 90, 255), IM_COL32(120, 210, 120, 255), IM_COL32(230, 200, 90, 255),
                IM_COL32(100, 150, 240, 255), IM_COL32(200, 120, 220, 255), IM_COL32(90, 200, 210, 255), IM_COL32(214, 216, 222, 255) };
            static const ImU32 bright[8] = {
                IM_COL32(120, 120, 120, 255), IM_COL32(255, 120, 120, 255), IM_COL32(150, 240, 150, 255), IM_COL32(255, 230, 120, 255),
                IM_COL32(130, 180, 255, 255), IM_COL32(230, 150, 250, 255), IM_COL32(120, 230, 240, 255), IM_COL32(255, 255, 255, 255) };
            if (code >= 30 && code <= 37) return normal[code - 30];
            if (code >= 90 && code <= 97) return bright[code - 90];
            return kDefaultColor;
        }

        std::string Trim(const std::string& s)
        {
            size_t a = s.find_first_not_of(" \t\r\n");
            if (a == std::string::npos)
                return {};
            size_t b = s.find_last_not_of(" \t\r\n");
            return s.substr(a, b - a + 1);
        }
    }

    ConsolePanel::ConsolePanel()
    {
        m_CurrentColor = kDefaultColor;
        SyncProjectDirectory();
        AppendLine("Conqueror Console - commands run in the project folder. Type /help for editor commands, > to evaluate ConquerorScript.", kDimColor);
    }

    void ConsolePanel::SyncProjectDirectory()
    {
        auto project = Project::GetActiveProjectDirectory();
        if (project != m_LastProjectDirectory || m_WorkingDirectory.empty())
        {
            m_LastProjectDirectory = project;
            if (!project.empty())
                m_WorkingDirectory = project;
            else if (m_WorkingDirectory.empty())
            {
                std::string exe = FileSystem::GetExecutableDirectory();
                m_WorkingDirectory = exe.empty() ? std::filesystem::current_path() : std::filesystem::path(exe);
            }
        }
    }

    void ConsolePanel::SetWorkingDirectory(const std::filesystem::path& dir)
    {
        std::error_code ec;
        if (std::filesystem::is_directory(dir, ec))
            m_WorkingDirectory = std::filesystem::weakly_canonical(dir, ec);
    }

    void ConsolePanel::AppendLine(const std::string& text, ImU32 color)
    {
        Line line;
        line.Spans.push_back({ text, color, false });
        m_Lines.push_back(std::move(line));
        while (m_Lines.size() > kMaxLines)
            m_Lines.pop_front();
        m_ScrollToBottom = true;
    }

    // Akan çıktıyı satırlara böler ve ANSI SGR renk kodlarını uygular.
    // Satır sonu gelmemiş kısım m_PartialLine'da bekler (ekranda geçici satır olarak görünür).
    void ConsolePanel::AppendOutput(const std::string& text, ImU32 defaultColor)
    {
        m_PartialLine += text;
        m_ScrollToBottom = true;
        size_t lastNewline = m_PartialLine.rfind('\n');
        if (lastNewline == std::string::npos)
            return;
        std::string complete = m_PartialLine.substr(0, lastNewline + 1);
        m_PartialLine.erase(0, lastNewline + 1);

        Line current;
        std::string spanText;
        auto flushSpan = [&]()
        {
            if (!spanText.empty())
            {
                current.Spans.push_back({ spanText, m_CurrentColor ? m_CurrentColor : defaultColor, m_CurrentBold });
                spanText.clear();
            }
        };

        size_t i = 0;
        while (i < complete.size())
        {
            char c = complete[i];
            if (c == '\x1b' && i + 1 < complete.size() && complete[i + 1] == '[')
            {
                size_t end = i + 2;
                while (end < complete.size() && !std::isalpha((unsigned char)complete[end]))
                    ++end;
                if (end < complete.size() && complete[end] == 'm')
                {
                    flushSpan();
                    std::string params = complete.substr(i + 2, end - i - 2);
                    size_t pos = 0;
                    for (;;)
                    {
                        size_t semi = params.find(';', pos);
                        std::string p = params.substr(pos, semi == std::string::npos ? std::string::npos : semi - pos);
                        int code = p.empty() ? 0 : std::atoi(p.c_str());
                        if (code == 0) { m_CurrentColor = 0; m_CurrentBold = false; }
                        else if (code == 1) m_CurrentBold = true;
                        else if (code == 22) m_CurrentBold = false;
                        else if (code == 39) m_CurrentColor = 0;
                        else if ((code >= 30 && code <= 37) || (code >= 90 && code <= 97)) m_CurrentColor = AnsiColor(code);
                        if (semi == std::string::npos)
                            break;
                        pos = semi + 1;
                    }
                }
                i = (end < complete.size()) ? end + 1 : complete.size();
                continue;
            }
            if (c == '\n')
            {
                flushSpan();
                m_Lines.push_back(std::move(current));
                current = Line{};
            }
            else if (c == '\t')
                spanText += "    ";
            else if (c != '\r')
                spanText += c;
            ++i;
        }
        while (m_Lines.size() > kMaxLines)
            m_Lines.pop_front();
    }

    bool ConsolePanel::HandleBuiltin(const std::string& command)
    {
        if (command == "clear" || command == "cls")
        {
            m_Lines.clear();
            m_PartialLine.clear();
            return true;
        }
        if (command == "pwd")
        {
            AppendLine(m_WorkingDirectory.string(), kDefaultColor);
            return true;
        }
        if (command == "cd" || command.rfind("cd ", 0) == 0)
        {
            std::string target = Trim(command.size() > 2 ? command.substr(2) : "");
            std::filesystem::path dest;
            if (target.empty() || target == "~")
                dest = m_LastProjectDirectory.empty() ? m_WorkingDirectory : m_LastProjectDirectory;
            else
            {
                if (target.size() >= 2 && (target.front() == '"' || target.front() == '\'') && target.back() == target.front())
                    target = target.substr(1, target.size() - 2);
                dest = std::filesystem::path(target).is_absolute() ? std::filesystem::path(target) : m_WorkingDirectory / target;
            }
            std::error_code ec;
            if (std::filesystem::is_directory(dest, ec))
                m_WorkingDirectory = std::filesystem::weakly_canonical(dest, ec);
            else
                AppendLine("cd: no such directory: " + target, kErrorColor);
            return true;
        }
        // Unreal tarzı konsol: "r.Bloom 0", "t.MaxFPS 60", "stat fps", "slomo 0.5", "help r." ('~' ile zorla)
        if (command[0] == '~' || ConsoleSystem::IsKnown(command))
        {
            std::string line = command[0] == '~' ? Trim(command.substr(1)) : command;
            std::string output;
            bool ok = ConsoleSystem::Execute(line, output);
            std::istringstream lines(output);
            for (std::string l; std::getline(lines, l);)
                AppendLine(l, ok ? kDefaultColor : kErrorColor);
            return true;
        }
        if (command[0] == '>')
        {
            // ConquerorScript REPL: oyun sırasında çalışan betik VM'inde değerlendirilir
            std::string code = Trim(command.substr(1));
            if (code.empty())
            {
                AppendLine("Usage: > <ConquerorScript expression or statement>   e.g.  > [1, 2, 3].Map(x => x * x)", kPromptColor);
                return true;
            }
            std::string output;
            bool ok = CQS::CQSEngine::Evaluate(code, output);
            if (!output.empty())
                AppendLine(output, ok ? kDefaultColor : kErrorColor);
            return true;
        }
        if (command[0] == '/')
        {
            std::string editorCommand = Trim(command.substr(1));
            if (editorCommand == "help" || editorCommand.empty())
            {
                AppendLine("Editor commands (prefix with /):", kPromptColor);
                auto names = Automation::GetCommandNames();
                std::sort(names.begin(), names.end());
                std::string line;
                for (const auto& n : names)
                {
                    line += "  /" + n;
                    if (line.size() > 70) { AppendLine(line, kDefaultColor); line.clear(); }
                }
                if (!line.empty()) AppendLine(line, kDefaultColor);
                AppendLine("Shell built-ins: cd, pwd, clear/cls. Everything else runs in the system shell.", kDimColor);
                return true;
            }
            std::string error;
            if (Automation::ExecuteCommandLine(editorCommand, error))
                AppendLine("ok", kSuccessColor);
            else
                AppendLine("error: " + error, kErrorColor);
            return true;
        }
        return false;
    }

    void ConsolePanel::Execute(const std::string& commandLine)
    {
        std::string command = Trim(commandLine);
        if (command.empty())
            return;

        if (m_History.empty() || m_History.back() != command)
            m_History.push_back(command);
        if (m_History.size() > 200)
            m_History.erase(m_History.begin());
        m_HistoryPos = -1;

        // Süreç çalışıyorsa satır onun stdin'ine gider
        if (m_Process && m_Process->IsRunning())
        {
            AppendLine(command, kDimColor);
            m_Process->WriteInput(command + "\n");
            return;
        }

        AppendLine("> " + command, kPromptColor);
        if (HandleBuiltin(command))
            return;

        m_Process = std::make_unique<ProcessRunner>();
        if (!m_Process->Start(command, m_WorkingDirectory))
        {
            AppendLine("Failed to start process.", kErrorColor);
            m_Process.reset();
            return;
        }
        m_RunningCommand = command;
        m_CurrentColor = 0;
        m_CurrentBold = false;
    }

    void ConsolePanel::PollProcess()
    {
        if (!m_Process)
            return;
        std::string out = m_Process->ReadAvailable();
        if (!out.empty())
            AppendOutput(out, kDefaultColor);
        if (!m_Process->IsRunning())
        {
            out = m_Process->ReadAvailable();
            if (!out.empty())
                AppendOutput(out, kDefaultColor);
            if (!m_PartialLine.empty())
                AppendOutput("\n", kDefaultColor);
            int code = m_Process->GetExitCode();
            if (code != 0)
                AppendLine("[exit code " + std::to_string(code) + "]", code == -1 ? kDimColor : kErrorColor);
            m_Process.reset();
            m_RunningCommand.clear();
        }
    }

    int ConsolePanel::InputCallback(ImGuiInputTextCallbackData* data)
    {
        return static_cast<ConsolePanel*>(data->UserData)->OnInputCallback(data);
    }

    int ConsolePanel::OnInputCallback(ImGuiInputTextCallbackData* data)
    {
        if (data->EventFlag == ImGuiInputTextFlags_CallbackHistory)
        {
            if (m_History.empty())
                return 0;
            int prev = m_HistoryPos;
            if (data->EventKey == ImGuiKey_UpArrow)
                m_HistoryPos = (m_HistoryPos == -1) ? (int)m_History.size() - 1 : std::max(0, m_HistoryPos - 1);
            else if (data->EventKey == ImGuiKey_DownArrow && m_HistoryPos != -1)
                m_HistoryPos = (m_HistoryPos + 1 >= (int)m_History.size()) ? -1 : m_HistoryPos + 1;
            if (prev != m_HistoryPos)
            {
                const char* text = (m_HistoryPos >= 0) ? m_History[m_HistoryPos].c_str() : "";
                data->DeleteChars(0, data->BufTextLen);
                data->InsertChars(0, text);
            }
        }
        else if (data->EventFlag == ImGuiInputTextFlags_CallbackCompletion)
        {
            // Son kelimeyi çalışma dizinindeki dosya/klasör adlarıyla tamamla
            const char* end = data->Buf + data->CursorPos;
            const char* start = end;
            while (start > data->Buf && start[-1] != ' ' && start[-1] != '\t' && start[-1] != '"')
                --start;
            std::string word(start, end);
            std::filesystem::path base = m_WorkingDirectory;
            std::string prefix = word;
            size_t slash = word.find_last_of("/\\");
            std::string dirPart;
            if (slash != std::string::npos)
            {
                dirPart = word.substr(0, slash + 1);
                prefix = word.substr(slash + 1);
                base = std::filesystem::path(dirPart).is_absolute() ? std::filesystem::path(dirPart) : m_WorkingDirectory / dirPart;
            }
            std::vector<std::string> matches;
            std::error_code ec;
            for (auto& entry : std::filesystem::directory_iterator(base, ec))
            {
                std::string name = entry.path().filename().string();
                if (name.rfind(prefix, 0) == 0)
                    matches.push_back(name + (entry.is_directory(ec) ? "/" : ""));
            }
            if (matches.size() == 1)
            {
                data->DeleteChars((int)(start - data->Buf), (int)(end - start));
                data->InsertChars(data->CursorPos, (dirPart + matches[0]).c_str());
            }
            else if (matches.size() > 1)
            {
                // Ortak önek + seçenekleri listele
                std::string common = matches[0];
                for (auto& m : matches)
                {
                    size_t n = 0;
                    while (n < common.size() && n < m.size() && common[n] == m[n]) ++n;
                    common.resize(n);
                }
                if (common.size() > prefix.size())
                {
                    data->DeleteChars((int)(start - data->Buf), (int)(end - start));
                    data->InsertChars(data->CursorPos, (dirPart + common).c_str());
                }
                std::string list;
                for (auto& m : matches) list += m + "  ";
                AppendLine(list, kDimColor);
            }
        }
        return 0;
    }

    void ConsolePanel::OnImGuiRender(bool* open)
    {
        SyncProjectDirectory();
        PollProcess();

        if (!ImGui::Begin("Console", open))
        {
            ImGui::End();
            return;
        }

        // Araç çubuğu
        if (ImGui::Button("Clear"))
        {
            m_Lines.clear();
            m_PartialLine.clear();
        }
        ImGui::SameLine();
        bool running = m_Process && m_Process->IsRunning();
        ImGui::BeginDisabled(!running);
        if (UI::DangerButton("Stop"))
        {
            m_Process->Terminate();
            AppendLine("^C", kErrorColor);
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Project Root") && !m_LastProjectDirectory.empty())
            m_WorkingDirectory = m_LastProjectDirectory;
        ImGui::SameLine();
        ImGui::Checkbox("Auto Scroll", &m_AutoScroll);
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, UI::Colors::TextDim);
        ImGui::TextUnformatted(m_WorkingDirectory.string().c_str());
        ImGui::PopStyleColor();
        if (running)
        {
            ImGui::SameLine();
            UI::Badge(("running: " + m_RunningCommand).c_str(), UI::Colors::Warning);
        }

        // Çıktı alanı (monospace yoksa varsayılan font)
        float inputH = ImGui::GetFrameHeightWithSpacing() + 4.0f;
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.06f, 0.065f, 0.075f, 1.0f));
        ImGui::BeginChild("##termout", ImVec2(0, -inputH), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 1.0f));
        ImGuiListClipper clipper;
        clipper.Begin((int)m_Lines.size());
        while (clipper.Step())
        {
            for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
            {
                const auto& line = m_Lines[i];
                if (line.Spans.empty())
                {
                    ImGui::TextUnformatted(" ");
                    continue;
                }
                for (size_t s = 0; s < line.Spans.size(); ++s)
                {
                    if (s > 0)
                        ImGui::SameLine(0.0f, 0.0f);
                    ImGui::PushStyleColor(ImGuiCol_Text, line.Spans[s].Color);
                    ImGui::TextUnformatted(line.Spans[s].Text.c_str());
                    ImGui::PopStyleColor();
                }
            }
        }
        if (!m_PartialLine.empty())
        {
            // Henüz satır sonu gelmemiş çıktı (ör. "Password: " istemleri)
            std::string visible;
            for (size_t k = 0; k < m_PartialLine.size(); ++k)
            {
                if (m_PartialLine[k] == '\x1b')
                {
                    while (k < m_PartialLine.size() && !std::isalpha((unsigned char)m_PartialLine[k])) ++k;
                    continue;
                }
                if (m_PartialLine[k] != '\r') visible += m_PartialLine[k];
            }
            ImGui::TextUnformatted(visible.c_str());
        }
        ImGui::PopStyleVar();
        if (m_ScrollToBottom && m_AutoScroll)
            ImGui::SetScrollHereY(1.0f);
        m_ScrollToBottom = false;

        // Sağ tık: kopyala
        if (ImGui::BeginPopupContextWindow("##termctx"))
        {
            if (ImGui::MenuItem("Copy All"))
            {
                std::string all;
                for (auto& l : m_Lines)
                {
                    for (auto& s : l.Spans) all += s.Text;
                    all += "\n";
                }
                ImGui::SetClipboardText(all.c_str());
            }
            if (ImGui::MenuItem("Clear"))
                m_Lines.clear();
            ImGui::EndPopup();
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();

        // Komut satırı
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(kPromptColor));
        std::string folder = m_WorkingDirectory.filename().string();
        ImGui::AlignTextToFramePadding();
        ImGui::Text("%s %s", folder.empty() ? "/" : folder.c_str(), running ? ">" : "$");
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGuiInputTextFlags flags = ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackHistory | ImGuiInputTextFlags_CallbackCompletion;
        if (m_FocusInput)
        {
            ImGui::SetKeyboardFocusHere();
            m_FocusInput = false;
        }
        if (ImGui::InputTextWithHint("##terminput", running ? "input for the running process..." : "command (Tab: complete, Up/Down: history)", m_Input, sizeof(m_Input), flags, &ConsolePanel::InputCallback, this))
        {
            Execute(m_Input);
            m_Input[0] = 0;
            m_FocusInput = true;
        }
        // Ctrl+C: çalışan süreci durdur
        if (running && ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C, false) && !ImGui::GetIO().WantTextInput)
        {
            m_Process->Terminate();
            AppendLine("^C", kErrorColor);
        }

        ImGui::End();
    }
}
