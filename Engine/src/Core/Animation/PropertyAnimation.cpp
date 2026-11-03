#include "PropertyAnimation.h"

#include "Core/Logging/Log.h"
#include "Core/Serialization/SceneSerializer.h"
#include "Scene/ComponentRegistry.h"
#include "Scene/Components.h"
#include "Scene/Entity.h"
#include "Scene/Scene.h"
#include "Scripting/ConquerorScript/CQSValue.h"

#include <IconsFontAwesome6.h>
#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <unordered_map>

namespace Conqueror
{
    namespace fs = std::filesystem;

    float PropertyClip::ContentEnd() const
    {
        float end = 0.0f;
        for (const auto& c : Curves)
            if (!c.Channel.Keys.empty())
                end = std::max(end, c.Channel.Keys.back().Time);
        for (const auto& e : Events)
            end = std::max(end, e.Time);
        return end;
    }

    AnimCurveBinding* PropertyClip::Find(const std::string& path, const std::string& component, const std::string& field)
    {
        for (auto& c : Curves)
            if (c.Path == path && c.Component == component && c.Field == field)
                return &c;
        return nullptr;
    }

    namespace PropertyClips
    {
        namespace
        {
            const char* InterpName(KeyInterpolation i)
            {
                return i == KeyInterpolation::Linear ? "Linear" : i == KeyInterpolation::Constant ? "Constant" : "Smooth";
            }
            KeyInterpolation ParseInterp(const std::string& s)
            {
                return s == "Linear" ? KeyInterpolation::Linear : s == "Constant" ? KeyInterpolation::Constant : KeyInterpolation::Smooth;
            }

            struct CacheEntry
            {
                fs::file_time_type Time{};
                PropertyClip Clip;
                bool Ok = false;
            };
            std::unordered_map<std::string, CacheEntry>& Cache()
            {
                static std::unordered_map<std::string, CacheEntry> c;
                return c;
            }

            glm::vec4 ReadRegistryField(const ComponentField& field, void* component)
            {
                void* p = field.Ptr ? field.Ptr(component) : nullptr;
                if (!p)
                    return glm::vec4(0.0f);
                switch (field.Kind)
                {
                case FieldKind::Float:
                case FieldKind::Angle: return glm::vec4(*static_cast<float*>(p), 0, 0, 0);
                case FieldKind::Int: return glm::vec4((float)*static_cast<int*>(p), 0, 0, 0);
                case FieldKind::Bool: return glm::vec4(*static_cast<bool*>(p) ? 1.0f : 0.0f, 0, 0, 0);
                case FieldKind::Vec2: return glm::vec4(*static_cast<glm::vec2*>(p), 0, 0);
                case FieldKind::Vec3:
                case FieldKind::Color3: return glm::vec4(*static_cast<glm::vec3*>(p), 0);
                case FieldKind::Vec4:
                case FieldKind::Color4: return *static_cast<glm::vec4*>(p);
                default: return glm::vec4(0.0f);
                }
            }

            bool WriteRegistryField(const ComponentField& field, void* component, const glm::vec4& v)
            {
                void* p = field.Ptr ? field.Ptr(component) : nullptr;
                if (!p)
                    return false;
                switch (field.Kind)
                {
                case FieldKind::Float:
                case FieldKind::Angle: *static_cast<float*>(p) = v.x; return true;
                case FieldKind::Int: *static_cast<int*>(p) = (int)std::lround(v.x); return true;
                case FieldKind::Bool: *static_cast<bool*>(p) = v.x >= 0.5f; return true;
                case FieldKind::Vec2: *static_cast<glm::vec2*>(p) = glm::vec2(v); return true;
                case FieldKind::Vec3:
                case FieldKind::Color3: *static_cast<glm::vec3*>(p) = glm::vec3(v); return true;
                case FieldKind::Vec4:
                case FieldKind::Color4: *static_cast<glm::vec4*>(p) = v; return true;
                default: return false;
                }
            }

            // Klip zamanı: sarma moduna göre
            float WrapTime(float t, float length, AnimWrapMode mode, bool clipLoop, bool& finished)
            {
                finished = false;
                if (length <= 1e-5f)
                    return 0.0f;
                if (mode == AnimWrapMode::Default)
                    mode = clipLoop ? AnimWrapMode::Loop : AnimWrapMode::Once;
                switch (mode)
                {
                case AnimWrapMode::Loop: return std::fmod(std::max(t, 0.0f), length);
                case AnimWrapMode::PingPong:
                {
                    const float u = std::fmod(std::max(t, 0.0f), 2.0f * length);
                    return u <= length ? u : 2.0f * length - u;
                }
                case AnimWrapMode::ClampForever: return std::clamp(t, 0.0f, length);
                default:
                    if (t >= length)
                        finished = true;
                    return std::clamp(t, 0.0f, length);
                }
            }
        }

