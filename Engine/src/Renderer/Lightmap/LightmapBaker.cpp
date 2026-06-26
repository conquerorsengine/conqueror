#include "LightmapBaker.h"
#include "Scene/Scene.h"
#include "Scene/Entity.h"
#include "Scene/Components.h"
#include "Renderer/Utilities/Renderer3D/Renderer3D.h"
#include "Renderer/Utilities/Renderer3D/Mesh.h"
#include "Renderer/Utilities/Renderer3D/ModelLoader.h"
#include "Core/Logging/Log.h"

#include <algorithm>
#include <cmath>
#include <random>
#include <limits>

namespace Conqueror
{
    struct BakeTriangle
    {
        glm::vec3 v0, v1, v2;
        glm::vec3 n0, n1, n2;
        glm::vec2 uv0, uv1, uv2; // atlas UV
        glm::vec3 albedo;
        int meshID;
    };

    static bool RayTriTest(const glm::vec3& o, const glm::vec3& d,
                            const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2,
                            float& t, float& u, float& v)
    {
        const float E = 1e-6f;
        glm::vec3 e1 = v1-v0, e2 = v2-v0;
        glm::vec3 h = glm::cross(d, e2);
        float a = glm::dot(e1, h);
        if (a > -E && a < E) return false;
        float f = 1.0f/a;
        glm::vec3 s = o-v0;
        u = f*glm::dot(s,h); if (u<0||u>1) return false;
        glm::vec3 q = glm::cross(s,e1);
        v = f*glm::dot(d,q); if (v<0||u+v>1) return false;
        t = f*glm::dot(e2,q); return t > E;
    }

    static bool AnyHitExcl(const glm::vec3& o, const glm::vec3& d,
                            const std::vector<BakeTriangle>& tris, float mx, int exclMesh)
    {
        for (auto& tri : tris)
        {
            if (tri.meshID == exclMesh) continue;
            float tt, u, v;
            if (RayTriTest(o, d, tri.v0, tri.v1, tri.v2, tt, u, v) && tt > 0.01f && tt < mx)
                return true;
        }
        return false;
    }

    static bool Closest(const glm::vec3& o, const glm::vec3& d,
                         const std::vector<BakeTriangle>& tris, float& ct, int& ci, float& cu, float& cv)
    {
        ct = 1e10f; ci = -1;
        for (int i = 0; i < (int)tris.size(); i++)
        {
            float t, u, v;
            if (RayTriTest(o, d, tris[i].v0, tris[i].v1, tris[i].v2, t, u, v) && t > 0.01f && t < ct)
            { ct = t; ci = i; cu = u; cv = v; }
        }
        return ci >= 0;
    }

    static glm::vec3 CosHemi(const glm::vec3& n, std::mt19937& g)
    {
        std::uniform_real_distribution<float> d(0, 1);
        float r1 = d(g), r2 = d(g), phi = 6.28318f * r1;
        float ct = std::sqrt(1 - r2), st = std::sqrt(r2);
        glm::vec3 up = (std::abs(n.y) < 0.999f) ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
        glm::vec3 t = glm::normalize(glm::cross(up, n)), b = glm::cross(n, t);
        return glm::normalize(t * std::cos(phi) * st + b * std::sin(phi) * st + n * ct);
    }

    static bool BaryTest(const glm::vec2& p, const glm::vec2& a, const glm::vec2& b, const glm::vec2& c,
                          float& b0, float& b1, float& b2)
    {
        glm::vec2 v0 = b-a, v1 = c-a, v2 = p-a;
        float d00 = glm::dot(v0,v0), d01 = glm::dot(v0,v1), d11 = glm::dot(v1,v1);
        float d20 = glm::dot(v2,v0), d21 = glm::dot(v2,v1);
        float den = d00*d11 - d01*d01;
        if (std::abs(den) < 1e-10f) return false;
        b1 = (d11*d20 - d01*d21) / den;
        b2 = (d00*d21 - d01*d20) / den;
        b0 = 1 - b1 - b2;
        return b0 >= -0.001f && b1 >= -0.001f && b2 >= -0.001f;
    }

    LightmapBaker::LightmapBaker(const LightmapSettings& settings) : m_Settings(settings) {}
    void LightmapBaker::ReportProgress(float p, const std::string& s) { m_Progress = p; if (m_ProgressCallback) m_ProgressCallback(p, s); }

