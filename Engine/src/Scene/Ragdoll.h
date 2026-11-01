#pragma once

#include "Core/Base/Base.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <vector>

// Ragdoll (Unreal Physics Asset, otomatik): iskeletten yeterince uzun kemikler seçilir, her biri için kapsül
// fizik gövdesi ve üst gövdeye karakter eklemi (salınım / burulma sınırları) kurulur. Etkinken animasyon yerine
// gövdelerden poz üretilir; seçilmeyen kemikler (baş, parmaklar...) üst kemiğe bağlı kalır.
namespace Conqueror
{
    class Scene;
    struct RagdollRuntime;

    struct RagdollComponent
    {
        bool StartActive = false;         // oyun başlar başlamaz ragdoll
        float TotalMass = 60.0f;
        float RadiusScale = 0.22f;        // kapsül yarıçapı = kemik uzunluğu x ölçek
        float MinBoneLength = 0.06f;      // iskelet boyuna oranla (daha kısa kemikler gövde almaz)
        int MaxBodies = 20;
        float SwingLimit = 40.0f;         // derece
        float TwistLimit = 25.0f;
        float LinearDamping = 0.1f;

        // Çalışma zamanı
        bool Simulating = false;
        int BodyCount = 0;
        std::shared_ptr<RagdollRuntime> Runtime;

        RagdollComponent() = default;
        RagdollComponent(const RagdollComponent& o)
            : StartActive(o.StartActive), TotalMass(o.TotalMass), RadiusScale(o.RadiusScale), MinBoneLength(o.MinBoneLength),
              MaxBodies(o.MaxBodies), SwingLimit(o.SwingLimit), TwistLimit(o.TwistLimit), LinearDamping(o.LinearDamping) {}
        RagdollComponent& operator=(const RagdollComponent& o)
        {
            StartActive = o.StartActive; TotalMass = o.TotalMass; RadiusScale = o.RadiusScale; MinBoneLength = o.MinBoneLength;
            MaxBodies = o.MaxBodies; SwingLimit = o.SwingLimit; TwistLimit = o.TwistLimit; LinearDamping = o.LinearDamping;
            Simulating = false; BodyCount = 0; Runtime.reset();
            return *this;
        }
    };

    namespace Ragdoll
    {
        // Animasyonlu pozdan gövdeleri kur ve fiziğe bırak; impulse (dünya, N·s) point'e en yakın gövdeye uygulanır
        // impulse: point verilirse ona en yakın gövdeye; wholeBody ise kütleye oranla tüm gövdelere (hepsi aynı hızı alır:
        // itilen / düşen bir karakter gibi bütün olarak savrulur, tek uzuv kopacak kadar hızlanmaz)
        CQ_API bool Activate(Scene& scene, uint32_t entity, const glm::vec3& impulse = glm::vec3(0.0f), const glm::vec3& point = glm::vec3(0.0f),
                             bool wholeBody = false);
        // Gövdeleri kaldır, animasyona dön
        CQ_API void Deactivate(Scene& scene, uint32_t entity);
        // Etkin ragdoll'ün pozundan deri matrisleri (animasyon güncellemesi yerine); false = ragdoll yok
        CQ_API bool ApplyPose(Scene& scene, uint32_t entity, std::vector<glm::mat4>& outSkinning);
        // StartActive olanları başlat (oyun başı)
        CQ_API void Update(Scene& scene);
    }

    void RegisterRagdollComponents();
}
