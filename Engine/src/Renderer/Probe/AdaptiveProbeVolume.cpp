#include "AdaptiveProbeVolume.h"
#include "Scene/Scene.h"
#include "Scene/Entity.h"
#include "Scene/Components.h"
#include "Renderer/Utilities/Renderer3D/Renderer3D.h"
#include "Renderer/Utilities/Renderer3D/Mesh.h"
#include "Renderer/Utilities/Renderer3D/ModelLoader.h"
#include "Renderer/RHI/Cubemap.h"
#include "Core/Logging/Log.h"

#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <random>

namespace Conqueror
{
    AdaptiveProbeVolume::AdaptiveProbeVolume(const AdaptiveProbeVolumeSettings& settings)
        : m_Settings(settings)
    {
    }

    int AdaptiveProbeVolume::GetNumLevelsFromSpacing() const
    {
        if (m_Settings.MaxSpacing <= m_Settings.MinSpacing)
            return 1;

        int levels = 1;
        float s = m_Settings.MaxSpacing;
        while (s > m_Settings.MinSpacing + 0.01f && levels < 4)
        {
            s *= 0.5f;
            levels++;
        }
        return levels;
    }

    void AdaptiveProbeVolume::PlaceProbes(Scene* scene)
    {
        m_Probes.clear();

        glm::vec3 volumeMin = m_Settings.Origin + m_Settings.ProbeOffset;

        // Adaptive subdivision: start coarse, subdivide bricks that have geometry
        struct SubdivBrick {
            glm::vec3 minBound, maxBound;
            int level;
        };

        std::vector<SubdivBrick> leafBricks;
        float coarseSpacing = m_Settings.MaxSpacing;

        // Start with coarsest level bricks
        int coarseGridX = (int)std::ceil(m_Settings.Size.x / coarseSpacing);
        int coarseGridY = (int)std::ceil(m_Settings.Size.y / coarseSpacing);
        int coarseGridZ = (int)std::ceil(m_Settings.Size.z / coarseSpacing);

        for (int gz = 0; gz < coarseGridZ; gz++)
        {
            for (int gy = 0; gy < coarseGridY; gy++)
            {
                for (int gx = 0; gx < coarseGridX; gx++)
                {
                    SubdivBrick brick;
                    brick.minBound = m_Settings.Origin + glm::vec3(gx * coarseSpacing, gy * coarseSpacing, gz * coarseSpacing);
                    brick.maxBound = brick.minBound + glm::vec3(coarseSpacing);
                    brick.level = 0;
                    leafBricks.push_back(brick);
                }
            }
        }

        // Collect geometry AABBs for overlap testing
        struct AABB { glm::vec3 min, max; };
        std::vector<AABB> geomAABBs;

        auto& reg = scene->m_Registry;
        auto meshView = reg.view<TransformComponent, MeshRendererComponent>();
        for (auto e : meshView)
        {
            auto [tf, mr] = meshView.get<TransformComponent, MeshRendererComponent>(e);
            glm::mat4 wt = tf.GetTransform();
            std::shared_ptr<Mesh> mesh;
            switch (mr.Type) {
                case MeshType::Sphere: mesh = Renderer3D::GetSphereMesh(); break;
                case MeshType::Plane: mesh = Renderer3D::GetPlaneMesh(); break;
                case MeshType::Cylinder: mesh = Renderer3D::GetCylinderMesh(); break;
                default: mesh = Renderer3D::GetCubeMesh(); break;
            }
            if (!mesh) continue;
            glm::vec3 lMin(1e10f), lMax(-1e10f);
            for (auto& v : mesh->GetVertices())
            {
                glm::vec3 wp = glm::vec3(wt * glm::vec4(v.Position, 1.0f));
                lMin = glm::min(lMin, wp);
                lMax = glm::max(lMax, wp);
            }
            geomAABBs.push_back({ lMin, lMax });
        }

        auto modelView = reg.view<TransformComponent, ModelComponent>();
        for (auto e : modelView)
        {
            auto [tf, mc] = modelView.get<TransformComponent, ModelComponent>(e);
            if (!mc.ModelData) continue;
            glm::mat4 wt = tf.GetTransform();
            for (auto& mesh : mc.ModelData->Meshes)
            {
                if (!mesh) continue;
                glm::vec3 lMin(1e10f), lMax(-1e10f);
                for (auto& v : mesh->GetVertices())
                {
                    glm::vec3 wp = glm::vec3(wt * glm::vec4(v.Position, 1.0f));
                    lMin = glm::min(lMin, wp);
                    lMax = glm::max(lMax, wp);
                }
                geomAABBs.push_back({ lMin, lMax });
            }
        }

        auto overlapsGeometry = [&](const glm::vec3& bMin, const glm::vec3& bMax) -> bool {
            for (auto& g : geomAABBs)
            {
                if (bMin.x <= g.max.x && bMax.x >= g.min.x &&
                    bMin.y <= g.max.y && bMax.y >= g.min.y &&
                    bMin.z <= g.max.z && bMax.z >= g.min.z)
                    return true;
            }
            return false;
        };

        // Adaptive subdivision: subdivide bricks that overlap geometry
        int maxLevels = m_NumLevels;
        for (int level = 0; level < maxLevels - 1; level++)
        {
            std::vector<SubdivBrick> nextBricks;

            for (auto& brick : leafBricks)
            {
                if (brick.level != level)
                {
                    nextBricks.push_back(brick);
                    continue;
                }

                if (!overlapsGeometry(brick.minBound, brick.maxBound))
                {
                    nextBricks.push_back(brick);
                    continue;
                }

                // Subdivide into 8 children
                glm::vec3 mid = (brick.minBound + brick.maxBound) * 0.5f;
                glm::vec3 cornersLow[2] = { brick.minBound, mid };
                glm::vec3 cornersHigh[2] = { mid, brick.maxBound };

                for (int cz = 0; cz < 2; cz++)
                    for (int cy = 0; cy < 2; cy++)
                        for (int cx = 0; cx < 2; cx++)
                        {
                            SubdivBrick child;
                            child.minBound = glm::vec3(cornersLow[cx].x, cornersLow[cy].y, cornersLow[cz].z);
                            child.maxBound = glm::vec3(cornersHigh[cx].x, cornersHigh[cy].y, cornersHigh[cz].z);
                            child.level = level + 1;
                            nextBricks.push_back(child);
                        }
            }

            leafBricks = std::move(nextBricks);
        }

        // Collect unique probe positions from all leaf brick corners
        std::unordered_map<uint64_t, int> probeIndexMap;
        float finestSpacing = m_Settings.MinSpacing;

        auto hashPos = [](int x, int y, int z) -> uint64_t {
            uint64_t h = (uint64_t)(x + 100000) | ((uint64_t)(y + 100000) << 20) | ((uint64_t)(z + 100000) << 40);
            return h;
        };

        auto getOrCreateProbe = [&](const glm::vec3& pos) -> int {
            int ix = (int)std::round((pos.x - volumeMin.x) / finestSpacing);
            int iy = (int)std::round((pos.y - volumeMin.y) / finestSpacing);
            int iz = (int)std::round((pos.z - volumeMin.z) / finestSpacing);

            uint64_t key = hashPos(ix, iy, iz);
            auto it = probeIndexMap.find(key);
            if (it != probeIndexMap.end())
                return it->second;

            APVProbe probe;
            probe.Position = pos;
            probe.Valid = false;

            int idx = (int)m_Probes.size();
            m_Probes.push_back(probe);
            probeIndexMap[key] = idx;
            return idx;
        };

        // Place probes at leaf brick corners
        for (auto& brick : leafBricks)
        {
            glm::vec3 corners[8] = {
                { brick.minBound.x, brick.minBound.y, brick.minBound.z },
                { brick.maxBound.x, brick.minBound.y, brick.minBound.z },
                { brick.minBound.x, brick.maxBound.y, brick.minBound.z },
                { brick.maxBound.x, brick.maxBound.y, brick.minBound.z },
                { brick.minBound.x, brick.minBound.y, brick.maxBound.z },
                { brick.maxBound.x, brick.minBound.y, brick.maxBound.z },
                { brick.minBound.x, brick.maxBound.y, brick.maxBound.z },
                { brick.maxBound.x, brick.maxBound.y, brick.maxBound.z },
            };

            // Determine which brick level this belongs to
            float brickSize = brick.maxBound.x - brick.minBound.x;
            int brickLevel = 0;
            float s = m_Settings.MaxSpacing;
            while (s > brickSize + 0.01f && brickLevel < 3) { s *= 0.5f; brickLevel++; }

            APVBrick apvBrick;
            apvBrick.MinBound = brick.minBound;
            apvBrick.MaxBound = brick.maxBound;
            apvBrick.Level = brickLevel;
            apvBrick.HasGeometry = overlapsGeometry(brick.minBound, brick.maxBound);

            for (int c = 0; c < 8; c++)
                apvBrick.ProbeIndices[c] = getOrCreateProbe(corners[c]);

            m_Bricks[brickLevel].push_back(apvBrick);
        }

        CQ_CORE_INFO("APV: Adaptive subdivision - {0} leaf bricks, {1} probes across {2} levels",
                      leafBricks.size(), m_Probes.size(), m_NumLevels);
    }

