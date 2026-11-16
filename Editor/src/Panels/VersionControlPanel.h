#pragma once

#include "VersionControl/VersionControl.h"

#include <chrono>
#include <deque>
#include <filesystem>
#include <functional>
#include <future>
#include <set>
#include <string>
#include <vector>

namespace Conqueror::Editor
{
    // Window > Version Control (Unity Version Control penceresi / Unreal Source Control karşılığı; Git ve Perforce, gerçek git / p4 araçları):
    //  - Changes: değişen dosyalar (durum simgesi, yol, gönderime dahil kutusu), açıklama, Commit (Git) / Submit (Perforce: default ya da
    //    numaralı liste), seçili dosyalarda Revert, Perforce Shelve; çift tık farkı açar
    //  - History: proje ya da tek varlık geçmişi (yazar, tarih, ileti), seçili kaydın dosyaları, dosyaya çift tık: kayıttaki fark
    //  - Branches & Sync: Fetch / Pull (rebase ya da merge, çakışma listesi) / Push (ileride / geride sayıları), dallar (Create / Switch /
    //    Delete), uzak depolar; Perforce: Get Latest, çalışma alanı / akış
    //  - Diff penceresi: yan yana ya da birleşik, renkli eklenen / silinen satırlar; ikili dosyada boyutlar
    // Komutlar arka plan iş parçacığında çalışır (editör donmaz); otomasyon komutları ("vcsui") aynı işleri eşzamanlı çalıştırır.
    class VersionControlPanel
    {
    public:
        enum class Tab { Changes = 0, History, Sync };

        static VersionControlPanel& Get();
        // Her kare (pencere kapalıyken de: biten işlerin sonucu alınır)
        void OnImGuiRender();
        bool& GetIsOpen() { return m_IsOpen; }
        void Open(Tab tab);
        // Content Browser / Inspector: varlığın geçmişi, çalışma kopyası farkı
        void ShowHistory(const std::string& path);
        void ShowDiff(const std::string& path);
        void RegisterAutomation();

        // EditorLayer: açık sahnenin yolu ve yeniden yükleme (Pull / Switch / Revert sahne dosyasını değiştirdiyse bildirim düğmesi)
        std::function<std::string()> CurrentScenePath;
        std::function<void()> ReloadScene;

    private:
        using Apply = std::function<void()>;
        using Work = std::function<Apply()>;
        struct Job
        {
            std::string Label;
            Work Run;
        };
        struct SideRow
        {
            int Left = -1;
            int Right = -1;
            bool Header = false;
        };

        void Submit(const std::string& label, Work work);
        void Poll();
        void WaitIdle();
        bool Busy() const { return m_Job.valid() || !m_Queue.empty(); }

        void RefreshAll();
        void LoadHistory();
        void LoadCommitFiles(int index);
        // Sonucu durum satırına yazan işlem; changesFiles: dosyaları değiştirebilir (sahne yenileme bildirimi), history: geçmişi yenile
        void RunOp(const std::string& label, std::function<VersionControl::Result(const VersionControl::Context&)> op, bool changesFiles, bool history,
                   std::function<void(const VersionControl::Result&)> done = {});
        void SetStatus(bool ok, const std::string& text);
        void SetDiff(VersionControl::Diff diff);

        std::vector<VersionControl::Change> VisibleChanges() const;
        std::vector<VersionControl::Change> CheckedChanges() const;
        std::vector<VersionControl::Change> SelectedChanges() const;
        void DoCommit();
        void DoShelve();
        void DoRevertSelected();
        void DoDiffChange(const VersionControl::Change& change);
        void DoDiffCommitFile(int fileIndex);
        void DoSwitch(const VersionControl::Branch& branch);
        void DoDelete(const std::string& name, bool force);

        void DrawToolbar();
        void DrawChanges();
        void DrawHistory();
        void DrawSync();
        void DrawDiffWindow();

        bool m_IsOpen = false;
        int m_RequestTab = -1;
        bool m_Sync = false;   // otomasyon: işler hemen, bu iş parçacığında

        std::future<Apply> m_Job;
        std::string m_JobLabel;
        std::deque<Job> m_Queue;
        std::chrono::steady_clock::time_point m_LastRefresh{};
        std::string m_ContextKey;   // proje + kip: değişince yenilenir

        std::string m_Status;
        bool m_StatusOk = true;
        bool m_LastOk = true;
        bool m_SceneChanged = false;

        // Changes
        std::vector<VersionControl::Change> m_Changes;
        std::string m_ChangesError;
        std::set<std::string> m_Unchecked;
        std::set<std::string> m_Selected;
        std::string m_LastClicked;
        char m_Message[4096] = "";
        std::vector<VersionControl::Changelist> m_Changelists;
        std::string m_Changelist = "default";
        bool m_ConfirmRevert = false;

        // History
        std::string m_HistoryPath;
        std::vector<VersionControl::CommitInfo> m_Commits;
        std::string m_HistoryError;
        bool m_HistoryLoaded = false;
        int m_SelectedCommit = -1;
        std::vector<VersionControl::CommitFile> m_CommitFiles;
        std::string m_CommitFilesError;
        int m_SelectedCommitFile = -1;
        char m_HistoryFilter[128] = "";
        int m_HistoryMax = 300;

        // Branches & Sync
        VersionControl::RepoInfo m_Info;
        std::vector<VersionControl::Branch> m_Branches;
        char m_NewBranch[128] = "";
        bool m_SwitchToNew = true;
        char m_RemoteName[64] = "origin";
        char m_RemoteUrl[512] = "";
        bool m_PullRebase = true;
        std::string m_DeleteBranch;
        bool m_DeleteNeedsForce = false;
        bool m_OpenDeletePopup = false;

        // Diff
        VersionControl::Diff m_Diff;
        std::vector<SideRow> m_SideRows;
        bool m_ShowDiff = false;
        bool m_FocusDiff = false;
        bool m_SideBySide = true;
    };
}
