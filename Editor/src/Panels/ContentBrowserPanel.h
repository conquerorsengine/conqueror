#pragma once

#include <imgui.h>
#include "Scene/Entity.h"

#include <chrono>
#include <filesystem>
#include <set>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Conqueror { struct SpriteDef; }

namespace Conqueror
{
    class Texture2D;
}

namespace Conqueror::Editor
{
    // Proje varlık tarayıcısı (Unity Project penceresi / Unreal Content Browser karşılığı):
    // klasör ağacı, geçmişli gezinme, arama, ızgara/liste görünümü, resim önizlemeleri,
    // sürükle-bırak ile taşıma, yerinde yeniden adlandırma, varlık oluşturma.
    class ContentBrowserPanel
    {
    public:
        ContentBrowserPanel();
        ~ContentBrowserPanel() = default;

        void OnImGuiRender();
        void RefreshContext();

        using FileOpenCallbackFn = std::function<void(const std::filesystem::path&)>;
        void SetFileOpenCallback(const FileOpenCallbackFn& callback) { m_FileOpenCallback = callback; }
        void SetSpriteEditorCallback(const FileOpenCallbackFn& callback) { m_SpriteEditorCallback = callback; }

        const std::filesystem::path& GetSelectedPath() const { return m_SelectedPath; }
        // Dosya yöneticisinden bırakılanlar: bir sonraki çizimde üzerinde durulan klasöre ya da açık klasöre kopyalanır (Unity Project)
        void QueueDroppedFiles(const std::vector<std::string>& paths) { m_DroppedFiles.insert(m_DroppedFiles.end(), paths.begin(), paths.end()); }
        // Panel kapalıyken / sekmesi görünmüyorken bırakılan dosyalar: geçerli klasöre (yazılamıyorsa Assets'e) hemen içe aktarılır
        void ImportPendingDrops();
        const std::filesystem::path& GetCurrentDirectory() const { return m_CurrentDirectory; }
        void NavigateTo(const std::filesystem::path& directory, bool recordHistory = true);
        // Dosyanın klasörüne git ve seç (ör. "Select Prefab Asset")
        void Reveal(const std::filesystem::path& file)
        {
            NavigateTo(file.parent_path());
            m_SelectedPath = file;
            m_ScrollToSelected = true;
        }
        // Panel klavye odağında mı (genel kısayollar varlık yerine dosyaya uygulanmasın)
        bool IsFocused() const { return m_Focused; }
        // Sağ tık menüsündeki "<tür>" girişiyle aynı varlığı geçerli klasörde oluştur (otomasyon / testler); yeni yol
        std::filesystem::path CreateAssetOfKind(const std::string& kind) { return CreateAsset(kind); }

        // ---- Unity Project menüsü (sağ tık) işlemleri: menü, kısayollar ve otomasyon aynı yolu kullanır ----
        // EditorLayer bağlantıları (sahne ve pencere işleri)
        static inline std::function<int(const std::string&)> s_FindReferencesInScene;   // eşleşen nesneleri seçer, sayıyı döner
        static inline std::function<bool(const std::string&)> s_OpenSceneAdditive;
        static inline std::function<void(const std::string&)> s_ReloadModelInScene;
        static inline std::function<void(const std::string&)> s_OpenAddonManager;       // addon kimliği
        static inline std::function<void()> s_OpenScriptProject;
        static inline std::function<void(const std::string&)> s_OpenImportActivity;    // süzgeç (yol)

        void CopyAssets(const std::vector<std::filesystem::path>& paths, bool cut);
        bool PasteInto(const std::filesystem::path& directory);
        bool HasClipboard() const { return !m_Clipboard.empty(); }
        // Find References In Project: bu varlığın yolunu içeren proje dosyaları (sahne, prefab, malzeme, betik ...)
        std::vector<std::filesystem::path> FindReferencesInProject(const std::filesystem::path& asset) const;
        // Select Dependencies: varlığın kullandığı proje dosyaları (özyineli; kendisi dahil)
        std::vector<std::filesystem::path> CollectDependencies(const std::filesystem::path& asset) const;
        // Sonuç görünümü (Unity'de Project penceresi süzgeci): başlık + dosyalar; boş başlık kapatır
        void ShowResults(const std::string& title, std::vector<std::filesystem::path> results);
        const std::vector<std::filesystem::path>& GetResults() const { return m_Results; }
        // Paketler (.cqpackage: tar.gz; yollar proje köküne göre)
        bool ExportPackage(const std::vector<std::filesystem::path>& files, const std::filesystem::path& file, std::string* error = nullptr);
        bool BeginImportPackage(const std::filesystem::path& file, std::string* error = nullptr);   // "Import Package" penceresini açar
        int CommitImportPackage();                                                                  // seçili dosyaları yazar
        void BeginExportPackage(const std::vector<std::filesystem::path>& roots);                   // "Exporting Package" penceresi
        void Reimport(const std::filesystem::path& path);
        void ReimportAll();
        void OpenProperties(const std::filesystem::path& path);
        bool ExtractModelMaterials(const std::filesystem::path& model);
        void SelectAndReveal(const std::filesystem::path& path);

