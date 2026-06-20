#pragma once

#include "Core/Base/Base.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Conqueror
{
    struct AnimSceneNode
    {
        std::string Name;
        /// Assimp'in node matrisinden (aiMatrix Constructor) çıkarılan bind bileşenleri — animasyon interp ile aynı TRS sırası
        glm::vec3 BindPosition{ 0.f };
        glm::quat BindRotation{ 1.f, 0.f, 0.f, 0.f };
        glm::vec3 BindScale{ 1.f, 1.f, 1.f };
        glm::mat4 LocalBindTransform{ 1.f };
        std::vector<std::unique_ptr<AnimSceneNode>> Children;
    };

    struct SkeletonBone
    {
        std::string Name;
        int32_t ParentIndex = -1;
        glm::mat4 OffsetMatrix{ 1.f };
    };

    class CQ_API Skeleton
    {
    public:
        glm::mat4 GlobalInverseRoot{ 1.f };
        std::vector<SkeletonBone> Bones;
        std::unordered_map<std::string, uint32_t> BoneNameToIndex;
        std::unique_ptr<AnimSceneNode> RootAnimNode;

        uint32_t GetBoneCount() const { return static_cast<uint32_t>(Bones.size()); }
    };
}
