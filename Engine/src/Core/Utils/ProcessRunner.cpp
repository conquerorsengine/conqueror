#include "ProcessRunner.h"
#include "Core/Logging/Log.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include <chrono>
#include <thread>

namespace Conqueror
{
    ProcessRunner::~ProcessRunner()
    {
        Terminate();
        Cleanup();
    }

#ifdef _WIN32
    static std::wstring Utf8ToWide(const std::string& s)
    {
        if (s.empty())
            return {};
        int len = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
        std::wstring w((size_t)len, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), len);
        return w;
    }

    bool ProcessRunner::Start(const std::string& command, const std::filesystem::path& workingDirectory)
    {
        Terminate();
        Cleanup();

        SECURITY_ATTRIBUTES sa{};
        sa.nLength = sizeof(sa);
        sa.bInheritHandle = TRUE;

        HANDLE outRead = nullptr, outWrite = nullptr, inRead = nullptr, inWrite = nullptr;
        if (!CreatePipe(&outRead, &outWrite, &sa, 0))
            return false;
        SetHandleInformation(outRead, HANDLE_FLAG_INHERIT, 0);
        if (!CreatePipe(&inRead, &inWrite, &sa, 0))
        {
            CloseHandle(outRead); CloseHandle(outWrite);
            return false;
        }
        SetHandleInformation(inWrite, HANDLE_FLAG_INHERIT, 0);

        STARTUPINFOW si{};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdOutput = outWrite;
        si.hStdError = outWrite;
        si.hStdInput = inRead;

        std::wstring cmd = L"cmd.exe /S /C \"" + Utf8ToWide(command) + L"\"";
        std::wstring cwd = workingDirectory.wstring();
        PROCESS_INFORMATION pi{};
        BOOL ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW | CREATE_SUSPENDED,
                                 nullptr, cwd.empty() ? nullptr : cwd.c_str(), &si, &pi);
        CloseHandle(outWrite);
        CloseHandle(inRead);
        if (!ok)
        {
            CloseHandle(outRead);
            CloseHandle(inWrite);
            return false;
        }

        // Job object: Terminate() alt süreçleri de öldürsün
        HANDLE job = CreateJobObjectW(nullptr, nullptr);
        if (job)
        {
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
            info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            SetInformationJobObject(job, JobObjectExtendedLimitInformation, &info, sizeof(info));
            AssignProcessToJobObject(job, pi.hProcess);
        }
        ResumeThread(pi.hThread);

        m_Process = pi.hProcess;
        m_Thread = pi.hThread;
        m_StdoutRead = outRead;
        m_StdinWrite = inWrite;
        m_Job = job;
        m_Running = true;
        m_ExitCode = 0;
        return true;
    }

    bool ProcessRunner::IsRunning()
    {
        if (!m_Running || !m_Process)
            return false;
        DWORD code = 0;
        if (GetExitCodeProcess((HANDLE)m_Process, &code) && code != STILL_ACTIVE)
        {
            m_ExitCode = (int)code;
            m_Running = false;
        }
        return m_Running;
    }

    std::string ProcessRunner::ReadAvailable()
    {
        std::string out;
        if (!m_StdoutRead)
            return out;
        DWORD available = 0;
        while (PeekNamedPipe((HANDLE)m_StdoutRead, nullptr, 0, nullptr, &available, nullptr) && available > 0)
        {
            char buffer[4096];
            DWORD toRead = available < sizeof(buffer) ? available : (DWORD)sizeof(buffer);
            DWORD read = 0;
            if (!ReadFile((HANDLE)m_StdoutRead, buffer, toRead, &read, nullptr) || read == 0)
                break;
            out.append(buffer, read);
            if (out.size() > (1u << 20))
                break;
        }
        return out;
    }

    bool ProcessRunner::WriteInput(const std::string& text)
    {
        if (!m_StdinWrite)
            return false;
        DWORD written = 0;
        return WriteFile((HANDLE)m_StdinWrite, text.data(), (DWORD)text.size(), &written, nullptr) != 0;
    }

    void ProcessRunner::CloseInput()
    {
        if (m_StdinWrite) { CloseHandle((HANDLE)m_StdinWrite); m_StdinWrite = nullptr; }
    }

    void ProcessRunner::Terminate()
    {
        if (m_Running && m_Process)
        {
            if (m_Job)
                TerminateJobObject((HANDLE)m_Job, 1);
            else
                TerminateProcess((HANDLE)m_Process, 1);
            WaitForSingleObject((HANDLE)m_Process, 2000);
            m_Running = false;
            m_ExitCode = -1;
        }
    }

    void ProcessRunner::Cleanup()
    {
        if (m_StdoutRead) { CloseHandle((HANDLE)m_StdoutRead); m_StdoutRead = nullptr; }
        if (m_StdinWrite) { CloseHandle((HANDLE)m_StdinWrite); m_StdinWrite = nullptr; }
        if (m_Thread) { CloseHandle((HANDLE)m_Thread); m_Thread = nullptr; }
        if (m_Process) { CloseHandle((HANDLE)m_Process); m_Process = nullptr; }
        if (m_Job) { CloseHandle((HANDLE)m_Job); m_Job = nullptr; }
    }
