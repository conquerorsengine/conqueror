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

    // Kemik, kaynak nesnelerin ağırlıklı dönüşümünü ebeveyni gibi izler (Unity Multi-Parent Constraint): silah eldeyken iki el, binen
    // karakter. Maintain Offset: ilk karedeki göreli duruş korunur
    struct MultiParentConstraintComponent
    {
        float Weight = 1.0f;
        std::string Bone;
        uint64_t Source1 = 0, Source2 = 0, Source3 = 0;
        float SourceWeight1 = 1.0f, SourceWeight2 = 1.0f, SourceWeight3 = 1.0f;
        bool MaintainOffset = true;
        bool ConstrainPosition = true, ConstrainRotation = true;
        // Çalışma zamanı
        bool OffsetsCaptured = false;
        glm::mat4 Offsets[3] = { glm::mat4(1.0f), glm::mat4(1.0f), glm::mat4(1.0f) };
    };

    // Kemiğin konumu kaynakların ağırlıklı ortalaması (Unity Multi-Position Constraint), eksen eksen
    struct MultiPositionConstraintComponent
    {
        float Weight = 1.0f;
        std::string Bone;
        uint64_t Source1 = 0, Source2 = 0, Source3 = 0;
        float SourceWeight1 = 1.0f, SourceWeight2 = 1.0f, SourceWeight3 = 1.0f;
        bool MaintainOffset = false;
        glm::vec3 Offset{ 0.0f };
        bool ConstrainX = true, ConstrainY = true, ConstrainZ = true;
        bool OffsetCaptured = false;
        glm::vec3 CapturedOffset{ 0.0f };
    };

    // Kemiğin dönüşü kaynakların ağırlıklı ortalaması (Unity Multi-Rotation Constraint)
    struct MultiRotationConstraintComponent
    {
        float Weight = 1.0f;
        std::string Bone;
        uint64_t Source1 = 0, Source2 = 0, Source3 = 0;
        float SourceWeight1 = 1.0f, SourceWeight2 = 1.0f, SourceWeight3 = 1.0f;
        bool MaintainOffset = false;
        glm::vec3 Offset{ 0.0f };            // derece
        bool ConstrainX = true, ConstrainY = true, ConstrainZ = true;
        bool OffsetCaptured = false;
        glm::quat CapturedOffset{ 1, 0, 0, 0 };
    };

    enum class RigOverrideSpace : int { World = 0, Local, Pivot };

    // Kemiğin dönüşümünü bir kaynak nesneyle ya da elle girilen değerle değiştirir (Unity Override Transform)
    struct OverrideTransformComponent
    {
        float Weight = 1.0f;
        std::string Bone;
        uint64_t Source = 0;                 // varsa onun dönüşümü, yoksa Position / Rotation
        RigOverrideSpace Space = RigOverrideSpace::Pivot;
        glm::vec3 Position{ 0.0f };
        glm::vec3 Rotation{ 0.0f };          // derece
        float PositionWeight = 1.0f, RotationWeight = 1.0f;
    };

    // Kaynak kemiğin (el) bükülmesini (eksen etrafındaki dönüşü) ara kemiklere (ön kol bükülme kemikleri) ağırlıkla dağıtır
    // (Unity Twist Correction): bilek dönünce ön kolun deri ağı ezilmez
    struct TwistCorrectionComponent
    {
        float Weight = 1.0f;
        std::string Source;
        RigAxis TwistAxis = RigAxis::Y;
        std::string Twist1, Twist2, Twist3;
        float TwistWeight1 = 0.5f, TwistWeight2 = 0.0f, TwistWeight3 = 0.0f;
    };

    // Kemik iki kaynak arasında karışır (Unity Blend Constraint): konum ve dönüş ayrı oranla
    struct BlendConstraintComponent
    {
        float Weight = 1.0f;
        std::string Bone;
        uint64_t SourceA = 0, SourceB = 0;
        bool BlendPosition = true, BlendRotation = true;
        float PositionWeight = 0.5f, RotationWeight = 0.5f;   // 0 = A, 1 = B
    };

    // Kökten uca zincirin dönüşleri kök ve uç hedefleri arasında yumuşakça dağılır (Unity Twist Chain Constraint): omurga, kuyruk
    struct TwistChainConstraintComponent
    {
        float Weight = 1.0f;
        std::string Root, Tip;
        uint64_t RootTarget = 0, TipTarget = 0;
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
