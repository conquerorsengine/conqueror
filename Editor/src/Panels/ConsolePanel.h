#pragma once

#include "Core/Utils/ProcessRunner.h"

#include <imgui.h>

#include <deque>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace Conqueror::Editor
{
    // Gömülü terminal: komutlar proje kökünde (veya 'cd' ile değiştirilen dizinde)
    // sistem kabuğunda çalışır. '/' ile başlayan satırlar editör komutlarıdır
    // (/play, /stop, /select <ad>, /panel <ad>, /help ...).
    class ConsolePanel
    {
    public:
        ConsolePanel();
        ~ConsolePanel() = default;

        void OnImGuiRender(bool* open = nullptr);
        void SetWorkingDirectory(const std::filesystem::path& dir);
        const std::filesystem::path& GetWorkingDirectory() const { return m_WorkingDirectory; }

        // Programatik çalıştırma (menüler, otomasyon)
        void Execute(const std::string& commandLine);

    private:
        struct Span
        {
            std::string Text;
            ImU32 Color;
            bool Bold = false;
        };
        struct Line
        {
            std::vector<Span> Spans;
        };

        void AppendOutput(const std::string& text, ImU32 defaultColor);
        void AppendLine(const std::string& text, ImU32 color);
        void PollProcess();
        bool HandleBuiltin(const std::string& command);
        void SyncProjectDirectory();

        static int InputCallback(ImGuiInputTextCallbackData* data);
        int OnInputCallback(ImGuiInputTextCallbackData* data);

        std::deque<Line> m_Lines;
        std::string m_PartialLine;        // satır sonu gelmemiş çıktı
        ImU32 m_CurrentColor = 0;
        bool m_CurrentBold = false;
        static constexpr size_t kMaxLines = 8000;

        std::unique_ptr<ProcessRunner> m_Process;
        std::string m_RunningCommand;
        std::filesystem::path m_WorkingDirectory;
        std::filesystem::path m_LastProjectDirectory;

        char m_Input[1024] = {};
        std::vector<std::string> m_History;
        int m_HistoryPos = -1;
        bool m_ScrollToBottom = false;
        bool m_FocusInput = false;
        bool m_AutoScroll = true;
    };
}
