#include "Renderer3D.h"
#include "ClusterMesh.h"
#include "Renderer/PostProcess.h"
#include "Core/Tiering/QualitySettings.h"
#include "RendererUBO.h"
#include "Renderer/Renderer.h"
#include "Renderer/TextureSlots.h"
#include "ModelLoader.h"
#include "SkinnedMesh.h"
#include "Renderer/RHI/RenderCommand.h"
#include "Renderer/RHI/Cubemap.h"
#include "Scene/Scene.h"
#include "Core/Logging/Log.h"

#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <cstdlib>
#include <cstdio>
#include <limits>
#include <unordered_map>
#include <unordered_set>

namespace Conqueror
{
    ShadowPass Renderer3D::s_ShadowPass;
    std::shared_ptr<Mesh> Renderer3D::s_CubeMesh = nullptr;
    std::shared_ptr<Mesh> Renderer3D::s_PlaneMesh = nullptr;
    std::shared_ptr<Mesh> Renderer3D::s_CylinderMesh = nullptr;
    std::shared_ptr<Mesh> Renderer3D::s_SphereMesh = nullptr;
    std::shared_ptr<Mesh> Renderer3D::s_ConeMesh = nullptr;
    std::shared_ptr<Mesh> Renderer3D::s_ArrowMesh = nullptr;
    std::shared_ptr<Shader> Renderer3D::s_PBRShader = nullptr;
    std::shared_ptr<Shader> Renderer3D::s_PBRSkinnedShader = nullptr;
    std::shared_ptr<Shader> Renderer3D::s_UnlitShader = nullptr;
    std::shared_ptr<Shader> Renderer3D::s_TerrainShader = nullptr;
    // GL bağlamı kapanmadan Shutdown'da bırakılır (statik yıkıcı sırası sorunu)
    static std::shared_ptr<Texture2D> s_TerrainWhite;
    // Editör simgeleri (ışık / kamera): Shutdown'da GL bağlamı kapanmadan bırakılır (statik yıkıcıda glDeleteTextures çökertiyordu)
    static std::shared_ptr<Shader> s_IconShader;
    static std::unordered_map<std::string, std::shared_ptr<Texture2D>> s_Icons;
    std::shared_ptr<Shader> Renderer3D::s_SkyboxShader = nullptr;

    namespace
    {
        struct ProbeEntry
        {
            glm::vec3 Position;
            glm::vec3 BoxMin;
            glm::vec3 BoxMax;
            uint32_t CubemapID;
            uint32_t MipCount;
            float Intensity;
            float BlendDistance;
            std::shared_ptr<Cubemap> Keepalive;
        };

        struct TransparentItem
        {
            std::shared_ptr<Mesh> MeshRef;
            glm::mat4 Transform;
            std::shared_ptr<Material> MaterialRef;
            bool DoubleSided;
            float Distance;
            uint32_t VertexColors = 0;   // Mesh Paint tamponu (0 = yok)
            glm::vec3 Tint{ 1.0f };      // örnek renk çarpanı (Model.Tint)
            bool Moving = false;         // Temporal AA hareket bayrağı
        };

        // Kuyruğa alınan opak çizim: çizim anındaki örnek durumu (köşe rengi, renk çarpanı, gölge alma) yakalanır
        struct OpaqueItem
        {
            std::shared_ptr<Mesh> MeshRef;
            glm::mat4 Transform;
            std::shared_ptr<Material> MaterialRef;
            float Distance2;             // (kamera - sınır merkezi uzaklığı + dünya yarıçapı)^2
            uint32_t SortKey;            // 0 opak, 1 alfa kesmeli (opaklardan sonra)
            uint32_t VertexColors;
            glm::vec3 Tint;
            bool DoubleSided;
            bool ReceiveShadows;
            bool Moving;                 // Temporal AA hareket bayrağı
        };

        // Mesh Paint: sonraki çizimlerin mesh sırasına göre köşe rengi tamponları
        const std::vector<uint32_t>* s_VertexColorBuffers = nullptr;
        size_t s_VertexColorMesh = 0;
        constexpr GLuint kVertexColorAttrib = 6;
        constexpr GLuint kInstanceAttrib = 8;            // 8..11 matris sütunları, 12 renk
        constexpr GLsizei kInstanceStride = sizeof(float) * 20;

        struct State
        {
            RenderCamera Camera;
            SceneEnvironment Env;
            bool FrameActive = false;
            bool FrameDirty = true;
            float TextureLodBias = 0.0f;   // malzeme dokusu mip sapması (BeginScene'de, sahne hedefi bağlıyken alınır)
            // Görüş kırpması: kameranın kesik koni düzlemleri (kamera değişince yeniden çıkarılır)
            glm::vec4 Frustum[6]{};
            bool FrustumDirty = true;
            uint64_t FrameIndex = 1;

            FrameUBO Frame{};
            LightsUBO Lights{};
            int PointCount = 0;
            int SpotCount = 0;
            int AreaCount = 0;
            DirectionalLightComponent Sun;
            bool HasSun = false;

            std::vector<ProbeEntry> Probes;
            std::vector<TransparentItem> Transparent;
            std::vector<OpaqueItem> Opaque;
            std::unordered_map<const Shader*, uint64_t> ShaderFrame;

            uint32_t FrameUBOID = 0;
            uint32_t LightsUBOID = 0;
            uint32_t FallbackCube = 0;
            uint32_t FallbackBlack2D = 0;

            // IBL önbelleği
            std::weak_ptr<Cubemap> CachedSkybox;
            uint32_t CachedSkyboxID = 0;
            std::shared_ptr<EnvironmentMap> EnvMap;
            bool EnvIsProcedural = false;
            ProceduralSkySettings LastSky;
            std::chrono::steady_clock::time_point LastProceduralUpdate{};

            // Geriye uyumluluk
            std::shared_ptr<Texture2D> GlobalLightmap;
            std::shared_ptr<AdaptiveProbeVolume> GlobalAPV;

            // APV dokuları
            uint32_t APVPositionTex = 0;
            uint32_t APVSHTex = 0;
            int APVLastProbeCount = 0;
            const AdaptiveProbeVolume* APVUploaded = nullptr;
            int APVProbeCount = 0;

            uint64_t EnvCheckedFrame = 0;
            bool ReceiveShadows = true;
            glm::vec3 Tint{ 1.0f };
            int DebugView = 0;
            bool Wireframe = false;
            glm::mat4 GizmoViewProjection = glm::mat4(1.0f);
            // Decal derinlik kopyası (MSAA çözülmüş, örneklenebilir)
            uint32_t DecalDepthTex = 0, DecalDepthFBO = 0;
            int DecalW = 0, DecalH = 0;
            int DecalFormat = 0;
            std::shared_ptr<Shader> DecalShader;
            std::chrono::steady_clock::time_point LastHotReloadCheck{};
        };

        State* s = nullptr;

        uint32_t CreateFallbackCube()
        {
            uint32_t id = 0;
            glGenTextures(1, &id);
            glBindTexture(GL_TEXTURE_CUBE_MAP, id);
            const float black[4] = { 0, 0, 0, 1 };
            for (int f = 0; f < 6; ++f)
                glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + f, 0, GL_RGBA16F, 1, 1, 0, GL_RGBA, GL_FLOAT, black);
            glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            return id;
        }

