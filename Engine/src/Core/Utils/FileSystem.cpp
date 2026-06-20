#include "FileSystem.h"
#include "Core/Logging/Log.h"

#include <fstream>
#include <sstream>

#ifdef _WIN32
    #include <windows.h>
    #include <direct.h>
    #define getcwd _getcwd
#else
    #include <unistd.h>
    #include <limits.h>
#endif

namespace Conqueror
{
    namespace FileSystem
    {
        bool Exists(const std::string& path)
        {
            return std::filesystem::exists(path);
        }

        bool IsFile(const std::string& path)
        {
            return std::filesystem::is_regular_file(path);
        }

        bool IsDirectory(const std::string& path)
        {
            return std::filesystem::is_directory(path);
        }

        bool ReadFile(const std::string& path, std::string& outContent)
        {
            std::ifstream file(path, std::ios::in);
            if (!file.is_open())
            {
                CQ_CORE_ERROR("Failed to open file: {0}", path);
                return false;
            }

            std::stringstream buffer;
            buffer << file.rdbuf();
            outContent = buffer.str();
            file.close();
            return true;
        }

        bool ReadFileBinary(const std::string& path, std::vector<uint8_t>& outData)
        {
            std::ifstream file(path, std::ios::binary | std::ios::ate);
            if (!file.is_open())
            {
                CQ_CORE_ERROR("Failed to open file: {0}", path);
                return false;
            }

            size_t size = file.tellg();
            file.seekg(0, std::ios::beg);

            outData.resize(size);
            file.read(reinterpret_cast<char*>(outData.data()), size);
            file.close();
            return true;
        }

        bool WriteFile(const std::string& path, const std::string& content)
        {
            std::ofstream file(path, std::ios::out | std::ios::trunc);
            if (!file.is_open())
            {
                CQ_CORE_ERROR("Failed to create file: {0}", path);
                return false;
            }

            file << content;
            file.close();
            return true;
        }

        bool WriteFileBinary(const std::string& path, const std::vector<uint8_t>& data)
        {
            std::ofstream file(path, std::ios::binary | std::ios::trunc);
            if (!file.is_open())
            {
                CQ_CORE_ERROR("Failed to create file: {0}", path);
                return false;
            }

            file.write(reinterpret_cast<const char*>(data.data()), data.size());
            file.close();
            return true;
        }

        bool CreateDirectory(const std::string& path)
        {
            return std::filesystem::create_directory(path);
        }

        bool CreateDirectories(const std::string& path)
        {
            return std::filesystem::create_directories(path);
        }

        bool DeleteFile(const std::string& path)
        {
            return std::filesystem::remove(path);
        }

        bool DeleteDirectory(const std::string& path, bool recursive)
        {
            if (recursive)
                return std::filesystem::remove_all(path) > 0;
            else
                return std::filesystem::remove(path);
        }

        bool CopyFile(const std::string& src, const std::string& dest, bool overwrite)
        {
            auto options = overwrite ? std::filesystem::copy_options::overwrite_existing 
                                     : std::filesystem::copy_options::none;
            
            try
            {
                std::filesystem::copy_file(src, dest, options);
                return true;
            }
            catch (const std::exception& e)
            {
                CQ_CORE_ERROR("Failed to copy file: {0}", e.what());
                return false;
            }
        }

        bool MoveFile(const std::string& src, const std::string& dest)
        {
            try
            {
                std::filesystem::rename(src, dest);
                return true;
            }
            catch (const std::exception& e)
            {
                CQ_CORE_ERROR("Failed to move file: {0}", e.what());
                return false;
            }
        }

        bool Rename(const std::string& oldPath, const std::string& newPath)
        {
            return MoveFile(oldPath, newPath);
        }

        uint64_t GetFileSize(const std::string& path)
        {
            try
            {
                return std::filesystem::file_size(path);
            }
            catch (...)
            {
                return 0;
            }
        }

        uint64_t GetLastWriteTime(const std::string& path)
        {
            try
            {
                auto ftime = std::filesystem::last_write_time(path);
                return ftime.time_since_epoch().count();
            }
            catch (...)
            {
                return 0;
            }
        }

        std::string GetExtension(const std::string& path)
        {
            return std::filesystem::path(path).extension().string();
        }

        std::string GetFileName(const std::string& path)
        {
            return std::filesystem::path(path).filename().string();
        }

        std::string GetFileNameWithoutExtension(const std::string& path)
        {
            return std::filesystem::path(path).stem().string();
        }

        std::string GetDirectoryPath(const std::string& path)
        {
            return std::filesystem::path(path).parent_path().string();
        }

