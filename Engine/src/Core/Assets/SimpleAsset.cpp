#include "SimpleAsset.h"

#include "Core/Project/Project.h"
#include "Core/Serialization/SceneSerializer.h"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>

namespace Conqueror::SimpleAssets
{
    namespace fs = std::filesystem;
    std::vector<Def> BuildDefs();

    namespace
    {
        std::string Lower(std::string s)
        {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
            return s;
        }

        void ParseFloats(const std::string& text, float* out, int count)
        {
            std::stringstream ss(text);
            std::string part;
            for (int i = 0; i < count && std::getline(ss, part, ' '); ++i)
                out[i] = (float)std::atof(part.c_str());
        }
    }

    std::string Data::Str(const std::string& name, const std::string& def) const
    {
        auto it = Values.find(name);
        return it == Values.end() ? def : it->second;
    }

    float Data::Float(const std::string& name, float def) const
    {
        auto it = Values.find(name);
        return it == Values.end() || it->second.empty() ? def : (float)std::atof(it->second.c_str());
    }

    int Data::Int(const std::string& name, int def) const
    {
        auto it = Values.find(name);
        return it == Values.end() || it->second.empty() ? def : std::atoi(it->second.c_str());
    }

    bool Data::Bool(const std::string& name, bool def) const
    {
        auto it = Values.find(name);
        return it == Values.end() || it->second.empty() ? def : (it->second == "true" || it->second == "1");
    }

    glm::vec4 Data::Color(const std::string& name, const glm::vec4& def) const
    {
        auto it = Values.find(name);
        if (it == Values.end() || it->second.empty())
            return def;
        glm::vec4 v = def;
        ParseFloats(it->second, &v.x, 4);
        return v;
    }

    glm::vec3 Data::Vec3(const std::string& name, const glm::vec3& def) const
    {
        auto it = Values.find(name);
        if (it == Values.end() || it->second.empty())
            return def;
        glm::vec3 v = def;
        ParseFloats(it->second, &v.x, 3);
        return v;
    }

    const std::vector<std::vector<std::string>>& Data::Table(const std::string& name) const
    {
        static const std::vector<std::vector<std::string>> empty;
        auto it = Tables.find(name);
        return it == Tables.end() ? empty : it->second;
    }

    const std::vector<Def>& All()
    {
        static const std::vector<Def> defs = BuildDefs();
        return defs;
    }

    const Def* ForExtension(const std::string& extension)
    {
        static const std::unordered_map<std::string, const Def*> byExt = [] {
            std::unordered_map<std::string, const Def*> m;
            for (const auto& d : All())
                m[d.Extension] = &d;
            return m;
        }();
        auto it = byExt.find(Lower(extension));
        return it == byExt.end() ? nullptr : it->second;
    }

    const Def* ForKind(const std::string& kind)
    {
        for (const auto& d : All())
            if (d.Kind == kind)
                return &d;
        return nullptr;
    }

    bool IsKind(const std::string& path, const char* kind)
    {
        const Def* d = ForExtension(fs::path(path).extension().string());
        return d && d->Kind == kind;
    }

    Data Defaults(const Def& def)
    {
        Data data;
        for (const auto& f : def.Fields)
        {
            if (f.Type == FieldType::Table)
                data.Tables[f.Name];
            else
                data.Values[f.Name] = f.Default;
        }
        return data;
    }

