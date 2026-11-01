#pragma once

#include "Core/Base/Base.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>

// CSG (Godot CSGShape3D): kutu / küre / silindir / koni şekilleri hiyerarşide birleşim, çıkarma ve kesişimle
// tek bir meshe dönüştürülür (BSP ağacı). Kök CSG nesnesi sonucu Mesh Renderer ile çizer, isteğe bağlı statik
// Mesh Collider alır; alt CSG nesneleri yalnızca geometri verir. Değişiklikte (dönüşüm / ayar) yeniden kurulur.
namespace Conqueror
{
    class Scene;
    class Mesh;

    enum class CSGShapeType : int { Box = 0, Sphere, Cylinder, Cone };
    enum class CSGOperation : int { Union = 0, Subtraction, Intersection };

    struct CSGShapeComponent
    {
        CSGShapeType Shape = CSGShapeType::Box;
        CSGOperation Operation = CSGOperation::Union;    // üst CSG'ye uygulanış biçimi (kökte yok sayılır)
        glm::vec3 Size{ 2.0f };           // kutu
        float Radius = 1.0f;              // küre / silindir / koni
        float Height = 2.0f;
        int Sides = 24;                   // küre / silindir dilim sayısı
        bool SmoothFaces = false;         // küre / silindir yumuşak normaller
        bool UseCollision = true;         // kökte statik çarpıştırıcı

        // Çalışma zamanı
        uint64_t BuiltSignature = 0;
        int Triangles = 0;
    };

    namespace CSG
    {
        // Kök CSG nesnelerinin meshlerini gerekirse yeniden kur (düzenleyicide ve oyunda)
        CQ_API void Update(Scene& scene);
        // Bir kökü hemen kur; üçgen sayısı
        CQ_API int Rebuild(Scene& scene, uint32_t root);
    }

    void RegisterCSGComponents();
}