        bool Load(const std::string& pathIn, PropertyClip& out, std::string* error)
        {
            try
            {
                YAML::Node root = YAML::LoadFile(SceneSerializer::ResolveAssetPath(pathIn));
                YAML::Node a = root["AnimationClip"];
                if (!a)
                {
                    if (error) *error = "not an animation clip";
                    return false;
                }
                PropertyClip clip;
                clip.Length = a["Length"].as<float>(1.0f);
                clip.FrameRate = a["FrameRate"].as<float>(60.0f);
                clip.Loop = a["Loop"].as<bool>(true);
                for (const auto& cn : a["Curves"])
                {
                    AnimCurveBinding b;
                    b.Path = cn["Path"].as<std::string>("");
                    b.Component = cn["Component"].as<std::string>("Transform");
                    b.Field = cn["Field"].as<std::string>("");
                    b.Channel.Name = b.Field;
                    for (const auto& kn : cn["Keys"])
                    {
                        if (!kn.IsSequence() || kn.size() < 5)
                            continue;
                        TimelineKey k;
                        k.Time = kn[0].as<float>();
                        k.Value = glm::vec4(kn[1].as<float>(), kn[2].as<float>(), kn[3].as<float>(), kn[4].as<float>());
                        k.Interp = kn.size() > 5 ? ParseInterp(kn[5].as<std::string>()) : KeyInterpolation::Smooth;
                        b.Channel.Keys.push_back(k);
                    }
                    std::sort(b.Channel.Keys.begin(), b.Channel.Keys.end(), [](const TimelineKey& x, const TimelineKey& y) { return x.Time < y.Time; });
                    clip.Curves.push_back(std::move(b));
                }
                for (const auto& en : a["Events"])
                {
                    AnimEventDef e;
                    e.Time = en["Time"].as<float>(0.0f);
                    e.Function = en["Function"].as<std::string>("");
                    e.Argument = en["Argument"].as<std::string>("");
                    clip.Events.push_back(e);
                }
                out = std::move(clip);
                return true;
            }
            catch (const std::exception& e)
            {
                if (error) *error = e.what();
                return false;
            }
        }

        bool Save(const std::string& pathIn, const PropertyClip& clip)
        {
            YAML::Emitter out;
            out << YAML::BeginMap << YAML::Key << "AnimationClip" << YAML::Value << YAML::BeginMap;
            out << YAML::Key << "Length" << YAML::Value << clip.Length;
            out << YAML::Key << "FrameRate" << YAML::Value << clip.FrameRate;
            out << YAML::Key << "Loop" << YAML::Value << clip.Loop;
            out << YAML::Key << "Curves" << YAML::Value << YAML::BeginSeq;
            for (const auto& c : clip.Curves)
            {
                out << YAML::BeginMap;
                out << YAML::Key << "Path" << YAML::Value << c.Path;
                out << YAML::Key << "Component" << YAML::Value << c.Component;
                out << YAML::Key << "Field" << YAML::Value << c.Field;
                // Anahtar: [zaman, x, y, z, w, ara değerleme]
                out << YAML::Key << "Keys" << YAML::Value << YAML::BeginSeq;
                for (const auto& k : c.Channel.Keys)
                    out << YAML::Flow << YAML::BeginSeq << k.Time << k.Value.x << k.Value.y << k.Value.z << k.Value.w << InterpName(k.Interp)
                        << YAML::EndSeq;
                out << YAML::EndSeq << YAML::EndMap;
            }
            out << YAML::EndSeq;
            out << YAML::Key << "Events" << YAML::Value << YAML::BeginSeq;
            for (const auto& e : clip.Events)
                out << YAML::Flow << YAML::BeginMap << YAML::Key << "Time" << YAML::Value << e.Time << YAML::Key << "Function" << YAML::Value
                    << e.Function << YAML::Key << "Argument" << YAML::Value << e.Argument << YAML::EndMap;
            out << YAML::EndSeq << YAML::EndMap << YAML::EndMap;
            const std::string path = SceneSerializer::ResolveAssetPath(pathIn);
            std::ofstream f(path, std::ios::binary | std::ios::trunc);
            if (!f)
                return false;
            f << "# Conqueror's Engine - Animation Clip\n" << out.c_str() << "\n";
            f.close();
            Cache().erase(path);
            return true;
        }