#else
    bool ProcessRunner::Start(const std::string& command, const std::filesystem::path& workingDirectory)
    {
        Terminate();
        Cleanup();

        int outPipe[2];
        int inPipe[2];
        if (pipe(outPipe) != 0)
            return false;
        if (pipe(inPipe) != 0)
        {
            close(outPipe[0]); close(outPipe[1]);
            return false;
        }

        pid_t pid = fork();
        if (pid < 0)
        {
            close(outPipe[0]); close(outPipe[1]); close(inPipe[0]); close(inPipe[1]);
            return false;
        }

        if (pid == 0)
        {
            // Çocuk: kendi süreç grubunda (Terminate tüm grubu öldürür)
            setpgid(0, 0);
            dup2(inPipe[0], STDIN_FILENO);
            dup2(outPipe[1], STDOUT_FILENO);
            dup2(outPipe[1], STDERR_FILENO);
            close(outPipe[0]); close(outPipe[1]); close(inPipe[0]); close(inPipe[1]);
            if (!workingDirectory.empty() && chdir(workingDirectory.c_str()) != 0)
                _exit(127);
            setenv("TERM", "xterm-256color", 1);
            setenv("CLICOLOR_FORCE", "1", 0);
            execl("/bin/sh", "sh", "-c", command.c_str(), (char*)nullptr);
            _exit(127);
        }

        setpgid(pid, pid);
        close(outPipe[1]);
        close(inPipe[0]);
        fcntl(outPipe[0], F_SETFL, fcntl(outPipe[0], F_GETFL) | O_NONBLOCK);
        fcntl(outPipe[0], F_SETFD, FD_CLOEXEC);
        fcntl(inPipe[1], F_SETFD, FD_CLOEXEC);
        signal(SIGPIPE, SIG_IGN);

        m_Pid = pid;
        m_StdoutFd = outPipe[0];
        m_StdinFd = inPipe[1];
        m_Running = true;
        m_ExitCode = 0;
        return true;
    }

    bool ProcessRunner::IsRunning()
    {
        if (!m_Running || m_Pid <= 0)
            return false;
        int status = 0;
        pid_t r = waitpid(m_Pid, &status, WNOHANG);
        if (r == m_Pid)
        {
            m_Running = false;
            if (WIFEXITED(status)) m_ExitCode = WEXITSTATUS(status);
            else if (WIFSIGNALED(status)) m_ExitCode = 128 + WTERMSIG(status);
        }
        else if (r < 0 && errno == ECHILD)
        {
            m_Running = false;
        }
        return m_Running;
    }

    std::string ProcessRunner::ReadAvailable()
    {
        std::string out;
        if (m_StdoutFd < 0)
            return out;
        char buffer[4096];
        for (;;)
        {
            ssize_t n = read(m_StdoutFd, buffer, sizeof(buffer));
            if (n > 0)
            {
                out.append(buffer, (size_t)n);
                if (out.size() > (1u << 20))
                    break;
                continue;
            }
            break; // 0 = EOF, -1 = EAGAIN/hata
        }
        return out;
    }

    bool ProcessRunner::WriteInput(const std::string& text)
    {
        if (m_StdinFd < 0)
            return false;
        size_t written = 0;
        while (written < text.size())
        {
            ssize_t n = write(m_StdinFd, text.data() + written, text.size() - written);
            if (n < 0)
            {
                if (errno == EINTR)
                    continue;
                return false;
            }
            written += (size_t)n;
        }
        return true;
    }

    void ProcessRunner::CloseInput()
    {
        if (m_StdinFd >= 0) { close(m_StdinFd); m_StdinFd = -1; }
    }

    void ProcessRunner::Terminate()
    {
        if (m_Running && m_Pid > 0)
        {
            kill(-m_Pid, SIGTERM);
            for (int i = 0; i < 20 && IsRunning(); ++i)
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            if (IsRunning())
            {
                kill(-m_Pid, SIGKILL);
                waitpid(m_Pid, nullptr, 0);
            }
            m_Running = false;
            m_ExitCode = -1;
        }
    }

    void ProcessRunner::Cleanup()
    {
        if (m_StdoutFd >= 0) { close(m_StdoutFd); m_StdoutFd = -1; }
        if (m_StdinFd >= 0) { close(m_StdinFd); m_StdinFd = -1; }
        if (m_Pid > 0 && !m_Running)
            waitpid(m_Pid, nullptr, WNOHANG);
        m_Pid = -1;
    }
#endif
}