    void LightmapBaker::Bake(Scene* scene)
    {
        if (!scene || m_IsBaking) return;
        m_IsBaking = true;
        m_Progress = 0.0f;

        ReportProgress(0.0f, "Collecting geometry...");
        std::vector<BakeTriangle> tris;
        auto& reg = scene->m_Registry;

        // Collect geometry
        int meshCounter = 0;
        auto meshView = reg.view<TransformComponent, MeshRendererComponent>();
        for (auto e : meshView)
        {
            auto [tf, mr] = meshView.get<TransformComponent, MeshRendererComponent>(e);
            glm::mat4 wt = tf.GetTransform();
            glm::mat3 nm = glm::mat3(glm::transpose(glm::inverse(wt)));
            std::shared_ptr<Mesh> mesh;
            switch (mr.Type) {
                case MeshType::Sphere: mesh = Renderer3D::GetSphereMesh(); break;
                case MeshType::Plane: mesh = Renderer3D::GetPlaneMesh(); break;
                case MeshType::Cylinder: mesh = Renderer3D::GetCylinderMesh(); break;
                default: mesh = Renderer3D::GetCubeMesh(); break;
            }
            if (!mesh) continue;
            glm::vec3 alb = mr.MaterialInstance ? mr.MaterialInstance->Albedo : glm::vec3(mr.Color);
            const auto& vs = mesh->GetVertices();
            const auto& is = mesh->GetIndices();
            for (size_t i = 0; i + 2 < is.size(); i += 3)
            {
                BakeTriangle bt;
                for (int j = 0; j < 3; j++)
                {
                    const Vertex& v = vs[is[i+j]];
                    glm::vec3 wp = glm::vec3(wt * glm::vec4(v.Position, 1));
                    glm::vec3 wn = glm::normalize(nm * v.Normal);
                    if (j == 0) { bt.v0 = wp; bt.n0 = wn; }
                    else if (j == 1) { bt.v1 = wp; bt.n1 = wn; }
                    else { bt.v2 = wp; bt.n2 = wn; }
                }
                bt.albedo = alb;
                bt.meshID = meshCounter;
                // uv0/uv1/uv2 = mesh'in original UV1'i (cube icin her face 0-1 arasi)
                bt.uv0 = vs[is[i+0]].TexCoords;
                bt.uv1 = vs[is[i+1]].TexCoords;
                bt.uv2 = vs[is[i+2]].TexCoords;
                tris.push_back(bt);
            }
            meshCounter++;
        }

        auto modelView = reg.view<TransformComponent, ModelComponent>();
        for (auto e : modelView)
        {
            auto [tf, mc] = modelView.get<TransformComponent, ModelComponent>(e);
            if (!mc.ModelData) continue;
            glm::mat4 wt = tf.GetTransform();
            glm::mat3 nm = glm::mat3(glm::transpose(glm::inverse(wt)));
            for (size_t m = 0; m < mc.ModelData->Meshes.size(); m++)
            {
                auto& mesh = mc.ModelData->Meshes[m];
                if (!mesh) continue;
                glm::vec3 alb(1);
                if (m < mc.ModelData->Materials.size() && mc.ModelData->Materials[m])
                    alb = mc.ModelData->Materials[m]->Albedo;
                const auto& vs = mesh->GetVertices();
                const auto& is = mesh->GetIndices();
                for (size_t i = 0; i + 2 < is.size(); i += 3)
                {
                    BakeTriangle bt;
                    for (int j = 0; j < 3; j++)
                    {
                        const Vertex& v = vs[is[i+j]];
                        bt.v0; glm::vec3 wp = glm::vec3(wt * glm::vec4(v.Position, 1));
                        glm::vec3 wn = glm::normalize(nm * v.Normal);
                        if (j == 0) { bt.v0 = wp; bt.n0 = wn; }
                        else if (j == 1) { bt.v1 = wp; bt.n1 = wn; }
                        else { bt.v2 = wp; bt.n2 = wn; }
                    }
                    bt.albedo = alb;
                    bt.meshID = meshCounter;
                    bt.uv0 = vs[is[i+0]].TexCoords;
                    bt.uv1 = vs[is[i+1]].TexCoords;
                    bt.uv2 = vs[is[i+2]].TexCoords;
                    tris.push_back(bt);
                }
                meshCounter++;
            }
        }

        if (tris.empty()) { m_IsBaking = false; ReportProgress(1, "No geometry!"); return; }
        CQ_CORE_INFO("Lightmap: {0} triangles, {1} meshes", tris.size(), meshCounter);

        // Light collection
        glm::vec3 lightDir(0, -1, 0);
        glm::vec3 lightColor(1, 0.95f, 0.9f);
        float lightIntensity = 1.0f;
        Entity sun = scene->GetSunSourceEntity();
        if (sun && sun.HasComponent<DirectionalLightComponent>())
        {
            auto& dl = sun.GetComponent<DirectionalLightComponent>();
            lightDir = glm::normalize(dl.Direction);
            lightColor = dl.Color;
            lightIntensity = dl.Intensity;
        }

        // Atlas - her triangle icin ayri bolge
        // Triangle'larin UV1'i [0,1] arasi, bunu dogrudan kullanacagiz
        // Ama mesh'ler arasi cakismayi onlemek icin:
        // Her triangle'in meshID'sine gore UV offset uygula
        uint32_t W = (uint32_t)std::min(m_Settings.MaxLightmapSize, 512);
        uint32_t H = W;

        m_Atlas.Width = W;
        m_Atlas.Height = H;
        m_Atlas.Texels.resize(W * H, glm::vec3(0));

        // Her triangle icin UV1'i dogrudan kullan (cube face'leri zaten [0,1] arasi)
        // Farkli mesh'ler icin UV offset kullan
        // Cube icin: 6 face * 2 tri = 12 tri. Her face'in UV1'i [0,1] arasi
        // Biz atlas'ta her face'e farkli bolge verecegiz
        // Basit: mesh'in triangle sayisina gore atlas'i bol
        for (int i = 0; i < (int)tris.size(); i++)
        {
            // Her triangle icin: mesh'in icerigindeki index
            int localIdx = 0;
            for (int j = i - 1; j >= 0; j--)
            {
                if (tris[j].meshID != tris[i].meshID) break;
                localIdx++;
            }

            // Mesh icindeki face sayisi (cube icin 6)
            int meshTriCount = 0;
            for (int j = 0; j < (int)tris.size(); j++)
            {
                if (tris[j].meshID == tris[i].meshID) meshTriCount++;
            }
            int faceCount = meshTriCount / 2;
            if (faceCount < 1) faceCount = 1;

            int faceIdx = localIdx / 2;
            int col = faceIdx;
            float pad = 0.02f;
            float u0 = (float)col / (float)faceCount + pad / (float)faceCount;
            float u1 = (float)(col + 1) / (float)faceCount - pad / (float)faceCount;
            float v0 = pad;
            float v1 = 1.0f - pad;

            tris[i].uv0 = glm::vec2(u0, v0);
            tris[i].uv1 = glm::vec2(u1, v0);
            tris[i].uv2 = glm::vec2(u1, v1);
        }

        // Mesh TexCoords2'leri guncelle - cube icin her face'in atlas UV'si
        {
            auto cubeMesh = Renderer3D::GetCubeMesh();
            if (cubeMesh && (int)tris.size() >= 12)
            {
                std::vector<glm::vec2> newUV2(cubeMesh->GetVertexCount(), glm::vec2(0.5f));
                int cubeTriCount = 0;
                for (auto& t : tris) { if (t.meshID == 0) cubeTriCount++; }
                int cubeFaces = cubeTriCount / 2;

                for (int face = 0; face < 6 && face < cubeFaces; face++)
                {
                    int triBase = face * 2;
                    if (triBase >= (int)tris.size()) break;
                    if (tris[triBase].meshID != 0) continue;

                    glm::vec2 auv0 = tris[triBase].uv0;
                    glm::vec2 auv1 = tris[triBase].uv1;

                    int vi = face * 4;
                    if (vi + 3 < (int)newUV2.size())
                    {
                        newUV2[vi + 0] = auv0;
                        newUV2[vi + 1] = glm::vec2(auv1.x, auv0.y);
                        newUV2[vi + 2] = auv1;
                        newUV2[vi + 3] = glm::vec2(auv0.x, auv1.y);
                    }
                }
                cubeMesh->UpdateUV2(newUV2);
            }
        }

        std::mt19937 gen(42);
        glm::vec3 ambient = scene->GetAmbientColor() * scene->GetAmbientIntensity();

        // Direct lighting
        ReportProgress(0.05f, "Direct lighting...");
        for (uint32_t py = 0; py < H; py++)
        {
            if (py % 16 == 0) ReportProgress(0.05f + 0.35f * ((float)py / (float)H), "Direct: " + std::to_string(py) + "/" + std::to_string(H));
            for (uint32_t px = 0; px < W; px++)
            {
                float pu = ((float)px + 0.5f) / (float)W;
                float pv = ((float)py + 0.5f) / (float)H;

                for (auto& tri : tris)
                {
                    float b0, b1, b2;
                    if (!BaryTest(glm::vec2(pu, pv), tri.uv0, tri.uv1, tri.uv2, b0, b1, b2))
                        continue;

                    glm::vec3 wp = tri.v0 * b0 + tri.v1 * b1 + tri.v2 * b2;
                    glm::vec3 n = glm::normalize(tri.n0 * b0 + tri.n1 * b1 + tri.n2 * b2);

                    // Self-shadow engeli: ayni mesh'i atla
                    bool shadow = AnyHitExcl(wp + n * 0.02f, -lightDir, tris, 500, tri.meshID);
                    glm::vec3 light(0);
                    if (!shadow)
                        light = lightColor * lightIntensity * std::max(0.0f, glm::dot(n, -lightDir));

                    // Ambient: her face'e minimum isik
                    float upward = std::max(0.0f, glm::dot(n, glm::vec3(0, 1, 0)));
                    glm::vec3 ambientContrib = ambient * (0.01f + upward * 0.02f);
                    light += ambientContrib;

                    m_Atlas.Texels[py * W + px] = light * tri.albedo;
                    break;
                }
            }
        }

        // Indirect (1 bounce)
        ReportProgress(0.4f, "Indirect lighting...");
        std::vector<glm::vec3> ind(W * H, glm::vec3(0));
        for (uint32_t py = 0; py < H; py++)
        {
            if (py % 16 == 0) ReportProgress(0.4f + 0.3f * ((float)py / (float)H), "Indirect: " + std::to_string(py) + "/" + std::to_string(H));
            for (uint32_t px = 0; px < W; px++)
            {
                float pu = ((float)px + 0.5f) / (float)W;
                float pv = ((float)py + 0.5f) / (float)H;
                for (auto& tri : tris)
                {
                    float b0, b1, b2;
                    if (!BaryTest(glm::vec2(pu, pv), tri.uv0, tri.uv1, tri.uv2, b0, b1, b2))
                        continue;
                    glm::vec3 wp = tri.v0 * b0 + tri.v1 * b1 + tri.v2 * b2;
                    glm::vec3 n = glm::normalize(tri.n0 * b0 + tri.n1 * b1 + tri.n2 * b2);
                    glm::vec3 il(0);
                    int samples = std::min(m_Settings.IndirectSamples, 64);
                    for (int s = 0; s < samples; s++)
                    {
                        glm::vec3 dir = CosHemi(n, gen);
                        float ct; int ci; float cu, cv;
                        if (Closest(wp + n * 0.02f, dir, tris, ct, ci, cu, cv))
                        {
                            auto& ht = tris[ci];
                            glm::vec3 hp = ht.v0 * (1 - cu - cv) + ht.v1 * cu + ht.v2 * cv;
                            glm::vec3 hn = glm::normalize(ht.n0 * (1 - cu - cv) + ht.n1 * cu + ht.n2 * cv);
                            bool sh = AnyHitExcl(hp + hn * 0.02f, -lightDir, tris, 500, ht.meshID);
                            float nl = std::max(0.0f, glm::dot(hn, -lightDir));
                            glm::vec3 hitLight = sh ? glm::vec3(0.01f) : lightColor * lightIntensity * nl + glm::vec3(0.01f);
                            il += ht.albedo * hitLight;
                        }
                    }
                    ind[py * W + px] = il / (float)samples;
                    break;
                }
            }
        }
        for (size_t i = 0; i < m_Atlas.Texels.size(); i++)
            m_Atlas.Texels[i] += ind[i] * 0.25f;

        // AO
        if (m_Settings.AmbientOcclusion)
        {
            ReportProgress(0.7f, "AO...");
            for (uint32_t py = 0; py < H; py++)
            {
                if (py % 16 == 0) ReportProgress(0.7f + 0.1f * ((float)py / (float)H), "AO: " + std::to_string(py) + "/" + std::to_string(H));
                for (uint32_t px = 0; px < W; px++)
                {
                    float pu = ((float)px + 0.5f) / (float)W;
                    float pv = ((float)py + 0.5f) / (float)H;
                    for (auto& tri : tris)
                    {
                        float b0, b1, b2;
                        if (!BaryTest(glm::vec2(pu, pv), tri.uv0, tri.uv1, tri.uv2, b0, b1, b2))
                            continue;
                        glm::vec3 wp = tri.v0 * b0 + tri.v1 * b1 + tri.v2 * b2;
                        glm::vec3 n = glm::normalize(tri.n0 * b0 + tri.n1 * b1 + tri.n2 * b2);
                        float ao = 0;
                        for (int s = 0; s < 32; s++)
                        {
                            glm::vec3 dir = CosHemi(n, gen);
                            if (AnyHitExcl(wp + n * 0.02f, dir, tris, 2.0f, tri.meshID))
                                ao += 1.0f;
                        }
                        m_Atlas.Texels[py * W + px] *= (1.0f - ao / 32.0f * 0.7f);
                        break;
                    }
                }
            }
        }

        ReportProgress(0.85f, "Filtering...");
        ApplyFiltering();
        ReportProgress(1.0f, "Done!");
        m_IsBaking = false;
        CQ_CORE_INFO("Lightmap done: {0}x{1}, {2} tris, {3} meshes", W, H, tris.size(), meshCounter);
    }

