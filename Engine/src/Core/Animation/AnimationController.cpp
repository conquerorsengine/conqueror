#include "AnimationController.h"
#include "Renderer/Utilities/Renderer3D/ModelLoader.h"
#include "Core/Animation/BlendSpace.h"
#include <algorithm>
#include <filesystem>
#include <unordered_map>
#include "Core/Logging/Log.h"
#include "Core/Project/Project.h"

#include <yaml-cpp/yaml.h>
#include <algorithm>
#include <filesystem>
#include <fstream>

namespace Conqueror
{
    namespace
    {
        std::string ResolveSerializablePath(const std::string& path)
        {
            if (path.empty())
                return path;
            auto projectDir = Project::GetActiveProjectDirectory();
            if (!projectDir.empty())
            {
                std::filesystem::path p(path);
                if (p.is_relative())
                {
                    auto absolute = projectDir / p;
                    std::error_code ec;
                    if (std::filesystem::exists(absolute, ec))
                        return absolute.string();
                }
            }
            return path;
        }

        const char* ParamTypeToString(AnimParameterType type)
        {
            switch (type)
            {
                case AnimParameterType::Float: return "Float";
                case AnimParameterType::Bool: return "Bool";
                case AnimParameterType::Int: return "Int";
                case AnimParameterType::Trigger: return "Trigger";
            }
            return "Float";
        }

        AnimParameterType ParamTypeFromString(const std::string& s)
        {
            if (s == "Bool") return AnimParameterType::Bool;
            if (s == "Int") return AnimParameterType::Int;
            if (s == "Trigger") return AnimParameterType::Trigger;
            return AnimParameterType::Float;
        }

        const char* ConditionModeToString(AnimConditionMode m)
        {
            switch (m)
            {
                case AnimConditionMode::Greater: return "Greater";
                case AnimConditionMode::Less: return "Less";
                case AnimConditionMode::Equals: return "Equals";
                case AnimConditionMode::NotEquals: return "NotEquals";
                case AnimConditionMode::If: return "If";
                case AnimConditionMode::IfNot: return "IfNot";
            }
            return "Greater";
        }

        AnimConditionMode ConditionModeFromString(const std::string& s)
        {
            if (s == "Less") return AnimConditionMode::Less;
            if (s == "Equals") return AnimConditionMode::Equals;
            if (s == "NotEquals") return AnimConditionMode::NotEquals;
            if (s == "If") return AnimConditionMode::If;
            if (s == "IfNot") return AnimConditionMode::IfNot;
            return AnimConditionMode::Greater;
        }

        template<typename T>
        void ReadIf(const YAML::Node& node, const char* key, T& value)
        {
            if (node && node[key])
            {
                try { value = node[key].as<T>(); }
                catch (const std::exception&) {}
            }
        }

        void EmitVec2(YAML::Emitter& out, const char* key, const glm::vec2& v)
        {
            out << YAML::Key << key << YAML::Value << YAML::Flow << YAML::BeginSeq << v.x << v.y << YAML::EndSeq;
        }

        void ReadVec2(const YAML::Node& node, const char* key, glm::vec2& v)
        {
            if (node && node[key] && node[key].IsSequence() && node[key].size() == 2)
            {
                try
                {
                    v.x = node[key][0].as<float>();
                    v.y = node[key][1].as<float>();
                }
                catch (const std::exception&) {}
            }
        }

        void SerializeTransition(YAML::Emitter& out, const AnimTransition& t)
        {
            out << YAML::BeginMap;
            out << YAML::Key << "From" << YAML::Value << t.FromState;
            out << YAML::Key << "To" << YAML::Value << t.ToState;
            if (!t.Name.empty())
                out << YAML::Key << "Name" << YAML::Value << t.Name;
            out << YAML::Key << "Duration" << YAML::Value << t.Duration;
            out << YAML::Key << "ExitTime" << YAML::Value << t.ExitTime;
            out << YAML::Key << "HasExitTime" << YAML::Value << t.HasExitTime;
            out << YAML::Key << "FixedDuration" << YAML::Value << t.FixedDuration;
            out << YAML::Key << "Offset" << YAML::Value << t.Offset;
            out << YAML::Key << "Interruption" << YAML::Value << (int)t.Interruption;
            out << YAML::Key << "OrderedInterruption" << YAML::Value << t.OrderedInterruption;
            out << YAML::Key << "CanTransitionToSelf" << YAML::Value << t.CanTransitionToSelf;
            out << YAML::Key << "Mute" << YAML::Value << t.Mute;
            out << YAML::Key << "Solo" << YAML::Value << t.Solo;
            out << YAML::Key << "Conditions" << YAML::Value << YAML::BeginSeq;
            for (const auto& c : t.Conditions)
            {
                out << YAML::BeginMap;
                out << YAML::Key << "Parameter" << YAML::Value << c.ParameterName;
                out << YAML::Key << "Mode" << YAML::Value << ConditionModeToString(c.Mode);
                out << YAML::Key << "Threshold" << YAML::Value << c.Threshold;
                out << YAML::EndMap;
            }
            out << YAML::EndSeq;
            out << YAML::EndMap;
        }

