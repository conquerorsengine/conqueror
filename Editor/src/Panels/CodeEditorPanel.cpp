#include "CodeEditorPanel.h"
#include "Core/Logging/Log.h"
#include <fstream>
#include <sstream>

namespace Conqueror::Editor
{
    CodeEditorPanel::CodeEditorPanel()
    {
        m_Editor.SetLanguageDefinition(TextEditor::LanguageDefinition::CPlusPlus());
        m_Editor.SetPalette(TextEditor::GetDarkPalette());
    }

    void CodeEditorPanel::OpenFile(const std::filesystem::path& path)
    {
        if (!std::filesystem::exists(path)) return;

        m_FilePath = path;
        std::ifstream file(path);
        if (file.is_open())
        {
            std::stringstream ss;
            ss << file.rdbuf();
            m_Content = ss.str();
            m_Editor.SetText(m_Content);
            
            m_IsOpen = true;
            m_IsDirty = false;
            CQ_CORE_INFO("CodeEditor: Opened {0}", path.string());
        }
    }

    void CodeEditorPanel::SaveFile()
    {
        if (m_FilePath.empty()) return;

        std::ofstream file(m_FilePath);
        if (file.is_open())
        {
            file << m_Editor.GetText();
            m_IsDirty = false;
            CQ_CORE_INFO("CodeEditor: Saved {0}", m_FilePath.string());
        }
    }

    void CodeEditorPanel::OnImGuiRender()
    {
        if (!m_IsOpen) return;

        ImGui::SetNextWindowSize(ImVec2(600, 400), ImGuiCond_FirstUseEver);
        
        std::string title = "Code Editor - " + m_FilePath.filename().string();
        if (m_IsDirty) title += "*";
        title += "###CodeEditor";

        if (ImGui::Begin(title.c_str(), nullptr))
        {
            m_Editor.Render("TextEditor");
            
            if (m_Editor.IsTextChanged())
            {
                m_IsDirty = true;
            }
        }
        ImGui::End();
    }
}
