#include "PlayerPrefs.h"

#include "Core/Project/Project.h"
#include "Core/Utils/Platform.h"
#include "Core/Logging/Log.h"

#include <yaml-cpp/yaml.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>

namespace Conqueror
{
    namespace
    {
        struct PrefEntry
        {
            enum class Kind { Int, Float, String } Type = Kind::Int;
            int64_t I = 0;
            double F = 0.0;
            std::string S;
        };

        struct PrefsState
        {
            std::mutex Mutex;
            std::map<std::string, PrefEntry> Entries;
            std::filesystem::path LoadedFile;   // boş: henüz yüklenmedi
            bool Dirty = false;
        };

        PrefsState& State()
        {
            static PrefsState* s = new PrefsState();   // kapanışta yok edilmez (statik sıra sorunları yok)
            return *s;
        }

        // Dosya adında geçersiz karakterleri temizle (Windows kuralları her yerde)
        std::string SanitizeName(const std::string& name, const char* fallback)
        {
            std::string out;
            for (unsigned char c : name)
                out += (c < 32 || std::string_view("<>:\"/\\|?*").find((char)c) != std::string_view::npos) ? '_' : (char)c;
            while (!out.empty() && (out.back() == '.' || out.back() == ' '))
                out.pop_back();
            while (!out.empty() && out.front() == ' ')
                out.erase(out.begin());
            return out.empty() ? std::string(fallback) : out;
        }

        std::filesystem::path UserDataRoot()
        {
#if defined(CQ_PLATFORM_LINUX)
            if (const char* xdg = std::getenv("XDG_DATA_HOME"); xdg && *xdg == '/')
                return std::filesystem::path(xdg);
#endif
            std::string dir = Platform::GetUserAppDataDirectory();
            if (dir.empty())
                dir = Platform::GetTempDirectory();
            return std::filesystem::path(dir);
        }

        std::filesystem::path PersistentDir()
        {
            std::string organization = "Conqueror", product = "Default";
            if (auto project = Project::GetActive())
            {
                organization = SanitizeName(project->GetConfig().Organization, "Conqueror");
                product = SanitizeName(project->GetConfig().Name, "Default");
            }
            return UserDataRoot() / organization / product;
        }

        bool WriteLocked(PrefsState& s)
        {
            if (!s.Dirty || s.LoadedFile.empty())
                return true;
            YAML::Emitter out;
            out.SetDoublePrecision(17);
            out << YAML::BeginMap;
            const PrefEntry::Kind kinds[3] = { PrefEntry::Kind::Int, PrefEntry::Kind::Float, PrefEntry::Kind::String };
            const char* sections[3] = { "Ints", "Floats", "Strings" };
            for (int k = 0; k < 3; ++k)
            {
                out << YAML::Key << sections[k] << YAML::Value << YAML::BeginMap;
                for (const auto& [key, e] : s.Entries)
                {
                    if (e.Type != kinds[k])
                        continue;
                    out << YAML::Key << key << YAML::Value;
                    if (e.Type == PrefEntry::Kind::Int) out << e.I;
                    else if (e.Type == PrefEntry::Kind::Float) out << e.F;
                    else out << YAML::DoubleQuoted << e.S;
                }
                out << YAML::EndMap;
            }
            out << YAML::EndMap;

            std::error_code ec;
            std::filesystem::create_directories(s.LoadedFile.parent_path(), ec);
            std::filesystem::path tmp = s.LoadedFile;
            tmp += ".tmp";
            {
                std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
                if (!f)
                {
                    CQ_CORE_ERROR("[PlayerPrefs] Cannot write {}", tmp.string());
                    return false;
                }
                f << out.c_str() << "\n";
                if (!f.good())
                    return false;
            }
            std::filesystem::rename(tmp, s.LoadedFile, ec);
            if (ec)
            {
                // Bazı platformlarda hedef varken yeniden adlandırma başarısız olabilir
                std::filesystem::remove(s.LoadedFile, ec);
                std::filesystem::rename(tmp, s.LoadedFile, ec);
                if (ec)
                {
                    CQ_CORE_ERROR("[PlayerPrefs] Cannot replace {}: {}", s.LoadedFile.string(), ec.message());
                    return false;
                }
            }
            s.Dirty = false;
            return true;
        }