        AnimTransition DeserializeTransition(const YAML::Node& n)
        {
            AnimTransition t;
            ReadIf(n, "From", t.FromState);
            ReadIf(n, "To", t.ToState);
            ReadIf(n, "Name", t.Name);
            ReadIf(n, "Duration", t.Duration);
            ReadIf(n, "ExitTime", t.ExitTime);
            ReadIf(n, "HasExitTime", t.HasExitTime);
            ReadIf(n, "FixedDuration", t.FixedDuration);
            ReadIf(n, "Offset", t.Offset);
            int interruption = (int)t.Interruption;
            ReadIf(n, "Interruption", interruption);
            t.Interruption = (AnimInterruptionSource)std::clamp(interruption, 0, 4);
            ReadIf(n, "OrderedInterruption", t.OrderedInterruption);
            ReadIf(n, "CanTransitionToSelf", t.CanTransitionToSelf);
            ReadIf(n, "Mute", t.Mute);
            ReadIf(n, "Solo", t.Solo);
            if (auto conds = n["Conditions"])
            {
                for (auto c : conds)
                {
                    AnimCondition cond;
                    ReadIf(c, "Parameter", cond.ParameterName);
                    std::string mode = "Greater";
                    ReadIf(c, "Mode", mode);
                    cond.Mode = ConditionModeFromString(mode);
                    ReadIf(c, "Threshold", cond.Threshold);
                    t.Conditions.push_back(cond);
                }
            }
            return t;
        }

        void EmitNotifies(YAML::Emitter& out, const std::vector<AnimNotify>& notifies)
        {
            if (notifies.empty())
                return;
            out << YAML::Key << "Notifies" << YAML::Value << YAML::BeginSeq;
            for (const auto& nt : notifies)
            {
                out << YAML::Flow << YAML::BeginMap;
                out << YAML::Key << "Name" << YAML::Value << nt.Name;
                out << YAML::Key << "Time" << YAML::Value << nt.Time;
                if (nt.Duration > 0.0f)
                    out << YAML::Key << "Duration" << YAML::Value << nt.Duration;
                out << YAML::EndMap;
            }
            out << YAML::EndSeq;
        }

        void ReadNotifies(const YAML::Node& n, std::vector<AnimNotify>& out)
        {
            if (auto list = n["Notifies"])
                for (auto item : list)
                {
                    AnimNotify nt;
                    ReadIf(item, "Name", nt.Name);
                    ReadIf(item, "Time", nt.Time);
                    ReadIf(item, "Duration", nt.Duration);
                    nt.Time = std::clamp(nt.Time, 0.0f, 1.0f);
                    nt.Duration = std::max(nt.Duration, 0.0f);
                    out.push_back(nt);
                }
        }

