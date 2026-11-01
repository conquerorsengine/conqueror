#pragma once

#include "Core/Base/Base.h"

#include <filesystem>
#include <string>
#include <vector>

namespace Conqueror
{
    // Unity SceneManager benzeri: betikler sahne yükleme ister, ev sahibi katman (editörün Play modu ya da
    // derlenmiş oyun) karenin sonunda yeni sahneyi yükleyip çalıştırır. Sahneler Build Settings listesindeki
    // sıraya göre indekslenir; adla ("Level2"), proje göreli yolla ya da indeksle istenebilir.
    class CQ_API SceneManager
    {
    public:
        // İstekler karenin sonunda işlenir; çözülemezse false (GetLastError)
        static bool RequestLoad(const std::string& nameOrPath);
        static bool RequestLoadIndex(int index);
        static bool RequestReload();
        static bool HasPendingLoad();
        static std::filesystem::path TakePendingLoad();

        // Oyundan çıkış (editörde Play durur)
        static void RequestQuit();
        static bool TakeQuitRequest();

        // Derleme listesi (etkin sahneler, proje göreli)
        static std::vector<std::string> GetBuildScenes();
        static int GetSceneCount();

        // Ev sahibi katman yüklediği sahneyi bildirir
        static void SetActiveScenePath(const std::filesystem::path& path);
        static const std::filesystem::path& GetActiveScenePath();
        static std::string GetActiveSceneName();
        static int GetActiveSceneIndex();

        static std::filesystem::path Resolve(const std::string& nameOrPath);
        static const std::string& GetLastError();
        static void Reset();
    };
}
