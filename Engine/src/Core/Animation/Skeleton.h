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

    // Animasyon düğüm ağacının düz (ebeveyn önce) listesi: poz örnekleme/karıştırma için
    struct FlatAnimNode
    {
        const AnimSceneNode* Node = nullptr;
        int32_t Parent = -1;
        int32_t BoneIndex = -1;
    };

    class CQ_API Skeleton
    {
    public:
        // İlk kullanımda RootAnimNode'dan oluşturulur (Animator::Flatten)
        mutable std::vector<FlatAnimNode> FlatNodes;

        glm::mat4 GlobalInverseRoot{ 1.f };
        std::vector<SkeletonBone> Bones;
        std::unordered_map<std::string, uint32_t> BoneNameToIndex;
        std::unique_ptr<AnimSceneNode> RootAnimNode;
        // Blend shape (morph target) kanalları: modelin tüm ağlarındaki adların birleşimi. Pozun FlatNodes'tan sonraki öğeleri
        // bu kanalların ağırlıklarıdır (Translation.x): klip örnekleme, karıştırma, katmanlar ve eklemeli pozlar ağırlıkları da taşır.
        std::vector<std::string> MorphNames;

        uint32_t GetBoneCount() const { return static_cast<uint32_t>(Bones.size()); }
    };
}
