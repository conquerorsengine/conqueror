#pragma once

#include "Core/Base/Base.h"

#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace Conqueror
{
    enum class AnimParameterType
    {
        Float, Bool, Int, Trigger
    };

    enum class AnimConditionMode
    {
        Greater, Less, Equals, NotEquals
    };

    struct AnimCondition
    {
        std::string ParameterName;
        AnimConditionMode Mode = AnimConditionMode::Greater;
        float Threshold = 0.f;
    };

    struct AnimTransition
    {
        std::string FromState;
        std::string ToState;
        float Duration = 0.25f;
        float ExitTime = 0.8f;
        bool HasExitTime = true;
        std::vector<AnimCondition> Conditions;
    };

    struct AnimState
    {
        std::string Name;
        std::string ClipName;
        int ClipIndex = 0;
        float Speed = 1.f;
        bool Loop = true;
        float CycleOffset = 0.f;
        bool Mirror = false;
        bool FootIK = false;
        bool WriteDefaults = true;
        std::string MotionTimeParameter;
        glm::vec2 EditorPosition{ 0.f };
        std::vector<std::string> Behaviours;
    };

    struct AnimLayer
    {
        std::string Name = "Base Layer";
        float Weight = 1.f;
        bool Solo = false;
        bool Mute = false;
        std::string DefaultState;
        std::vector<AnimState> States;
        std::vector<AnimTransition> Transitions;
    };

    struct AnimParameter
    {
        std::string Name;
        AnimParameterType Type = AnimParameterType::Float;
        float DefaultValue = 0.f;
    };

    struct AnimSubStateData
    {
        std::string Name;
        std::vector<AnimState> States;
        std::vector<AnimTransition> Transitions;
        std::vector<AnimSubStateData> SubStates;
        std::string DefaultState;
        glm::vec2 EditorPosition = {0.f, 0.f};
        std::vector<AnimLayer> Layers;
    };

    class CQ_API AnimationController
    {
    public:
        std::vector<AnimLayer> Layers;
        std::vector<AnimParameter> Parameters;
        std::vector<AnimSubStateData> SubStates;
        int NextNodeID = 1;

        bool Serialize(const std::string& filepath) const;
        static std::shared_ptr<AnimationController> Deserialize(const std::string& filepath);

        AnimLayer* GetLayer(const std::string& name);
        AnimState* GetState(AnimLayer& layer, const std::string& name);
        AnimState* GetDefaultState(AnimLayer& layer);
    };
}
