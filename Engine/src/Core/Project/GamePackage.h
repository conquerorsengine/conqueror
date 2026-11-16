#pragma once

#include "Core/Base/Base.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <utility>
#include <vector>

// Taşınabilir oyun paketi (.cqspace): LÖVE .love / Java .jar / Godot .pck gibi tek dosya; motorun oyuncusu (ConquerorRuntime) onu her
// platformda açar. Kap AssetBundle biçimidir (Core/Assets/AssetBundle.h: "CQAB" başlığı, dosya tablosu, dosya başına zstd + CRC32);
// içinde proje dosyası (.cqproj), Assets/, Packages/ ve son girdi olarak "cqspace.yaml" bildirimi (manifest) bulunur. Motor kaynakları
// (gölgelendiriciler, yazı tipleri, motor dokuları) pakette yoktur: onları oyuncu getirir, bu yüzden paket en az kendisini yapan motor
// sürümünü ister. Açılan paket önbellek klasörüne bir kez çözülür (aynı içerik özeti = aynı klasör) ve oradan normal proje gibi yüklenir.
namespace Conqueror::GamePackage
{
    constexpr const char* Extension = ".cqspace";
    constexpr const char* ManifestName = "cqspace.yaml";
    constexpr const char* MimeType = "application/x-cqspace";
    constexpr int FormatVersion = 1;

    struct Manifest
    {
        int Format = FormatVersion;            // paket biçimi sürümü
        std::string Name;                      // oyunun adı (Product Name)
        std::string Version;                   // oyunun sürümü
        std::string Company;
        std::string EngineVersion;             // gereken en düşük motor sürümü (paketi yapan editörün sürümü)
        std::string CreatedBy;                 // "Conqueror Engine 0.575.3"
        std::string ProjectFile;               // paketteki .cqproj
        std::string StartScene;                // proje göreli
        std::vector<std::string> Scenes;
        std::vector<std::string> ScriptLanguages;   // kullanılan betik dilleri: "cqs", "luaq", "graph", "stategraph", "py", "cs", "java"
        std::vector<std::string> Features;          // gereken motor özellikleri: "python", "dotnet", "java", "openxr"
        std::string BuildId;                   // içerik özeti (16 onaltılık): çözülmüş kopyanın önbellek anahtarı
        std::string Created;                   // "2026-11-16 12:00:00"
        bool Development = false;
        uint64_t Files = 0;                    // içerik dosyası sayısı (bildirim hariç)
        uint64_t Size = 0;                     // içeriğin açılmış toplam boyutu
    };

    CQ_API std::string SerializeManifest(const Manifest& manifest);
    CQ_API bool ParseManifest(const std::string& text, Manifest& manifest, std::string* error = nullptr);

    // Paketi yazar. files: (proje göreli yol, kaynak dosya), generated: (proje göreli yol, içerik) — ör. derleme için yazılan .cqproj.
    // Bildirim son girdi olarak eklenir; BuildId / Files / Size burada doldurulur. progress: (bitti, toplam), false = iptal.
    CQ_API bool Write(const std::string& packageFile, Manifest manifest, const std::vector<std::pair<std::string, std::string>>& files,
                      const std::vector<std::pair<std::string, std::string>>& generated, std::string* error = nullptr,
                      const std::function<bool(size_t, size_t)>& progress = {}, Manifest* written = nullptr);
    CQ_API bool ReadManifest(const std::string& packageFile, Manifest& manifest, std::string* error = nullptr);
    // Paketteki tek bir dosya (ör. .cqproj: pencere boyutu, başlık ve grafik API'si oyuncu açılmadan okunur)
    CQ_API bool ReadFile(const std::string& packageFile, const std::string& path, std::string& data, std::string* error = nullptr);

    // "0.575.3" ile "0.577.3" karşılaştırması: -1, 0, 1 (eksik parçalar 0)
    CQ_API int CompareVersions(const std::string& a, const std::string& b);
    // Bu oyuncu paketi açabilir mi: biçim sürümü, motor sürümü, bilinen özellikler. false: reason kullanıcıya gösterilen metin
    CQ_API bool CheckCompatible(const Manifest& manifest, std::string& reason);
    // Betik dilleri ve özelliklerin çalışma zamanları bu oyuncuda var mı (Python / .NET SDK / JDK kurulu mu). Proje etkinleştirildikten
    // (betik çalışma zamanları kurulduktan) sonra çağrılır
    CQ_API bool CheckFeaturesAvailable(const Manifest& manifest, std::string& reason);
    // Makine koduna derlenen betik dilleri (C++, C, Rust, Zig): her platform için ayrı derlenir, pakette çalışamaz
    CQ_API bool IsNativeLanguage(const std::string& languageId);
    // Betik dilinin oyuncu bilgisayarında kurulu yazılım isteyen özelliği ("py" -> "python", "cs" -> "dotnet", "java" -> "java"), yoksa ""
    CQ_API std::string FeatureForLanguage(const std::string& languageId);

    // Önbellek kökü: <XDG_CACHE_HOME | ~/.cache | ~/Library/Caches | %LOCALAPPDATA%>/ConquerorEngine/Games
    // Yol bir .cqspace paketi mi (uzantı, büyük / küçük harf fark etmez)
    CQ_API bool IsPackagePath(const std::string& path);
    // Oyuncunun açılış ekranında .cqspace aranan klasörler: oyuncunun klasörü (ve Games/), ~/Games, İndirilenler
    CQ_API std::vector<std::filesystem::path> GameFolders();
    CQ_API std::filesystem::path CacheRoot();
    // Çözülmüş kopyanın klasörü: <önbellek kökü>/<ad>-<BuildId>
    CQ_API std::filesystem::path CacheDirectory(const Manifest& manifest);
    // Açar: bildirim + uyumluluk + (ilk açılışta) önbelleğe çözme. projectDir: çözülmüş proje klasörü. progress: 0..1 (çözme), false = iptal
    CQ_API bool Open(const std::string& packageFile, Manifest& manifest, std::filesystem::path& projectDir, std::string& error,
                     const std::function<bool(float)>& progress = {});

    // Son açılan oyunlar (oyuncunun açılış ekranı): <kullanıcı veri klasörü>/ConquerorEngine/Player/recent_games.yaml
    struct RecentGame
    {
        std::string Path;
        std::string Name;
        std::string Version;
        int64_t LastOpened = 0;     // Unix zamanı
    };
    CQ_API std::vector<RecentGame> LoadRecent();
    CQ_API void AddRecent(const std::string& packageFile, const Manifest& manifest);
    CQ_API void RemoveRecent(const std::string& packageFile);

    // Platform katmanlarının (iOS belge açma ...) çalışan oyuncuya paket açma isteği; oyuncunun açılış ekranı her karede alır
    CQ_API void RequestOpen(const std::string& packageFile);
    CQ_API bool TakeOpenRequest(std::string& packageFile);
}
