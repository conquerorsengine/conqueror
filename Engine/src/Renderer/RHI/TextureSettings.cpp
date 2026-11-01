#include "TextureSettings.h"
#include "Core/Logging/Log.h"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>

namespace Conqueror
{
    bool TextureImportSettings::operator==(const TextureImportSettings& o) const
    {
        return Type == o.Type && Filter == o.Filter && Wrap == o.Wrap && Mipmaps == o.Mipmaps && Anisotropy == o.Anisotropy &&
               MaxSize == o.MaxSize && NormalFromGrayscale == o.NormalFromGrayscale && std::fabs(Bumpiness - o.Bumpiness) < 1e-6f &&
               std::fabs(PixelsPerUnit - o.PixelsPerUnit) < 1e-6f;
    }

    TextureImportSettings TextureImportSettings::DefaultsFor(TextureType type)
    {
        TextureImportSettings s;
        s.Type = type;
        if (type == TextureType::Sprite || type == TextureType::UI)
        {
            s.Mipmaps = false;
            s.Filter = TextureFilter::Bilinear;
            s.Wrap = TextureWrap::Clamp;
            s.Anisotropy = 1;
        }
        return s;
    }

    std::string TextureImportSettings::MetaPath(const std::string& texturePath)
    {
        return texturePath + ".meta";
    }

    const char* TextureImportSettings::TypeName(TextureType type)
    {
        switch (type)
        {
            case TextureType::NormalMap: return "NormalMap";
            case TextureType::Sprite: return "Sprite";
            case TextureType::UI: return "UI";
            default: return "Default";
        }
    }
    const char* TextureImportSettings::FilterName(TextureFilter f)
    {
        return f == TextureFilter::Point ? "Point" : f == TextureFilter::Bilinear ? "Bilinear" : "Trilinear";
    }
    const char* TextureImportSettings::WrapName(TextureWrap w)
    {
        return w == TextureWrap::Clamp ? "Clamp" : w == TextureWrap::Mirror ? "Mirror" : "Repeat";
    }

    namespace
    {
        template<typename E>
        E ParseEnum(const YAML::Node& n, const char* const* names, int count, E def)
        {
            if (!n || !n.IsScalar())
                return def;
            std::string v = n.as<std::string>();
            for (int i = 0; i < count; ++i)
                if (v == names[i])
                    return (E)i;
            return def;
        }
    }

    bool TextureImportSettings::Load(const std::string& texturePath, TextureImportSettings& out)
    {
        out = TextureImportSettings{};
        std::error_code ec;
        const std::string meta = MetaPath(texturePath);
        if (!std::filesystem::exists(meta, ec))
            return false;
        try
        {
            YAML::Node root = YAML::LoadFile(meta);
            YAML::Node t = root["TextureImporter"] ? root["TextureImporter"] : root;
            static const char* types[] = { "Default", "NormalMap", "Sprite", "UI" };
            static const char* filters[] = { "Point", "Bilinear", "Trilinear" };
            static const char* wraps[] = { "Repeat", "Clamp", "Mirror" };
            out.Type = ParseEnum(t["Type"], types, 4, TextureType::Default);
            out = DefaultsFor(out.Type);
            out.Filter = ParseEnum(t["Filter"], filters, 3, out.Filter);
            out.Wrap = ParseEnum(t["Wrap"], wraps, 3, out.Wrap);
            if (t["Mipmaps"]) out.Mipmaps = t["Mipmaps"].as<bool>();
            if (t["Anisotropy"]) out.Anisotropy = std::clamp(t["Anisotropy"].as<int>(), 1, 16);
            if (t["MaxSize"]) out.MaxSize = std::clamp(t["MaxSize"].as<int>(), 32, 16384);
            if (t["NormalFromGrayscale"]) out.NormalFromGrayscale = t["NormalFromGrayscale"].as<bool>();
            if (t["Bumpiness"]) out.Bumpiness = t["Bumpiness"].as<float>();
            if (t["PixelsPerUnit"]) out.PixelsPerUnit = std::max(0.01f, t["PixelsPerUnit"].as<float>());
            return true;
        }
        catch (const std::exception& e)
        {
            CQ_CORE_WARN("[Texture] Bad import settings {0}: {1}", meta, e.what());
            return false;
        }
    }

    bool TextureImportSettings::Save(const std::string& texturePath) const
    {
        YAML::Emitter out;
        out << YAML::BeginMap << YAML::Key << "TextureImporter" << YAML::Value << YAML::BeginMap;
        out << YAML::Key << "Type" << YAML::Value << TypeName(Type);
        out << YAML::Key << "Filter" << YAML::Value << FilterName(Filter);
        out << YAML::Key << "Wrap" << YAML::Value << WrapName(Wrap);
        out << YAML::Key << "Mipmaps" << YAML::Value << Mipmaps;
        out << YAML::Key << "Anisotropy" << YAML::Value << Anisotropy;
        out << YAML::Key << "MaxSize" << YAML::Value << MaxSize;
        out << YAML::Key << "NormalFromGrayscale" << YAML::Value << NormalFromGrayscale;
        out << YAML::Key << "Bumpiness" << YAML::Value << Bumpiness;
        out << YAML::Key << "PixelsPerUnit" << YAML::Value << PixelsPerUnit;
        out << YAML::EndMap << YAML::EndMap;
        std::ofstream f(MetaPath(texturePath));
        if (!f.is_open())
            return false;
        f << out.c_str() << "\n";
        return true;
    }
}