        // Varlık türü bilgisi (ikon + renk + ad)
        struct AssetTypeInfo
        {
            const char* Icon;
            ImU32 Color;
            const char* Label;
        };
        // Kayıtlı aramayı uygula (Search varlığı)
        void SetSearch(const std::string& query);
        const char* GetSearch() const { return m_Search; }

        // Motor paketleri (Packages/Core) salt okunur
        static bool IsReadOnly(const std::filesystem::path& path);
        // Yeni varlıkların yazılacağı klasör (paket içindeyken proje Assets)
        std::filesystem::path WritableDirectory() const;
        static AssetTypeInfo GetAssetTypeInfo(const std::filesystem::path& path, bool isDirectory);

    private:
        struct Entry
        {
            std::filesystem::path Path;
            std::string Name;      // uzantısız ad (klasörde tam ad)
            std::string Extension; // küçük harf
            bool IsDirectory = false;
            uintmax_t Size = 0;
            std::filesystem::file_time_type WriteTime{};
        };

        void DrawToolbar();
        void DrawFolderTree(const std::filesystem::path& path, int depth);
        void DrawContent();
        void DrawGridItem(const Entry& entry, float thumb);
        void DrawSliceItem(const Entry& entry, const SpriteDef& slice, float thumb);
        void DrawListItem(const Entry& entry);
        void DrawItemContextMenu(const Entry& entry);
        void DrawBackgroundContextMenu();
        void DrawCreateMenu();
        // Create menüsü girdileri (kategori > alt menü > giriş); arama da bunları kullanır
        struct CreateItem
        {
            std::string Category;    // "" en üst, "~" en alt, diğerleri Scene / 2D / Rendering ...
            std::string SubMenu;     // ör. 2D > Sprites
            std::string Label;
            std::string Icon;
            std::string Kind;        // CreateAsset türü; boş: bilgi satırı
            std::string Tooltip;
            bool Enabled = true;
            bool SeparatorBefore = false;
        };
        std::vector<CreateItem> CollectCreateItems();
        void RunCreate(const std::string& kind);
        // Unity Project menüsü: en üstte arama ve Asset Options ikonları, sonra türe özel seçenekler ve Project girişleri
        void DrawProjectMenu(const std::filesystem::path& target, bool isDirectory);
        // Menü işlemi: normal menüde ve aramada aynı tanım
        struct MenuAction
        {
            std::string Label;
            std::string Icon;
            std::string Shortcut;
            std::string SubMenu;     // ör. "Import Package"
            bool Enabled = true;
            bool SeparatorBefore = false;
            std::string Tooltip;
            std::function<void()> Run;
        };
        std::vector<MenuAction> TypeActions(const std::filesystem::path& target, bool isDirectory);
        std::vector<MenuAction> ProjectActions(const std::filesystem::path& target, bool isDirectory);
        void DrawPackageWindows();
        void DrawPropertyWindows();
        Entry MakeEntry(const std::filesystem::path& path) const;
        static bool CopyWithSidecars(const std::filesystem::path& source, const std::filesystem::path& target);
        void DrawModals();
        void DrawStatusBar();
        void HandleItemInteraction(const Entry& entry);
        void HandleKeyboard();

        // Kart çizimleri
        void DrawFolderGlyph(ImDrawList* dl, const ImVec2& min, float size, bool open, bool hasContent);
        void DrawFileGlyph(ImDrawList* dl, const ImVec2& min, float size, const Entry& entry);

