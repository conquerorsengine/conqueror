#pragma once

#include "Core/Base/Base.h"
#include "Renderer/RHI/Buffer.h"
#include "Renderer/RHI/VertexArray.h"

#include <glm/glm.hpp>
#include <vector>
#include <memory>

namespace Conqueror
{
    struct Vertex
    {
        glm::vec3 Position;
        glm::vec3 Normal;
        glm::vec3 Tangent;
        glm::vec2 TexCoords;
    };

    class Mesh
    {
    public:
        Mesh(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices);
        ~Mesh() = default;

        void Bind() const;
        void Unbind() const;

        uint32_t GetIndexCount() const { return m_IndexCount; }
        uint32_t GetVertexCount() const { return (uint32_t)m_Vertices.size(); }
        std::shared_ptr<VertexArray> GetVertexArray() const { return m_VertexArray; }

        const std::vector<Vertex>& GetVertices() const { return m_Vertices; }
        const std::vector<uint32_t>& GetIndices() const { return m_Indices; }

        // Cube mesh oluştur
        static std::shared_ptr<Mesh> CreateCube();
        
        // Sphere mesh oluştur (light gizmo için)
        static std::shared_ptr<Mesh> CreateSphere(float radius = 0.5f, uint32_t segments = 16);
        
        // Plane mesh oluştur
        static std::shared_ptr<Mesh> CreatePlane(float size = 1.0f, uint32_t subdivisions = 1);
        
        // Cylinder mesh oluştur
        static std::shared_ptr<Mesh> CreateCylinder(float radius = 0.5f, float height = 1.0f, uint32_t segments = 32);
        
        // Cone mesh oluştur (spot light gizmo için)
        static std::shared_ptr<Mesh> CreateCone(float radius = 0.5f, float height = 1.0f, uint32_t segments = 64);
        
        // Arrow mesh oluştur (directional light gizmo için)
        static std::shared_ptr<Mesh> CreateArrow(float shaftRadius = 0.05f, float shaftLength = 0.8f, 
                                                  float headRadius = 0.15f, float headLength = 0.3f, uint32_t segments = 32);

    private:
        void SetupMesh();

    private:
        std::vector<Vertex> m_Vertices;
        std::vector<uint32_t> m_Indices;
        uint32_t m_IndexCount;

        std::shared_ptr<VertexArray> m_VertexArray;
        std::shared_ptr<VertexBuffer> m_VertexBuffer;
        std::shared_ptr<IndexBuffer> m_IndexBuffer;
    };
}
