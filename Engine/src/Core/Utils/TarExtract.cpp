#include "TarExtract.h"

#include <zlib.h>
#include <zstd.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <memory>
#include <vector>

#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace Conqueror::TarExtract
{
    namespace fs = std::filesystem;

    namespace
    {
        // Sıkıştırılmış dosyadan düz tar akışı
        class Reader
        {
        public:
            virtual ~Reader() = default;
            virtual size_t Read(void* dst, size_t n) = 0;   // 0: son ya da hata (Failed)
            virtual float Progress() = 0;
            bool Failed = false;
            std::string Error;
        };

        // gzip (ya da sıkıştırılmamış tar: zlib düz dosyayı da olduğu gibi okur)
        class GzReader : public Reader
        {
        public:
            explicit GzReader(const fs::path& path)
            {
                std::error_code ec;
                m_Total = (double)std::max<uintmax_t>(1, fs::file_size(path, ec));
#ifdef _WIN32
                m_File = gzopen_w(path.c_str(), "rb");
#else
                m_File = gzopen(path.c_str(), "rb");
#endif
                if (!m_File)
                {
                    Failed = true;
                    Error = "cannot open " + path.string();
                }
                else
                    gzbuffer(m_File, 1 << 18);
            }
            ~GzReader() override
            {
                if (m_File)
                    gzclose(m_File);
            }
            size_t Read(void* dst, size_t n) override
            {
                if (!m_File)
                    return 0;
                const int r = gzread(m_File, dst, (unsigned)n);
                if (r < 0)
                {
                    int code = 0;
                    Failed = true;
                    Error = std::string("gzip: ") + gzerror(m_File, &code);
                    return 0;
                }
                return (size_t)r;
            }
            float Progress() override { return m_File ? (float)std::min(1.0, (double)gzoffset(m_File) / m_Total) : 0.0f; }

        private:
            gzFile m_File = nullptr;
            double m_Total = 1.0;
        };

        class ZstdReader : public Reader
        {
        public:
            explicit ZstdReader(const fs::path& path) : m_In(path, std::ios::binary), m_Stream(ZSTD_createDStream())
            {
                std::error_code ec;
                m_Total = (double)std::max<uintmax_t>(1, fs::file_size(path, ec));
                if (!m_In || !m_Stream)
                {
                    Failed = true;
                    Error = "cannot open " + path.string();
                }
                m_InData.resize(ZSTD_DStreamInSize());
                m_OutData.resize(ZSTD_DStreamOutSize());
            }
            ~ZstdReader() override
            {
                if (m_Stream)
                    ZSTD_freeDStream(m_Stream);
            }
            size_t Read(void* dst, size_t n) override
            {
                size_t got = 0;
                while (got < n && !Failed)
                {
                    if (m_OutPos < m_OutSize)
                    {
                        const size_t k = std::min(n - got, m_OutSize - m_OutPos);
                        std::memcpy((char*)dst + got, m_OutData.data() + m_OutPos, k);
                        m_OutPos += k;
                        got += k;
                        continue;
                    }
                    // Girdi bittiyse ve çözücünün bekleyen çıktısı yoksa dosyadan oku
                    if (m_InBuf.pos == m_InBuf.size && !m_LastOutputFull)
                    {
                        m_In.read(m_InData.data(), (std::streamsize)m_InData.size());
                        const size_t r = (size_t)m_In.gcount();
                        m_Consumed += r;
                        if (r == 0)
                        {
                            if (m_LastRet != 0)
                            {
                                Failed = true;
                                Error = "zstd: the archive is truncated";
                            }
                            break;
                        }
                        m_InBuf = { m_InData.data(), r, 0 };
                    }
                    ZSTD_outBuffer out = { m_OutData.data(), m_OutData.size(), 0 };
                    const size_t ret = ZSTD_decompressStream(m_Stream, &out, &m_InBuf);
                    if (ZSTD_isError(ret))
                    {
                        Failed = true;
                        Error = std::string("zstd: ") + ZSTD_getErrorName(ret);
                        break;
                    }
                    m_LastRet = ret;
                    m_LastOutputFull = out.pos == out.size;
                    m_OutSize = out.pos;
                    m_OutPos = 0;
                }
                return got;
            }
            float Progress() override { return (float)std::min(1.0, (double)m_Consumed / m_Total); }

        private:
            std::ifstream m_In;
            ZSTD_DStream* m_Stream = nullptr;
            std::vector<char> m_InData, m_OutData;
            ZSTD_inBuffer m_InBuf{ nullptr, 0, 0 };
            size_t m_OutPos = 0, m_OutSize = 0, m_LastRet = 0;
            bool m_LastOutputFull = false;
            uint64_t m_Consumed = 0;
            double m_Total = 1.0;
        };

        bool ReadFull(Reader& r, void* dst, size_t n)
        {
            size_t got = 0;
            while (got < n)
            {
                const size_t k = r.Read((char*)dst + got, n - got);
                if (k == 0)
                    return false;
                got += k;
            }
            return true;
        }

        bool Skip(Reader& r, uint64_t n)
        {
            char buf[65536];
            while (n > 0)
            {
                const size_t k = (size_t)std::min<uint64_t>(n, sizeof(buf));
                if (!ReadFull(r, buf, k))
                    return false;
                n -= k;
            }
            return true;
        }

        std::string Field(const char* p, size_t n)
        {
            size_t len = 0;
            while (len < n && p[len])
                ++len;
            return std::string(p, len);
        }

        uint64_t Number(const char* p, size_t n)
        {
            // GNU base-256 (büyük dosyalar) ya da sekizlik
            if ((unsigned char)p[0] & 0x80)
            {
                uint64_t v = (unsigned char)p[0] & 0x7f;
                for (size_t i = 1; i < n; ++i)
                    v = (v << 8) | (unsigned char)p[i];
                return v;
            }
            uint64_t v = 0;
            for (size_t i = 0; i < n && p[i]; ++i)
                if (p[i] >= '0' && p[i] <= '7')
                    v = v * 8 + (uint64_t)(p[i] - '0');
            return v;
        }

        fs::path FromUtf8(const std::string& s) { return fs::path(std::u8string(s.begin(), s.end())); }

        // Hedefin içinde kalan normalleştirilmiş göreli yol; dışarı çıkıyorsa boş
        fs::path SafeRelative(const std::string& name)
        {
            fs::path rel = FromUtf8(name).lexically_normal();
            if (rel.empty() || rel.is_absolute() || rel.has_root_name() || rel.has_root_directory())
                return {};
            if (rel == ".")
                return {};
            for (const auto& part : rel)
                if (part == "..")
                    return {};
            return rel;
        }

        void ParsePax(const std::string& data, std::string& path, std::string& link)
        {
            // "<uzunluk> anahtar=değer\n" kayıtları
            size_t i = 0;
            while (i < data.size())
            {
                const size_t sp = data.find(' ', i);
                if (sp == std::string::npos)
                    break;
                const size_t len = (size_t)std::strtoull(data.c_str() + i, nullptr, 10);
                if (len == 0 || i + len > data.size())
                    break;
                const std::string rec = data.substr(sp + 1, i + len - sp - 2);   // sondaki \n hariç
                const size_t eq = rec.find('=');
                if (eq != std::string::npos)
                {
                    const std::string key = rec.substr(0, eq);
                    if (key == "path")
                        path = rec.substr(eq + 1);
                    else if (key == "linkpath")
                        link = rec.substr(eq + 1);
                }
                i += len;
            }
        }
    }

    bool Extract(const fs::path& archive, const fs::path& destination, std::string& error, const std::function<void(float)>& progress)
    {
        const std::string ext = archive.extension().string();
        std::unique_ptr<Reader> reader;
        if (ext == ".zst" || ext == ".tzst")
            reader = std::make_unique<ZstdReader>(archive);
        else
            reader = std::make_unique<GzReader>(archive);
        if (reader->Failed)
        {
            error = reader->Error;
            return false;
        }
        std::error_code ec;
        fs::create_directories(destination, ec);
        const fs::path root = fs::weakly_canonical(destination, ec);

        char header[512];
        std::string longName, longLink, paxPath, paxLink;
        std::vector<char> buffer(1 << 20);
        float lastReported = -1.0f;
        int zeroBlocks = 0;
        while (true)
        {
            if (!ReadFull(*reader, header, sizeof(header)))
            {
                if (reader->Failed)
                {
                    error = reader->Error;
                    return false;
                }
                break;   // bitiş blokları olmadan biten arşiv de kabul
            }
            if (std::all_of(header, header + 512, [](char c) { return c == 0; }))
            {
                if (++zeroBlocks >= 2)
                    break;
                continue;
            }
            zeroBlocks = 0;
            // Sağlama toplamı (bozuk indirme)
            {
                uint64_t sum = 0;
                for (int i = 0; i < 512; ++i)
                    sum += (i >= 148 && i < 156) ? (unsigned char)' ' : (unsigned char)header[i];
                if (sum != Number(header + 148, 8))
                {
                    error = "the archive is damaged (tar header checksum)";
                    return false;
                }
            }
            const uint64_t size = Number(header + 124, 12);
            const char type = header[156];
            const uint64_t padded = (size + 511) / 512 * 512;
            std::string name = Field(header, 100);
            if (std::memcmp(header + 257, "ustar", 5) == 0)
            {
                const std::string prefix = Field(header + 345, 155);
                if (!prefix.empty())
                    name = prefix + "/" + name;
            }
            std::string link = Field(header + 157, 100);
            const unsigned mode = (unsigned)Number(header + 100, 8);

            if (type == 'L' || type == 'K' || type == 'x' || type == 'g')
            {
                std::string data((size_t)size, '\0');
                if (!ReadFull(*reader, data.data(), (size_t)size) || !Skip(*reader, padded - size))
                {
                    error = reader->Failed ? reader->Error : "the archive is truncated";
                    return false;
                }
                if (type == 'L')
                    longName = data.c_str();
                else if (type == 'K')
                    longLink = data.c_str();
                else if (type == 'x')
                    ParsePax(data, paxPath, paxLink);
                continue;
            }
            if (!paxPath.empty())
                name = paxPath;
            else if (!longName.empty())
                name = longName;
            if (!paxLink.empty())
                link = paxLink;
            else if (!longLink.empty())
                link = longLink;
            longName.clear();
            longLink.clear();
            paxPath.clear();
            paxLink.clear();

            const fs::path rel = SafeRelative(name);
            if (rel.empty())
            {
                if (name == "." || name == "./")
                {
                    if (!Skip(*reader, padded))
                        break;
                    continue;
                }
                error = "unsafe path in archive: " + name;
                return false;
            }
            const fs::path target = root / rel;
            fs::create_directories(target.parent_path(), ec);

            if (type == '5')
            {
                fs::create_directories(target, ec);
                if (!Skip(*reader, padded))
                    break;
                continue;
            }
            if (type == '2' || type == '1')
            {
                // Bağlantı hedefi de klasörün içinde kalmalı
                const fs::path linkRel = type == '1' ? SafeRelative(link) : SafeRelative((rel.parent_path() / FromUtf8(link)).lexically_normal().generic_string());
                if (linkRel.empty())
                {
                    error = "unsafe link in archive: " + name + " -> " + link;
                    return false;
                }
                fs::remove(target, ec);
                if (type == '1')
                    fs::copy_file(root / linkRel, target, fs::copy_options::overwrite_existing, ec);
                else
                {
#ifdef _WIN32
                    // Windows'ta sembolik bağlantı yetki ister: hedefi kopyala
                    if (fs::is_directory(root / linkRel, ec))
                        fs::copy(root / linkRel, target, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
                    else
                        fs::copy_file(root / linkRel, target, fs::copy_options::overwrite_existing, ec);
#else
                    fs::create_symlink(FromUtf8(link), target, ec);
#endif
                }
                if (!Skip(*reader, padded))
                    break;
                continue;
            }
            if (type != '0' && type != '\0' && type != '7')
            {
                if (!Skip(*reader, padded))
                    break;
                continue;
            }
            {
                fs::remove(target, ec);   // eski sürümden kalan sembolik bağlantının içine yazılmasın
                std::ofstream out(target, std::ios::binary | std::ios::trunc);
                if (!out)
                {
                    error = "cannot write " + target.string();
                    return false;
                }
                uint64_t left = size;
                while (left > 0)
                {
                    const size_t k = (size_t)std::min<uint64_t>(left, buffer.size());
                    if (!ReadFull(*reader, buffer.data(), k))
                    {
                        error = reader->Failed ? reader->Error : "the archive is truncated";
                        return false;
                    }
                    out.write(buffer.data(), (std::streamsize)k);
                    left -= k;
                }
                if (!out)
                {
                    error = "cannot write " + target.string() + " (disk full?)";
                    return false;
                }
            }
#ifndef _WIN32
            ::chmod(target.c_str(), (mode_t)(mode & 07777 ? mode & 07777 : 0644));
#else
            (void)mode;
#endif
            if (!Skip(*reader, padded - size))
                break;
            if (progress)
            {
                const float p = reader->Progress();
                if (p - lastReported >= 0.01f)
                {
                    lastReported = p;
                    progress(p);
                }
            }
        }
        if (reader->Failed)
        {
            error = reader->Error;
            return false;
        }
        if (progress)
            progress(1.0f);
        return true;
    }
}
