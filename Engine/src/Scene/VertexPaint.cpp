#include "VertexPaint.h"
#include "Scene/Scene.h"
#include "Scene/Entity.h"
#include "Scene/Components.h"
#include "Scene/ComponentRegistry.h"
#include "Renderer/Utilities/Renderer3D/Renderer3D.h"
#include "Renderer/Utilities/Renderer3D/ModelLoader.h"

#include <IconsFontAwesome6.h>
#include <glad/glad.h>

#include <algorithm>
#include <cmath>
#include <sstream>

namespace Conqueror
{
    struct VertexPaintRuntime
    {
        std::vector<std::vector<uint32_t>> Colors;   // mesh başına RGBA8 (paketli)
        std::vector<const Mesh*> Meshes;
        std::vector<uint32_t> Buffers;
        uint32_t Revision = 0;
        bool GPUDirty = true;

        ~VertexPaintRuntime()
        {
            if (!Buffers.empty())
                glDeleteBuffers((GLsizei)Buffers.size(), Buffers.data());
        }
    };

    namespace
    {
        constexpr uint32_t kWhite = 0xFFFFFFFFu;

        const char* kB64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

        std::string EncodeBase64(const uint8_t* data, size_t size)
        {
            std::string out;
            out.reserve((size + 2) / 3 * 4);
            for (size_t i = 0; i < size; i += 3)
            {
                uint32_t v = (uint32_t)data[i] << 16;
                if (i + 1 < size) v |= (uint32_t)data[i + 1] << 8;
                if (i + 2 < size) v |= data[i + 2];
                out.push_back(kB64[(v >> 18) & 63]);
                out.push_back(kB64[(v >> 12) & 63]);
                out.push_back(i + 1 < size ? kB64[(v >> 6) & 63] : '=');
                out.push_back(i + 2 < size ? kB64[v & 63] : '=');
            }
            return out;
        }

        std::vector<uint8_t> DecodeBase64(const std::string& text)
        {
            int table[256];
            std::fill(std::begin(table), std::end(table), -1);
            for (int i = 0; i < 64; ++i)
                table[(unsigned char)kB64[i]] = i;
            std::vector<uint8_t> out;
            out.reserve(text.size() * 3 / 4);
            uint32_t buffer = 0;
            int bits = 0;
            for (char ch : text)
            {
                const int v = table[(unsigned char)ch];
                if (v < 0)
                    continue;
                buffer = (buffer << 6) | (uint32_t)v;
                bits += 6;
                if (bits >= 8)
                {
                    bits -= 8;
                    out.push_back((uint8_t)((buffer >> bits) & 0xFF));
                }
            }
            return out;
        }

        uint32_t Pack(const glm::vec4& c)
        {
            auto b = [](float f) { return (uint32_t)std::lround(std::clamp(f, 0.0f, 1.0f) * 255.0f); };
            return b(c.r) | (b(c.g) << 8) | (b(c.b) << 16) | (b(c.a) << 24);
        }

        glm::vec4 Unpack(uint32_t v)
        {
            return glm::vec4((float)(v & 0xFF), (float)((v >> 8) & 0xFF), (float)((v >> 16) & 0xFF), (float)((v >> 24) & 0xFF)) / 255.0f;
        }

        std::string Encode(const std::vector<std::vector<uint32_t>>& colors)
        {
            std::ostringstream ss;
            ss << "vp1|";
            std::vector<uint8_t> bytes;
            for (size_t i = 0; i < colors.size(); ++i)
            {
                ss << (i ? "," : "") << colors[i].size();
                for (uint32_t c : colors[i])
                {
                    bytes.push_back((uint8_t)(c & 0xFF));
                    bytes.push_back((uint8_t)((c >> 8) & 0xFF));
                    bytes.push_back((uint8_t)((c >> 16) & 0xFF));
                    bytes.push_back((uint8_t)((c >> 24) & 0xFF));
                }
            }
            ss << "|" << EncodeBase64(bytes.data(), bytes.size());
            return ss.str();
        }

