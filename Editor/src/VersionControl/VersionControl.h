#pragma once

#include "Core/Project/Project.h"

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
    // ------------------------------------------------------------------ Version Control penceresi (VersionControlOps.cpp)
    // Unity Version Control / Unreal Source Control: değişiklik listesi + Commit / Submit, geçmiş, fark, Fetch / Pull / Push, dallar,
    // Perforce Get Latest / Shelve. Kendi sürüm denetimimiz yok: gerçek git ve p4 komut satırı araçları çalıştırılır. Bu işlevler
    // eşzamanlıdır ve ImGui'ye dokunmaz: pencere onları arka plan iş parçacığında çağırır (Context ana iş parçacığında alınır).
    struct Context
    {
        std::string ProjectDir;
        VersionControlSettings Settings;
        int Mode() const { return Settings.Mode; }
    };
    Context CurrentContext();

    struct Result
    {
        bool Ok = false;
        std::string Output;        // komut çıktısı / açıklama (kullanıcıya gösterilir)
        bool NeedsForce = false;   // Git: dal birleştirilmemiş (git branch -D gerekir)
    };

    // Gönderilecek değişiklik (Changes görünümü)
    struct Change
    {
        std::string Path;          // gösterilen yol: proje göreli ('/' ayraçlı)
        std::string RepoPath;      // Git: depo köküne göre yol; Perforce: yerel mutlak yol
        std::string OldRepoPath;   // Git: yeniden adlandırmanın eski yolu
        char Kind = 'M';           // M değişti, A eklendi, D silindi, R yeniden adlandırıldı, ? sürümlenmiyor, U çakışma
        std::string Changelist;    // Perforce: "default" ya da numara
        std::string DepotPath;     // Perforce
    };
    const char* ChangeKindName(char kind);
    std::vector<Change> ListChanges(const Context& ctx, std::string* error = nullptr);
    // Git: git add + git commit (yalnız verilen dosyalar); Perforce: dosyalar numaralı listeye taşınır (default ise yeni liste açılır),
    // açıklama yazılır, p4 submit -c
    Result Commit(const Context& ctx, const std::vector<Change>& files, const std::string& message, const std::string& changelist = "default");
    // Perforce: Commit gibi listeyi hazırlar, sonra p4 shelve -f -c (dosyalar açık kalır)
    Result Shelve(const Context& ctx, const std::vector<Change>& files, const std::string& description, const std::string& changelist = "default");
    // Git: değişen dosya HEAD'e döner, eklenen dosya izlemeden çıkar (silinmez), sürümlenmeyen dosyaya dokunulmaz; Perforce: p4 revert
    Result RevertChanges(const Context& ctx, const std::vector<Change>& files);

    struct Changelist
    {
        std::string Number;
        std::string Description;
    };
    std::vector<Changelist> PendingChangelists(const Context& ctx);   // Perforce: p4 changes -s pending -c <çalışma alanı>

    // Geçmiş
    struct CommitInfo
    {
        std::string Id;            // Git: tam karma, Perforce: değişiklik numarası
        std::string ShortId;
        std::string Author;
        std::string Email;
        std::string Date;          // yerel saat, "YYYY-MM-DD HH:MM"
        std::string Message;       // tam ileti
        std::string Summary;       // ilk satır
        std::string FileRevision;  // Perforce dosya geçmişi: dosyanın sürümü (#n)
    };
    // path boş: projenin geçmişi; dosya ya da klasör (proje göreli ya da mutlak): yalnız onu değiştiren kayıtlar
    std::vector<CommitInfo> Log(const Context& ctx, const std::string& path, int maxCount, std::string* error = nullptr);
    struct CommitFile
    {
        std::string Path;          // gösterilen yol (Git: proje göreli, dışındaysa "/depo yolu"; Perforce: depo yolu)
        std::string RepoPath;      // Git: depo köküne göre
        std::string OldRepoPath;   // Git: yeniden adlandırma
        char Kind = 'M';
        std::string DepotPath;     // Perforce
        std::string Revision;      // Perforce: bu değişiklikteki sürüm
        std::string Action;        // Perforce: edit / add / delete ...
    };
    std::vector<CommitFile> CommitFiles(const Context& ctx, const std::string& id, std::string* error = nullptr);

    // Fark (birleşik fark satırları; yan yana görünüm pencerede kurulur)
    struct DiffLine
    {
        char Kind = ' ';           // ' ' bağlam, '+' eklendi, '-' silindi, '@' parça başlığı
        int OldLine = 0;
        int NewLine = 0;
        std::string Text;
    };
    struct Diff
    {
        bool Ok = false;
        bool Binary = false;
        bool Identical = false;
        long long OldSize = -1;    // ikili dosya boyutları (-1: o tarafta yok)
        long long NewSize = -1;
        std::string Title;
        std::string OldLabel;
        std::string NewLabel;
        std::string Error;
        std::vector<DiffLine> Lines;
        int Added = 0;
        int Removed = 0;
    };
    // Çalışma kopyası ile HEAD (Git) / sahip olunan sürüm (Perforce)
    Diff DiffWorkingCopy(const Context& ctx, const Change& change);
    // Herhangi bir dosya (Inspector / Content Browser): durumuna göre DiffWorkingCopy
    Diff DiffWorkingFile(const Context& ctx, const std::string& path);
    // Geçmişteki kayıttaki dosya ile kaydın (ilk) ebeveyni / önceki sürümü
    Diff DiffInCommit(const Context& ctx, const std::string& id, const CommitFile& file);
    // Birleşik fark metnini ayrıştırır (test / araçlar)
    void ParseUnifiedDiff(const std::string& text, Diff& diff);

    // Depo / çalışma alanı bilgisi ve eşitleme
    struct RepoInfo
    {
        bool IsRepo = false;
        std::string Error;
        // Git
        std::string Branch;        // ayrık HEAD'de kısa karma
        bool Detached = false;
        std::string Upstream;      // ör. origin/main (yoksa boş)
        int Ahead = 0;
        int Behind = 0;
        std::vector<std::string> Remotes;
        std::string Operation;     // süren işlem: rebase / merge / cherry-pick (yoksa boş)
        std::vector<std::string> Conflicts;   // çakışan dosyalar (depo yolu)
        int UncommittedTracked = 0;           // izlenen ama kaydedilmemiş dosya sayısı
        // Perforce
        std::string User, Workspace, Stream, Root, Server;
    };
    RepoInfo GetRepoInfo(const Context& ctx);

    struct Branch
    {
        std::string Name;          // yerel: main, uzak: origin/main
        bool Current = false;
        bool Remote = false;
        std::string Upstream;
        std::string Track;         // "[ahead 1, behind 2]"
        std::string Date;
        std::string Subject;
    };
    std::vector<Branch> ListBranches(const Context& ctx, std::string* error = nullptr);
    Result Fetch(const Context& ctx);
    Result Pull(const Context& ctx, bool rebase);
    Result Push(const Context& ctx);
    Result CreateBranch(const Context& ctx, const std::string& name, bool switchTo);
    // Kaydedilmemiş değişiklik varken reddeder (nedenini söyler); uzak dal: izleyen yerel dal açılır
    Result SwitchBranch(const Context& ctx, const Branch& branch);
    Result DeleteBranch(const Context& ctx, const std::string& name, bool force);
    Result AddRemote(const Context& ctx, const std::string& name, const std::string& url);
    enum class Resolve { Mine, Theirs, MarkResolved };
    Result ResolveConflict(const Context& ctx, const std::string& repoPath, Resolve how);
    Result ContinueOperation(const Context& ctx);   // rebase --continue / merge kaydı
    Result AbortOperation(const Context& ctx);
    Result GetLatest(const Context& ctx);           // Perforce: p4 sync (Git: Pull)
}
