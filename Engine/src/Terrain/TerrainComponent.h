#pragma once

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <memory>
#include <new>
#include <string>
#include <vector>

namespace Conqueror
{
    class Texture2D;
    class Mesh;
    class Material;

    // Terrain doku katmanı (splat haritasının bir kanalı)
    struct TerrainLayer
    {
        std::string TexturePath;            // boşsa düz renk
        std::shared_ptr<Texture2D> Albedo;  // çalışma zamanı
        glm::vec3 Tint{ 1.0f };
        float Tiling = 12.0f;               // terrain boyunca tekrar sayısı
        float Roughness = 0.85f;
        float Metallic = 0.0f;
        // Terrain Layer varlığı (.cqterrainlayer, Unity Terrain Layer): doluysa yukarıdaki değerler dosyadan
        // (birçok arazi aynı katmanı paylaşır; Terrain panelindeki değişiklikler dosyaya yazılır)
        std::string Asset;
    };

    // Terrain Collider (Unity): aynı nesnedeki Terrain'in yükseklik alanı çarpıştırıcısı. Bileşen yoksa arazi katı değildir
    struct TerrainColliderComponent
    {
        std::string PhysicsMaterial;        // .cqphysmat (sürtünme / sekme); boşsa varsayılan
    };

    // Unity Terrain karşılığı: yükseklik haritası + 4 katmanlı splat (RGBA ağırlık).
    // Yerel uzayda (0,0,0)-(Width, Height, Length) aralığını kaplar; dönüşümle taşınır.
    struct TerrainComponent
    {
        static constexpr int MaxLayers = 4;

        float Width = 100.0f;
        float Length = 100.0f;
        float Height = 30.0f;               // en yüksek nokta (yükseklik 1.0)
        int Resolution = 129;               // kenar başına örnek (2^n + 1 önerilir)
        int SplatResolution = 256;

        std::vector<float> Heights;         // Resolution^2, 0..1, satır = z
        std::vector<uint8_t> Splat;         // SplatResolution^2 * 4 (RGBA ağırlık)
        std::array<TerrainLayer, MaxLayers> Layers;
        int LayerCount = 1;

        bool CastShadows = true;
        bool ReceiveShadows = true;
        bool EnableCollider = true;

        // ---- Çalışma zamanı (kaydedilmez)
        std::shared_ptr<Mesh> RuntimeMesh;
        std::shared_ptr<Texture2D> RuntimeSplat;
        std::shared_ptr<Material> RuntimeMaterial;
        bool MeshDirty = true;              // tam yeniden oluştur
        bool SplatDirty = true;
        glm::ivec4 DirtyRegion{ 0, 0, -1, -1 }; // kısmi vertex güncellemesi (x0, z0, x1, z1)
        uint32_t ColliderVersion = 0;       // fizik yeniden oluşturma için
        void* RuntimeBody = nullptr;        // Jolt gövde kimliği (oyun modu)
        uint32_t DataVersion = 1;

        TerrainComponent() = default;
        TerrainComponent(const TerrainComponent& other)
            : Width(other.Width), Length(other.Length), Height(other.Height), Resolution(other.Resolution),
              SplatResolution(other.SplatResolution), Heights(other.Heights), Splat(other.Splat), Layers(other.Layers),
              LayerCount(other.LayerCount), CastShadows(other.CastShadows), ReceiveShadows(other.ReceiveShadows),
              EnableCollider(other.EnableCollider)
        {
            // Kopya kendi GPU kaynaklarını oluşturur
        }
        TerrainComponent& operator=(const TerrainComponent& other)
        {
            if (this != &other)
            {
                this->~TerrainComponent();
                new (this) TerrainComponent(other);
            }
            return *this;
        }
    };
}
