#include "CurveAsset.h"

#include "Core/Logging/Log.h"
#include "Core/Serialization/SceneSerializer.h"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <unordered_map>

namespace Conqueror
{
    namespace
    {
        // Hermite parçası: s 0..1, dt parça süresi, m0 / m1 eğim (değer / saniye)
        float Hermite(float v0, float m0, float v1, float m1, float dt, float s)
        {
            const float s2 = s * s, s3 = s2 * s;
            const float h00 = 2.0f * s3 - 3.0f * s2 + 1.0f;
            const float h10 = s3 - 2.0f * s2 + s;
            const float h01 = -2.0f * s3 + 3.0f * s2;
            const float h11 = s3 - s2;
            return h00 * v0 + h10 * dt * m0 + h01 * v1 + h11 * dt * m1;
        }

        float Wrap(float t, float t0, float t1, CurveInfinity mode)
        {
            const float len = t1 - t0;
            if (len <= 1e-6f)
                return t0;
            if (mode == CurveInfinity::Loop)
            {
                float u = std::fmod(t - t0, len);
                if (u < 0.0f)
                    u += len;
                return t0 + u;
            }
            if (mode == CurveInfinity::PingPong)
            {
                float u = std::fmod(t - t0, 2.0f * len);
                if (u < 0.0f)
                    u += 2.0f * len;
                return u <= len ? t0 + u : t1 - (u - len);
            }
            return std::clamp(t, t0, t1);
        }
    }

    // ------------------------------------------------------------------ Kanal

    void CurveChannel::Recalculate()
    {
        std::stable_sort(Keys.begin(), Keys.end(), [](const CurveKey& a, const CurveKey& b) { return a.Time < b.Time; });
        const size_t n = Keys.size();
        for (size_t i = 0; i < n; ++i)
        {
            CurveKey& k = Keys[i];
            const CurveKey* prev = i > 0 ? &Keys[i - 1] : nullptr;
            const CurveKey* next = i + 1 < n ? &Keys[i + 1] : nullptr;
            auto slope = [](const CurveKey& a, const CurveKey& b) {
                const float dt = b.Time - a.Time;
                return dt > 1e-6f ? (b.Value - a.Value) / dt : 0.0f;
            };
            switch (k.Mode)
            {
            case CurveKeyMode::Constant:
                k.InTangent = 0.0f;
                k.OutTangent = 0.0f;
                break;
            case CurveKeyMode::Linear:
                k.InTangent = prev ? slope(*prev, k) : (next ? slope(k, *next) : 0.0f);
                k.OutTangent = next ? slope(k, *next) : k.InTangent;
                break;
            case CurveKeyMode::Auto:
            {
                // Catmull-Rom; tepe / çukurda ve uçlarda düz (Unreal "Auto" gibi taşma yapmaz)
                float m = 0.0f;
                if (prev && next)
                {
                    const bool extremum = (k.Value >= prev->Value && k.Value >= next->Value) || (k.Value <= prev->Value && k.Value <= next->Value);
                    const float dt = next->Time - prev->Time;
                    m = extremum || dt <= 1e-6f ? 0.0f : (next->Value - prev->Value) / dt;
                }
                k.InTangent = m;
                k.OutTangent = m;
                break;
            }
            case CurveKeyMode::Free:
                break;
            }
        }
    }

    int CurveChannel::AddKey(float time, float value, CurveKeyMode mode)
    {
        for (size_t i = 0; i < Keys.size(); ++i)
            if (std::fabs(Keys[i].Time - time) < 1e-5f)
            {
                Keys[i].Value = value;
                Recalculate();
                return (int)i;
            }
        CurveKey k;
        k.Time = time;
        k.Value = value;
        k.Mode = mode;
        Keys.push_back(k);
        Recalculate();
        for (size_t i = 0; i < Keys.size(); ++i)
            if (std::fabs(Keys[i].Time - time) < 1e-6f)
                return (int)i;
        return (int)Keys.size() - 1;
    }

    float CurveChannel::Evaluate(float time, CurveInfinity pre, CurveInfinity post) const
    {
        if (Keys.empty())
            return 0.0f;
        if (Keys.size() == 1)
            return Keys.front().Value;
        const CurveKey& first = Keys.front();
        const CurveKey& last = Keys.back();
        // Sonsuzluk: Linear uçtaki eğimle devam eder, diğerleri zamanı aralığa geri getirir
        if (time < first.Time)
        {
            if (pre == CurveInfinity::Linear)
                return first.Value + first.InTangent * (time - first.Time);
            time = Wrap(time, first.Time, last.Time, pre);
        }
        else if (time > last.Time)
        {
            if (post == CurveInfinity::Linear)
                return last.Value + last.OutTangent * (time - last.Time);
            time = Wrap(time, first.Time, last.Time, post);
        }
        // Parça (ikili arama)
        size_t hi = (size_t)(std::upper_bound(Keys.begin(), Keys.end(), time, [](float t, const CurveKey& k) { return t < k.Time; }) - Keys.begin());
        if (hi == 0)
            return first.Value;
        if (hi >= Keys.size())
            return last.Value;
        const CurveKey& a = Keys[hi - 1];
        const CurveKey& b = Keys[hi];
        const float dt = b.Time - a.Time;
        if (dt <= 1e-6f)
            return b.Value;
        if (a.Mode == CurveKeyMode::Constant)
            return a.Value;
        return Hermite(a.Value, a.OutTangent, b.Value, b.InTangent, dt, (time - a.Time) / dt);
    }

