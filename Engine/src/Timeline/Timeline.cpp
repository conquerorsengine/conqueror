#include "Timeline.h"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cmath>
#include <fstream>

namespace Conqueror
{
    namespace
    {
        YAML::Emitter& operator<<(YAML::Emitter& out, const glm::vec4& v)
        {
            out << YAML::Flow << YAML::BeginSeq << v.x << v.y << v.z << v.w << YAML::EndSeq;
            return out;
        }

        glm::vec4 ReadVec4(const YAML::Node& n)
        {
            glm::vec4 v(0.0f);
            if (n && n.IsSequence())
                for (size_t i = 0; i < 4 && i < n.size(); ++i)
                    v[(int)i] = n[i].as<float>();
            return v;
        }

        // Hermite yumuşatma (Catmull-Rom teğetleri) — anahtarlar arası "Smooth"
        glm::vec4 Hermite(const glm::vec4& p0, const glm::vec4& p1, const glm::vec4& m0, const glm::vec4& m1, float t)
        {
            const float t2 = t * t, t3 = t2 * t;
            return (2.0f * t3 - 3.0f * t2 + 1.0f) * p0 + (t3 - 2.0f * t2 + t) * m0 + (-2.0f * t3 + 3.0f * t2) * p1 + (t3 - t2) * m1;
        }
    }

    TimelineChannel* TimelineTrack::FindChannel(const std::string& name)
    {
        for (auto& c : Channels)
            if (c.Name == name)
                return &c;
        return nullptr;
    }

    const TimelineChannel* TimelineTrack::FindChannel(const std::string& name) const
    {
        for (const auto& c : Channels)
            if (c.Name == name)
                return &c;
        return nullptr;
    }

    TimelineChannel& TimelineTrack::GetOrAddChannel(const std::string& name)
    {
        if (TimelineChannel* c = FindChannel(name))
            return *c;
        Channels.push_back({ name, {} });
        return Channels.back();
    }

    const char* TimelineAsset::TrackTypeName(TimelineTrackType type)
    {
        switch (type)
        {
            case TimelineTrackType::Activation: return "Activation";
            case TimelineTrackType::Transform: return "Transform";
            case TimelineTrackType::Property: return "Property";
            case TimelineTrackType::Audio: return "Audio";
            case TimelineTrackType::Signal: return "Signal";
            case TimelineTrackType::Animation: return "Animation";
            default: return "Track";
        }
    }

    float TimelineAsset::ContentEnd() const
    {
        float end = 0.0f;
        for (const auto& t : Tracks)
        {
            for (const auto& c : t.Clips)
                end = std::max(end, c.End());
            for (const auto& ch : t.Channels)
                if (!ch.Keys.empty())
                    end = std::max(end, ch.Keys.back().Time);
            for (const auto& m : t.Markers)
                end = std::max(end, m.Time);
        }
        return end;
    }