        std::string ChangeExtension(const std::string& path, const std::string& extension)
        {
            std::filesystem::path p(path);
            return p.replace_extension(extension).string();
        }

        std::string GetAbsolutePath(const std::string& path)
        {
            return std::filesystem::absolute(path).string();
        }

        std::string GetRelativePath(const std::string& path, const std::string& base)
        {
            return std::filesystem::relative(path, base).string();
        }

        std::string CombinePaths(const std::string& path1, const std::string& path2)
        {
            return (std::filesystem::path(path1) / path2).string();
        }

        std::string NormalizePath(const std::string& path)
        {
            return std::filesystem::path(path).lexically_normal().string();
        }

        bool IsRelativePath(const std::string& path)
        {
            return std::filesystem::path(path).is_relative();
        }

        bool IsAbsolutePath(const std::string& path)
        {
            return std::filesystem::path(path).is_absolute();
        }

        std::vector<std::string> ListFiles(const std::string& directory, bool recursive)
        {
            std::vector<std::string> files;
            
            try
            {
                if (recursive)
                {
                    for (const auto& entry : std::filesystem::recursive_directory_iterator(directory))
                    {
                        if (entry.is_regular_file())
                            files.push_back(entry.path().string());
                    }
                }
                else
                {
                    for (const auto& entry : std::filesystem::directory_iterator(directory))
                    {
                        if (entry.is_regular_file())
                            files.push_back(entry.path().string());
                    }
                }
            }
            catch (const std::exception& e)
            {
                CQ_CORE_ERROR("Failed to list files: {0}", e.what());
            }
            
            return files;
        }

        std::vector<std::string> ListDirectories(const std::string& directory, bool recursive)
        {
            std::vector<std::string> directories;
            
            try
            {
                if (recursive)
                {
                    for (const auto& entry : std::filesystem::recursive_directory_iterator(directory))
                    {
                        if (entry.is_directory())
                            directories.push_back(entry.path().string());
                    }
                }
                else
                {
                    for (const auto& entry : std::filesystem::directory_iterator(directory))
                    {
                        if (entry.is_directory())
                            directories.push_back(entry.path().string());
                    }
                }
            }
            catch (const std::exception& e)
            {
                CQ_CORE_ERROR("Failed to list directories: {0}", e.what());
            }
            
            return directories;
        }

        std::vector<std::string> ListFilesWithExtension(const std::string& directory, const std::string& extension, bool recursive)
        {
            std::vector<std::string> files;
            
            try
            {
                if (recursive)
                {
                    for (const auto& entry : std::filesystem::recursive_directory_iterator(directory))
                    {
                        if (entry.is_regular_file() && entry.path().extension() == extension)
                            files.push_back(entry.path().string());
                    }
                }
                else
                {
                    for (const auto& entry : std::filesystem::directory_iterator(directory))
                    {
                        if (entry.is_regular_file() && entry.path().extension() == extension)
                            files.push_back(entry.path().string());
                    }
                }
            }
            catch (const std::exception& e)
            {
                CQ_CORE_ERROR("Failed to list files with extension: {0}", e.what());
            }
            
            return files;
        }

        std::string GetCurrentDirectory()
        {
            return std::filesystem::current_path().string();
        }

        bool SetCurrentDirectory(const std::string& path)
        {
            try
            {
                std::filesystem::current_path(path);
                return true;
            }
            catch (...)
            {
                return false;
            }
        }

        std::string GetExecutablePath()
        {
#ifdef _WIN32
            char buffer[MAX_PATH];
            GetModuleFileNameA(NULL, buffer, MAX_PATH);
            return std::string(buffer);
#else
            char buffer[PATH_MAX];
            ssize_t len = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
            if (len != -1)
            {
                buffer[len] = '\0';
                return std::string(buffer);
            }
            return "";
#endif
        }

        std::string GetExecutableDirectory()
        {
            return GetDirectoryPath(GetExecutablePath());
        }

        std::string GetTempDirectory()
        {
            return std::filesystem::temp_directory_path().string();
        }

        // FileWatcher implementation (basic stub)
        FileWatcher::FileWatcher(const std::string& path, FileWatchCallback callback)
            : m_Path(path), m_Callback(callback)
        {
        }

        FileWatcher::~FileWatcher()
        {
            Stop();
        }

        void FileWatcher::Start()
        {
            m_Watching = true;
            // Platform-specific implementation needed
        }

        void FileWatcher::Stop()
        {
            m_Watching = false;
        }
    }
}
