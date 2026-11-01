#include "ClothMesh.h"
#include "Scene/Scene.h"
#include "Scene/Components.h"
#include "Scene/PhysicsComponents.h"
#include "Renderer/Utilities/Renderer3D/Mesh.h"

#include <algorithm>
#include <cstring>

namespace Conqueror
{
    namespace ClothMesh
    {
        void BuildGrid(const ClothComponent& c, std::vector<glm::vec3>& local, std::vector<uint32_t>& tris, std::vector<glm::vec2>& uvs, std::vector<uint32_t>& pinned)
        {
            const int n = std::clamp(c.Resolution, 2, 128);
            local.clear();
            tris.clear();
            uvs.clear();
            pinned.clear();
            const glm::vec2 half = glm::max(c.Size, glm::vec2(0.01f)) * 0.5f;
            for (int j = 0; j <= n; ++j)
                for (int i = 0; i <= n; ++i)
                {
                    const float u = (float)i / (float)n, v = (float)j / (float)n;
                    const float x = -half.x + u * 2.0f * half.x, y = -half.y + v * 2.0f * half.y;
                    // Dikey: XY düzlemi (+Y üst kenar); yatay: XZ düzlemi (-Z "üst" kenar)
                    local.push_back(c.Orientation == ClothOrientation::Vertical ? glm::vec3(x, y, 0.0f) : glm::vec3(x, 0.0f, -y));
                    uvs.emplace_back(u, v);
                    const bool top = j == n, left = i == 0, right = i == n, bottom = j == 0;
                    bool pin = false;
                    switch (c.Pin)
                    {
                        case ClothPin::TopEdge: pin = top; break;
                        case ClothPin::TopCorners: pin = top && (left || right); break;
                        case ClothPin::LeftEdge: pin = left; break;
                        case ClothPin::AllCorners: pin = (top || bottom) && (left || right); break;
                        default: break;
                    }
                    if (pin)
                        pinned.push_back((uint32_t)(j * (n + 1) + i));
                }
            for (int j = 0; j < n; ++j)
                for (int i = 0; i < n; ++i)
                {
                    const uint32_t a = (uint32_t)(j * (n + 1) + i), b = a + 1, d = a + (uint32_t)(n + 1), e = d + 1;
                    // Ön yüz +Z (dikey) / +Y (yatay)
                    tris.insert(tris.end(), { a, b, e, a, e, d });
                }
        }

        static void FillVertices(std::vector<Vertex>& out, const std::vector<glm::vec3>& local, const std::vector<uint32_t>& tris, const std::vector<glm::vec2>* uvs)
        {
            const size_t n = local.size();
            std::vector<glm::vec3> normals(n, glm::vec3(0.0f));
            for (size_t t = 0; t + 2 < tris.size(); t += 3)
            {
                const glm::vec3 fn = glm::cross(local[tris[t + 1]] - local[tris[t]], local[tris[t + 2]] - local[tris[t]]);
                normals[tris[t]] += fn;
                normals[tris[t + 1]] += fn;
                normals[tris[t + 2]] += fn;
            }
            out.resize(n * 2);
            for (size_t i = 0; i < n; ++i)
            {
                glm::vec3 nrm = glm::length(normals[i]) > 1e-12f ? glm::normalize(normals[i]) : glm::vec3(0, 0, 1);
                Vertex& f = out[i];
                Vertex& b = out[i + n];
                f.Position = b.Position = local[i];
                f.Normal = nrm;
                b.Normal = -nrm;
                glm::vec3 helper = std::abs(nrm.y) < 0.9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
                f.Tangent = b.Tangent = glm::normalize(glm::cross(helper, nrm));
                if (uvs)
                {
                    f.TexCoords = b.TexCoords = (*uvs)[i];
                    f.TexCoords2 = b.TexCoords2 = (*uvs)[i];
                }
            }
        }

        std::shared_ptr<Mesh> CreateMesh(const std::vector<glm::vec3>& local, const std::vector<uint32_t>& tris, const std::vector<glm::vec2>& uvs)
        {
            std::vector<Vertex> verts;
            FillVertices(verts, local, tris, &uvs);
            const uint32_t n = (uint32_t)local.size();
            std::vector<uint32_t> idx = tris;
            // Arka katman: ters sargı
            for (size_t t = 0; t + 2 < tris.size(); t += 3)
                idx.insert(idx.end(), { tris[t] + n, tris[t + 2] + n, tris[t + 1] + n });
            return std::make_shared<Mesh>(verts, idx);
        }

        void UpdateMesh(Mesh& mesh, const std::vector<glm::vec3>& local, const std::vector<uint32_t>& tris)
        {
            if (mesh.GetVertexCount() != local.size() * 2)
                return;
            static thread_local std::vector<Vertex> verts;
            verts = mesh.GetVertices();
            std::vector<Vertex> fresh;
            FillVertices(fresh, local, tris, nullptr);
            for (size_t i = 0; i < verts.size(); ++i)
            {
                verts[i].Position = fresh[i].Position;
                verts[i].Normal = fresh[i].Normal;
                verts[i].Tangent = fresh[i].Tangent;
            }
            mesh.UpdateVertices(verts.data(), 0, (uint32_t)verts.size());
        }

        uint64_t Signature(const ClothComponent& c)
        {
            uint64_t h = 1469598103934665603ull;
            auto mix = [&](const void* p, size_t n) {
                const auto* b = (const unsigned char*)p;
                for (size_t i = 0; i < n; ++i) h = (h ^ b[i]) * 1099511628211ull;
            };
            mix(&c.Size, sizeof(c.Size));
            mix(&c.Resolution, sizeof(c.Resolution));
            mix(&c.Orientation, sizeof(c.Orientation));
            return h;
        }

        void EnsureRestMeshes(Scene& scene)
        {
            auto view = scene.m_Registry.view<ClothComponent, MeshRendererComponent>();
            for (auto e : view)
            {
                auto& c = view.get<ClothComponent>(e);
                auto& mr = view.get<MeshRendererComponent>(e);
                const uint64_t sig = Signature(c);
                if (mr.CustomMesh && c.BuiltSignature == sig)
                    continue;
                std::vector<glm::vec3> local;
                std::vector<uint32_t> tris, pinned;
                std::vector<glm::vec2> uvs;
                BuildGrid(c, local, tris, uvs, pinned);
                mr.CustomMesh = CreateMesh(local, tris, uvs);
                c.BuiltSignature = sig;
            }
        }
    }
}
