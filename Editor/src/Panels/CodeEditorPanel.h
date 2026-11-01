#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <imgui.h>
#include <TextEditor.h>

namespace Conqueror::Editor
{
    // Çok sekmeli kod düzenleyici: sözdizimi renklendirme (CQS, C#, C++, Lua, Python, Java, GLSL, HLSL),
    // canlı CQS teşhisi, bağlama duyarlı otomatik tamamlama, bul/değiştir, satıra git.
    class CodeEditorPanel
    {
    public:
        CodeEditorPanel();
        ~CodeEditorPanel();

        void OnImGuiRender();

        void OpenFile(const std::filesystem::path& path, int line = -1);
        void SaveFile();          // etkin belge
        void SaveAll();
        bool HasUnsavedChanges() const;

        bool& GetIsOpen() { return m_IsOpen; }
        // Kaydetme sonrası (ör. oyun sırasında betik sıcak yükleme)
        std::function<void(const std::filesystem::path&)> OnFileSaved;

        // Otomasyon/test: etkin belgeye metin yaz ve tamamlama listesini döndür
        bool TypeText(const std::string& text);
        std::vector<std::string> GetCompletionLabels() const;
        bool AcceptCompletion(int index = -1);
        int GetDiagnosticCount() const;
        std::string GetActiveText() const;
        int ReplaceAllText(const std::string& find, const std::string& replace, bool matchCase = true);

    private:
        struct Diagnostic
        {
            int Line = 0;
            int Column = 0;
            std::string Message;
            bool IsWarning = false;
        };

        struct Document
        {
            std::filesystem::path Path;
            std::unique_ptr<TextEditor> Editor;
            std::string Language;               // "cqs", "cs", "cpp", "lua", "py", "java", "glsl", "hlsl", "text"
            bool Dirty = false;
            bool NeedsCheck = false;
            double LastEdit = 0.0;
            std::vector<Diagnostic> Diagnostics;
            std::filesystem::file_time_type DiskTime{};
            bool ChangedOnDisk = false;
            bool UsesCRLF = false;              // kaydederken satır sonlarını koru
            int PendingGotoLine = -1;
        };

        struct Completion
        {
            std::string Label;
            std::string Insert;
            std::string Detail;
            std::string Doc;
            int Kind = 0;                       // 0 anahtar kelime, 1 fonksiyon, 2 değişken/alan, 3 sınıf, 4 kod parçası, 5 üye
            int Score = 0;
        };

        Document* Active();
        const Document* Active() const;
        void LoadDocument(Document& doc);
        void CloseDocument(int index);
        void RenderToolbar(Document* doc);
        void RenderFindBar(Document& doc);
        void RenderStatusBar(Document* doc);
        void RenderProblems(Document& doc);
        void RunDiagnostics(Document& doc);
        void CheckExternalChanges();

        void UpdateCompletion(Document& doc, bool force);
        void RenderCompletionPopup(Document& doc);
        void BuildCompletions(Document& doc, const std::string& prefix, char before, const std::string& objectName);
        void Accept(Document& doc, const Completion& c);
        bool FindNext(Document& doc, bool backwards, bool wrap = true);
        void ReplaceAll(Document& doc);

        std::vector<std::unique_ptr<Document>> m_Documents;
        int m_ActiveIndex = -1;
        int m_FocusIndex = -1;
        int m_ConfirmClose = -1;
        bool m_IsOpen = false;
        float m_FontScale = 1.0f;
        bool m_ShowWhitespace = false;
        bool m_ShowProblems = true;

        // Bul / değiştir / satıra git
        bool m_ShowFind = false;
        bool m_ShowReplace = false;
        bool m_FindCase = false;
        bool m_FocusFindInput = false;
        char m_FindBuffer[256] = {};
        char m_ReplaceBuffer[256] = {};
        bool m_ShowGoto = false;
        char m_GotoBuffer[16] = {};
        std::string m_FindStatus;

        // Otomatik tamamlama
        bool m_CompletionOpen = false;
        std::vector<Completion> m_Completions;
        int m_CompletionSelected = 0;
        std::string m_CompletionPrefix;
        bool m_SuppressCompletion = false;
        bool m_ScrollCompletion = false;
        bool m_EditorFocused = false;
        bool m_FocusEditor = false;
        double m_LastExternalCheck = 0.0;
    };
}
