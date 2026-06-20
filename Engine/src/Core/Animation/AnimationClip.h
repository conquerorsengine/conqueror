#pragma once

#include "Core/Base/Base.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <string>
#include <unordered_map>
#include <vector>

namespace Conqueror
{
    struct Vec3Keyframe
    {
        float TimeTicks = 0.f;
        glm::vec3 Value{};
    };

    struct QuatKeyframe
    {
        float TimeTicks = 0.f;
        glm::quat Value{ 1.f, 0.f, 0.f, 0.f };
    };

    struct BoneAnimChannel
    {
        std::vector<Vec3Keyframe> Positions;
        std::vector<QuatKeyframe> Rotations;
        std::vector<Vec3Keyframe> Scales;
    };

    class CQ_API AnimationClip
    {
    public:
        std::string Name;
        float DurationTicks = 0.f;
        float TicksPerSecond = 25.f;
        std::unordered_map<std::string, BoneAnimChannel> Channels;

        float DurationSeconds() const
        {
            if (TicksPerSecond <= 1e-6f)
                return 0.f;
            return DurationTicks / TicksPerSecond;
        }
    };
}
