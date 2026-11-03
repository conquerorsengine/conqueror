#include "Archive.h"

#include <zlib.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>

namespace Conqueror::Archive
{
    namespace
    {
        // ustar başlığı (512 bayt)
        struct Header
        {
            char Name[100];
            char Mode[8];
            char Uid[8];
            char Gid[8];
            char Size[12];
            char Mtime[12];
            char Checksum[8];
            char Type;
            char Linkname[100];
            char Magic[6];
            char Version[2];
            char Uname[32];
            char Gname[32];
            char Devmajor[8];
            char Devminor[8];
            char Prefix[155];
            char Pad[12];
        };
        static_assert(sizeof(Header) == 512, "tar header must be 512 bytes");

        void Octal(char* dst, size_t width, uint64_t value)
        {
            std::snprintf(dst, width, "%0*llo", (int)width - 1, (unsigned long long)value);
        }

        uint64_t ParseOctal(const char* s, size_t width)
        {
            uint64_t v = 0;
            for (size_t i = 0; i < width && s[i]; ++i)
            {
                if (s[i] == ' ')
                    continue;
                if (s[i] < '0' || s[i] > '7')
                    break;
                v = v * 8 + (uint64_t)(s[i] - '0');
            }
            return v;
        }

        // Uzun yollar: ustar önek (155) + ad (100); daha uzunu için GNU "././@LongLink"
        bool SplitPath(const std::string& path, std::string& prefix, std::string& name)
        {
            if (path.size() <= 100)
            {
                prefix.clear();
                name = path;
                return true;
            }
            for (size_t i = path.size() - 1; i > 0; --i)
                if (path[i] == '/' && path.size() - i - 1 <= 100 && i <= 155)
                {
                    prefix = path.substr(0, i);
                    name = path.substr(i + 1);
                    return true;
                }
            return false;
        }

        void FillHeader(Header& h, const std::string& name, const std::string& prefix, uint64_t size, char type)
        {
            std::memset(&h, 0, sizeof(h));
            std::memcpy(h.Name, name.data(), std::min<size_t>(name.size(), sizeof(h.Name)));
            std::memcpy(h.Prefix, prefix.data(), std::min<size_t>(prefix.size(), sizeof(h.Prefix)));
            Octal(h.Mode, sizeof(h.Mode), 0644);
            Octal(h.Uid, sizeof(h.Uid), 0);
            Octal(h.Gid, sizeof(h.Gid), 0);
            Octal(h.Size, sizeof(h.Size), size);
            Octal(h.Mtime, sizeof(h.Mtime), (uint64_t)std::time(nullptr));
            h.Type = type;
            std::memcpy(h.Magic, "ustar", 6);
            std::memcpy(h.Version, "00", 2);
            std::memset(h.Checksum, ' ', sizeof(h.Checksum));
            unsigned sum = 0;
            const auto* bytes = reinterpret_cast<const unsigned char*>(&h);
            for (size_t i = 0; i < sizeof(h); ++i)
                sum += bytes[i];
            std::snprintf(h.Checksum, sizeof(h.Checksum), "%06o", sum);
            h.Checksum[7] = ' ';
        }

        bool WriteAll(gzFile f, const void* data, size_t size)
        {
            const auto* p = static_cast<const char*>(data);
            while (size > 0)
            {
                const unsigned chunk = (unsigned)std::min<size_t>(size, 1u << 30);
                if (gzwrite(f, p, chunk) != (int)chunk)
                    return false;
                p += chunk;
                size -= chunk;
            }
            return true;
        }

        bool ReadAll(gzFile f, void* data, size_t size)
        {
            auto* p = static_cast<char*>(data);
            while (size > 0)
            {
                const unsigned chunk = (unsigned)std::min<size_t>(size, 1u << 30);
                if (gzread(f, p, chunk) != (int)chunk)
                    return false;
                p += chunk;
                size -= chunk;
            }
            return true;
        }
    }

    bool WriteTarGz(const std::string& file, const std::vector<Entry>& entries, std::string* error)
    {
        gzFile f = gzopen(file.c_str(), "wb9");
        if (!f)
        {
            if (error) *error = "cannot write " + file;
            return false;
        }
        static const char zeros[512] = {};
        bool ok = true;
        for (const auto& e : entries)
        {
            std::string prefix, name;
            Header h;
            if (!SplitPath(e.Path, prefix, name))
            {
                // GNU uzun ad girdisi
                const std::string longName = e.Path + '\0';
                FillHeader(h, "././@LongLink", "", longName.size(), 'L');
                ok = ok && WriteAll(f, &h, sizeof(h)) && WriteAll(f, longName.data(), longName.size());
                if (const size_t pad = (512 - longName.size() % 512) % 512)
                    ok = ok && WriteAll(f, zeros, pad);
                name = e.Path.substr(0, 100);
                prefix.clear();
            }
            FillHeader(h, name, prefix, e.Data.size(), '0');
            ok = ok && WriteAll(f, &h, sizeof(h));
            if (!e.Data.empty())
                ok = ok && WriteAll(f, e.Data.data(), e.Data.size());
            if (const size_t pad = (512 - e.Data.size() % 512) % 512)
                ok = ok && WriteAll(f, zeros, pad);
        }
        ok = ok && WriteAll(f, zeros, 512) && WriteAll(f, zeros, 512);
        if (gzclose(f) != Z_OK)
            ok = false;
        if (!ok && error)
            *error = "write error in " + file;
        return ok;
    }

    bool ReadTarGz(const std::string& file, std::vector<Entry>& out, std::string* error)
    {
        out.clear();
        gzFile f = gzopen(file.c_str(), "rb");
        if (!f)
        {
            if (error) *error = "cannot open " + file;
            return false;
        }
        std::string longName;
        bool ok = true;
        for (;;)
        {
            Header h;
            if (gzread(f, &h, sizeof(h)) != (int)sizeof(h))
                break;
            if (h.Name[0] == '\0')
                break;   // arşiv sonu
            const uint64_t size = ParseOctal(h.Size, sizeof(h.Size));
            if (size > (1ull << 32))
            {
                ok = false;
                if (error) *error = "entry too large in " + file;
                break;
            }
            std::vector<uint8_t> data((size_t)size);
            if (size && !ReadAll(f, data.data(), (size_t)size))
            {
                ok = false;
                if (error) *error = "truncated archive " + file;
                break;
            }
            if (const size_t pad = (size_t)((512 - size % 512) % 512))
            {
                char skip[512];
                if (gzread(f, skip, (unsigned)pad) != (int)pad)
                {
                    ok = false;
                    break;
                }
            }
            if (h.Type == 'L')
            {
                longName.assign(data.begin(), data.end());
                while (!longName.empty() && longName.back() == '\0')
                    longName.pop_back();
                continue;
            }
            if (h.Type != '0' && h.Type != '\0')
            {
                longName.clear();
                continue;   // klasör ve diğer türler: dosyalar yolu zaten taşır
            }
            Entry e;
            if (!longName.empty())
                e.Path = longName;
            else
            {
                const std::string name(h.Name, strnlen(h.Name, sizeof(h.Name)));
                const std::string prefix(h.Prefix, strnlen(h.Prefix, sizeof(h.Prefix)));
                e.Path = prefix.empty() ? name : prefix + "/" + name;
            }
            longName.clear();
            e.Data = std::move(data);
            out.push_back(std::move(e));
        }
        gzclose(f);
        return ok;
    }
}
