#pragma once

#include "Core/Base/Base.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

// Editör kurulumları ve modüller (47, Unity Hub "Installs" karşılığı).
// - Sürümler GitHub Releases'tan gelir: normal sürüm = Official, GitHub ön sürümü = Pre-release, "conquerorx" etiketli ön sürüm =
//   ConquerorX kanalı (her commit'te CI'ın güncellediği yapı; kuruluysa motor her açılışta en son commit'e güncellenir).
// - Her sürümün "conqueror-release.json" bildirimi editör arşivlerini (işletim sistemi başına) ve modül arşivlerini (boyut, SHA-256)
//   listeler. İlk yayımlanan sürümden önceki sürümler indirilemez: kaynaktan derlenir ("Build it yourself").
// - Modüller: platform derleme desteği (hazır derlenmiş oyuncu: kullanıcı araç zinciri kurmaz), betik dilleri (her dil ayrı modül;
//   gerektirdiği araç - JDK, .NET, C/C++ derleyicisi ... - algılanır, lisansı izin veriyorsa modülle birlikte gelir) ve belgeler.
// - Kurulumlar kullanıcının veri klasöründe (Hub ayarlarından ya da CQ_INSTALLS_DIR ile değişir); liste Hub ayar klasöründeki
//   installs.yaml'da. İndirme curl ile (Windows 10+, macOS ve Linux'ta hazır gelir), arşivler TarExtract ile açılır.
namespace Conqueror::Installs
{
    enum class Channel : int { Official = 0, PreRelease, ConquerorX, Local };
    CQ_API const char* ChannelName(Channel channel);
    CQ_API Channel ChannelFromName(const std::string& name);

    // Bir modülün bu bilgisayarda gerektirdiği araç
    struct Requirement
    {
        std::string Id;            // "jdk", "dotnet", "cpp-compiler", "rust", "android-sdk" ...
        std::string Name;          // "Java Development Kit 17+"
        bool Found = false;
        std::string Detail;        // bulunan yol / sürüm ya da eksik bilgisi
        std::string HowTo;         // nasıl kurulur (eksikse)
        bool Bundleable = false;   // lisansı motorla birlikte dağıtmaya izin veriyor: modül arşivi içerir
        std::string License;       // aracın lisansı (bilgi)
    };

    struct ModuleInfo
    {
        std::string Id;            // "android", "java", "cpp" ...
        std::string Name;          // "Android Build Support"
        std::string Category;      // "Platforms", "Languages", "Documentation"
        std::string Description;
        bool BuiltIn = false;      // editörün içinde gelir (ayrı indirme yok)
        bool Default = false;      // Install Editor penceresinde önceden işaretli
        std::vector<std::string> Requires;   // gereksinim kimlikleri
        std::vector<std::string> Bundles;    // modül arşivinin içinde gelen gereksinimler (lisansı izin verenler: JDK, .NET, Zig ...)
        std::vector<std::string> Hosts;      // boş: her işletim sistemi; aksi halde "linux" / "windows" / "macos"
        // Sürüm bildiriminden (yoksa indirilemez)
        std::string Asset, Url, Sha256;
        uint64_t Size = 0, InstalledSize = 0;
    };

    struct Release
    {
        std::string Version;       // "0.575.3"
        std::string Tag;           // "v0.575.3" / "conquerorx"
        std::string Title, Date, Notes, Url, Commit;
        Channel Kind = Channel::Official;
        bool HasManifest = false;
        // Bu işletim sisteminin editör arşivi
        std::string EditorAsset, EditorUrl, EditorSha256;
        uint64_t EditorSize = 0;
        std::string ManifestUrl;
        std::vector<std::pair<std::string, std::string>> Assets;   // sürümün dosyaları: ad, indirme adresi
        std::vector<ModuleInfo> Modules;   // bildirimdeki modüller (katalog bilgileriyle birleşik)
    };

    struct Install
    {
        std::string Version;
        Channel Kind = Channel::Official;
        std::filesystem::path Path;
        std::string Commit;
        std::vector<std::string> Modules;   // kurulu (indirilmiş) modüller
        bool Located = false;               // Locate ile eklendi: kaldırırken klasör silinmez, yalnız listeden çıkar
        bool Bundled = false;               // Hub'ın kendi klasöründeki editör (her zaman listede)
        std::string InstalledAt;
    };

    // ---- Konum ve kayıt
    CQ_API std::filesystem::path DefaultInstallRoot();
    CQ_API std::filesystem::path InstallRoot();                         // CQ_INSTALLS_DIR > SetInstallRoot > varsayılan
    CQ_API void SetInstallRoot(const std::filesystem::path& root);
    CQ_API std::vector<Install> LoadInstalls(const std::filesystem::path& configDir);
    CQ_API bool SaveInstalls(const std::filesystem::path& configDir, const std::vector<Install>& installs);
    // Kayda ekle / güncelle (aynı sürüm ve kanal varsa değiştirir); iş parçacığından da çağrılır
    CQ_API bool RegisterInstall(const std::filesystem::path& configDir, const Install& install);
    CQ_API bool UnregisterInstall(const std::filesystem::path& configDir, const std::filesystem::path& path);
    // Kurulum klasörünü siler (yalnız InstallRoot içindeyse) ve kayıttan çıkarır; Locate ile eklenen yalnız kayıttan çıkar
    CQ_API bool Uninstall(const std::filesystem::path& configDir, const Install& install, std::string& error);

