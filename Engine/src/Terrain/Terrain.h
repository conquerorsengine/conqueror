#pragma once

#include "Core/Base/Base.h"
#include "TerrainComponent.h"

#include <glm/glm.hpp>

#include <filesystem>
#include <string>

namespace Conqueror
{
    enum class TerrainBrushOp : int
    {
        Raise = 0,
        Lower,
        Smooth,
        Flatten,      // hedef yüksekliğe doğru
        Noise,
        Paint,        // doku katmanı boyama
        Count
    };

    struct TerrainBrush
    {
        float Radius = 6.0f;          // yerel birim
        float Strength = 0.5f;        // 0..1
        float Falloff = 0.6f;         // 0 sert kenar .. 1 yumuşak
        float TargetHeight = 0.2f;    // Flatten (0..1)
        float NoiseScale = 0.15f;
        int Layer = 0;                // Paint
        // Brush varlığı (.cqbrush, Unity Terrain Brush): gri tonlu maske dokusu fırçanın şeklini verir
        std::string Brush;
    };

    // Brush varlığı: maske (beyaz = tam etki), düşüş, döndürme, ölçek
    struct TerrainBrushAsset
    {
        std::string Mask;             // gri tonlu görsel (boş = yuvarlak)
        float Falloff = 0.0f;         // maskeye ek kenar yumuşatma (0..1)
        float Rotation = 0.0f;        // derece
        float Scale = 1.0f;           // yarıçap çarpanı
        bool Invert = false;
    };

    // Terrain Layer / Brush varlık dosyaları
    namespace TerrainAssets
    {
        constexpr const char* LayerExtension = ".cqterrainlayer";
        constexpr const char* BrushExtension = ".cqbrush";
        CQ_API bool LoadLayer(const std::string& path, TerrainLayer& out);   // önbellekli; Asset alanı korunmaz
        CQ_API bool SaveLayer(const std::string& path, const TerrainLayer& layer);
        CQ_API bool LoadBrush(const std::string& path, TerrainBrushAsset& out);
        CQ_API bool SaveBrush(const std::string& path, const TerrainBrushAsset& brush);
        // Maske örneği 0..1 (uv 0..1); maske yoksa false
        CQ_API bool SampleBrushMask(const std::string& maskPath, float u, float v, float& out);
    }

    struct TerrainNoiseSettings
    {
        int Seed = 1337;
        float Frequency = 2.5f;       // terrain boyunca
        int Octaves = 6;
        float Persistence = 0.5f;
        float Lacunarity = 2.0f;
        float Amplitude = 0.6f;       // 0..1 yükseklik
        float BaseHeight = 0.05f;
        bool Ridged = false;
        int ErosionIterations = 0;    // termal erozyon geçişi
        float IslandFalloff = 0.0f;   // kenarlara doğru alçalt (0 kapalı)
    };

    class CQ_API TerrainSystem
    {
    public:
        // Veriyi (yükseklik + splat) verilen çözünürlükte oluştur
        static void Initialize(TerrainComponent& t, int resolution = 129, float flatHeight01 = 0.0f);
        static void EnsureData(TerrainComponent& t);
        static void Resample(TerrainComponent& t, int newResolution);

        // GPU kaynaklarını güncelle (mesh, splat dokusu, katman dokuları) ve çiz
        static void UpdateGPU(TerrainComponent& t);
        static void Render(TerrainComponent& t, const glm::mat4& transform);

        // Örnekleme
        static float SampleHeight01(const TerrainComponent& t, float u, float v);
        static float GetHeightLocal(const TerrainComponent& t, float x, float z);
        static glm::vec3 GetNormalLocal(const TerrainComponent& t, float x, float z);
        static bool Raycast(const TerrainComponent& t, const glm::mat4& world, const glm::vec3& origin, const glm::vec3& direction,
                            glm::vec3& outHitWorld, float maxDistance = 10000.0f);
        static void GetLocalBounds(const TerrainComponent& t, glm::vec3& outMin, glm::vec3& outMax);

        // Fırçalar (merkez yerel xz, dt saniye)
        static void ApplyBrush(TerrainComponent& t, TerrainBrushOp op, const TerrainBrush& brush, const glm::vec2& centerLocal, float dt);

        // Üretim / toplu işlemler
        static void GenerateNoise(TerrainComponent& t, const TerrainNoiseSettings& settings);
        static void Flatten(TerrainComponent& t, float height01);
        static void SmoothAll(TerrainComponent& t, int iterations = 1);
        static void ThermalErosion(TerrainComponent& t, int iterations, float talus = 0.004f);
        // Eğim/yüksekliğe göre otomatik boya: 0 çimen, 1 kaya (dik), 2 kar (yüksek), 3 kum (alçak)
        static void AutoPaint(TerrainComponent& t);
        static void FillLayer(TerrainComponent& t, int layer);

        // Yükseklik haritası içe/dışa aktarma (PNG 8/16 bit, RAW 16 bit)
        static bool ImportHeightmap(TerrainComponent& t, const std::filesystem::path& path);
        static bool ExportHeightmap(const TerrainComponent& t, const std::filesystem::path& path);

        // Serileştirme yardımcıları (base64)
        static std::string EncodeHeights(const TerrainComponent& t);
        static bool DecodeHeights(TerrainComponent& t, const std::string& data);
        static std::string EncodeSplat(const TerrainComponent& t);
        static bool DecodeSplat(TerrainComponent& t, const std::string& data);

        static void MarkHeightsDirty(TerrainComponent& t, int x0, int z0, int x1, int z1);
        static void ReleaseGPU(TerrainComponent& t);
    };
}
