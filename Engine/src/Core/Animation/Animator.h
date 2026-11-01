#pragma once

#include "AnimationClip.h"
#include "Skeleton.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <vector>

namespace Conqueror
{
    // Tek bir düğümün yerel dönüşümü
    struct BonePose
    {
        glm::vec3 Translation{ 0.0f };
        glm::quat Rotation{ 1.0f, 0.0f, 0.0f, 0.0f };
        glm::vec3 Scale{ 1.0f };
    };

    // Skeleton::FlatNodes sırasıyla düğüm pozları
    using Pose = std::vector<BonePose>;

    class CQ_API Animator
    {
    public:
        static const std::vector<FlatAnimNode>& Flatten(const Skeleton& skeleton);

        static void BindPose(const Skeleton& skeleton, Pose& out);
        // loop=false: süre sonunda son karede kalır (başa sarmaz)
        static void SamplePose(const Skeleton& skeleton, const AnimationClip& clip, float timeSeconds, bool loop, Pose& out);
        // out = lerp/slerp(a, b, t); out a veya b ile aynı olabilir
        static void BlendPoses(const Pose& a, const Pose& b, float t, Pose& out);
        // Ağırlıklı toplam (blend tree): Begin -> Add... -> Normalize
        static void BeginAccumulate(size_t nodeCount, Pose& accumulator);
        static void AccumulatePose(const Pose& pose, float weight, Pose& accumulator);
        static void NormalizeAccumulated(Pose& accumulator, float totalWeight, const Pose& fallback);
        // base += (additive - reference) * weight
        static void ApplyAdditive(Pose& base, const Pose& additive, const Pose& reference, float weight);
        // Pozdan GPU skinning matrisleri
        static void PoseToSkinning(const Skeleton& skeleton, const Pose& pose, std::vector<glm::mat4>& outMatrices);

        // Eski API: tek klip, döngülü
        static void ComputeBoneMatrices(const Skeleton& skeleton, const AnimationClip& clip, float timeSeconds,
            std::vector<glm::mat4>& outMatrices);
    };
}