        void SerializeState(YAML::Emitter& out, const AnimState& s)
        {
            out << YAML::BeginMap;
            out << YAML::Key << "Name" << YAML::Value << s.Name;
            out << YAML::Key << "Type" << YAML::Value << (s.Type == AnimStateType::BlendTree ? "BlendTree" : "State");
            out << YAML::Key << "ClipName" << YAML::Value << s.ClipName;
            out << YAML::Key << "ClipIndex" << YAML::Value << s.ClipIndex;
            out << YAML::Key << "Speed" << YAML::Value << s.Speed;
            out << YAML::Key << "Loop" << YAML::Value << s.Loop;
            out << YAML::Key << "CycleOffset" << YAML::Value << s.CycleOffset;
            out << YAML::Key << "Mirror" << YAML::Value << s.Mirror;
            out << YAML::Key << "FootIK" << YAML::Value << s.FootIK;
            out << YAML::Key << "WriteDefaults" << YAML::Value << s.WriteDefaults;
            out << YAML::Key << "MotionTimeParameter" << YAML::Value << s.MotionTimeParameter;
            out << YAML::Key << "SpeedParameter" << YAML::Value << s.SpeedParameter;
            out << YAML::Key << "Tag" << YAML::Value << s.Tag;
            out << YAML::Key << "EditorPosX" << YAML::Value << s.EditorPosition.x;
            out << YAML::Key << "EditorPosY" << YAML::Value << s.EditorPosition.y;
            out << YAML::Key << "Behaviours" << YAML::Value << YAML::BeginSeq;
            for (const auto& b : s.Behaviours)
                out << b;
            out << YAML::EndSeq;
            EmitNotifies(out, s.Notifies);
            if (s.Type == AnimStateType::BlendTree)
            {
                if (!s.BlendSpace.empty())
                    out << YAML::Key << "BlendSpace" << YAML::Value << s.BlendSpace;
                out << YAML::Key << "BlendType" << YAML::Value << (int)s.BlendType;
                out << YAML::Key << "BlendParameter" << YAML::Value << s.BlendParameter;
                out << YAML::Key << "BlendParameterY" << YAML::Value << s.BlendParameterY;
                out << YAML::Key << "BlendChildren" << YAML::Value << YAML::BeginSeq;
                // Blend Space'li durumda çocuklar varlıktan (dosyaya yazılmaz)
                for (const auto& c : s.BlendSpace.empty() ? s.BlendChildren : std::vector<AnimBlendChild>{})
                {
                    out << YAML::BeginMap;
                    out << YAML::Key << "ClipIndex" << YAML::Value << c.ClipIndex;
                    out << YAML::Key << "ClipName" << YAML::Value << c.ClipName;
                    out << YAML::Key << "Threshold" << YAML::Value << c.Threshold;
                    EmitVec2(out, "Position", c.Position);
                    out << YAML::Key << "TimeScale" << YAML::Value << c.TimeScale;
                    out << YAML::Key << "DirectParameter" << YAML::Value << c.DirectParameter;
                    out << YAML::Key << "Mirror" << YAML::Value << c.Mirror;
                    out << YAML::EndMap;
                }
                out << YAML::EndSeq;
            }
            out << YAML::EndMap;
        }

        AnimState DeserializeState(const YAML::Node& n)
        {
            AnimState s;
            ReadIf(n, "Name", s.Name);
            std::string type = "State";
            ReadIf(n, "Type", type);
            s.Type = type == "BlendTree" ? AnimStateType::BlendTree : AnimStateType::State;
            ReadIf(n, "ClipName", s.ClipName);
            ReadIf(n, "ClipIndex", s.ClipIndex);
            ReadIf(n, "Speed", s.Speed);
            ReadIf(n, "Loop", s.Loop);
            ReadIf(n, "CycleOffset", s.CycleOffset);
            ReadIf(n, "Mirror", s.Mirror);
            ReadIf(n, "FootIK", s.FootIK);
            ReadIf(n, "WriteDefaults", s.WriteDefaults);
            ReadIf(n, "MotionTimeParameter", s.MotionTimeParameter);
            ReadIf(n, "SpeedParameter", s.SpeedParameter);
            ReadIf(n, "Tag", s.Tag);
            ReadIf(n, "EditorPosX", s.EditorPosition.x);
            ReadIf(n, "EditorPosY", s.EditorPosition.y);
            if (auto b = n["Behaviours"])
                for (auto item : b)
                    s.Behaviours.push_back(item.as<std::string>());
            ReadNotifies(n, s.Notifies);
            int blendType = (int)s.BlendType;
            ReadIf(n, "BlendType", blendType);
            s.BlendType = (AnimBlendTreeType)std::clamp(blendType, 0, 4);
            ReadIf(n, "BlendParameter", s.BlendParameter);
            ReadIf(n, "BlendParameterY", s.BlendParameterY);
            ReadIf(n, "BlendSpace", s.BlendSpace);
            if (auto children = n["BlendChildren"])
            {
                for (auto c : children)
                {
                    AnimBlendChild child;
                    ReadIf(c, "ClipIndex", child.ClipIndex);
                    ReadIf(c, "ClipName", child.ClipName);
                    ReadIf(c, "Threshold", child.Threshold);
                    ReadVec2(c, "Position", child.Position);
                    ReadIf(c, "TimeScale", child.TimeScale);
                    ReadIf(c, "DirectParameter", child.DirectParameter);
                    ReadIf(c, "Mirror", child.Mirror);
                    s.BlendChildren.push_back(child);
                }
            }
            if (!s.BlendSpace.empty())
                AnimationController::ApplyBlendSpace(s);
            return s;
        }

