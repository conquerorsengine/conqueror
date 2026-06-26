#include "Mesh.h"
#include "Renderer/Renderer.h"

namespace Conqueror
{
    Mesh::Mesh(const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices)
        : m_Vertices(vertices), m_Indices(indices), m_IndexCount((uint32_t)indices.size())
    {
        SetupMesh();
        
        // Memory tracking
        float memoryMB = (float)(m_Vertices.size() * sizeof(Vertex) + m_Indices.size() * sizeof(uint32_t)) / (1024.0f * 1024.0f);
        Renderer::GetStats().MeshMemory += memoryMB;
    }

    void Mesh::SetupMesh()
    {
        // Vertex Array oluştur
        m_VertexArray = VertexArray::Create();

        // Vertex Buffer oluştur
        m_VertexBuffer = VertexBuffer::Create((float*)m_Vertices.data(), 
                                               (uint32_t)(m_Vertices.size() * sizeof(Vertex)));

        // Vertex layout tanımla
        BufferLayout layout = {
            { ShaderDataType::Float3, "a_Position" },
            { ShaderDataType::Float3, "a_Normal" },
            { ShaderDataType::Float3, "a_Tangent" },
            { ShaderDataType::Float2, "a_TexCoord" },
            { ShaderDataType::Float2, "a_TexCoord2" }
        };
        m_VertexBuffer->SetLayout(layout);
        m_VertexArray->AddVertexBuffer(m_VertexBuffer);

        // Index Buffer oluştur
        m_IndexBuffer = IndexBuffer::Create(m_Indices.data(), m_IndexCount);
        m_VertexArray->SetIndexBuffer(m_IndexBuffer);
    }

    void Mesh::Bind() const
    {
        m_VertexArray->Bind();
    }

    void Mesh::Unbind() const
    {
        m_VertexArray->Unbind();
    }

    void Mesh::UpdateUV2(const std::vector<glm::vec2>& newUV2)
    {
        if (newUV2.size() != m_Vertices.size()) return;

        for (size_t i = 0; i < m_Vertices.size(); i++)
            m_Vertices[i].TexCoords2 = newUV2[i];

        // GPU'ya yeniden yukle
        m_VertexBuffer->Bind();
        m_VertexBuffer->SetData(m_Vertices.data(), (uint32_t)(m_Vertices.size() * sizeof(Vertex)));
        m_VertexBuffer->Unbind();
    }

