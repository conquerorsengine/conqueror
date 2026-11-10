#include "Scene/MeshRendererMaterial.h"
#include "Renderer/Utilities/Renderer3D/Material.h"
#include "ShadowPass.h"
#include "Scene/Scene.h"
#include "Scene/Entity.h"
#include "Renderer/Utilities/Renderer3D/Renderer3D.h"
#include "Renderer/Utilities/Renderer3D/Mesh.h"
#include "Renderer/Utilities/Renderer3D/SkinnedMesh.h"
#include "Scene/BlendShapes.h"
#include "Scene/MetaHuman.h"
#include "Renderer/Utilities/Renderer3D/ModelLoader.h"
#include "Renderer/Utilities/Renderer3D/ClusterMesh.h"
#include "Scene/VirtualGeometry.h"
#include "Scene/MultiMesh.h"
#include "Renderer/RHI/RenderCommand.h"
#include "Renderer/TextureSlots.h"
#include "Renderer/RHI/Texture.h"
#include "Renderer/Utilities/Renderer3D/Material.h"
#include "Renderer/Renderer.h"
#include "Core/Logging/Log.h"

#include <unordered_map>
#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

namespace Conqueror
{
    static const char* s_DepthVertexSrc = R"(
        #version 450 core
        layout(location = 0) in vec3 a_Position;
        uniform mat4 u_LightSpaceMatrix;
        uniform mat4 u_Transform;
        void main()
        {
            gl_Position = u_LightSpaceMatrix * u_Transform * vec4(a_Position, 1.0);
        }
    )";

    // MultiMesh örnekleri: yerel örnek matrisi 8..11 özniteliklerinde
    static const char* s_InstancedDepthVertexSrc = R"(
        #version 450 core
        layout(location = 0) in vec3 a_Position;
        layout(location = 8) in vec4 a_Instance0;
        layout(location = 9) in vec4 a_Instance1;
        layout(location = 10) in vec4 a_Instance2;
        layout(location = 11) in vec4 a_Instance3;
        uniform mat4 u_LightSpaceMatrix;
        uniform mat4 u_Transform;
        void main()
        {
            gl_Position = u_LightSpaceMatrix * u_Transform * mat4(a_Instance0, a_Instance1, a_Instance2, a_Instance3) * vec4(a_Position, 1.0);
        }
    )";

    // Skinning.glsl ile aynı palet / blend shape okuma (kaynak dizesi olduğu için #include yerine burada)
    static const char* s_SkinnedDepthVertexSrc = R"(
        #version 450 core
        layout(location = 0) in vec3 a_Position;
        layout(location = 4) in ivec4 a_BoneIDs;
        layout(location = 5) in vec4 a_BoneWeights;
        uniform mat4 u_LightSpaceMatrix;
        uniform mat4 u_Transform;
        uniform mat4 u_BoneTransforms[128];
        uniform int u_BoneTexture = 0;
        uniform int u_BoneCount = 0;
        uniform sampler2D u_BonePalette;
        uniform int u_HasMorph = 0;
        uniform sampler2D u_MorphDeltas;
        mat4 Bone(int id)
        {
            if (u_BoneTexture == 1)
            {
                int t = id * 4;
                ivec2 p = ivec2(t % 1024, t / 1024);
                return mat4(texelFetch(u_BonePalette, p, 0), texelFetch(u_BonePalette, p + ivec2(1, 0), 0),
                            texelFetch(u_BonePalette, p + ivec2(2, 0), 0), texelFetch(u_BonePalette, p + ivec2(3, 0), 0));
            }
            return u_BoneTransforms[id];
        }
        void main()
        {
            int limit = u_BoneTexture == 1 ? u_BoneCount : 128;
            mat4 skin = mat4(0.0);
            float total = 0.0;
            for (int i = 0; i < 4; ++i)
            {
                float w = a_BoneWeights[i];
                int b = a_BoneIDs[i];
                if (w <= 0.0 || b < 0 || b >= limit) continue;
                skin += Bone(b) * w;
                total += w;
            }
            skin = total < 1e-5 ? mat4(1.0) : skin / total;
            vec3 position = a_Position;
            if (u_HasMorph == 1)
            {
                int t = gl_VertexID * 2;
                position += texelFetch(u_MorphDeltas, ivec2(t % 1024, t / 1024), 0).xyz;
            }
            gl_Position = u_LightSpaceMatrix * u_Transform * skin * vec4(position, 1.0);
        }
    )";

    static const char* s_DepthFragmentSrc = R"(
        #version 450 core
        void main() { }
    )";

    // Alfa kırpmalı (Cutout) yüzeyler: yaprak kartları, tel örgü, delikli levhalar gölgede de delikli düşer
    static const char* s_CutoutDepthVertexSrc = R"(
        #version 450 core
        layout(location = 0) in vec3 a_Position;
        layout(location = 3) in vec2 a_TexCoord;
        uniform mat4 u_LightSpaceMatrix;
        uniform mat4 u_Transform;
        uniform vec2 u_UVScale;
        uniform vec2 u_UVOffset;
        out vec2 v_UV;
        void main()
        {
            v_UV = a_TexCoord * u_UVScale + u_UVOffset;
            gl_Position = u_LightSpaceMatrix * u_Transform * vec4(a_Position, 1.0);
        }
    )";

    static const char* s_CutoutDepthFragmentSrc = R"(
        #version 450 core
        in vec2 v_UV;
        uniform sampler2D u_AlbedoMap;
        uniform float u_AlphaClip;
        uniform float u_Alpha;
        void main()
        {
            if (texture(u_AlbedoMap, v_UV).a * u_Alpha < u_AlphaClip)
                discard;
        }
    )";

    ShadowPass::ShadowPass()
    {
    }

    bool ShadowPass::CutoutReady() const
    {
        return m_CutoutDepthShader && m_CutoutDepthShader->IsValid();
    }

    void ShadowPass::DrawCutout(const Mesh& mesh, const glm::mat4& transform, const glm::mat4& lightSpace, Texture2D& albedo,
                                float alphaClip, float alpha, const glm::vec2& uvScale, const glm::vec2& uvOffset)
    {
        m_CutoutDepthShader->Bind();
        m_CutoutDepthShader->SetMat4("u_LightSpaceMatrix", lightSpace);
        m_CutoutDepthShader->SetMat4("u_Transform", transform);
        m_CutoutDepthShader->SetFloat2("u_UVScale", uvScale);
        m_CutoutDepthShader->SetFloat2("u_UVOffset", uvOffset);
        m_CutoutDepthShader->SetFloat("u_AlphaClip", alphaClip);
        m_CutoutDepthShader->SetFloat("u_Alpha", alpha);
        albedo.Bind(0);
        m_CutoutDepthShader->SetInt("u_AlbedoMap", 0);
        RenderCommand::DrawIndexed(mesh.GetVertexArray(), mesh.GetIndexCount());
    }

    void ShadowPass::Init()
    {
        m_DirectionalShadowMap = ShadowMap::Create(4096, 4096);
        m_DepthShader = Shader::Create("ShadowDepthShader", s_DepthVertexSrc, s_DepthFragmentSrc);
        m_SkinnedDepthShader = Shader::Create("ShadowDepthSkinnedShader", s_SkinnedDepthVertexSrc, s_DepthFragmentSrc);
        m_InstancedDepthShader = Shader::Create("ShadowDepthInstancedShader", s_InstancedDepthVertexSrc, s_DepthFragmentSrc);
        m_CutoutDepthShader = Shader::Create("ShadowDepthCutoutShader", s_CutoutDepthVertexSrc, s_CutoutDepthFragmentSrc);
        m_LightSpaceMatrices.assign(kMaxCascades, glm::mat4(1.0f));
        m_CascadeSplits.assign(kMaxCascades, 0.0f);
        m_CascadeTexelWorld.assign(kMaxCascades, 0.05f);
        m_CascadeRects.assign(kMaxCascades, glm::vec4(0.0f, 0.0f, 1.0f, 1.0f));
    }

    void ShadowPass::Shutdown()
    {
        m_DirectionalShadowMap.reset();
        m_DepthShader.reset();
        m_SkinnedDepthShader.reset();
        m_InstancedDepthShader.reset();
        m_CutoutDepthShader.reset();
        m_LightSpaceMatrices.clear();
        m_CascadeSplits.clear();
        m_CascadeTexelWorld.clear();
        m_CascadeRects.clear();
        m_ActiveCascades = 0;
    }

    uint32_t ShadowPass::GetAtlasTextureID() const
    {
        return m_DirectionalShadowMap ? m_DirectionalShadowMap->GetDepthTextureID() : 0;
    }

    void ShadowPass::EnsureResolution(int resolution)
    {
        resolution = std::clamp(resolution, 512, 8192);
        if (!m_DirectionalShadowMap || (int)m_DirectionalShadowMap->GetWidth() != resolution)
            m_DirectionalShadowMap = ShadowMap::Create((uint32_t)resolution, (uint32_t)resolution);
    }

    void ShadowPass::Execute(Scene* scene, const DirectionalLightComponent& dirLight,
                              const glm::vec3& lightDirection,
                              const glm::mat4& cameraView, const glm::mat4& cameraProj,
                              const glm::vec3& cameraPos)
    {
        ShadowSettings settings;
        settings.Enabled = dirLight.CastShadows;
        Execute(scene, lightDirection, cameraView, cameraProj, cameraPos, settings);
    }

    void ShadowPass::Execute(Scene* scene, const glm::vec3& lightDirection,
                             const glm::mat4& cameraView, const glm::mat4& cameraProj,
                             const glm::vec3& cameraPos, const ShadowSettings& settings)
    {
        m_ActiveCascades = 0;
        if (!scene || !m_DepthShader || !m_DepthShader->IsValid() || !settings.Enabled)
            return;
        if (glm::dot(lightDirection, lightDirection) < 1e-8f)
            return;

        m_LastSettings = settings;
        EnsureResolution(settings.Resolution);

        const int cascades = std::clamp(settings.CascadeCount, 1, kMaxCascades);
        const float shadowFar = std::max(settings.Distance, 1.0f);
        const float shadowNear = 0.1f;
        CalculateCascadeSplits(shadowNear, shadowFar, cascades, settings.CascadeSplitLambda);

        // Atlas yerleşimi
        const int cols = cascades == 1 ? 1 : 2;
        const int rows = cascades <= 2 ? 1 : 2;
        const uint32_t atlas = m_DirectionalShadowMap->GetWidth();
        const uint32_t tileW = atlas / cols;
        const uint32_t tileH = atlas / rows;

        GLint prevViewport[4];
        GLint prevFBO;
        glGetIntegerv(GL_VIEWPORT, prevViewport);
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);
        GLboolean cullWasEnabled = glIsEnabled(GL_CULL_FACE);
        GLint prevCullMode = GL_BACK;
        glGetIntegerv(GL_CULL_FACE_MODE, &prevCullMode);

        m_DirectionalShadowMap->Bind();
        glViewport(0, 0, atlas, atlas);
        glClear(GL_DEPTH_BUFFER_BIT);
        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_TRUE);
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        // Eğimli yüzeylerde akne'yi bastır
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(1.5f, 2.0f);

        const glm::vec3 lightDir = glm::normalize(lightDirection);
        // Işık neredeyse dikeyse 'up' vektörünü değiştir (lookAt NaN üretmesin)
        const glm::vec3 up = std::abs(lightDir.y) > 0.99f ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);

        const glm::mat4 invCamVP = glm::inverse(cameraProj * cameraView);
        glm::vec3 worldNear[4], worldFar[4];
        const glm::vec2 ndc[4] = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } };
        for (int k = 0; k < 4; ++k)
        {
            glm::vec4 n = invCamVP * glm::vec4(ndc[k], -1.0f, 1.0f);
            glm::vec4 f = invCamVP * glm::vec4(ndc[k], 1.0f, 1.0f);
            worldNear[k] = glm::vec3(n) / n.w;
            worldFar[k] = glm::vec3(f) / f.w;
        }
        // Kamera ışınlarını gölge mesafesine göre yeniden ölçekle (yalnızca yön kullanılır)
        glm::vec3 camForward = -glm::vec3(glm::inverse(cameraView)[2]);
        glm::vec3 rayDir[4];
        for (int k = 0; k < 4; ++k)
        {
            glm::vec3 d = worldFar[k] - worldNear[k];
            float along = glm::dot(d, camForward);
            rayDir[k] = std::abs(along) > 1e-5f ? d / along : glm::normalize(d); // ileri ekseninde birim
        }

        auto& registry = scene->m_Registry;
        auto meshView = registry.view<TransformComponent, MeshRendererComponent>();
        auto modelView = registry.view<TransformComponent, MeshRendererComponent>();

        // Kademe kırpması: her kademe yalnız kendi ışık hacmine giren gölge düşürenleri çizer (GPU zaten kırpardı; burada çizim
        // çağrısı hiç yapılmaz). İskeletli modellerin poz kutusu pahalıdır: karede bir kez hesaplanır.
        const bool cull = Renderer3D::GetFrustumCulling();
        struct CasterBox { glm::vec3 Center{ 0.0f }, Extents{ 0.0f }; bool Valid = false; };
        static thread_local std::unordered_map<uint32_t, CasterBox> s_SkinnedBoxes;
        s_SkinnedBoxes.clear();
        glm::vec4 planes[6];
        auto outside = [&](const Mesh& mesh, const glm::mat4& world) {
            if (!cull || !(mesh.GetBoundsRadius() > 0.0f))
                return false;
            glm::vec3 c, e;
            Renderer3D::TransformBox(world, mesh.GetBoundsCenter(), mesh.GetBoundsExtents(), c, e);
            return !Renderer3D::BoxInFrustum(planes, c, e);
        };

        for (int i = 0; i < cascades; ++i)
        {
            float splitNear = (i == 0) ? shadowNear : m_CascadeSplits[i - 1];
            float splitFar = m_CascadeSplits[i];

            glm::vec3 corners[8];
            for (int k = 0; k < 4; ++k)
            {
                corners[k] = cameraPos + rayDir[k] * splitNear;
                corners[k + 4] = cameraPos + rayDir[k] * splitFar;
            }

            // Kararlı sınırlayıcı küre (kamera dönerken gölge yüzmez)
            glm::vec3 center(0.0f);
            for (auto& c : corners) center += c;
            center /= 8.0f;
            float radius = 0.0f;
            for (auto& c : corners) radius = std::max(radius, glm::length(c - center));
            radius = std::ceil(radius * 16.0f) / 16.0f;
            radius = std::max(radius, 1.0f);

            const float texelSize = (radius * 2.0f) / (float)tileW;
            m_CascadeTexelWorld[i] = texelSize;

            // Merkezi texel ızgarasına oturt. Izgara dünya orijinine bağlı, yalnız dönen bir ışık uzayında olmalı:
            // merkeze göre kurulan bir görünümde merkez hep (0,0)'a düşer ve yuvarlama hiçbir şey yapmaz
            // (kamera her kıpırdadığında gölge kenarları titrer).
            const glm::mat4 lightRot = glm::lookAt(glm::vec3(0.0f), lightDir, up);
            glm::vec4 centerLS = lightRot * glm::vec4(center, 1.0f);
            centerLS.x = std::floor(centerLS.x / texelSize + 0.5f) * texelSize;
            centerLS.y = std::floor(centerLS.y / texelSize + 0.5f) * texelSize;
            center = glm::vec3(glm::inverse(lightRot) * centerLS);

            // Kamera arkasındaki yüksek dağlar/binalar da gölge düşürsün diye z aralığı geniş
            const float zBack = radius * 6.0f;
            glm::mat4 lightView = glm::lookAt(center - lightDir * zBack, center, up);
            glm::mat4 lightProjection = glm::ortho(-radius, radius, -radius, radius, 0.0f, zBack + radius * 2.0f);
            m_LightSpaceMatrices[i] = lightProjection * lightView;

            const int col = i % cols;
            const int row = i / cols;
            m_CascadeRects[i] = glm::vec4((float)col / cols, (float)row / rows, 1.0f / cols, 1.0f / rows);
            glViewport(col * tileW, row * tileH, tileW, tileH);

            m_DepthShader->Bind();
            m_DepthShader->SetMat4("u_LightSpaceMatrix", m_LightSpaceMatrices[i]);
            Renderer3D::ExtractFrustum(m_LightSpaceMatrices[i], planes);

            for (auto entity : meshView)
            {
                if (!scene->IsEntityActive(entity))
                    continue;
                auto [transform, meshRenderer] = meshView.get<TransformComponent, MeshRendererComponent>(entity);
                if (meshRenderer.IsModel()) continue;   // model ağı model döngüsünde
                // Çoğunlukla saydam yüzeyler (cam) ışığı kesmez
                const Material& mat = MeshRendererMaterial::Shared(meshRenderer);
                if (!meshRenderer.CastShadows || (mat.Rendering == Material::RenderMode::Transparent && mat.Opacity < 0.5f))
                    continue;

                // Prosedürel ağ (kumaş, Spline Extrude...) varsa çizimdeki gibi o kullanılır; yoksa ilkel ağ
                std::shared_ptr<Mesh> mesh = meshRenderer.CustomMesh;
                if (!mesh)
                {
                    switch (meshRenderer.Type)
                    {
                        case MeshType::Sphere:   mesh = Renderer3D::GetSphereMesh(); break;
                        case MeshType::Plane:    mesh = Renderer3D::GetPlaneMesh(); break;
                        case MeshType::Cylinder: mesh = Renderer3D::GetCylinderMesh(); break;
                        default:                 mesh = Renderer3D::GetCubeMesh(); break;
                    }
                }
                if (!mesh || outside(*mesh, transform.WorldTransform))
                    continue;
                // Düzlem ve prosedürel yüzeyler (kumaş gibi tek katman) tek yüzlü olabilir; arka yüz ayıklaması onları
                // gölgeden silmesin
                const bool twoSided = meshRenderer.CustomMesh || meshRenderer.Type == MeshType::Plane;
                const Mesh* lod = mesh->HasLODs() ? mesh->GetLOD(Renderer3D::SelectMeshLOD(*mesh, transform.WorldTransform)) : mesh.get();
                // Alfa kırpmalı doku (nesne UV'si): delikler gölgede de delik (dünyaya hizalı UV'de tam gölge)
                if (mat.Rendering == Material::RenderMode::Cutout && mat.AlbedoMap && !mat.WorldAligned && CutoutReady())
                {
                    glDisable(GL_CULL_FACE);
                    DrawCutout(*lod, transform.WorldTransform, m_LightSpaceMatrices[i], *mat.AlbedoMap,
                               mat.AlphaClip, mat.Opacity, mat.Tiling, mat.Offset);
                    glEnable(GL_CULL_FACE);
                    m_DepthShader->Bind();
                    Renderer::GetStats().ShadowDrawCalls++;
                    continue;
                }
                if (twoSided) glDisable(GL_CULL_FACE);
                m_DepthShader->SetMat4("u_Transform", transform.WorldTransform);
                RenderCommand::DrawIndexed(lod->GetVertexArray(), lod->GetIndexCount());
                if (twoSided) glEnable(GL_CULL_FACE);
                Renderer::GetStats().ShadowDrawCalls++;
            }

            // Terrain gölgeleri (mesh ilk çizimde oluşturulur)
            {
                auto terrainView = registry.view<TransformComponent, TerrainComponent>();
                for (auto entity : terrainView)
                {
                    if (!scene->IsEntityActive(entity))
                        continue;
                    auto [transform, terrain] = terrainView.get<TransformComponent, TerrainComponent>(entity);
                    if (!terrain.CastShadows || !terrain.RuntimeMesh || outside(*terrain.RuntimeMesh, transform.WorldTransform))
                        continue;
                    m_DepthShader->Bind();
                    m_DepthShader->SetMat4("u_LightSpaceMatrix", m_LightSpaceMatrices[i]);
                    m_DepthShader->SetMat4("u_Transform", transform.WorldTransform);
                    RenderCommand::DrawIndexed(terrain.RuntimeMesh->GetVertexArray(), terrain.RuntimeMesh->GetIndexCount());
                    Renderer::GetStats().ShadowDrawCalls++;
                }
            }

            for (auto entity : modelView)
            {
                if (!scene->IsEntityActive(entity))
                    continue;
                auto [transform, modelComponent] = modelView.get<TransformComponent, MeshRendererComponent>(entity);
                auto& model = modelComponent.ModelData;
                if (!model || !modelComponent.CastShadows)
                    continue;

                if (model->IsSkinned && model->SkeletonData && !model->SkinnedMeshes.empty() && m_SkinnedDepthShader && m_SkinnedDepthShader->IsValid())
                {
                    Entity e{ entity, scene };
                    const std::vector<glm::mat4>* bones = nullptr;
                    if (const auto* lp = registry.try_get<LeaderPoseComponent>(entity); lp && !lp->BoneMatrices.empty())
                        bones = &lp->BoneMatrices;
                    else if (e.HasComponent<AnimatorComponent>())
                        bones = &e.GetComponent<AnimatorComponent>().BoneMatricesGPU;
                    else if (e.HasComponent<AnimationComponent>())
                        bones = &e.GetComponent<AnimationComponent>().BoneMatricesGPU;
                    const uint32_t boneCount = model->SkeletonData->GetBoneCount();

                    if (cull)
                    {
                        auto it = s_SkinnedBoxes.find((uint32_t)entity);
                        if (it == s_SkinnedBoxes.end())
                        {
                            // Aşağıdaki yükleme ile aynı palet: geçerli kemikler ya da 128 birim matris
                            const bool posed = bones && bones->size() >= boneCount && boneCount > 0;
                            CasterBox box;
                            glm::vec3 c, ext;
                            if (Renderer3D::SkinnedModelBounds(*model, posed ? bones->data() : nullptr, posed ? boneCount : 0u,
                                                               posed ? boneCount : 128u, c, ext))
                            {
                                Renderer3D::TransformBox(transform.WorldTransform, c, ext, box.Center, box.Extents);
                                box.Valid = true;
                            }
                            it = s_SkinnedBoxes.emplace((uint32_t)entity, box).first;
                        }
                        if (it->second.Valid && !Renderer3D::BoxInFrustum(planes, it->second.Center, it->second.Extents))
                            continue;
                    }

                    m_SkinnedDepthShader->Bind();
                    m_SkinnedDepthShader->SetMat4("u_LightSpaceMatrix", m_LightSpaceMatrices[i]);
                    m_SkinnedDepthShader->SetMat4("u_Transform", transform.WorldTransform);
                    if (bones && bones->size() >= boneCount && boneCount > 0)
                    {
                        Renderer3D::SetBonePalette(m_SkinnedDepthShader, bones->data(), boneCount);
                    }
                    else
                    {
                        static std::vector<glm::mat4> s_Identity;
                        s_Identity.assign(std::max<uint32_t>(boneCount, 1u), glm::mat4(1.0f));
                        Renderer3D::SetBonePalette(m_SkinnedDepthShader, s_Identity.data(), (uint32_t)s_Identity.size());
                    }
                    const std::vector<uint32_t>* morph = BlendShapes::GetTextures(*scene, (uint32_t)entity);
                    for (size_t mi = 0; mi < model->SkinnedMeshes.size(); ++mi)
                    {
                        const auto& mesh = model->SkinnedMeshes[mi];
                        Renderer3D::SetMorphDeltas(m_SkinnedDepthShader, morph && mi < morph->size() ? (*morph)[mi] : 0u);
                        RenderCommand::DrawIndexed(mesh->GetVertexArray(), mesh->GetIndexCount());
                        Renderer::GetStats().ShadowDrawCalls++;
                    }
                    Renderer3D::SetMorphDeltas(m_SkinnedDepthShader, 0u);
                    m_DepthShader->Bind();
                    m_DepthShader->SetMat4("u_LightSpaceMatrix", m_LightSpaceMatrices[i]);
                    continue;
                }

                m_DepthShader->SetMat4("u_Transform", transform.WorldTransform);
                // Virtual Geometry: gölgede daha kaba hata eşiğiyle küme seçimi (ana kameraya göre)
                if (auto* vg = registry.try_get<VirtualGeometryComponent>(entity); vg && vg->Enabled && vg->Ready)
                {
                    const RenderCamera& cam = Renderer3D::GetCamera();
                    const glm::mat4& world = transform.WorldTransform;
                    const glm::vec3 cameraLocal = glm::vec3(glm::inverse(world) * glm::vec4(cam.Position, 1.0f));
                    const float scale = std::max({ glm::length(glm::vec3(world[0])), glm::length(glm::vec3(world[1])), glm::length(glm::vec3(world[2])) });
                    const float pixelScale = std::abs(cam.Projection[1][1]) * (float)std::max(cam.ViewportHeight, 1u) * 0.5f;
                    static thread_local std::vector<uint32_t> s_Selected;
                    bool drawnAll = true;
                    for (auto& mesh : model->Meshes)
                    {
                        auto clusters = VirtualGeometry::Acquire(mesh, vg->ClusterSize);
                        if (!clusters || !clusters->IsUploaded()) { drawnAll = false; break; }
                        // Kümeler de kademenin ışık hacmiyle kırpılır
                        clusters->Select(cameraLocal, scale, pixelScale, vg->ShadowErrorThreshold, cam.Orthographic, world,
                                         cull ? planes : nullptr, s_Selected);
                        clusters->Draw(s_Selected);
                        Renderer::GetStats().ShadowDrawCalls++;
                    }
                    if (drawnAll)
                        continue;
                }
                for (size_t mi = 0; mi < model->Meshes.size(); ++mi)
                {
                    auto& mesh = model->Meshes[mi];
                    if (!mesh || outside(*mesh, transform.WorldTransform))
                        continue;
                    // Gölge de otomatik LOD'u kullanır (ucuz derinlik çizimi)
                    const Mesh* lod = mesh->HasLODs() ? mesh->GetLOD(Renderer3D::SelectMeshLOD(*mesh, transform.WorldTransform)) : mesh.get();
                    const Material* mat = mi < model->Materials.size() ? model->Materials[mi].get() : nullptr;
                    if (mat && mat->Rendering == Material::RenderMode::Cutout && mat->AlbedoMap && CutoutReady())
                    {
                        // Yaprak kartı / tel örgü: albedo alfasıyla kırpılır; çift yüzlü malzemede iki yüz de gölge düşürür
                        const bool open = mat->Culling == Material::CullMode::Off;
                        if (open) glDisable(GL_CULL_FACE);
                        DrawCutout(*lod, transform.WorldTransform, m_LightSpaceMatrices[i], *mat->AlbedoMap, mat->AlphaClip, 1.0f,
                                   glm::vec2(1.0f), glm::vec2(0.0f));
                        if (open) glEnable(GL_CULL_FACE);
                        m_DepthShader->Bind();
                        Renderer::GetStats().ShadowDrawCalls++;
                        continue;
                    }
                    RenderCommand::DrawIndexed(lod->GetVertexArray(), lod->GetIndexCount());
                    Renderer::GetStats().ShadowDrawCalls++;
                }
            }

            // MultiMesh: her toplu örnek kümesi tek çağrıda
            if (m_InstancedDepthShader && m_InstancedDepthShader->IsValid())
            {
                m_InstancedDepthShader->Bind();
                m_InstancedDepthShader->SetMat4("u_LightSpaceMatrix", m_LightSpaceMatrices[i]);
                // Örnek kümeleri de kademenin ışık hacmiyle kırpılır
                MultiMesh::ForEachShadowBatch(*scene, cull ? planes : nullptr, [&](const Mesh& mesh, const glm::mat4& world, uint32_t buffer,
                                                                                   uint32_t first, uint32_t count, bool doubleSided) {
                    m_InstancedDepthShader->SetMat4("u_Transform", world);
                    if (doubleSided) glDisable(GL_CULL_FACE);
                    Renderer3D::BindInstanceAttributes(mesh, buffer, first);
                    glDrawElementsInstanced(GL_TRIANGLES, (GLsizei)mesh.GetIndexCount(), GL_UNSIGNED_INT, nullptr, (GLsizei)count);
                    Renderer3D::UnbindInstanceAttributes(mesh);
                    if (doubleSided) glEnable(GL_CULL_FACE);
                    Renderer::GetStats().ShadowDrawCalls++;
                });
            }
        }

        m_ActiveCascades = cascades;

        glDisable(GL_POLYGON_OFFSET_FILL);
        if (!cullWasEnabled)
            glDisable(GL_CULL_FACE);
        glCullFace(prevCullMode);
        glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
        glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
    }

    void ShadowPass::BindShadowMapsToShader(std::shared_ptr<Shader> shader)
    {
        if (!shader || !m_DirectionalShadowMap)
            return;
        shader->Bind();
        glActiveTexture(GL_TEXTURE0 + TextureSlot::ShadowAtlas);
        glBindTexture(GL_TEXTURE_2D, m_DirectionalShadowMap->GetDepthTextureID());
        shader->SetInt("u_ShadowMap", (int)TextureSlot::ShadowAtlas);
    }

    void ShadowPass::CalculateCascadeSplits(float nearPlane, float farPlane, int count, float lambda)
    {
        const float range = farPlane - nearPlane;
        const float ratio = farPlane / nearPlane;
        for (int i = 0; i < count; ++i)
        {
            float p = (float)(i + 1) / (float)count;
            float logSplit = nearPlane * std::pow(ratio, p);
            float uniformSplit = nearPlane + range * p;
            m_CascadeSplits[i] = lambda * logSplit + (1.0f - lambda) * uniformSplit;
        }
        for (int i = count; i < kMaxCascades; ++i)
            m_CascadeSplits[i] = farPlane;
    }
}
