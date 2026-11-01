#pragma once

#include "Core/Base/Base.h"

#include <filesystem>
#include <string>

namespace Conqueror
{
    // Bir kabuk komutunu alt süreçte çalıştırır; stdout+stderr birleşik olarak
    // bloklamadan okunur, stdin'e yazılabilir, süreç (ve çocukları) sonlandırılabilir.
    // Linux/macOS: /bin/sh -c, Windows: cmd.exe /S /C
    class CQ_API ProcessRunner
    {
    public:
        ProcessRunner() = default;
        ~ProcessRunner();
        ProcessRunner(const ProcessRunner&) = delete;
        ProcessRunner& operator=(const ProcessRunner&) = delete;

        bool Start(const std::string& command, const std::filesystem::path& workingDirectory);
        bool IsRunning();
        // Mevcut çıktıyı döndürür (yoksa boş); hiçbir zaman bloklamaz
        std::string ReadAvailable();
        bool WriteInput(const std::string& text);
        // stdin'i kapatır (EOF): ffmpeg gibi araçlar çıktıyı bitirir
        void CloseInput();
        void Terminate();
        int GetExitCode() const { return m_ExitCode; }

    private:
        void Cleanup();

        int m_ExitCode = 0;
        bool m_Running = false;
#ifdef _WIN32
        void* m_Process = nullptr;
        void* m_Thread = nullptr;
        void* m_StdoutRead = nullptr;
        void* m_StdinWrite = nullptr;
        void* m_Job = nullptr;
#else
        int m_Pid = -1;
        int m_StdoutFd = -1;
        int m_StdinFd = -1;
#endif
    };
}