        // Veri -> mesh başına renkler (eksikler beyaz; köşe sayısı değişmiş meshler sıfırlanır)
        void Decode(const std::string& data, const std::vector<std::shared_ptr<Mesh>>& meshes, std::vector<std::vector<uint32_t>>& out)
        {
            out.assign(meshes.size(), {});
            for (size_t i = 0; i < meshes.size(); ++i)
                out[i].assign(meshes[i] ? meshes[i]->GetVertexCount() : 0, kWhite);
            if (data.rfind("vp1|", 0) != 0)
                return;
            const size_t bar = data.find('|', 4);
            if (bar == std::string::npos)
                return;
            std::vector<size_t> counts;
            std::stringstream ss(data.substr(4, bar - 4));
            std::string item;
            while (std::getline(ss, item, ','))
                counts.push_back((size_t)std::strtoull(item.c_str(), nullptr, 10));
            const std::vector<uint8_t> bytes = DecodeBase64(data.substr(bar + 1));
            size_t offset = 0;
            for (size_t m = 0; m < counts.size(); ++m)
            {
                const size_t n = counts[m];
                if (m < out.size() && out[m].size() == n && offset + n * 4 <= bytes.size())
                    for (size_t v = 0; v < n; ++v)
                    {
                        const uint8_t* p = &bytes[offset + v * 4];
                        out[m][v] = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
                    }
                offset += n * 4;
            }
        }

        // CPU tarafı: renkler güncel, meshler aynı
        VertexPaintRuntime& EnsureRuntime(VertexPaintComponent& comp, const std::vector<std::shared_ptr<Mesh>>& meshes)
        {
            if (!comp.Runtime)
                comp.Runtime = std::make_shared<VertexPaintRuntime>();
            VertexPaintRuntime& rt = *comp.Runtime;
            bool sameMeshes = rt.Meshes.size() == meshes.size();
            for (size_t i = 0; sameMeshes && i < meshes.size(); ++i)
                sameMeshes = rt.Meshes[i] == meshes[i].get();
            if (rt.Revision != comp.Revision || !sameMeshes)
            {
                Decode(comp.Data, meshes, rt.Colors);
                rt.Meshes.clear();
                for (auto& m : meshes)
                    rt.Meshes.push_back(m.get());
                rt.Revision = comp.Revision;
                rt.GPUDirty = true;
            }
            return rt;
        }

        void Commit(VertexPaintComponent& comp, VertexPaintRuntime& rt)
        {
            comp.Data = Encode(rt.Colors);
            comp.Revision++;
            rt.Revision = comp.Revision;
            rt.GPUDirty = true;
        }

        bool RayTriangle(const glm::vec3& o, const glm::vec3& d, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, float& t)
        {
            const glm::vec3 e1 = b - a, e2 = c - a;
            const glm::vec3 p = glm::cross(d, e2);
            const float det = glm::dot(e1, p);
            if (std::abs(det) < 1e-12f)
                return false;
            const float inv = 1.0f / det;
            const glm::vec3 s = o - a;
            const float u = glm::dot(s, p) * inv;
            if (u < 0.0f || u > 1.0f)
                return false;
            const glm::vec3 q = glm::cross(s, e1);
            const float v = glm::dot(d, q) * inv;
            if (v < 0.0f || u + v > 1.0f)
                return false;
            t = glm::dot(e2, q) * inv;
            return t > 1e-6f;
        }
    }

    namespace VertexPaint
    {
        std::vector<std::shared_ptr<Mesh>> MeshesOf(Entity entity)
        {
            std::vector<std::shared_ptr<Mesh>> out;
            if (!entity)
                return out;
            if (entity.HasComponent<ModelComponent>())
            {
                auto& mc = entity.GetComponent<ModelComponent>();
                if (mc.ModelData && !mc.ModelData->IsSkinned)
                    for (auto& m : mc.ModelData->Meshes)
                        out.push_back(m);
                return out;
            }
            if (entity.HasComponent<MeshRendererComponent>())
            {
                auto& mr = entity.GetComponent<MeshRendererComponent>();
                std::shared_ptr<Mesh> mesh = mr.CustomMesh;
                if (!mesh)
                {
                    switch (mr.Type)
                    {
                        case MeshType::Sphere: mesh = Renderer3D::GetSphereMesh(); break;
                        case MeshType::Plane: mesh = Renderer3D::GetPlaneMesh(); break;
                        case MeshType::Cylinder: mesh = Renderer3D::GetCylinderMesh(); break;
                        default: mesh = Renderer3D::GetCubeMesh(); break;
                    }
                }
                if (mesh)
                    out.push_back(mesh);
            }
            return out;
        }

