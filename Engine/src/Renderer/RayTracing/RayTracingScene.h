#pragma once

#include "Core/Base/Base.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>

// GPU ışın izleme sahnesi (Path Tracer ve Ray Traced GI ortak), iki seviyeli BVH:
//  - BLAS: her mesh (ilkel, özel mesh, model alt meshi, arazi, MultiMesh örnek kümesi) kendi nesne uzayında bir kez binned SAH
//    BVH'ye dizilir (büyük meshler arka plan iş parçacığında); aynı meshi kullanan tüm nesneler aynı BLAS'ı paylaşır.
//  - TLAS: örnekler (dünya matrisi + BLAS + malzeme) her değişimde yeniden dizilir; hareket eden nesneler yalnız TLAS'ı yeniler.
// Malzemeler (renk = renk ayarı x doku ortalaması, metal / pürüz, ışıma, Cutout / saydam kapsaması), bu karenin ışıkları (güneş,
// nokta, spot, alan; Renderer3D'ye verilenler) ve gökyüzü (motorun ortam ışığı ayarı) compute shader'lara bağlanır.
// CommonGLSL: yapılar, tamponlar (SSBO 0-7), trace / occluded, isabet bilgisi (hitInfo), gökyüzü (ambient / background),
// GGX BRDF ve gölge ışınlı doğrudan ışık (directLight).
namespace Conqueror
{
    class Scene;

    namespace RayTracingScene
    {
        struct Stats
        {
            int Triangles = 0;               // örneklerin toplam üçgeni (paylaşılan meshler her örnekte sayılır)
            int Nodes = 0;                   // TLAS düğümü (0: boş sahne)
            int Instances = 0;
            int UniqueMeshes = 0;            // hazır BLAS sayısı
            int PendingBuilds = 0;           // arka planda dizilen BLAS
            uint64_t GeometryVersion = 0;    // sahnenin geometrisi / malzemeleri her değiştiğinde artar
            glm::vec3 BoundsMin{ 0.0f }, BoundsMax{ 0.0f };
        };

        CQ_API const char* CommonGLSL();
        // "#version 450 core" + yerel boyut + CommonGLSL + body ile compute programı (0: derlenemedi, hata günlükte)
        CQ_API uint32_t CompileCompute(const std::string& name, const std::string& localSize, const std::string& body);
        // Sahneyi günceller: değişen örnekler için TLAS, yeni / değişen meshler için BLAS. wait: bekleyen BLAS yapımları bitene dek
        // bekler (Path Tracer eksik geometriyle örnek biriktirmesin); false: hazır olmayan mesh bu karede yok sayılır (Ray Traced GI)
        CQ_API const Stats& Update(Scene& scene, bool wait = true);
        CQ_API const Stats& GetStats();
        // Programa sahne + ışık + gökyüzü bağlar (SSBO 0-7; küpler bgUnit / lightUnit, doku ortalamaları avgUnit);
        // dönüş: ışık / ortam özeti (değişim algılama)
        CQ_API uint64_t Bind(uint32_t program, int bgUnit = 0, int lightUnit = 1, int avgUnit = 2);
    }
}