        // Makine alanlarını AÇIK bir map'in içine yazar
        void SerializeMachineFields(YAML::Emitter& out, const AnimStateMachine& m)
        {
            out << YAML::Key << "DefaultState" << YAML::Value << m.DefaultState;
            EmitVec2(out, "EditorPosition", m.EditorPosition);
            EmitVec2(out, "EntryPosition", m.EntryPosition);
            EmitVec2(out, "AnyStatePosition", m.AnyStatePosition);
            EmitVec2(out, "ExitPosition", m.ExitPosition);
            EmitVec2(out, "ParentPosition", m.ParentPosition);

            out << YAML::Key << "States" << YAML::Value << YAML::BeginSeq;
            for (const auto& s : m.States)
                SerializeState(out, s);
            out << YAML::EndSeq;

            out << YAML::Key << "Transitions" << YAML::Value << YAML::BeginSeq;
            for (const auto& t : m.Transitions)
                SerializeTransition(out, t);
            out << YAML::EndSeq;

            out << YAML::Key << "SubStates" << YAML::Value << YAML::BeginSeq;
            for (const auto& sub : m.SubStates)
            {
                out << YAML::BeginMap;
                out << YAML::Key << "Name" << YAML::Value << sub.Name;
                SerializeMachineFields(out, sub);
                out << YAML::EndMap;
            }
            out << YAML::EndSeq;
        }

        void DeserializeMachineFields(const YAML::Node& n, AnimStateMachine& m, int depth = 0)
        {
            if (depth > 32)
                return;
            ReadIf(n, "DefaultState", m.DefaultState);
            ReadVec2(n, "EditorPosition", m.EditorPosition);
            // Eski biçim
            ReadIf(n, "EditorPosX", m.EditorPosition.x);
            ReadIf(n, "EditorPosY", m.EditorPosition.y);
            ReadVec2(n, "EntryPosition", m.EntryPosition);
            ReadVec2(n, "AnyStatePosition", m.AnyStatePosition);
            ReadVec2(n, "ExitPosition", m.ExitPosition);
            ReadVec2(n, "ParentPosition", m.ParentPosition);

            if (auto states = n["States"])
                for (auto s : states)
                    m.States.push_back(DeserializeState(s));

            auto addSub = [&](const YAML::Node& subNode)
            {
                AnimStateMachine sub;
                ReadIf(subNode, "Name", sub.Name);
                for (const auto& existing : m.SubStates)
                    if (existing.Name == sub.Name)
                        return; // eski hatalı dosyalarda tekrarlanan kopyalar
                DeserializeMachineFields(subNode, sub, depth + 1);
                m.SubStates.push_back(std::move(sub));
            };

            if (auto transitions = n["Transitions"])
            {
                for (auto t : transitions)
                {
                    m.Transitions.push_back(DeserializeTransition(t));
                    // Eski sürüm hatası: katman sub-state'leri geçişlerin içine yazılmıştı
                    if (auto legacySubs = t["SubStates"])
                        for (auto s : legacySubs)
                            addSub(s);
                }
            }

            if (auto subs = n["SubStates"])
                for (auto s : subs)
                    addSub(s);

            if (m.DefaultState.empty() && !m.States.empty())
                m.DefaultState = m.States.front().Name;
        }

        bool CollectNames(const AnimStateMachine& m, const std::string& name)
        {
            for (const auto& s : m.States)
                if (s.Name == name)
                    return true;
            for (const auto& sub : m.SubStates)
                if (sub.Name == name || CollectNames(sub, name))
                    return true;
            return false;
        }
    }

    std::shared_ptr<AnimationController> AnimationController::CreateDefault()
    {
        auto controller = std::make_shared<AnimationController>();
        controller->Layers.emplace_back();
        return controller;
    }

