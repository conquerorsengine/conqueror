#pragma once

#include "Core/Base/Base.h"

#include <string>

namespace Conqueror
{
    // Çökme raporlayıcı (Unreal CrashReporter benzeri, süreç içi):
    //  - Linux/macOS: SIGSEGV/SIGABRT/SIGFPE/SIGILL/SIGBUS için alternatif yığında işleyici, backtrace -> rapor dosyası + stderr
    //  - Windows: işlenmeyen istisna filtresi -> modül+adres yığını içeren rapor + minidump (.dmp, dbghelp varsa)
    // Rapor: <ReportDirectory>/crash_<zaman>_<pid>.txt. Sanitizer derlemelerinde kurulmaz (ASan raporu bozulmasın).
    class CQ_API CrashHandler
    {
    public:
        // Bir kez kurulur; appName rapor başlığında görünür
        static void Install(const std::string& appName);
        // Rapor klasörü (proje açıldığında <proje>/crash_dumps, derlenmiş oyunda kullanıcı veri klasörü)
        static void SetReportDirectory(const std::string& directory);
        static std::string GetReportDirectory();
        static bool IsInstalled();
        // Test: bilerek çök ("segv", "abort", "fpe")
        [[noreturn]] static void TriggerTestCrash(const std::string& kind);
    };
}
