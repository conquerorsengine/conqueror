#include "BlendSpace.h"

#include "Core/Serialization/SceneSerializer.h"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <fstream>

namespace Conqueror::BlendSpaces
{
    namespace
    {
        BlendSpaceAxis ReadAxis(const YAML::Node& n, const BlendSpaceAxis& fallback)
        {
            BlendSpaceAxis a = fallback;
            if (!n)
                return a;
            a.Name = n["Name"].as<std::string>(a.Name);
            a.Min = n["Min"].as<float>(a.Min);
            a.Max = n["Max"].as<float>(a.Max);
            a.Divisions = std::clamp(n["Divisions"].as<int>(a.Divisions), 1, 32);
            if (a.Max <= a.Min)
                a.Max = a.Min + 1.0f;
            return a;
        }

        void EmitAxis(YAML::Emitter& out, const char* key, const BlendSpaceAxis& a)
        {
            out << YAML::Key << key << YAML::Value << YAML::Flow << YAML::BeginMap;
            out << YAML::Key << "Name" << YAML::Value << a.Name;
            out << YAML::Key << "Min" << YAML::Value << a.Min;
            out << YAML::Key << "Max" << YAML::Value << a.Max;
            out << YAML::Key << "Divisions" << YAML::Value << a.Divisions;
            out << YAML::EndMap;
        }
    }

    BlendSpaceAsset Default(bool is2D)
    {
        BlendSpaceAsset a;
        a.Is2D = is2D;
        a.AxisX = { "Speed", 0.0f, 600.0f, 4 };
        a.AxisY = { "Direction", -180.0f, 180.0f, 4 };
        if (!is2D)
            a.AxisX = { "Speed", 0.0f, 10.0f, 4 };
        return a;
    }

    bool Load(const std::string& path, BlendSpaceAsset& out, std::string* error)
    {
        try
        {
            YAML::Node root = YAML::LoadFile(SceneSerializer::ResolveAssetPath(path));
            YAML::Node b = root["BlendSpace"];
            if (!b)
            {
                if (error) *error = "not a blend space file";
                return false;
            }
            out = {};
            out.Is2D = b["Type"].as<std::string>("2D") != "1D";
            const BlendSpaceAsset d = Default(out.Is2D);
            out.AxisX = ReadAxis(b["AxisX"], d.AxisX);
            out.AxisY = ReadAxis(b["AxisY"], d.AxisY);
            out.ClipsFrom = b["ClipsFrom"].as<std::string>("");
            if (auto list = b["Samples"]; list && list.IsSequence())
                for (const auto& s : list)
                {
                    BlendSpaceSample sample;
                    sample.Clip = s["Clip"].as<std::string>("");
                    sample.Position.x = s["X"].as<float>(0.0f);
                    sample.Position.y = s["Y"].as<float>(0.0f);
                    out.Samples.push_back(sample);
                }
            return true;
        }
        catch (const std::exception& e)
        {
            if (error) *error = e.what();
            return false;
        }
    }

    std::string ToText(const BlendSpaceAsset& a)
    {
        YAML::Emitter out;
        out << YAML::BeginMap << YAML::Key << "BlendSpace" << YAML::Value << YAML::BeginMap;
        out << YAML::Key << "Type" << YAML::Value << (a.Is2D ? "2D" : "1D");
        EmitAxis(out, "AxisX", a.AxisX);
        if (a.Is2D)
            EmitAxis(out, "AxisY", a.AxisY);
        out << YAML::Key << "ClipsFrom" << YAML::Value << a.ClipsFrom;
        out << YAML::Key << "Samples" << YAML::Value << YAML::BeginSeq;
        for (const auto& s : a.Samples)
        {
            out << YAML::Flow << YAML::BeginMap << YAML::Key << "Clip" << YAML::Value << s.Clip << YAML::Key << "X" << YAML::Value << s.Position.x;
            if (a.Is2D)
                out << YAML::Key << "Y" << YAML::Value << s.Position.y;
            out << YAML::EndMap;
        }
        out << YAML::EndSeq << YAML::EndMap << YAML::EndMap;
        return std::string("# Conqueror's Engine - Blend Space\n") + out.c_str() + "\n";
    }

    bool Save(const std::string& path, const BlendSpaceAsset& asset)
    {
        std::ofstream f(path, std::ios::binary);
        if (!f)
            return false;
        f << ToText(asset);
        return (bool)f;
    }
}
