#pragma once

#include "Core/Base/Base.h"

#include <glm/glm.hpp>

#include <memory>
#include <string>
#include <vector>

// URP 2D Renderer karşılığı:
//  - Light 2D: Global (ortam), Point (spot açılarıyla), Sprite (çerez dokusu), Freeform (çokgen)
//  - Shadow Caster 2D: çarpıştırıcı / sprite / özel çokgen şeklinden sert gölge
//  - Sprite Mask: Sprite Renderer "Mask Interaction" ile içeride / dışarıda görünürlük
// Sahnede en az bir Light 2D varsa sprite'lar ve karo haritaları ışık dokusuyla çarpılır;
// hiç yoksa her şey eskisi gibi ışıksız çizilir.
namespace Conqueror
{
    class Scene;
    class Texture2D;

    enum class Light2DType : int { Global = 0, Point, Sprite, Freeform };

    struct Light2DComponent
    {
        Light2DType Type = Light2DType::Point;
        glm::vec3 Color{ 1.0f };
        float Intensity = 1.0f;

        // Point / Spot (dünya birimi, ölçekten bağımsız)
        float InnerRadius = 0.0f;
        float OuterRadius = 5.0f;
        float InnerAngle = 360.0f;        // derece (360 = tam daire)
        float OuterAngle = 360.0f;
        float FalloffStrength = 0.5f;     // 0 doğrusal .. 1 keskin merkez

        // Sprite ışığı: dokunun rengi ışık olarak yansıtılır (boyut: piksel / 100 x ölçek)
        std::string Sprite;

        // Freeform: yerel çokgen + dışa doğru yumuşak kenar
        std::vector<glm::vec2> Points{ { -1.0f, -1.0f }, { 1.0f, -1.0f }, { 1.0f, 1.0f }, { -1.0f, 1.0f } };
        float FalloffDistance = 0.5f;

        bool Shadows = false;
        float ShadowStrength = 0.8f;      // 0..1 (1 = tam karanlık)

        // Normal haritası: Sprite Editor > Secondary Textures'taki _NormalMap dokuları bu ışığa tepki verir (Point / Spot)
        bool UseNormalMap = false;
        float NormalDistance = 3.0f;      // ışığın sprite düzleminden yüksekliği (dünya birimi)
        float VolumeIntensity = 0.0f;     // sisli ortamda görünen ışık hüzmesi (sprite'ların üstüne eklenir)

        // Çalışma zamanı
        std::shared_ptr<Texture2D> SpriteTexture;
        std::string LoadedSprite;
    };

    enum class ShadowCasterShape : int { Auto = 0, Custom };

    struct ShadowCaster2DComponent
    {
        bool CastsShadows = true;
        bool SelfShadows = false;         // kapalıyken kendi yüzeyi kendi gölgesinde kalmaz
        ShadowCasterShape Shape = ShadowCasterShape::Auto;   // Auto: 2D çarpıştırıcı, yoksa sprite sınırı
        std::vector<glm::vec2> Points{ { -0.5f, -0.5f }, { 0.5f, -0.5f }, { 0.5f, 0.5f }, { -0.5f, 0.5f } };
    };

    struct SpriteMaskComponent
    {
        std::string Sprite;               // boşsa düz dikdörtgen (1x1 x ölçek)
        float AlphaCutoff = 0.2f;
        float PixelsPerUnit = 100.0f;
        // Çalışma zamanı
        std::shared_ptr<Texture2D> SpriteTexture;
        std::string LoadedSprite;
    };

    namespace Lighting2D
    {
        // 2D geçişinden önce: ışık dokusunu üretir, maskeleri stencil'e yazar ve Renderer2D'yi ayarlar.
        // Dönüş: sprite'lar ışıkla çarpılacak mı (sahnede Light 2D var mı)
        CQ_API bool BeginPass(Scene& scene, const glm::mat4& viewProjection);
        // 2D geçişinden sonra: hacim ışıkları + Renderer2D ışık durumunu sıfırla
        CQ_API void EndPass(Scene& scene, const glm::mat4& viewProjection);
        // Bir dünya noktasındaki toplam ışık (betik / test): gölgeler dahil değil
        CQ_API glm::vec3 SampleLight(Scene& scene, const glm::vec2& worldPoint);
        CQ_API void DrawGizmo(Scene& scene, uint32_t entity);
        CQ_API void Shutdown();
    }

    void RegisterLighting2DComponents();
}