    bool Load(const std::string& path, Data& out)
    {
        const Def* def = ForExtension(fs::path(path).extension().string());
        if (!def)
            return false;
        struct Cached { fs::file_time_type Time; Data Value; };
        static std::unordered_map<std::string, Cached> cache;
        const std::string file = SceneSerializer::ResolveAssetPath(path);
        std::error_code ec;
        const auto t = fs::last_write_time(file, ec);
        if (ec)
            return false;
        auto it = cache.find(file);
        if (it == cache.end() || it->second.Time != t)
        {
            Data data = Defaults(*def);
            try
            {
                YAML::Node root = YAML::LoadFile(file);
                YAML::Node values = root["Fields"];
                if (!root["Asset"] || root["Asset"].as<std::string>() != def->Kind)
                    return false;
                for (const auto& f : def->Fields)
                {
                    YAML::Node n = values ? values[f.Name] : YAML::Node();
                    if (!n)
                        continue;
                    if (f.Type != FieldType::Table)
                    {
                        data.Values[f.Name] = n.IsNull() ? std::string() : n.as<std::string>();
                        continue;
                    }
                    auto& table = data.Tables[f.Name];
                    if (n.IsSequence())
                        for (const auto& row : n)
                        {
                            std::vector<std::string> cells;
                            if (row.IsSequence())
                                for (const auto& c : row)
                                    cells.push_back(c.IsNull() ? std::string() : c.as<std::string>());
                            else if (row.IsScalar())
                                cells.push_back(row.as<std::string>());
                            cells.resize(std::max<size_t>(f.Columns.size(), 1));
                            for (size_t i = 0; i < f.Columns.size(); ++i)
                                if (cells[i].empty() && !f.Columns[i].Default.empty() && !row.IsScalar() && i >= row.size())
                                    cells[i] = f.Columns[i].Default;
                            table.push_back(std::move(cells));
                        }
                }
            }
            catch (...)
            {
                return false;
            }
            it = cache.insert_or_assign(file, Cached{ t, std::move(data) }).first;
        }
        out = it->second.Value;
        return true;
    }

    bool Save(const std::string& path, const Def& def, const Data& data)
    {
        YAML::Emitter out;
        out << YAML::BeginMap;
        out << YAML::Key << "Asset" << YAML::Value << def.Kind;
        out << YAML::Key << "Fields" << YAML::Value << YAML::BeginMap;
        for (const auto& f : def.Fields)
        {
            out << YAML::Key << f.Name;
            if (f.Type != FieldType::Table)
            {
                auto it = data.Values.find(f.Name);
                out << YAML::Value << YAML::DoubleQuoted << (it == data.Values.end() ? f.Default : it->second);
                continue;
            }
            out << YAML::Value << YAML::BeginSeq;
            auto it = data.Tables.find(f.Name);
            if (it != data.Tables.end())
                for (const auto& row : it->second)
                {
                    out << YAML::Flow << YAML::BeginSeq;
                    for (const auto& c : row)
                        out << YAML::DoubleQuoted << c;
                    out << YAML::EndSeq;
                }
            out << YAML::EndSeq;
        }
        out << YAML::EndMap << YAML::EndMap;
        std::ofstream file(SceneSerializer::ResolveAssetPath(path), std::ios::binary);
        if (!file)
            return false;
        file << out.c_str() << "\n";
        return file.good();
    }

    std::vector<std::string> FindAll(const char* kind)
    {
        std::vector<std::string> out;
        const Def* def = ForKind(kind);
        const fs::path assets = Project::GetAssetDirectory();
        std::error_code ec;
        if (!def || assets.empty() || !fs::exists(assets, ec))
            return out;
        for (fs::recursive_directory_iterator it(assets, fs::directory_options::skip_permission_denied, ec), end; it != end; it.increment(ec))
        {
            if (ec)
                break;
            if (it->is_regular_file(ec) && Lower(it->path().extension().string()) == def->Extension)
                out.push_back(it->path().string());
        }
        std::sort(out.begin(), out.end());
        return out;
    }

    std::string Cell(const std::vector<std::string>& row, size_t index, const std::string& def)
    {
        return index < row.size() && !row[index].empty() ? row[index] : def;
    }

    float CellFloat(const std::vector<std::string>& row, size_t index, float def)
    {
        return index < row.size() && !row[index].empty() ? (float)std::atof(row[index].c_str()) : def;
    }

    bool CellBool(const std::vector<std::string>& row, size_t index, bool def)
    {
        return index < row.size() && !row[index].empty() ? (row[index] == "true" || row[index] == "1") : def;
    }
}
