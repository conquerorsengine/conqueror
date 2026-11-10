#pragma once

#include "Core/Base/Base.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

// Unity Splines paketi: Spline Container (eğri), Spline Animate (yol boyunca hareket),
// Spline Extrude (boru ağı), Spline Instantiate (yol boyunca kopya dizme).
namespace Conqueror
{
    class Scene;
    class Entity;

    enum class SplineTangentMode : int { CatmullRom = 0, Linear };

    struct SplineContainerComponent
    {
        std::vector<glm::vec3> Knots{ { -3.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, -2.0f }, { 3.0f, 0.0f, 0.0f } }; // yerel uzayda
        bool Closed = false;
        SplineTangentMode Mode = SplineTangentMode::CatmullRom;
        bool ShowGizmo = true;           // seçili değilken de çiz
    };

    enum class SplineLoopMode : int { Once = 0, Loop, PingPong };

    struct SplineAnimateComponent
    {
        uint64_t Container = 0;          // eğri varlığı (0 = bu varlık)
        float Speed = 2.0f;              // m/s
        bool UseDuration = false;
        float Duration = 5.0f;           // s (UseDuration açıkken)
        SplineLoopMode Loop = SplineLoopMode::Loop;
        bool AlignToPath = true;
        bool PlayOnAwake = true;
        float StartOffset = 0.0f;        // 0..1
        // Çalışma zamanı (serileştirilmez)
        float NormalizedTime = 0.0f;
        bool Playing = false;
        bool Started = false;
        int Direction = 1;
    };

    // Tube: yuvarlak boru; Profile: kendi kesitin (yol, kerb, bariyer, ray) eğri boyunca, yatay (dünya yukarısına
    // göre) çerçevede uzatılır — yol eğri dönerken yatıp burulmaz
    enum class SplineExtrudeShape : int { Tube = 0, Profile };

    struct SplineExtrudeComponent
    {
        uint64_t Container = 0;          // 0 = bu varlık
        SplineExtrudeShape Shape = SplineExtrudeShape::Tube;
        float Radius = 0.25f;
        int Sides = 12;
        float SegmentsPerUnit = 4.0f;
        bool Capped = true;
        float RangeStart = 0.0f;         // 0..1: başlangıç (parçalara bölme: her parça kendi aralığını üretir)
        float Range = 1.0f;              // 0..1: eğrinin ne kadarı (büyüyen yol); bitiş
        // Profile: kesit noktaları (x = sağa, y = yukarı metre, z = kesit boyunca doku koordinatı). Ardışık noktalar bir
        // yüz oluşturur (sert kenar); görünen yüz gidiş yönünün solundadır: yukarı bakan yol için soldan sağa, katı
        // şekiller için saat yönünde sırala. Aynı konumda art arda iki nokta yalnız doku koordinatını değiştirir.
        std::vector<glm::vec3> Profile{ { -1.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 1.0f } };
        float UVLength = 4.0f;           // eğri boyunca kaç metrede bir doku tekrarı (kapalı eğride tam sayıya yuvarlanır)
        bool SwapUV = false;             // u = eğri boyunca, v = kesit (bariyer yazıları gibi yatay dokular)
        // Çalışma zamanı
        size_t BuiltHash = 0;
    };

    // Spline Mesh (Unreal Spline Mesh Component): bir ağ eğri boyunca bükülür. Kaynak: Source Model ya da Mesh Renderer'ın ilkel ağı.
    // Tile kapalıysa ağ eğrinin aralığına bir kez uzatılır; açıksa kendi boyunda tekrar eder (çit, ray, boru hattı)
    enum class SplineMeshAxis : int { X = 0, Y, Z };

    struct SplineMeshComponent
    {
        uint64_t Container = 0;          // 0 = bu varlık
        std::string SourceModel;         // .glb / .gltf / .fbx / .obj (boşsa Mesh Renderer'ın ilkeli)
        SplineMeshAxis ForwardAxis = SplineMeshAxis::Z;
        float RangeStart = 0.0f;
        float Range = 1.0f;
        bool Tile = false;
        glm::vec2 Scale{ 1.0f, 1.0f };   // kesit ölçeği (sağ, yukarı)
        // Çalışma zamanı
        size_t BuiltHash = 0;
    };

    struct SplineInstantiateComponent
    {
        uint64_t Container = 0;          // 0 = bu varlık
        std::string Prefab;              // .cqprefab (boşsa Template kopyalanır)
        uint64_t Template = 0;           // kopyalanacak varlık
        float Spacing = 1.0f;            // m
        bool AlignToPath = true;
        glm::vec3 PositionOffset{ 0.0f };
        // Çalışma zamanı
        bool Spawned = false;
    };

    namespace Splines
    {
        // Yerel uzayda eğri noktası; t 0..1 yay uzunluğuna göre (sabit hız)
        CQ_API glm::vec3 EvaluateLocal(const SplineContainerComponent& spline, float t, glm::vec3* tangent = nullptr);
        CQ_API float LocalLength(const SplineContainerComponent& spline);
        // Dünya uzayı (varlığın dönüşümüyle)
        CQ_API bool EvaluateWorld(Scene& scene, uint32_t containerEntity, float t, glm::vec3& position, glm::vec3* tangent = nullptr);
        CQ_API float WorldLength(Scene& scene, uint32_t containerEntity);
        // Bir dünya noktasına en yakın t
        CQ_API float NearestT(Scene& scene, uint32_t containerEntity, const glm::vec3& point);

        // Animate / Extrude / Instantiate (düzenleyicide Extrude önizlenir)
        CQ_API void Update(Scene& scene, float dt, bool runtime);
        // Seçili varlıklar için gizmo (eğri, düğümler)
        CQ_API void DrawGizmo(Scene& scene, uint32_t entity, bool selected);
        CQ_API void ResetRuntime(Scene& scene);
    }

    void RegisterSplineComponents();
}
