#pragma once

#include "Core/Base/Base.h"

#include <glm/glm.hpp>

#include <string>

// Foliage Type / Landscape Grass Type / Procedural Foliage Spawner / PCG Graph varlıklarını kullanan hacim:
// Generate ile sahneye MultiMesh örnekleri üretir (Unreal Procedural Foliage Volume ve PCG Volume karşılığı).
namespace Conqueror
{
    class Scene;
    class Entity;

    struct FoliageVolumeComponent
    {
        std::string Spawner;            // .cqfoliagespawner
        std::string GrassType;          // .cqgrasstype
        std::string Graph;              // .cqpcg
        glm::vec3 Size{ 40.0f, 0.0f, 40.0f };
        int Seed = 1;
        bool GenerateOnPlay = false;

        // Bilgi
        int Instances = 0;
        bool PlayGenerated = false;
    };

    namespace Foliage
    {
        constexpr const char* GraphExtension = ".cqpcg";
        constexpr const char* GraphKind = "PCGGraph";
        CQ_API void Generate(Scene& scene, Entity volume);
        CQ_API void Clear(Scene& scene, Entity volume);
        CQ_API void Update(Scene& scene);
        void RegisterComponents();
    }
}