    bool AnimationController::Serialize(const std::string& filepath) const
    {
        YAML::Emitter out;
        out << YAML::BeginMap;
        out << YAML::Key << "AnimationController" << YAML::Value << YAML::BeginMap;
        out << YAML::Key << "Version" << YAML::Value << 2;

        out << YAML::Key << "Layers" << YAML::Value << YAML::BeginSeq;
        for (const auto& layer : Layers)
        {
            out << YAML::BeginMap;
            out << YAML::Key << "Name" << YAML::Value << layer.Name;
            out << YAML::Key << "Weight" << YAML::Value << layer.Weight;
            out << YAML::Key << "Solo" << YAML::Value << layer.Solo;
            out << YAML::Key << "Mute" << YAML::Value << layer.Mute;
            out << YAML::Key << "Blending" << YAML::Value << (int)layer.Blending;
            if (!layer.FromBone.empty())
                out << YAML::Key << "FromBone" << YAML::Value << layer.FromBone;
            if (!layer.AvatarMask.empty())
                out << YAML::Key << "AvatarMask" << YAML::Value << layer.AvatarMask;
            if (!layer.WeightParameter.empty())
                out << YAML::Key << "WeightParameter" << YAML::Value << layer.WeightParameter;
            SerializeMachineFields(out, layer);
            out << YAML::EndMap;
        }
        out << YAML::EndSeq;

        out << YAML::Key << "Parameters" << YAML::Value << YAML::BeginSeq;
        for (const auto& param : Parameters)
        {
            out << YAML::BeginMap;
            out << YAML::Key << "Name" << YAML::Value << param.Name;
            out << YAML::Key << "Type" << YAML::Value << ParamTypeToString(param.Type);
            out << YAML::Key << "Default" << YAML::Value << param.DefaultValue;
            out << YAML::EndMap;
        }
        out << YAML::EndSeq;

        out << YAML::Key << "NextNodeID" << YAML::Value << NextNodeID;
        out << YAML::EndMap;
        out << YAML::EndMap;

        // Önce geçici dosyaya yaz, sonra taşı (yazım sırasında çökme dosyayı bozmasın)
        std::string target = filepath;
        std::string temp = filepath + ".tmp";
        {
            std::ofstream fout(temp, std::ios::trunc);
            if (!fout)
            {
                CQ_CORE_ERROR("AnimationController: cannot write {0}", filepath);
                return false;
            }
            fout << out.c_str();
        }
        std::error_code ec;
        std::filesystem::rename(temp, target, ec);
        if (ec)
        {
            std::filesystem::copy_file(temp, target, std::filesystem::copy_options::overwrite_existing, ec);
            std::filesystem::remove(temp, ec);
        }
        CQ_CORE_INFO("AnimationController saved: {0}", filepath);
        return true;
    }

    namespace
    {
        template<typename Fn>
        void ForEachClipRef(AnimStateMachine& machine, Fn&& fn)
        {
            for (auto& st : machine.States)
            {
                fn(st.ClipName, st.ClipIndex);
                for (auto& c : st.BlendChildren)
                    fn(c.ClipName, c.ClipIndex);
            }
            for (auto& sub : machine.SubStates)
                ForEachClipRef(sub, fn);
        }
    }

    std::vector<std::string> AnimationController::CollectClipNames() const
    {
        std::vector<std::string> names;
        auto* self = const_cast<AnimationController*>(this);
        for (auto& layer : self->Layers)
            ForEachClipRef(layer, [&](std::string& name, int&) {
                if (!name.empty() && std::find(names.begin(), names.end(), name) == names.end())
                    names.push_back(name);
            });
        return names;
    }

    bool AnimationController::ApplyBlendSpace(AnimState& state)
    {
        BlendSpaceAsset asset;
        std::string error;
        if (state.BlendSpace.empty() || !BlendSpaces::Load(state.BlendSpace, asset, &error))
        {
            if (!state.BlendSpace.empty())
                CQ_CORE_WARN("Blend Space '{0}' could not be read: {1}", state.BlendSpace, error);
            return false;
        }
        state.Type = AnimStateType::BlendTree;
        state.BlendType = asset.Is2D ? AnimBlendTreeType::FreeformCartesian2D : AnimBlendTreeType::Simple1D;
        state.BlendChildren.clear();
        for (const auto& sample : asset.Samples)
        {
            AnimBlendChild child;
            child.ClipName = sample.Clip;
            child.ClipIndex = -1;
            child.Threshold = sample.Position.x;
            child.Position = sample.Position;
            state.BlendChildren.push_back(child);
        }
        // 1D: eşikler artan sırada olmalı
        if (!asset.Is2D)
            std::sort(state.BlendChildren.begin(), state.BlendChildren.end(), [](const AnimBlendChild& a, const AnimBlendChild& b) { return a.Threshold < b.Threshold; });
        return true;
    }

