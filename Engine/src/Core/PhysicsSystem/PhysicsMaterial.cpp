#include "PhysicsMaterial.h"

#include "Core/Logging/Log.h"
#include "Core/Project/Project.h"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <unordered_map>

namespace Conqueror::PhysicsMaterials
{
    namespace
    {
        struct Cached
        {
            PhysicsMaterialAsset Mat;
            std::filesystem::file_time_type Time{};
        };

        std::filesystem::path Resolve(const std::string& path)
        {
            std::filesystem::path p(path);
            if (p.is_relative() && Project::GetActive())
            {
                auto inProject = Project::GetActiveProjectDirectory() / p;
                std::error_code ec;
                if (std::filesystem::exists(inProject, ec))
                    return inProject;
            }
            return p;
        }
    }

    bool IsFile(const std::string& path)
    {
        std::string ext = std::filesystem::path(path).extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        return ext == Extension;
    }

    bool Load(const std::string& path, PhysicsMaterialAsset& out)
    {
        if (path.empty())
            return false;
        static std::unordered_map<std::string, Cached> s_Cache;
        const auto file = Resolve(path);
        std::error_code ec;
        auto t = std::filesystem::last_write_time(file, ec);
        if (ec)
            return false;
        const std::string key = file.lexically_normal().generic_string();
        if (auto it = s_Cache.find(key); it != s_Cache.end() && it->second.Time == t)
        {
            out = it->second.Mat;
            return true;
        }
        try
        {
            YAML::Node root = YAML::LoadFile(file.string());
            YAML::Node n = root["PhysicsMaterial"];
            if (!n)
                return false;
            PhysicsMaterialAsset m;
            if (n["Friction"]) m.Friction = std::max(0.0f, n["Friction"].as<float>());
            if (n["Bounciness"]) m.Bounciness = std::clamp(n["Bounciness"].as<float>(), 0.0f, 1.0f);
            s_Cache[key] = { m, t };
            out = m;
            return true;
        }
        catch (const std::exception& e)
        {
            CQ_CORE_WARN("Physics material '{0}' could not be read: {1}", path, e.what());
            return false;
        }
    }

    bool Save(const PhysicsMaterialAsset& material, const std::string& path)
    {
        YAML::Emitter out;
        out << YAML::BeginMap << YAML::Key << "PhysicsMaterial" << YAML::Value << YAML::BeginMap;
        out << YAML::Key << "Friction" << YAML::Value << material.Friction;
        out << YAML::Key << "Bounciness" << YAML::Value << material.Bounciness;
        out << YAML::EndMap << YAML::EndMap;
        std::ofstream f(Resolve(path), std::ios::trunc);
        if (!f)
            return false;
        f << out.c_str() << "\n";
        return true;
    }
}