    std::shared_ptr<Mesh> Mesh::CreateCube()
    {
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;

        // Cube vertices (24 vertex - her yüz için 4)
        // Front face
        vertices.push_back({ {-1.0f, -1.0f,  1.0f}, { 0.0f,  0.0f,  1.0f}, { 1.0f,  0.0f,  0.0f}, {0.0f, 0.0f}, {0.0f, 0.0f} });
        vertices.push_back({ { 1.0f, -1.0f,  1.0f}, { 0.0f,  0.0f,  1.0f}, { 1.0f,  0.0f,  0.0f}, {1.0f, 0.0f}, {1.0f, 0.0f} });
        vertices.push_back({ { 1.0f,  1.0f,  1.0f}, { 0.0f,  0.0f,  1.0f}, { 1.0f,  0.0f,  0.0f}, {1.0f, 1.0f}, {1.0f, 1.0f} });
        vertices.push_back({ {-1.0f,  1.0f,  1.0f}, { 0.0f,  0.0f,  1.0f}, { 1.0f,  0.0f,  0.0f}, {0.0f, 1.0f}, {0.0f, 1.0f} });

        // Back face
        vertices.push_back({ { 1.0f, -1.0f, -1.0f}, { 0.0f,  0.0f, -1.0f}, {-1.0f,  0.0f,  0.0f}, {0.0f, 0.0f}, {0.0f, 0.0f} });
        vertices.push_back({ {-1.0f, -1.0f, -1.0f}, { 0.0f,  0.0f, -1.0f}, {-1.0f,  0.0f,  0.0f}, {1.0f, 0.0f}, {1.0f, 0.0f} });
        vertices.push_back({ {-1.0f,  1.0f, -1.0f}, { 0.0f,  0.0f, -1.0f}, {-1.0f,  0.0f,  0.0f}, {1.0f, 1.0f}, {1.0f, 1.0f} });
        vertices.push_back({ { 1.0f,  1.0f, -1.0f}, { 0.0f,  0.0f, -1.0f}, {-1.0f,  0.0f,  0.0f}, {0.0f, 1.0f}, {0.0f, 1.0f} });

        // Top face
        vertices.push_back({ {-1.0f,  1.0f,  1.0f}, { 0.0f,  1.0f,  0.0f}, { 1.0f,  0.0f,  0.0f}, {0.0f, 0.0f}, {0.0f, 0.0f} });
        vertices.push_back({ { 1.0f,  1.0f,  1.0f}, { 0.0f,  1.0f,  0.0f}, { 1.0f,  0.0f,  0.0f}, {1.0f, 0.0f}, {1.0f, 0.0f} });
        vertices.push_back({ { 1.0f,  1.0f, -1.0f}, { 0.0f,  1.0f,  0.0f}, { 1.0f,  0.0f,  0.0f}, {1.0f, 1.0f}, {1.0f, 1.0f} });
        vertices.push_back({ {-1.0f,  1.0f, -1.0f}, { 0.0f,  1.0f,  0.0f}, { 1.0f,  0.0f,  0.0f}, {0.0f, 1.0f}, {0.0f, 1.0f} });

        // Bottom face
        vertices.push_back({ {-1.0f, -1.0f, -1.0f}, { 0.0f, -1.0f,  0.0f}, { 1.0f,  0.0f,  0.0f}, {0.0f, 0.0f}, {0.0f, 0.0f} });
        vertices.push_back({ { 1.0f, -1.0f, -1.0f}, { 0.0f, -1.0f,  0.0f}, { 1.0f,  0.0f,  0.0f}, {1.0f, 0.0f}, {1.0f, 0.0f} });
        vertices.push_back({ { 1.0f, -1.0f,  1.0f}, { 0.0f, -1.0f,  0.0f}, { 1.0f,  0.0f,  0.0f}, {1.0f, 1.0f}, {1.0f, 1.0f} });
        vertices.push_back({ {-1.0f, -1.0f,  1.0f}, { 0.0f, -1.0f,  0.0f}, { 1.0f,  0.0f,  0.0f}, {0.0f, 1.0f}, {0.0f, 1.0f} });

        // Right face
        vertices.push_back({ { 1.0f, -1.0f,  1.0f}, { 1.0f,  0.0f,  0.0f}, { 0.0f,  0.0f, -1.0f}, {0.0f, 0.0f}, {0.0f, 0.0f} });
        vertices.push_back({ { 1.0f, -1.0f, -1.0f}, { 1.0f,  0.0f,  0.0f}, { 0.0f,  0.0f, -1.0f}, {1.0f, 0.0f}, {1.0f, 0.0f} });
        vertices.push_back({ { 1.0f,  1.0f, -1.0f}, { 1.0f,  0.0f,  0.0f}, { 0.0f,  0.0f, -1.0f}, {1.0f, 1.0f}, {1.0f, 1.0f} });
        vertices.push_back({ { 1.0f,  1.0f,  1.0f}, { 1.0f,  0.0f,  0.0f}, { 0.0f,  0.0f, -1.0f}, {0.0f, 1.0f}, {0.0f, 1.0f} });

        // Left face
        vertices.push_back({ {-1.0f, -1.0f, -1.0f}, {-1.0f,  0.0f,  0.0f}, { 0.0f,  0.0f,  1.0f}, {0.0f, 0.0f}, {0.0f, 0.0f} });
        vertices.push_back({ {-1.0f, -1.0f,  1.0f}, {-1.0f,  0.0f,  0.0f}, { 0.0f,  0.0f,  1.0f}, {1.0f, 0.0f}, {1.0f, 0.0f} });
        vertices.push_back({ {-1.0f,  1.0f,  1.0f}, {-1.0f,  0.0f,  0.0f}, { 0.0f,  0.0f,  1.0f}, {1.0f, 1.0f}, {1.0f, 1.0f} });
        vertices.push_back({ {-1.0f,  1.0f, -1.0f}, {-1.0f,  0.0f,  0.0f}, { 0.0f,  0.0f,  1.0f}, {0.0f, 1.0f}, {0.0f, 1.0f} });

        // Indices (her yüz için 2 triangle = 6 index)
        for (uint32_t i = 0; i < 6; i++)
        {
            uint32_t offset = i * 4;
            indices.push_back(offset + 0);
            indices.push_back(offset + 1);
            indices.push_back(offset + 2);
            indices.push_back(offset + 2);
            indices.push_back(offset + 3);
            indices.push_back(offset + 0);
        }

        return std::make_shared<Mesh>(vertices, indices);
    }