        uint32_t CreateFallback2D()
        {
            uint32_t id = 0;
            glGenTextures(1, &id);
            glBindTexture(GL_TEXTURE_2D, id);
            const float black[4] = { 0, 0, 0, 0 };
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, 1, 1, 0, GL_RGBA, GL_FLOAT, black);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            return id;
        }

        bool SceneHasLightmap()
        {
            return s->Env.Lightmap != nullptr;
        }

        bool LightIsBakedAway(LightMode mode)
        {
            return mode == LightMode::Baked && SceneHasLightmap();
        }

        glm::mat4 OrientationFromDirection(const glm::vec3& direction)
        {
            glm::vec3 dir = glm::normalize(direction);
            glm::vec3 up = std::abs(dir.y) > 0.999f ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
            glm::vec3 right = glm::normalize(glm::cross(up, dir));
            glm::vec3 newUp = glm::cross(dir, right);
            return glm::mat4(glm::vec4(right, 0.0f), glm::vec4(dir, 0.0f), glm::vec4(newUp, 0.0f), glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
        }
    }

    // ------------------------------------------------------------------ RenderCamera

    RenderCamera RenderCamera::FromEditorCamera(const EditorCamera& camera, uint32_t width, uint32_t height)
    {
        RenderCamera rc;
        rc.View = camera.GetViewMatrix();
        rc.Projection = camera.GetProjectionMatrix();
        rc.Position = camera.GetPosition();
        rc.ViewportWidth = std::max(width, 1u);
        rc.ViewportHeight = std::max(height, 1u);
        rc.Orthographic = camera.Is2D();
        return rc;
    }

    RenderCamera RenderCamera::FromSceneCamera(const SceneCamera& camera, const glm::mat4& worldTransform, uint32_t width, uint32_t height)
    {
        RenderCamera rc;
        rc.View = glm::inverse(worldTransform);
        rc.Projection = camera.GetProjection();
        rc.Position = glm::vec3(worldTransform[3]);
        rc.Orthographic = camera.GetProjectionType() == SceneCamera::ProjectionType::Orthographic;
        rc.NearClip = rc.Orthographic ? camera.GetOrthographicNearClip() : camera.GetPerspectiveNearClip();
        rc.FarClip = rc.Orthographic ? camera.GetOrthographicFarClip() : camera.GetPerspectiveFarClip();
        rc.ViewportWidth = std::max(width, 1u);
        rc.ViewportHeight = std::max(height, 1u);
        return rc;
    }

    // ------------------------------------------------------------------ Init / Shutdown

    void Renderer3D::Init()
    {
        s = new State();

        s->Sun.Direction = glm::vec3(-0.5f, -1.0f, -0.3f);
        s->Sun.Intensity = 0.5f;

        s_CubeMesh = Mesh::CreateCube();
        s_PlaneMesh = Mesh::CreatePlane(2.0f, 1);
        s_CylinderMesh = Mesh::CreateCylinder(1.0f, 2.0f, 32);
        s_SphereMesh = Mesh::CreateSphere(1.0f, 64);
        s_ConeMesh = Mesh::CreateCone(0.5f, 1.0f, 64);
        s_ArrowMesh = Mesh::CreateArrow(0.05f, 0.7f, 0.2f, 0.3f, 32);

        s_PBRShader = Shader::Create("Engine/src/Shaders/3D/Forward/PBR.cqsh");
        s_PBRSkinnedShader = Shader::Create("Engine/src/Shaders/3D/Forward/PBRSkinned.cqsh");
        s_UnlitShader = Shader::Create("Engine/src/Shaders/3D/Forward/Unlit.cqsh");
        s_TerrainShader = Shader::Create("Engine/src/Shaders/3D/Forward/Terrain.cqsh");
        s_SkyboxShader = Shader::Create("Engine/src/Shaders/3D/Skybox.cqsh");

        glGenBuffers(1, &s->FrameUBOID);
        glBindBuffer(GL_UNIFORM_BUFFER, s->FrameUBOID);
        glBufferData(GL_UNIFORM_BUFFER, sizeof(FrameUBO), nullptr, GL_DYNAMIC_DRAW);
        glGenBuffers(1, &s->LightsUBOID);
        glBindBuffer(GL_UNIFORM_BUFFER, s->LightsUBOID);
        glBufferData(GL_UNIFORM_BUFFER, sizeof(LightsUBO), nullptr, GL_DYNAMIC_DRAW);
        glBindBuffer(GL_UNIFORM_BUFFER, 0);
        glBindBufferBase(GL_UNIFORM_BUFFER, UniformBinding::Frame, s->FrameUBOID);
        glBindBufferBase(GL_UNIFORM_BUFFER, UniformBinding::Lights, s->LightsUBOID);

        s->FallbackCube = CreateFallbackCube();
        s->FallbackBlack2D = CreateFallback2D();
        glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);

        s_ShadowPass.Init();
        EnvironmentMap::GetBRDFLUT();
    }

    namespace
    {
        void ReleaseVelocityResources();
    }

    void Renderer3D::Shutdown()
    {
        s_ShadowPass.Shutdown();
        if (s)
        {
            if (s->FrameUBOID) glDeleteBuffers(1, &s->FrameUBOID);
            if (s->LightsUBOID) glDeleteBuffers(1, &s->LightsUBOID);
            if (s->FallbackCube) glDeleteTextures(1, &s->FallbackCube);
            if (s->FallbackBlack2D) glDeleteTextures(1, &s->FallbackBlack2D);
            if (s->APVPositionTex) glDeleteTextures(1, &s->APVPositionTex);
            if (s->APVSHTex) glDeleteTextures(1, &s->APVSHTex);
            if (s->DecalDepthTex) glDeleteTextures(1, &s->DecalDepthTex);
            if (s->DecalDepthFBO) glDeleteFramebuffers(1, &s->DecalDepthFBO);
            s->EnvMap.reset();
            delete s;
            s = nullptr;
        }
        EnvironmentMap::Shutdown();
        s_PBRShader.reset();
        s_PBRSkinnedShader.reset();
        ReleaseVelocityResources();   // hareket kayıtları meshleri / modelleri canlı tutar: GL bağlamı kapanmadan bırakılır
        s_UnlitShader.reset();
        s_TerrainShader.reset();
        s_TerrainWhite.reset();
        s_IconShader.reset();
        s_Icons.clear();
        s_SkyboxShader.reset();
        // Statik meshler GL bağlamı kapanmadan bırakılmalı (aksi halde çıkışta çökme)
        s_CubeMesh.reset();
        s_PlaneMesh.reset();
        s_CylinderMesh.reset();
        s_SphereMesh.reset();
        s_ConeMesh.reset();
        s_ArrowMesh.reset();
    }

    // ------------------------------------------------------------------ Hareket maskesi (Temporal AA)
    // TAA hareketi yalnızca kameradan bilir. Bu karede dönüşümü değişen opak meshler ve animasyonlu modeller G-buffer'a
    // piksel hassasiyetinde "hareketli" bayrağı yazar (normal ekinin w'si 2 yerine 4 + metalik); saydam nesneler ve
    // parçacıklar G-buffer'a yazmadığından onların ekran dikdörtgenleri ayrıca verilir. TAA hareketli yerde geçmişi
    // sıkı kırpar (hareket eden nesne iz bırakmaz), hareketsiz yerde geçmişe güvenir (alt piksel çizgiler yanıp
    // sönmez). Görünüm (sahne hedefi) başına izlenir; nesne kimliği = varlığın UUID'si + mesh + varlığın içinde kaçıncı çizimi
    // olduğu (kimliksiz çizimde mesh + bu karedeki kaçıncı çizimi olduğu).
    namespace
    {
        struct MotionTrack
        {
            std::unordered_map<uint64_t, glm::mat4> Prev, Cur;
            std::unordered_map<const void*, uint32_t> Occurrence;
            std::unordered_map<uint64_t, uint32_t> IdentityOccurrence;
            std::vector<glm::vec4> LastRects;   // önceki karenin dikdörtgenleri (nesnenin ayrıldığı yer)
            std::vector<glm::vec4> Rects;       // bu kare
            // Hareket eden opak nesnelerin dünya kutuları (bu ve önceki kare; min, max çifti): TAA bunların gölge hacmindeki
            // gölgeli piksellerde geçmişe güvenmez (gölge eski yerinde iz bırakmaz, yeni yerine gecikmeden geçer)
            std::vector<glm::vec4> LastShadowBoxes;
            std::vector<glm::vec4> ShadowBoxes;
            // Nesne hareket vektörü çizimleri (bu kare): katı meshler önceki dönüşümleriyle, animasyonlu modeller bu karenin
            // kemikleriyle
            struct VelocityDraw
            {
                std::shared_ptr<Mesh> MeshRef;   // LOD meshini canlı tutar
                const Mesh* Draw = nullptr;
                glm::mat4 Cur{ 1.0f }, Prev{ 1.0f };
                int Cull = 1;                    // 0 kapalı, 1 arka, 2 ön
                bool Exact = true;
            };
            struct SkinnedVelocityDraw
            {
                std::shared_ptr<Model> ModelRef;
                glm::mat4 Cur{ 1.0f }, Prev{ 1.0f };
                std::vector<glm::mat4> Bones;
            };
            std::vector<VelocityDraw> Velocity;
            std::vector<SkinnedVelocityDraw> SkinnedVelocity;
            std::unordered_map<uint64_t, glm::mat4> PrevSkinned, CurSkinned;
            std::unordered_map<const void*, uint32_t> SkinnedOccurrence;
            std::unordered_map<uint64_t, uint32_t> SkinnedIdentityOccurrence;
        };
        std::unordered_map<const void*, MotionTrack> s_Motion;
        MotionTrack* s_MotionCur = nullptr;
        glm::mat4 s_MotionViewProj{ 1.0f };
        // GBAA / SRAA geometri geçişi: görünüm başına bu karenin opak mesh çizimleri (yalnız istendiğinde toplanır)
        struct GeometryDraw
        {
            std::shared_ptr<Mesh> MeshRef;
            const Mesh* Draw = nullptr;
            glm::mat4 Transform{ 1.0f };
            int Cull = 1;   // 0 kapalı, 1 arka, 2 ön
        };
        std::unordered_map<const void*, std::vector<GeometryDraw>> s_Geometry;
        std::vector<GeometryDraw>* s_GeometryCur = nullptr;
        std::shared_ptr<Shader> s_GBAAShader, s_SRAAShader;
        bool s_QueuedMoving = false;   // kuyruktan çizilen öğenin hareket bayrağı
        uint64_t s_DrawIdentity = 0;   // sonraki çizimlerin varlık kimliği (0 = yok)

        // Çizimin kareden kareye kararlı anahtarı. Varlık kimliği varsa: varlık + mesh + varlığın içinde kaçıncı çizim. Mesh +
        // karedeki sıra tek başına kararsızdı: aynı meshi kullanan bir nesne belirip kaybolunca (havuzdan etkinleşen mermi izi
        // küpü) sonraki bütün küplerin sırası kayıyor, hepsi başka bir küpün önceki dönüşümüyle "hareket etmiş" sayılıp tüm
        // ekranda TAA geçmişi atılıyordu (ateş edince ve iz sönünce küplerin arası ve çizgiler silikleşip sonra netleşiyordu)
        uint64_t MotionKey(std::unordered_map<const void*, uint32_t>& occurrences, std::unordered_map<uint64_t, uint32_t>& identityOccurrences,
                           const void* mesh)
        {
            const uint64_t m = (uint64_t)(uintptr_t)mesh * 0x9E3779B97F4A7C15ull;
            if (s_DrawIdentity != 0)
            {
                const uint64_t base = m ^ (s_DrawIdentity * 0xD6E8FEB86659FD93ull) ^ 0xA5A5A5A5A5A5A5A5ull;
                const uint32_t occurrence = identityOccurrences[base]++;
                return base ^ ((uint64_t)occurrence * 0xC2B2AE3D27D4EB4Full);
            }
            const uint32_t occurrence = occurrences[mesh]++;
            return m ^ ((uint64_t)occurrence * 0xC2B2AE3D27D4EB4Full);
        }
        bool s_DrawCastsShadows = true;   // çizilen nesne gölge düşürüyor mu (gölge kutusu yalnız düşürenlere)

        // Kutunun yakın düzlemin önündeki kısmının ekran dikdörtgeni (kenarlar yakın düzlemde kırpılır)
        void AddMotionBox(const glm::vec3& c, const glm::vec3& e)
        {
            if (!s_MotionCur)
                return;
            constexpr float kNearW = 0.01f;
            glm::vec4 clip[8];
            for (int i = 0; i < 8; ++i)
            {
                const glm::vec3 p = c + e * glm::vec3((i & 1) ? 1.0f : -1.0f, (i & 2) ? 1.0f : -1.0f, (i & 4) ? 1.0f : -1.0f);
                clip[i] = s_MotionViewProj * glm::vec4(p, 1.0f);
            }
            glm::vec2 lo(1e9f), hi(-1e9f);
            bool any = false;
            auto include = [&](const glm::vec4& q) {
                const glm::vec2 uv = glm::vec2(q) / q.w * 0.5f + 0.5f;
                lo = glm::min(lo, uv);
                hi = glm::max(hi, uv);
                any = true;
            };
            for (int i = 0; i < 8; ++i)
                if (clip[i].w > kNearW)
                    include(clip[i]);
            // 12 kenar: bir ucu önde bir ucu arkada olanın yakın düzlemle kesişimi
            for (int i = 0; i < 8; ++i)
                for (int bit = 1; bit < 8; bit <<= 1)
                {
                    const int j = i | bit;
                    if (j == i)
                        continue;
                    const glm::vec4& a = clip[i];
                    const glm::vec4& b = clip[j];
                    if ((a.w > kNearW) == (b.w > kNearW))
                        continue;
                    const float t = (kNearW - a.w) / (b.w - a.w);
                    include(a + (b - a) * t);
                }
            if (!any)
                return;
            lo = glm::clamp(lo, glm::vec2(0.0f), glm::vec2(1.0f));
            hi = glm::clamp(hi, glm::vec2(0.0f), glm::vec2(1.0f));
            if (hi.x <= lo.x || hi.y <= lo.y)
                return;
            s_MotionCur->Rects.push_back(glm::vec4(lo, hi));
        }

        // Hareket eden kutu gölge düşürür: dünya kutusu, gölge süzgecinin yumuşattığı kenar payıyla saklanır. Gölgenin nereye
        // düştüğü TAA'da piksel piksel bulunur (noktadan güneşe ışın bu kutuya çarpıyor mu); zemin yüksekliği tahmin edilmez.
        void AddShadowBox(const glm::vec3& c, const glm::vec3& e)
        {
            if (!s_MotionCur || !s || !s->HasSun || !s_DrawCastsShadows)
                return;
            const float margin = 0.15f + 0.1f * std::max(e.x, std::max(e.y, e.z));
            s_MotionCur->ShadowBoxes.push_back(glm::vec4(c - e - margin, 0.0f));
            s_MotionCur->ShadowBoxes.push_back(glm::vec4(c + e + margin, 0.0f));
        }

        // Nesne bu karede önceki kareye göre hareket etti mi (yeni beliren nesne de hareketli). prevOut: önceki dönüşüm
        // (yeni beliren nesnede yok)
        bool TrackMeshMotion(const void* mesh, const glm::mat4& transform, const glm::mat4** prevOut = nullptr)
        {
            if (prevOut)
                *prevOut = nullptr;
            if (!s_MotionCur)
                return false;
            const uint64_t key = MotionKey(s_MotionCur->Occurrence, s_MotionCur->IdentityOccurrence, mesh);
            s_MotionCur->Cur[key] = transform;
            auto it = s_MotionCur->Prev.find(key);
            if (it == s_MotionCur->Prev.end())
                return true;
            const glm::mat4& prev = it->second;
            if (prevOut)
                *prevOut = &prev;
            for (int col = 0; col < 3; ++col)
                for (int row = 0; row < 3; ++row)
                    if (std::abs(prev[col][row] - transform[col][row]) > 1e-5f)
                        return true;
            for (int row = 0; row < 3; ++row)
                if (std::abs(prev[3][row] - transform[3][row]) > 1e-4f)
                    return true;
            return false;
        }
    }

    void Renderer3D::MarkMovingBounds(const glm::vec3& center, const glm::vec3& extents)
    {
        // Parçacıklar sahne bittikten (EndScene) sonra çizilir: hareket kaydı kapanmıştır, şu an çizilen görünümün kaydı
        // (BeginScene'deki görüş-izdüşümüyle) kullanılır
        if (!s_MotionCur && PostProcessStack::WantsMotionData())
        {
            auto it = s_Motion.find(PostProcessStack::GetCurrentStack());
            if (it != s_Motion.end())
            {
                s_MotionCur = &it->second;
                AddMotionBox(center, extents);
                s_MotionCur = nullptr;
            }
            return;
        }
        AddMotionBox(center, extents);
    }

    void Renderer3D::SetDrawIdentity(uint64_t identity)
    {
        s_DrawIdentity = identity;
    }

    bool Renderer3D::HasVelocityDraws(const void* view)
    {
        auto it = s_Motion.find(view);
        return it != s_Motion.end() && (!it->second.Velocity.empty() || !it->second.SkinnedVelocity.empty());
    }

    bool Renderer3D::ConsumeLightingChange(const void* view)
    {
        struct LightEnergy { float Sum = 0.0f; int Count = 0; bool Valid = false; };
        static std::unordered_map<const void*, LightEnergy> s_LightEnergy;
        if (!s)
            return false;
        float sum = 0.0f;
        for (int i = 0; i < s->PointCount; ++i)
        {
            const glm::vec4& c = s->Lights.PointLights[i].ColorIntensity;
            sum += c.r + c.g + c.b;
        }
        for (int i = 0; i < s->SpotCount; ++i)
        {
            const glm::vec4& c = s->Lights.SpotLights[i].ColorIntensity;
            sum += c.r + c.g + c.b;
        }
        const int count = s->PointCount + s->SpotCount;
        LightEnergy& last = s_LightEnergy[view];
        const bool changed = last.Valid && (count != last.Count || std::abs(sum - last.Sum) > 0.02f * std::max(sum, last.Sum) + 1e-3f);
        last.Sum = sum;
        last.Count = count;
        last.Valid = true;
        return changed;
    }

    namespace
    {
        std::shared_ptr<Shader> s_VelocityShader, s_VelocitySkinnedShader, s_ReactiveMeshShader;

        void ReleaseVelocityResources()
        {
            s_VelocityShader.reset();
            s_VelocitySkinnedShader.reset();
            s_ReactiveMeshShader.reset();
            s_Motion.clear();
            s_MotionCur = nullptr;
            // GBAA / SRAA geometri listeleri meshleri canlı tutar, gölgelendiriciler GL nesnesidir: bağlam kapanmadan bırakılır
            s_Geometry.clear();
            s_GeometryCur = nullptr;
            s_GBAAShader.reset();
            s_SRAAShader.reset();
        }

        // Hareket eden saydam meshin kapladığı pikseller TAA tepki maskesine yazılır (bu görünümün sahne derinliğine LEQUAL,
        // derinlik yazılmaz, en büyük değer harmanlaması). Maske bu görünümde kullanılamıyorsa false
        bool DrawReactiveMesh(const Mesh& mesh, const glm::mat4& transform)
        {
            if (!mesh.GetVertexArray() || mesh.GetIndexCount() == 0)
                return false;
            if (!s_ReactiveMeshShader)
                s_ReactiveMeshShader = Shader::Create("Engine/src/Shaders/3D/ReactiveMask.cqsh");
            if (!s_ReactiveMeshShader || !s_ReactiveMeshShader->IsValid())
                return false;
            if (!PostProcessStack::BeginReactiveDraw())
                return false;
            const GLboolean blend = glIsEnabled(GL_BLEND), depthTest = glIsEnabled(GL_DEPTH_TEST), cull = glIsEnabled(GL_CULL_FACE);
            GLboolean depthMask = GL_TRUE;
            glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
            GLint depthFunc = GL_LESS;
            glGetIntegerv(GL_DEPTH_FUNC, &depthFunc);
            glEnable(GL_BLEND);
            glBlendEquation(GL_MAX);
            glBlendFunc(GL_ONE, GL_ONE);
            glEnable(GL_DEPTH_TEST);
            glDepthFunc(GL_LEQUAL);
            glDepthMask(GL_FALSE);
            glDisable(GL_CULL_FACE);
            s_ReactiveMeshShader->Bind();
            s_ReactiveMeshShader->SetMat4("u_ViewProjection", s_MotionViewProj);
            s_ReactiveMeshShader->SetMat4("u_Transform", transform);
            RenderCommand::DrawIndexed(mesh.GetVertexArray(), mesh.GetIndexCount());
            glBlendEquation(GL_FUNC_ADD);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            if (!blend)
                glDisable(GL_BLEND);
            if (!depthTest)
                glDisable(GL_DEPTH_TEST);
            if (cull)
                glEnable(GL_CULL_FACE);
            glDepthFunc((GLenum)depthFunc);
            glDepthMask(depthMask);
            PostProcessStack::EndReactiveDraw();
            return true;
        }
    }

    bool Renderer3D::RenderGeometry(const void* view, GeometryPass pass, const glm::mat4& viewProj, const glm::vec2& viewportSize)
    {
        auto it = s_Geometry.find(view);
        if (it == s_Geometry.end() || it->second.empty())
            return false;
        std::shared_ptr<Shader>& sh = pass == GeometryPass::GBAAEdges ? s_GBAAShader : s_SRAAShader;
        if (!sh)
            sh = Shader::Create(pass == GeometryPass::GBAAEdges ? "Engine/src/Shaders/3D/GBAAEdges.cqsh" : "Engine/src/Shaders/3D/SRAAGeometry.cqsh");
        if (!sh || !sh->IsValid())
            return false;
        GLboolean depthTest = glIsEnabled(GL_DEPTH_TEST), cullFace = glIsEnabled(GL_CULL_FACE), blend = glIsEnabled(GL_BLEND);
        GLboolean offsetFill = glIsEnabled(GL_POLYGON_OFFSET_FILL);
        GLint depthFunc = GL_LESS, cullMode = GL_BACK;
        GLboolean depthMask = GL_TRUE;
        glGetIntegerv(GL_DEPTH_FUNC, &depthFunc);
        glGetIntegerv(GL_CULL_FACE_MODE, &cullMode);
        glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
        glEnable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);
        if (pass == GeometryPass::GBAAEdges)
        {
            // Sahne derinliğine LEQUAL (biraz öne çekilmiş): yalnız görünen yüzeyin kenar bilgisi yazılır
            glDepthFunc(GL_LEQUAL);
            glDepthMask(GL_FALSE);
            glEnable(GL_POLYGON_OFFSET_FILL);
            glPolygonOffset(-1.0f, -1.0f);
        }
        else
        {
            glDepthFunc(GL_LESS);
            glDepthMask(GL_TRUE);
            glDisable(GL_POLYGON_OFFSET_FILL);
        }
        sh->Bind();
        sh->SetMat4("u_ViewProj", viewProj);
        sh->SetFloat2("u_ViewportSize", viewportSize);
        bool drew = false;
        for (const auto& d : it->second)
        {
            if (!d.Draw || !d.Draw->GetVertexArray())
                continue;
            if (d.Cull == 0)
                glDisable(GL_CULL_FACE);
            else
            {
                glEnable(GL_CULL_FACE);
                glCullFace(d.Cull == 2 ? GL_FRONT : GL_BACK);
            }
            sh->SetMat4("u_Transform", d.Transform);
            RenderCommand::DrawIndexed(d.Draw->GetVertexArray(), d.Draw->GetIndexCount());
            drew = true;
        }
        if (depthTest) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
        if (cullFace) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
        if (blend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
        if (offsetFill) glEnable(GL_POLYGON_OFFSET_FILL); else glDisable(GL_POLYGON_OFFSET_FILL);
        glDepthFunc((GLenum)depthFunc);
        glCullFace((GLenum)cullMode);
        glDepthMask(depthMask);
        return drew;
    }

    bool Renderer3D::RenderVelocity(const void* view, const glm::mat4& jitteredViewProj, const glm::mat4& viewProj, const glm::mat4& prevViewProj)
    {
        auto it = s_Motion.find(view);
        if (it == s_Motion.end() || (it->second.Velocity.empty() && it->second.SkinnedVelocity.empty()))
            return false;
        if (!s_VelocityShader)
            s_VelocityShader = Shader::Create("Engine/src/Shaders/3D/Velocity.cqsh");
        if (!s_VelocitySkinnedShader)
            s_VelocitySkinnedShader = Shader::Create("Engine/src/Shaders/3D/VelocitySkinned.cqsh");
        // Durum: sahne derinliğine LEQUAL (biraz öne çekilmiş), derinlik / harmanlama yazılmaz; çağıran çerçeve tamponunu bağlar
        GLboolean depthTest = glIsEnabled(GL_DEPTH_TEST), cullFace = glIsEnabled(GL_CULL_FACE), blend = glIsEnabled(GL_BLEND);
        GLboolean offsetFill = glIsEnabled(GL_POLYGON_OFFSET_FILL);
        GLint depthFunc = GL_LESS, cullMode = GL_BACK;
        GLboolean depthMask = GL_TRUE;
        glGetIntegerv(GL_DEPTH_FUNC, &depthFunc);
        glGetIntegerv(GL_CULL_FACE_MODE, &cullMode);
        glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);
        glDepthMask(GL_FALSE);
        glDisable(GL_BLEND);
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(-1.0f, -1.0f);
        bool drew = false;
        auto setCull = [](int cull) {
            if (cull == 0)
                glDisable(GL_CULL_FACE);
            else
            {
                glEnable(GL_CULL_FACE);
                glCullFace(cull == 2 ? GL_FRONT : GL_BACK);
            }
        };
        if (s_VelocityShader && s_VelocityShader->IsValid())
        {
            auto& sh = s_VelocityShader;
            sh->Bind();
            sh->SetMat4("u_ViewProjJittered", jitteredViewProj);
            sh->SetMat4("u_ViewProj", viewProj);
            sh->SetMat4("u_PrevViewProj", prevViewProj);
            for (const auto& d : it->second.Velocity)
            {
                if (!d.Draw || !d.Draw->GetVertexArray())
                    continue;
                setCull(d.Cull);
                sh->SetMat4("u_Transform", d.Cur);
                sh->SetMat4("u_PrevTransform", d.Prev);
                sh->SetFloat("u_Exact", d.Exact ? 1.0f : 0.5f);
                RenderCommand::DrawIndexed(d.Draw->GetVertexArray(), d.Draw->GetIndexCount());
                drew = true;
            }
        }
        if (s_VelocitySkinnedShader && s_VelocitySkinnedShader->IsValid())
        {
            auto& sh = s_VelocitySkinnedShader;
            sh->Bind();
            sh->SetMat4("u_ViewProjJittered", jitteredViewProj);
            sh->SetMat4("u_ViewProj", viewProj);
            sh->SetMat4("u_PrevViewProj", prevViewProj);
            sh->SetFloat("u_Exact", 0.5f);
            for (const auto& d : it->second.SkinnedVelocity)
            {
                if (!d.ModelRef || d.Bones.empty())
                    continue;
                sh->SetMat4("u_Transform", d.Cur);
                sh->SetMat4("u_PrevTransform", d.Prev);
                sh->SetMat4Array("u_BoneTransforms", d.Bones.data(), (uint32_t)d.Bones.size());
                for (size_t i = 0; i < d.ModelRef->SkinnedMeshes.size(); ++i)
                {
                    const auto& mesh = d.ModelRef->SkinnedMeshes[i];
                    if (!mesh || !mesh->GetVertexArray())
                        continue;
                    const auto& material = i < d.ModelRef->Materials.size() ? d.ModelRef->Materials[i] : nullptr;
                    const Material::CullMode cull = material ? material->Culling : Material::CullMode::Back;
                    setCull(cull == Material::CullMode::Off ? 0 : (cull == Material::CullMode::Front ? 2 : 1));
                    RenderCommand::DrawIndexed(mesh->GetVertexArray(), mesh->GetIndexCount());
                    drew = true;
                }
            }
        }
        // Açılan alan: nesnelerin önceki konumları (bu karenin kamerasıyla) yalnız w kanalına 2 olarak işaretlenir, derinlik
        // testi yok. Hareketsiz duran nesne kıpırdamaya başladığı ilk karede eski yeri "önceki karede hareket etti" sayılmıyor,
        // geçmişte orada kalan görüntüsü (yerde yatan hedef levhası) açılan zeminde bir süre iz bırakıyordu
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glColorMaski(0, GL_FALSE, GL_FALSE, GL_FALSE, GL_TRUE);
        if (s_VelocityShader && s_VelocityShader->IsValid())
        {
            auto& sh = s_VelocityShader;
            sh->Bind();
            sh->SetInt("u_PrevCoverage", 1);
            for (const auto& d : it->second.Velocity)
            {
                if (!d.Draw || !d.Draw->GetVertexArray())
                    continue;
                sh->SetMat4("u_Transform", d.Prev);
                sh->SetMat4("u_PrevTransform", d.Prev);
                RenderCommand::DrawIndexed(d.Draw->GetVertexArray(), d.Draw->GetIndexCount());
            }
            sh->SetInt("u_PrevCoverage", 0);
        }
        if (s_VelocitySkinnedShader && s_VelocitySkinnedShader->IsValid())
        {
            auto& sh = s_VelocitySkinnedShader;
            sh->Bind();
            sh->SetInt("u_PrevCoverage", 1);
            for (const auto& d : it->second.SkinnedVelocity)
            {
                if (!d.ModelRef || d.Bones.empty())
                    continue;
                sh->SetMat4("u_Transform", d.Prev);
                sh->SetMat4("u_PrevTransform", d.Prev);
                sh->SetMat4Array("u_BoneTransforms", d.Bones.data(), (uint32_t)d.Bones.size());
                for (const auto& mesh : d.ModelRef->SkinnedMeshes)
                    if (mesh && mesh->GetVertexArray())
                        RenderCommand::DrawIndexed(mesh->GetVertexArray(), mesh->GetIndexCount());
            }
            sh->SetInt("u_PrevCoverage", 0);
        }
        glColorMaski(0, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glEnable(GL_DEPTH_TEST);
        glPolygonOffset(0.0f, 0.0f);
        if (!offsetFill) glDisable(GL_POLYGON_OFFSET_FILL);
        glDepthMask(depthMask);
        glDepthFunc((GLenum)depthFunc);
        if (!depthTest) glDisable(GL_DEPTH_TEST);
        if (cullFace) { glEnable(GL_CULL_FACE); glCullFace((GLenum)cullMode); } else glDisable(GL_CULL_FACE);
        if (blend) glEnable(GL_BLEND);
        return drew;
    }

    void Renderer3D::GetMovingRects(const void* view, std::vector<glm::vec4>& out)
    {
        out.clear();
        auto it = s_Motion.find(view);
        if (it == s_Motion.end())
            return;
        out = it->second.Rects;
        out.insert(out.end(), it->second.LastRects.begin(), it->second.LastRects.end());
    }

    void Renderer3D::SetDrawCastsShadows(bool castsShadows)
    {
        s_DrawCastsShadows = castsShadows;
    }

    bool Renderer3D::GetMovingShadowBoxes(const void* view, std::vector<glm::vec4>& minMax, glm::vec3& towardSun)
    {
        minMax.clear();
        if (!s || !s->HasSun)
            return false;
        const glm::vec3 L = glm::normalize(s->Sun.Direction);
        if (L.y > -0.05f)
            return false;   // güneş ufukta / altında
        towardSun = -L;
        auto it = s_Motion.find(view);
        if (it == s_Motion.end())
            return true;
        minMax = it->second.ShadowBoxes;
        minMax.insert(minMax.end(), it->second.LastShadowBoxes.begin(), it->second.LastShadowBoxes.end());
        return true;
    }

    // ------------------------------------------------------------------ Kare

    float Renderer3D::GetFrameTime()
    {
        return s ? s->Env.Time : 0.0f;
    }

    void Renderer3D::BeginFrame(const RenderCamera& camera, const SceneEnvironment& environment)
    {
        s->Camera = camera;
        s->Env = environment;
        // Çözünürlük bildirilmemişse (oyun sahnesi kopyası) geçerli çizim alanından: ekran uzayı hata / LOD hesapları için
        if (s->Camera.ViewportHeight <= 1 || s->Camera.ViewportWidth <= 1)
        {
            GLint viewport[4] = { 0, 0, 1, 1 };
            glGetIntegerv(GL_VIEWPORT, viewport);
            s->Camera.ViewportWidth = (uint32_t)std::max(viewport[2], 1);
            s->Camera.ViewportHeight = (uint32_t)std::max(viewport[3], 1);
        }
        // GBAA / SRAA: bu görünümün opak çizimleri bu karede toplanır (son geçişte özel gölgelendiriciyle yeniden çizilir)
        s_GeometryCur = nullptr;
        if (PostProcessStack::WantsGeometry())
        {
            s_GeometryCur = &s_Geometry[PostProcessStack::GetCurrentStack()];
            s_GeometryCur->clear();
        }
        // Temporal AA: bu görünümün sahne hedefine çiziliyorsa alt piksel titreşimi (NDC kayması)
        glm::vec2 jitter;
        s_MotionCur = nullptr;
        if (PostProcessStack::GetCurrentJitter(jitter))
        {
            for (int c = 0; c < 4; ++c)
            {
                s->Camera.Projection[c][0] += jitter.x * s->Camera.Projection[c][3];
                s->Camera.Projection[c][1] += jitter.y * s->Camera.Projection[c][3];
            }
        }
        // Hareket verisi (zamansal yöntemler ve tek kare yöntemlerindeki zamansal kararlılık katmanı): nesne hareket vektörleri,
        // hareket bayrağı, hareket eden nesnenin önceki yeri
        // Yükseltici iç çözünürlükte çiziyorsa malzeme dokularının mip sapması (gölge geçişi başka hedef bağlamadan önce, şimdi)
        s->TextureLodBias = PostProcessStack::GetCurrentTextureLodBias();
        s->FrameDirty = true;
        if (PostProcessStack::WantsMotionData())
        {
            // Hareket maskesi bu görünüm için: önceki karenin kutuları / dikdörtgenleri saklanır
            MotionTrack& track = s_Motion[PostProcessStack::GetCurrentStack()];
            std::swap(track.Prev, track.Cur);
            track.Cur.clear();
            track.Occurrence.clear();
            track.IdentityOccurrence.clear();
            track.LastRects.swap(track.Rects);
            track.Rects.clear();
            track.LastShadowBoxes.swap(track.ShadowBoxes);
            track.ShadowBoxes.clear();
            track.Velocity.clear();
            track.SkinnedVelocity.clear();
            std::swap(track.PrevSkinned, track.CurSkinned);
            track.CurSkinned.clear();
            track.SkinnedOccurrence.clear();
            track.SkinnedIdentityOccurrence.clear();
            s_MotionCur = &track;
            s_MotionViewProj = s->Camera.Projection * s->Camera.View;
        }
        // Proje kalite düzeyi: gölge atlası / kademe / mesafe üst sınırı (Unity Quality Settings)
        {
            const QualityPreset& q = QualitySettings::GetPreset();
            auto& sh = s->Env.Settings.Shadows;
            sh.Resolution = std::min(sh.Resolution, std::max(q.ShadowResolution * 2, 256));
            sh.CascadeCount = std::clamp(std::min(sh.CascadeCount, q.ShadowCascades), 1, 4);
            sh.Distance = std::min(sh.Distance, std::max(q.ShadowDistance, 1.0f));
        }
        if (!s->Env.Lightmap)
            s->Env.Lightmap = s->GlobalLightmap;
        if (!s->Env.APV)
            s->Env.APV = s->GlobalAPV;

        s->FrameActive = true;
        s->FrameDirty = true;
        s->FrustumDirty = true;
        ++s->FrameIndex;
        s->Transparent.clear();
        s->Opaque.clear();
        s->GizmoViewProjection = camera.Projection * camera.View;
        ClearLights();
        ClearReflectionProbes();
        s->HasSun = false;
        s->EnvCheckedFrame = 0; // ortam haritası ilk çizimde (güneş bilindikten sonra) güncellenir
    }

    void Renderer3D::BeginScene(EditorCamera& camera)
    {
        s->GizmoViewProjection = camera.GetViewProjection();
        if (!s->FrameActive)
        {
            s->Camera = RenderCamera::FromEditorCamera(camera, 1, 1);
            s->FrameDirty = true;
            s->FrustumDirty = true;
        }
    }

    void Renderer3D::BeginScene(const SceneCamera& camera, const glm::mat4& transform)
    {
        s->GizmoViewProjection = camera.GetProjection() * glm::inverse(transform);
        if (!s->FrameActive)
        {
            s->Camera = RenderCamera::FromSceneCamera(camera, transform, 1, 1);
            s->FrameDirty = true;
            s->FrustumDirty = true;
        }
    }

    void Renderer3D::UpdateEnvironmentMap()
    {
        auto& env = s->Env;
        bool wantSkyIBL = env.Settings.Environment.ImageBasedLighting && env.AmbientSource == 0;
        if (!wantSkyIBL && !env.Skybox)
            return; // eski haritayı koru (yansımalar için), maliyet yok

        if (env.Skybox && env.Skybox->GetRendererID() != 0)
        {
            auto cached = s->CachedSkybox.lock();
            if (!s->EnvMap || s->EnvIsProcedural || cached != env.Skybox || s->CachedSkyboxID != env.Skybox->GetRendererID())
            {
                s->EnvMap = EnvironmentMap::FromCubemap(env.Skybox->GetRendererID(), std::max(env.Skybox->GetWidth(), 64u));
                s->CachedSkybox = env.Skybox;
                s->CachedSkyboxID = env.Skybox->GetRendererID();
                s->EnvIsProcedural = false;
                s->FrameDirty = true;
            }
            return;
        }

        if (!env.UseProceduralSky)
            return;

        // Prosedürel gökyüzü güneşi izler (en fazla saniyede 8 kez yeniden üretilir)
        ProceduralSkySettings sky;
        sky.SunDirection = s->HasSun ? s->Sun.Direction : glm::vec3(-0.4f, -0.8f, -0.3f);
        if (glm::dot(sky.SunDirection, sky.SunDirection) < 1e-6f)
            sky.SunDirection = glm::vec3(-0.4f, -0.8f, -0.3f);
        sky.SunColor = s->HasSun ? s->Sun.Color : glm::vec3(1.0f, 0.96f, 0.9f);
        sky.SunIntensity = 1.0f;
        const auto& es = env.Settings.Environment;
        if (es.Sky == SkyMode::Atmosphere)
        {
            sky.Physical = true;
            sky.SunIntensity = std::max(es.AtmosphereIntensity, 0.0f);
            sky.Rayleigh = std::max(es.AtmosphereRayleigh, 0.0f);
            sky.Mie = std::max(es.AtmosphereMie, 0.0f);
            sky.MieAnisotropy = es.AtmosphereMieAnisotropy;
            sky.Ozone = std::max(es.AtmosphereOzone, 0.0f);
            sky.SunDisk = es.AtmosphereSunDisk;
            sky.GroundAlbedo = es.AtmosphereGroundAlbedo;
        }

        auto now = std::chrono::steady_clock::now();
        bool changed = !s->EnvMap || !s->EnvIsProcedural || !(sky == s->LastSky);
        bool throttled = std::chrono::duration<float>(now - s->LastProceduralUpdate).count() < 0.125f;
        if (changed && (!throttled || !s->EnvMap || !s->EnvIsProcedural))
        {
            auto map = EnvironmentMap::FromProceduralSky(sky, sky.Physical ? 256u : 128u);
            if (map)
            {
                s->EnvMap = map;
                s->EnvIsProcedural = true;
                s->LastSky = sky;
                s->LastProceduralUpdate = now;
                s->CachedSkybox.reset();
                s->FrameDirty = true;
            }
        }
    }

    void Renderer3D::SetDirectionalLight(const DirectionalLightComponent& light)
    {
        s->Sun = light;
        s->HasSun = true;
        s->FrameDirty = true;
    }

    void Renderer3D::ClearDirectionalLight()
    {
        s->HasSun = false;
        s->FrameDirty = true;
    }

    void Renderer3D::AddPointLight(const glm::vec3& position, const PointLightComponent& light)
    {
        if (s->PointCount >= kMaxPointLights || LightIsBakedAway(light.Mode))
            return;
        auto& gpu = s->Lights.PointLights[s->PointCount++];
        gpu.PositionRange = glm::vec4(position, std::max(light.Range, 0.01f));
        gpu.ColorIntensity = glm::vec4(light.Color * light.Intensity, 0.0f);
        gpu.Attenuation = glm::vec4(light.Constant, light.Linear, light.Quadratic, 0.0f);
        s->FrameDirty = true;
    }

    void Renderer3D::AddSpotLight(const glm::vec3& position, const SpotLightComponent& light)
    {
        AddSpotLight(position, light.Direction, light);
    }

    void Renderer3D::AddSpotLight(const glm::vec3& position, const glm::vec3& direction, const SpotLightComponent& light)
    {
        if (s->SpotCount >= kMaxSpotLights || LightIsBakedAway(light.Mode))
            return;
        glm::vec3 dir = glm::dot(direction, direction) > 1e-8f ? glm::normalize(direction) : glm::vec3(0.0f, -1.0f, 0.0f);
        float inner = std::min(light.InnerConeAngle, light.OuterConeAngle);
        float outer = std::max(light.InnerConeAngle, light.OuterConeAngle);
        auto& gpu = s->Lights.SpotLights[s->SpotCount++];
        gpu.PositionRange = glm::vec4(position, std::max(light.Range, 0.01f));
        gpu.ColorIntensity = glm::vec4(light.Color * light.Intensity, 0.0f);
        gpu.DirectionCosOuter = glm::vec4(dir, std::cos(glm::radians(outer)));
        gpu.AttenuationCosInner = glm::vec4(light.Constant, light.Linear, light.Quadratic, std::cos(glm::radians(inner)));
        s->FrameDirty = true;
    }

    void Renderer3D::AddAreaLight(const glm::vec3& center, const glm::vec3& halfRight, const glm::vec3& halfUp, const glm::vec3& radiance,
                                  float range, bool twoSided, bool disc)
    {
        if (s->AreaCount >= kMaxAreaLights)
            return;
        auto& gpu = s->Lights.AreaLights[s->AreaCount++];
        gpu.PositionRange = glm::vec4(center, std::max(range, 0.01f));
        gpu.ColorIntensity = glm::vec4(radiance, twoSided ? 1.0f : 0.0f);
        gpu.Right = glm::vec4(halfRight, disc ? 1.0f : 0.0f);
        gpu.Up = glm::vec4(halfUp, 0.0f);
        s->FrameDirty = true;
    }

    void Renderer3D::ClearLights()
    {
        s->PointCount = 0;
        s->SpotCount = 0;
        s->AreaCount = 0;
        s->FrameDirty = true;
    }

    void Renderer3D::AddReflectionProbe(const glm::vec3& position, const glm::vec3& boxMin,
                                        const glm::vec3& boxMax, std::shared_ptr<Cubemap> cubemap,
                                        float intensity, float blendDistance)
    {
        if (!cubemap || s->Probes.size() >= TextureSlot::MaxReflectionProbes)
            return;
        uint32_t mips = 1;
        for (uint32_t sz = std::max(cubemap->GetWidth(), 1u); sz > 1; sz >>= 1) ++mips;
        ProbeEntry p{ position, glm::min(boxMin, boxMax), glm::max(boxMin, boxMax), cubemap->GetRendererID(), mips, intensity, blendDistance, cubemap };
        s->Probes.push_back(p);
        s->FrameDirty = true;
    }

    void Renderer3D::AddReflectionProbe(const glm::vec3& position, uint32_t cubemapID, uint32_t mipCount,
                                        const glm::vec3& boxMin, const glm::vec3& boxMax,
                                        float intensity, float blendDistance)
    {
        if (!cubemapID || s->Probes.size() >= TextureSlot::MaxReflectionProbes)
            return;
        ProbeEntry p{ position, glm::min(boxMin, boxMax), glm::max(boxMin, boxMax), cubemapID, std::max(mipCount, 1u), intensity, blendDistance, nullptr };
        s->Probes.push_back(p);
        s->FrameDirty = true;
    }

    void Renderer3D::ClearReflectionProbes()
    {
        s->Probes.clear();
        s->FrameDirty = true;
    }

    void Renderer3D::SetLightmap(std::shared_ptr<Texture2D> lightmap)
    {
        s->GlobalLightmap = lightmap;
        s->FrameDirty = true;
    }

    void Renderer3D::SetAdaptiveProbeVolume(std::shared_ptr<AdaptiveProbeVolume> apv)
    {
        s->GlobalAPV = apv;
        s->FrameDirty = true;
    }

    std::shared_ptr<Texture2D> Renderer3D::GetGlobalLightmap() { return s->GlobalLightmap; }
    std::shared_ptr<AdaptiveProbeVolume> Renderer3D::GetGlobalAPV() { return s->GlobalAPV; }
    const SceneEnvironment& Renderer3D::GetEnvironment() { return s->Env; }
    const RenderCamera& Renderer3D::GetCamera() { return s->Camera; }
    std::shared_ptr<EnvironmentMap> Renderer3D::GetActiveEnvironmentMap() { return s->EnvMap; }

    void Renderer3D::RenderShadows(Scene* scene)
    {
        const auto& settings = s->Env.Settings.Shadows;
        bool sunCasts = s->HasSun && s->Sun.CastShadows && !LightIsBakedAway(s->Sun.Mode);
        if (!scene || !settings.Enabled || !sunCasts)
        {
            s_ShadowPass.Execute(nullptr, glm::vec3(0.0f), glm::mat4(1.0f), glm::mat4(1.0f), glm::vec3(0.0f), settings);
            s->FrameDirty = true;
            return;
        }
        s_ShadowPass.Execute(scene, s->Sun.Direction, s->Camera.View, s->Camera.Projection, s->Camera.Position, settings);
        Renderer::GetStats().ShadowCascades = (uint32_t)s_ShadowPass.GetActiveCascadeCount();
        s->FrameDirty = true;
    }

    // ------------------------------------------------------------------ UBO yükleme

    void Renderer3D::UploadFrameData()
    {
        if (s->FrameActive && s->EnvCheckedFrame == 0)
        {
            s->EnvCheckedFrame = 1;
            UpdateEnvironmentMap();
        }
        if (!s->FrameDirty)
            return;
        s->FrameDirty = false;

        const auto& cam = s->Camera;
        const auto& env = s->Env;
        const auto& rs = env.Settings;
        FrameUBO& f = s->Frame;

        f.View = cam.View;
        f.Projection = cam.Projection;
        f.ViewProjection = cam.Projection * cam.View;
        f.InvView = glm::inverse(cam.View);
        f.InvProjection = glm::inverse(cam.Projection);
        f.CameraPosTime = glm::vec4(cam.Position, env.Time);
        f.ScreenSize = glm::vec4((float)cam.ViewportWidth, (float)cam.ViewportHeight, 1.0f / (float)cam.ViewportWidth, 1.0f / (float)cam.ViewportHeight);

        // Ortam
        const bool hasIBL = s->EnvMap && rs.Environment.ImageBasedLighting;
        glm::vec3 skyRGB = env.AmbientSource == 2 ? env.SkyColor : env.AmbientColor;
        float ambientScale = env.AmbientIntensity * (env.AmbientSource == 0 ? rs.Environment.SkyboxIntensity : 1.0f);
        f.AmbientSkyColor = glm::vec4(skyRGB, ambientScale);
        f.AmbientEquatorColor = glm::vec4(env.EquatorColor, 0.0f);
        f.AmbientGroundColor = glm::vec4(env.GroundColor, 0.0f);
        f.AmbientParams = glm::ivec4(env.AmbientSource, hasIBL ? 1 : 0, s->PointCount, s->SpotCount);
        if (hasIBL)
        {
            const auto& sh = s->EnvMap->GetIrradianceSH();
            for (int i = 0; i < 9; ++i)
                f.IrradianceSH[i] = glm::vec4(sh[i], 0.0f);
        }
        else
        {
            for (int i = 0; i < 9; ++i)
                f.IrradianceSH[i] = glm::vec4(0.0f);
        }
        float rot = glm::radians(env.Skybox ? env.SkyboxRotation : 0.0f);
        float skyExposure = env.Skybox ? env.SkyboxExposure : 1.0f;
        f.IBLParams = glm::vec4(rs.Environment.ReflectionIntensity * skyExposure, hasIBL ? s->EnvMap->GetPrefilteredMaxMip() : 0.0f, std::cos(rot), std::sin(rot));
        if (hasIBL && env.Skybox)
            f.AmbientSkyColor.a *= skyExposure;

        // Sis
        f.FogColor = glm::vec4(rs.Fog.Color, rs.Fog.Enabled ? 1.0f : 0.0f);
        f.FogParams = glm::vec4((float)rs.Fog.Mode, rs.Fog.Density, rs.Fog.Start, rs.Fog.End);
        f.FogParams2 = glm::vec4(rs.Fog.HeightFalloff, rs.Fog.BaseHeight, 0.0f, 0.0f);

        // Güneş
        bool sunActive = s->HasSun && !LightIsBakedAway(s->Sun.Mode) && s->Sun.Intensity > 0.0f;
        glm::vec3 sunDir = glm::dot(s->Sun.Direction, s->Sun.Direction) > 1e-8f ? glm::normalize(s->Sun.Direction) : glm::vec3(0.0f, -1.0f, 0.0f);
        f.SunDirection = glm::vec4(sunDir, sunActive ? 1.0f : 0.0f);
        f.SunColor = glm::vec4(s->Sun.Color * s->Sun.Intensity, 0.0f);

        // Gölgeler
        int cascades = s_ShadowPass.GetActiveCascadeCount();
        bool shadowsOn = cascades > 0 && rs.Shadows.Enabled && s->Sun.CastShadows;
        f.ShadowParams = glm::vec4(shadowsOn ? 1.0f : 0.0f, rs.Shadows.Strength, (float)rs.Shadows.Quality, rs.Shadows.Softness);
        float fadeBand = std::max(rs.Shadows.Distance * 0.06f, 0.5f);
        f.ShadowParams2 = glm::vec4(rs.Shadows.DepthBias, rs.Shadows.NormalBias, (float)cascades, fadeBand);
        f.ShadowColor = glm::vec4(rs.Shadows.Color, 0.0f);
        const auto& lsm = s_ShadowPass.GetLightSpaceMatrices();
        const auto& splits = s_ShadowPass.GetCascadeSplits();
        const auto& texel = s_ShadowPass.GetCascadeTexelWorld();
        const auto& rects = s_ShadowPass.GetCascadeRects();
        for (int i = 0; i < kMaxCascades; ++i)
        {
            f.LightSpace[i] = i < (int)lsm.size() ? lsm[i] : glm::mat4(1.0f);
            f.CascadeRects[i] = i < (int)rects.size() ? rects[i] : glm::vec4(0, 0, 1, 1);
            f.CascadeSplits[i] = i < (int)splits.size() ? splits[i] : 0.0f;
            f.CascadeTexelWorld[i] = i < (int)texel.size() ? texel[i] : 0.05f;
        }

        bool apvOn = env.APV && env.APV->IsBaked();
        f.FeatureFlags = glm::ivec4(env.HDROutput ? 1 : 0, env.Lightmap ? 1 : 0, apvOn ? 1 : 0, (int)s->Probes.size());
        f.DebugParams = glm::ivec4(s->DebugView, s->AreaCount, 0, 0);
        // Yükseltici iç çözünürlükte çiziyorsa dokular çıktı çözünürlüğüne uygun mipten örneklenir (yoksa her şey ölçekten de bulanık)
        f.RenderParams = glm::vec4(s->TextureLodBias, 0.0f, 0.0f, 0.0f);

        glBindBuffer(GL_UNIFORM_BUFFER, s->FrameUBOID);
        glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(FrameUBO), &f);
        glBindBuffer(GL_UNIFORM_BUFFER, s->LightsUBOID);
        // Sadece kullanılan ışıkları yükle
        glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(GPUPointLight) * s->PointCount, s->Lights.PointLights);
        glBufferSubData(GL_UNIFORM_BUFFER, offsetof(LightsUBO, SpotLights), sizeof(GPUSpotLight) * s->SpotCount, s->Lights.SpotLights);
        if (s->AreaCount > 0)
            glBufferSubData(GL_UNIFORM_BUFFER, offsetof(LightsUBO, AreaLights), sizeof(GPUAreaLight) * s->AreaCount, s->Lights.AreaLights);
        glBindBuffer(GL_UNIFORM_BUFFER, 0);
        glBindBufferBase(GL_UNIFORM_BUFFER, UniformBinding::Frame, s->FrameUBOID);
        glBindBufferBase(GL_UNIFORM_BUFFER, UniformBinding::Lights, s->LightsUBOID);

        // Global doku birimleri
        glActiveTexture(GL_TEXTURE0 + TextureSlot::ShadowAtlas);
        glBindTexture(GL_TEXTURE_2D, s_ShadowPass.GetAtlasTextureID());
        glActiveTexture(GL_TEXTURE0 + TextureSlot::IBLPrefiltered);
        glBindTexture(GL_TEXTURE_CUBE_MAP, hasIBL ? s->EnvMap->GetPrefilteredID() : s->FallbackCube);
        glActiveTexture(GL_TEXTURE0 + TextureSlot::BRDFLut);
        glBindTexture(GL_TEXTURE_2D, EnvironmentMap::GetBRDFLUT());
        glActiveTexture(GL_TEXTURE0 + TextureSlot::Lightmap);
        glBindTexture(GL_TEXTURE_2D, env.Lightmap ? env.Lightmap->GetRendererID() : s->FallbackBlack2D);
        for (uint32_t i = 0; i < TextureSlot::MaxReflectionProbes; ++i)
        {
            glActiveTexture(GL_TEXTURE0 + TextureSlot::ReflectionProbeFirst + i);
            glBindTexture(GL_TEXTURE_CUBE_MAP, i < s->Probes.size() ? s->Probes[i].CubemapID : s->FallbackCube);
        }

        // APV
        s->APVProbeCount = 0;
        if (apvOn)
        {
            auto& apv = env.APV;
            int numProbes = std::min(apv->GetTotalProbeCount(), APV_MAX_PROBES);
            if (numProbes > 0)
            {
                if (s->APVLastProbeCount != numProbes || !s->APVPositionTex)
                {
                    if (s->APVPositionTex) glDeleteTextures(1, &s->APVPositionTex);
                    if (s->APVSHTex) glDeleteTextures(1, &s->APVSHTex);
                    glGenTextures(1, &s->APVPositionTex);
                    glGenTextures(1, &s->APVSHTex);
                    glBindTexture(GL_TEXTURE_2D, s->APVPositionTex);
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, numProbes, 1, 0, GL_RGBA, GL_FLOAT, nullptr);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                    glBindTexture(GL_TEXTURE_2D, s->APVSHTex);
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, APV_SH_COEFFS, numProbes, 0, GL_RGBA, GL_FLOAT, nullptr);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                    s->APVLastProbeCount = numProbes;
                    s->APVUploaded = nullptr;
                }
                if (s->APVUploaded != apv.get())
                {
                    std::vector<float> pos((size_t)numProbes * 4);
                    std::vector<float> sh((size_t)numProbes * APV_SH_COEFFS * 4, 0.0f);
                    for (int i = 0; i < numProbes; ++i)
                    {
                        const auto& probe = apv->GetProbes()[i];
                        pos[i * 4 + 0] = probe.Position.x;
                        pos[i * 4 + 1] = probe.Position.y;
                        pos[i * 4 + 2] = probe.Position.z;
                        pos[i * 4 + 3] = probe.Valid ? 1.0f : 0.0f;
                        for (int c = 0; c < APV_SH_COEFFS; ++c)
                        {
                            size_t idx = ((size_t)i * APV_SH_COEFFS + c) * 4;
                            sh[idx + 0] = probe.SH.Coeffs[c].x;
                            sh[idx + 1] = probe.SH.Coeffs[c].y;
                            sh[idx + 2] = probe.SH.Coeffs[c].z;
                        }
                    }
                    glBindTexture(GL_TEXTURE_2D, s->APVPositionTex);
                    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, numProbes, 1, GL_RGBA, GL_FLOAT, pos.data());
                    glBindTexture(GL_TEXTURE_2D, s->APVSHTex);
                    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, APV_SH_COEFFS, numProbes, GL_RGBA, GL_FLOAT, sh.data());
                    s->APVUploaded = apv.get();
                }
                s->APVProbeCount = numProbes;
            }
        }
        glActiveTexture(GL_TEXTURE0 + TextureSlot::APVPositions);
        glBindTexture(GL_TEXTURE_2D, s->APVProbeCount ? s->APVPositionTex : s->FallbackBlack2D);
        glActiveTexture(GL_TEXTURE0 + TextureSlot::APVSH);
        glBindTexture(GL_TEXTURE_2D, s->APVProbeCount ? s->APVSHTex : s->FallbackBlack2D);
        glActiveTexture(GL_TEXTURE0);

        // Shader başına sampler/probe uniform'ları yeniden gönderilsin
        ++s->FrameIndex;
    }

    void Renderer3D::BindFrameResources(const std::shared_ptr<Shader>& shader)
    {
        if (!shader)
            return;
        auto& last = s->ShaderFrame[shader.get()];
        if (last == s->FrameIndex)
            return;
        last = s->FrameIndex;

        shader->SetInt("u_ShadowMap", (int)TextureSlot::ShadowAtlas);
        shader->SetInt("u_IBLPrefiltered", (int)TextureSlot::IBLPrefiltered);
        shader->SetInt("u_BRDFLUT", (int)TextureSlot::BRDFLut);
        shader->SetInt("u_Lightmap", (int)TextureSlot::Lightmap);
        shader->SetInt("u_APVPositionTex", (int)TextureSlot::APVPositions);
        shader->SetInt("u_APVSHTex", (int)TextureSlot::APVSH);
        shader->SetInt("u_APVNumProbes", s->APVProbeCount);
        int probeUnits[TextureSlot::MaxReflectionProbes];
        for (uint32_t i = 0; i < TextureSlot::MaxReflectionProbes; ++i)
            probeUnits[i] = (int)(TextureSlot::ReflectionProbeFirst + i);
        shader->SetIntArray("u_ReflectionProbes", probeUnits, TextureSlot::MaxReflectionProbes);
        for (size_t i = 0; i < s->Probes.size(); ++i)
        {
            const auto& p = s->Probes[i];
            std::string idx = "[" + std::to_string(i) + "]";
            shader->SetFloat4("u_ProbePositionIntensity" + idx, glm::vec4(p.Position, p.Intensity));
            shader->SetFloat4("u_ProbeBoxMin" + idx, glm::vec4(p.BoxMin, p.BlendDistance));
            shader->SetFloat4("u_ProbeBoxMax" + idx, glm::vec4(p.BoxMax, (float)p.MipCount));
        }
    }

    // ------------------------------------------------------------------ Gökyüzü

    void Renderer3D::DrawEnvironment()
    {
        UploadFrameData();
        auto& env = s->Env;
        if (!env.DrawSky)
            return;
        uint32_t cube = 0;
        float exposure = 1.0f;
        glm::vec3 tint(1.0f);
        if (env.BackgroundSkybox && env.BackgroundSkybox->GetRendererID())
        {
            cube = env.BackgroundSkybox->GetRendererID();
            exposure = env.BackgroundExposure;
            tint = env.BackgroundTint;
        }
        else if (env.Skybox && env.Skybox->GetRendererID())
        {
            cube = env.Skybox->GetRendererID();
            exposure = env.SkyboxExposure;
            tint = env.SkyboxTint;
        }
        else if (env.UseProceduralSky && s->EnvMap && s->EnvIsProcedural)
        {
            cube = s->EnvMap->GetSkyCubemapID();
        }
        if (!cube || !s_SkyboxShader || !s_SkyboxShader->IsValid())
            return;

        GLint prevDepthFunc = GL_LESS;
        glGetIntegerv(GL_DEPTH_FUNC, &prevDepthFunc);
        GLboolean cull = glIsEnabled(GL_CULL_FACE);
        glDepthFunc(GL_LEQUAL);
        glDepthMask(GL_FALSE);
        glDisable(GL_CULL_FACE);

        // Gökyüzü izdüşümü: kameranın görüş açısı (ortografikte 60°), küçük yakın/uzak düzlem (iyi koşullu ters)
        const RenderCamera& cam = s->Camera;
        const float p00 = cam.Projection[0][0], p11 = cam.Projection[1][1];
        const float aspect = std::abs(p00) > 1e-6f ? std::abs(p11 / p00) : 1.0f;
        float fovY = glm::radians(60.0f);
        if (!cam.Orthographic && std::abs(p11) > 1e-6f)
            fovY = 2.0f * std::atan(1.0f / std::abs(p11));
        glm::mat4 skyProj = glm::perspective(fovY, std::max(aspect, 1e-3f), 0.1f, 10.0f);
        if (!cam.Orthographic)
        {
            // Merkez dışı izdüşüm (jitter / kayık görünüm) korunur
            skyProj[2][0] = cam.Projection[2][0];
            skyProj[2][1] = cam.Projection[2][1];
        }
        const glm::mat4 skyInvViewProj = glm::inverse(skyProj * glm::mat4(glm::mat3(cam.View)));

        s_SkyboxShader->Bind();
        s_SkyboxShader->SetInt("u_Skybox", 0);
        s_SkyboxShader->SetFloat("u_Exposure", exposure);
        s_SkyboxShader->SetFloat3("u_Tint", tint);
        s_SkyboxShader->SetMat4("u_SkyInvViewProj", skyInvViewProj);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_CUBE_MAP, cube);
        static uint32_t s_SkyVAO = 0;
        if (!s_SkyVAO)
            glGenVertexArrays(1, &s_SkyVAO);
        glBindVertexArray(s_SkyVAO);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindVertexArray(0);

        glDepthMask(GL_TRUE);
        glDepthFunc(prevDepthFunc);
        if (cull) glEnable(GL_CULL_FACE);
        Renderer::GetStats().DrawCalls++;
    }

    void Renderer3D::DrawSkybox(std::shared_ptr<Cubemap> skybox, float exposure, float rotation, const glm::vec3& tint)
    {
        if (skybox && skybox != s->Env.Skybox)
        {
            s->Env.Skybox = skybox;
            UpdateEnvironmentMap();
        }
        s->Env.SkyboxExposure = exposure;
        s->Env.SkyboxRotation = rotation;
        s->Env.SkyboxTint = tint;
        s->FrameDirty = true;
        DrawEnvironment();
    }

    // ------------------------------------------------------------------ Çizim

    void Renderer3D::ApplyCulling(const std::shared_ptr<Material>& material, bool doubleSided)
    {
        Material::CullMode mode = material ? material->Culling : Material::CullMode::Back;
        if (doubleSided || mode == Material::CullMode::Off)
        {
            glDisable(GL_CULL_FACE);
            return;
        }
        glEnable(GL_CULL_FACE);
        glCullFace(mode == Material::CullMode::Front ? GL_FRONT : GL_BACK);
    }

    namespace
    {
        int s_ForcedLOD = -1;
        bool s_FrustumCulling = true;
        int s_OpaqueSorting = 1;
        uint32_t s_QueuedVertexColors = 0;

        uint32_t CurrentVertexColors()
        {
            if (!s_VertexColorBuffers || s_VertexColorMesh >= s_VertexColorBuffers->size())
                return 0;
            return (*s_VertexColorBuffers)[s_VertexColorMesh];
        }

        // Etkin karede kamera kesik konisinin dışında kalan çizim atlanır (kare dışı çizimlerde kamera belirsiz: çizilir)
        bool CameraSees(const glm::vec3& worldCenter, const glm::vec3& worldExtents)
        {
            if (!s_FrustumCulling || !s->FrameActive)
                return true;
            if (s->FrustumDirty)
            {
                Renderer3D::ExtractFrustum(s->Camera.Projection * s->Camera.View, s->Frustum);
                s->FrustumDirty = false;
            }
            if (Renderer3D::BoxInFrustum(s->Frustum, worldCenter, worldExtents))
                return true;
            Renderer::GetStats().CulledObjects++;
            return false;
        }

        // Sınırı bilinmeyen (boş / tek noktalı) mesh her zaman çizilir
        bool CameraSeesMesh(const Mesh& mesh, const glm::mat4& transform)
        {
            if (!s_FrustumCulling || !s->FrameActive || !(mesh.GetBoundsRadius() > 0.0f))
                return true;
            glm::vec3 center, extents;
            Renderer3D::TransformBox(transform, mesh.GetBoundsCenter(), mesh.GetBoundsExtents(), center, extents);
            return CameraSees(center, extents);
        }
    }

    void Renderer3D::SetForcedLOD(int level) { s_ForcedLOD = level; }
    int Renderer3D::GetForcedLOD() { return s_ForcedLOD; }
    void Renderer3D::SetFrustumCulling(bool enabled) { s_FrustumCulling = enabled; }
    bool Renderer3D::GetFrustumCulling() { return s_FrustumCulling; }
    void Renderer3D::SetOpaqueSorting(int mode) { s_OpaqueSorting = std::clamp(mode, 0, 1); }
    int Renderer3D::GetOpaqueSorting() { return s_OpaqueSorting; }

    bool Renderer3D::BoxInFrustum(const glm::vec4 planes[6], const glm::vec3& center, const glm::vec3& extents)
    {
        for (int i = 0; i < 6; ++i)
        {
            const glm::vec3 n(planes[i]);
            // Kutunun düzlem normali boyunca yarı kalınlığı: merkez düzlemin bundan fazla dışındaysa kutu tamamen dışarıda
            const float r = extents.x * std::abs(n.x) + extents.y * std::abs(n.y) + extents.z * std::abs(n.z);
            if (glm::dot(n, center) + planes[i].w < -r)
                return false;
        }
        return true;
    }

    void Renderer3D::TransformBox(const glm::mat4& m, const glm::vec3& center, const glm::vec3& extents, glm::vec3& outCenter,
                                  glm::vec3& outExtents)
    {
        outCenter = glm::vec3(m * glm::vec4(center, 1.0f));
        outExtents = glm::abs(glm::vec3(m[0])) * extents.x + glm::abs(glm::vec3(m[1])) * extents.y + glm::abs(glm::vec3(m[2])) * extents.z;
    }

    bool Renderer3D::SkinnedModelBounds(const Model& model, const glm::mat4* bones, uint32_t boneCount, uint32_t palette,
                                        glm::vec3& outCenter, glm::vec3& outExtents)
    {
        // Deri köşesi, etkileyen kemiklerin matrisleriyle taşınmış konumlarının ağırlıklı ortalamasıdır (shader ağırlıkları
        // normalize eder): her kemiğin taşıdığı bağlama kutusunun birleşimi onu her pozda kapsar
        glm::vec3 mn(std::numeric_limits<float>::max()), mx(-std::numeric_limits<float>::max());
        bool any = false;
        for (const auto& mesh : model.SkinnedMeshes)
        {
            if (!mesh)
                continue;
            for (const auto& box : mesh->GetBoneBoxes())
            {
                glm::vec3 c = box.Center, e = box.Extents;
                if (box.Bone >= 0)
                {
                    const uint32_t b = (uint32_t)box.Bone;
                    if (bones && b < boneCount)
                        TransformBox(bones[b], box.Center, box.Extents, c, e);
                    else if (b >= palette)
                        return false;   // shader'a yüklenmeyen kemik: konumu bilinmez, model kırpılmaz
                }
                mn = glm::min(mn, c - e);
                mx = glm::max(mx, c + e);
                any = true;
            }
        }
        if (!any)
            return false;
        outCenter = (mn + mx) * 0.5f;
        outExtents = (mx - mn) * 0.5f;
        return true;
    }

    int Renderer3D::SelectMeshLOD(const Mesh& mesh, const glm::mat4& transform)
    {
        if (!mesh.HasLODs())
            return 0;
        if (s_ForcedLOD >= 0)
            return std::min(s_ForcedLOD, mesh.GetLODCount() - 1);
        const RenderCamera& cam = s->Camera;
        const glm::vec3 center = glm::vec3(transform * glm::vec4(mesh.GetBoundsCenter(), 1.0f));
        const float scale = std::max({ glm::length(glm::vec3(transform[0])), glm::length(glm::vec3(transform[1])), glm::length(glm::vec3(transform[2])) });
        const float radius = mesh.GetBoundsRadius() * scale;
        float screen = radius * std::abs(cam.Projection[1][1]);
        if (!cam.Orthographic)
            screen /= std::max(glm::distance(center, cam.Position), 1e-3f);
        screen *= std::max(QualitySettings::GetPreset().LODBias, 0.01f);
        return mesh.SelectLOD(screen);
    }

    void Renderer3D::ExtractFrustum(const glm::mat4& vp, glm::vec4 planes[6])
    {
        // Gribb-Hartmann: satır birleşimleri, normalize (nokta·xyz + w >= 0 içeride)
        const glm::vec4 r0(vp[0][0], vp[1][0], vp[2][0], vp[3][0]);
        const glm::vec4 r1(vp[0][1], vp[1][1], vp[2][1], vp[3][1]);
        const glm::vec4 r2(vp[0][2], vp[1][2], vp[2][2], vp[3][2]);
        const glm::vec4 r3(vp[0][3], vp[1][3], vp[2][3], vp[3][3]);
        planes[0] = r3 + r0; planes[1] = r3 - r0;
        planes[2] = r3 + r1; planes[3] = r3 - r1;
        planes[4] = r3 + r2; planes[5] = r3 - r2;
        for (int i = 0; i < 6; ++i)
            planes[i] /= std::max(glm::length(glm::vec3(planes[i])), 1e-9f);
    }

    uint32_t Renderer3D::DrawClusterMesh(const ClusterMesh& clusters, const glm::mat4& transform, const std::shared_ptr<Material>& materialIn,
                                         float thresholdPixels, bool debugColors, uint32_t* outClusterCount)
    {
        if (!clusters.IsUploaded())
            return 0;
        std::shared_ptr<Material> material = materialIn ? materialIn : Material::CreateDefault();
        const RenderCamera& cam = s->Camera;
        // Seçim: kamera nesne uzayında, hata piksele çevrilir
        const glm::mat4 inv = glm::inverse(transform);
        const glm::vec3 cameraLocal = glm::vec3(inv * glm::vec4(cam.Position, 1.0f));
        const float scale = std::max({ glm::length(glm::vec3(transform[0])), glm::length(glm::vec3(transform[1])), glm::length(glm::vec3(transform[2])) });
        const float pixelScale = std::abs(cam.Projection[1][1]) * (float)std::max(cam.ViewportHeight, 1u) * 0.5f;
        glm::vec4 frustum[6];
        ExtractFrustum(cam.Projection * cam.View, frustum);
        static thread_local std::vector<uint32_t> s_Selected;
        clusters.Select(cameraLocal, scale, pixelScale, thresholdPixels, cam.Orthographic, transform, frustum, s_Selected);
        if (outClusterCount)
            *outClusterCount = (uint32_t)s_Selected.size();
        if (s_Selected.empty())
            return 0;

        UploadFrameData();
        auto shader = (material->MaterialShader && material->MaterialShader->IsValid()) ? material->MaterialShader : s_PBRShader;
        if (!shader || !shader->IsValid())
            return 0;
        shader->Bind();
        BindFrameResources(shader);
        shader->SetMat4("u_Transform", transform);
        shader->SetMat4("u_NormalMatrix", glm::mat4(glm::inverseTranspose(glm::mat3(transform))));
        shader->SetInt("u_ReceiveShadows", s->ReceiveShadows ? 1 : 0);
        shader->SetFloat3("u_Tint", s->Tint);
        shader->SetInt("u_UseVertexColor", 0);
        material->Bind(shader);
        if (material->MaterialShader)
            material->BindGraphUniforms(shader);
        ApplyCulling(material, false);
        if (s->Wireframe)
            glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        uint32_t triangles = 0;
        if (debugColors)
        {
            // Nanite "Clusters" görselleştirmesi: her küme kendi renginde (tek tek çizilir)
            static thread_local std::vector<uint32_t> s_One(1);
            for (uint32_t c : s_Selected)
            {
                uint32_t h = c * 2654435761u;
                const glm::vec3 color(((h >> 0) & 255) / 255.0f, ((h >> 8) & 255) / 255.0f, ((h >> 16) & 255) / 255.0f);
                shader->SetFloat3("u_Material.Albedo", glm::mix(color, glm::vec3(1.0f), 0.15f));
                s_One[0] = c;
                triangles += clusters.Draw(s_One);
            }
        }
        else
            triangles = clusters.Draw(s_Selected);
        if (s->Wireframe)
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        glDisable(GL_CULL_FACE);
        auto& stats = Renderer::GetStats();
        stats.DrawCalls++;
        stats.MeshCount++;
        stats.Triangles += triangles;
        return triangles;
    }

    void Renderer3D::DrawMeshInternal(const std::shared_ptr<Mesh>& meshIn, const glm::mat4& transform,
                                      const std::shared_ptr<Material>& materialIn, bool doubleSided, bool fromQueue)
    {
        if (!meshIn)
            return;
        // Hareket (TAA): kimlik kararlı kalsın diye kırpmadan önce; graph malzeme zamanla değişebilir -> hareketli
        const glm::mat4* prevTransform = nullptr;
        const bool transformMoved = !fromQueue && TrackMeshMotion(meshIn.get(), transform, &prevTransform);
        const glm::mat4 prevCopy = prevTransform ? *prevTransform : transform;   // harita sonraki çizimlerde değişebilir
        const bool moving = fromQueue ? s_QueuedMoving : (transformMoved || (s_MotionCur && materialIn && materialIn->MaterialShader));
        // Yer değiştiren nesnenin gölgesi de yer değiştirir (kamera görmese de gölgesi görüş alanına düşebilir)
        if (transformMoved && s_MotionCur && meshIn->GetBoundsRadius() > 0.0f && (!materialIn || materialIn->Rendering != Material::RenderMode::Transparent))
        {
            glm::vec3 c, e;
            TransformBox(transform, meshIn->GetBoundsCenter(), meshIn->GetBoundsExtents(), c, e);
            AddShadowBox(c, e);
        }
        // Görüş kırpması: kameranın kesik konisi dışındaki mesh çizilmez, saydam kuyruğa da alınmaz
        if (!fromQueue && !CameraSeesMesh(*meshIn, transform))
            return;
        // Mesh Paint: örneğe özel köşe renkleri (kuyruktan gelen çizimde kuyruğa alınırken yakalanan tampon)
        const uint32_t vertexColors = fromQueue ? s_QueuedVertexColors : CurrentVertexColors();
        // Otomatik LOD: uzaktaki nesnelerde sadeleştirilmiş seviye (boyanmış nesne LOD0: köşe sırası korunur)
        const Mesh* mesh = (meshIn->HasLODs() && !vertexColors) ? meshIn->GetLOD(SelectMeshLOD(*meshIn, transform)) : meshIn.get();
        std::shared_ptr<Material> material = materialIn ? materialIn : Material::CreateDefault();

        if (material->Rendering == Material::RenderMode::Transparent && !fromQueue && s->FrameActive)
        {
            float dist = glm::length(glm::vec3(transform[3]) - s->Camera.Position);
            s->Transparent.push_back({ meshIn, transform, material, doubleSided, dist, CurrentVertexColors(), s->Tint, moving });
            // Saydamlar G-buffer'a yazmaz: hareketliyse çizilirken TAA tepki maskesine kapladığı pikseller yazılır (aşağıda);
            // maske kullanılamıyorsa (MSAA / TXAA) ekran dikdörtgeni
            if (moving && meshIn->GetBoundsRadius() > 0.0f && !PostProcessStack::CanDrawReactive())
            {
                glm::vec3 c, e;
                TransformBox(transform, meshIn->GetBoundsCenter(), meshIn->GetBoundsExtents(), c, e);
                AddMotionBox(c, e);
            }
            return;
        }
        // GBAA / SRAA geometri listesi (yalnız Opaque kipi: alfa kesmeli yüzeyin görünen kenarı üçgen kenarı değildir)
        if (!fromQueue && s_GeometryCur && material->Rendering == Material::RenderMode::Opaque)
        {
            const Material::CullMode cull = material->Culling;
            s_GeometryCur->push_back({ meshIn, mesh, transform, (doubleSided || cull == Material::CullMode::Off) ? 0 : (cull == Material::CullMode::Front ? 2 : 1) });
        }
        // Hareket eden opak nesne: TAA hareket vektörü geçişinde kendi hareketiyle çizilir (önceki konumu biliniyorsa)
        if (!fromQueue && transformMoved && prevTransform && s_MotionCur && material->Rendering != Material::RenderMode::Transparent)
        {
            MotionTrack::VelocityDraw v;
            v.MeshRef = meshIn;
            v.Draw = mesh;
            v.Cur = transform;
            v.Prev = prevCopy;
            const Material::CullMode cull = material->Culling;
            v.Cull = (doubleSided || cull == Material::CullMode::Off) ? 0 : (cull == Material::CullMode::Front ? 2 : 1);
            v.Exact = !material->MaterialShader;   // graph malzemede köşe hareketi olabilir: yaklaşık
            s_MotionCur->Velocity.push_back(std::move(v));
            // Eski yeri (ışınlanma, yeniden doğma, hızlı nesne) hareket vektörü geçişinin "önceki konum" çizimi piksel piksel
            // işaretler. Burada ayrıca eklenen kaba ekran dikdörtgeni kaldırıldı: dünya eksenine hizalı kutu, çapraz uçan ince
            // mermide (havuzdan gelen ölçekli küp) namludan ekranın üstüne kadar geniş bir şeridi kaplıyor, TAA orada üç kare
            // geçmişi atıyordu: küpün üstünde ateş edince çizgiler silinip köşeler merdivenleşiyordu
        }
        // Opak: kuyruğa (FlushOpaque yakından uzağa çizer)
        if (!fromQueue && s->FrameActive && s_OpaqueSorting)
        {
            const glm::vec3 center = glm::vec3(transform * glm::vec4(meshIn->GetBoundsCenter(), 1.0f));
            const glm::vec3 d = center - s->Camera.Position;
            // Sıralama uzaklığı: merkez + dünya yarıçapı. Büyük zemin / duvar / tavan (merkezi yakın görünse de) küçük
            // nesnelerden sonra çizilir; üstlerindeki eşyaların altında kalan pikselleri gölgelendirilmeden elenir
            const float scale = std::sqrt(std::max({ glm::dot(glm::vec3(transform[0]), glm::vec3(transform[0])),
                                                     glm::dot(glm::vec3(transform[1]), glm::vec3(transform[1])),
                                                     glm::dot(glm::vec3(transform[2]), glm::vec3(transform[2])) }));
            const float far = std::sqrt(glm::dot(d, d)) + meshIn->GetBoundsRadius() * scale;
            const float dist2 = far * far;
            // Alfa kesmeli (discard): erken derinlik testi yapılamaz, opakların derinliği yazıldıktan sonra çizilir
            const uint32_t key = material->Rendering == Material::RenderMode::Cutout ? 1u : 0u;
            s->Opaque.push_back({ meshIn, transform, material, dist2, key, vertexColors, s->Tint, doubleSided, s->ReceiveShadows, moving });
            return;
        }

        UploadFrameData();

        auto shader = (material->MaterialShader && material->MaterialShader->IsValid()) ? material->MaterialShader : s_PBRShader;
        if (!shader || !shader->IsValid())
            return;
        shader->Bind();
        BindFrameResources(shader);

        shader->SetMat4("u_Transform", transform);
        shader->SetMat4("u_NormalMatrix", glm::mat4(glm::inverseTranspose(glm::mat3(transform))));
        shader->SetInt("u_ReceiveShadows", s->ReceiveShadows ? 1 : 0);
        shader->SetFloat3("u_Tint", s->Tint);
        material->Bind(shader);
        if (material->MaterialShader)
            material->BindGraphUniforms(shader);

        ApplyCulling(material, doubleSided);
        bool transparent = material->Rendering == Material::RenderMode::Transparent;
        if (transparent)
        {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glDepthMask(GL_FALSE);
        }

        if (s->Wireframe)
            glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        // Paylaşılan VAO'ya bu çizim için renk akışı bağlanır, çizimden sonra kapatılır (diğer örnekler beyaz okur)
        const bool painted = vertexColors && mesh->GetVertexArray();
        shader->SetInt("u_UseVertexColor", painted ? 1 : 0);
        if (painted)
        {
            mesh->GetVertexArray()->Bind();
            glBindBuffer(GL_ARRAY_BUFFER, vertexColors);
            glEnableVertexAttribArray(kVertexColorAttrib);
            glVertexAttribPointer(kVertexColorAttrib, 4, GL_UNSIGNED_BYTE, GL_TRUE, 4, nullptr);
        }
        if (moving)
            shader->SetFloat("u_MotionFlag", 2.0f);
        RenderCommand::DrawIndexed(mesh->GetVertexArray(), mesh->GetIndexCount());
        if (moving)
            shader->SetFloat("u_MotionFlag", 0.0f);   // aynı programı kullanan sonraki çizimler hareketsiz başlar
        if (painted)
        {
            mesh->GetVertexArray()->Bind();
            glDisableVertexAttribArray(kVertexColorAttrib);
        }
        if (s->Wireframe)
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

        if (transparent)
        {
            glDepthMask(GL_TRUE);
            glDisable(GL_BLEND);
            Renderer::GetStats().TransparentDraws++;
            // Hareket eden saydam mesh (mermi izi, namlu alevi): TAA tepki maskesine yalnız kapladığı pikseller yazılır. Maske
            // yazılamazsa (sahne hedefi bağlı değil) ekran dikdörtgenine dönülür
            if (moving && PostProcessStack::CanDrawReactive() && !DrawReactiveMesh(*mesh, transform)
                && meshIn->GetBoundsRadius() > 0.0f)
            {
                glm::vec3 c, e;
                TransformBox(transform, meshIn->GetBoundsCenter(), meshIn->GetBoundsExtents(), c, e);
                AddMotionBox(c, e);
            }
        }
        glDisable(GL_CULL_FACE);

        auto& stats = Renderer::GetStats();
        stats.DrawCalls++;
        stats.MeshCount++;
        stats.Vertices += mesh->GetVertexCount();
        stats.Triangles += mesh->GetIndexCount() / 3;
    }

    void Renderer3D::BindInstanceAttributes(const Mesh& mesh, uint32_t instanceBuffer, uint32_t firstInstance)
    {
        mesh.GetVertexArray()->Bind();
        glBindBuffer(GL_ARRAY_BUFFER, instanceBuffer);
        const uintptr_t base = (uintptr_t)firstInstance * (uintptr_t)kInstanceStride;
        for (GLuint i = 0; i < 5; ++i)
        {
            glEnableVertexAttribArray(kInstanceAttrib + i);
            glVertexAttribPointer(kInstanceAttrib + i, 4, GL_FLOAT, GL_FALSE, kInstanceStride, (const void*)(base + i * 16));
            glVertexAttribDivisor(kInstanceAttrib + i, 1);
        }
    }

    void Renderer3D::UnbindInstanceAttributes(const Mesh& mesh)
    {
        mesh.GetVertexArray()->Bind();
        for (GLuint i = 0; i < 5; ++i)
        {
            glVertexAttribDivisor(kInstanceAttrib + i, 0);
            glDisableVertexAttribArray(kInstanceAttrib + i);
        }
    }

    void Renderer3D::DrawMeshInstanced(const std::shared_ptr<Mesh>& mesh, const glm::mat4& transform, uint32_t instanceBuffer,
                                       uint32_t instanceCount, const std::shared_ptr<Material>& materialIn,
                                       const std::vector<glm::mat4>* fallbackTransforms, bool doubleSided, uint32_t firstInstance)
    {
        if (!mesh || !mesh->GetVertexArray() || !instanceBuffer || instanceCount == 0)
            return;
        std::shared_ptr<Material> material = materialIn ? materialIn : Material::CreateDefault();
        if (material->MaterialShader && material->MaterialShader->IsValid())
        {
            if (fallbackTransforms)
                for (const glm::mat4& local : *fallbackTransforms)
                    DrawMeshInternal(mesh, transform * local, material, doubleSided, false);
            return;
        }
        if (!s_PBRShader || !s_PBRShader->IsValid())
            return;
        UploadFrameData();
        const auto& shader = s_PBRShader;
        shader->Bind();
        BindFrameResources(shader);
        shader->SetMat4("u_Transform", transform);
        shader->SetMat4("u_NormalMatrix", glm::mat4(glm::inverseTranspose(glm::mat3(transform))));
        shader->SetInt("u_ReceiveShadows", s->ReceiveShadows ? 1 : 0);
        shader->SetFloat3("u_Tint", s->Tint);
        shader->SetInt("u_UseVertexColor", 0);
        shader->SetInt("u_Instanced", 1);
        material->Bind(shader);
        ApplyCulling(material, doubleSided);
        const bool transparent = material->Rendering == Material::RenderMode::Transparent;
        if (transparent)
        {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glDepthMask(GL_FALSE);
        }
        if (s->Wireframe)
            glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        BindInstanceAttributes(*mesh, instanceBuffer, firstInstance);
        glDrawElementsInstanced(GL_TRIANGLES, (GLsizei)mesh->GetIndexCount(), GL_UNSIGNED_INT, nullptr, (GLsizei)instanceCount);
        UnbindInstanceAttributes(*mesh);
        shader->SetInt("u_Instanced", 0);
        if (s->Wireframe)
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        if (transparent)
        {
            glDepthMask(GL_TRUE);
            glDisable(GL_BLEND);
        }
        glDisable(GL_CULL_FACE);

        auto& stats = Renderer::GetStats();
        stats.DrawCalls++;
        stats.MeshCount += instanceCount;
        stats.Vertices += mesh->GetVertexCount() * instanceCount;
        stats.Triangles += mesh->GetIndexCount() / 3 * instanceCount;
    }

    void Renderer3D::DrawTerrain(const std::shared_ptr<Mesh>& mesh, const glm::mat4& transform, const TerrainDrawParams& params)
    {
        if (!mesh || !s_TerrainShader || !s_TerrainShader->IsValid())
            return;
        UploadFrameData();
        auto& shader = s_TerrainShader;
        shader->Bind();
        BindFrameResources(shader);
        shader->SetMat4("u_Transform", transform);
        shader->SetMat4("u_NormalMatrix", glm::mat4(glm::inverseTranspose(glm::mat3(transform))));
        shader->SetInt("u_ReceiveShadows", (params.ReceiveShadows && s->ReceiveShadows) ? 1 : 0);
        shader->SetFloat3("u_Tint", glm::vec3(1.0f));
        shader->SetInt("u_LayerCount", params.LayerCount);
        shader->SetFloat2("u_TerrainSize", params.Size);

        // Dokular: 6 = splat, 7..10 = katmanlar (8..11 normalde Shader Graph'a ayrılı; terrain grafik kullanmaz)
        if (!s_TerrainWhite)
        {
            s_TerrainWhite = Texture2D::Create(1, 1);
            uint32_t white = 0xffffffff;
            s_TerrainWhite->SetData(&white, 4);
        }
        const auto& s_White = s_TerrainWhite;
        (params.Splat ? params.Splat : s_White)->Bind(6);
        shader->SetInt("u_Splat", 6);
        for (int i = 0; i < 4; ++i)
        {
            const auto& tex = params.Layers[(size_t)i];
            (tex ? tex : s_White)->Bind((uint32_t)(7 + i));
            char name[32];
            snprintf(name, sizeof(name), "u_Layer%d", i);
            shader->SetInt(name, 7 + i);
            snprintf(name, sizeof(name), "u_LayerTint[%d]", i);
            shader->SetFloat4(name, params.Tint[(size_t)i]);
            snprintf(name, sizeof(name), "u_LayerParams[%d]", i);
            shader->SetFloat4(name, params.Params[(size_t)i]);
        }

        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        if (s->Wireframe)
            glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        RenderCommand::DrawIndexed(mesh->GetVertexArray(), mesh->GetIndexCount());
        if (s->Wireframe)
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        glDisable(GL_CULL_FACE);

        auto& stats = Renderer::GetStats();
        stats.DrawCalls++;
        stats.MeshCount++;
        stats.Vertices += mesh->GetVertexCount();
        stats.Triangles += mesh->GetIndexCount() / 3;
    }

    void Renderer3D::SetDebugView(DebugView view) { s->DebugView = (int)view; s->FrameDirty = true; }
    Renderer3D::DebugView Renderer3D::GetDebugView() { return (DebugView)s->DebugView; }
    void Renderer3D::SetWireframe(bool wireframe) { s->Wireframe = wireframe; }
    bool Renderer3D::GetWireframe() { return s->Wireframe; }

    void Renderer3D::SetReceiveShadows(bool receive)
    {
        s->ReceiveShadows = receive;
    }

    void Renderer3D::SetTint(const glm::vec3& tint)
    {
        s->Tint = tint;
    }

    void Renderer3D::DrawMesh(const std::shared_ptr<Mesh>& mesh, const glm::mat4& transform, std::shared_ptr<Material> material, bool doubleSided)
    {
        DrawMeshInternal(mesh, transform, material, doubleSided, false);
    }

    void Renderer3D::DrawDecals(const std::vector<DecalDraw>& decals)
    {
        if (decals.empty() || !s)
            return;
        // Decal'ler opak derinliği okur: kuyruktaki opaklar önce çizilir
        FlushOpaque();
        if (!s->DecalShader)
            s->DecalShader = Shader::Create("Engine/src/Shaders/3D/Decal.cqsh");
        if (!s->DecalShader)
            return;

        GLint drawFbo = 0;
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFbo);
        if (drawFbo == 0)
            return;   // varsayılan pencere tamponunda derinlik okunamaz
        GLint vp[4];
        glGetIntegerv(GL_VIEWPORT, vp);
        if (vp[2] <= 0 || vp[3] <= 0)
            return;

        // Mevcut derinlik ekinin biçimi (blit için kopya aynı biçimde olmalı)
        GLint objType = GL_NONE, objName = 0, format = 0;
        GLenum attachment = GL_DEPTH_ATTACHMENT;
        glGetNamedFramebufferAttachmentParameteriv(drawFbo, attachment, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &objType);
        if (objType == GL_NONE)
        {
            attachment = GL_DEPTH_STENCIL_ATTACHMENT;
            glGetNamedFramebufferAttachmentParameteriv(drawFbo, attachment, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &objType);
        }
        if (objType == GL_NONE)
            return;
        glGetNamedFramebufferAttachmentParameteriv(drawFbo, attachment, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &objName);
        if (objType == GL_TEXTURE)
            glGetTextureLevelParameteriv((GLuint)objName, 0, GL_TEXTURE_INTERNAL_FORMAT, &format);
        else if (objType == GL_RENDERBUFFER)
            glGetNamedRenderbufferParameteriv((GLuint)objName, GL_RENDERBUFFER_INTERNAL_FORMAT, &format);
        if (format == 0 || vp[2] < 1 || vp[3] < 1)
            return;

        if (!s->DecalDepthTex || s->DecalW != vp[2] || s->DecalH != vp[3] || s->DecalFormat != format)
        {
            if (s->DecalDepthTex) glDeleteTextures(1, &s->DecalDepthTex);
            if (s->DecalDepthFBO) glDeleteFramebuffers(1, &s->DecalDepthFBO);
            glCreateTextures(GL_TEXTURE_2D, 1, &s->DecalDepthTex);
            glTextureStorage2D(s->DecalDepthTex, 1, (GLenum)format, vp[2], vp[3]);
            glTextureParameteri(s->DecalDepthTex, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTextureParameteri(s->DecalDepthTex, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTextureParameteri(s->DecalDepthTex, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTextureParameteri(s->DecalDepthTex, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glCreateFramebuffers(1, &s->DecalDepthFBO);
            bool hasStencil = format == GL_DEPTH24_STENCIL8 || format == GL_DEPTH32F_STENCIL8;
            glNamedFramebufferTexture(s->DecalDepthFBO, hasStencil ? GL_DEPTH_STENCIL_ATTACHMENT : GL_DEPTH_ATTACHMENT, s->DecalDepthTex, 0);
            s->DecalW = vp[2];
            s->DecalH = vp[3];
            s->DecalFormat = format;
        }
        glBlitNamedFramebuffer((GLuint)drawFbo, s->DecalDepthFBO, vp[0], vp[1], vp[0] + vp[2], vp[1] + vp[3], 0, 0, vp[2], vp[3],
                               GL_DEPTH_BUFFER_BIT, GL_NEAREST);
        glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)drawFbo);
        glViewport(vp[0], vp[1], vp[2], vp[3]);

        UploadFrameData();
        // Varlık kimliği gibi ek renk hedeflerine yazma (seçim tamponu bozulmasın)
        for (GLuint i = 1; i < 4; ++i)
            glColorMaski(i, GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        GLboolean depthTest = glIsEnabled(GL_DEPTH_TEST), blend = glIsEnabled(GL_BLEND), cull = glIsEnabled(GL_CULL_FACE);
        GLboolean depthMask;
        glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glEnable(GL_BLEND);
        glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
        // Arka yüzler: kamera kutunun içindeyken de çizilir
        glEnable(GL_CULL_FACE);
        glCullFace(GL_FRONT);

        auto& shader = s->DecalShader;
        shader->Bind();
        BindFrameResources(shader);
        glBindTextureUnit(0, s->DecalDepthTex);
        shader->SetInt("u_Depth", 0);
        shader->SetInt("u_Albedo", 1);
        shader->SetFloat2("u_ViewportOrigin", glm::vec2((float)vp[0], (float)vp[1]));
        shader->SetFloat2("u_ViewportSize", glm::vec2((float)vp[2], (float)vp[3]));
        shader->SetMat4("u_ViewProjection", s->Frame.ViewProjection);
        for (const auto& d : decals)
        {
            shader->SetMat4("u_Transform", d.Transform);
            shader->SetMat4("u_InvTransform", glm::inverse(d.Transform));
            shader->SetFloat4("u_Color", d.Color);
            shader->SetFloat2("u_Tiling", d.Tiling);
            shader->SetFloat2("u_Offset", d.Offset);
            shader->SetFloat("u_AngleFadeCos", d.AngleFadeCos);
            shader->SetFloat("u_Emission", d.Emission);
            shader->SetInt("u_UseTexture", d.Texture ? 1 : 0);
            if (d.Texture)
                d.Texture->Bind(1);
            s_CubeMesh->Bind();
            glDrawElements(GL_TRIANGLES, (GLsizei)s_CubeMesh->GetIndexCount(), GL_UNSIGNED_INT, nullptr);
        }

        glCullFace(GL_BACK);
        if (!cull) glDisable(GL_CULL_FACE);
        if (!blend) glDisable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        if (depthTest) glEnable(GL_DEPTH_TEST);
        glDepthMask(depthMask);
        for (GLuint i = 1; i < 4; ++i)
            glColorMaski(i, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    }

    void Renderer3D::DrawCube(const glm::mat4& transform, std::shared_ptr<Material> material) { DrawMeshInternal(s_CubeMesh, transform, material, false, false); }
    void Renderer3D::DrawSphere(const glm::mat4& transform, std::shared_ptr<Material> material) { DrawMeshInternal(s_SphereMesh, transform, material, false, false); }
    void Renderer3D::DrawPlane(const glm::mat4& transform, std::shared_ptr<Material> material) { DrawMeshInternal(s_PlaneMesh, transform, material, false, false); }
    void Renderer3D::DrawCylinder(const glm::mat4& transform, std::shared_ptr<Material> material) { DrawMeshInternal(s_CylinderMesh, transform, material, false, false); }

    void Renderer3D::DrawModel(const glm::mat4& transform, std::shared_ptr<Model> model)
    {
        if (!model)
            return;
        for (size_t i = 0; i < model->Meshes.size(); ++i)
        {
            auto material = (i < model->Materials.size()) ? model->Materials[i] : nullptr;
            s_VertexColorMesh = i;
            DrawMeshInternal(model->Meshes[i], transform, material, false, false);
        }
        s_VertexColorMesh = 0;
    }

    void Renderer3D::SetVertexColorBuffers(const std::vector<uint32_t>* buffers)
    {
        s_VertexColorBuffers = buffers;
        s_VertexColorMesh = 0;
    }

    void Renderer3D::DrawSkinnedModel(const glm::mat4& transform, std::shared_ptr<Model> model,
                                      const std::vector<glm::mat4>& boneMatrices)
    {
        if (!model || !model->IsSkinned || !model->SkeletonData || model->SkinnedMeshes.empty())
            return;
        if (!s_PBRSkinnedShader || !s_PBRSkinnedShader->IsValid())
            return;

        const uint32_t boneCount = model->SkeletonData->GetBoneCount();
        if (boneCount == 0)
            return;
        constexpr uint32_t kMaxBones = 128;
        const uint32_t palette = std::min(boneCount, kMaxBones);
        static std::vector<glm::mat4> s_Scratch;
        s_Scratch.resize(palette);
        for (uint32_t i = 0; i < palette; ++i)
            s_Scratch[i] = (i < boneMatrices.size()) ? boneMatrices[i] : glm::mat4(1.0f);

        // Görüş kırpması: animasyonlu pozu saran kutu kameranın dışındaysa model çizilmez
        if (s_FrustumCulling && s->FrameActive)
        {
            glm::vec3 center, extents;
            if (SkinnedModelBounds(*model, s_Scratch.data(), palette, palette, center, extents))
            {
                glm::vec3 worldCenter, worldExtents;
                TransformBox(transform, center, extents, worldCenter, worldExtents);
                if (!CameraSees(worldCenter, worldExtents))
                    return;
            }
        }

        UploadFrameData();
        auto& shader = s_PBRSkinnedShader;
        shader->Bind();
        BindFrameResources(shader);
        shader->SetMat4("u_Transform", transform);
        shader->SetMat4("u_NormalMatrix", glm::mat4(glm::inverseTranspose(glm::mat3(transform))));
        shader->SetMat4Array("u_BoneTransforms", s_Scratch.data(), palette);
        shader->SetFloat3("u_Tint", s->Tint);
        // Animasyonlu model her karede hareketli (kemikler dönüşüm değişmeden de oynar): TAA bu piksellerde iz bırakmaz
        shader->SetFloat("u_MotionFlag", s_MotionCur ? 2.0f : 0.0f);
        // Hareket vektörü: önceki model dönüşümü (varlık kimliğine, yoksa aynı modelin kaçıncı çizimi olduğuna göre); kemikler bu karenin
        if (s_MotionCur)
        {
            const uint64_t key = MotionKey(s_MotionCur->SkinnedOccurrence, s_MotionCur->SkinnedIdentityOccurrence, model.get());
            s_MotionCur->CurSkinned[key] = transform;
            auto prev = s_MotionCur->PrevSkinned.find(key);
            if (prev != s_MotionCur->PrevSkinned.end())
            {
                MotionTrack::SkinnedVelocityDraw v;
                v.ModelRef = model;
                v.Cur = transform;
                v.Prev = prev->second;
                v.Bones.assign(s_Scratch.begin(), s_Scratch.begin() + palette);
                s_MotionCur->SkinnedVelocity.push_back(std::move(v));
            }
        }
        // Gölgesi de her karede değişir
        if (s_MotionCur)
        {
            glm::vec3 center, extents;
            if (SkinnedModelBounds(*model, s_Scratch.data(), palette, palette, center, extents))
            {
                glm::vec3 worldCenter, worldExtents;
                TransformBox(transform, center, extents, worldCenter, worldExtents);
                AddShadowBox(worldCenter, worldExtents);
            }
        }

        for (size_t i = 0; i < model->SkinnedMeshes.size(); ++i)
        {
            auto& mesh = model->SkinnedMeshes[i];
            auto material = (i < model->Materials.size() && model->Materials[i]) ? model->Materials[i] : Material::CreateDefault();
            material->Bind(shader);
            ApplyCulling(material, false);
            if (s->Wireframe)
                glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
            RenderCommand::DrawIndexed(mesh->GetVertexArray(), mesh->GetIndexCount());
            if (s->Wireframe)
                glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

            auto& stats = Renderer::GetStats();
            stats.DrawCalls++;
            stats.MeshCount++;
            stats.Vertices += mesh->GetVertexCount();
            stats.Triangles += mesh->GetIndexCount() / 3;
        }
        shader->SetFloat("u_MotionFlag", 0.0f);
        glDisable(GL_CULL_FACE);
    }

    void Renderer3D::FlushOpaque()
    {
        if (!s || s->Opaque.empty())
            return;
        // Önden arkaya: yakındaki yüzey derinliği önce yazar, arkadakilerin parçaları gölgelendirilmeden elenir; alfa
        // kesmeliler opaklardan sonra
        std::sort(s->Opaque.begin(), s->Opaque.end(), [](const OpaqueItem& a, const OpaqueItem& b) {
            if (a.SortKey != b.SortKey)
                return a.SortKey < b.SortKey;
            return a.Distance2 < b.Distance2;
        });
        const glm::vec3 tint = s->Tint;
        const bool receive = s->ReceiveShadows;
        for (auto& item : s->Opaque)
        {
            s_QueuedVertexColors = item.VertexColors;
            s->Tint = item.Tint;
            s->ReceiveShadows = item.ReceiveShadows;
            s_QueuedMoving = item.Moving;
            DrawMeshInternal(item.MeshRef, item.Transform, item.MaterialRef, item.DoubleSided, true);
        }
        s->Tint = tint;
        s->ReceiveShadows = receive;
        s_QueuedVertexColors = 0;
        s_QueuedMoving = false;
        s->Opaque.clear();
    }

    void Renderer3D::EndScene()
    {
        FlushOpaque();
        if (!s->Transparent.empty())
        {
            std::sort(s->Transparent.begin(), s->Transparent.end(),
                [](const TransparentItem& a, const TransparentItem& b) { return a.Distance > b.Distance; });
            const glm::vec3 tint = s->Tint;
            for (auto& item : s->Transparent)
            {
                s_QueuedVertexColors = item.VertexColors;
                s->Tint = item.Tint;
                s_QueuedMoving = item.Moving;
                DrawMeshInternal(item.MeshRef, item.Transform, item.MaterialRef, item.DoubleSided, true);
            }
            s->Tint = tint;
            s_QueuedVertexColors = 0;
            s_QueuedMoving = false;
            s->Transparent.clear();
        }
        s->FrameActive = false;
        s_MotionCur = nullptr;
    }

    // ------------------------------------------------------------------ Gizmo'lar

    static void DrawUnlit(const std::shared_ptr<Shader>& shader, const std::shared_ptr<Mesh>& mesh, const glm::mat4& viewProj,
                          const glm::mat4& transform, const glm::vec3& color, bool linearOutput)
    {
        if (!shader || !shader->IsValid() || !mesh)
            return;
        shader->Bind();
        shader->SetMat4("u_ViewProjection", viewProj);
        shader->SetMat4("u_Transform", transform);
        shader->SetFloat3("u_Color", color);
        shader->SetInt("u_UseTexture", 0);
        shader->SetInt("u_LinearOutput", linearOutput ? 1 : 0);
        RenderCommand::DrawIndexed(mesh->GetVertexArray(), mesh->GetIndexCount());
        Renderer::GetStats().DrawCalls++;
    }

    void Renderer3D::DrawLightGizmo(const glm::vec3& position, const glm::vec3& color, float size)
    {
        glm::mat4 transform = glm::scale(glm::translate(glm::mat4(1.0f), position), glm::vec3(size));
        DrawUnlit(s_UnlitShader, s_SphereMesh, s->GizmoViewProjection, transform, color, s->Env.HDROutput);
    }

    namespace
    {
        // Simge çizimi: görüş-izdüşümü ve çıktı (doğrusal HDR sahne / sRGB son görüntü) çağırana göre
        void DrawIconQuad(const std::string& iconPath, const glm::vec3& position, const glm::vec3& cameraRight, const glm::vec3& cameraUp,
                          float worldSize, const glm::vec3& color, float alpha, const glm::mat4& viewProjection, bool linearOutput,
                          const std::shared_ptr<Mesh>& plane);
    }

    void Renderer3D::DrawGizmoIcon(const std::string& iconPath, const glm::vec3& position, const glm::vec3& cameraRight, const glm::vec3& cameraUp,
                                   float worldSize, const glm::vec3& color, float alpha)
    {
        DrawIconQuad(iconPath, position, cameraRight, cameraUp, worldSize, color, alpha, s->GizmoViewProjection, s->Env.HDROutput, s_PlaneMesh);
    }

    void Renderer3D::DrawGizmoIconOverlay(const std::string& iconPath, const glm::vec3& position, const glm::vec3& cameraRight, const glm::vec3& cameraUp,
                                          float worldSize, const glm::vec3& color, float alpha, const glm::mat4& viewProjection)
    {
        // Son görüntünün ikinci eki (nesne kimliği, fareyle seçim) değişmez
        glColorMaski(1, GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        DrawIconQuad(iconPath, position, cameraRight, cameraUp, worldSize, color, alpha, viewProjection, false, s_PlaneMesh);
        glColorMaski(1, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    }

    namespace
    {
    void DrawIconQuad(const std::string& iconPath, const glm::vec3& position, const glm::vec3& cameraRight, const glm::vec3& cameraUp,
                      float worldSize, const glm::vec3& color, float alpha, const glm::mat4& viewProjection, bool linearOutput,
                      const std::shared_ptr<Mesh>& plane)
    {
        if (!s_IconShader)
            s_IconShader = Shader::Create("Engine/src/Shaders/Editor/GizmoIcon.cqsh");
        if (!s_IconShader || !s_IconShader->IsValid() || !plane)
            return;
        auto& tex = s_Icons[iconPath];
        if (!tex)
            tex = Texture2D::Create(iconPath);
        if (!tex)
            return;
        // Düzlem ağı XZ'de (-1..1, u = x, v = z): x -> kamera sağı, z -> kamera yukarısı, y -> kameraya
        const glm::vec3 toCamera = glm::normalize(glm::cross(cameraRight, cameraUp));
        const float h = worldSize * 0.5f;
        glm::mat4 transform(1.0f);
        transform[0] = glm::vec4(cameraRight * h, 0.0f);
        transform[1] = glm::vec4(toCamera, 0.0f);
        transform[2] = glm::vec4(-cameraUp * h, 0.0f);
        transform[3] = glm::vec4(position, 1.0f);
        const GLboolean depth = glIsEnabled(GL_DEPTH_TEST);
        const GLboolean blend = glIsEnabled(GL_BLEND);
        const GLboolean cull = glIsEnabled(GL_CULL_FACE);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        s_IconShader->Bind();
        s_IconShader->SetMat4("u_ViewProjection", viewProjection);
        s_IconShader->SetMat4("u_Transform", transform);
        s_IconShader->SetFloat3("u_Color", color);
        s_IconShader->SetFloat("u_Alpha", alpha);
        s_IconShader->SetInt("u_LinearOutput", linearOutput ? 1 : 0);
        tex->Bind(0);
        s_IconShader->SetInt("u_Icon", 0);
        RenderCommand::DrawIndexed(plane->GetVertexArray(), plane->GetIndexCount());
        Renderer::GetStats().DrawCalls++;
        if (depth) glEnable(GL_DEPTH_TEST);
        if (!blend) glDisable(GL_BLEND);
        if (cull) glEnable(GL_CULL_FACE);
    }
    }

    void Renderer3D::DrawSpotLightGizmo(const glm::vec3& position, const glm::vec3& direction, const glm::vec3& color, float size)
    {
        glm::mat4 transform = glm::translate(glm::mat4(1.0f), position) * OrientationFromDirection(direction);
        transform = glm::scale(transform, glm::vec3(size));
        DrawUnlit(s_UnlitShader, s_ConeMesh, s->GizmoViewProjection, transform, color, s->Env.HDROutput);
    }

    void Renderer3D::DrawDirectionalLightGizmo(const glm::vec3& position, const glm::vec3& direction, const glm::vec3& color, float size)
    {
        glm::mat4 transform = glm::translate(glm::mat4(1.0f), position) * OrientationFromDirection(direction);
        transform = glm::scale(transform, glm::vec3(size));
        DrawUnlit(s_UnlitShader, s_ArrowMesh, s->GizmoViewProjection, transform, color, s->Env.HDROutput);
    }

    // ------------------------------------------------------------------ Hot reload

    void Renderer3D::ReloadShaders()
    {
        for (auto* sh : { &s_PBRShader, &s_PBRSkinnedShader, &s_UnlitShader, &s_SkyboxShader, &s_TerrainShader })
            if (*sh)
                (*sh)->Reload();
        s->ShaderFrame.clear();
        s->FrameDirty = true;
    }

    void Renderer3D::CheckShaderHotReload()
    {
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration<float>(now - s->LastHotReloadCheck).count() < 1.0f)
            return;
        s->LastHotReloadCheck = now;
        bool reloaded = false;
        for (auto* sh : { &s_PBRShader, &s_PBRSkinnedShader, &s_UnlitShader, &s_SkyboxShader, &s_TerrainShader })
        {
            if (*sh && (*sh)->IsSourceModified())
            {
                (*sh)->Reload();
                reloaded = true;
            }
        }
        if (reloaded)
        {
            s->ShaderFrame.clear();
            s->FrameDirty = true;
        }
    }
}
