#pragma once

#include "Core/Base/Base.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>

// GPU ışın izleme sahnesi (Path Tracer ve Ray Traced GI ortak): Mesh Renderer üçgenleri dünya uzayında medyan bölmeli BVH'ye
// dizilir (sahne değişince yeniden), malzemeler (renk / metal / pürüz / ışıma) ve bu karenin ışıkları (güneş, nokta, spot, alan;
// Renderer3D'ye verilenler) ile gökyüzü (motorun ortam ışığı ayarı) compute shader'lara bağlanır. CommonGLSL: yapılar, tamponlar
// (SSBO 0-5), trace / occluded, gökyüzü (ambient / background), GGX BRDF ve gölge ışınlı doğrudan ışık (directLight).
namespace Conqueror
{
    class Scene;

    namespace RayTracingScene
    {
        struct Stats
        {
            int Triangles = 0;
            int Nodes = 0;
            uint64_t GeometryVersion = 0;    // BVH her yeniden kurulduğunda artar
            glm::vec3 BoundsMin{ 0.0f }, BoundsMax{ 0.0f };
        };

        CQ_API const char* CommonGLSL();
        // "#version 450 core" + yerel boyut + CommonGLSL + body ile compute programı (0: derlenemedi, hata günlükte)
        CQ_API uint32_t CompileCompute(const std::string& name, const std::string& localSize, const std::string& body);
        // Sahne değiştiyse BVH'yi yeniden kurar (minInterval sn içinde en çok bir kez; 0: her değişimde)
        CQ_API const Stats& Update(Scene& scene, float minInterval = 0.0f);
        CQ_API const Stats& GetStats();
        // Programa sahne + ışık + gökyüzü bağlar (SSBO 0-5, küpler bgUnit / lightUnit); dönüş: ışık / ortam özeti (değişim algılama)
        CQ_API uint64_t Bind(uint32_t program, int bgUnit = 0, int lightUnit = 1);
    }
}
