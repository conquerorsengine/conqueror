#include "AssetBundleBuilder.h"

#include "Core/Assets/AssetBundle.h"
#include "Core/Logging/Log.h"
#include "Core/Project/Project.h"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <map>
#include <set>

namespace Conqueror::Editor::AssetBundleBuilder
{
    namespace fs = std::filesystem;

    namespace
    {
        fs::path ProjectDir() { return Project::GetActive() ? Project::GetActiveProjectDirectory() : fs::path(); }
        fs::path AssignmentFile() { return ProjectDir() / "AssetBundles.yaml"; }

        std::string Relative(const std::string& path)
        {
            const fs::path project = ProjectDir();
            if (project.empty() || path.empty())
                return {};
            fs::path p(path);
            if (p.is_relative())
                p = project / p;
            std::error_code ec;
            const fs::path rel = fs::relative(p.lexically_normal(), project, ec);
            if (ec || rel.empty() || rel.generic_string().rfind("..", 0) == 0)
                return {};
            return rel.generic_string();
        }

        // Paket adı: küçük harf, harf / rakam / - _ . / (Unity adları küçük harfe çevirir)
        std::string CleanName(const std::string& name)
        {
            std::string out;
            for (char c : name)
            {
                const unsigned char u = (unsigned char)c;
                if (std::isalnum(u) || c == '-' || c == '_' || c == '.' || c == '/')
                    out += (char)std::tolower(u);
                else if (c == ' ')
                    out += '_';
            }
            return out;
        }

        std::map<std::string, std::string> Load()
        {
            std::map<std::string, std::string> map;
            std::error_code ec;
            if (!fs::exists(AssignmentFile(), ec))
                return map;
            try
            {
                YAML::Node root = YAML::LoadFile(AssignmentFile().string());
                for (const auto& kv : root["AssetBundles"])
                    map[kv.first.as<std::string>()] = kv.second.as<std::string>();
            }
            catch (...) {}
            return map;
        }

        void Save(const std::map<std::string, std::string>& map)
        {
            YAML::Emitter out;
            out << YAML::BeginMap << YAML::Key << "AssetBundles" << YAML::Value << YAML::BeginMap;
            for (const auto& [path, bundle] : map)
                out << YAML::Key << path << YAML::Value << bundle;
            out << YAML::EndMap << YAML::EndMap;
            std::ofstream(AssignmentFile(), std::ios::trunc) << out.c_str() << "\n";
        }

        bool IsSidecar(const fs::path& p)
        {
            const std::string ext = p.extension().string();
            return ext == ".meta" || ext == ".cqsprite";
        }
    }

    std::string GetAssignedName(const std::string& assetPath)
    {
        const std::string rel = Relative(assetPath);
        const auto map = Load();
        auto it = map.find(rel);
        return it == map.end() ? std::string() : it->second;
    }

    std::string GetBundleName(const std::string& assetPath)
    {
        const auto map = Load();
        for (fs::path p = Relative(assetPath); !p.empty(); p = p.parent_path())
        {
            auto it = map.find(p.generic_string());
            if (it != map.end())
                return it->second;
            if (p == p.parent_path())
                break;
        }
        return {};
    }

    void SetBundleName(const std::string& assetPath, const std::string& bundle)
    {
        const std::string rel = Relative(assetPath);
        if (rel.empty())
            return;
        auto map = Load();
        const std::string name = CleanName(bundle);
        if (name.empty())
            map.erase(rel);
        else
            map[rel] = name;
        Save(map);
    }

    std::vector<std::string> GetAllBundleNames()
    {
        std::set<std::string> names;
        for (const auto& [path, bundle] : Load())
            names.insert(bundle);
        return { names.begin(), names.end() };
    }

    std::vector<std::string> GetAssetsInBundle(const std::string& bundle)
    {
        std::set<std::string> files;
        const fs::path project = ProjectDir();
        std::error_code ec;
        const auto map = Load();
        for (const auto& [path, name] : map)
        {
            if (name != bundle)
                continue;
            const fs::path full = project / path;
            if (fs::is_directory(full, ec))
            {
                for (fs::recursive_directory_iterator it(full, ec), end; !ec && it != end; it.increment(ec))
                    if (it->is_regular_file(ec) && !IsSidecar(it->path()))
                    {
                        const std::string rel = Relative(it->path().string());
                        // İç içe atama: alt klasör / dosya başka pakete verildiyse onundur
                        if (GetBundleName(rel) == bundle)
                            files.insert(rel);
                    }
            }
            else if (fs::is_regular_file(full, ec))
                files.insert(path);
        }
        return { files.begin(), files.end() };
    }

