#include "AudioRandomContainer.h"

#include "Core/Serialization/SceneSerializer.h"
#include "Core/Audio/SoundCue.h"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <deque>
#include <filesystem>
#include <fstream>
#include <random>
#include <unordered_map>

namespace Conqueror::AudioRandomContainers
{
    namespace
    {
        std::mt19937& Rng()
        {
            static std::mt19937 rng(std::random_device{}());
            return rng;
        }
        float Uniform(float a, float b) { return std::uniform_real_distribution<float>(a, b)(Rng()); }

        struct OwnerState
        {
            std::string Path;
            size_t Index = 0;               // Sequential
            std::vector<size_t> Bag;        // Shuffle
            std::deque<size_t> History;     // Random: son çalınanlar
        };
        std::unordered_map<uint64_t, OwnerState>& States()
        {
            static std::unordered_map<uint64_t, OwnerState> s;
            return s;
        }
    }

    bool IsAsset(const std::string& path)
    {
        std::string e = std::filesystem::path(path).extension().string();
        std::transform(e.begin(), e.end(), e.begin(), [](unsigned char c) { return (char)std::tolower(c); });
        return e == Extension || e == SoundCues::Extension;
    }

    bool Load(const std::string& path, AudioRandomContainerAsset& a)
    {
        struct Cached { std::filesystem::file_time_type Time; AudioRandomContainerAsset Asset; };
        static std::unordered_map<std::string, Cached> cache;
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
                YAML::Node n = YAML::LoadFile(file)["AudioRandomContainer"];
                if (!n)
                    return false;
                AudioRandomContainerAsset x;
                if (auto c = n["Clips"]; c && c.IsSequence())
                    for (const auto& v : c) x.Clips.push_back(v.as<std::string>());
                if (n["Volume"]) x.VolumeDb = n["Volume"].as<float>();
                if (n["VolumeRandom"]) x.VolumeRandomDb = std::max(0.0f, n["VolumeRandom"].as<float>());
                if (n["Pitch"]) x.PitchSemitones = n["Pitch"].as<float>();
                if (n["PitchRandom"]) x.PitchRandomSemitones = std::max(0.0f, n["PitchRandom"].as<float>());
                if (n["Mode"]) x.Mode = std::clamp(n["Mode"].as<int>(), 0, 2);
                if (n["AvoidRepeat"]) x.AvoidRepeat = std::max(0, n["AvoidRepeat"].as<int>());
                if (n["Trigger"]) x.Trigger = std::clamp(n["Trigger"].as<int>(), 0, 1);
                if (n["Interval"]) x.Interval = std::max(0.01f, n["Interval"].as<float>());
                if (n["IntervalRandom"]) x.IntervalRandom = std::max(0.0f, n["IntervalRandom"].as<float>());
                if (n["Count"]) x.Count = std::max(0, n["Count"].as<int>());
                it = cache.insert_or_assign(file, Cached{ t, x }).first;
            }
            catch (...) { return false; }
        }
        a = it->second.Asset;
        return true;
    }

    bool Save(const std::string& path, const AudioRandomContainerAsset& a)
    {
        YAML::Emitter e;
        e << YAML::BeginMap << YAML::Key << "AudioRandomContainer" << YAML::Value << YAML::BeginMap;
        e << YAML::Key << "Clips" << YAML::Value << YAML::BeginSeq;
        for (const auto& c : a.Clips) e << c;
        e << YAML::EndSeq;
        e << YAML::Key << "Volume" << YAML::Value << a.VolumeDb;
        e << YAML::Key << "VolumeRandom" << YAML::Value << a.VolumeRandomDb;
        e << YAML::Key << "Pitch" << YAML::Value << a.PitchSemitones;
        e << YAML::Key << "PitchRandom" << YAML::Value << a.PitchRandomSemitones;
        e << YAML::Key << "Mode" << YAML::Value << a.Mode;
        e << YAML::Key << "AvoidRepeat" << YAML::Value << a.AvoidRepeat;
        e << YAML::Key << "Trigger" << YAML::Value << a.Trigger;
        e << YAML::Key << "Interval" << YAML::Value << a.Interval;
        e << YAML::Key << "IntervalRandom" << YAML::Value << a.IntervalRandom;
        e << YAML::Key << "Count" << YAML::Value << a.Count;
        e << YAML::EndMap << YAML::EndMap;
        std::ofstream f(SceneSerializer::ResolveAssetPath(path), std::ios::trunc);
        if (!f)
            return false;
        f << e.c_str() << "\n";
        return true;
    }

    bool Next(const std::string& path, uint64_t owner, Pick& out)
    {
        if (std::filesystem::path(path).extension() == SoundCues::Extension)
            return SoundCues::Next(path, owner, out);
        AudioRandomContainerAsset a;
        if (!Load(path, a) || a.Clips.empty())
            return false;
        OwnerState& st = States()[owner];
        if (st.Path != path)
            st = OwnerState{ path };
        const size_t n = a.Clips.size();
        size_t index = 0;
        if (a.Mode == 0)
        {
            index = st.Index % n;
            st.Index = index + 1;
        }
        else if (a.Mode == 1)
        {
            // Karıştır: her klip bir kez, sonra yeniden karıştır (bir önceki ilk olmasın)
            if (st.Bag.empty())
            {
                for (size_t i = 0; i < n; ++i) st.Bag.push_back(i);
                std::shuffle(st.Bag.begin(), st.Bag.end(), Rng());
                if (n > 1 && !st.History.empty() && st.Bag.back() == st.History.back())
                    std::swap(st.Bag.front(), st.Bag.back());
            }
            index = st.Bag.back();
            st.Bag.pop_back();
        }
        else
        {
            std::vector<size_t> allowed;
            const size_t avoid = std::min<size_t>((size_t)a.AvoidRepeat, n - 1);
            for (size_t i = 0; i < n; ++i)
            {
                bool recent = false;
                for (size_t k = 0; k < avoid && k < st.History.size(); ++k)
                    recent = recent || st.History[st.History.size() - 1 - k] == i;
                if (!recent)
                    allowed.push_back(i);
            }
            index = allowed[std::uniform_int_distribution<size_t>(0, allowed.size() - 1)(Rng())];
        }
        st.History.push_back(index);
        while (st.History.size() > 16)
            st.History.pop_front();
        out.Clip = a.Clips[index];
        const float db = a.VolumeDb + (a.VolumeRandomDb > 0.0f ? Uniform(-a.VolumeRandomDb, a.VolumeRandomDb) : 0.0f);
        const float semis = a.PitchSemitones + (a.PitchRandomSemitones > 0.0f ? Uniform(-a.PitchRandomSemitones, a.PitchRandomSemitones) : 0.0f);
        out.VolumeScale = std::pow(10.0f, db / 20.0f);
        out.PitchScale = std::pow(2.0f, semis / 12.0f);
        return true;
    }

    void Reset(uint64_t owner)
    {
        States().erase(owner);
    }

    void ResetAll()
    {
        States().clear();
    }

    float NextInterval(const AudioRandomContainerAsset& a)
    {
        return std::max(0.01f, a.Interval + (a.IntervalRandom > 0.0f ? Uniform(-a.IntervalRandom, a.IntervalRandom) : 0.0f));
    }
}
