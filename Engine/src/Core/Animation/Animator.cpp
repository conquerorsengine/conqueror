#include "Animator.h"

#include <cmath>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Conqueror
{
    namespace
    {
        /// LearnOpenGL `Bone::Update`: çeviri * dönüşüm * ölçek — Assimp anim kanallarıyla yaygın uyum.
        glm::mat4 ComposeLearnOpenGLAnimLocal(const glm::vec3& position, const glm::quat& rotation, const glm::vec3& scale)
        {
            const glm::mat4 T = glm::translate(glm::mat4(1.f), position);
            const glm::mat4 R = glm::mat4_cast(glm::normalize(rotation));
            const glm::mat4 S = glm::scale(glm::mat4(1.f), scale);
            return T * R * S;
        }

        glm::vec3 SampleVec3(const std::vector<Vec3Keyframe>& keys, float t, const glm::vec3& fallback)
        {
            if (keys.empty())
                return fallback;
            if (keys.size() == 1)
                return keys[0].Value;

            if (t <= keys.front().TimeTicks)
                return keys.front().Value;
            if (t >= keys.back().TimeTicks)
                return keys.back().Value;

            for (size_t i = 0; i + 1 < keys.size(); ++i)
            {
                if (t < keys[i + 1].TimeTicks)
                {
                    float ta = keys[i].TimeTicks;
                    float tb = keys[i + 1].TimeTicks;
                    float blend = (t - ta) / glm::max(tb - ta, 1e-6f);
                    return glm::mix(keys[i].Value, keys[i + 1].Value, blend);
                }
            }
            return keys.back().Value;
        }

        glm::quat SampleQuat(const std::vector<QuatKeyframe>& keys, float t, const glm::quat& fallback)
        {
            if (keys.empty())
                return fallback;
            if (keys.size() == 1)
                return keys[0].Value;

            if (t <= keys.front().TimeTicks)
                return keys.front().Value;
            if (t >= keys.back().TimeTicks)
                return keys.back().Value;

            for (size_t i = 0; i + 1 < keys.size(); ++i)
            {
                if (t < keys[i + 1].TimeTicks)
                {
                    float ta = keys[i].TimeTicks;
                    float tb = keys[i + 1].TimeTicks;
                    float blend = (t - ta) / glm::max(tb - ta, 1e-6f);
                    return glm::normalize(glm::slerp(keys[i].Value, keys[i + 1].Value, blend));
                }
            }
            return keys.back().Value;
        }

        glm::mat4 LocalTransformFromClip(const AnimSceneNode& node, const AnimationClip& clip, float ticks)
        {
            auto it = clip.Channels.find(node.Name);
            if (it == clip.Channels.end())
                return node.LocalBindTransform;

            const BoneAnimChannel& ch = it->second;
            if (ch.Positions.empty() && ch.Rotations.empty() && ch.Scales.empty())
                return node.LocalBindTransform;

            glm::vec3 T = SampleVec3(ch.Positions, ticks, node.BindPosition);
            glm::quat R = SampleQuat(ch.Rotations, ticks, node.BindRotation);
            glm::vec3 S = SampleVec3(ch.Scales, ticks, node.BindScale);
            return ComposeLearnOpenGLAnimLocal(T, R, S);
        }

        void VisitAnimNode(const AnimSceneNode& node, const glm::mat4& parentWorld, const Skeleton& skeleton,
            const AnimationClip& clip, float ticks, std::vector<glm::mat4>& outMatrices)
        {
            glm::mat4 local = LocalTransformFromClip(node, clip, ticks);
            glm::mat4 global = parentWorld * local;

            auto boneIt = skeleton.BoneNameToIndex.find(node.Name);
            if (boneIt != skeleton.BoneNameToIndex.end())
            {
                uint32_t idx = boneIt->second;
                if (idx < outMatrices.size())
                    outMatrices[idx] = skeleton.GlobalInverseRoot * global * skeleton.Bones[idx].OffsetMatrix;
            }

            for (const auto& child : node.Children)
                VisitAnimNode(*child, global, skeleton, clip, ticks, outMatrices);
        }

        float WrapTicks(float ticks, float duration)
        {
            if (duration <= 1e-6f)
                return 0.f;
            float t = std::fmod(ticks, duration);
            if (t < 0.f)
                t += duration;
            return t;
        }
    }

    void Animator::ComputeBoneMatrices(const Skeleton& skeleton, const AnimationClip& clip, float timeSeconds,
        std::vector<glm::mat4>& outMatrices)
    {
        const uint32_t n = skeleton.GetBoneCount();
        outMatrices.resize(n);
        if (n == 0 || !skeleton.RootAnimNode)
            return;

        for (uint32_t i = 0; i < n; ++i)
            outMatrices[i] = glm::mat4(1.f);

        float ticks = timeSeconds * clip.TicksPerSecond;
        ticks = WrapTicks(ticks, clip.DurationTicks);

        VisitAnimNode(*skeleton.RootAnimNode, glm::mat4(1.f), skeleton, clip, ticks, outMatrices);
    }
}