        const std::vector<uint32_t>* Prepare(Entity entity)
        {
            if (!entity || !entity.HasComponent<VertexPaintComponent>())
                return nullptr;
            auto& comp = entity.GetComponent<VertexPaintComponent>();
            if (!comp.Enabled || comp.Data.empty())
                return nullptr;
            const auto meshes = MeshesOf(entity);
            if (meshes.empty())
                return nullptr;
            VertexPaintRuntime& rt = EnsureRuntime(comp, meshes);
            if (rt.GPUDirty)
            {
                if (rt.Buffers.size() != rt.Colors.size())
                {
                    if (!rt.Buffers.empty())
                        glDeleteBuffers((GLsizei)rt.Buffers.size(), rt.Buffers.data());
                    rt.Buffers.assign(rt.Colors.size(), 0);
                    glGenBuffers((GLsizei)rt.Buffers.size(), rt.Buffers.data());
                }
                for (size_t i = 0; i < rt.Colors.size(); ++i)
                {
                    glBindBuffer(GL_ARRAY_BUFFER, rt.Buffers[i]);
                    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(rt.Colors[i].size() * 4), rt.Colors[i].data(), GL_DYNAMIC_DRAW);
                }
                glBindBuffer(GL_ARRAY_BUFFER, 0);
                rt.GPUDirty = false;
            }
            return &rt.Buffers;
        }

        int PaintSphere(Entity entity, const glm::vec3& worldCenter, float radius, const glm::vec4& color, float strength, float falloff, uint32_t channelMask)
        {
            if (!entity || radius <= 0.0f)
                return 0;
            if (!entity.HasComponent<VertexPaintComponent>())
                entity.AddComponent<VertexPaintComponent>();
            auto& comp = entity.GetComponent<VertexPaintComponent>();
            const auto meshes = MeshesOf(entity);
            if (meshes.empty())
                return 0;
            VertexPaintRuntime& rt = EnsureRuntime(comp, meshes);
            const glm::mat4& world = entity.GetComponent<TransformComponent>().WorldTransform;
            const float inner = radius * std::clamp(1.0f - falloff, 0.0f, 1.0f);
            int painted = 0;
            for (size_t m = 0; m < meshes.size(); ++m)
            {
                const auto& verts = meshes[m]->GetVertices();
                auto& colors = rt.Colors[m];
                for (size_t v = 0; v < verts.size() && v < colors.size(); ++v)
                {
                    const glm::vec3 wp = glm::vec3(world * glm::vec4(verts[v].Position, 1.0f));
                    const float d = glm::distance(wp, worldCenter);
                    if (d > radius)
                        continue;
                    float w = std::clamp(strength, 0.0f, 1.0f);
                    if (d > inner && radius > inner)
                        w *= 1.0f - glm::smoothstep(inner, radius, d);
                    if (w <= 0.0f)
                        continue;
                    glm::vec4 c = Unpack(colors[v]);
                    for (int ch = 0; ch < 4; ++ch)
                        if (channelMask & (1u << ch))
                            c[ch] = c[ch] + (color[ch] - c[ch]) * w;
                    colors[v] = Pack(c);
                    ++painted;
                }
            }
            if (painted)
                Commit(comp, rt);
            return painted;
        }

        void Fill(Entity entity, const glm::vec4& color)
        {
            if (!entity)
                return;
            if (!entity.HasComponent<VertexPaintComponent>())
                entity.AddComponent<VertexPaintComponent>();
            auto& comp = entity.GetComponent<VertexPaintComponent>();
            const auto meshes = MeshesOf(entity);
            VertexPaintRuntime& rt = EnsureRuntime(comp, meshes);
            const uint32_t packed = Pack(color);
            for (auto& colors : rt.Colors)
                std::fill(colors.begin(), colors.end(), packed);
            Commit(comp, rt);
        }

