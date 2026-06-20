#pragma once

#include "AnimationClip.h"
#include "Skeleton.h"

#include <glm/glm.hpp>

#include <vector>

namespace Conqueror
{
    class CQ_API Animator
    {
    public:
        static void ComputeBoneMatrices(const Skeleton& skeleton, const AnimationClip& clip, float timeSeconds,
            std::vector<glm::mat4>& outMatrices);
    };
}
