#pragma once

#include "Core/Base/Base.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// Avatar Mask (.cqmask, Unity Create > Avatar Mask): bir animasyon katmanının hangi kemikleri etkileyeceği.
//  - İnsan bölümleri (Humanoid): Root, Body, Head, Left / Right Arm, Left / Right Hand (parmaklar), Left / Right Leg.
//    Kemik adlarından bölüm bulunur (UE Mannequin, Mixamo, Blender, Unity adlandırmaları); yardımcı kemikler ebeveyninin
//    bölümünü alır.
//  - Transform listesi: bir modelin iskeletinden tek tek kemikler (boşsa yalnız bölümler geçerli).
// Animator Controller katmanının Mask yuvasına verilir (Unity Layer > Mask): katman yalnız işaretli kemikleri karıştırır.
namespace Conqueror
{
    enum class HumanPart : int { Root = 0, Body, Head, LeftArm, RightArm, LeftHand, RightHand, LeftLeg, RightLeg, Count };

    struct AvatarMask
    {
        bool Parts[(int)HumanPart::Count] = { true, true, true, true, true, true, true, true, true };
        std::string SourceModel;                                    // Transform listesinin alındığı model
        std::vector<std::pair<std::string, bool>> Transforms;       // kemik adı -> dahil

        bool UsesTransforms() const { return !Transforms.empty(); }
    };

    namespace AvatarMasks
    {
        inline constexpr const char* Extension = ".cqmask";

        CQ_API bool Load(const std::string& path, AvatarMask& out, std::string* error = nullptr);
        CQ_API bool Save(const std::string& path, const AvatarMask& mask);
        // Önbellekli (dosya değişince yeniden); yoksa nullptr
        CQ_API const AvatarMask* Get(const std::string& path);
        CQ_API const char* PartName(HumanPart part);

        // Kemik adından insan bölümü (bilinmiyorsa Count: ebeveyninki kullanılır)
        CQ_API HumanPart ClassifyBone(const std::string& boneName);
        // Düzleştirilmiş iskelet için düğüm başına 0/1: names[i] kemik adı, parents[i] ebeveyn dizini (-1 kök)
        CQ_API void BuildNodeMask(const AvatarMask& mask, const std::vector<std::string>& names, const std::vector<int>& parents,
                                  std::vector<uint8_t>& out);
    }
}
