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
        glm::vec2 EditorPosition{ 0.f };
    };

    struct AnimLayer
    {
        std::string Name = "Base Layer";
        float Weight = 1.f;
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

    class CQ_API AnimationController
    {
    public:
        std::vector<AnimLayer> Layers;
        std::vector<AnimParameter> Parameters;
        int NextNodeID = 1;

        bool Serialize(const std::string& filepath) const;
        static std::shared_ptr<AnimationController> Deserialize(const std::string& filepath);

        AnimLayer* GetLayer(const std::string& name);
        AnimState* GetState(AnimLayer& layer, const std::string& name);
        AnimState* GetDefaultState(AnimLayer& layer);
    };
}