    void AdaptiveProbeVolume::Bake(Scene* scene)
    {
        if (!scene || m_IsBaking) return;
        m_IsBaking = true;
        m_IsBaked = false;
        m_Progress = 0.0f;

        CQ_CORE_INFO("APV: Starting bake...");

        ReportProgress(0.0f, "Adaptive subdivision...");
        PlaceProbes(scene);

        ReportProgress(0.15f, "Baking probes...");
        BakeProbes(scene);

        if (m_InvaliditySettings.VirtualOffset)
        {
            ReportProgress(0.85f, "Applying virtual offset...");
            ApplyVirtualOffset(scene);
        }

        if (m_InvaliditySettings.Dilation)
        {
            ReportProgress(0.9f, "Applying dilation...");
            ApplyDilation();
        }

        // Mark valid probes
        for (auto& probe : m_Probes)
        {
            probe.Valid = IsProbeValid(probe);
        }

        m_IsBaked = true;
        m_IsBaking = false;
        m_Progress = 1.0f;

        CQ_CORE_INFO("APV: Bake complete - {0} probes, {1} levels", m_Probes.size(), m_NumLevels);
    }

    void AdaptiveProbeVolume::BakeProbes(Scene* scene)
    {
        int total = (int)m_Probes.size();
        if (total == 0) return;

        for (int i = 0; i < total; i++)
        {
            float p = 0.15f + 0.7f * ((float)i / (float)total);
            ReportProgress(p, "Baking probe " + std::to_string(i + 1) + "/" + std::to_string(total));
            BakeSingleProbe(scene, m_Probes[i]);
        }
    }

