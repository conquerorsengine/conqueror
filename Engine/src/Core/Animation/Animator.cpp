#include "Animator.h"

#include <algorithm>
#include <cmath>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace Conqueror
{
    namespace
    {
        glm::mat4 Compose(const BonePose& p)
        {
            const glm::mat4 T = glm::translate(glm::mat4(1.f), p.Translation);
            const glm::mat4 R = glm::mat4_cast(glm::normalize(p.Rotation));
            const glm::mat4 S = glm::scale(glm::mat4(1.f), p.Scale);
            return T * R * S;
        }

        // İkili arama ile anahtar kare
        template<typename K>
        size_t FindKey(const std::vector<K>& keys, float t)
        {
            auto it = std::upper_bound(keys.begin(), keys.end(), t, [](float value, const K& k) { return value < k.TimeTicks; });
            size_t idx = (size_t)std::distance(keys.begin(), it);
            return idx == 0 ? 0 : idx - 1;
        }

        glm::vec3 SampleVec3(const std::vector<Vec3Keyframe>& keys, float t, const glm::vec3& fallback)
        {
            if (keys.empty())
                return fallback;
            if (keys.size() == 1 || t <= keys.front().TimeTicks)
                return keys.front().Value;
            if (t >= keys.back().TimeTicks)
                return keys.back().Value;
            size_t i = FindKey(keys, t);
            const auto& a = keys[i];
            const auto& b = keys[std::min(i + 1, keys.size() - 1)];
            float blend = (t - a.TimeTicks) / std::max(b.TimeTicks - a.TimeTicks, 1e-6f);
            return glm::mix(a.Value, b.Value, std::clamp(blend, 0.0f, 1.0f));
        }

        glm::quat SampleQuat(const std::vector<QuatKeyframe>& keys, float t, const glm::quat& fallback)
        {
            if (keys.empty())
                return fallback;
            if (keys.size() == 1 || t <= keys.front().TimeTicks)
                return keys.front().Value;
            if (t >= keys.back().TimeTicks)
                return keys.back().Value;
            size_t i = FindKey(keys, t);
            const auto& a = keys[i];
            const auto& b = keys[std::min(i + 1, keys.size() - 1)];
            float blend = (t - a.TimeTicks) / std::max(b.TimeTicks - a.TimeTicks, 1e-6f);
            return glm::normalize(glm::slerp(a.Value, b.Value, std::clamp(blend, 0.0f, 1.0f)));
        }

        void FlattenRecursive(const AnimSceneNode* node, int32_t parent, const Skeleton& skeleton, std::vector<FlatAnimNode>& out)
        {
            FlatAnimNode flat;
            flat.Node = node;
            flat.Parent = parent;
            auto it = skeleton.BoneNameToIndex.find(node->Name);
            flat.BoneIndex = it != skeleton.BoneNameToIndex.end() ? (int32_t)it->second : -1;
            int32_t self = (int32_t)out.size();
            out.push_back(flat);
            for (const auto& child : node->Children)
                FlattenRecursive(child.get(), self, skeleton, out);
        }
    }

    const std::vector<FlatAnimNode>& Animator::Flatten(const Skeleton& skeleton)
    {
        if (skeleton.FlatNodes.empty() && skeleton.RootAnimNode)
            FlattenRecursive(skeleton.RootAnimNode.get(), -1, skeleton, skeleton.FlatNodes);
        return skeleton.FlatNodes;
    }

    void Animator::BindPose(const Skeleton& skeleton, Pose& out)
    {
        const auto& nodes = Flatten(skeleton);
        out.resize(nodes.size());
        for (size_t i = 0; i < nodes.size(); ++i)
        {
            out[i].Translation = nodes[i].Node->BindPosition;
            out[i].Rotation = nodes[i].Node->BindRotation;
            out[i].Scale = nodes[i].Node->BindScale;
        }
    }

    void Animator::SamplePose(const Skeleton& skeleton, const AnimationClip& clip, float timeSeconds, bool loop, Pose& out)
    {
        const auto& nodes = Flatten(skeleton);
        out.resize(nodes.size());

        float ticks = timeSeconds * clip.TicksPerSecond;
        const float duration = clip.DurationTicks;
        if (duration > 1e-6f)
        {
            if (loop)
            {
                ticks = std::fmod(ticks, duration);
                if (ticks < 0.0f)
                    ticks += duration;
            }
            else
            {
                ticks = std::clamp(ticks, 0.0f, duration);
            }
        }
        else
        {
            ticks = 0.0f;
        }

        for (size_t i = 0; i < nodes.size(); ++i)
        {
            const AnimSceneNode* node = nodes[i].Node;
            auto it = clip.Channels.find(node->Name);
            if (it == clip.Channels.end())
            {
                out[i].Translation = node->BindPosition;
                out[i].Rotation = node->BindRotation;
                out[i].Scale = node->BindScale;
                continue;
            }
            const BoneAnimChannel& ch = it->second;
            out[i].Translation = SampleVec3(ch.Positions, ticks, node->BindPosition);
            out[i].Rotation = SampleQuat(ch.Rotations, ticks, node->BindRotation);
            out[i].Scale = SampleVec3(ch.Scales, ticks, node->BindScale);
        }
    }

    void Animator::BlendPoses(const Pose& a, const Pose& b, float t, Pose& out)
    {
        const size_t n = std::min(a.size(), b.size());
        t = std::clamp(t, 0.0f, 1.0f);
        if (&out != &a && &out != &b)
            out.resize(n);
        for (size_t i = 0; i < n; ++i)
        {
            BonePose r;
            r.Translation = glm::mix(a[i].Translation, b[i].Translation, t);
            r.Rotation = glm::normalize(glm::slerp(a[i].Rotation, b[i].Rotation, t));
            r.Scale = glm::mix(a[i].Scale, b[i].Scale, t);
            out[i] = r;
        }
    }

    void Animator::BeginAccumulate(size_t nodeCount, Pose& accumulator)
    {
        accumulator.assign(nodeCount, BonePose{ glm::vec3(0.0f), glm::quat(0.0f, 0.0f, 0.0f, 0.0f), glm::vec3(0.0f) });
    }

    void Animator::AccumulatePose(const Pose& pose, float weight, Pose& accumulator)
    {
        const size_t n = std::min(pose.size(), accumulator.size());
        for (size_t i = 0; i < n; ++i)
        {
            accumulator[i].Translation += pose[i].Translation * weight;
            accumulator[i].Scale += pose[i].Scale * weight;
            // Kuaterniyon işaret düzeltmesi (aynı yarıküre)
            glm::quat q = pose[i].Rotation;
            const glm::quat& acc = accumulator[i].Rotation;
            if (glm::dot(acc, q) < 0.0f)
                q = -q;
            accumulator[i].Rotation.w += q.w * weight;
            accumulator[i].Rotation.x += q.x * weight;
            accumulator[i].Rotation.y += q.y * weight;
            accumulator[i].Rotation.z += q.z * weight;
        }
    }

    void Animator::NormalizeAccumulated(Pose& accumulator, float totalWeight, const Pose& fallback)
    {
        if (totalWeight <= 1e-6f)
        {
            accumulator = fallback;
            return;
        }
        const float inv = 1.0f / totalWeight;
        for (size_t i = 0; i < accumulator.size(); ++i)
        {
            accumulator[i].Translation *= inv;
            accumulator[i].Scale *= inv;
            float len = glm::length(accumulator[i].Rotation);
            accumulator[i].Rotation = len > 1e-6f ? accumulator[i].Rotation / len
                                                  : (i < fallback.size() ? fallback[i].Rotation : glm::quat(1, 0, 0, 0));
        }
    }

    void Animator::ApplyAdditive(Pose& base, const Pose& additive, const Pose& reference, float weight)
    {
        const size_t n = std::min({ base.size(), additive.size(), reference.size() });
        for (size_t i = 0; i < n; ++i)
        {
            glm::vec3 dT = additive[i].Translation - reference[i].Translation;
            glm::quat dR = glm::normalize(additive[i].Rotation * glm::inverse(reference[i].Rotation));
            glm::vec3 dS = additive[i].Scale / glm::max(reference[i].Scale, glm::vec3(1e-4f));
            base[i].Translation += dT * weight;
            base[i].Rotation = glm::normalize(glm::slerp(glm::quat(1, 0, 0, 0), dR, weight) * base[i].Rotation);
            base[i].Scale *= glm::mix(glm::vec3(1.0f), dS, weight);
        }
    }

    void Animator::PoseToSkinning(const Skeleton& skeleton, const Pose& pose, std::vector<glm::mat4>& outMatrices)
    {
        const auto& nodes = Flatten(skeleton);
        const uint32_t boneCount = skeleton.GetBoneCount();
        outMatrices.assign(boneCount, glm::mat4(1.0f));
        if (nodes.empty() || pose.size() < nodes.size())
            return;

        static thread_local std::vector<glm::mat4> s_Global;
        s_Global.resize(nodes.size());
        for (size_t i = 0; i < nodes.size(); ++i)
        {
            glm::mat4 local = Compose(pose[i]);
            s_Global[i] = nodes[i].Parent >= 0 ? s_Global[(size_t)nodes[i].Parent] * local : local;
            int32_t bone = nodes[i].BoneIndex;
            if (bone >= 0 && (uint32_t)bone < boneCount)
                outMatrices[(size_t)bone] = skeleton.GlobalInverseRoot * s_Global[i] * skeleton.Bones[(size_t)bone].OffsetMatrix;
        }
    }

    void Animator::ComputeBoneMatrices(const Skeleton& skeleton, const AnimationClip& clip, float timeSeconds,
        std::vector<glm::mat4>& outMatrices)
    {
        static thread_local Pose s_Pose;
        SamplePose(skeleton, clip, timeSeconds, true, s_Pose);
        PoseToSkinning(skeleton, s_Pose, outMatrices);
    }
}
