#include "NavMesh.h"
#include <limits>
#include <cmath>
#include <map>
#include "Scene/Scene.h"
#include "Scene/Components.h"
#include "Renderer/Utilities/Renderer3D/Mesh.h"

namespace Conqueror
{
    void NavMesh::AddTriangle(const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2, int areaType)
    {
        NavTriangle tri;
        tri.Vertices[0] = v0;
        tri.Vertices[1] = v1;
        tri.Vertices[2] = v2;
        tri.Center = (v0 + v1 + v2) / 3.0f;
        tri.AreaType = areaType;

        // Normal hesabı
        glm::vec3 edge1 = v1 - v0;
        glm::vec3 edge2 = v2 - v0;
        tri.Normal = glm::normalize(glm::cross(edge1, edge2));

        // Alan hesabı (Area = 0.5 * ||edge1 x edge2||)
        tri.Area = 0.5f * glm::length(glm::cross(edge1, edge2));

        m_Triangles.push_back(tri);
    }

    void NavMesh::BuildConnectivity()
    {
        // Basit N^2 komşuluk bulma (ortak kenar var mı?)
        // Gerçek bir sistemde half-edge veri yapısı veya uzaysal bölme kullanılır.
        for (size_t i = 0; i < m_Triangles.size(); ++i)
        {
            for (size_t j = i + 1; j < m_Triangles.size(); ++j)
            {
                int sharedVertices = 0;
                
                for (int v1 = 0; v1 < 3; ++v1)
                {
                    for (int v2 = 0; v2 < 3; ++v2)
                    {
                        if (glm::distance(m_Triangles[i].Vertices[v1], m_Triangles[j].Vertices[v2]) < 0.01f)
                        {
                            sharedVertices++;
                        }
                    }
                }

                if (sharedVertices >= 2) // En az iki nokta (bir kenar) ortak
                {
                    // Komşuları boş slotlara ekle
                    for (int k = 0; k < 3; ++k)
                    {
                        if (m_Triangles[i].Neighbors[k] == -1)
                        {
                            m_Triangles[i].Neighbors[k] = (int)j;
                            break;
                        }
                    }

                    for (int k = 0; k < 3; ++k)
                    {
                        if (m_Triangles[j].Neighbors[k] == -1)
                        {
                            m_Triangles[j].Neighbors[k] = (int)i;
                            break;
                        }
                    }
                }
            }
        }
    }

    int NavMesh::FindClosestTriangle(const glm::vec3& position) const
    {
        if (m_Triangles.empty()) return -1;

        int closestIdx = -1;
        float minDistance = std::numeric_limits<float>::max();

        for (size_t i = 0; i < m_Triangles.size(); ++i)
        {
            float dist = glm::distance(m_Triangles[i].Center, position);
            if (dist < minDistance)
            {
                minDistance = dist;
                closestIdx = (int)i;
            }
        }

        return closestIdx;
    }

    void NavMesh::Clear()
    {
        m_Triangles.clear();
    }

    // Gerçek geometri çıkarma ve NavMesh Bake işlemi (Özel Voxelizer / Geometri Filtreleyici)
    void NavMeshBuilder::Bake(class Scene* scene, NavMesh& outNavMesh)
    {
        outNavMesh.Clear();
        if (!scene) return;

        // Yürünebilirlik açısı (Örn: 45 dereceden dik yokuşlara çıkılamaz)
        float maxWalkableSlopeAngle = 45.0f;
        float minWalkableCos = glm::cos(glm::radians(maxWalkableSlopeAngle));

        auto view = scene->m_Registry.view<TransformComponent, NavMeshSurfaceComponent>();
        
        for (auto entity : view)
        {
            auto [transformComp, surfaceComp] = view.get<TransformComponent, NavMeshSurfaceComponent>(entity);
            
            // Eğer yüzey yürünemez işaretlendiyse atla
            if (surfaceComp.AreaType == 1) // 1: Not Walkable
                continue;

            glm::mat4 transform = transformComp.GetTransform();
            
            // Entitiy'de MeshRenderer varsa (Küp)
            if (scene->m_Registry.all_of<MeshRendererComponent>(entity))
            {
                auto cubeMesh = Mesh::CreateCube();
                const auto& vertices = cubeMesh->GetVertices();
                const auto& indices = cubeMesh->GetIndices();

                for (size_t i = 0; i < indices.size(); i += 3)
                {
                    // Yerel pozisyonları dünya pozisyonuna çevir
                    glm::vec3 v0 = glm::vec3(transform * glm::vec4(vertices[indices[i]].Position, 1.0f));
                    glm::vec3 v1 = glm::vec3(transform * glm::vec4(vertices[indices[i+1]].Position, 1.0f));
                    glm::vec3 v2 = glm::vec3(transform * glm::vec4(vertices[indices[i+2]].Position, 1.0f));

                    // Üçgenin dünya uzayındaki normalini hesapla
                    glm::vec3 edge1 = v1 - v0;
                    glm::vec3 edge2 = v2 - v0;
                    glm::vec3 normal = glm::normalize(glm::cross(edge1, edge2));

                    // Eğer yüzey yukarıya bakıyorsa (Eğim yürünebilir mi?)
                    // Yukarı yön: (0, 1, 0)
                    float slopeCos = glm::dot(normal, glm::vec3(0.0f, 1.0f, 0.0f));
                    
                    if (slopeCos >= minWalkableCos)
                    {
                        // Bu üçgen yürünebilir! NavMesh'e ekle
                        outNavMesh.AddTriangle(v0, v1, v2, surfaceComp.AreaType);
                    }
                }
            }
            
            // (İleride ModelComponent için de benzer döngü eklenecek, şu anlık temel geometri hazır)
        }

        // Biriken tüm üçgenler arasında komşuluk bağlantılarını kur
        outNavMesh.BuildConnectivity();
        
        CQ_CORE_INFO("NavMesh Baked Successfully! Total Triangles: {0}", outNavMesh.GetTriangles().size());
    }
}
