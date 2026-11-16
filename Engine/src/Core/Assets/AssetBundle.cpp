#include "AssetBundle.h"

#include "Core/Logging/Log.h"
#include "Core/Project/Project.h"
#include "Core/Utils/Compression.h"
#include "Core/Utils/Platform.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <unordered_map>

#include <zlib.h>

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

        // CRC32 (IEEE, Crypto::CRC32Compute ile aynı değer): zlib'in tablolu sürümü, büyük oyun paketlerinde bit bit hesaptan çok hızlı
        uint32_t Crc(const void* data, size_t size)
        {
            uLong crc = crc32(0L, Z_NULL, 0);
            const auto* p = static_cast<const Bytef*>(data);
            while (size > 0)
            {
                const uInt chunk = (uInt)std::min<size_t>(size, 1u << 30);
                crc = crc32(crc, p, chunk);
                p += chunk;
                size -= chunk;
            }
            return (uint32_t)crc;
        }

        // Güvenlik: arşivdeki yol çıkarıldığı klasörün dışına çıkamaz (mutlak yol, sürücü adı, "..")
        bool SafeRelativePath(const std::string& path)
        {
            const fs::path p = fs::path(path).lexically_normal();
            if (path.empty() || p.empty() || p.is_absolute() || p.has_root_name() || p.has_root_directory())
                return false;
            for (const auto& part : p)
                if (part == "..")
                    return false;
            return true;
        }

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
                if (!SafeRelativePath(e.Path))
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
        return ReadFile(path, data) ? Crc(data.data(), data.size()) : 0u;
    }

    bool Write(const std::string& bundleFile, const std::vector<std::pair<std::string, std::string>>& files, std::string* error)
    {
        std::vector<WriteEntry> entries;
        entries.reserve(files.size());
        for (const auto& [path, source] : files)
            entries.push_back({ path, source, {} });
        return WriteEntries(bundleFile, entries, error);
    }

    bool WriteEntries(const std::string& bundleFile, const std::vector<WriteEntry>& entries, std::string* error, const Progress& progress,
                      std::vector<FileEntry>* written)
    {
        // Tablo kayıtları sabit boyutludur (yol uzunluğu belli): önce yer tutucu tablo, veri blokları, sonra gerçek tablo aynı yere
        std::vector<FileEntry> table(entries.size());
        for (size_t i = 0; i < entries.size(); ++i)
        {
            table[i].Path = fs::path(entries[i].Path).generic_string();
            if (!SafeRelativePath(table[i].Path) || table[i].Path.size() > 4096)
            {
                if (error) *error = "invalid path in the archive: " + entries[i].Path;
                return false;
            }
        }
        std::error_code ec;
        if (fs::path(bundleFile).has_parent_path())
            fs::create_directories(fs::path(bundleFile).parent_path(), ec);
        std::ofstream out(bundleFile, std::ios::binary | std::ios::trunc);
        if (!out)
        {
            if (error) *error = "cannot write " + bundleFile;
            return false;
        }
        auto writeTable = [&]() {
            std::string t;
            t.append(kMagic, 4);
            Put(t, kVersion);
            Put(t, (uint32_t)table.size());
            for (const auto& e : table)
            {
                Put(t, (uint32_t)e.Path.size());
                t += e.Path;
                Put(t, e.Size);
                Put(t, e.CompressedSize);
                Put(t, e.Crc);
            }
            out.write(t.data(), (std::streamsize)t.size());
        };
        writeTable();
        for (size_t i = 0; i < entries.size(); ++i)
        {
            if (progress && !progress(i, entries.size()))
            {
                if (error) *error = "Cancelled";
                return false;
            }
            std::string loaded;
            if (entries[i].Generate)
                loaded = entries[i].Generate(std::vector<FileEntry>(table.begin(), table.begin() + (std::ptrdiff_t)i));
            else if (!entries[i].Source.empty() && !ReadFile(entries[i].Source, loaded))
            {
                if (error) *error = "cannot read " + entries[i].Source;
                return false;
            }
            const std::string& data = entries[i].Source.empty() && !entries[i].Generate ? entries[i].Data : loaded;
            // Boş dosya: veri bloğu yok (zstd boş girdiyi sıkıştırmaz)
            std::vector<uint8_t> packed;
            if (!data.empty() && !Compression::Compress(data.data(), data.size(), packed, CompressionAlgorithm::Zstd, CompressionLevel::Default))
            {
                if (error) *error = "cannot compress " + (entries[i].Source.empty() ? entries[i].Path : entries[i].Source);
                return false;
            }
            table[i].Size = data.size();
            table[i].CompressedSize = packed.size();
            table[i].Crc = Crc(data.data(), data.size());
            out.write(reinterpret_cast<const char*>(packed.data()), (std::streamsize)packed.size());
            if (!out)
            {
                if (error) *error = "write error in " + bundleFile + " (disk full?)";
                return false;
            }
        }
        out.seekp(0);
        writeTable();
        out.flush();
        if (!out)
        {
            if (error) *error = "write error in " + bundleFile;
            return false;
        }
        out.close();
        if (progress)
            progress(entries.size(), entries.size());
        if (written)
            *written = std::move(table);
        return true;
    }

    bool ReadEntry(const std::string& bundleFile, const std::string& path, std::string& data, std::string* error)
    {
        std::ifstream in(bundleFile, std::ios::binary);
        std::vector<FileEntry> entries;
        if (!in)
        {
            if (error) *error = "cannot open " + bundleFile;
            return false;
        }
        if (!ReadHeader(in, entries, error))
            return false;
        // Veri blokları tablo sırasıyla art arda: aranan girdinin konumu öncekilerin sıkıştırılmış boyutlarının toplamı
        const std::string key = fs::path(path).lexically_normal().generic_string();
        uint64_t offset = (uint64_t)in.tellg();
        for (const auto& e : entries)
        {
            if (fs::path(e.Path).lexically_normal().generic_string() != key)
            {
                offset += e.CompressedSize;
                continue;
            }
            if (e.CompressedSize > (1ull << 32) || e.Size > (1ull << 32))
            {
                if (error) *error = "entry too large: " + e.Path;
                return false;
            }
            std::string packed((size_t)e.CompressedSize, '\0');
            in.seekg((std::streamoff)offset);
            std::vector<uint8_t> out;
            if (!in.read(packed.data(), (std::streamsize)packed.size()) ||
                (!packed.empty() && !Compression::Decompress(packed.data(), packed.size(), out, CompressionAlgorithm::Zstd)) || out.size() != e.Size ||
                Crc(out.data(), out.size()) != e.Crc)
            {
                if (error) *error = "corrupt entry " + e.Path;
                return false;
            }
            data.assign(reinterpret_cast<const char*>(out.data()), out.size());
            return true;
        }
        if (error) *error = path + " is not in " + fs::path(bundleFile).filename().string();
        return false;
    }

    bool Extract(const std::string& bundleFile, const std::string& directory, std::string* error, const Progress& progress)
    {
        std::ifstream in(bundleFile, std::ios::binary);
        std::vector<FileEntry> entries;
        if (!in)
        {
            if (error) *error = "cannot open " + bundleFile;
            return false;
        }
        if (!ReadHeader(in, entries, error))
            return false;
        std::error_code ec;
        const fs::path root(directory);
        fs::create_directories(root, ec);
        for (size_t i = 0; i < entries.size(); ++i)
        {
            const auto& e = entries[i];
            if (progress && !progress(i, entries.size()))
            {
                if (error) *error = "Cancelled";
                return false;
            }
            if (e.CompressedSize > (1ull << 32) || e.Size > (1ull << 32))
            {
                if (error) *error = "entry too large: " + e.Path;
                return false;
            }
            std::string packed((size_t)e.CompressedSize, '\0');
            if (!in.read(packed.data(), (std::streamsize)packed.size()))
            {
                if (error) *error = "truncated archive";
                return false;
            }
            std::vector<uint8_t> data;
            if ((!packed.empty() && !Compression::Decompress(packed.data(), packed.size(), data, CompressionAlgorithm::Zstd)) || data.size() != e.Size ||
                Crc(data.data(), data.size()) != e.Crc)
            {
                if (error) *error = "corrupt entry " + e.Path;
                return false;
            }
            const fs::path out = root / fs::path(e.Path).lexically_normal();
            fs::create_directories(out.parent_path(), ec);
            std::ofstream file(out, std::ios::binary | std::ios::trunc);
            file.write(reinterpret_cast<const char*>(data.data()), (std::streamsize)data.size());
            if (!file)
            {
                if (error) *error = "cannot write " + out.string() + " (disk full?)";
                return false;
            }
        }
        if (progress)
            progress(entries.size(), entries.size());
        return true;
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
            in.close();
            fs::remove_all(root, ec);
            std::string extractError;
            if (!Extract(file.string(), root.string(), &extractError))
            {
                if (error) *error = extractError;
                CQ_CORE_ERROR("[AssetBundle] {0}: {1}", file.string(), extractError);
                fs::remove_all(root, ec);
                return 0;
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
