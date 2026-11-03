#pragma once

#include "Core/Base/Base.h"
#include "Core/Animation/Animator.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <string>

// Unity Animation Rigging karşılığı: animasyon pozunun üstüne çalışan kemik kısıtları.
// Kısıtlar karakterin (iskeletli Model) altındaki varlıklara eklenir ve kemiklere adla başvurur; aynı türden
// birden çok kısıt (iki bacak, iki kol) ayrı alt varlıklarla kurulur. Üstlerdeki Rig bileşenleri ağırlığı çarpar.
namespace Conqueror
{
    class Scene;
    class Skeleton;

    enum class RigAxis : int { X = 0, NegX, Y, NegY, Z, NegZ };

    struct RigComponent
    {
        float Weight = 1.0f;
    };

    // Kök - orta - uç zinciri (kol / bacak) hedefe uzanır; ipucu (hint) dirseğin / dizin yönünü belirler
    struct TwoBoneIKConstraintComponent
    {
        float Weight = 1.0f;
        std::string Root, Mid, Tip;
        uint64_t Target = 0;
        uint64_t Hint = 0;
        float TargetPositionWeight = 1.0f;
        float TargetRotationWeight = 0.0f;   // uç kemik hedefin dönüşünü alsın mı
    };

    // Kemiği (baş, göz, silah) hedefe çevirir
    struct MultiAimConstraintComponent
    {
        float Weight = 1.0f;
        std::string Bone;
        uint64_t Target = 0;
        RigAxis AimAxis = RigAxis::Z;
        RigAxis UpAxis = RigAxis::Y;
        float MaxAngle = 180.0f;             // derece: dönüş sınırı
    };

    // Kökten uca istenen uzunlukta zincir (kuyruk, dokunaç, omurga) FABRIK ile hedefe uzanır
    struct ChainIKConstraintComponent
    {
        float Weight = 1.0f;
        std::string Root, Tip;
        uint64_t Target = 0;
        int MaxIterations = 15;
        float Tolerance = 0.001f;
    };

    // Kemik animasyonlu pozunu gecikmeyle izler (ikincil hareket: kuyruk, anten, saç)
    struct DampedTransformComponent
    {
        float Weight = 1.0f;
        std::string Bone;
        float DampPosition = 0.5f;           // 0 sert .. 1 çok gevşek
        float DampRotation = 0.5f;
        // Çalışma zamanı
        bool Initialized = false;
        glm::vec3 Position{ 0.0f };
        glm::quat Rotation{ 1, 0, 0, 0 };
    };

    namespace RigConstraints
    {
        // Animasyon güncellemesinin başında: kısıtları sahiplerine (iskeletli modele) göre grupla
        CQ_API void BeginFrame(Scene& scene, float dt);
        // Poz deri matrislerine çevrilmeden önce: bu modelin kısıtlarını uygula
        CQ_API void Apply(Scene& scene, uint32_t modelEntity, const Skeleton& skeleton, Pose& pose);
        CQ_API bool HasConstraints(uint32_t modelEntity);
        // Kemiğin dünya dönüşümü (son poz); bulunamazsa false
        CQ_API bool GetBoneWorldTransform(Scene& scene, uint32_t modelEntity, const std::string& bone, glm::mat4& out);
        // Kemiğin modelin kendi uzayındaki dönüşümü (son poz, IK dahil); modelin dünya dönüşümünden bağımsız
        CQ_API bool GetBoneModelTransform(Scene& scene, uint32_t modelEntity, const std::string& bone, glm::mat4& out);
    }

    void RegisterRigComponents();
}