        const PropertyClip* Get(const std::string& pathIn)
        {
            if (pathIn.empty())
                return nullptr;
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
                e.Ok = Load(path, e.Clip, &error);
                if (!e.Ok)
                    CQ_CORE_WARN("Animation Clip {0}: {1}", path, error);
            }
            return e.Ok ? &e.Clip : nullptr;
        }

        void Invalidate(const std::string& path)
        {
            Cache().erase(SceneSerializer::ResolveAssetPath(path));
        }

        Entity ResolvePath(Entity root, const std::string& path)
        {
            if (!root || path.empty())
                return root;
            Entity current = root;
            size_t start = 0;
            while (current && start <= path.size())
            {
                const size_t slash = path.find('/', start);
                const std::string name = path.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
                Entity found{};
                for (Entity child : current.GetChildren())
                    if (child.HasComponent<TagComponent>() && child.GetComponent<TagComponent>().Tag == name)
                    {
                        found = child;
                        break;
                    }
                current = found;
                if (slash == std::string::npos)
                    break;
                start = slash + 1;
            }
            return current;
        }

        std::string PathOf(Entity root, Entity target)
        {
            std::string path;
            for (Entity e = target; e && e != root; e = e.GetParent())
            {
                const std::string name = e.HasComponent<TagComponent>() ? e.GetComponent<TagComponent>().Tag : std::string();
                path = path.empty() ? name : name + "/" + path;
                if (!e.GetParent())
                    break;
            }
            return path;
        }

        bool ReadValue(Entity target, const std::string& component, const std::string& field, glm::vec4& out)
        {
            if (!target)
                return false;
            if (component == "Transform")
            {
                const auto& tc = target.GetComponent<TransformComponent>();
                if (field == "Position") out = glm::vec4(tc.Position, 0.0f);
                else if (field == "Rotation") out = glm::vec4(glm::degrees(tc.Rotation), 0.0f);
                else if (field == "Scale") out = glm::vec4(tc.Scale, 0.0f);
                else return false;
                return true;
            }
            ComponentInfo* info = ComponentRegistry::Find(component);
            if (!info || !info->Has || !info->Has(target))
                return false;
            const ComponentField* f = ComponentRegistry::FindField(*info, field);
            if (!f)
                return false;
            out = ReadRegistryField(*f, info->Get(target));
            return true;
        }

        bool WriteValue(Entity target, const std::string& component, const std::string& field, const glm::vec4& v)
        {
            if (!target)
                return false;
            if (component == "Transform")
            {
                auto& tc = target.GetComponent<TransformComponent>();
                if (field == "Position") tc.Position = glm::vec3(v);
                else if (field == "Rotation") tc.Rotation = glm::radians(glm::vec3(v));
                else if (field == "Scale") tc.Scale = glm::vec3(v);
                else return false;
                return true;
            }
            ComponentInfo* info = ComponentRegistry::Find(component);
            if (!info || !info->Has || !info->Has(target))
                return false;
            const ComponentField* f = ComponentRegistry::FindField(*info, field);
            return f && WriteRegistryField(*f, info->Get(target), v);
        }

        void Apply(Entity root, const PropertyClip& clip, float time)
        {
            for (const auto& c : clip.Curves)
            {
                if (c.Channel.Keys.empty())
                    continue;
                Entity target = ResolvePath(root, c.Path);
                if (!target)
                    continue;
                glm::vec4 current(0.0f);
                ReadValue(target, c.Component, c.Field, current);
                WriteValue(target, c.Component, c.Field, EvaluateChannel(c.Channel, time, current));
            }
        }

        void FireEvents(Scene& scene, Entity root, const PropertyClip& clip, float from, float to)
        {
            for (const auto& e : clip.Events)
            {
                if (e.Function.empty() || !(e.Time > from && e.Time <= to))
                    continue;
                if (e.Argument.empty())
                    scene.InvokeScriptMethod((uint32_t)(entt::entity)root, e.Function.c_str(), nullptr, 0);
                else
                {
                    CQS::Value arg = CQS::Value::MakeString(e.Argument);
                    scene.InvokeScriptMethod((uint32_t)(entt::entity)root, e.Function.c_str(), &arg, 1);
                }
            }
        }

