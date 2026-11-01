#include "CrashHandler.h"

#include "Core/Logging/Log.h"
#include "Core/Utils/Platform.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <exception>
#include <filesystem>
#include <mutex>
#include <stdexcept>
#include <thread>

#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
    #define CQ_CRASH_SANITIZER 1
#elif defined(__has_feature)
    #if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer) || __has_feature(memory_sanitizer)
        #define CQ_CRASH_SANITIZER 1
    #endif
#endif

#if defined(CQ_PLATFORM_WINDOWS)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
    #include <csignal>
#else
    #include <csignal>
    #include "Platform/Android/ExecInfoCompat.h"
    #include <fcntl.h>
    #include <sys/stat.h>
    #include <unistd.h>
#endif

namespace Conqueror
{
    namespace
    {
        // Çökme anında bellek ayırmadan kullanılacak, önceden hazırlanmış tamponlar
        char s_AppName[128] = "Conqueror";
        char s_ReportDir[2048] = "";
        char s_TerminateMessage[1024] = "";
        std::atomic<bool> s_Installed{ false };
        std::atomic<int> s_InHandler{ 0 };
        std::mutex s_DirMutex;

        void CopyString(char* dst, size_t size, const std::string& src)
        {
            const size_t n = std::min(size - 1, src.size());
            std::memcpy(dst, src.data(), n);
            dst[n] = '\0';
        }

        // Sinyal güvenli küçük biçimlendirme yardımcıları
        size_t AppendStr(char* buf, size_t pos, size_t cap, const char* s)
        {
            while (*s && pos + 1 < cap) buf[pos++] = *s++;
            buf[pos] = '\0';
            return pos;
        }

        size_t AppendUInt(char* buf, size_t pos, size_t cap, unsigned long long v, int base = 10, int minDigits = 1)
        {
            char tmp[32];
            int n = 0;
            do { const int d = (int)(v % (unsigned)base); tmp[n++] = (char)(d < 10 ? '0' + d : 'a' + d - 10); v /= (unsigned)base; } while (v && n < 31);
            while (n < minDigits && n < 31) tmp[n++] = '0';
            while (n > 0 && pos + 1 < cap) buf[pos++] = tmp[--n];
            buf[pos] = '\0';
            return pos;
        }

        // <dizin>/crash_<epoch>_<pid>.<ext>
        void BuildReportPath(char* out, size_t cap, const char* ext, unsigned long long pid)
        {
            size_t p = 0;
            out[0] = '\0';
            p = AppendStr(out, p, cap, s_ReportDir);
            if (p > 0 && out[p - 1] != '/' && out[p - 1] != '\\')
                p = AppendStr(out, p, cap, "/");
            p = AppendStr(out, p, cap, "crash_");
            p = AppendUInt(out, p, cap, (unsigned long long)std::time(nullptr));
            p = AppendStr(out, p, cap, "_");
            p = AppendUInt(out, p, cap, pid);
            AppendStr(out, p, cap, ext);
        }

        void OnTerminate()
        {
            // Yakalanmamış C++ istisnası: mesajı rapora ekle, sonra abort (SIGABRT işleyicisi raporu yazar)
            const char* msg = "std::terminate called";
            static char buffer[1024];
            if (std::exception_ptr ex = std::current_exception())
            {
                try { std::rethrow_exception(ex); }
                catch (const std::exception& e)
                {
                    std::snprintf(buffer, sizeof(buffer), "Uncaught exception: %s", e.what());
                    msg = buffer;
                }
                catch (...) { msg = "Uncaught non-standard exception"; }
            }
            CopyString(s_TerminateMessage, sizeof(s_TerminateMessage), msg);
            std::abort();
        }

#if !defined(CQ_PLATFORM_WINDOWS)
        alignas(16) char s_AltStack[1 << 16];

        const char* SignalName(int sig)
        {
            switch (sig)
            {
                case SIGSEGV: return "SIGSEGV (segmentation fault)";
                case SIGABRT: return "SIGABRT (abort)";
                case SIGFPE: return "SIGFPE (arithmetic error)";
                case SIGILL: return "SIGILL (illegal instruction)";
                case SIGBUS: return "SIGBUS (bus error)";
                default: return "signal";
            }
        }

        void WriteAll(int fd, const char* s)
        {
            size_t len = std::strlen(s);
            while (len > 0)
            {
                const ssize_t w = ::write(fd, s, len);
                if (w <= 0) return;
                s += w;
                len -= (size_t)w;
            }
        }

