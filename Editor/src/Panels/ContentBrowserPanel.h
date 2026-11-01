#pragma once

#include <imgui.h>
#include "Scene/Entity.h"

#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

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
        const std::filesystem::path& GetCurrentDirectory() const { return m_CurrentDirectory; }
        void NavigateTo(const std::filesystem::path& directory, bool recordHistory = true);
        // Dosyanın klasörüne git ve seç (ör. "Select Prefab Asset")
        void Reveal(const std::filesystem::path& file)
        {
            NavigateTo(file.parent_path());
            m_SelectedPath = file;
        }
        // Panel klavye odağında mı (genel kısayollar varlık yerine dosyaya uygulanmasın)
        bool IsFocused() const { return m_Focused; }

        // Varlık türü bilgisi (ikon + renk + ad)
        struct AssetTypeInfo
        {
            const char* Icon;
            ImU32 Color;
            const char* Label;
        };
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
        void DrawListItem(const Entry& entry);
        void DrawItemContextMenu(const Entry& entry);
        void DrawBackgroundContextMenu();
        void DrawCreateMenu();
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
        void OpenFile(const std::filesystem::path& path);
        bool MoveInto(const std::filesystem::path& source, const std::filesystem::path& directory);
        void BeginRename(const std::filesystem::path& path);
        void CommitRename();
        void DuplicatePath(const std::filesystem::path& path);
        void DeletePath(const std::filesystem::path& path);
        std::filesystem::path MakeUniquePath(const std::filesystem::path& desired) const;
        std::filesystem::path CreateAsset(const std::string& kind);
        void ImportFile();
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
    };
}
