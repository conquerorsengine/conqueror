#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <vector>
#include <functional>

namespace Conqueror
{
    class Texture2D;
}

namespace Conqueror::Editor
{
    class ContentBrowserPanel
    {
    public:
        ContentBrowserPanel();
        ~ContentBrowserPanel() = default;

        void OnImGuiRender();
        void RefreshContext();

        using FileOpenCallbackFn = std::function<void(const std::filesystem::path&)>;
        void SetFileOpenCallback(const FileOpenCallbackFn& callback) { m_FileOpenCallback = callback; }

    private:
        void RenderFolderTree(const std::filesystem::path& path);
        void RenderContentGrid();
        void RenderBreadcrumb();
        void RenderContextMenu();
        
        void LoadIcons();
        void CreateNewFolder();
        void CreateNewScript();
        void CreateConquerorScript();
        void CreateShader();
        void CreateEmptyFile();
        void DeleteSelected();
        void RenameSelected();
        void DuplicateSelected();
        void OpenFile(const std::filesystem::path& path);
        void ImportFile();
        
        std::shared_ptr<Texture2D> GetFileIconTexture(const std::filesystem::path& path);
        std::string GetFileIcon(const std::filesystem::path& path);
        std::string GetFileNameWithoutExtension(const std::filesystem::path& path);
        
    private:
        std::filesystem::path m_CurrentDirectory;
        std::filesystem::path m_BaseDirectory;
        
        // İkonlar
        std::shared_ptr<Texture2D> m_FolderIcon;
        std::shared_ptr<Texture2D> m_FileIcon;
        std::shared_ptr<Texture2D> m_ScriptIcon;
        std::shared_ptr<Texture2D> m_ImageIcon;
        std::shared_ptr<Texture2D> m_SceneIcon;
        
        // Seçili item
        std::filesystem::path m_SelectedPath;
        
        // Rename state
        bool m_IsRenaming = false;
        char m_RenameBuffer[256] = {};
        
        // New folder/script state
        bool m_CreatingNewFolder = false;
        bool m_CreatingNewScript = false;
        bool m_CreatingConquerorScript = false;
        bool m_CreatingShader = false;
        bool m_CreatingEmptyFile = false;
        char m_NewItemNameBuffer[256] = {};

        FileOpenCallbackFn m_FileOpenCallback;
    };
}
