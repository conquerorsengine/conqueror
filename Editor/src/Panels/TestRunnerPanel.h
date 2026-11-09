#pragma once

#include <functional>
#include <string>

namespace Conqueror::Editor
{
    // Window > Test Runner: TestScript betiklerini listeler, Edit Mode / Play Mode çalıştırır, sonuçları gösterir.
    class TestRunnerPanel
    {
    public:
        void OnImGuiRender();
        bool& GetIsOpen() { return m_IsOpen; }
        // Play Mode testleri için Play'e geçirir (EditorLayer)
        std::function<void()> OnEnterPlay;
        std::function<void(const std::string&)> OnOpenScript;

    private:
        bool m_IsOpen = false;
        bool m_FirstDraw = true;
    };
}