    std::shared_ptr<Mesh> Mesh::CreateSphere(float radius, uint32_t segments)
    {
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;

        // Sphere vertices (UV sphere)
        for (uint32_t y = 0; y <= segments; y++)
        {
            for (uint32_t x = 0; x <= segments; x++)
            {
                float xSegment = (float)x / (float)segments;
                float ySegment = (float)y / (float)segments;
                float xPos = std::cos(xSegment * 2.0f * 3.14159f) * std::sin(ySegment * 3.14159f);
                float yPos = std::cos(ySegment * 3.14159f);
                float zPos = std::sin(xSegment * 2.0f * 3.14159f) * std::sin(ySegment * 3.14159f);

                Vertex vertex;
                vertex.Position = glm::vec3(xPos, yPos, zPos) * radius;
                vertex.Normal = glm::normalize(glm::vec3(xPos, yPos, zPos));
                vertex.TexCoords = glm::vec2(xSegment, ySegment);
                vertex.TexCoords2 = glm::vec2(xSegment, ySegment);
                vertex.Tangent = glm::vec3(0.0f);
                vertices.push_back(vertex);
            }
        }

        // Sphere indices
        for (uint32_t y = 0; y < segments; y++)
        {
            for (uint32_t x = 0; x < segments; x++)
            {
                indices.push_back(y * (segments + 1) + x);
                indices.push_back((y + 1) * (segments + 1) + x);
                indices.push_back((y + 1) * (segments + 1) + x + 1);

                indices.push_back(y * (segments + 1) + x);
                indices.push_back((y + 1) * (segments + 1) + x + 1);
                indices.push_back(y * (segments + 1) + x + 1);
            }
        }

        return std::make_shared<Mesh>(vertices, indices);
    }

    std::shared_ptr<Mesh> Mesh::CreatePlane(float size, uint32_t subdivisions)
    {
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;

        float halfSize = size * 0.5f;
        float step = size / (float)subdivisions;

        for (uint32_t z = 0; z <= subdivisions; z++)
        {
            for (uint32_t x = 0; x <= subdivisions; x++)
            {
                float xPos = -halfSize + (float)x * step;
                float zPos = -halfSize + (float)z * step;
                float u = (float)x / (float)subdivisions;
                float v = (float)z / (float)subdivisions;

                Vertex vertex;
                vertex.Position = glm::vec3(xPos, 0.0f, zPos);
                vertex.Normal = glm::vec3(0.0f, 1.0f, 0.0f);
                vertex.Tangent = glm::vec3(1.0f, 0.0f, 0.0f);
                vertex.TexCoords = glm::vec2(u, v);
                vertex.TexCoords2 = glm::vec2(u, v);
                vertices.push_back(vertex);
            }
        }

        for (uint32_t z = 0; z < subdivisions; z++)
        {
            for (uint32_t x = 0; x < subdivisions; x++)
            {
                uint32_t topLeft = z * (subdivisions + 1) + x;
                uint32_t topRight = topLeft + 1;
                uint32_t bottomLeft = (z + 1) * (subdivisions + 1) + x;
                uint32_t bottomRight = bottomLeft + 1;

                indices.push_back(topLeft);
                indices.push_back(bottomLeft);
                indices.push_back(topRight);

                indices.push_back(topRight);
                indices.push_back(bottomLeft);
                indices.push_back(bottomRight);
            }
        }

        return std::make_shared<Mesh>(vertices, indices);
    }