        void SetColors(Entity entity, int meshIndex, const std::vector<glm::vec4>& colors)
        {
            if (!entity)
                return;
            if (!entity.HasComponent<VertexPaintComponent>())
                entity.AddComponent<VertexPaintComponent>();
            auto& comp = entity.GetComponent<VertexPaintComponent>();
            const auto meshes = MeshesOf(entity);
            VertexPaintRuntime& rt = EnsureRuntime(comp, meshes);
            if (meshIndex < 0 || meshIndex >= (int)rt.Colors.size())
                return;
            auto& dst = rt.Colors[(size_t)meshIndex];
            for (size_t v = 0; v < dst.size() && v < colors.size(); ++v)
                dst[v] = Pack(colors[v]);
            Commit(comp, rt);
        }

        glm::vec4 GetColor(Entity entity, int meshIndex, int vertex)
        {
            if (!entity || !entity.HasComponent<VertexPaintComponent>())
                return glm::vec4(1.0f);
            auto& comp = entity.GetComponent<VertexPaintComponent>();
            const auto meshes = MeshesOf(entity);
            VertexPaintRuntime& rt = EnsureRuntime(comp, meshes);
            if (meshIndex < 0 || meshIndex >= (int)rt.Colors.size() || vertex < 0 || vertex >= (int)rt.Colors[(size_t)meshIndex].size())
                return glm::vec4(1.0f);
            return Unpack(rt.Colors[(size_t)meshIndex][(size_t)vertex]);
        }

        bool Raycast(Entity entity, const glm::vec3& origin, const glm::vec3& direction, float& outDistance, glm::vec3& outNormal)
        {
            const auto meshes = MeshesOf(entity);
            if (meshes.empty())
                return false;
            const glm::mat4& world = entity.GetComponent<TransformComponent>().WorldTransform;
            const glm::mat4 inv = glm::inverse(world);
            const glm::vec3 lo = glm::vec3(inv * glm::vec4(origin, 1.0f));
            const glm::vec3 ld = glm::vec3(inv * glm::vec4(direction, 0.0f));
            float best = 1e30f;
            glm::vec3 bestNormal(0.0f, 1.0f, 0.0f);
            bool hit = false;
            for (const auto& mesh : meshes)
            {
                const auto& verts = mesh->GetVertices();
                const auto& idx = mesh->GetIndices();
                for (size_t i = 0; i + 2 < idx.size(); i += 3)
                {
                    const glm::vec3& a = verts[idx[i]].Position;
                    const glm::vec3& b = verts[idx[i + 1]].Position;
                    const glm::vec3& c = verts[idx[i + 2]].Position;
                    float t;
                    if (RayTriangle(lo, ld, a, b, c, t) && t < best)
                    {
                        best = t;
                        bestNormal = glm::cross(b - a, c - a);
                        hit = true;
                    }
                }
            }
            if (!hit)
                return false;
            // Yerel t -> dünya uzaklığı
            const glm::vec3 worldHit = glm::vec3(world * glm::vec4(lo + ld * best, 1.0f));
            outDistance = glm::distance(origin, worldHit);
            outNormal = glm::normalize(glm::mat3(glm::transpose(inv)) * bestNormal);
            return true;
        }

        void ReleaseRuntime(VertexPaintComponent& component)
        {
            component.Runtime.reset();
        }
    }

    void RegisterVertexPaintComponents()
    {
        using C = VertexPaintComponent;
        ComponentDef<C>("Vertex Paint", "VertexPaintComponent", "Rendering", ICON_FA_PAINTBRUSH)
            .Describe("Per-object vertex colors painted in the viewport (Unreal Mesh Paint). PBR materials multiply albedo by the color; "
                      "Shader Graph reads it with the Vertex Color node. Painted objects always draw their full-detail LOD.")
            .Field("Enabled", "Enabled", &C::Enabled)
            // Boya verisi kaydedilir ama denetçide gösterilmez (fırça ile düzenlenir)
            .Field("Data", "Data", &C::Data).VisibleIf([](const C&) { return false; });
    }
}
