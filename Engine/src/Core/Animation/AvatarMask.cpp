#include "AvatarMask.h"

#include "Core/Logging/Log.h"
#include "Core/Serialization/SceneSerializer.h"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <unordered_map>

namespace Conqueror::AvatarMasks
{
    namespace fs = std::filesystem;

    const char* PartName(HumanPart part)
    {
        static const char* names[] = { "Root", "Body", "Head", "LeftArm", "RightArm", "LeftHand", "RightHand", "LeftLeg", "RightLeg" };
        const int i = (int)part;
        return i >= 0 && i < (int)HumanPart::Count ? names[i] : "?";
    }

    namespace
    {
        std::string Lower(std::string s)
        {
            for (char& c : s)
                c = (char)std::tolower((unsigned char)c);
            return s;
        }

        bool Has(const std::string& s, const char* word) { return s.find(word) != std::string::npos; }

        // Taraf: 1 sol, 2 sağ, 0 yok. "upperarm_l", "LeftArm", "mixamorig:LeftHand", "hand.L", "L_Hand", "thigh l"
        int SideOf(const std::string& raw)
        {
            std::string s = Lower(raw);
            if (const size_t colon = s.rfind(':'); colon != std::string::npos)
                s = s.substr(colon + 1);
            if (Has(s, "left"))
                return 1;
            if (Has(s, "right"))
                return 2;
            auto endsWith = [&](const char* suf) {
                const size_t n = std::strlen(suf);
                return s.size() >= n && s.compare(s.size() - n, n, suf) == 0;
            };
            auto startsWith = [&](const char* pre) { return s.rfind(pre, 0) == 0; };
            // Sonda "_l_" gibi parça sayısı da olabilir: "thumb_01_l", "index_metacarpal_l"
            for (const char* suf : { "_l", ".l", " l", "-l" })
                if (endsWith(suf))
                    return 1;
            for (const char* suf : { "_r", ".r", " r", "-r" })
                if (endsWith(suf))
                    return 2;
            for (const char* pre : { "l_", "l.", "l " })
                if (startsWith(pre))
                    return 1;
            for (const char* pre : { "r_", "r.", "r " })
                if (startsWith(pre))
                    return 2;
            if (Has(s, "_l_") || Has(s, ".l."))
                return 1;
            if (Has(s, "_r_") || Has(s, ".r."))
                return 2;
            return 0;
        }

        struct CacheEntry
        {
            fs::file_time_type Time{};
            AvatarMask Mask;
            bool Ok = false;
        };
        std::unordered_map<std::string, CacheEntry>& Cache()
        {
            static std::unordered_map<std::string, CacheEntry> c;
            return c;
        }
    }

    HumanPart ClassifyBone(const std::string& raw)
    {
        std::string s = Lower(raw);
        if (const size_t colon = s.rfind(':'); colon != std::string::npos)
            s = s.substr(colon + 1);
        const int side = SideOf(raw);
        const bool finger = Has(s, "thumb") || Has(s, "index") || Has(s, "middle") || Has(s, "ring") || Has(s, "pinky") ||
                            Has(s, "little") || Has(s, "finger") || Has(s, "metacarpal");
        const bool hand = finger || Has(s, "hand") || Has(s, "palm") || Has(s, "wrist");
        const bool arm = Has(s, "clavicle") || Has(s, "shoulder") || Has(s, "upperarm") || Has(s, "upper_arm") || Has(s, "lowerarm") ||
                         Has(s, "lower_arm") || Has(s, "forearm") || Has(s, "elbow") || Has(s, "arm");
        const bool leg = Has(s, "thigh") || Has(s, "calf") || Has(s, "upleg") || Has(s, "leg") || Has(s, "knee") || Has(s, "shin") ||
                         Has(s, "foot") || Has(s, "toe") || Has(s, "ball") || Has(s, "ankle");
        if (side != 0)
        {
            if (hand)
                return side == 1 ? HumanPart::LeftHand : HumanPart::RightHand;
            if (arm)
                return side == 1 ? HumanPart::LeftArm : HumanPart::RightArm;
            if (leg)
                return side == 1 ? HumanPart::LeftLeg : HumanPart::RightLeg;
        }
        if (Has(s, "head") || Has(s, "neck") || Has(s, "jaw") || Has(s, "eye") || Has(s, "skull"))
            return HumanPart::Head;
        if (Has(s, "pelvis") || Has(s, "hips") || Has(s, "hip") || Has(s, "spine") || Has(s, "chest") || Has(s, "torso") || Has(s, "abdomen"))
            return HumanPart::Body;
        if (s == "root" || Has(s, "armature") || Has(s, "root"))
            return HumanPart::Root;
        return HumanPart::Count;
    }

