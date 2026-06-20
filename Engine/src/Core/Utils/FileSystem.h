#pragma once

#include "Core/Base/Base.h"
#include <string>
#include <vector>
#include <functional>
#include <filesystem>

namespace Conqueror
{
    namespace FileSystem
    {
        // Dosya varlık kontrolü
        CQ_API bool Exists(const std::string& path);
        CQ_API bool IsFile(const std::string& path);
        CQ_API bool IsDirectory(const std::string& path);

        // Dosya okuma/yazma
        CQ_API bool ReadFile(const std::string& path, std::string& outContent);
        CQ_API bool ReadFileBinary(const std::string& path, std::vector<uint8_t>& outData);
        CQ_API bool WriteFile(const std::string& path, const std::string& content);
        CQ_API bool WriteFileBinary(const std::string& path, const std::vector<uint8_t>& data);

        // Dosya/dizin işlemleri
        CQ_API bool CreateDirectory(const std::string& path);
        CQ_API bool CreateDirectories(const std::string& path);
        CQ_API bool DeleteFile(const std::string& path);
        CQ_API bool DeleteDirectory(const std::string& path, bool recursive = false);
        CQ_API bool CopyFile(const std::string& src, const std::string& dest, bool overwrite = false);
        CQ_API bool MoveFile(const std::string& src, const std::string& dest);
        CQ_API bool Rename(const std::string& oldPath, const std::string& newPath);

        // Dosya bilgileri
        CQ_API uint64_t GetFileSize(const std::string& path);
        CQ_API uint64_t GetLastWriteTime(const std::string& path);
        CQ_API std::string GetExtension(const std::string& path);
        CQ_API std::string GetFileName(const std::string& path);
        CQ_API std::string GetFileNameWithoutExtension(const std::string& path);
        CQ_API std::string GetDirectoryPath(const std::string& path);
        CQ_API std::string ChangeExtension(const std::string& path, const std::string& extension);
        CQ_API std::string GetDirectoryPath(const std::string& path);

        // Path işlemleri
        CQ_API std::string GetAbsolutePath(const std::string& path);
        CQ_API std::string GetRelativePath(const std::string& path, const std::string& base);
        CQ_API std::string CombinePaths(const std::string& path1, const std::string& path2);
        CQ_API std::string NormalizePath(const std::string& path);
        CQ_API bool IsAbsolutePath(const std::string& path);
        CQ_API bool IsRelativePath(const std::string& path);

        // Dizin listeleme
        CQ_API std::vector<std::string> ListFiles(const std::string& directory, bool recursive = false);
        CQ_API std::vector<std::string> ListDirectories(const std::string& directory, bool recursive = false);
        CQ_API std::vector<std::string> ListFilesWithExtension(const std::string& directory, const std::string& extension, bool recursive = false);

        // Özel dizinler
        CQ_API std::string GetCurrentDirectory();
        CQ_API bool SetCurrentDirectory(const std::string& path);
        CQ_API std::string GetExecutablePath();
        CQ_API std::string GetExecutableDirectory();
        CQ_API std::string GetTempDirectory();

        // File watching
        using FileWatchCallback = std::function<void(const std::string& path, bool created, bool modified, bool deleted)>;
        
        class CQ_API FileWatcher
        {
        public:
            FileWatcher(const std::string& path, FileWatchCallback callback);
            ~FileWatcher();

            void Start();
            void Stop();
            bool IsWatching() const { return m_Watching; }

        private:
            std::string m_Path;
            FileWatchCallback m_Callback;
            bool m_Watching = false;
            void* m_PlatformData = nullptr;
        };
    }
}
