#include "AssetBundle.h"

#include "Core/Logging/Log.h"
#include "Core/Project/Project.h"
#include "Core/Utils/Compression.h"
#include "Core/Utils/Crypto.h"
#include "Core/Utils/Platform.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <unordered_map>

namespace Conqueror::AssetBundles
{
    namespace fs = std::filesystem;

    namespace
    {
        constexpr char kMagic[4] = { 'C', 'Q', 'A', 'B' };
        constexpr uint32_t kVersion = 1;

        struct Mounted
        {
            std::string Name;           // arşiv adı (uzantısız)
            std::string File;           // arşivin tam yolu
            fs::path Root;              // çözüldüğü önbellek klasörü
            std::vector<std::string> Assets;
        };

        std::mutex& Mutex()
        {
            static std::mutex m;
            return m;
        }
        std::unordered_map<int, Mounted>& Bundles()
        {
            static std::unordered_map<int, Mounted> bundles;
            return bundles;
        }
        int& NextHandle()
        {
            static int next = 1;
            return next;
        }

        template<typename T> void Put(std::string& out, T v) { out.append(reinterpret_cast<const char*>(&v), sizeof(T)); }
        template<typename T> bool Take(std::ifstream& in, T& v) { return (bool)in.read(reinterpret_cast<char*>(&v), sizeof(T)); }

        bool ReadFile(const fs::path& path, std::string& data)
        {
            std::ifstream in(path, std::ios::binary);
            if (!in)
                return false;
            data.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
            return true;
        }

        // Tablo + veri başlangıcı
        bool ReadHeader(std::ifstream& in, std::vector<FileEntry>& entries, std::string* error)
        {
            char magic[4];
            uint32_t version = 0, count = 0;
            if (!in.read(magic, 4) || std::memcmp(magic, kMagic, 4) != 0 || !Take(in, version) || version != kVersion || !Take(in, count) || count > 1000000)
            {
                if (error) *error = "not an asset bundle (or an unsupported version)";
                return false;
            }
            entries.clear();
            entries.reserve(count);
            for (uint32_t i = 0; i < count; ++i)
            {
                uint32_t len = 0;
                FileEntry e;
                if (!Take(in, len) || len > 4096)
                {
                    if (error) *error = "corrupt file table";
                    return false;
                }
                e.Path.resize(len);
                if (!in.read(e.Path.data(), len) || !Take(in, e.Size) || !Take(in, e.CompressedSize) || !Take(in, e.Crc))
                {
                    if (error) *error = "corrupt file table";
                    return false;
                }
                // Güvenlik: arşivdeki yol önbellek klasörünün dışına çıkamaz
                const fs::path p = fs::path(e.Path).lexically_normal();
                if (p.is_absolute() || p.empty() || p.begin()->string() == "..")
                {
                    if (error) *error = "unsafe path in the bundle: " + e.Path;
                    return false;
                }
                entries.push_back(std::move(e));
            }
            return true;
        }

        fs::path CacheRoot()
        {
            std::string base = Platform::GetUserAppDataDirectory();
            std::error_code ec;
            fs::path root = base.empty() ? fs::temp_directory_path(ec) : fs::path(base);
            return root / "ConquerorEngine" / "AssetBundleCache";
        }

        std::string Hex(uint32_t v)
        {
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%08x", v);
            return buf;
        }
    }

    uint32_t FileCrc(const std::string& path)
    {
        std::string data;
        return ReadFile(path, data) ? Crypto::CRC32Compute(data) : 0u;
    }

    bool Write(const std::string& bundleFile, const std::vector<std::pair<std::string, std::string>>& files, std::string* error)
    {
        std::string table, blob;
        table.append(kMagic, 4);
        Put(table, kVersion);
        Put(table, (uint32_t)files.size());
        for (const auto& [path, source] : files)
        {
            std::string data;
            if (!ReadFile(source, data))
            {
                if (error) *error = "cannot read " + source;
                return false;
            }
            std::vector<uint8_t> packed;
            if (!Compression::Compress(data.data(), data.size(), packed, CompressionAlgorithm::Zstd, CompressionLevel::Default))
            {
                if (error) *error = "cannot compress " + source;
                return false;
            }
            const std::string rel = fs::path(path).generic_string();
            Put(table, (uint32_t)rel.size());
            table += rel;
            Put(table, (uint64_t)data.size());
            Put(table, (uint64_t)packed.size());
            Put(table, Crypto::CRC32Compute(data));
            blob.append(reinterpret_cast<const char*>(packed.data()), packed.size());
        }
        std::error_code ec;
        fs::create_directories(fs::path(bundleFile).parent_path(), ec);
        std::ofstream out(bundleFile, std::ios::binary | std::ios::trunc);
        if (!out)
        {
            if (error) *error = "cannot write " + bundleFile;
            return false;
        }
        out.write(table.data(), (std::streamsize)table.size());
        out.write(blob.data(), (std::streamsize)blob.size());
        return (bool)out;
    }

