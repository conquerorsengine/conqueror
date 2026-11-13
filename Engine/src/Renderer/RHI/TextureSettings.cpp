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
               std::fabs(PixelsPerUnit - o.PixelsPerUnit) < 1e-6f && Shape == o.Shape && SpriteMode == o.SpriteMode && PolygonSides == o.PolygonSides &&
               TightMesh == o.TightMesh && ExtrudeEdges == o.ExtrudeEdges && GeneratePhysicsShape == o.GeneratePhysicsShape && Resize == o.Resize &&
               Format == o.Format && Compression == o.Compression && UseCrunch == o.UseCrunch && CrunchQuality == o.CrunchQuality &&
               StreamingMipmaps == o.StreamingMipmaps && StreamingPriority == o.StreamingPriority && ReadWrite == o.ReadWrite &&
               Platforms[0] == o.Platforms[0] && Platforms[1] == o.Platforms[1] && Platforms[2] == o.Platforms[2];
    }

    TexturePlatform TextureImportSettings::CurrentPlatform()
    {
#if defined(__EMSCRIPTEN__)
        return TexturePlatform::Web;
#elif defined(__ANDROID__) || (defined(__APPLE__) && defined(TARGET_OS_IPHONE) && TARGET_OS_IPHONE)
        return TexturePlatform::Mobile;
#else
        return TexturePlatform::PC;
#endif
    }

    TextureImportSettings TextureImportSettings::ForPlatform(TexturePlatform platform) const
    {
        TextureImportSettings s = *this;
        const PlatformTextureSettings& p = Platforms[(int)platform];
        if (p.Override)
        {
            s.MaxSize = p.MaxSize;
            s.Resize = p.Resize;
            s.Format = p.Format;
            s.Compression = p.Compression;
            s.UseCrunch = p.UseCrunch;
            s.CrunchQuality = p.CrunchQuality;
        }
        return s;
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
            static const char* shapes[] = { "2D", "Cube" };
            static const char* spriteModes[] = { "Single", "Multiple", "Polygon" };
            out.Shape = ParseEnum(t["Shape"], shapes, 2, out.Shape);
            out.SpriteMode = ParseEnum(t["SpriteMode"], spriteModes, 3, out.SpriteMode);
            if (t["PolygonSides"]) out.PolygonSides = std::clamp(t["PolygonSides"].as<int>(), 3, 128);
            if (t["MeshType"]) out.TightMesh = t["MeshType"].as<std::string>() != "FullRect";
            if (t["ExtrudeEdges"]) out.ExtrudeEdges = std::clamp(t["ExtrudeEdges"].as<int>(), 0, 32);
            if (t["GeneratePhysicsShape"]) out.GeneratePhysicsShape = t["GeneratePhysicsShape"].as<bool>();
            if (t["StreamingMipmaps"]) out.StreamingMipmaps = t["StreamingMipmaps"].as<bool>();
            if (t["StreamingPriority"]) out.StreamingPriority = std::clamp(t["StreamingPriority"].as<int>(), -128, 127);
            if (t["ReadWrite"]) out.ReadWrite = t["ReadWrite"].as<bool>();
            auto readPlatform = [](const YAML::Node& n, int& maxSize, TextureResizeAlgorithm& resize, TextureFormatChoice& format, TextureCompression& compression, bool& crunch, int& quality) {
                static const char* resizes[] = { "Mitchell", "Bilinear" };
                static const char* formats[] = { "Automatic", "RGBA32", "RGB24", "R8" };
                static const char* compressions[] = { "None", "Low", "Normal", "High" };
                if (n["MaxSize"]) maxSize = std::clamp(n["MaxSize"].as<int>(), 32, 16384);
                resize = ParseEnum(n["Resize"], resizes, 2, resize);
                format = ParseEnum(n["Format"], formats, 4, format);
                compression = ParseEnum(n["Compression"], compressions, 4, compression);
                if (n["Crunch"]) crunch = n["Crunch"].as<bool>();
                if (n["CrunchQuality"]) quality = std::clamp(n["CrunchQuality"].as<int>(), 0, 100);
            };
            int unused = out.MaxSize;
            readPlatform(t, unused, out.Resize, out.Format, out.Compression, out.UseCrunch, out.CrunchQuality);
            if (auto list = t["Platforms"]; list && list.IsMap())
            {
                static const char* platforms[] = { "PC", "Web", "Mobile" };
                for (int i = 0; i < kTexturePlatformCount; ++i)
                    if (auto n = list[platforms[i]])
                    {
                        auto& p = out.Platforms[i];
                        p.Override = true;
                        p.MaxSize = out.MaxSize;
                        readPlatform(n, p.MaxSize, p.Resize, p.Format, p.Compression, p.UseCrunch, p.CrunchQuality);
                    }
            }
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
        out << YAML::Key << "Shape" << YAML::Value << (Shape == TextureShape::Cube ? "Cube" : "2D");
        out << YAML::Key << "SpriteMode" << YAML::Value << (SpriteMode == SpriteImportMode::Multiple ? "Multiple" : SpriteMode == SpriteImportMode::Polygon ? "Polygon" : "Single");
        out << YAML::Key << "PolygonSides" << YAML::Value << PolygonSides;
        out << YAML::Key << "MeshType" << YAML::Value << (TightMesh ? "Tight" : "FullRect");
        out << YAML::Key << "ExtrudeEdges" << YAML::Value << ExtrudeEdges;
        out << YAML::Key << "GeneratePhysicsShape" << YAML::Value << GeneratePhysicsShape;
        if (StreamingMipmaps)
        {
            out << YAML::Key << "StreamingMipmaps" << YAML::Value << StreamingMipmaps;
            out << YAML::Key << "StreamingPriority" << YAML::Value << StreamingPriority;
        }
        if (ReadWrite)
            out << YAML::Key << "ReadWrite" << YAML::Value << true;
        auto writePlatform = [&out](TextureResizeAlgorithm resize, TextureFormatChoice format, TextureCompression compression, bool crunch, int quality) {
            static const char* formats[] = { "Automatic", "RGBA32", "RGB24", "R8" };
            static const char* compressions[] = { "None", "Low", "Normal", "High" };
            out << YAML::Key << "Resize" << YAML::Value << (resize == TextureResizeAlgorithm::Bilinear ? "Bilinear" : "Mitchell");
            out << YAML::Key << "Format" << YAML::Value << formats[(int)format];
            out << YAML::Key << "Compression" << YAML::Value << compressions[(int)compression];
            out << YAML::Key << "Crunch" << YAML::Value << crunch;
            out << YAML::Key << "CrunchQuality" << YAML::Value << quality;
        };
        writePlatform(Resize, Format, Compression, UseCrunch, CrunchQuality);
        if (Platforms[0].Override || Platforms[1].Override || Platforms[2].Override)
        {
            static const char* platforms[] = { "PC", "Web", "Mobile" };
            out << YAML::Key << "Platforms" << YAML::Value << YAML::BeginMap;
            for (int i = 0; i < kTexturePlatformCount; ++i)
                if (Platforms[i].Override)
                {
                    const auto& p = Platforms[i];
                    out << YAML::Key << platforms[i] << YAML::Value << YAML::BeginMap;
                    out << YAML::Key << "MaxSize" << YAML::Value << p.MaxSize;
                    writePlatform(p.Resize, p.Format, p.Compression, p.UseCrunch, p.CrunchQuality);
                    out << YAML::EndMap;
                }
            out << YAML::EndMap;
        }
        out << YAML::EndMap << YAML::EndMap;
        std::ofstream f(MetaPath(texturePath));
        if (!f.is_open())
            return false;
        f << out.c_str() << "\n";
        return true;
    }
}