        void RefreshEntries(bool force = false);
        // Hiyerarşiden bırakılan varlıktan prefab oluştur
        void CreatePrefabFrom(Entity entity, const std::filesystem::path& directory);
        std::shared_ptr<Texture2D> GetThumbnail(const Entry& entry);
        bool IsImage(const std::string& ext) const;

        // Dosya işlemleri
    public:
        void OpenFile(const std::filesystem::path& path);
    private:
        bool MoveInto(const std::filesystem::path& source, const std::filesystem::path& directory);
        void BeginRename(const std::filesystem::path& path);
        void CommitRename();
        void DuplicatePath(const std::filesystem::path& path);
        void DeletePath(const std::filesystem::path& path);
        std::filesystem::path MakeUniquePath(const std::filesystem::path& desired) const;
        std::filesystem::path CreateAsset(const std::string& kind);
        void ImportFile();
        void ImportDroppedFiles();
        std::vector<std::string> m_DroppedFiles;
        std::filesystem::path m_HoveredFolder;   // bu karede fare altındaki klasör
        void RevealInFileManager(const std::filesystem::path& path);

    private:
        std::filesystem::path m_NewScriptPath;     // oluşturulup henüz adlandırılan betik
        std::string m_NewScriptClass;
        std::filesystem::path m_CurrentDirectory;
        std::filesystem::path m_BaseDirectory;

        // Gezinme geçmişi
        std::vector<std::filesystem::path> m_BackStack;
        std::vector<std::filesystem::path> m_ForwardStack;

        // Dizin içeriği önbelleği
        std::vector<Entry> m_Entries;
        std::filesystem::path m_EntriesDirectory;
        std::chrono::steady_clock::time_point m_LastRefresh{};
        bool m_Dirty = true;

        // Önizleme önbelleği
        struct Thumbnail
        {
            std::shared_ptr<Texture2D> Texture;
            std::filesystem::file_time_type WriteTime{};
            bool Failed = false;
        };
        std::unordered_map<std::string, Thumbnail> m_Thumbnails;
        int m_ThumbnailLoadsThisFrame = 0;

        // Seçim / yeniden adlandırma
        std::filesystem::path m_SelectedPath;
        std::string m_SelectedSlice;                 // görselin seçili dilimi (> oku)
        std::string m_PendingInspectorSlice;         // fare bırakılınca Inspector'a geçecek dilim
        std::set<std::string> m_ExpandedSheets;      // > oku açık görseller
        std::filesystem::path m_PendingInspectorAsset;   // tıklandı, bırakılınca Inspector'da açılacak
        std::filesystem::path m_RenamingPath;
        char m_RenameBuffer[256] = {};
        int m_RenameFrame = 0;
        std::filesystem::path m_PendingDelete;
        bool m_OpenDeleteModal = false;

        // Görünüm
        float m_ThumbnailSize = 84.0f;
        bool m_ListView = false;
        char m_Search[128] = {};
        bool m_Focused = false;
        float m_TreeWidth = 210.0f;

        FileOpenCallbackFn m_FileOpenCallback;
        FileOpenCallbackFn m_SpriteEditorCallback;

        // Varlık panosu (Asset Options: Kes / Kopyala / Yapıştır)
        std::vector<std::filesystem::path> m_Clipboard;
        bool m_ClipboardCut = false;
        // Menü araması
        char m_MenuSearch[96] = {};
        // Sonuç görünümü
        std::string m_ResultsTitle;
        std::vector<std::filesystem::path> m_Results;
        // Paket pencereleri
        struct PackageItem
        {
            std::string Path;        // proje köküne göre
            bool Selected = true;
            bool Exists = false;     // içe aktarmada: projede zaten var (üzerine yazılır)
            size_t Size = 0;
        };
        bool m_ExportOpen = false;
        bool m_ExportDependencies = true;
        std::vector<std::filesystem::path> m_ExportRoots;
        std::vector<PackageItem> m_ExportItems;
        bool m_ImportOpen = false;
        std::string m_ImportFile;
        std::vector<PackageItem> m_ImportItems;
        std::vector<std::pair<std::string, std::vector<uint8_t>>> m_ImportData;
        // Properties pencereleri (Alt+P)
        std::vector<std::filesystem::path> m_PropertyWindows;
        // Reveal / Select: seçili varlık görünür olsun diye bir kez kaydır (Unity "ping")
        bool m_ScrollToSelected = false;
    };
}