    bool TimelineAsset::Save(const std::filesystem::path& path, std::string* error) const
    {
        YAML::Emitter out;
        out << YAML::BeginMap;
        out << YAML::Key << "Timeline" << YAML::Value << YAML::BeginMap;
        out << YAML::Key << "Duration" << YAML::Value << Duration;
        out << YAML::Key << "FrameRate" << YAML::Value << FrameRate;
        out << YAML::Key << "Tracks" << YAML::Value << YAML::BeginSeq;
        for (const auto& t : Tracks)
        {
            out << YAML::BeginMap;
            out << YAML::Key << "Name" << YAML::Value << t.Name;
            out << YAML::Key << "Type" << YAML::Value << TrackTypeName(t.Type);
            if (t.Muted) out << YAML::Key << "Muted" << YAML::Value << true;
            if (t.Locked) out << YAML::Key << "Locked" << YAML::Value << true;
            if (t.Type == TimelineTrackType::Activation) out << YAML::Key << "PostPlayback" << YAML::Value << t.PostPlayback;
            if (t.Type == TimelineTrackType::Property)
            {
                out << YAML::Key << "Component" << YAML::Value << t.Component;
                out << YAML::Key << "Field" << YAML::Value << t.Field;
            }
            if (!t.Channels.empty())
            {
                out << YAML::Key << "Channels" << YAML::Value << YAML::BeginSeq;
                for (const auto& ch : t.Channels)
                {
                    out << YAML::BeginMap;
                    out << YAML::Key << "Name" << YAML::Value << ch.Name;
                    out << YAML::Key << "Keys" << YAML::Value << YAML::BeginSeq;
                    for (const auto& k : ch.Keys)
                    {
                        out << YAML::Flow << YAML::BeginMap;
                        out << YAML::Key << "T" << YAML::Value << k.Time;
                        out << YAML::Key << "V" << YAML::Value << k.Value;
                        if (k.Interp != KeyInterpolation::Smooth)
                            out << YAML::Key << "I" << YAML::Value << (int)k.Interp;
                        out << YAML::EndMap;
                    }
                    out << YAML::EndSeq;
                    out << YAML::EndMap;
                }
                out << YAML::EndSeq;
            }
            if (!t.Clips.empty())
            {
                out << YAML::Key << "Clips" << YAML::Value << YAML::BeginSeq;
                for (const auto& c : t.Clips)
                {
                    out << YAML::BeginMap;
                    out << YAML::Key << "Start" << YAML::Value << c.Start;
                    out << YAML::Key << "Duration" << YAML::Value << c.Duration;
                    if (!c.Name.empty()) out << YAML::Key << "Name" << YAML::Value << c.Name;
                    if (!c.Asset.empty()) out << YAML::Key << "Asset" << YAML::Value << c.Asset;
                    if (c.Volume != 1.0f) out << YAML::Key << "Volume" << YAML::Value << c.Volume;
                    if (c.Speed != 1.0f) out << YAML::Key << "Speed" << YAML::Value << c.Speed;
                    if (c.ClipIn != 0.0f) out << YAML::Key << "ClipIn" << YAML::Value << c.ClipIn;
                    if (c.AnimationIndex != 0) out << YAML::Key << "AnimationIndex" << YAML::Value << c.AnimationIndex;
                    if (!c.Loop) out << YAML::Key << "Loop" << YAML::Value << false;
                    out << YAML::EndMap;
                }
                out << YAML::EndSeq;
            }
            if (!t.Markers.empty())
            {
                out << YAML::Key << "Markers" << YAML::Value << YAML::BeginSeq;
                for (const auto& m : t.Markers)
                {
                    out << YAML::Flow << YAML::BeginMap;
                    out << YAML::Key << "Time" << YAML::Value << m.Time;
                    out << YAML::Key << "Method" << YAML::Value << m.Method;
                    if (!m.Argument.empty()) out << YAML::Key << "Argument" << YAML::Value << m.Argument;
                    if (!m.Signal.empty()) out << YAML::Key << "Signal" << YAML::Value << m.Signal;
                    out << YAML::EndMap;
                }
                out << YAML::EndSeq;
            }
            out << YAML::EndMap;
        }
        out << YAML::EndSeq;
        out << YAML::EndMap;
        out << YAML::EndMap;

        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
        std::ofstream f(path, std::ios::binary);
        if (!f.is_open())
        {
            if (error) *error = "Cannot write " + path.string();
            return false;
        }
        f << out.c_str();
        return f.good();
    }

    std::shared_ptr<TimelineAsset> TimelineAsset::Load(const std::filesystem::path& path, std::string* error)
    {
        try
        {
            YAML::Node root = YAML::LoadFile(path.string());
            YAML::Node tl = root["Timeline"];
            if (!tl || !tl.IsMap())
            {
                if (error) *error = "Not a timeline file";
                return nullptr;
            }
            auto asset = std::make_shared<TimelineAsset>();
            asset->Duration = std::max(0.01f, tl["Duration"].as<float>(5.0f));
            asset->FrameRate = std::clamp(tl["FrameRate"].as<float>(60.0f), 1.0f, 240.0f);
            if (auto tracks = tl["Tracks"]; tracks && tracks.IsSequence())
            {
                for (const auto& tn : tracks)
                {
                    TimelineTrack t;
                    t.Name = tn["Name"].as<std::string>("Track");
                    const std::string type = tn["Type"].as<std::string>("Transform");
                    for (int i = 0; i < (int)TimelineTrackType::Count; ++i)
                        if (type == TrackTypeName((TimelineTrackType)i))
                            t.Type = (TimelineTrackType)i;
                    t.Muted = tn["Muted"].as<bool>(false);
                    t.Locked = tn["Locked"].as<bool>(false);
                    t.PostPlayback = tn["PostPlayback"].as<int>(0);
                    t.Component = tn["Component"].as<std::string>("");
                    t.Field = tn["Field"].as<std::string>("");
                    if (auto chans = tn["Channels"]; chans && chans.IsSequence())
                        for (const auto& cn : chans)
                        {
                            TimelineChannel ch;
                            ch.Name = cn["Name"].as<std::string>("");
                            if (auto keys = cn["Keys"]; keys && keys.IsSequence())
                                for (const auto& kn : keys)
                                {
                                    TimelineKey k;
                                    k.Time = kn["T"].as<float>(0.0f);
                                    k.Value = ReadVec4(kn["V"]);
                                    k.Interp = (KeyInterpolation)std::clamp(kn["I"].as<int>(0), 0, 2);
                                    ch.Keys.push_back(k);
                                }
                            std::sort(ch.Keys.begin(), ch.Keys.end(), [](const TimelineKey& a, const TimelineKey& b) { return a.Time < b.Time; });
                            t.Channels.push_back(std::move(ch));
                        }
                    if (auto clips = tn["Clips"]; clips && clips.IsSequence())
                        for (const auto& cn : clips)
                        {
                            TimelineClip c;
                            c.Start = cn["Start"].as<float>(0.0f);
                            c.Duration = std::max(0.01f, cn["Duration"].as<float>(1.0f));
                            c.Name = cn["Name"].as<std::string>("");
                            c.Asset = cn["Asset"].as<std::string>("");
                            c.Volume = cn["Volume"].as<float>(1.0f);
                            c.Speed = cn["Speed"].as<float>(1.0f);
                            c.ClipIn = cn["ClipIn"].as<float>(0.0f);
                            c.AnimationIndex = cn["AnimationIndex"].as<int>(0);
                            c.Loop = cn["Loop"].as<bool>(true);
                            t.Clips.push_back(c);
                        }
                    if (auto markers = tn["Markers"]; markers && markers.IsSequence())
                        for (const auto& mn : markers)
                        {
                            TimelineMarker m;
                            m.Time = mn["Time"].as<float>(0.0f);
                            m.Method = mn["Method"].as<std::string>("");
                            m.Argument = mn["Argument"].as<std::string>("");
                            m.Signal = mn["Signal"].as<std::string>("");
                            t.Markers.push_back(m);
                        }
                    std::sort(t.Clips.begin(), t.Clips.end(), [](const TimelineClip& a, const TimelineClip& b) { return a.Start < b.Start; });
                    std::sort(t.Markers.begin(), t.Markers.end(), [](const TimelineMarker& a, const TimelineMarker& b) { return a.Time < b.Time; });
                    asset->Tracks.push_back(std::move(t));
                }
            }
            return asset;
        }
        catch (const std::exception& e)
        {
            if (error) *error = e.what();
            return nullptr;
        }
    }