        void SignalHandler(int sig, siginfo_t* info, void*)
        {
            if (s_InHandler.fetch_add(1) > 0)
            {
                // İşleyici içinde ikinci çökme: varsayılan davranış
                ::signal(sig, SIG_DFL);
                ::raise(sig);
                return;
            }
            ::alarm(5);   // bir şey asılı kalırsa SIGALRM süreci sonlandırır

            char line[512];
            size_t p = 0;
            p = AppendStr(line, p, sizeof(line), "\n========== ");
            p = AppendStr(line, p, sizeof(line), s_AppName);
            p = AppendStr(line, p, sizeof(line), " crashed ==========\nSignal: ");
            p = AppendStr(line, p, sizeof(line), SignalName(sig));
            p = AppendStr(line, p, sizeof(line), "\nFault address: 0x");
            p = AppendUInt(line, p, sizeof(line), (unsigned long long)(uintptr_t)(info ? info->si_addr : nullptr), 16);
            p = AppendStr(line, p, sizeof(line), "\nTime (unix): ");
            p = AppendUInt(line, p, sizeof(line), (unsigned long long)std::time(nullptr));
            p = AppendStr(line, p, sizeof(line), "\nProcess id: ");
            p = AppendUInt(line, p, sizeof(line), (unsigned long long)::getpid());
            AppendStr(line, p, sizeof(line), "\n");

            char path[2300];
            BuildReportPath(path, sizeof(path), ".txt", (unsigned long long)::getpid());
            ::mkdir(s_ReportDir, 0755);   // sinyal güvenli; zaten varsa EEXIST
            const int fd = ::open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);

            void* frames[96];
            const int count = ::backtrace(frames, 96);
            for (int out : { fd, (int)STDERR_FILENO })
            {
                if (out < 0) continue;
                WriteAll(out, line);
                if (s_TerminateMessage[0])
                {
                    WriteAll(out, s_TerminateMessage);
                    WriteAll(out, "\n");
                }
                WriteAll(out, "Call stack (symbolize with addr2line / llvm-symbolizer if names are missing):\n");
                ::backtrace_symbols_fd(frames, count, out);
                WriteAll(out, "See Conqueror.log for the log leading up to the crash.\n");
            }
            if (fd >= 0)
            {
                ::close(fd);
                WriteAll(STDERR_FILENO, "Crash report written to ");
                WriteAll(STDERR_FILENO, path);
                WriteAll(STDERR_FILENO, "\n");
            }

            // En iyi çaba: günlük tamponlarını diske yaz (kilit tutuluyorsa alarm sonlandırır)
            Log::FlushAll();

            ::signal(sig, SIG_DFL);
            ::raise(sig);
        }

        void InstallPlatform()
        {
            // backtrace ilk çağrıda libgcc yükler (bellek ayırır): işleyiciden önce bir kez çağır
            void* warm[4];
            ::backtrace(warm, 4);

            stack_t ss{};
            ss.ss_sp = s_AltStack;
            ss.ss_size = sizeof(s_AltStack);
            ss.ss_flags = 0;
            ::sigaltstack(&ss, nullptr);   // yığın taşmasında da işleyici çalışabilsin

            struct sigaction sa{};
            sa.sa_sigaction = SignalHandler;
            sigemptyset(&sa.sa_mask);
            sa.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESETHAND;
            for (int sig : { SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGBUS })
                ::sigaction(sig, &sa, nullptr);
        }
#else
        void WriteAll(HANDLE file, const char* s)
        {
            DWORD written = 0;
            if (file != INVALID_HANDLE_VALUE)
                ::WriteFile(file, s, (DWORD)std::strlen(s), &written, nullptr);
        }

        const char* ExceptionName(DWORD code)
        {
            switch (code)
            {
                case EXCEPTION_ACCESS_VIOLATION: return "EXCEPTION_ACCESS_VIOLATION";
                case EXCEPTION_STACK_OVERFLOW: return "EXCEPTION_STACK_OVERFLOW";
                case EXCEPTION_INT_DIVIDE_BY_ZERO: return "EXCEPTION_INT_DIVIDE_BY_ZERO";
                case EXCEPTION_ILLEGAL_INSTRUCTION: return "EXCEPTION_ILLEGAL_INSTRUCTION";
                case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "EXCEPTION_ARRAY_BOUNDS_EXCEEDED";
                case EXCEPTION_IN_PAGE_ERROR: return "EXCEPTION_IN_PAGE_ERROR";
                case EXCEPTION_PRIV_INSTRUCTION: return "EXCEPTION_PRIV_INSTRUCTION";
                case 0xC0000409: return "STATUS_STACK_BUFFER_OVERRUN / fail fast";
                case 0xE06D7363: return "C++ exception";
                default: return "exception";
            }
        }

