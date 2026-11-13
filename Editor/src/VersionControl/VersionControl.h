#pragma once

#include <string>
#include <vector>

namespace Conqueror::Editor::VersionControl
{
    // Unity Version Control (Project Settings > Version Control): Git (Git LFS dosya kilitleri) ya da Perforce (Helix Core).
    // Durumlar arka planda toplu sorgulanır (Git: git status + git lfs locks, Perforce: p4 opened + p4 fstat) ve önbellekten okunur;
    // Inspector başlığındaki Check Out / Lock / Unlock / Revert düğmeleri komutu hemen çalıştırır ve durumu yeniler.
    enum class State
    {
        Unknown = 0,
        NotVersioned,       // sürüm denetiminde değil (Git: izlenmiyor, Perforce: depoda yok)
        Clean,              // depodaki haliyle aynı
        Modified,           // Git: değiştirildi
        Added,              // eklendi (gönderilmedi)
        CheckedOut,         // Perforce: bu çalışma alanında açık (p4 edit)
        LockedLocal,        // bu kullanıcı kilitledi (Git LFS lock / p4 lock)
        LockedOther,        // başkası kilitledi (düzenlemeyin)
        CheckedOutOther,    // Perforce: başka bir çalışma alanında açık
    };

    struct FileStatus
    {
        State Status = State::Unknown;
        std::string Owner;       // kilitleyen / açan kullanıcı (başkasıysa)
    };

    // Proje ayarındaki kip: 0 None, 1 Git, 2 Perforce
    int Mode();
    bool IsEnabled();
    const char* ModeName();

    // Önbellekteki durum (arka plan yenilemesi birkaç saniyede bir); proje dışındaki yol: Unknown
    FileStatus Get(const std::string& path);
    // Arka plan yenilemesini başlat (değişiklik sonrası); wait = sonucu bekle (otomasyon / test)
    void Refresh(bool wait = false);
    // Her kare (EditorLayer): süresi dolan önbelleği yeniler
    void Update();

    // İşlemler (komut çıktısı message'a); başarılıysa durum yenilenir
    bool CheckOut(const std::string& path, std::string* message = nullptr);   // Perforce: p4 edit (Git: gerekmez, true)
    bool Lock(const std::string& path, std::string* message = nullptr);       // Git: git lfs lock, Perforce: p4 lock (gerekirse önce edit)
    bool Unlock(const std::string& path, std::string* message = nullptr);     // Git: git lfs unlock, Perforce: p4 unlock
    bool Revert(const std::string& path, std::string* message = nullptr);     // Git: git checkout --, Perforce: p4 revert
    bool Add(const std::string& path, std::string* message = nullptr);        // Git: git add, Perforce: p4 add
    // Bağlantı testi (Project Settings): git rev-parse / p4 info
    bool TestConnection(std::string* message);
    // Otomasyon / test: proje klasöründe Git deposu açar ve ilk kaydı yapar
    bool InitGitRepository(std::string* message);

    const char* StateName(State state);
}
