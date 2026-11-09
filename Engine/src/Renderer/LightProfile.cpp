#include "LightProfile.h"

#include "Core/Assets/SimpleAsset.h"
#include "Core/Logging/Log.h"
#include "Core/Serialization/SceneSerializer.h"
#include "Scene/ComponentRegistry.h"
#include "Scene/Entity.h"
#include "Scene/Scene.h"

#include <IconsFontAwesome6.h>
#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <vector>

namespace Conqueror
{
    template<> void Scene::OnComponentAdded<LightProfileComponent>(Entity, LightProfileComponent&) {}

    namespace LightProfiles
    {
        namespace
        {
            namespace fs = std::filesystem;

            struct Curve
            {
                std::vector<float> Angles;
                std::vector<float> Values;   // en büyüğe göre 0..1
                float Brightness = 1.0f;
            };

            bool ParseIES(const std::string& file, Curve& out)
            {
                std::ifstream in(file, std::ios::binary);
                if (!in)
                    return false;
                std::string line;
                // TILT= satırına kadar başlık
                while (std::getline(in, line))
                    if (line.rfind("TILT=", 0) == 0 || line.find("TILT=") != std::string::npos)
                        break;
                if (line.find("TILT=NONE") == std::string::npos && line.find("TILT= NONE") == std::string::npos)
                    return false;
                std::vector<float> numbers;
                std::string token;
                while (in >> token)
                    numbers.push_back((float)std::atof(token.c_str()));
                // lamba sayısı, lümen, çarpan, dikey açı sayısı, yatay açı sayısı, tür, birim, g, u, y, balast, gelecek, watt
                if (numbers.size() < 13)
                    return false;
                const int vertical = (int)numbers[3], horizontal = (int)numbers[4];
                const float multiplier = numbers[2];
                const size_t base = 13;
                if (vertical <= 1 || horizontal < 1 || numbers.size() < base + (size_t)vertical + (size_t)horizontal + (size_t)(vertical * horizontal))
                    return false;
                out.Angles.assign(numbers.begin() + (long)base, numbers.begin() + (long)base + vertical);
                std::vector<float> values((size_t)vertical, 0.0f);
                const size_t candela = base + (size_t)vertical + (size_t)horizontal;
                for (int h = 0; h < horizontal; ++h)
                    for (int v = 0; v < vertical; ++v)
                        values[(size_t)v] += numbers[candela + (size_t)(h * vertical + v)] * multiplier / (float)horizontal;
                const float peak = *std::max_element(values.begin(), values.end());
                if (peak <= 0.0f)
                    return false;
                for (auto& v : values)
                    v /= peak;
                out.Values = std::move(values);
                return true;
            }

            const Curve* Find(const std::string& path)
            {
                struct Cached { fs::file_time_type Time; Curve Value; bool Ok; };
                static std::map<std::string, Cached> cache;
                const std::string file = SceneSerializer::ResolveAssetPath(path);
                std::error_code ec;
                const auto time = fs::last_write_time(file, ec);
                if (ec)
                    return nullptr;
                auto it = cache.find(file);
                if (it != cache.end() && it->second.Time == time)
                    return it->second.Ok ? &it->second.Value : nullptr;
                Curve curve;
                bool ok = false;
                if (fs::path(file).extension() == ".ies")
                    ok = ParseIES(file, curve);
                else
                {
                    SimpleAssets::Data data;
                    if (SimpleAssets::Load(file, data))
                    {
                        curve.Brightness = data.Float("Brightness", 1.0f);
                        const std::string source = data.Str("Source");
                        if (!source.empty())
                            ok = ParseIES(SceneSerializer::ResolveAssetPath(source), curve);
                        else
                        {
                            for (const auto& row : data.Table("Points"))
                            {
                                curve.Angles.push_back(SimpleAssets::CellFloat(row, 0));
                                curve.Values.push_back(SimpleAssets::CellFloat(row, 1));
                            }
                            ok = curve.Angles.size() >= 2;
                        }
                    }
                }
                if (!ok)
                    CQ_CORE_WARN("[Light Profile] Cannot read {0}", path);
                it = cache.insert_or_assign(file, Cached{ time, std::move(curve), ok }).first;
                return ok ? &it->second.Value : nullptr;
            }

            float Evaluate(const Curve& c, float angle)
            {
                if (angle <= c.Angles.front())
                    return c.Values.front();
                for (size_t i = 0; i + 1 < c.Angles.size(); ++i)
                    if (angle <= c.Angles[i + 1])
                    {
                        const float d = c.Angles[i + 1] - c.Angles[i];
                        const float k = d > 0.0f ? (angle - c.Angles[i]) / d : 0.0f;
                        return c.Values[i] + (c.Values[i + 1] - c.Values[i]) * k;
                    }
                return c.Values.back();
            }
        }

        bool Sample(const std::string& path, float maxAngleDegrees, glm::vec4& out0, glm::vec4& out1)
        {
            const Curve* curve = path.empty() ? nullptr : Find(path);
            if (!curve)
                return false;
            float samples[8];
            for (int i = 0; i < 8; ++i)
                samples[i] = Evaluate(*curve, maxAngleDegrees * (float)i / 7.0f) * curve->Brightness;
            out0 = glm::vec4(samples[0], samples[1], samples[2], samples[3]);
            out1 = glm::vec4(samples[4], samples[5], samples[6], samples[7]);
            return true;
        }

        void RegisterComponents()
        {
            using C = LightProfileComponent;
            ComponentDef<C>("Light Profile", "LightProfileComponent", "Rendering", ICON_FA_LIGHTBULB)
                .Describe("Gives a Spot Light or Point Light on this object the angular falloff of a photometric profile (.cqlightprofile or .ies).")
                .Field("Profile", "Profile", &C::Profile, FieldKind::Asset).Filter(".cqlightprofile;.ies");
        }
    }
}
