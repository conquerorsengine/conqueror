#pragma once

#include <string>
#include <vector>
#include <filesystem>
#include <imgui.h>
#include <TextEditor.h>

namespace Conqueror::Editor
{
    class CodeEditorPanel
    {
    public:
        CodeEditorPanel();
        ~CodeEditorPanel() = default;

        void OnImGuiRender();

        // Dosya aç
        void OpenFile(const std::filesystem::path& path);
        
        // Kaydet
        void SaveFile();

        bool& GetIsOpen() { return m_IsOpen; }

    private:
        void RenderSyntaxHighlightedText(const std::string& text);
        ImVec4 GetTokenColor(const std::string& token);

        std::filesystem::path m_FilePath;
        std::string           m_Content;
        TextEditor            m_Editor;
        
        bool m_IsOpen = false;
        bool m_IsDirty = false;
    };
}