    bool ReadTable(const std::string& bundleFile, std::vector<FileEntry>& entries, std::string* error)
    {
        std::ifstream in(bundleFile, std::ios::binary);
        if (!in)
        {
            if (error) *error = "cannot open " + bundleFile;
            return false;
        }
        return ReadHeader(in, entries, error);
    }

    int Load(const std::string& bundleFileIn, std::string* error)
    {
        fs::path file(bundleFileIn);
        std::error_code ec;
        // Göreli yol: proje (editör) ya da oyunun veri klasörü
        if (file.is_relative() && Project::GetActive())
            if (fs::path p = Project::GetActiveProjectDirectory() / file; fs::exists(p, ec))
                file = p;
        file = file.lexically_normal();
        {
            std::lock_guard lock(Mutex());
            for (const auto& [handle, b] : Bundles())
                if (b.File == file.string())
                    return handle;
        }
        std::ifstream in(file, std::ios::binary);
        std::vector<FileEntry> entries;
        if (!in || !ReadHeader(in, entries, error))
        {
            if (error && error->empty()) *error = "cannot open " + file.string();
            CQ_CORE_ERROR("[AssetBundle] {0}: {1}", file.string(), error ? *error : std::string("cannot open"));
            return 0;
        }
        const uint32_t crc = FileCrc(file.string());
        const fs::path root = CacheRoot() / (file.stem().string() + "-" + Hex(crc));
        const fs::path marker = root / ".complete";
        Mounted m;
        m.Name = file.stem().string();
        m.File = file.string();
        m.Root = root;
        if (!fs::exists(marker, ec))
        {
            // İlk açılış: dosyalar önbelleğe çözülür ve doğrulanır
            fs::remove_all(root, ec);
            fs::create_directories(root, ec);
            for (const auto& e : entries)
            {
                std::string packed(e.CompressedSize, '\0');
                if (!in.read(packed.data(), (std::streamsize)packed.size()))
                {
                    if (error) *error = "truncated bundle";
                    return 0;
                }
                std::vector<uint8_t> data;
                if (!Compression::Decompress(packed.data(), packed.size(), data, CompressionAlgorithm::Zstd) || data.size() != e.Size ||
                    Crypto::CRC32Compute(std::string(data.begin(), data.end())) != e.Crc)
                {
                    if (error) *error = "corrupt entry " + e.Path;
                    CQ_CORE_ERROR("[AssetBundle] {0}: corrupt entry {1}", file.string(), e.Path);
                    return 0;
                }
                const fs::path out = root / fs::path(e.Path).lexically_normal();
                fs::create_directories(out.parent_path(), ec);
                std::ofstream(out, std::ios::binary | std::ios::trunc).write(reinterpret_cast<const char*>(data.data()), (std::streamsize)data.size());
            }
            std::ofstream(marker) << m.File << "\n";
        }
        for (const auto& e : entries)
            m.Assets.push_back(fs::path(e.Path).generic_string());
        std::lock_guard lock(Mutex());
        const int handle = NextHandle()++;
        CQ_CORE_INFO("[AssetBundle] Loaded {0} ({1} files)", m.Name, m.Assets.size());
        Bundles()[handle] = std::move(m);
        return handle;
    }

    bool Unload(int handle)
    {
        std::lock_guard lock(Mutex());
        return Bundles().erase(handle) > 0;
    }

    void UnloadAll()
    {
        std::lock_guard lock(Mutex());
        Bundles().clear();
    }

    bool Contains(int handle, const std::string& assetPath)
    {
        const std::string key = fs::path(assetPath).lexically_normal().generic_string();
        std::lock_guard lock(Mutex());
        auto it = Bundles().find(handle);
        if (it == Bundles().end())
            return false;
        return std::find(it->second.Assets.begin(), it->second.Assets.end(), key) != it->second.Assets.end();
    }

    std::vector<std::string> GetAssetNames(int handle)
    {
        std::lock_guard lock(Mutex());
        auto it = Bundles().find(handle);
        return it == Bundles().end() ? std::vector<std::string>{} : it->second.Assets;
    }

    std::vector<std::string> GetLoadedBundles()
    {
        std::lock_guard lock(Mutex());
        std::vector<std::string> names;
        for (const auto& [handle, b] : Bundles())
            names.push_back(b.Name);
        return names;
    }

    std::string ResolveMounted(const std::string& relativePath)
    {
        if (relativePath.empty())
            return {};
        const fs::path rel = fs::path(relativePath).lexically_normal();
        if (rel.is_absolute())
            return {};
        std::lock_guard lock(Mutex());
        if (Bundles().empty())
            return {};
        std::error_code ec;
        for (const auto& [handle, b] : Bundles())
        {
            const fs::path p = b.Root / rel;
            if (fs::exists(p, ec))
                return p.string();
        }
        return {};
    }
}
