#pragma once

#include "Core/Base/Base.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

// MetaHuman desteği (Unreal): MetaHuman dışa aktarımının (MetaHuman Creator / Bridge -> FBX / glTF) parçaları tek karakterde
// birleşir. Parçalar ada göre tanınır (gövde, yüz, saç, kaş, kirpik, sakal, göz, diş, giysi); gövde dışındaki parçalar Leader Pose
// ile gövde iskeletini izler (Unreal "Set Leader Pose Component"), malzemeler gölgelendirme modeline (deri: Subsurface, saç: Hair,
// alfa kırpmalı kartlar) geçer. Yüz animasyonu: Live Link Face (iPhone ARKit) CSV kaydının 52 blend shape eğrisi yüz ağının
// blend shape'lerine (aynı ad ya da eşleme tablosu) uygulanır.
namespace Conqueror
{
    class Scene;
    class Entity;

    // Bu nesnenin iskeleti Leader'ın pozunu kemik adına göre izler (yalnız ortak kemikler; yüzün kendi kemikleri kendi animasyonunda)
    struct LeaderPoseComponent
    {
        uint64_t Leader = 0;          // varlık UUID
        // Bilgi
        int SharedBones = 0;
        std::vector<glm::mat4> BoneMatrices;   // çalışma zamanı: çizimde Animator yerine kullanılır
    };

    // Live Link Face CSV kaydını blend shape'lere oynatır (ARKit: eyeBlinkLeft, jawOpen, mouthSmileLeft ...)
    struct LiveLinkFaceComponent
    {
        std::string Recording;        // .csv (Live Link Face "Export" dosyası)
        bool Playing = true;
        bool Loop = true;
        float Speed = 1.0f;
        float Intensity = 1.0f;       // tüm ağırlıklar bununla çarpılır
        float FrameRate = 60.0f;      // zaman kodu kare bölümü
        std::string Remap;            // "ARKitAdı=BlendShape; ..." (boş: aynı ad, büyük / küçük harf ve _L/_R / Left/Right biçimleri denenir)
        // Bilgi
        float Time = 0.0f;
        float Duration = 0.0f;
        int Curves = 0;               // dosyadaki eğri sayısı
        int Mapped = 0;               // modelde karşılığı bulunan eğri sayısı
    };

    // MetaHuman karakter kökü: Folder'daki dışa aktarım parçalarını çocuk nesneler olarak kurar
    struct MetaHumanComponent
    {
        std::string Folder;           // proje göreli klasör (FBX / glTF / GLB parçaları)
        bool SkinShading = true;      // gövde / yüz malzemeleri Subsurface (deri)
        bool HairShading = true;      // saç / kaş / kirpik: Hair + alfa kırpma
        // Bilgi
        int Parts = 0;
        std::string Body;
        std::string Face;
        std::vector<uint64_t> PartIds;   // Assemble'ın kurduğu çocuklar (yeniden kurulunca silinir)
    };

    namespace MetaHuman
    {
        // Parça türü dosya adından (body, face, hair, eyebrows, eyelashes, beard, mustache, eyes, teeth, clothing, other)
        CQ_API std::string ClassifyPart(const std::string& fileName);
        // Kökün altına parçaları kurar (önceki kurulumun çocuklarını yeniler); kurulan parça sayısı
        CQ_API int Assemble(Scene& scene, Entity root, std::string& error);
        // Her karede çizimden önce (animasyondan sonra): Leader Pose ve Live Link Face
        CQ_API void Update(Scene& scene, float dt);
        // ARKit 52 blend shape adı listesi (Live Link Face sırası)
        CQ_API const std::vector<std::string>& ARKitShapes();
    }

    void RegisterMetaHumanComponents();
}