    std::shared_ptr<Mesh> Mesh::CreateCylinder(float radius, float height, uint32_t segments)
    {
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;

        float halfHeight = height * 0.5f;

        // Yan yüzey
        for (uint32_t i = 0; i <= segments; i++)
        {
            float angle = (float)i / (float)segments * 2.0f * 3.14159f;
            float x = std::cos(angle) * radius;
            float z = std::sin(angle) * radius;
            glm::vec3 normal = glm::normalize(glm::vec3(x, 0.0f, z));
            float u = (float)i / (float)segments;

            Vertex bottom;
            bottom.Position = glm::vec3(x, -halfHeight, z);
            bottom.Normal = normal;
            bottom.Tangent = glm::vec3(0.0f);
            bottom.TexCoords = glm::vec2(u, 0.0f);
            bottom.TexCoords2 = glm::vec2(u, 0.0f);
            vertices.push_back(bottom);

            Vertex top;
            top.Position = glm::vec3(x, halfHeight, z);
            top.Normal = normal;
            top.Tangent = glm::vec3(0.0f);
            top.TexCoords = glm::vec2(u, 1.0f);
            top.TexCoords2 = glm::vec2(u, 1.0f);
            vertices.push_back(top);
        }

        for (uint32_t i = 0; i < segments; i++)
        {
            uint32_t current = i * 2;
            uint32_t next = (i + 1) * 2;

            indices.push_back(current);
            indices.push_back(next);
            indices.push_back(current + 1);

            indices.push_back(current + 1);
            indices.push_back(next);
            indices.push_back(next + 1);
        }

        // Üst kapak
        uint32_t topCenter = (uint32_t)vertices.size();
        Vertex tc;
        tc.Position = glm::vec3(0.0f, halfHeight, 0.0f);
        tc.Normal = glm::vec3(0.0f, 1.0f, 0.0f);
        tc.Tangent = glm::vec3(1.0f, 0.0f, 0.0f);
        tc.TexCoords = glm::vec2(0.5f, 0.5f);
        tc.TexCoords2 = glm::vec2(0.5f, 0.5f);
        vertices.push_back(tc);

        for (uint32_t i = 0; i <= segments; i++)
        {
            float angle = (float)i / (float)segments * 2.0f * 3.14159f;
            Vertex v;
            v.Position = glm::vec3(std::cos(angle) * radius, halfHeight, std::sin(angle) * radius);
            v.Normal = glm::vec3(0.0f, 1.0f, 0.0f);
            v.Tangent = glm::vec3(1.0f, 0.0f, 0.0f);
            v.TexCoords = glm::vec2(std::cos(angle) * 0.5f + 0.5f, std::sin(angle) * 0.5f + 0.5f);
            v.TexCoords2 = v.TexCoords;
            vertices.push_back(v);
        }

        for (uint32_t i = 0; i < segments; i++)
        {
            indices.push_back(topCenter);
            indices.push_back(topCenter + 1 + i);
            indices.push_back(topCenter + 1 + i + 1);
        }

        // Alt kapak
        uint32_t botCenter = (uint32_t)vertices.size();
        Vertex bc;
        bc.Position = glm::vec3(0.0f, -halfHeight, 0.0f);
        bc.Normal = glm::vec3(0.0f, -1.0f, 0.0f);
        bc.Tangent = glm::vec3(1.0f, 0.0f, 0.0f);
        bc.TexCoords = glm::vec2(0.5f, 0.5f);
        bc.TexCoords2 = glm::vec2(0.5f, 0.5f);
        vertices.push_back(bc);

        for (uint32_t i = 0; i <= segments; i++)
        {
            float angle = (float)i / (float)segments * 2.0f * 3.14159f;
            Vertex v;
            v.Position = glm::vec3(std::cos(angle) * radius, -halfHeight, std::sin(angle) * radius);
            v.Normal = glm::vec3(0.0f, -1.0f, 0.0f);
            v.Tangent = glm::vec3(1.0f, 0.0f, 0.0f);
            v.TexCoords = glm::vec2(std::cos(angle) * 0.5f + 0.5f, std::sin(angle) * 0.5f + 0.5f);
            v.TexCoords2 = v.TexCoords;
            vertices.push_back(v);
        }

        for (uint32_t i = 0; i < segments; i++)
        {
            indices.push_back(botCenter);
            indices.push_back(botCenter + 1 + i + 1);
            indices.push_back(botCenter + 1 + i);
        }

        return std::make_shared<Mesh>(vertices, indices);
    }

