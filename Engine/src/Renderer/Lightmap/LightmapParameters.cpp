#include "LightmapParameters.h"

#include "Core/Serialization/SceneSerializer.h"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <unordered_map>

namespace Conqueror::LightmapParameterAssets
{
    bool Load(const std::string& path, LightmapParameters& out)
    {
        out = {};
        if (path.empty())
            return false;
        struct Cached { std::filesystem::file_time_type Time; LightmapParameters Params; };
        static std::unordered_map<std::string, Cached> cache;
        static std::mutex mutex;   // pişirme ayrı thread'de okur
        std::lock_guard<std::mutex> lock(mutex);
        const std::string file = SceneSerializer::ResolveAssetPath(path);
        std::error_code ec;
        const auto t = std::filesystem::last_write_time(file, ec);
        if (ec)
            return false;
        auto it = cache.find(file);
        if (it == cache.end() || it->second.Time != t)
        {
            try
            {
                YAML::Node n = YAML::LoadFile(file)["LightmapParameters"];
                if (!n)
                    return false;
                LightmapParameters p;
                if (n["AntiAliasingSamples"]) p.AntiAliasingSamples = std::clamp(n["AntiAliasingSamples"].as<int>(), 1, 16);
                if (n["BackfaceTolerance"]) p.BackfaceTolerance = std::clamp(n["BackfaceTolerance"].as<float>(), 0.0f, 1.0f);
                if (n["Pushoff"]) p.Pushoff = std::clamp(n["Pushoff"].as<float>(), 0.0f, 1.0f);
                if (n["IndirectSampleScale"]) p.IndirectSampleScale = std::clamp(n["IndirectSampleScale"].as<float>(), 0.1f, 8.0f);
                it = cache.insert_or_assign(file, Cached{ t, p }).first;
            }
            catch (...) { return false; }
        }
        out = it->second.Params;
        return true;
    }

    bool Save(const std::string& path, const LightmapParameters& p)
    {
        YAML::Emitter e;
        e << YAML::BeginMap << YAML::Key << "LightmapParameters" << YAML::Value << YAML::BeginMap;
        e << YAML::Key << "AntiAliasingSamples" << YAML::Value << p.AntiAliasingSamples;
        e << YAML::Key << "BackfaceTolerance" << YAML::Value << p.BackfaceTolerance;
        e << YAML::Key << "Pushoff" << YAML::Value << p.Pushoff;
        e << YAML::Key << "IndirectSampleScale" << YAML::Value << p.IndirectSampleScale;
        e << YAML::EndMap << YAML::EndMap;
        std::ofstream f(SceneSerializer::ResolveAssetPath(path), std::ios::trunc);
        if (!f)
            return false;
        f << e.c_str() << "\n";
        return true;
    }
}