        // Etkin projenin dosyası yüklü değilse (ya da proje değiştiyse) yükle
        void EnsureLoadedLocked(PrefsState& s)
        {
            std::filesystem::path file = PersistentDir() / "prefs.yaml";
            if (file == s.LoadedFile)
                return;
            WriteLocked(s);   // önceki projenin bekleyen değişiklikleri
            s.Entries.clear();
            s.Dirty = false;
            s.LoadedFile = file;

            std::error_code ec;
            if (!std::filesystem::exists(file, ec))
                return;
            try
            {
                YAML::Node root = YAML::LoadFile(file.string());
                auto readSection = [&](const char* name, PrefEntry::Kind kind) {
                    YAML::Node section = root[name];
                    if (!section || !section.IsMap())
                        return;
                    for (const auto& it : section)
                    {
                        PrefEntry e;
                        e.Type = kind;
                        if (kind == PrefEntry::Kind::Int) e.I = it.second.as<int64_t>(0);
                        else if (kind == PrefEntry::Kind::Float) e.F = it.second.as<double>(0.0);
                        else e.S = it.second.as<std::string>("");
                        s.Entries[it.first.as<std::string>()] = std::move(e);
                    }
                };
                readSection("Ints", PrefEntry::Kind::Int);
                readSection("Floats", PrefEntry::Kind::Float);
                readSection("Strings", PrefEntry::Kind::String);
            }
            catch (const std::exception& ex)
            {
                CQ_CORE_WARN("[PlayerPrefs] {} could not be read ({}); starting empty", file.string(), ex.what());
                s.Entries.clear();
            }
        }

        template<typename Fn>
        auto WithState(Fn&& fn)
        {
            PrefsState& s = State();
            std::lock_guard<std::mutex> lock(s.Mutex);
            EnsureLoadedLocked(s);
            return fn(s);
        }
    }

    void PlayerPrefs::SetInt(const std::string& key, int64_t value)
    {
        WithState([&](PrefsState& s) {
            PrefEntry& e = s.Entries[key];
            e.Type = PrefEntry::Kind::Int;
            e.I = value;
            e.S.clear();
            s.Dirty = true;
            return 0;
        });
    }

    void PlayerPrefs::SetFloat(const std::string& key, double value)
    {
        WithState([&](PrefsState& s) {
            PrefEntry& e = s.Entries[key];
            e.Type = PrefEntry::Kind::Float;
            e.F = value;
            e.S.clear();
            s.Dirty = true;
            return 0;
        });
    }

    void PlayerPrefs::SetString(const std::string& key, const std::string& value)
    {
        WithState([&](PrefsState& s) {
            PrefEntry& e = s.Entries[key];
            e.Type = PrefEntry::Kind::String;
            e.S = value;
            s.Dirty = true;
            return 0;
        });
    }

    int64_t PlayerPrefs::GetInt(const std::string& key, int64_t defaultValue)
    {
        return WithState([&](PrefsState& s) -> int64_t {
            auto it = s.Entries.find(key);
            if (it == s.Entries.end()) return defaultValue;
            switch (it->second.Type)
            {
                case PrefEntry::Kind::Int: return it->second.I;
                case PrefEntry::Kind::Float: return (int64_t)it->second.F;
                default: return defaultValue;
            }
        });
    }

    double PlayerPrefs::GetFloat(const std::string& key, double defaultValue)
    {
        return WithState([&](PrefsState& s) -> double {
            auto it = s.Entries.find(key);
            if (it == s.Entries.end()) return defaultValue;
            switch (it->second.Type)
            {
                case PrefEntry::Kind::Float: return it->second.F;
                case PrefEntry::Kind::Int: return (double)it->second.I;
                default: return defaultValue;
            }
        });
    }

    std::string PlayerPrefs::GetString(const std::string& key, const std::string& defaultValue)
    {
        return WithState([&](PrefsState& s) -> std::string {
            auto it = s.Entries.find(key);
            if (it == s.Entries.end()) return defaultValue;
            switch (it->second.Type)
            {
                case PrefEntry::Kind::String: return it->second.S;
                case PrefEntry::Kind::Int: return std::to_string(it->second.I);
                default: return std::to_string(it->second.F);
            }
        });
    }

    bool PlayerPrefs::HasKey(const std::string& key)
    {
        return WithState([&](PrefsState& s) { return s.Entries.count(key) != 0; });
    }

    void PlayerPrefs::DeleteKey(const std::string& key)
    {
        WithState([&](PrefsState& s) {
            if (s.Entries.erase(key))
                s.Dirty = true;
            return 0;
        });
    }

    void PlayerPrefs::DeleteAll()
    {
        WithState([&](PrefsState& s) {
            if (!s.Entries.empty())
                s.Dirty = true;
            s.Entries.clear();
            return 0;
        });
    }

    bool PlayerPrefs::Save()
    {
        PrefsState& s = State();
        std::lock_guard<std::mutex> lock(s.Mutex);
        // Hiç kullanılmadıysa dosya oluşturma
        if (s.LoadedFile.empty())
            return true;
        return WriteLocked(s);
    }

    std::string PlayerPrefs::GetPersistentDataPath()
    {
        std::filesystem::path dir = PersistentDir();
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);
        return dir.generic_string();
    }
}
