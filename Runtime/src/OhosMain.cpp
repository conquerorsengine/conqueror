// HarmonyOS giriş noktası (oyuncu): libconqueror.so yüklenirken NAPI modülü "conqueror" kaydedilir (ArkTS: import engine from
// 'libconqueror.so', XComponent libraryname: 'conqueror'). HAP'teki oyun dosyaları ilk açılışta filesDir/game'e çıkarılır,
// çalışma dizini oraya alınır ve masaüstündeki oyuncu (CQ_RuntimeMain) motor iş parçacığında çalışır.
#if defined(__OHOS__)

#include "Platform/OHOS/OHOSPlatform.h"

#include <string>
#include <vector>

int CQ_RuntimeMain(int argc, char** argv);

namespace
{
    // Proje klasörü verilmediyse oyun klasörünün kendisi (çalışma dizini, .cqproj orada)
    int PlayerEntry(int argc, char** argv)
    {
        std::vector<char*> args(argv, argv + argc);
        std::string project = Conqueror::OHOS::ContentDirectory();
        if (argc < 2 && !project.empty())
            args.push_back(project.data());
        const int count = (int)args.size();
        args.push_back(nullptr);
        return CQ_RuntimeMain(count, args.data());
    }
}

extern "C" __attribute__((constructor)) void CQ_RegisterPlayerModule(void)
{
    Conqueror::OHOS::RegisterModule("conqueror", &PlayerEntry, "game");
}

#endif
