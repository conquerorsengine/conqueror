#pragma once

#include "Core/Base/Base.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <vector>

// Chaos Destruction / Geometry Collection (sade): nesnenin meshi Voronoi hücrelerine bölünür (konveks çokyüzlü
// kırpma; iç yüzler ayrı renkte), yeterli çarpışma itkisi ya da betik isteğiyle nesne parçalara ayrılır. Parçalar
// kendi konveks çarpıştırıcıları ve hacimlerine göre kütleleriyle fizik gövdesi olur, dışa doğru savrulur.
namespace Conqueror
{
    class Scene;
    class Mesh;

    namespace Physics3D { struct ContactEvent; }

    struct FractureComponent
    {
        int Pieces = 12;                  // Voronoi hücresi sayısı (2..64)
        int Seed = 7;
        float BreakImpulse = 6.0f;        // çarpışma itkisi eşiği (0 = yalnızca betik / FractureBreak)
        float ExplosionSpeed = 1.5f;      // parçaların kırılma noktasından uzaklaşma hızı (m/s)
        float PieceLifetime = 10.0f;      // s (0 = kalıcı)
        glm::vec3 InteriorColor{ 0.72f, 0.66f, 0.58f };
        bool InheritVelocity = true;

        // Çalışma zamanı
        bool Broken = false;
        int PieceCount = 0;
    };

    namespace Fracture
    {
        struct Piece
        {
            std::shared_ptr<Mesh> PieceMesh;
            std::vector<glm::vec4> Colors;   // köşe renkleri (iç yüzler InteriorColor)
            glm::vec3 Center{ 0.0f };
            float Volume = 0.0f;
        };

        // Yerel uzayda parçalar (aynı mesh / sayı / tohum için önbellekten)
        CQ_API const std::vector<Piece>& BuildPieces(const std::shared_ptr<Mesh>& source, int count, uint32_t seed, const glm::vec3& interiorColor);
        // Nesneyi kır: parça varlıkları oluşturulur, kaynak silinir; parça sayısı
        CQ_API int Break(Scene& scene, uint32_t entity, const glm::vec3& worldPoint, float impulse);
        // Çarpışma bildirimi (eşik aşılırsa kırılma sıraya alınır)
        CQ_API void OnContact(Scene& scene, const Physics3D::ContactEvent& event);
        // Sıradaki kırılmalar + parça ömürleri
        CQ_API void Update(Scene& scene, float dt);
        CQ_API void ForgetScene(Scene* scene);
        // Parça meshleri GPU kaynağı tutar: GL bağlamı kapanmadan bırakılmalı
        CQ_API void ClearCache();
    }

    void RegisterFractureComponents();
}