    void LightmapBaker::ApplyFiltering()
    {
        if (m_Settings.Filtering == 1) return;
        uint32_t w = m_Atlas.Width, h = m_Atlas.Height;
        if (w < 3 || h < 3) return;
        auto f = m_Atlas.Texels;
        for (uint32_t y = 1; y < h - 1; y++)
            for (uint32_t x = 1; x < w - 1; x++)
            {
                glm::vec3 s(0); float wt = 0;
                for (int ky = -1; ky <= 1; ky++)
                    for (int kx = -1; kx <= 1; kx++)
                    {
                        float fw = 1.0f / (1 + std::abs((float)kx) + std::abs((float)ky));
                        s += m_Atlas.Texels[(y + ky) * w + (x + kx)] * fw;
                        wt += fw;
                    }
                f[y * w + x] = s / wt;
            }
        m_Atlas.Texels = f;
    }

    std::shared_ptr<Texture2D> LightmapBaker::CreateLightmapTexture() const
    {
        if (!m_Atlas.Width || !m_Atlas.Height || m_Atlas.Texels.empty()) return nullptr;
        std::vector<uint8_t> px(m_Atlas.Width * m_Atlas.Height * 4);
        for (size_t i = 0; i < m_Atlas.Texels.size(); i++)
        {
            // Linear data, tonemapping yok - shader yapacak
            glm::vec3 c = glm::clamp(m_Atlas.Texels[i], glm::vec3(0), glm::vec3(10));
            // sRGB gamma encode (texture saklama icin)
            c = glm::pow(c, glm::vec3(1.0f / 2.2f));
            px[i * 4 + 0] = (uint8_t)(glm::clamp(c.r, 0.f, 1.f) * 255);
            px[i * 4 + 1] = (uint8_t)(glm::clamp(c.g, 0.f, 1.f) * 255);
            px[i * 4 + 2] = (uint8_t)(glm::clamp(c.b, 0.f, 1.f) * 255);
            px[i * 4 + 3] = 255;
        }
        auto tex = Texture2D::Create(m_Atlas.Width, m_Atlas.Height);
        if (tex) tex->SetData(px.data(), (uint32_t)px.size());
        return tex;
    }

    std::shared_ptr<LightmapBaker> LightmapBaker::Create(const LightmapSettings& settings)
    {
        return std::make_shared<LightmapBaker>(settings);
    }
}