    bool IsInAnyBundle(const std::string& relativePath)
    {
        return !GetBundleName(relativePath).empty();
    }

    Result Build(const std::string& platform, const DependencyFn& dependencies)
    {
        Result result;
        const fs::path project = ProjectDir();
        if (project.empty())
        {
            result.Error = "No project is open.";
            return result;
        }
        const std::vector<std::string> names = GetAllBundleNames();
        if (names.empty())
        {
            result.Error = "No asset has an AssetBundle name (Inspector > AssetBundle).";
            return result;
        }
        // Açık atamaların sahibi
        std::map<std::string, std::string> owner;
        std::map<std::string, std::vector<std::string>> explicitFiles;
        for (const auto& name : names)
            for (const auto& f : GetAssetsInBundle(name))
            {
                owner[f] = name;
                explicitFiles[name].push_back(f);
            }
        const fs::path outDir = project / "AssetBundles" / platform;
        std::error_code ec;
        fs::create_directories(outDir, ec);
        YAML::Emitter manifest;
        manifest << YAML::BeginMap << YAML::Key << "AssetBundleManifest" << YAML::Value << YAML::BeginMap;
        manifest << YAML::Key << "Platform" << YAML::Value << platform;
        manifest << YAML::Key << "Bundles" << YAML::Value << YAML::BeginSeq;
        std::set<std::string> written;
        for (const auto& name : names)
        {
            std::set<std::string> files(explicitFiles[name].begin(), explicitFiles[name].end());
            std::set<std::string> bundleDeps;
            // Bağımlılıklar: başka pakettekiler paket bağımlılığı, sahipsizler bu pakete (örtük)
            if (dependencies)
                for (const auto& f : explicitFiles[name])
                    for (const auto& dep : dependencies(project / f))
                    {
                        const std::string rel = Relative(dep.string());
                        if (rel.empty() || rel.rfind("Assets/", 0) != 0 || IsSidecar(dep))
                            continue;
                        auto it = owner.find(rel);
                        if (it != owner.end() && it->second != name)
                            bundleDeps.insert(it->second);
                        else if (it == owner.end())
                            files.insert(rel);
                    }
            std::vector<std::pair<std::string, std::string>> entries;
            for (const auto& f : files)
            {
                entries.push_back({ f, (project / f).string() });
                // İçe aktarma ayarları ve dilimler dosyanın yanında
                for (const char* sidecar : { ".meta", ".cqsprite" })
                    if (fs::exists(project / (f + sidecar), ec))
                        entries.push_back({ f + sidecar, (project / (f + sidecar)).string() });
            }
            const fs::path file = outDir / (name + AssetBundles::Extension);
            fs::create_directories(file.parent_path(), ec);
            std::string error;
            if (!AssetBundles::Write(file.string(), entries, &error))
            {
                result.Error = name + ": " + error;
                return result;
            }
            written.insert(fs::path(file).lexically_normal().string());
            manifest << YAML::BeginMap;
            manifest << YAML::Key << "Name" << YAML::Value << name;
            manifest << YAML::Key << "File" << YAML::Value << fs::relative(file, outDir, ec).generic_string();
            char crc[16];
            std::snprintf(crc, sizeof(crc), "%08x", AssetBundles::FileCrc(file.string()));
            manifest << YAML::Key << "CRC" << YAML::Value << crc;
            manifest << YAML::Key << "Assets" << YAML::Value << YAML::BeginSeq;
            for (const auto& f : files) manifest << f;
            manifest << YAML::EndSeq;
            manifest << YAML::Key << "Dependencies" << YAML::Value << YAML::BeginSeq;
            for (const auto& d : bundleDeps) manifest << d;
            manifest << YAML::EndSeq;
            manifest << YAML::EndMap;
            ++result.Bundles;
            result.Files += (int)files.size();
        }
        manifest << YAML::EndSeq << YAML::EndMap << YAML::EndMap;
        std::ofstream(outDir / (platform + ".manifest"), std::ios::trunc) << manifest.c_str() << "\n";
        // Artık atanmayan eski paketler (yalnız bu klasördeki .cqbundle dosyaları)
        for (fs::recursive_directory_iterator it(outDir, ec), end; !ec && it != end; it.increment(ec))
            if (it->is_regular_file(ec) && it->path().extension() == AssetBundles::Extension &&
                !written.count(it->path().lexically_normal().string()))
                fs::remove(it->path(), ec);
        result.Ok = true;
        result.OutputDir = outDir.string();
        CQ_CORE_INFO("[AssetBundles] Built {0} bundle(s), {1} asset(s) -> {2}", result.Bundles, result.Files, outDir.string());
        return result;
    }
}