    void AdaptiveProbeVolume::BakeSingleProbe(Scene* scene, APVProbe& probe)
    {
        uint32_t res = m_Settings.ProbeResolution;
        std::vector<glm::vec3> cubemapData;
        RenderProbeCubemap(scene, probe.Position, res, cubemapData);
        CubemapToSH(cubemapData, res, probe.SH);
        probe.Valid = true;
    }

    void AdaptiveProbeVolume::RenderProbeCubemap(Scene* scene, const glm::vec3& position,
                                                  uint32_t resolution, std::vector<glm::vec3>& outCubemap)
    {
        outCubemap.resize(resolution * resolution * 6, glm::vec3(0.0f));

        if (!scene) return;

        auto& reg = scene->m_Registry;

        // Collect scene geometry
        struct BakeTri {
            glm::vec3 v0, v1, v2;
            glm::vec3 n0, n1, n2;
            glm::vec3 albedo;
        };
        std::vector<BakeTri> tris;

        auto meshView = reg.view<TransformComponent, MeshRendererComponent>();
        for (auto e : meshView)
        {
            auto [tf, mr] = meshView.get<TransformComponent, MeshRendererComponent>(e);
            glm::mat4 wt = tf.GetTransform();
            glm::mat3 nm = glm::mat3(glm::transpose(glm::inverse(wt)));
            glm::vec3 alb = mr.MaterialInstance ? mr.MaterialInstance->Albedo : glm::vec3(mr.Color);

            std::shared_ptr<Mesh> mesh;
            switch (mr.Type) {
                case MeshType::Sphere: mesh = Renderer3D::GetSphereMesh(); break;
                case MeshType::Plane: mesh = Renderer3D::GetPlaneMesh(); break;
                case MeshType::Cylinder: mesh = Renderer3D::GetCylinderMesh(); break;
                default: mesh = Renderer3D::GetCubeMesh(); break;
            }
            if (!mesh) continue;

            const auto& vs = mesh->GetVertices();
            const auto& is = mesh->GetIndices();
            for (size_t i = 0; i + 2 < is.size(); i += 3)
            {
                BakeTri bt;
                for (int j = 0; j < 3; j++)
                {
                    const Vertex& v = vs[is[i + j]];
                    glm::vec3 wp = glm::vec3(wt * glm::vec4(v.Position, 1));
                    glm::vec3 wn = glm::normalize(nm * v.Normal);
                    if (j == 0) { bt.v0 = wp; bt.n0 = wn; }
                    else if (j == 1) { bt.v1 = wp; bt.n1 = wn; }
                    else { bt.v2 = wp; bt.n2 = wn; }
                }
                bt.albedo = alb;
                tris.push_back(bt);
            }
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
                    BakeTri bt;
                    for (int j = 0; j < 3; j++)
                    {
                        const Vertex& v = vs[is[i + j]];
                        glm::vec3 wp = glm::vec3(wt * glm::vec4(v.Position, 1));
                        glm::vec3 wn = glm::normalize(nm * v.Normal);
                        if (j == 0) { bt.v0 = wp; bt.n0 = wn; }
                        else if (j == 1) { bt.v1 = wp; bt.n1 = wn; }
                        else { bt.v2 = wp; bt.n2 = wn; }
                    }
                    bt.albedo = alb;
                    tris.push_back(bt);
                }
            }
        }

        if (tris.empty()) return;

        // Get lighting
        glm::vec3 lightDir(0, -1, 0);
        glm::vec3 lightColor(0, 0, 0);
        float lightIntensity = 0.0f;
        Entity sun = scene->GetSunSourceEntity();
        if (sun && sun.HasComponent<DirectionalLightComponent>())
        {
            auto& dl = sun.GetComponent<DirectionalLightComponent>();
            lightDir = glm::normalize(dl.Direction);
            lightColor = dl.Color;
            lightIntensity = dl.Intensity;
        }

        glm::vec3 ambient = scene->GetAmbientColor() * scene->GetAmbientIntensity() * 0.01f;

        // Collect point lights
        struct BakedPointLight { glm::vec3 pos, color; float intensity, range; };
        std::vector<BakedPointLight> pointLights;
        auto plView = reg.view<TransformComponent, PointLightComponent>();
        for (auto e : plView)
        {
            auto [tf, pl] = plView.get<TransformComponent, PointLightComponent>(e);
            pointLights.push_back({ tf.Position, pl.Color, pl.Intensity, pl.Range });
        }

        // Ray-triangle intersection
        auto rayTriTest = [](const glm::vec3& o, const glm::vec3& d,
                             const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2,
                             float& t, float& u, float& v) -> bool {
            const float E = 1e-6f;
            glm::vec3 e1 = v1 - v0, e2 = v2 - v0;
            glm::vec3 h = glm::cross(d, e2);
            float a = glm::dot(e1, h);
            if (a > -E && a < E) return false;
            float f = 1.0f / a;
            glm::vec3 s = o - v0;
            u = f * glm::dot(s, h); if (u < 0 || u > 1) return false;
            glm::vec3 q = glm::cross(s, e1);
            v = f * glm::dot(d, q); if (v < 0 || u + v > 1) return false;
            t = f * glm::dot(e2, q); return t > E;
        };

        auto anyHit = [&](const glm::vec3& o, const glm::vec3& d, float maxDist) -> bool {
            for (auto& tri : tris)
            {
                float t, u, v;
                if (rayTriTest(o, d, tri.v0, tri.v1, tri.v2, t, u, v) && t > 0.01f && t < maxDist)
                    return true;
            }
            return false;
        };

        auto closestHit = [&](const glm::vec3& o, const glm::vec3& d, float& ct, int& ci, float& cu, float& cv) -> bool {
            ct = 1e10f; ci = -1;
            for (int i = 0; i < (int)tris.size(); i++)
            {
                float t, u, v;
                if (rayTriTest(o, d, tris[i].v0, tris[i].v1, tris[i].v2, t, u, v) && t > 0.01f && t < ct)
                { ct = t; ci = i; cu = u; cv = v; }
            }
            return ci >= 0;
        };

        // 6 cubemap face directions
        struct FaceDir { glm::vec3 right, up, forward; };
        FaceDir faces[6] = {
            { glm::vec3(0,0,-1), glm::vec3(0,-1,0), glm::vec3(1,0,0) },  // +X
            { glm::vec3(0,0,1),  glm::vec3(0,-1,0), glm::vec3(-1,0,0) }, // -X
            { glm::vec3(1,0,0),  glm::vec3(0,0,1),  glm::vec3(0,1,0) },  // +Y
            { glm::vec3(1,0,0),  glm::vec3(0,0,-1), glm::vec3(0,-1,0) }, // -Y
            { glm::vec3(1,0,0),  glm::vec3(0,-1,0), glm::vec3(0,0,1) },  // +Z
            { glm::vec3(-1,0,0), glm::vec3(0,-1,0), glm::vec3(0,0,-1) }, // -Z
        };

        std::mt19937 gen(42);
        std::uniform_real_distribution<float> dist(0.0f, 1.0f);

        for (int face = 0; face < 6; face++)
        {
            glm::vec3 right = faces[face].right;
            glm::vec3 up = faces[face].up;
            glm::vec3 fwd = faces[face].forward;

            for (uint32_t py = 0; py < resolution; py++)
            {
                for (uint32_t px = 0; px < resolution; px++)
                {
                    // Pixel to direction on cubemap face
                    float u = ((float)px + 0.5f) / (float)resolution * 2.0f - 1.0f;
                    float v = ((float)py + 0.5f) / (float)resolution * 2.0f - 1.0f;

                    glm::vec3 dir = glm::normalize(fwd + right * u + up * v);

                    // Cast ray from probe position
                    glm::vec3 rayOrig = position + dir * 0.02f;
                    glm::vec3 rayDir = dir;

                    glm::vec3 color(0.0f);
                    float t; int ci; float cu, cv;

                    if (closestHit(rayOrig, rayDir, t, ci, cu, cv))
                    {
                        auto& ht = tris[ci];
                        glm::vec3 hitPos = ht.v0 * (1 - cu - cv) + ht.v1 * cu + ht.v2 * cv;
                        glm::vec3 hitNormal = glm::normalize(ht.n0 * (1 - cu - cv) + ht.n1 * cu + ht.n2 * cv);

                        // Direct light
                        bool inShadow = anyHit(hitPos + hitNormal * 0.02f, -lightDir, 500.0f);
                        float ndotl = std::max(0.0f, glm::dot(hitNormal, -lightDir));
                        glm::vec3 directLight = inShadow ? glm::vec3(0.0f) : lightColor * lightIntensity * ndotl;

                        // Point lights
                        for (auto& pl : pointLights)
                        {
                            float dist = glm::length(pl.pos - hitPos);
                            if (dist < pl.range)
                            {
                                glm::vec3 plDir = glm::normalize(pl.pos - hitPos);
                                float plNdotL = std::max(0.0f, glm::dot(hitNormal, plDir));
                                float attenuation = 1.0f / (1.0f + 0.09f * dist + 0.032f * dist * dist);
                                bool plShadow = anyHit(hitPos + hitNormal * 0.02f, plDir, dist);
                                if (!plShadow)
                                    directLight += pl.color * pl.intensity * plNdotL * attenuation;
                            }
                        }

                        // Indirect (1 bounce)
                        glm::vec3 indirect(0.0f);
                        int numSamples = 8;
                        for (int s = 0; s < numSamples; s++)
                        {
                            float r1 = dist(gen), r2 = dist(gen), phi = 6.28318f * r1;
                            float ct = std::sqrt(1 - r2), st = std::sqrt(r2);
                            glm::vec3 sampleDir = glm::normalize(
                                glm::vec3(std::cos(phi) * st, std::sin(phi) * st, ct));

                            // Reorient to normal
                            glm::vec3 upVec = (std::abs(hitNormal.y) < 0.999f) ? glm::vec3(0,1,0) : glm::vec3(1,0,0);
                            glm::vec3 tan = glm::normalize(glm::cross(upVec, hitNormal));
                            glm::vec3 bitan = glm::cross(hitNormal, tan);
                            sampleDir = glm::normalize(tan * std::cos(phi) * st + bitan * std::sin(phi) * st + hitNormal * ct);

                            float sT; int sCi; float sCu, sCv;
                            if (closestHit(hitPos + hitNormal * 0.02f, sampleDir, sT, sCi, sCu, sCv))
                            {
                                auto& sHit = tris[sCi];
                                glm::vec3 sPos = sHit.v0 * (1 - sCu - sCv) + sHit.v1 * sCu + sHit.v2 * sCv;
                                glm::vec3 sN = glm::normalize(sHit.n0 * (1 - sCu - sCv) + sHit.n1 * sCu + sHit.n2 * sCv);
                                float sNdotL = std::max(0.0f, glm::dot(sN, -lightDir));
                                bool sShadow = anyHit(sPos + sN * 0.02f, -lightDir, 500.0f);
                                glm::vec3 sLight = sShadow ? glm::vec3(0.3f) : lightColor * lightIntensity * sNdotL + glm::vec3(0.1f);
                                indirect += sHit.albedo * sLight;
                            }
                            else
                            {
                                indirect += ambient * 2.0f;
                            }
                        }
                        indirect /= (float)numSamples;

                        color = ht.albedo * (ambient + directLight + indirect);
                    }
                    else
                    {
                        // Sky color
                        color = ambient * 2.0f;
                        // Add some sky gradient
                        float skyFactor = glm::clamp(dir.y * 0.5f + 0.5f, 0.0f, 1.0f);
                        color += glm::vec3(0.3f, 0.5f, 0.8f) * skyFactor * 0.5f;
                    }

                    outCubemap[face * resolution * resolution + py * resolution + px] = color;
                }
            }
        }
    }

    void AdaptiveProbeVolume::CubemapToSH(const std::vector<glm::vec3>& cubemapData, uint32_t resolution,
                                            SHL2Coefficients& outSH)
    {
        for (int i = 0; i < 9; i++)
            outSH.Coeffs[i] = glm::vec3(0.0f);

        float totalWeight = 0.0f;
        float texelSolidAngle = 4.0f / (float)(resolution * resolution);

        for (int face = 0; face < 6; face++)
        {
            for (uint32_t py = 0; py < resolution; py++)
            {
                for (uint32_t px = 0; px < resolution; px++)
                {
                    float u = ((float)px + 0.5f) / (float)resolution * 2.0f - 1.0f;
                    float v = ((float)py + 0.5f) / (float)resolution * 2.0f - 1.0f;

                    // Cubemap face directions
                    glm::vec3 dir(0.0f);
                    switch (face)
                    {
                        case 0: dir = glm::vec3(1, -v, -u); break;   // +X
                        case 1: dir = glm::vec3(-1, -v, u); break;   // -X
                        case 2: dir = glm::vec3(u, 1, v); break;     // +Y
                        case 3: dir = glm::vec3(u, -1, -v); break;   // -Y
                        case 4: dir = glm::vec3(u, -v, 1); break;    // +Z
                        case 5: dir = glm::vec3(-u, -v, -1); break;  // -Z
                    }
                    dir = glm::normalize(dir);

                    float x = dir.x, y = dir.y, z = dir.z;

                    // Solid angle weight for this texel
                    float weight = texelSolidAngle / (float)(1 + u * u + v * v);

                    glm::vec3 color = cubemapData[face * resolution * resolution + py * resolution + px];

                    // SH L2 basis functions
                    outSH.Coeffs[0] += color * 0.282095f * weight;
                    outSH.Coeffs[1] += color * 0.488603f * y * weight;
                    outSH.Coeffs[2] += color * 0.488603f * z * weight;
                    outSH.Coeffs[3] += color * 0.488603f * x * weight;
                    outSH.Coeffs[4] += color * 1.092548f * x * y * weight;
                    outSH.Coeffs[5] += color * 1.092548f * y * z * weight;
                    outSH.Coeffs[6] += color * 0.315392f * (3.0f * z * z - 1.0f) * weight;
                    outSH.Coeffs[7] += color * 1.092548f * x * z * weight;
                    outSH.Coeffs[8] += color * 0.546274f * (x * x - y * y) * weight;

                    totalWeight += weight;
                }
            }
        }

        if (totalWeight > 0.0f)
        {
            for (int i = 0; i < 9; i++)
                outSH.Coeffs[i] /= totalWeight;
        }
    }

    bool AdaptiveProbeVolume::IsProbeValid(const APVProbe& probe) const
    {
        // Check if SH data is non-zero
        float totalMagnitude = 0.0f;
        for (int i = 0; i < 9; i++)
            totalMagnitude += glm::length(probe.SH.Coeffs[i]);
        return totalMagnitude > 0.001f;
    }

    void AdaptiveProbeVolume::ApplyVirtualOffset(Scene* scene)
    {
        if (!scene) return;

        auto& reg = scene->m_Registry;

        // Collect all triangles for raycasting
        struct VOTri { glm::vec3 v0, v1, v2; };
        std::vector<VOTri> tris;

        auto meshView = reg.view<TransformComponent, MeshRendererComponent>();
        for (auto e : meshView)
        {
            auto [tf, mr] = meshView.get<TransformComponent, MeshRendererComponent>(e);
            glm::mat4 wt = tf.GetTransform();
            std::shared_ptr<Mesh> mesh;
            switch (mr.Type) {
                case MeshType::Sphere: mesh = Renderer3D::GetSphereMesh(); break;
                case MeshType::Plane: mesh = Renderer3D::GetPlaneMesh(); break;
                case MeshType::Cylinder: mesh = Renderer3D::GetCylinderMesh(); break;
                default: mesh = Renderer3D::GetCubeMesh(); break;
            }
            if (!mesh) continue;

            const auto& vs = mesh->GetVertices();
            const auto& is = mesh->GetIndices();
            for (size_t i = 0; i + 2 < is.size(); i += 3)
            {
                VOTri tri;
                tri.v0 = glm::vec3(wt * glm::vec4(vs[is[i]].Position, 1));
                tri.v1 = glm::vec3(wt * glm::vec4(vs[is[i + 1]].Position, 1));
                tri.v2 = glm::vec3(wt * glm::vec4(vs[is[i + 2]].Position, 1));
                tris.push_back(tri);
            }
        }

        auto modelView = reg.view<TransformComponent, ModelComponent>();
        for (auto e : modelView)
        {
            auto [tf, mc] = modelView.get<TransformComponent, ModelComponent>(e);
            if (!mc.ModelData) continue;
            glm::mat4 wt = tf.GetTransform();

            for (auto& mesh : mc.ModelData->Meshes)
            {
                if (!mesh) continue;
                const auto& vs = mesh->GetVertices();
                const auto& is = mesh->GetIndices();
                for (size_t i = 0; i + 2 < is.size(); i += 3)
                {
                    VOTri tri;
                    tri.v0 = glm::vec3(wt * glm::vec4(vs[is[i]].Position, 1));
                    tri.v1 = glm::vec3(wt * glm::vec4(vs[is[i + 1]].Position, 1));
                    tri.v2 = glm::vec3(wt * glm::vec4(vs[is[i + 2]].Position, 1));
                    tris.push_back(tri);
                }
            }
        }

        if (tris.empty()) return;

        auto rayTriTest = [](const glm::vec3& o, const glm::vec3& d,
                             const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2,
                             float& t) -> bool {
            const float E = 1e-6f;
            glm::vec3 e1 = v1 - v0, e2 = v2 - v0;
            glm::vec3 h = glm::cross(d, e2);
            float a = glm::dot(e1, h);
            if (a > -E && a < E) return false;
            float f = 1.0f / a;
            glm::vec3 s = o - v0;
            float u = f * glm::dot(s, h); if (u < 0 || u > 1) return false;
            glm::vec3 q = glm::cross(s, e1);
            float v = f * glm::dot(d, q); if (v < 0 || u + v > 1) return false;
            t = f * glm::dot(e2, q); return t > E;
        };

        float searchDist = m_Settings.MinSpacing * m_InvaliditySettings.SearchDistanceMultiplier;

        for (auto& probe : m_Probes)
        {
            glm::vec3 offset(0.0f);

            glm::vec3 dirs[6] = {
                glm::vec3(1,0,0), glm::vec3(-1,0,0),
                glm::vec3(0,1,0), glm::vec3(0,-1,0),
                glm::vec3(0,0,1), glm::vec3(0,0,-1)
            };

            for (auto& dir : dirs)
            {
                float closestT = 1e10f;
                for (auto& tri : tris)
                {
                    float tt;
                    if (rayTriTest(probe.Position, dir, tri.v0, tri.v1, tri.v2, tt) && tt < closestT)
                        closestT = tt;
                }

                if (closestT < searchDist)
                {
                    offset += dir * (searchDist - closestT + m_InvaliditySettings.GeometryBias);
                }
            }

            if (glm::length(offset) > 0.001f)
                probe.Position += glm::normalize(offset) * m_InvaliditySettings.GeometryBias * 2.0f;
        }
    }

    void AdaptiveProbeVolume::ApplyDilation()
    {
        int distance = m_InvaliditySettings.DilationDistance;
        if (distance <= 0) return;

        for (int iter = 0; iter < distance; iter++)
        {
            for (int i = 0; i < (int)m_Probes.size(); i++)
            {
                if (m_Probes[i].Valid) continue;

                // Find nearest valid probe
                float bestDist = 1e10f;
                int bestIdx = -1;
                for (int j = 0; j < (int)m_Probes.size(); j++)
                {
                    if (!m_Probes[j].Valid) continue;
                    float d = glm::length(m_Probes[i].Position - m_Probes[j].Position);
                    if (d < bestDist)
                    {
                        bestDist = d;
                        bestIdx = j;
                    }
                }

                if (bestIdx >= 0 && bestDist < m_Settings.MinSpacing * (iter + 2))
                {
                    m_Probes[i].SH = m_Probes[bestIdx].SH;
                    m_Probes[i].Valid = true;
                }
            }
        }
    }

    void AdaptiveProbeVolume::EvaluateIrradiance(const glm::vec3& worldPos, const glm::vec3& normal,
                                                   glm::vec3& outIrradiance) const
    {
        outIrradiance = glm::vec3(0.0f);

        if (!m_IsBaked || m_Probes.empty()) return;

        // Find 8 surrounding probes using trilinear interpolation
        float totalWeight = 0.0f;
        glm::vec3 totalSH[9];
        for (int i = 0; i < 9; i++) totalSH[i] = glm::vec3(0.0f);

        float spacing = m_Settings.MinSpacing;

        // Find the 8 corner probes
        glm::vec3 volumeMin = m_Settings.Origin + m_Settings.ProbeOffset;
        float fx = (worldPos.x - volumeMin.x) / spacing;
        float fy = (worldPos.y - volumeMin.y) / spacing;
        float fz = (worldPos.z - volumeMin.z) / spacing;

        int ix = (int)std::floor(fx);
        int iy = (int)std::floor(fy);
        int iz = (int)std::floor(fz);

        float tx = fx - ix;
        float ty = fy - iy;
        float tz = fz - iz;

        tx = glm::clamp(tx, 0.0f, 1.0f);
        ty = glm::clamp(ty, 0.0f, 1.0f);
        tz = glm::clamp(tz, 0.0f, 1.0f);

        // 8 corner weights for trilinear interpolation
        float weights[8] = {
            (1 - tx) * (1 - ty) * (1 - tz),
            tx * (1 - ty) * (1 - tz),
            (1 - tx) * ty * (1 - tz),
            tx * ty * (1 - tz),
            (1 - tx) * (1 - ty) * tz,
            tx * (1 - ty) * tz,
            (1 - tx) * ty * tz,
            tx * ty * tz
        };

        int cornerX[8] = { ix, ix + 1, ix, ix + 1, ix, ix + 1, ix, ix + 1 };
        int cornerY[8] = { iy, iy, iy + 1, iy + 1, iy, iy, iy + 1, iy + 1 };
        int cornerZ[8] = { iz, iz, iz, iz, iz + 1, iz + 1, iz + 1, iz + 1 };

        for (int c = 0; c < 8; c++)
        {
            glm::vec3 cornerPos = volumeMin + glm::vec3(cornerX[c], cornerY[c], cornerZ[c]) * spacing;

            // Find probe at this position
            float bestDist = 1e10f;
            int bestIdx = -1;
            for (int p = 0; p < (int)m_Probes.size(); p++)
            {
                float d = glm::length(m_Probes[p].Position - cornerPos);
                if (d < bestDist)
                {
                    bestDist = d;
                    bestIdx = p;
                }
            }

            if (bestIdx >= 0 && m_Probes[bestIdx].Valid && bestDist < spacing * 0.5f)
            {
                float w = weights[c];
                for (int sh = 0; sh < 9; sh++)
                    totalSH[sh] += m_Probes[bestIdx].SH.Coeffs[sh] * w;
                totalWeight += w;
            }
        }

        if (totalWeight > 0.0f)
        {
            SHL2Coefficients interpolated;
            for (int i = 0; i < 9; i++)
                interpolated.Coeffs[i] = totalSH[i] / totalWeight;

            outIrradiance = interpolated.Evaluate(normal);
        }
    }

    void AdaptiveProbeVolume::ReportProgress(float progress, const std::string& step)
    {
        m_Progress = progress;
        if (m_ProgressCallback)
            m_ProgressCallback(progress, step);
    }

    std::shared_ptr<AdaptiveProbeVolume> AdaptiveProbeVolume::Create(const AdaptiveProbeVolumeSettings& settings)
    {
        return std::make_shared<AdaptiveProbeVolume>(settings);
    }
}
