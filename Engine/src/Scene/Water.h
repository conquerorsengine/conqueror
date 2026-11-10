#pragma once

#include "Core/Base/Base.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

// Unreal Water karşılığı: Water Body (okyanus / göl / özel) Gerstner dalgalı yüzey, Fresnel yansıma, derinliğe göre renk, kıyıda
// yumuşama ve köpük. Buoyancy bileşeni Rigidbody'leri aynı dalga fonksiyonuna göre yüzdürür (pontonlar).
namespace Conqueror
{
    class Scene;

    enum class WaterBodyType : int { Ocean = 0, Lake, Custom };

    struct WaterBodyComponent
    {
        WaterBodyType Type = WaterBodyType::Lake;
        glm::vec2 Size{ 20.0f, 20.0f };       // göl / özel: yerel boy (m); okyanus kamerayı izleyen geniş ızgara
        int Resolution = 128;                 // kenar başına hücre
        int WaveCount = 3;                    // 0..4
        float WaveAmplitude = 0.25f;          // m
        float WaveLength = 8.0f;              // m
        float WaveSpeed = 2.0f;               // m/s
        float WaveDirection = 30.0f;          // derece
        float Steepness = 0.5f;               // 0..1
        glm::vec3 ShallowColor{ 0.12f, 0.6f, 0.62f };
        glm::vec3 DeepColor{ 0.02f, 0.12f, 0.2f };
        float Opacity = 0.85f;
        float DepthFade = 4.0f;               // m
        float ShoreFade = 0.5f;               // m
        float Roughness = 0.05f;
        float Foam = 0.5f;
    };

    struct BuoyancyComponent
    {
        std::vector<glm::vec3> Pontoons{ { 0.0f, 0.0f, 0.0f } };   // yerel noktalar
        float PontoonRadius = 0.5f;
        float BuoyancyCoefficient = 1.0f;
        float WaterDrag = 3.0f;               // batma oranıyla hız sönümü (1/s): sallanma birkaç saniyede durulur
        float WaterAngularDrag = 3.0f;
        // Çalışma zamanı
        bool InWater = false;
        float Submerged = 0.0f;               // 0..1
    };

    namespace WaterBodies
    {
        // Dünya noktasındaki su yüksekliği (dalgalar dahil, GPU ile aynı fonksiyon); su yoksa found = false
        CQ_API float WaterHeight(Scene& scene, const glm::vec3& point, float time, bool& found);
        // Her sabit fizik adımında (kuvvetler o adıma uygulanır): yüzdürme
        CQ_API void FixedUpdate(Scene& scene, float dt);
        CQ_API void Render(Scene& scene, const glm::vec3& cameraPosition);
        CQ_API void DrawGizmo(Scene& scene, uint32_t entity);
    }

    void RegisterWaterComponents();
}
