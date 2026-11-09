#pragma once

#include "Core/Animation/Animator.h"
#include "Core/Base/Base.h"

#include <cstdint>
#include <string>
#include <vector>

// Animasyon varlıklarını kullanan bileşenler: Pose Asset, Aim Offset, IK Rig, Retargeter, Skeleton (Unreal karşılıkları).
namespace Conqueror
{
    class Scene;
    class Entity;

    struct PoseAssetComponent
    {
        std::string Asset;            // .cqpose
        std::string PoseName;
        float Weight = 1.0f;
    };

    struct AimOffsetComponent
    {
        std::string Asset;            // .cqaimoffset
        float Yaw = 0.0f;
        float Pitch = 0.0f;
        float Weight = 1.0f;
    };

    struct IKRigComponent
    {
        std::string Rig;              // .cqikrig
        float Weight = 1.0f;

        bool Built = false;
    };

    struct RetargetPlayerComponent
    {
        std::string Retargeter;       // .cqretarget
        bool Playing = true;
        float Speed = 1.0f;
        float Weight = 1.0f;

        float Time = 0.0f;
    };

    namespace AnimationAssets
    {
        // Poz deri matrislerine çevrilmeden önce
        CQ_API void Apply(Scene& scene, uint32_t entity, const Skeleton& skeleton, Pose& pose, float dt);
        CQ_API void Update(Scene& scene);
        CQ_API std::vector<std::string> BoneNames(const std::string& modelPath);
        CQ_API std::vector<std::string> ClipNames(const std::string& modelPath);
        // Pose Asset'e o andaki pozu yaz (Inspector)
        CQ_API bool CapturePose(const std::string& assetPath, const std::string& modelPath, const std::string& clip, float time, const std::string& poseName);
        void RegisterComponents();
    }
}
