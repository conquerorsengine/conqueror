#pragma once

#include "Core/Base/Base.h"

#include <glm/glm.hpp>

#include <memory>
#include <string>
#include <vector>

// Kameraya dönük çiziciler:
//  - Billboard Renderer (Unity): uzak ağaç / ot / tabela için tek dokulu dörtgen (alfa kesme, isteğe bağlı ışık)
//  - Halo (Unity): nesnenin çevresinde yumuşak parıltı
//  - Lens Flare (URP Lens Flare SRP): ışık kaynağından ekran merkezine uzanan parıltı, halka, hayaletler ve çizgi;
//    örtülme (occlusion) GPU sorgusuyla ölçülür, ekran dışına çıkınca söner
namespace Conqueror
{
    class Scene;
    class Texture2D;
    struct RenderCamera;

    enum class BillboardAlignment : int { FaceCamera = 0, VerticalAxis };

    struct BillboardRendererComponent
    {
        std::string Texture;
        glm::vec4 Color{ 1.0f };
        glm::vec2 Size{ 1.0f, 1.0f };            // genişlik, yükseklik (ölçekle çarpılır)
        BillboardAlignment Alignment = BillboardAlignment::VerticalAxis;
        bool PivotAtBottom = true;               // ağaçlar: alt kenar varlık konumunda
        float AlphaCutoff = 0.5f;
        bool Lit = true;                         // ortam + güneş ışığıyla renklendir
        // Çalışma zamanı
        std::shared_ptr<Texture2D> TextureRef;
        std::string LoadedTexture;
    };

    struct HaloComponent
    {
        glm::vec3 Color{ 1.0f, 0.85f, 0.55f };
        float Size = 1.0f;                       // yarıçap (metre)
        float Intensity = 1.0f;
    };

    // Lens Flare varlığı (.cqflare, Unity Lens Flare Data): sıralı parlama öğeleri
    enum class LensFlareShape : int { Glow = 0, Ring, Hexagon, Circle, Streak };
    struct LensFlareElement
    {
        LensFlareShape Shape = LensFlareShape::Glow;
        glm::vec3 Color{ 1.0f };
        float Size = 0.25f;          // ekran yüksekliğine oran
        float Intensity = 1.0f;
        float Position = 0.0f;       // 0 = ışığın üstünde, 0.5 = ekran merkezi, 1 = ışığın aynası
        int Count = 1;               // tekrar (hayaletler)
        float Spacing = 0.3f;        // tekrarlar arası konum adımı
        float Stretch = 1.0f;        // yatay uzatma (çizgi)
    };
    namespace LensFlareAssets
    {
        constexpr const char* Extension = ".cqflare";
        // Önbellekli (dosya değişince yeniden okunur)
        CQ_API bool Load(const std::string& path, std::vector<LensFlareElement>& elements);
        CQ_API bool Save(const std::string& path, const std::vector<LensFlareElement>& elements);
        // Bileşenin yerleşik ayarlarına eşdeğer öğeler (yeni varlığın başlangıcı)
        CQ_API std::vector<LensFlareElement> DefaultElements();
    }

    struct LensFlareComponent
    {
        // Lens Flare varlığı: doluysa öğeler dosyadan (aşağıdaki Elements ayarları yerine)
        std::string Flare;
        glm::vec3 Color{ 1.0f };
        float Intensity = 1.0f;
        float Scale = 1.0f;
        bool UseLightColor = true;               // varlıktaki ışığın rengiyle çarp
        bool UseOcclusion = true;
        float OcclusionRadius = 0.25f;           // metre (yönlü ışıkta ekran oranı)
        float MaxDistance = 0.0f;                // > 0: bu mesafeden sonra söner

        float GlowSize = 0.25f;                  // ekran yüksekliğine oran
        float GlowIntensity = 1.0f;
        float RingSize = 0.22f;
        float RingIntensity = 0.1f;
        int GhostCount = 4;
        float GhostSpacing = 0.3f;               // ışıktan ekran merkezine doğru adım (0..1 = ışık..ayna)
        float GhostSize = 0.08f;
        float GhostIntensity = 0.25f;
        bool HexagonGhosts = true;
        float StreakLength = 0.6f;
        float StreakIntensity = 0.3f;

        // Çalışma zamanı: son ölçülen görünürlük (0..1)
        float Visibility = 0.0f;
    };

    namespace Billboards
    {
        // Opak geçişin sonunda (saydamlardan önce): alfa kesmeli billboard'lar
        CQ_API void RenderOpaque(Scene& scene, const RenderCamera& camera);
        // Saydamlar ve parçacıklardan sonra: halolar ve lens parlamaları (toplamalı)
        CQ_API void RenderEffects(Scene& scene, const RenderCamera& camera);
        CQ_API bool GetLocalBounds(Scene& scene, uint32_t entity, glm::vec3& outMin, glm::vec3& outMax);
        CQ_API void Shutdown();
    }

    void RegisterBillboardComponents();
}
