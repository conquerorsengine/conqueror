#pragma once

#include "Core/Base/Base.h"

#include <string>
#include <vector>

namespace Conqueror
{
    // Platform desteği modülleri (Unity Hub "Modules" karşılığı): her hedef platform için bu bilgisayarda gereken araç
    // zinciri var mı, yoksa ne kurulmalı. Build Settings, Build All ve hub ayarları aynı bilgiyi kullanır. Konsollar yok.
    namespace PlatformModules
    {
        enum class Status
        {
            Ready,          // bu bilgisayardan editörle derlenir
            Missing,        // araç zinciri eksik: Install metni ne kurulacağını söyler
            NeedsHost,      // başka bir işletim sisteminde derlenir (ör. macOS / iOS için Mac)
            CommandLine,    // komut satırından derlenir (ör. QNX SDP), editörün Build düğmesi sürmez
            NotAvailable    // bu motor sürümünde oynatıcısı yok (iOS, Web)
        };

        struct Module
        {
            std::string Platform;       // TargetPlatform adı ("Windows", "Android" ...)
            Status State = Status::Missing;
            std::string Toolchain;      // ne kullanılır (ör. "MinGW-w64 cross compiler")
            std::string Detail;         // bulunan yol / sürüm ya da eksik parça
            std::string Install;        // eksikse kurulum talimatı (komut)
        };

        CQ_API const char* StatusName(Status status);
        // Tüm platformlar (her çağrıda yeniden algılanır; hızlıdır: PATH ve klasör kontrolleri)
        CQ_API std::vector<Module> Detect();
        CQ_API Module Detect(const std::string& platform);
        // Bu bilgisayardan editörle derlenebilir mi
        CQ_API bool IsReady(const std::string& platform);
        // Yardımcılar: PATH'te program, Android SDK kökü
        CQ_API std::string FindProgram(const std::string& name);
        CQ_API std::string AndroidSdkRoot();
    }
}