    void AnimationController::ResolveClips(const Model& model)
    {
        for (auto& layer : Layers)
            ForEachClipRef(layer, [&](std::string& name, int& index) {
                if (index >= 0 || name.empty())
                    return;
                for (size_t i = 0; i < model.Animations.size(); ++i)
                    if (model.Animations[i] && model.Animations[i]->Name == name)
                    {
                        index = (int)i;
                        return;
                    }
                CQ_CORE_WARN("Animator Override Controller: clip '{0}' not found in the model", name);
            });
        PendingClipResolve = false;
    }

    std::shared_ptr<AnimationController> AnimationController::Deserialize(const std::string& filepath)
    {
        std::string resolvedPath = ResolveSerializablePath(filepath);

        // Animator Override Controller: temel controller'ı yükle, klip adlarını değiştir (dizinler modelden ada göre)
        if (std::filesystem::path(resolvedPath).extension() == ".cqoverride")
        {
            YAML::Node root;
            try { root = YAML::LoadFile(resolvedPath); }
            catch (const std::exception& e)
            {
                CQ_CORE_ERROR("Failed to load Animator Override Controller '{0}': {1}", filepath, e.what());
                return nullptr;
            }
            YAML::Node o = root["AnimatorOverrideController"];
            if (!o || !o["Controller"] || o["Controller"].as<std::string>().empty())
            {
                CQ_CORE_WARN("Animator Override Controller '{0}' has no base controller", filepath);
                return nullptr;
            }
            auto base = Deserialize(o["Controller"].as<std::string>());
            if (!base)
                return nullptr;
            std::unordered_map<std::string, std::string> map;
            if (auto list = o["Overrides"]; list && list.IsSequence())
                for (const auto& item : list)
                {
                    const std::string from = item["Original"].as<std::string>(""), to = item["Override"].as<std::string>("");
                    if (!from.empty() && !to.empty())
                        map[from] = to;
                }
            for (auto& layer : base->Layers)
                ForEachClipRef(layer, [&](std::string& name, int& index) {
                    auto it = map.find(name);
                    if (it == map.end())
                        return;
                    name = it->second;
                    index = -1;
                    base->PendingClipResolve = true;
                });
            return base;
        }

        YAML::Node data;
        try
        {
            data = YAML::LoadFile(resolvedPath);
        }
        catch (const std::exception& e)
        {
            CQ_CORE_ERROR("Failed to load AnimationController '{0}': {1}", filepath, e.what());
            return nullptr;
        }

        auto acNode = data["AnimationController"];
        if (!acNode)
        {
            CQ_CORE_ERROR("Invalid AnimationController file: {0}", filepath);
            return nullptr;
        }

        auto controller = std::make_shared<AnimationController>();
        ReadIf(acNode, "NextNodeID", controller->NextNodeID);

        if (auto layers = acNode["Layers"])
        {
            for (auto layerNode : layers)
            {
                AnimLayer layer;
                ReadIf(layerNode, "Name", layer.Name);
                ReadIf(layerNode, "Weight", layer.Weight);
                ReadIf(layerNode, "Solo", layer.Solo);
                ReadIf(layerNode, "Mute", layer.Mute);
                int blending = 0;
                ReadIf(layerNode, "Blending", blending);
                layer.Blending = blending == 1 ? AnimLayerBlending::Additive : AnimLayerBlending::Override;
                ReadIf(layerNode, "FromBone", layer.FromBone);
                ReadIf(layerNode, "AvatarMask", layer.AvatarMask);
                ReadIf(layerNode, "WeightParameter", layer.WeightParameter);
                DeserializeMachineFields(layerNode, layer);
                controller->Layers.push_back(std::move(layer));
            }
        }
        if (controller->Layers.empty())
            controller->Layers.emplace_back();

        if (auto params = acNode["Parameters"])
        {
            for (auto p : params)
            {
                AnimParameter param;
                ReadIf(p, "Name", param.Name);
                std::string type = "Float";
                ReadIf(p, "Type", type);
                param.Type = ParamTypeFromString(type);
                ReadIf(p, "Default", param.DefaultValue);
                controller->Parameters.push_back(param);
            }
        }

        // Blend Space örnekleri adla gelir: dizinler ilk güncellemede modelden
        for (auto& layer : controller->Layers)
            ForEachClipRef(layer, [&](std::string& name, int& index) {
                if (index < 0 && !name.empty())
                    controller->PendingClipResolve = true;
            });

        CQ_CORE_INFO("AnimationController loaded: {0}", filepath);
        return controller;
    }