    // ------------------------------------------------------------------ Varlık

    CurveAsset CurveAsset::Default(CurveType type)
    {
        CurveAsset c;
        c.Type = type;
        auto channel = [](const char* name, float a, float b) {
            CurveChannel ch;
            ch.Name = name;
            ch.AddKey(0.0f, a, CurveKeyMode::Linear);
            ch.AddKey(1.0f, b, CurveKeyMode::Linear);
            return ch;
        };
        switch (type)
        {
        case CurveType::Float:
            c.Channels.push_back(channel("Value", 0.0f, 1.0f));
            break;
        case CurveType::Vector:
            c.Channels.push_back(channel("X", 0.0f, 1.0f));
            c.Channels.push_back(channel("Y", 0.0f, 1.0f));
            c.Channels.push_back(channel("Z", 0.0f, 1.0f));
            break;
        case CurveType::Color:
            c.Channels.push_back(channel("R", 1.0f, 1.0f));
            c.Channels.push_back(channel("G", 1.0f, 1.0f));
            c.Channels.push_back(channel("B", 1.0f, 1.0f));
            c.Channels.push_back(channel("A", 1.0f, 1.0f));
            break;
        }
        return c;
    }

    glm::vec4 CurveAsset::Evaluate(float time) const
    {
        glm::vec4 out(0.0f);
        for (size_t i = 0; i < Channels.size() && i < 4; ++i)
            out[(int)i] = Channels[i].Evaluate(time, PreInfinity, PostInfinity);
        if (Type == CurveType::Color && Channels.size() < 4)
            out.a = 1.0f;
        return out;
    }

    void CurveAsset::Range(float& minTime, float& maxTime, float& minValue, float& maxValue) const
    {
        minTime = 1e9f;
        maxTime = -1e9f;
        minValue = 1e9f;
        maxValue = -1e9f;
        for (const auto& ch : Channels)
            for (const auto& k : ch.Keys)
            {
                minTime = std::min(minTime, k.Time);
                maxTime = std::max(maxTime, k.Time);
                minValue = std::min(minValue, k.Value);
                maxValue = std::max(maxValue, k.Value);
            }
        if (minTime > maxTime)
        {
            minTime = 0.0f;
            maxTime = 1.0f;
        }
        if (minValue > maxValue)
        {
            minValue = 0.0f;
            maxValue = 1.0f;
        }
        // Teğetli eğri anahtarların dışına taşabilir: örneklenmiş değerler de aralığa
        for (const auto& ch : Channels)
            for (int i = 0; i <= 64; ++i)
            {
                const float v = ch.Evaluate(minTime + (maxTime - minTime) * (float)i / 64.0f, PreInfinity, PostInfinity);
                minValue = std::min(minValue, v);
                maxValue = std::max(maxValue, v);
            }
    }

    void CurveAsset::Recalculate()
    {
        for (auto& ch : Channels)
            ch.Recalculate();
    }

    // ------------------------------------------------------------------ Dosya

    namespace CurveAssets
    {
        namespace fs = std::filesystem;

        const char* TypeName(CurveType type)
        {
            switch (type)
            {
            case CurveType::Vector: return "Vector";
            case CurveType::Color: return "Color";
            default: return "Float";
            }
        }
        const char* ModeName(CurveKeyMode mode)
        {
            switch (mode)
            {
            case CurveKeyMode::Linear: return "Linear";
            case CurveKeyMode::Constant: return "Constant";
            case CurveKeyMode::Free: return "Free";
            default: return "Auto";
            }
        }
        const char* InfinityName(CurveInfinity mode)
        {
            switch (mode)
            {
            case CurveInfinity::Loop: return "Loop";
            case CurveInfinity::PingPong: return "PingPong";
            case CurveInfinity::Linear: return "Linear";
            default: return "Clamp";
            }
        }

        namespace
        {
            template<typename E>
            E Parse(const std::string& s, const char* (*name)(E), int count, E fallback)
            {
                for (int i = 0; i < count; ++i)
                    if (s == name((E)i))
                        return (E)i;
                return fallback;
            }

