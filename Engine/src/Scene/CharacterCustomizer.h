#pragma once

#include "Core/Base/Base.h"
#include "Core/Animation/Animator.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <string>

// Character Customizer (MetaHuman Creator benzeri, sade): insansı (ya da herhangi) iskeletli modelin oranları
// kemik ölçekleme / uzunluk ile, görünümü örneğe özel materyal kopyalarıyla değiştirilir: boy, baş boyutu,
// gövde genişliği, kol / bacak uzunluğu, ten rengi (açıktan koyuya), saç ve göz rengi, kıyafet dokusu; deri materyalleri
// Subsurface (deri) gölgelendirme modeline, saç materyalleri Hair modeline geçirilir.
namespace Conqueror
{
    class Scene;
    class Skeleton;

    struct CharacterCustomizerComponent
    {
        float Height = 1.0f;
        float HeadSize = 1.0f;
        float BodyWidth = 1.0f;
        float ArmLength = 1.0f;
        float LegLength = 1.0f;
        float SkinTone = 0.3f;            // 0 açık .. 1 koyu
        glm::vec3 HairColor{ 0.23f, 0.15f, 0.09f };
        glm::vec3 EyeColor{ 0.25f, 0.42f, 0.55f };
        bool SkinShading = true;          // deri materyallerine Subsurface modeli
        std::string SkinMaterials = "skin,body,face,head";   // materyal / mesh adı içeren (virgüllü); "*" = hepsi
        std::string HairMaterials = "hair,brow,beard";
        std::string EyeMaterials = "eye,iris";
        // Kıyafet dokusu: eşleşen materyallerin taban rengi bu doku olur (giysi, ayakkabı, saç UV uzayında boyanmış tek
        // doku; ör. gri manken -> giyinik karakter). Boş = yok. Kumaş için pürüzlülük de ayarlanır.
        std::string OutfitTexture;
        std::string OutfitMaterials = "*";
        float OutfitRoughness = 0.72f;
        bool OutfitKeepNormalMap = false;      // modelin kendi normal haritası (ör. kostüm dikişleri, kaslar) kalsın mı

        // Çalışma zamanı (kopyalanmaz): örneğe özel model kopyası (meshler paylaşılır, materyaller kopya)
        uint64_t AppliedSignature = 0;
        std::shared_ptr<struct Model> InstanceModel;

        CharacterCustomizerComponent() = default;
        CharacterCustomizerComponent(const CharacterCustomizerComponent& o) { *this = o; }
        CharacterCustomizerComponent& operator=(const CharacterCustomizerComponent& o)
        {
            Height = o.Height; HeadSize = o.HeadSize; BodyWidth = o.BodyWidth; ArmLength = o.ArmLength; LegLength = o.LegLength;
            SkinTone = o.SkinTone; HairColor = o.HairColor; EyeColor = o.EyeColor; SkinShading = o.SkinShading;
            SkinMaterials = o.SkinMaterials; HairMaterials = o.HairMaterials; EyeMaterials = o.EyeMaterials;
            OutfitTexture = o.OutfitTexture; OutfitMaterials = o.OutfitMaterials; OutfitRoughness = o.OutfitRoughness;
            OutfitKeepNormalMap = o.OutfitKeepNormalMap;
            AppliedSignature = 0;
            InstanceModel.reset();
            return *this;
        }
    };

    namespace CharacterCustomizer
    {
        // Animasyon pozuna oranları uygula (deri matrislerinden önce)
        CQ_API void ApplyToPose(const CharacterCustomizerComponent& c, const Skeleton& skeleton, Pose& pose);
        // Materyal görünümünü güncelle (değiştiyse); her karede çağrılabilir
        CQ_API void Update(Scene& scene);
        CQ_API glm::vec3 SkinColor(float tone);
    }

    void RegisterCharacterCustomizerComponents();
}