    AnimLayer* AnimationController::GetLayer(const std::string& name)
    {
        for (auto& layer : Layers)
            if (layer.Name == name)
                return &layer;
        return nullptr;
    }

    AnimState* AnimationController::GetState(AnimLayer& layer, const std::string& name)
    {
        return FindState(layer, name);
    }

    AnimState* AnimationController::GetDefaultState(AnimLayer& layer)
    {
        return FindState(layer, layer.DefaultState);
    }

    AnimState* AnimationController::FindState(AnimStateMachine& machine, const std::string& name, AnimStateMachine** owner)
    {
        for (auto& s : machine.States)
        {
            if (s.Name == name)
            {
                if (owner) *owner = &machine;
                return &s;
            }
        }
        for (auto& sub : machine.SubStates)
            if (AnimState* found = FindState(sub, name, owner))
                return found;
        return nullptr;
    }

    const AnimState* AnimationController::FindState(const AnimStateMachine& machine, const std::string& name, const AnimStateMachine** owner)
    {
        for (const auto& s : machine.States)
        {
            if (s.Name == name)
            {
                if (owner) *owner = &machine;
                return &s;
            }
        }
        for (const auto& sub : machine.SubStates)
            if (const AnimState* found = FindState(sub, name, owner))
                return found;
        return nullptr;
    }

    AnimStateMachine* AnimationController::FindSubMachine(AnimStateMachine& machine, const std::string& name, AnimStateMachine** parent)
    {
        for (auto& sub : machine.SubStates)
        {
            if (sub.Name == name)
            {
                if (parent) *parent = &machine;
                return &sub;
            }
            if (AnimStateMachine* found = FindSubMachine(sub, name, parent))
                return found;
        }
        return nullptr;
    }

    const AnimStateMachine* AnimationController::FindSubMachine(const AnimStateMachine& machine, const std::string& name, const AnimStateMachine** parent)
    {
        for (const auto& sub : machine.SubStates)
        {
            if (sub.Name == name)
            {
                if (parent) *parent = &machine;
                return &sub;
            }
            if (const AnimStateMachine* found = FindSubMachine(sub, name, parent))
                return found;
        }
        return nullptr;
    }

    bool AnimationController::IsNameUsed(const AnimStateMachine& root, const std::string& name)
    {
        if (name == AnimNodeNames::Entry || name == AnimNodeNames::AnyState || name == AnimNodeNames::Exit)
            return true;
        return CollectNames(root, name);
    }

    std::string AnimationController::MakeUniqueName(const AnimStateMachine& root, const std::string& base)
    {
        std::string b = base.empty() ? std::string("New State") : base;
        if (!IsNameUsed(root, b))
            return b;
        for (int i = 1; i < 100000; ++i)
        {
            std::string candidate = b + " " + std::to_string(i);
            if (!IsNameUsed(root, candidate))
                return candidate;
        }
        return b + " *";
    }

    void AnimationController::RenameNode(AnimStateMachine& root, const std::string& oldName, const std::string& newName)
    {
        if (root.DefaultState == oldName)
            root.DefaultState = newName;
        for (auto& t : root.Transitions)
        {
            if (t.FromState == oldName) t.FromState = newName;
            if (t.ToState == oldName) t.ToState = newName;
        }
        for (auto& s : root.States)
            if (s.Name == oldName)
                s.Name = newName;
        for (auto& sub : root.SubStates)
        {
            if (sub.Name == oldName)
                sub.Name = newName;
            RenameNode(sub, oldName, newName);
        }
    }