            struct CacheEntry
            {
                fs::file_time_type Time{};
                CurveAsset Curve;
                bool Ok = false;
            };
            std::unordered_map<std::string, CacheEntry>& Cache()
            {
                static std::unordered_map<std::string, CacheEntry> c;
                return c;
            }
        }

        bool Save(const std::string& pathIn, const CurveAsset& curve)
        {
            YAML::Emitter out;
            out << YAML::BeginMap << YAML::Key << "Curve" << YAML::Value << YAML::BeginMap;
            out << YAML::Key << "Type" << YAML::Value << TypeName(curve.Type);
            out << YAML::Key << "PreInfinity" << YAML::Value << InfinityName(curve.PreInfinity);
            out << YAML::Key << "PostInfinity" << YAML::Value << InfinityName(curve.PostInfinity);
            out << YAML::Key << "Channels" << YAML::Value << YAML::BeginSeq;
            for (const auto& ch : curve.Channels)
            {
                out << YAML::BeginMap << YAML::Key << "Name" << YAML::Value << ch.Name;
                // Anahtar: [zaman, değer, giriş teğeti, çıkış teğeti, mod]
                out << YAML::Key << "Keys" << YAML::Value << YAML::BeginSeq;
                for (const auto& k : ch.Keys)
                    out << YAML::Flow << YAML::BeginSeq << k.Time << k.Value << k.InTangent << k.OutTangent << ModeName(k.Mode) << YAML::EndSeq;
                out << YAML::EndSeq << YAML::EndMap;
            }
            out << YAML::EndSeq << YAML::EndMap << YAML::EndMap;
            const std::string path = SceneSerializer::ResolveAssetPath(pathIn);
            std::ofstream f(path, std::ios::binary | std::ios::trunc);
            if (!f)
                return false;
            f << "# Conqueror's Engine - Curve\n" << out.c_str() << "\n";
            f.close();
            Invalidate(path);
            return true;
        }

        bool Load(const std::string& pathIn, CurveAsset& out, std::string* error)
        {
            const std::string path = SceneSerializer::ResolveAssetPath(pathIn);
            try
            {
                YAML::Node root = YAML::LoadFile(path);
                YAML::Node c = root["Curve"];
                if (!c)
                {
                    if (error) *error = "not a curve file";
                    return false;
                }
                CurveAsset curve;
                curve.Type = Parse<CurveType>(c["Type"].as<std::string>("Float"), &TypeName, 3, CurveType::Float);
                curve.PreInfinity = Parse<CurveInfinity>(c["PreInfinity"].as<std::string>("Clamp"), &InfinityName, 4, CurveInfinity::Clamp);
                curve.PostInfinity = Parse<CurveInfinity>(c["PostInfinity"].as<std::string>("Clamp"), &InfinityName, 4, CurveInfinity::Clamp);
                for (const auto& chn : c["Channels"])
                {
                    CurveChannel ch;
                    ch.Name = chn["Name"].as<std::string>("");
                    for (const auto& kn : chn["Keys"])
                    {
                        if (!kn.IsSequence() || kn.size() < 2)
                            continue;
                        CurveKey k;
                        k.Time = kn[0].as<float>();
                        k.Value = kn[1].as<float>();
                        k.InTangent = kn.size() > 2 ? kn[2].as<float>() : 0.0f;
                        k.OutTangent = kn.size() > 3 ? kn[3].as<float>() : 0.0f;
                        k.Mode = kn.size() > 4 ? Parse<CurveKeyMode>(kn[4].as<std::string>(), &ModeName, 4, CurveKeyMode::Auto) : CurveKeyMode::Auto;
                        ch.Keys.push_back(k);
                    }
                    ch.Recalculate();
                    curve.Channels.push_back(std::move(ch));
                }
                if (curve.Channels.empty())
                    curve = CurveAsset::Default(curve.Type);
                out = std::move(curve);
                return true;
            }
            catch (const std::exception& e)
            {
                if (error) *error = e.what();
                return false;
            }
        }

        const CurveAsset* Get(const std::string& pathIn)
        {
            const std::string path = SceneSerializer::ResolveAssetPath(pathIn);
            std::error_code ec;
            const auto time = fs::last_write_time(path, ec);
            if (ec)
                return nullptr;
            auto& e = Cache()[path];
            if (!e.Ok || e.Time != time)
            {
                e.Time = time;
                std::string error;
                e.Ok = Load(path, e.Curve, &error);
                if (!e.Ok)
                    CQ_CORE_WARN("Curve {0}: {1}", path, error);
            }
            return e.Ok ? &e.Curve : nullptr;
        }

        void Invalidate(const std::string& path)
        {
            Cache().erase(SceneSerializer::ResolveAssetPath(path));
        }
    }
}