    CQ_API std::string HostOS();       // "linux" / "windows" / "macos" / "freebsd" ...
    CQ_API std::string HostId();       // "linux-x86_64", "windows-x86_64", "macos-universal", "linux-arm64" ...
    CQ_API std::filesystem::path EditorExecutable(const std::filesystem::path& installDir);   // yoksa boş
    // Kurulum klasöründeki modüller (Modules/<id>/ klasörleri)
    CQ_API std::vector<std::string> InstalledModules(const std::filesystem::path& installDir);
    // Kurulum klasörünün sürümü: "conqueror-install.json" (CI yazar) ya da editörün --version çıktısı
    CQ_API std::string ReadInstallVersion(const std::filesystem::path& installDir, std::string* commit = nullptr, Channel* channel = nullptr);
    // Editör başlarken: kendi kurulumundaki modüllerin araçları (Modules/<id>/bin) PATH'e eklenir (paketlenmiş JDK, Zig, Rust ...)
    CQ_API void ApplyModuleToolPaths(const std::filesystem::path& installDir);
    // Platform modülünün hazır derlenmiş oyuncusu: <kurulum>/Modules/<modül>/player/<platform>/ (bin/ConquerorRuntime, lib/);
    // yoksa boş (Build, oyuncuyu kaynaktan derler)
    CQ_API std::filesystem::path PrebuiltPlayerDir(const std::filesystem::path& installDir, const std::string& platform);
    CQ_API std::string PlatformModuleId(const std::string& platform);

    // ---- Modül kataloğu ve gereksinimler
    CQ_API const std::vector<ModuleInfo>& Catalog();
    CQ_API const ModuleInfo* FindModule(const std::string& id);
    CQ_API bool ModuleAvailableOnHost(const ModuleInfo& module, const std::string& hostOS);
    // Algılama (PATH, ortam değişkenleri, bilinen klasörler; süreç çalıştırmaz). installDir: paketlenmiş araçlar için
    CQ_API Requirement CheckRequirement(const std::string& id, const std::filesystem::path& installDir = {});

    // ---- Sürümler
    CQ_API std::string ReleasesApiUrl();   // CQ_RELEASES_API ya da https://api.github.com/repos/conquerorsengine/conqueror/releases
    CQ_API std::string SourceRepositoryUrl();
    // GitHub Releases API yanıtı (JSON dizi); taslaklar atlanır, en yeni önce
    CQ_API bool ParseReleases(const std::string& json, std::vector<Release>& out, std::string& error);
    // conqueror-release.json: bu işletim sisteminin editör arşivi + modüller
    CQ_API bool ApplyManifest(Release& release, const std::string& manifestJson, std::string& error);
    // "0.575.3" < "0.577.3"; sayısal parçalar, sonda ön ek ("-rc1") daha eski
    CQ_API int CompareVersions(const std::string& a, const std::string& b);
    // İlk yayımlanan sürüm (indirilebilir en eskisi); daha eskileri için "Build it yourself" metni
    CQ_API std::string BuildItYourselfText(const std::string& oldestPublished);

    // ---- Ağ (curl): metin indir / dosya indir (iptal edilebilir, ilerleme: indirilen bayt)
    CQ_API bool FetchText(const std::string& url, std::string& out, std::string& error);
    CQ_API bool DownloadFile(const std::string& url, const std::filesystem::path& destination, std::string& error,
                             const std::atomic<bool>* cancel = nullptr, const std::function<void(uint64_t)>& onBytes = {});
    CQ_API std::string FileSha256(const std::filesystem::path& file);

    // ---- Kurulum işi (arka plan iş parçacığı): editör + seçili modüller, ya da var olan kuruluma modül ekleme, ya da ConquerorX
    // güncellemesi (yeni yapı hazırlanır, sonra eskisinin yerine geçer; modüller korunur)
    struct JobFile
    {
        std::string Name, Url, Sha256, ModuleId;   // ModuleId boş: editör arşivi
        uint64_t Size = 0;
    };
    struct JobRequest
    {
        std::string Version, Commit;
        Channel Kind = Channel::Official;
        std::filesystem::path ConfigDir;
        std::filesystem::path Target;               // kurulum klasörü
        std::vector<JobFile> Files;
        bool AddToExisting = false;                 // yalnız modül ekleme (editör arşivi yok)
        bool Replace = false;                       // ConquerorX güncellemesi: Target'ın yerine geçer
        std::vector<std::string> ExistingModules;   // kayda yazılacak eski modüller (modül ekleme / güncelleme)
    };
    struct Job
    {
        JobRequest Request;
        std::atomic<bool> Finished{ false };
        std::atomic<bool> Cancel{ false };
        std::atomic<bool> Succeeded{ false };
        std::atomic<float> Progress{ 0.0f };
        mutable std::mutex Mutex;
        std::string Step;    // "Downloading Android Build Support (12/80 MB)"
        std::string Error;

        std::string GetStep() const { std::lock_guard<std::mutex> lock(Mutex); return Step; }
        std::string GetError() const { std::lock_guard<std::mutex> lock(Mutex); return Error; }
    };
    CQ_API std::shared_ptr<Job> StartJob(const JobRequest& request);
    // Bir sürümden kurulum isteği (editör + modüller); bildirimi olmayan / bu işletim sistemine editörü olmayan sürüm için false
    CQ_API bool MakeInstallRequest(const Release& release, const std::vector<std::string>& modules, const std::filesystem::path& configDir,
                                   JobRequest& out, std::string& error);
    CQ_API bool MakeAddModulesRequest(const Release& release, const Install& install, const std::vector<std::string>& modules,
                                      const std::filesystem::path& configDir, JobRequest& out, std::string& error);
}
