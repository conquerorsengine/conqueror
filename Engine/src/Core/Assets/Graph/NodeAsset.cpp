#include "NodeAsset.h"

#include "Core/Serialization/SceneSerializer.h"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>

namespace Conqueror::NodeAssets
{
    std::string Node::Str(const std::string& key, const std::string& def) const
    {
        auto it = Props.find(key);
        return it == Props.end() ? def : it->second;
    }

    float Node::Float(const std::string& key, float def) const
    {
        auto it = Props.find(key);
        return it == Props.end() || it->second.empty() ? def : (float)std::atof(it->second.c_str());
    }

    int Node::Int(const std::string& key, int def) const
    {
        auto it = Props.find(key);
        return it == Props.end() || it->second.empty() ? def : std::atoi(it->second.c_str());
    }

    bool Node::Bool(const std::string& key, bool def) const
    {
        auto it = Props.find(key);
        return it == Props.end() || it->second.empty() ? def : (it->second == "true" || it->second == "1");
    }

    Node* File::Find(int id)
    {
        for (auto& n : Nodes)
            if (n.Id == id)
                return &n;
        return nullptr;
    }

    const Node* File::Find(int id) const
    {
        return const_cast<File*>(this)->Find(id);
    }

    int File::Add(const std::string& type, float x, float y)
    {
        Node n;
        n.Id = NextId++;
        n.Type = type;
        n.X = x;
        n.Y = y;
        Nodes.push_back(std::move(n));
        return Nodes.back().Id;
    }

    void File::Remove(int id)
    {
        Links.erase(std::remove_if(Links.begin(), Links.end(), [id](const Link& l) { return l.From == id || l.To == id; }), Links.end());
        Nodes.erase(std::remove_if(Nodes.begin(), Nodes.end(), [id](const Node& n) { return n.Id == id; }), Nodes.end());
    }

    bool File::Connect(int from, int fromPin, int to, int toPin)
    {
        if (from == to || !Find(from) || !Find(to))
            return false;
        for (const auto& l : Links)
            if (l.From == from && l.FromPin == fromPin && l.To == to && l.ToPin == toPin)
                return false;
        Link l;
        l.Id = NextId++;
        l.From = from;
        l.FromPin = fromPin;
        l.To = to;
        l.ToPin = toPin;
        Links.push_back(l);
        return true;
    }

    void File::Disconnect(int linkId)
    {
        Links.erase(std::remove_if(Links.begin(), Links.end(), [linkId](const Link& l) { return l.Id == linkId; }), Links.end());
    }

    std::vector<const Link*> File::Out(int node, int pin) const
    {
        std::vector<const Link*> r;
        for (const auto& l : Links)
            if (l.From == node && (pin < 0 || l.FromPin == pin))
                r.push_back(&l);
        return r;
    }

    std::vector<const Link*> File::In(int node, int pin) const
    {
        std::vector<const Link*> r;
        for (const auto& l : Links)
            if (l.To == node && (pin < 0 || l.ToPin == pin))
                r.push_back(&l);
        return r;
    }

    std::string File::ToText() const
    {
        YAML::Emitter out;
        out << YAML::BeginMap;
        out << YAML::Key << "NodeAsset" << YAML::Value << Kind;
        out << YAML::Key << "NextId" << YAML::Value << NextId;
        out << YAML::Key << "Nodes" << YAML::Value << YAML::BeginSeq;
        for (const auto& n : Nodes)
        {
            out << YAML::BeginMap;
            out << YAML::Key << "Id" << YAML::Value << n.Id;
            out << YAML::Key << "Type" << YAML::Value << n.Type;
            out << YAML::Key << "X" << YAML::Value << n.X;
            out << YAML::Key << "Y" << YAML::Value << n.Y;
            if (!n.Props.empty())
            {
                out << YAML::Key << "Props" << YAML::Value << YAML::BeginMap;
                for (const auto& [k, v] : n.Props)
                    out << YAML::Key << k << YAML::Value << YAML::DoubleQuoted << v;
                out << YAML::EndMap;
            }
            out << YAML::EndMap;
        }
        out << YAML::EndSeq;
        out << YAML::Key << "Links" << YAML::Value << YAML::BeginSeq;
        for (const auto& l : Links)
            out << YAML::Flow << YAML::BeginSeq << l.Id << l.From << l.FromPin << l.To << l.ToPin << YAML::EndSeq;
        out << YAML::EndSeq;
        out << YAML::EndMap;
        return out.c_str();
    }

    bool File::FromText(const std::string& text)
    {
        try
        {
            YAML::Node root = YAML::Load(text);
            if (!root["NodeAsset"])
                return false;
            File f;
            f.Kind = root["NodeAsset"].as<std::string>();
            if (root["NextId"]) f.NextId = root["NextId"].as<int>();
            if (auto nodes = root["Nodes"]; nodes && nodes.IsSequence())
                for (const auto& v : nodes)
                {
                    Node n;
                    n.Id = v["Id"].as<int>();
                    n.Type = v["Type"].as<std::string>();
                    if (v["X"]) n.X = v["X"].as<float>();
                    if (v["Y"]) n.Y = v["Y"].as<float>();
                    if (auto p = v["Props"]; p && p.IsMap())
                        for (const auto& kv : p)
                            n.Props[kv.first.as<std::string>()] = kv.second.IsNull() ? std::string() : kv.second.as<std::string>();
                    f.NextId = std::max(f.NextId, n.Id + 1);
                    f.Nodes.push_back(std::move(n));
                }
            if (auto links = root["Links"]; links && links.IsSequence())
                for (const auto& v : links)
                {
                    if (!v.IsSequence() || v.size() < 5)
                        continue;
                    Link l;
                    l.Id = v[0].as<int>();
                    l.From = v[1].as<int>();
                    l.FromPin = v[2].as<int>();
                    l.To = v[3].as<int>();
                    l.ToPin = v[4].as<int>();
                    f.NextId = std::max(f.NextId, l.Id + 1);
                    f.Links.push_back(l);
                }
            *this = std::move(f);
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    bool Load(const std::string& path, File& out)
    {
        struct Cached { std::filesystem::file_time_type Time; File Asset; };
        static std::unordered_map<std::string, Cached> cache;
        const std::string file = SceneSerializer::ResolveAssetPath(path);
        std::error_code ec;
        const auto t = std::filesystem::last_write_time(file, ec);
        if (ec)
            return false;
        auto it = cache.find(file);
        if (it == cache.end() || it->second.Time != t)
        {
            std::ifstream in(file, std::ios::binary);
            std::stringstream ss;
            ss << in.rdbuf();
            File f;
            if (!f.FromText(ss.str()))
                return false;
            it = cache.insert_or_assign(file, Cached{ t, std::move(f) }).first;
        }
        out = it->second.Asset;
        return true;
    }

    bool Save(const std::string& path, const File& file)
    {
        std::ofstream out(SceneSerializer::ResolveAssetPath(path), std::ios::binary);
        if (!out)
            return false;
        out << file.ToText() << "\n";
        return out.good();
    }

    std::string KindOf(const std::string& path)
    {
        File f;
        return Load(path, f) ? f.Kind : std::string();
    }
}
