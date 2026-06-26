#include "AnimationController.h"
#include "Core/Logging/Log.h"
#include "Core/Project/Project.h"

#include <yaml-cpp/yaml.h>
#include <fstream>
#include <filesystem>

namespace Conqueror
{
    static std::string ToSerializablePath(const std::string& absolutePath)
    {
        if (absolutePath.empty())
            return absolutePath;

        auto projectDir = Project::GetActiveProjectDirectory();
        if (!projectDir.empty())
        {
            std::error_code ec;
            auto relative = std::filesystem::relative(absolutePath, projectDir, ec);
            if (!ec && !relative.empty() && relative.native()[0] != '.')
                return relative.string();
        }

        return absolutePath;
    }

    static std::string ResolveSerializablePath(const std::string& path)
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
                if (std::filesystem::exists(absolute))
                    return absolute.string();
            }
        }

        return path;
    }

    static const char* AnimParameterTypeToString(AnimParameterType type)
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

    static AnimParameterType AnimParameterTypeFromString(const std::string& str)
    {
        if (str == "Bool") return AnimParameterType::Bool;
        if (str == "Int") return AnimParameterType::Int;
        if (str == "Trigger") return AnimParameterType::Trigger;
        return AnimParameterType::Float;
    }

    static const char* AnimConditionModeToString(AnimConditionMode mode)
    {
        switch (mode)
        {
            case AnimConditionMode::Greater: return "Greater";
            case AnimConditionMode::Less: return "Less";
            case AnimConditionMode::Equals: return "Equals";
            case AnimConditionMode::NotEquals: return "NotEquals";
        }
        return "Greater";
    }

    static AnimConditionMode AnimConditionModeFromString(const std::string& str)
    {
        if (str == "Less") return AnimConditionMode::Less;
        if (str == "Equals") return AnimConditionMode::Equals;
        if (str == "NotEquals") return AnimConditionMode::NotEquals;
        return AnimConditionMode::Greater;
    }

    static void SerializeSubState(YAML::Emitter& out, const AnimSubStateData& ss)
    {
        out << YAML::BeginMap;
        out << YAML::Key << "Name" << YAML::Value << ss.Name;
        out << YAML::Key << "EditorPosX" << YAML::Value << ss.EditorPosition.x;
        out << YAML::Key << "EditorPosY" << YAML::Value << ss.EditorPosition.y;
        out << YAML::Key << "DefaultState" << YAML::Value << ss.DefaultState;

        out << YAML::Key << "States" << YAML::Value << YAML::BeginSeq;
        for (const auto& state : ss.States)
        {
            out << YAML::BeginMap;
            out << YAML::Key << "Name" << YAML::Value << state.Name;
            out << YAML::Key << "ClipName" << YAML::Value << state.ClipName;
            out << YAML::Key << "ClipIndex" << YAML::Value << state.ClipIndex;
            out << YAML::Key << "Speed" << YAML::Value << state.Speed;
            out << YAML::Key << "Loop" << YAML::Value << state.Loop;
            out << YAML::Key << "EditorPosX" << YAML::Value << state.EditorPosition.x;
            out << YAML::Key << "EditorPosY" << YAML::Value << state.EditorPosition.y;
            out << YAML::EndMap;
        }
        out << YAML::EndSeq;

        out << YAML::Key << "Transitions" << YAML::Value << YAML::BeginSeq;
        for (const auto& trans : ss.Transitions)
        {
            out << YAML::BeginMap;
            out << YAML::Key << "From" << YAML::Value << trans.FromState;
            out << YAML::Key << "To" << YAML::Value << trans.ToState;
            out << YAML::Key << "Duration" << YAML::Value << trans.Duration;
            out << YAML::Key << "ExitTime" << YAML::Value << trans.ExitTime;
            out << YAML::Key << "HasExitTime" << YAML::Value << trans.HasExitTime;
            out << YAML::EndMap;
        }
        out << YAML::EndSeq;

        out << YAML::Key << "Layers" << YAML::Value << YAML::BeginSeq;
        for (const auto& layer : ss.Layers)
        {
            out << YAML::BeginMap;
            out << YAML::Key << "Name" << YAML::Value << layer.Name;
            out << YAML::Key << "Weight" << YAML::Value << layer.Weight;
            out << YAML::Key << "Solo" << YAML::Value << layer.Solo;
            out << YAML::Key << "Mute" << YAML::Value << layer.Mute;
            out << YAML::Key << "DefaultState" << YAML::Value << layer.DefaultState;

            out << YAML::Key << "States" << YAML::Value << YAML::BeginSeq;
            for (const auto& state : layer.States)
            {
                out << YAML::BeginMap;
                out << YAML::Key << "Name" << YAML::Value << state.Name;
                out << YAML::Key << "Type" << YAML::Value << (state.Type == AnimStateType::BlendTree ? "BlendTree" : "State");
                out << YAML::Key << "ClipName" << YAML::Value << state.ClipName;
                out << YAML::Key << "ClipIndex" << YAML::Value << state.ClipIndex;
                out << YAML::Key << "Speed" << YAML::Value << state.Speed;
                out << YAML::Key << "Loop" << YAML::Value << state.Loop;
                out << YAML::Key << "EditorPosX" << YAML::Value << state.EditorPosition.x;
                out << YAML::Key << "EditorPosY" << YAML::Value << state.EditorPosition.y;
                out << YAML::EndMap;
            }
            out << YAML::EndSeq;

            out << YAML::Key << "Transitions" << YAML::Value << YAML::BeginSeq;
            for (const auto& transition : layer.Transitions)
            {
                out << YAML::BeginMap;
                out << YAML::Key << "From" << YAML::Value << transition.FromState;
                out << YAML::Key << "To" << YAML::Value << transition.ToState;
                out << YAML::Key << "Duration" << YAML::Value << transition.Duration;
                out << YAML::Key << "ExitTime" << YAML::Value << transition.ExitTime;
                out << YAML::Key << "HasExitTime" << YAML::Value << transition.HasExitTime;
                out << YAML::EndMap;
            }
            out << YAML::EndSeq;

            out << YAML::EndMap;
        }
        out << YAML::EndSeq;

        out << YAML::Key << "SubStates" << YAML::Value << YAML::BeginSeq;
        for (const auto& nestedSS : ss.SubStates)
        {
            SerializeSubState(out, nestedSS);
        }
        out << YAML::EndSeq;

        out << YAML::EndMap;
    }

    static AnimSubStateData DeserializeSubState(const YAML::Node& ssNode)
    {
        AnimSubStateData ss;
        ss.Name = ssNode["Name"].as<std::string>();
        if (ssNode["EditorPosX"])
            ss.EditorPosition.x = ssNode["EditorPosX"].as<float>();
        if (ssNode["EditorPosY"])
            ss.EditorPosition.y = ssNode["EditorPosY"].as<float>();
        if (ssNode["DefaultState"])
            ss.DefaultState = ssNode["DefaultState"].as<std::string>();

        auto statesNode = ssNode["States"];
        if (statesNode)
        {
            for (auto stateNode : statesNode)
            {
                AnimState state;
                state.Name = stateNode["Name"].as<std::string>();
                if (stateNode["Type"])
                    state.Type = stateNode["Type"].as<std::string>() == "BlendTree" ? AnimStateType::BlendTree : AnimStateType::State;
                state.ClipName = stateNode["ClipName"].as<std::string>();
                state.ClipIndex = stateNode["ClipIndex"].as<int>();
                state.Speed = stateNode["Speed"].as<float>();
                state.Loop = stateNode["Loop"].as<bool>();
                if (stateNode["EditorPosX"])
                    state.EditorPosition.x = stateNode["EditorPosX"].as<float>();
                if (stateNode["EditorPosY"])
                    state.EditorPosition.y = stateNode["EditorPosY"].as<float>();
                ss.States.push_back(state);
            }
        }

        auto transNode = ssNode["Transitions"];
        if (transNode)
        {
            for (auto tNode : transNode)
            {
                AnimTransition transition;
                transition.FromState = tNode["From"].as<std::string>();
                transition.ToState = tNode["To"].as<std::string>();
                transition.Duration = tNode["Duration"].as<float>();
                transition.ExitTime = tNode["ExitTime"].as<float>();
                if (tNode["HasExitTime"])
                    transition.HasExitTime = tNode["HasExitTime"].as<bool>();
                ss.Transitions.push_back(transition);
            }
        }

        auto layersNode = ssNode["Layers"];
        if (layersNode)
        {
            for (auto layerNode : layersNode)
            {
                AnimLayer layer;
                layer.Name = layerNode["Name"].as<std::string>();
                layer.Weight = layerNode["Weight"].as<float>();
                if (layerNode["Solo"]) layer.Solo = layerNode["Solo"].as<bool>();
                if (layerNode["Mute"]) layer.Mute = layerNode["Mute"].as<bool>();
                layer.DefaultState = layerNode["DefaultState"].as<std::string>();

                auto layerStatesNode = layerNode["States"];
                if (layerStatesNode)
                {
                    for (auto stateNode : layerStatesNode)
                    {
                        AnimState state;
                        state.Name = stateNode["Name"].as<std::string>();
                        state.ClipName = stateNode["ClipName"].as<std::string>();
                        state.ClipIndex = stateNode["ClipIndex"].as<int>();
                        state.Speed = stateNode["Speed"].as<float>();
                        state.Loop = stateNode["Loop"].as<bool>();
                        if (stateNode["EditorPosX"])
                            state.EditorPosition.x = stateNode["EditorPosX"].as<float>();
                        if (stateNode["EditorPosY"])
                            state.EditorPosition.y = stateNode["EditorPosY"].as<float>();
                        layer.States.push_back(state);
                    }
                }

                auto layerTransNode = layerNode["Transitions"];
                if (layerTransNode)
                {
                    for (auto tNode : layerTransNode)
                    {
                        AnimTransition transition;
                        transition.FromState = tNode["From"].as<std::string>();
                        transition.ToState = tNode["To"].as<std::string>();
                        transition.Duration = tNode["Duration"].as<float>();
                        transition.ExitTime = tNode["ExitTime"].as<float>();
                        if (tNode["HasExitTime"])
                            transition.HasExitTime = tNode["HasExitTime"].as<bool>();
                        layer.Transitions.push_back(transition);
                    }
                }

                ss.Layers.push_back(layer);
            }
        }

        auto nestedSSNode = ssNode["SubStates"];
        if (nestedSSNode)
        {
            for (auto nssNode : nestedSSNode)
            {
                ss.SubStates.push_back(DeserializeSubState(nssNode));
            }
        }

        return ss;
    }

    bool AnimationController::Serialize(const std::string& filepath) const
    {
        YAML::Emitter out;
        out << YAML::BeginMap;
        out << YAML::Key << "AnimationController" << YAML::Value << YAML::BeginMap;

        // Layers
        out << YAML::Key << "Layers" << YAML::Value << YAML::BeginSeq;
        for (const auto& layer : Layers)
        {
            out << YAML::BeginMap;
            out << YAML::Key << "Name" << YAML::Value << layer.Name;
            out << YAML::Key << "Weight" << YAML::Value << layer.Weight;
            out << YAML::Key << "Solo" << YAML::Value << layer.Solo;
            out << YAML::Key << "Mute" << YAML::Value << layer.Mute;
            out << YAML::Key << "DefaultState" << YAML::Value << layer.DefaultState;

            // States
            out << YAML::Key << "States" << YAML::Value << YAML::BeginSeq;
            for (const auto& state : layer.States)
            {
                out << YAML::BeginMap;
                out << YAML::Key << "Name" << YAML::Value << state.Name;
                out << YAML::Key << "Type" << YAML::Value << (state.Type == AnimStateType::BlendTree ? "BlendTree" : "State");
                out << YAML::Key << "ClipName" << YAML::Value << state.ClipName;
                out << YAML::Key << "ClipIndex" << YAML::Value << state.ClipIndex;
                out << YAML::Key << "Speed" << YAML::Value << state.Speed;
                out << YAML::Key << "Loop" << YAML::Value << state.Loop;
                out << YAML::Key << "CycleOffset" << YAML::Value << state.CycleOffset;
                out << YAML::Key << "Mirror" << YAML::Value << state.Mirror;
                out << YAML::Key << "FootIK" << YAML::Value << state.FootIK;
                out << YAML::Key << "WriteDefaults" << YAML::Value << state.WriteDefaults;
                out << YAML::Key << "MotionTimeParameter" << YAML::Value << state.MotionTimeParameter;
                out << YAML::Key << "EditorPosX" << YAML::Value << state.EditorPosition.x;
                out << YAML::Key << "EditorPosY" << YAML::Value << state.EditorPosition.y;
                out << YAML::Key << "Behaviours" << YAML::Value << YAML::BeginSeq;
                for (const auto& b : state.Behaviours)
                    out << b;
                out << YAML::EndSeq;
                out << YAML::EndMap;
            }
            out << YAML::EndSeq;

            // Transitions
            out << YAML::Key << "Transitions" << YAML::Value << YAML::BeginSeq;
            for (const auto& transition : layer.Transitions)
            {
                out << YAML::BeginMap;
                out << YAML::Key << "From" << YAML::Value << transition.FromState;
                out << YAML::Key << "To" << YAML::Value << transition.ToState;
                out << YAML::Key << "Duration" << YAML::Value << transition.Duration;
                out << YAML::Key << "ExitTime" << YAML::Value << transition.ExitTime;
                out << YAML::Key << "HasExitTime" << YAML::Value << transition.HasExitTime;

                out << YAML::Key << "Conditions" << YAML::Value << YAML::BeginSeq;
                for (const auto& cond : transition.Conditions)
                {
                    out << YAML::BeginMap;
                    out << YAML::Key << "Parameter" << YAML::Value << cond.ParameterName;
                    out << YAML::Key << "Mode" << YAML::Value << AnimConditionModeToString(cond.Mode);
                    out << YAML::Key << "Threshold" << YAML::Value << cond.Threshold;
                    out << YAML::EndMap;
                }
                out << YAML::EndSeq;

                // Layer SubStates
                out << YAML::Key << "SubStates" << YAML::Value << YAML::BeginSeq;
                for (const auto& ss : layer.SubStates)
                {
                    SerializeSubState(out, ss);
                }
                out << YAML::EndSeq;

                out << YAML::EndMap;
            }
            out << YAML::EndSeq;

            out << YAML::EndMap;
        }
        out << YAML::EndSeq;

        // SubStates (root level removed, now per-layer)

        // Parameters
        out << YAML::Key << "Parameters" << YAML::Value << YAML::BeginSeq;
        for (const auto& param : Parameters)
        {
            out << YAML::BeginMap;
            out << YAML::Key << "Name" << YAML::Value << param.Name;
            out << YAML::Key << "Type" << YAML::Value << AnimParameterTypeToString(param.Type);
            out << YAML::Key << "Default" << YAML::Value << param.DefaultValue;
            out << YAML::EndMap;
        }
        out << YAML::EndSeq;

        out << YAML::Key << "NextNodeID" << YAML::Value << NextNodeID;

        out << YAML::EndMap; // AnimationController
        out << YAML::EndMap;

        std::ofstream fout(filepath);
        fout << out.c_str();
        fout.close();

        CQ_CORE_INFO("AnimationController serialized: {0}", filepath);
        return true;
    }

    std::shared_ptr<AnimationController> AnimationController::Deserialize(const std::string& filepath)
    {
        std::string resolvedPath = ResolveSerializablePath(filepath);

        YAML::Node data;
        try
        {
            data = YAML::LoadFile(resolvedPath);
        }
        catch (const YAML::ParserException& e)
        {
            CQ_CORE_ERROR("Failed to parse AnimationController: {0}", e.what());
            return nullptr;
        }

        auto acNode = data["AnimationController"];
        if (!acNode)
        {
            CQ_CORE_ERROR("Invalid AnimationController file: {0}", filepath);
            return nullptr;
        }

        auto controller = std::make_shared<AnimationController>();

        if (acNode["NextNodeID"])
            controller->NextNodeID = acNode["NextNodeID"].as<int>();

        // Layers
        auto layersNode = acNode["Layers"];
        if (layersNode)
        {
            for (auto layerNode : layersNode)
            {
                AnimLayer layer;
                layer.Name = layerNode["Name"].as<std::string>();
                layer.Weight = layerNode["Weight"].as<float>();
                if (layerNode["Solo"]) layer.Solo = layerNode["Solo"].as<bool>();
                if (layerNode["Mute"]) layer.Mute = layerNode["Mute"].as<bool>();
                layer.DefaultState = layerNode["DefaultState"].as<std::string>();

                auto statesNode = layerNode["States"];
                if (statesNode)
                {
                    for (auto stateNode : statesNode)
                    {
                        AnimState state;
                        state.Name = stateNode["Name"].as<std::string>();
                        state.ClipName = stateNode["ClipName"].as<std::string>();
                        state.ClipIndex = stateNode["ClipIndex"].as<int>();
                        state.Speed = stateNode["Speed"].as<float>();
                        state.Loop = stateNode["Loop"].as<bool>();
                        if (stateNode["CycleOffset"]) state.CycleOffset = stateNode["CycleOffset"].as<float>();
                        if (stateNode["Mirror"]) state.Mirror = stateNode["Mirror"].as<bool>();
                        if (stateNode["FootIK"]) state.FootIK = stateNode["FootIK"].as<bool>();
                        if (stateNode["WriteDefaults"]) state.WriteDefaults = stateNode["WriteDefaults"].as<bool>();
                        if (stateNode["MotionTimeParameter"]) state.MotionTimeParameter = stateNode["MotionTimeParameter"].as<std::string>();
                        if (stateNode["EditorPosX"])
                            state.EditorPosition.x = stateNode["EditorPosX"].as<float>();
                        if (stateNode["EditorPosY"])
                            state.EditorPosition.y = stateNode["EditorPosY"].as<float>();
                        auto behavioursNode = stateNode["Behaviours"];
                        if (behavioursNode)
                        {
                            for (auto bNode : behavioursNode)
                                state.Behaviours.push_back(bNode.as<std::string>());
                        }
                        layer.States.push_back(state);
                    }
                }

                auto transitionsNode = layerNode["Transitions"];
                if (transitionsNode)
                {
                    for (auto transNode : transitionsNode)
                    {
                        AnimTransition transition;
                        transition.FromState = transNode["From"].as<std::string>();
                        transition.ToState = transNode["To"].as<std::string>();
                        transition.Duration = transNode["Duration"].as<float>();
                        transition.ExitTime = transNode["ExitTime"].as<float>();
                        if (transNode["HasExitTime"])
                            transition.HasExitTime = transNode["HasExitTime"].as<bool>();

                        auto conditionsNode = transNode["Conditions"];
                        if (conditionsNode)
                        {
                            for (auto condNode : conditionsNode)
                            {
                                AnimCondition cond;
                                cond.ParameterName = condNode["Parameter"].as<std::string>();
                                cond.Mode = AnimConditionModeFromString(condNode["Mode"].as<std::string>());
                                cond.Threshold = condNode["Threshold"].as<float>();
                                transition.Conditions.push_back(cond);
                            }
                        }

                        layer.Transitions.push_back(transition);
                    }
                }

                // Layer SubStates
                auto layerSubStatesNode = layerNode["SubStates"];
                if (layerSubStatesNode)
                {
                    for (auto ssNode : layerSubStatesNode)
                    {
                        layer.SubStates.push_back(DeserializeSubState(ssNode));
                    }
                }

                controller->Layers.push_back(layer);
            }
        }

        // Parameters
        auto paramsNode = acNode["Parameters"];
        if (paramsNode)
        {
            for (auto paramNode : paramsNode)
            {
                AnimParameter param;
                param.Name = paramNode["Name"].as<std::string>();
                param.Type = AnimParameterTypeFromString(paramNode["Type"].as<std::string>());
                param.DefaultValue = paramNode["Default"].as<float>();
                controller->Parameters.push_back(param);
            }
        }

        CQ_CORE_INFO("AnimationController deserialized: {0}", filepath);
        return controller;
    }

    AnimLayer* AnimationController::GetLayer(const std::string& name)
    {
        for (auto& layer : Layers)
        {
            if (layer.Name == name)
                return &layer;
        }
        return nullptr;
    }

    AnimState* AnimationController::GetState(AnimLayer& layer, const std::string& name)
    {
        for (auto& state : layer.States)
        {
            if (state.Name == name)
                return &state;
        }
        return nullptr;
    }

    AnimState* AnimationController::GetDefaultState(AnimLayer& layer)
    {
        return GetState(layer, layer.DefaultState);
    }
}