        // "modul.dll+0x1234" (sembolleri addr2line / llvm-symbolizer ya da .pdb ile çözülür)
        void DescribeAddress(char* out, size_t cap, void* address)
        {
            size_t p = 0;
            out[0] = '\0';
            HMODULE module = nullptr;
            char name[MAX_PATH] = "?";
            if (::GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)address, &module) && module)
            {
                char full[MAX_PATH];
                if (::GetModuleFileNameA(module, full, MAX_PATH))
                {
                    const char* base = std::strrchr(full, '\\');
                    CopyString(name, sizeof(name), base ? base + 1 : full);
                }
            }
            p = AppendStr(out, p, cap, name);
            p = AppendStr(out, p, cap, "+0x");
            p = AppendUInt(out, p, cap, (unsigned long long)((uintptr_t)address - (uintptr_t)module), 16);
            p = AppendStr(out, p, cap, "  (0x");
            p = AppendUInt(out, p, cap, (unsigned long long)(uintptr_t)address, 16, 16);
            AppendStr(out, p, cap, ")");
        }

        void WriteReport(const char* reason, DWORD code, void* address, EXCEPTION_POINTERS* ep)
        {
            char path[2300];
            BuildReportPath(path, sizeof(path), ".txt", (unsigned long long)::GetCurrentProcessId());
            ::CreateDirectoryA(s_ReportDir, nullptr);   // zaten varsa başarısız olur, sorun değil
            HANDLE file = ::CreateFileA(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

            char line[1024];
            size_t p = 0;
            p = AppendStr(line, p, sizeof(line), "========== ");
            p = AppendStr(line, p, sizeof(line), s_AppName);
            p = AppendStr(line, p, sizeof(line), " crashed ==========\r\nReason: ");
            p = AppendStr(line, p, sizeof(line), reason);
            if (code)
            {
                p = AppendStr(line, p, sizeof(line), " (0x");
                p = AppendUInt(line, p, sizeof(line), code, 16, 8);
                p = AppendStr(line, p, sizeof(line), ")");
            }
            p = AppendStr(line, p, sizeof(line), "\r\nTime (unix): ");
            p = AppendUInt(line, p, sizeof(line), (unsigned long long)std::time(nullptr));
            AppendStr(line, p, sizeof(line), "\r\n");
            WriteAll(file, line);
            if (address)
            {
                DescribeAddress(line, sizeof(line), address);
                WriteAll(file, "Fault address: ");
                WriteAll(file, line);
                WriteAll(file, "\r\n");
            }
            if (s_TerminateMessage[0])
            {
                WriteAll(file, s_TerminateMessage);
                WriteAll(file, "\r\n");
            }
            WriteAll(file, "Call stack:\r\n");
            void* frames[62];
            const USHORT count = ::CaptureStackBackTrace(0, 62, frames, nullptr);
            for (USHORT i = 0; i < count; ++i)
            {
                DescribeAddress(line, sizeof(line), frames[i]);
                WriteAll(file, "  ");
                WriteAll(file, line);
                WriteAll(file, "\r\n");
            }
            WriteAll(file, "See Conqueror.log for the log leading up to the crash.\r\n");
            if (file != INVALID_HANDLE_VALUE)
                ::CloseHandle(file);
            std::fprintf(stderr, "%s crashed (%s). Report: %s\n", s_AppName, reason, path);

            // Minidump (Visual Studio / WinDbg ile açılır)
            if (HMODULE dbghelp = ::LoadLibraryA("dbghelp.dll"))
            {
                using MiniDumpWriteDumpFn = BOOL(WINAPI*)(HANDLE, DWORD, HANDLE, int, void*, void*, void*);
                if (auto writeDump = (MiniDumpWriteDumpFn)(void*)::GetProcAddress(dbghelp, "MiniDumpWriteDump"))
                {
                    char dumpPath[2300];
                    BuildReportPath(dumpPath, sizeof(dumpPath), ".dmp", (unsigned long long)::GetCurrentProcessId());
                    HANDLE dump = ::CreateFileA(dumpPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
                    if (dump != INVALID_HANDLE_VALUE)
                    {
                        struct { DWORD ThreadId; EXCEPTION_POINTERS* Pointers; BOOL ClientPointers; } info{ ::GetCurrentThreadId(), ep, FALSE };
                        // MiniDumpNormal | MiniDumpWithThreadInfo
                        writeDump(::GetCurrentProcess(), ::GetCurrentProcessId(), dump, 0x00000000 | 0x00001000, ep ? &info : nullptr, nullptr, nullptr);
                        ::CloseHandle(dump);
                    }
                }
            }
            Log::FlushAll();
        }

        LONG WINAPI UnhandledFilter(EXCEPTION_POINTERS* ep)
        {
            if (s_InHandler.fetch_add(1) > 0)
                return EXCEPTION_CONTINUE_SEARCH;
            const DWORD code = ep && ep->ExceptionRecord ? ep->ExceptionRecord->ExceptionCode : 0;
            void* address = ep && ep->ExceptionRecord ? ep->ExceptionRecord->ExceptionAddress : nullptr;
            WriteReport(ExceptionName(code), code, address, ep);
            return EXCEPTION_EXECUTE_HANDLER;
        }

        void AbortHandler(int)
        {
            if (s_InHandler.fetch_add(1) == 0)
                WriteReport("abort()", 0, nullptr, nullptr);
            ::signal(SIGABRT, SIG_DFL);
            std::_Exit(3);
        }

        void InstallPlatform()
        {
            ULONG guarantee = 64 * 1024;   // yığın taşmasında filtre için yer
            ::SetThreadStackGuarantee(&guarantee);
            ::SetUnhandledExceptionFilter(UnhandledFilter);
            ::signal(SIGABRT, AbortHandler);
        }
#endif
    }

    void CrashHandler::Install(const std::string& appName)
    {
        if (s_Installed.exchange(true))
            return;
        CopyString(s_AppName, sizeof(s_AppName), appName);
        if (!s_ReportDir[0])
        {
            std::string dir = Platform::GetUserAppDataDirectory();
            if (dir.empty())
                dir = Platform::GetTempDirectory();
            const auto defaultDir = std::filesystem::path(dir) / "ConquerorEngine" / "crash_dumps";
            std::error_code ec;
            std::filesystem::create_directories(defaultDir.parent_path(), ec);
            SetReportDirectory(defaultDir.string());
        }
        std::set_terminate(OnTerminate);
#if defined(CQ_CRASH_SANITIZER)
        CQ_CORE_INFO("Crash handler: sanitizer build, native handlers left to the sanitizer");
#else
        if (const char* off = std::getenv("CQ_NO_CRASH_HANDLER"); off && *off == '1')
            return;
        InstallPlatform();
#endif
    }

    void CrashHandler::SetReportDirectory(const std::string& directory)
    {
        // Klasör yalnızca çökme anında oluşturulur (her açılan projede boş "crash_dumps" bırakmamak için)
        std::lock_guard<std::mutex> lock(s_DirMutex);
        CopyString(s_ReportDir, sizeof(s_ReportDir), directory);
    }

    std::string CrashHandler::GetReportDirectory()
    {
        std::lock_guard<std::mutex> lock(s_DirMutex);
        return s_ReportDir;
    }

    bool CrashHandler::IsInstalled() { return s_Installed.load(); }

    void CrashHandler::TriggerTestCrash(const std::string& kind)
    {
        CQ_CORE_WARN("Triggering test crash: {}", kind);
        Log::FlushAll();
        if (kind == "abort")
            std::abort();
        if (kind == "throw")
        {
            // İş parçacığından kaçan istisna -> std::terminate (yakalayan bir çağıran olsa bile)
            std::thread([] { throw std::runtime_error("test exception from CrashHandler::TriggerTestCrash"); }).join();
        }
        if (kind == "fpe")
        {
            // "1 / x" derleyici tarafından bölmesiz karşılaştırmaya çevrilebilir: iki uçucu değişken
            volatile int numerator = 7;
            volatile int zero = 0;
            volatile int r = numerator / zero;
            (void)r;
        }
        volatile int* null = nullptr;
        *null = 42;
        std::abort();
    }
}
