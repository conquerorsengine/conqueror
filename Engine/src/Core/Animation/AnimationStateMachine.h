#pragma once

#include "Core/Base/Base.h"
#include "AnimationController.h"
#include "Animator.h"

#include <functional>
#include <memory>
#include <vector>

namespace Conqueror
{
    struct AnimationComponent;
    struct Model;

    // AnimationController'ı (katmanlar, durumlar, geçişler, blend tree'ler)
    // çalıştırıp AnimationComponent::BoneMatricesGPU'yu üreten çalışma zamanı.
    class CQ_API AnimationStateMachine
    {
    public:
        // Bileşen parametrelerini controller ile eşitle (eksikleri ekler, değerleri korur)
        static void SyncParameters(AnimationComponent& ac, const AnimationController& controller);
        // Durumları baştan başlat (Play'e girerken, controller değişince)
        static void Reset(AnimationComponent& ac);
        // postPose: son poz deri matrislerine çevrilmeden önce (Animation Rigging kısıtları)
        static void Update(AnimationComponent& ac, const Model& model, float deltaSeconds, const std::function<void(Pose&)>* postPose = nullptr);

        // Editör/araçlar için: bir durumun saniye cinsinden süresi (blend tree ağırlıklı)
        static float GetStateDuration(const AnimState& state, const Model& model, const AnimationComponent* ac = nullptr);
        // Blend tree çocuk ağırlıkları (editör önizlemesi)
        static void ComputeBlendWeights(const AnimState& state, const AnimationComponent* ac, std::vector<float>& outWeights);
        static float GetParameterFloat(const AnimationComponent& ac, const std::string& name, float fallback = 0.0f);

        // Montaj (Unreal Anim Montage): oynayan montaj kesilir. section boş = ilk bölüm. Döndürür: oynatma süresi (sn), 0 = başarısız
        static float PlayMontage(AnimationComponent& ac, const Model* model, const std::shared_ptr<AnimMontage>& montage,
                                 float rate = 1.0f, const std::string& section = "");
        // blendOut < 0: montajın kendi BlendOut süresi
        static void StopMontage(AnimationComponent& ac, float blendOut = -1.0f);
        static bool JumpToSection(AnimationComponent& ac, const Model* model, const std::string& section);
        // from bölümü bitince to'ya geç ("Stop" = bitir, boş = zaman çizelgesinde devam)
        static bool SetNextSection(AnimationComponent& ac, const std::string& from, const std::string& to);
    };
}
