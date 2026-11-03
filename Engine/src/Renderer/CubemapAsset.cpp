#include "CubemapAsset.h"

#include "Core/Serialization/SceneSerializer.h"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>

namespace Conqueror::CubemapAssets
{
    bool IsAsset(const std::string& path)
    {
        std::string e = std::filesystem::path(path).extension().string();
        for (char& c : e)
            c = (char)std::tolower((unsigned char)c);
        return e == Extension;
    }

    const char* FaceName(int face)
    {
        static const char* names[6] = { "+X (Right)", "-X (Left)", "+Y (Top)", "-Y (Bottom)", "+Z (Front)", "-Z (Back)" };
        return face >= 0 && face < 6 ? names[face] : "?";
    }

    bool Load(const std::string& pathIn, CubemapAssetData& out, std::string* error)
    {
        try
        {
            YAML::Node root = YAML::LoadFile(SceneSerializer::ResolveAssetPath(pathIn));
            YAML::Node c = root["Cubemap"];
            if (!c)
            {
                if (error) *error = "not a cubemap file";
                return false;
            }
            CubemapAssetData d;
            d.Source = c["Source"].as<std::string>("Faces") == "Panorama" ? CubemapAssetData::SourceKind::Panorama : CubemapAssetData::SourceKind::Faces;
            if (auto faces = c["Faces"]; faces && faces.IsSequence())
                for (size_t i = 0; i < 6 && i < faces.size(); ++i)
                    d.Faces[i] = faces[i].as<std::string>("");
            d.Panorama = c["Panorama"].as<std::string>("");
            d.Resolution = std::clamp(c["Resolution"].as<int>(512), 32, 4096);
            out = d;
            return true;
        }
        catch (const std::exception& e)
        {
            if (error) *error = e.what();
            return false;
        }
    }

    bool Save(const std::string& pathIn, const CubemapAssetData& d)
    {
        YAML::Emitter out;
        out << YAML::BeginMap << YAML::Key << "Cubemap" << YAML::Value << YAML::BeginMap;
        out << YAML::Key << "Source" << YAML::Value << (d.Source == CubemapAssetData::SourceKind::Panorama ? "Panorama" : "Faces");
        out << YAML::Key << "Faces" << YAML::Value << YAML::BeginSeq;
        for (const auto& f : d.Faces)
            out << f;
        out << YAML::EndSeq;
        out << YAML::Key << "Panorama" << YAML::Value << d.Panorama;
        out << YAML::Key << "Resolution" << YAML::Value << d.Resolution;
        out << YAML::EndMap << YAML::EndMap;
        std::ofstream f(SceneSerializer::ResolveAssetPath(pathIn), std::ios::binary | std::ios::trunc);
        if (!f)
            return false;
        f << "# Conqueror's Engine - Cubemap\n" << out.c_str() << "\n";
        return true;
    }
}
