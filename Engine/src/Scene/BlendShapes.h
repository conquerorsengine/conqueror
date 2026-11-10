#pragma once

#include "Core/Base/Base.h"

#include <cstdint>
#include <string>
#include <vector>

// Blend shape'ler (Unity Skinned Mesh Renderer BlendShapes, Unreal Morph Targets, Godot Blend Shapes): modeldeki morph hedefleri
// ağırlıklarıyla karıştırılır. Etkin ağırlık = animasyon (klipteki morph eğrileri, durum makinesi / karışım / katmanlar dahil)
// + bu bileşendeki ağırlık (Inspector kaydırıcıları, betik SetBlendShapeWeight). Farklar CPU'da yalnız ağırlıklar değişince
// toplanır ve köşe aşamasında eklenir (renk, gölge, hareket vektörü geçişleri aynı şekli görür).
namespace Conqueror
{
    class Scene;
    struct Model;
    namespace Scripting { class NativeRegistry; }

    struct BlendShapeWeight
    {
        std::string Name;
        float Weight = 0.0f;
    };

    struct BlendShapesComponent
    {
        std::vector<BlendShapeWeight> Weights;   // elle verilen ağırlıklar (ad ile)
        // Bilgi
        int Count = 0;                           // modeldeki blend shape sayısı
        int Active = 0;                          // son karede sıfırdan farklı etkin ağırlık sayısı
    };

    namespace BlendShapes
    {
        // Her karede çizimden önce: etkin ağırlıklar -> fark dokuları (değişmeyenler yeniden hesaplanmaz)
        CQ_API void Update(Scene& scene);
        // Varlığın animasyonlu ağları için fark dokuları (ağ sırasıyla, 0: morph yok); yoksa nullptr
        CQ_API const std::vector<uint32_t>* GetTextures(const Scene& scene, uint32_t entity);
        CQ_API void Forget(const Scene& scene);
        // Modelin blend shape adları (Skeleton::MorphNames)
        CQ_API const std::vector<std::string>* Names(Scene& scene, uint32_t entity);
        CQ_API bool SetWeight(Scene& scene, uint32_t entity, const std::string& name, float weight);
        CQ_API float GetWeight(Scene& scene, uint32_t entity, const std::string& name);
        void RegisterNatives(Scripting::NativeRegistry* vm);
    }

    void RegisterBlendShapeComponents();
}