    std::shared_ptr<Mesh> Mesh::CreateCone(float radius, float height, uint32_t segments)
    {
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;

        // Tepe noktası
        Vertex apex;
        apex.Position = glm::vec3(0.0f, height, 0.0f);
        apex.Normal = glm::vec3(0.0f, 1.0f, 0.0f);
        apex.Tangent = glm::vec3(1.0f, 0.0f, 0.0f);
        apex.TexCoords = glm::vec2(0.5f, 1.0f);
        apex.TexCoords2 = glm::vec2(0.5f, 1.0f);
        vertices.push_back(apex);

        // Taban merkezi
        Vertex baseCenter;
        baseCenter.Position = glm::vec3(0.0f, 0.0f, 0.0f);
        baseCenter.Normal = glm::vec3(0.0f, -1.0f, 0.0f);
        baseCenter.Tangent = glm::vec3(1.0f, 0.0f, 0.0f);
        baseCenter.TexCoords = glm::vec2(0.5f, 0.0f);
        baseCenter.TexCoords2 = glm::vec2(0.5f, 0.0f);
        vertices.push_back(baseCenter);

        // Taban çevresi
        for (uint32_t i = 0; i <= segments; i++)
        {
            float angle = (float)i / (float)segments * 2.0f * 3.14159f;
            float x = std::cos(angle) * radius;
            float z = std::sin(angle) * radius;

            // Yan yüzey için vertex
            Vertex sideVertex;
            sideVertex.Position = glm::vec3(x, 0.0f, z);
            glm::vec3 toApex = glm::normalize(apex.Position - sideVertex.Position);
            glm::vec3 radial = glm::normalize(glm::vec3(x, 0.0f, z));
            sideVertex.Normal = glm::normalize(glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), radial) + toApex);
            sideVertex.Tangent = glm::vec3(0.0f);
            sideVertex.TexCoords = glm::vec2((float)i / (float)segments, 0.0f);
            sideVertex.TexCoords2 = sideVertex.TexCoords;
            vertices.push_back(sideVertex);