    void BuildNodeMask(const AvatarMask& mask, const std::vector<std::string>& names, const std::vector<int>& parents, std::vector<uint8_t>& out)
    {
        const size_t n = names.size();
        out.assign(n, 1);
        if (mask.UsesTransforms())
        {
            // Açık kemik listesi: listede olmayan kemik ebeveyninin durumunu alır
            std::unordered_map<std::string, bool> explicitState(mask.Transforms.begin(), mask.Transforms.end());
            for (size_t i = 0; i < n; ++i)
            {
                auto it = explicitState.find(names[i]);
                if (it != explicitState.end())
                    out[i] = it->second ? 1 : 0;
                else
                    out[i] = parents[i] >= 0 && (size_t)parents[i] < i ? out[(size_t)parents[i]] : 1;
            }
            return;
        }
        // İnsan bölümleri: sınıflandırılamayan kemik ebeveyninin bölümünü alır (bükülme / IK yardımcıları)
        std::vector<int> part(n, (int)HumanPart::Root);
        for (size_t i = 0; i < n; ++i)
        {
            HumanPart p = ClassifyBone(names[i]);
            if (p == HumanPart::Count)
                p = parents[i] >= 0 && (size_t)parents[i] < i ? (HumanPart)part[(size_t)parents[i]] : HumanPart::Root;
            part[i] = (int)p;
            out[i] = mask.Parts[(int)p] ? 1 : 0;
        }
    }

    bool Load(const std::string& pathIn, AvatarMask& out, std::string* error)
    {
        try
        {
            YAML::Node root = YAML::LoadFile(SceneSerializer::ResolveAssetPath(pathIn));
            YAML::Node m = root["AvatarMask"];
            if (!m)
            {
                if (error) *error = "not an avatar mask";
                return false;
            }
            AvatarMask mask;
            if (auto parts = m["Humanoid"]; parts && parts.IsMap())
                for (int i = 0; i < (int)HumanPart::Count; ++i)
                    if (auto v = parts[PartName((HumanPart)i)])
                        mask.Parts[i] = v.as<bool>();
            mask.SourceModel = m["SourceModel"].as<std::string>("");
            if (auto tr = m["Transforms"]; tr && tr.IsSequence())
                for (const auto& t : tr)
                    if (t.IsSequence() && t.size() >= 2)
                        mask.Transforms.push_back({ t[0].as<std::string>(), t[1].as<bool>() });
            out = std::move(mask);
            return true;
        }
        catch (const std::exception& e)
        {
            if (error) *error = e.what();
            return false;
        }
    }

    bool Save(const std::string& pathIn, const AvatarMask& mask)
    {
        YAML::Emitter out;
        out << YAML::BeginMap << YAML::Key << "AvatarMask" << YAML::Value << YAML::BeginMap;
        out << YAML::Key << "Humanoid" << YAML::Value << YAML::BeginMap;
        for (int i = 0; i < (int)HumanPart::Count; ++i)
            out << YAML::Key << PartName((HumanPart)i) << YAML::Value << mask.Parts[i];
        out << YAML::EndMap;
        out << YAML::Key << "SourceModel" << YAML::Value << mask.SourceModel;
        out << YAML::Key << "Transforms" << YAML::Value << YAML::BeginSeq;
        for (const auto& [name, on] : mask.Transforms)
            out << YAML::Flow << YAML::BeginSeq << name << on << YAML::EndSeq;
        out << YAML::EndSeq << YAML::EndMap << YAML::EndMap;
        const std::string path = SceneSerializer::ResolveAssetPath(pathIn);
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        if (!f)
            return false;
        f << "# Conqueror's Engine - Avatar Mask\n" << out.c_str() << "\n";
        f.close();
        Cache().erase(path);
        return true;
    }

    const AvatarMask* Get(const std::string& pathIn)
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
            e.Ok = Load(path, e.Mask, &error);
            if (!e.Ok)
                CQ_CORE_WARN("Avatar Mask {0}: {1}", path, error);
        }
        return e.Ok ? &e.Mask : nullptr;
    }
}