    namespace
    {
        void RenameParameterInMachine(AnimStateMachine& m, const std::string& oldName, const std::string& newName)
        {
            for (auto& t : m.Transitions)
                for (auto& c : t.Conditions)
                    if (c.ParameterName == oldName)
                        c.ParameterName = newName;
            for (auto& s : m.States)
            {
                if (s.BlendParameter == oldName) s.BlendParameter = newName;
                if (s.BlendParameterY == oldName) s.BlendParameterY = newName;
                if (s.MotionTimeParameter == oldName) s.MotionTimeParameter = newName;
                if (s.SpeedParameter == oldName) s.SpeedParameter = newName;
                for (auto& c : s.BlendChildren)
                    if (c.DirectParameter == oldName)
                        c.DirectParameter = newName;
            }
            for (auto& sub : m.SubStates)
                RenameParameterInMachine(sub, oldName, newName);
        }
    }

    void AnimationController::RenameParameter(const std::string& oldName, const std::string& newName)
    {
        for (auto& p : Parameters)
            if (p.Name == oldName)
                p.Name = newName;
        for (auto& layer : Layers)
            RenameParameterInMachine(layer, oldName, newName);
    }

    AnimParameter* AnimationController::FindParameter(const std::string& name)
    {
        for (auto& p : Parameters)
            if (p.Name == name)
                return &p;
        return nullptr;
    }

    // ---- Anim Montage (.cqmontage)

    bool AnimMontage::Serialize(const std::string& filepath) const
    {
        YAML::Emitter out;
        out << YAML::BeginMap;
        out << YAML::Key << "AnimMontage" << YAML::Value << YAML::BeginMap;
        out << YAML::Key << "Name" << YAML::Value << Name;
        out << YAML::Key << "ClipName" << YAML::Value << ClipName;
        out << YAML::Key << "ClipIndex" << YAML::Value << ClipIndex;
        out << YAML::Key << "Rate" << YAML::Value << Rate;
        out << YAML::Key << "BlendIn" << YAML::Value << BlendIn;
        out << YAML::Key << "BlendOut" << YAML::Value << BlendOut;
        out << YAML::Key << "FromBone" << YAML::Value << FromBone;
        out << YAML::Key << "Sections" << YAML::Value << YAML::BeginSeq;
        for (const auto& sec : Sections)
        {
            out << YAML::Flow << YAML::BeginMap;
            out << YAML::Key << "Name" << YAML::Value << sec.Name;
            out << YAML::Key << "Start" << YAML::Value << sec.Start;
            out << YAML::Key << "Next" << YAML::Value << sec.Next;
            out << YAML::EndMap;
        }
        out << YAML::EndSeq;
        EmitNotifies(out, Notifies);
        out << YAML::EndMap;
        out << YAML::EndMap;
        std::ofstream fout(filepath, std::ios::trunc);
        if (!fout)
            return false;
        fout << out.c_str();
        return (bool)fout;
    }

    std::shared_ptr<AnimMontage> AnimMontage::Load(const std::string& filepath)
    {
        YAML::Node data;
        try
        {
            data = YAML::LoadFile(ResolveSerializablePath(filepath));
        }
        catch (const std::exception& e)
        {
            CQ_CORE_ERROR("Failed to load AnimMontage '{0}': {1}", filepath, e.what());
            return nullptr;
        }
        auto n = data["AnimMontage"];
        if (!n)
            return nullptr;
        auto m = std::make_shared<AnimMontage>();
        ReadIf(n, "Name", m->Name);
        if (m->Name.empty())
            m->Name = std::filesystem::path(filepath).stem().string();
        ReadIf(n, "ClipName", m->ClipName);
        ReadIf(n, "ClipIndex", m->ClipIndex);
        ReadIf(n, "Rate", m->Rate);
        ReadIf(n, "BlendIn", m->BlendIn);
        ReadIf(n, "BlendOut", m->BlendOut);
        ReadIf(n, "FromBone", m->FromBone);
        if (auto list = n["Sections"])
            for (auto item : list)
            {
                AnimMontageSection sec;
                ReadIf(item, "Name", sec.Name);
                ReadIf(item, "Start", sec.Start);
                ReadIf(item, "Next", sec.Next);
                sec.Start = std::clamp(sec.Start, 0.0f, 1.0f);
                m->Sections.push_back(sec);
            }
        std::stable_sort(m->Sections.begin(), m->Sections.end(), [](const AnimMontageSection& a, const AnimMontageSection& b) { return a.Start < b.Start; });
        ReadNotifies(n, m->Notifies);
        return m;
    }

    const AnimMontageSection* AnimMontage::FindSection(const std::string& name) const
    {
        for (const auto& sec : Sections)
            if (sec.Name == name)
                return &sec;
        return nullptr;
    }
}