        std::string ResolveClip(const AnimationPlayerComponent& a, const std::string& nameOrPath)
        {
            if (nameOrPath.empty())
                return a.Clip.empty() && !a.Clips.empty() ? a.Clips.front() : a.Clip;
            // Bileşenin kliplerinden biri: tam yol ya da dosya adı (uzantısız)
            auto matches = [&](const std::string& p) {
                return !p.empty() && (p == nameOrPath || fs::path(p).stem().string() == nameOrPath || fs::path(p).filename().string() == nameOrPath);
            };
            if (matches(a.Clip))
                return a.Clip;
            for (const auto& c : a.Clips)
                if (matches(c))
                    return c;
            return nameOrPath;   // proje içindeki başka bir klip dosyası
        }

        bool Play(Entity entity, const std::string& clip)
        {
            if (!entity || !entity.HasComponent<AnimationPlayerComponent>())
                return false;
            auto& a = entity.GetComponent<AnimationPlayerComponent>();
            a.PlayingClip = ResolveClip(a, clip);
            a.Time = 0.0f;
            a.Playing = !a.PlayingClip.empty();
            a.Started = true;
            return a.Playing;
        }

        void Stop(Entity entity)
        {
            if (entity && entity.HasComponent<AnimationPlayerComponent>())
                entity.GetComponent<AnimationPlayerComponent>().Playing = false;
        }

        void Update(Scene& scene, float dt)
        {
            auto view = scene.m_Registry.view<AnimationPlayerComponent>();
            for (auto handle : view)
            {
                Entity e{ handle, &scene };
                auto& a = view.get<AnimationPlayerComponent>(handle);
                if (!a.Started)
                {
                    a.Started = true;
                    const std::string first = ResolveClip(a, std::string());
                    if (a.PlayAutomatically && !first.empty())
                    {
                        a.PlayingClip = first;
                        a.Time = 0.0f;
                        a.Playing = true;
                    }
                }
                if (!a.Playing)
                    continue;
                const PropertyClip* clip = Get(a.PlayingClip);
                if (!clip)
                    continue;
                const float length = std::max(clip->Length, 1e-4f);
                const float before = a.Time;
                a.Time += dt * a.Speed;
                bool finished = false;
                const float prevLocal = WrapTime(before, length, a.WrapMode, clip->Loop, finished);
                const float local = WrapTime(a.Time, length, a.WrapMode, clip->Loop, finished);
                Apply(e, *clip, local);
                // Olaylar: döngüde sona geçildiyse iki parça
                if (local >= prevLocal)
                    FireEvents(scene, e, *clip, before <= 0.0f ? -1e-4f : prevLocal, local);
                else
                {
                    FireEvents(scene, e, *clip, prevLocal, length);
                    FireEvents(scene, e, *clip, -1e-4f, local);
                }
                if (finished)
                    a.Playing = false;
            }
        }

        void RegisterComponent()
        {
            ComponentDef<AnimationPlayerComponent>("Animation", "AnimationPlayerComponent", "Animation", ICON_FA_FILM)
                .Color({ 0.55f, 0.8f, 0.95f, 1.0f })
                .Describe("Plays an Animation Clip (.cqanim) on this object and its children (Unity Animation component): "
                          "keyed positions, rotations, scales and component fields, with Animation Events. Edit clips in Window > Animation.")
                .Field("Clip", "Clip", &AnimationPlayerComponent::Clip, FieldKind::Asset).Filter(".cqanim")
                .Field("PlayAutomatically", "Play Automatically", &AnimationPlayerComponent::PlayAutomatically)
                .Field("WrapMode", "Wrap Mode", &AnimationPlayerComponent::WrapMode).Options({ "Default", "Once", "Loop", "Ping Pong", "Clamp Forever" })
                .Field("Speed", "Speed", &AnimationPlayerComponent::Speed).Range(-10.0f, 10.0f, 0.01f)
                .CustomData(
                    [](YAML::Emitter& out, AnimationPlayerComponent& a) {
                        out << YAML::Key << "Clips" << YAML::Value << YAML::BeginSeq;
                        for (const auto& c : a.Clips)
                            out << SceneSerializer::ToAssetPath(c);
                        out << YAML::EndSeq;
                    },
                    [](const YAML::Node& n, AnimationPlayerComponent& a) {
                        a.Clips.clear();
                        if (auto list = n["Clips"]; list && list.IsSequence())
                            for (const auto& c : list)
                                a.Clips.push_back(c.as<std::string>());
                    });
        }
    }
}
