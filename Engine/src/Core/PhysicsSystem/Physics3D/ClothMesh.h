#pragma once

#include "Core/Base/Base.h"

#include <glm/glm.hpp>

#include <memory>
#include <vector>

// Kumaş ızgarası: yerel uzayda dinlenme konumları + çift yüzlü görüntü mesh'i (ön / arka katman)
namespace Conqueror
{
    struct ClothComponent;
    class Mesh;
    class Scene;

    namespace ClothMesh
    {
        void BuildGrid(const ClothComponent& c, std::vector<glm::vec3>& localPositions, std::vector<uint32_t>& triangles,
                       std::vector<glm::vec2>& uvs, std::vector<uint32_t>& pinned);
        std::shared_ptr<Mesh> CreateMesh(const std::vector<glm::vec3>& local, const std::vector<uint32_t>& triangles, const std::vector<glm::vec2>& uvs);
        void UpdateMesh(Mesh& mesh, const std::vector<glm::vec3>& local, const std::vector<uint32_t>& triangles);
        uint64_t Signature(const ClothComponent& c);
        // Editör: kumaşın düz dinlenme hali Mesh Renderer'da görünsün
        CQ_API void EnsureRestMeshes(Scene& scene);
    }
}