    glm::vec4 EvaluateChannel(const TimelineChannel& channel, float t, const glm::vec4& fallback)
    {
        const auto& keys = channel.Keys;
        if (keys.empty())
            return fallback;
        if (t <= keys.front().Time)
            return keys.front().Value;
        if (t >= keys.back().Time)
            return keys.back().Value;
        // İkili arama: t'yi çevreleyen anahtarlar
        auto it = std::upper_bound(keys.begin(), keys.end(), t, [](float v, const TimelineKey& k) { return v < k.Time; });
        const size_t i1 = (size_t)(it - keys.begin());
        const size_t i0 = i1 - 1;
        const TimelineKey& a = keys[i0];
        const TimelineKey& b = keys[i1];
        const float span = std::max(1e-6f, b.Time - a.Time);
        const float u = (t - a.Time) / span;
        switch (a.Interp)
        {
            case KeyInterpolation::Constant: return a.Value;
            case KeyInterpolation::Linear: return glm::mix(a.Value, b.Value, u);
            default:
            {
                // Catmull-Rom teğetleri (uç anahtarlarda düz), aralık süresine göre ölçeklenir. Sabit (Constant) anahtar
                // eğriyi böler (kamera kesmesi): öbür taraftaki anahtar teğete katılmaz, kesmeden sonra savrulma olmaz
                const bool prevJoined = i0 > 0 && keys[i0 - 1].Interp != KeyInterpolation::Constant;
                const bool nextJoined = i1 + 1 < keys.size() && b.Interp != KeyInterpolation::Constant;
                const glm::vec4 prev = prevJoined ? keys[i0 - 1].Value : a.Value;
                const float prevT = prevJoined ? keys[i0 - 1].Time : a.Time - span;
                const glm::vec4 next = nextJoined ? keys[i1 + 1].Value : b.Value;
                const float nextT = nextJoined ? keys[i1 + 1].Time : b.Time + span;
                const glm::vec4 m0 = prevJoined ? (b.Value - prev) / std::max(1e-6f, b.Time - prevT) * span
                                   : (i0 > 0 ? b.Value - a.Value : glm::vec4(0.0f));
                const glm::vec4 m1 = nextJoined ? (next - a.Value) / std::max(1e-6f, nextT - a.Time) * span
                                   : (i1 + 1 < keys.size() ? b.Value - a.Value : glm::vec4(0.0f));
                return Hermite(a.Value, b.Value, m0, m1, u);
            }
        }
    }

    void SetKey(TimelineChannel& channel, float time, const glm::vec4& value, float frameRate, KeyInterpolation interp)
    {
        const float half = 0.5f / std::max(1.0f, frameRate);
        for (auto& k : channel.Keys)
            if (std::abs(k.Time - time) <= half)
            {
                k.Value = value;
                return;
            }
        TimelineKey k;
        k.Time = time;
        k.Value = value;
        k.Interp = interp;
        channel.Keys.insert(std::upper_bound(channel.Keys.begin(), channel.Keys.end(), time,
                                             [](float v, const TimelineKey& key) { return v < key.Time; }), k);
    }
}