            // Taban için vertex
            Vertex baseVertex;
            baseVertex.Position = glm::vec3(x, 0.0f, z);
            baseVertex.Normal = glm::vec3(0.0f, -1.0f, 0.0f);
            baseVertex.Tangent = glm::vec3(1.0f, 0.0f, 0.0f);
            baseVertex.TexCoords = glm::vec2((float)i / (float)segments, 0.0f);
            baseVertex.TexCoords2 = baseVertex.TexCoords;
            vertices.push_back(baseVertex);
        }

        // Yan yüzey üçgenleri (apex'ten taban'a)
        for (uint32_t i = 0; i < segments; i++)
        {
            uint32_t current = 2 + i * 2;
            uint32_t next = 2 + (i + 1) * 2;
            
            indices.push_back(0);      // Apex
            indices.push_back(current);
            indices.push_back(next);
        }

        // Taban üçgenleri
        for (uint32_t i = 0; i < segments; i++)
        {
            uint32_t current = 3 + i * 2;
            uint32_t next = 3 + (i + 1) * 2;
            
            indices.push_back(1);      // Base center
            indices.push_back(next);
            indices.push_back(current);
        }

        return std::make_shared<Mesh>(vertices, indices);
    }

    std::shared_ptr<Mesh> Mesh::CreateArrow(float shaftRadius, float shaftLength, float headRadius, float headLength, uint32_t segments)
    {
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;

        // Shaft (silindir)
        for (uint32_t i = 0; i <= segments; i++)
        {
            float angle = (float)i / (float)segments * 2.0f * 3.14159f;
            float x = std::cos(angle) * shaftRadius;
            float z = std::sin(angle) * shaftRadius;
            glm::vec3 normal = glm::normalize(glm::vec3(x, 0.0f, z));

            // Alt
            Vertex bottom;
            bottom.Position = glm::vec3(x, 0.0f, z);
            bottom.Normal = normal;
            bottom.Tangent = glm::vec3(0.0f);
            bottom.TexCoords = glm::vec2((float)i / (float)segments, 0.0f);
            bottom.TexCoords2 = bottom.TexCoords;
            vertices.push_back(bottom);

            // Üst
            Vertex top;
            top.Position = glm::vec3(x, shaftLength, z);
            top.Normal = normal;
            top.Tangent = glm::vec3(0.0f);
            top.TexCoords = glm::vec2((float)i / (float)segments, 1.0f);
            top.TexCoords2 = top.TexCoords;
            vertices.push_back(top);
        }

        // Shaft üçgenleri
        for (uint32_t i = 0; i < segments; i++)
        {
            uint32_t current = i * 2;
            uint32_t next = (i + 1) * 2;

            indices.push_back(current);
            indices.push_back(next);
            indices.push_back(current + 1);

            indices.push_back(current + 1);
            indices.push_back(next);
            indices.push_back(next + 1);
        }

        uint32_t headBaseStart = (uint32_t)vertices.size();

        // Head (koni)
        Vertex headApex;
        headApex.Position = glm::vec3(0.0f, shaftLength + headLength, 0.0f);
        headApex.Normal = glm::vec3(0.0f, 1.0f, 0.0f);
        headApex.Tangent = glm::vec3(1.0f, 0.0f, 0.0f);
        headApex.TexCoords = glm::vec2(0.5f, 1.0f);
        headApex.TexCoords2 = glm::vec2(0.5f, 1.0f);
        vertices.push_back(headApex);

        for (uint32_t i = 0; i <= segments; i++)
        {
            float angle = (float)i / (float)segments * 2.0f * 3.14159f;
            float x = std::cos(angle) * headRadius;
            float z = std::sin(angle) * headRadius;

            Vertex headBase;
            headBase.Position = glm::vec3(x, shaftLength, z);
            glm::vec3 toApex = glm::normalize(headApex.Position - headBase.Position);
            glm::vec3 radial = glm::normalize(glm::vec3(x, 0.0f, z));
            headBase.Normal = glm::normalize(glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), radial) + toApex);
            headBase.Tangent = glm::vec3(0.0f);
            headBase.TexCoords = glm::vec2((float)i / (float)segments, 0.0f);
            headBase.TexCoords2 = headBase.TexCoords;
            vertices.push_back(headBase);
        }

        // Head üçgenleri
        for (uint32_t i = 0; i < segments; i++)
        {
            uint32_t current = headBaseStart + 1 + i;
            uint32_t next = headBaseStart + 1 + (i + 1);

            indices.push_back(headBaseStart); // Apex
            indices.push_back(current);
            indices.push_back(next);
        }

        return std::make_shared<Mesh>(vertices, indices);
    }
}
