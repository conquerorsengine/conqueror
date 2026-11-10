#pragma once

#include "Core/Base/Base.h"
#include "Mesh.h"
#include "Material.h"
#include "Renderer/RHI/Shader.h"
#include "Renderer/RHI/Cubemap.h"
#include "Renderer/RenderSettings.h"
#include "Renderer/Probe/AdaptiveProbeVolume.h"
#include "Renderer/IBL/EnvironmentMap.h"
#include "Scene/EditorCamera.h"
#include "Scene/Components.h"
#include "Renderer/Shadow/ShadowPass.h"

#include <glm/glm.hpp>

#include <array>
#include <memory>
#include <vector>

namespace Conqueror
{
    class Scene;

    // Bir kare için kamera bilgisi (editör kamerası ya da oyun kamerası)
    struct RenderCamera
    {
        glm::mat4 View = glm::mat4(1.0f);
        glm::mat4 Projection = glm::mat4(1.0f);
        glm::vec3 Position = glm::vec3(0.0f);
        float NearClip = 0.1f;
        float FarClip = 1000.0f;
        uint32_t ViewportWidth = 1;
        uint32_t ViewportHeight = 1;
        bool Orthographic = false;

        static RenderCamera FromEditorCamera(const EditorCamera& camera, uint32_t width, uint32_t height);
        static RenderCamera FromSceneCamera(const SceneCamera& camera, const glm::mat4& worldTransform, uint32_t width, uint32_t height);
    };

    // Sahneden renderer'a aktarılan ortam verisi
    struct SceneEnvironment
    {
        int AmbientSource = 0;                  // 0 skybox, 1 renk, 2 gradyan
        glm::vec3 AmbientColor = glm::vec3(0.3f);
        glm::vec3 SkyColor = glm::vec3(0.5f, 0.6f, 0.7f);
        glm::vec3 EquatorColor = glm::vec3(0.3f);
        glm::vec3 GroundColor = glm::vec3(0.2f, 0.15f, 0.1f);
        float AmbientIntensity = 1.0f;

        std::shared_ptr<Cubemap> Skybox;
        bool UseProceduralSky = true;           // skybox yoksa prosedürel gökyüzü
        bool DrawSky = true;                    // false: gökyüzü çizilmez (aydınlatma korunur)
        float SkyboxExposure = 1.0f;
        float SkyboxRotation = 0.0f;            // derece
        glm::vec3 SkyboxTint = glm::vec3(1.0f);
        // Kameraya özel arka plan gökyüzü (Skybox bileşeni): yalnızca çizimde, IBL'de değil
        std::shared_ptr<Cubemap> BackgroundSkybox;
        float BackgroundExposure = 1.0f;
        glm::vec3 BackgroundTint = glm::vec3(1.0f);

        RenderSettings Settings;
        std::shared_ptr<Texture2D> Lightmap;
        std::shared_ptr<AdaptiveProbeVolume> APV;
        bool HDROutput = true;                  // false: shader içinde ton eşleme (eski yol)
        float Time = 0.0f;
    };

    // Terrain çizimi (splat + 4 katman)
    struct TerrainDrawParams
    {
        std::shared_ptr<Texture2D> Splat;
        std::array<std::shared_ptr<Texture2D>, 4> Layers;
        std::array<glm::vec4, 4> Tint{};    // rgb renk, a = doku var mı
        std::array<glm::vec4, 4> Params{};  // x tekrar, y pürüzlülük, z metaliklik
        glm::vec2 Size{ 100.0f };
        int LayerCount = 1;
        bool ReceiveShadows = true;
    };

    class CQ_API Renderer3D
    {
    public:
        static void Init();
        static void Shutdown();

        // ---- Yeni kare API'si
        static void BeginFrame(const RenderCamera& camera, const SceneEnvironment& environment);
        // Geçerli karenin çizim zamanı (malzeme animasyonları; düzenleyicide Realtime kapalıyken durur)
        static float GetFrameTime();
        static void RenderShadows(Scene* scene);
        static void DrawEnvironment();          // skybox / prosedürel gökyüzü
        static void EndScene();                 // şeffaf kuyruğu çizer, kareyi kapatır
        // Opak kuyruk: karede toplanan opak meshler kameraya yakından uzağa çizilir (erken derinlik testi arkadaki yüzeylerin
        // pahalı ışıklandırmasını atlar). Sahne, arazi / MultiMesh'ten önce çağırır; EndScene ve decal'ler de boşaltır.
        static void FlushOpaque();
        // r.OpaqueSort: 1 (varsayılan) önce opaklar, sonra alfa kesmeliler (yaprak, tel örgü: discard erken derinlik
        // testini kapatır), her grup yakından uzağa (Unity Geometry / AlphaTest kuyrukları); 0 geldikleri sırayla anında
        static void SetOpaqueSorting(int mode);
        static int GetOpaqueSorting();

        // ---- Eski API (gizmo geçişi ve eski kod için)
        static void BeginScene(EditorCamera& camera);
        static void BeginScene(const SceneCamera& camera, const glm::mat4& transform);

        // Işıklar
        static void SetDirectionalLight(const DirectionalLightComponent& light);
        static void ClearDirectionalLight();
        static void AddPointLight(const glm::vec3& position, const PointLightComponent& light);
        // Son eklenen ışığın Light Profile örnekleri (8 örnek, 2 vec4)
        static void SetLastPointLightProfile(const glm::vec4& p0, const glm::vec4& p1);
        static void SetLastSpotLightProfile(const glm::vec4& p0, const glm::vec4& p1);
        static void AddSpotLight(const glm::vec3& position, const SpotLightComponent& light);
        static void AddSpotLight(const glm::vec3& position, const glm::vec3& direction, const SpotLightComponent& light);
        // Dikdörtgen / disk alan ışığı (+Z = sağ x yukarı yönüne yayar); radiance = renk * yoğunluk
        static void AddAreaLight(const glm::vec3& center, const glm::vec3& halfRight, const glm::vec3& halfUp, const glm::vec3& radiance,
                                 float range, bool twoSided, bool disc);
        static void ClearLights();

        // Gölge (eski erişim)
        static ShadowPass& GetShadowPass() { return s_ShadowPass; }

        // Yansıma probları (kutu izdüşümlü)
        static void AddReflectionProbe(const glm::vec3& position, const glm::vec3& boxMin,
                                       const glm::vec3& boxMax, std::shared_ptr<Cubemap> cubemap,
                                       float intensity = 1.0f, float blendDistance = 1.0f);
        static void AddReflectionProbe(const glm::vec3& position, uint32_t cubemapID, uint32_t mipCount,
                                       const glm::vec3& boxMin, const glm::vec3& boxMax,
                                       float intensity = 1.0f, float blendDistance = 1.0f);
        static void ClearReflectionProbes();

        // Geriye uyumluluk (artık sahne ortamı üzerinden ayarlanır)
        static void SetLightmap(std::shared_ptr<Texture2D> lightmap);
        static void SetAdaptiveProbeVolume(std::shared_ptr<AdaptiveProbeVolume> apv);
        static std::shared_ptr<Texture2D> GetGlobalLightmap();
        static std::shared_ptr<AdaptiveProbeVolume> GetGlobalAPV();

        // Editör görünüm modları
        enum class DebugView : int { Shaded = 0, Unlit, Normals, LightingOnly, MetallicRoughness, AmbientOcclusion };
        static void SetDebugView(DebugView view);
        static DebugView GetDebugView();
        static void SetWireframe(bool wireframe);   // yalnızca tel kafes
        static bool GetWireframe();

        // Bir sonraki çizimlerin gölge alıp almayacağı (MeshRenderer.ReceiveShadows)
        static void SetReceiveShadows(bool receive);
        // Sonraki çizimler statik (lightmap'e pişirilmiş) mi: değilse ortam ışığını ışık problarından ya da gökyüzünden alır (Unity)
        static void SetDrawLightmapped(bool lightmapped);
        // Sonraki çizimlerin taban rengi çarpanı (Model.Tint; çizimden sonra 1'e döndürülür)
        static void SetTint(const glm::vec3& tint);
        // Mesh Paint: sonraki çizimlerde (model içi mesh sırasına göre) örneğe özel köşe rengi tamponları; nullptr = yok
        static void SetVertexColorBuffers(const std::vector<uint32_t>* buffers);

        // Çizim
        static void DrawMesh(const std::shared_ptr<Mesh>& mesh, const glm::mat4& transform, std::shared_ptr<Material> material, bool doubleSided = false);
        // MultiMesh (GPU örnekleme): örnek tamponu satır başına mat4 (yerel) + vec4 renk (80 bayt), tek çizim çağrısı.
        // Shader Graph materyali örnek özniteliklerini tanımaz: fallbackTransforms ile örnek başına çizilir.
        // firstInstance: tampondaki ilk örnek (görünür örnek kümeleri aralık aralık çizilir)
        static void DrawMeshInstanced(const std::shared_ptr<Mesh>& mesh, const glm::mat4& transform, uint32_t instanceBuffer,
                                      uint32_t instanceCount, const std::shared_ptr<Material>& material,
                                      const std::vector<glm::mat4>* fallbackTransforms = nullptr, bool doubleSided = false,
                                      uint32_t firstInstance = 0);
        // Örnek tamponunu meshin VAO'suna 8..12 özniteliklerine bağlar / çözer (gölge geçişi de kullanır); firstInstance
        // öznitelik ofsetiyle uygulanır (base instance gerektirmez: çeviri katmanlarında da çalışır)
        static void BindInstanceAttributes(const Mesh& mesh, uint32_t instanceBuffer, uint32_t firstInstance = 0);
        static void UnbindInstanceAttributes(const Mesh& mesh);

        // Çıkartmalar (Decal Projector): opak çizimden sonra çağrılır; derinlikten yüzeyi kurar,
        // birim kutunun (-1..1) içindeki yüzeylere -Z yönünde dokuyu yansıtır
        struct DecalDraw
        {
            glm::mat4 Transform{ 1.0f };
            std::shared_ptr<Texture2D> Texture;
            glm::vec4 Color{ 1.0f };
            glm::vec2 Tiling{ 1.0f }, Offset{ 0.0f };
            float AngleFadeCos = 0.17f;       // yüzey normali ile yansıtma ekseni arasındaki açı sınırı
            float Emission = 0.0f;
        };
        static void DrawDecals(const std::vector<DecalDraw>& decals);
        static void DrawCube(const glm::mat4& transform, std::shared_ptr<Material> material);
        static void DrawSphere(const glm::mat4& transform, std::shared_ptr<Material> material);
        static void DrawPlane(const glm::mat4& transform, std::shared_ptr<Material> material);
        static void DrawCylinder(const glm::mat4& transform, std::shared_ptr<Material> material);
        static void DrawModel(const glm::mat4& transform, std::shared_ptr<struct Model> model);
        static void DrawSkinnedModel(const glm::mat4& transform, std::shared_ptr<struct Model> model,
            const std::vector<glm::mat4>& boneMatrices);
        static void DrawTerrain(const std::shared_ptr<Mesh>& mesh, const glm::mat4& transform, const TerrainDrawParams& params);

        // Editör gizmo'ları (unlit)
        static void DrawLightGizmo(const glm::vec3& position, const glm::vec3& color, float size = 0.2f);
        // Editör simgesi (ampul, güneş, kamera...): kameraya dönük, sabit ekran boyunda, her şeyin üstünde (Unity gibi)
        static void DrawGizmoIcon(const std::string& iconPath, const glm::vec3& position, const glm::vec3& cameraRight, const glm::vec3& cameraUp,
                                  float worldSize, const glm::vec3& color, float alpha = 1.0f);
        // İşlenmiş son görüntünün üstüne (TAA / ton eşleme sonrası, sRGB çıktı) simge: sahne geçişinin dışında çağrılır
        static void DrawGizmoIconOverlay(const std::string& iconPath, const glm::vec3& position, const glm::vec3& cameraRight, const glm::vec3& cameraUp,
                                         float worldSize, const glm::vec3& color, float alpha, const glm::mat4& viewProjection);
        static void DrawSpotLightGizmo(const glm::vec3& position, const glm::vec3& direction, const glm::vec3& color, float size = 0.2f);
        static void DrawDirectionalLightGizmo(const glm::vec3& position, const glm::vec3& direction, const glm::vec3& color, float size = 0.3f);

        // Eski skybox çağrısı (BeginFrame ortamı kullanır; parametreler geçersiz kılar)
        static void DrawSkybox(std::shared_ptr<Cubemap> skybox, float exposure = 1.0f, float rotation = 0.0f, const glm::vec3& tint = glm::vec3(1.0f));

        // Mesh erişimi (gölge/bake geçişleri)
        static std::shared_ptr<Mesh> GetCubeMesh() { return s_CubeMesh; }
        static std::shared_ptr<Mesh> GetSphereMesh() { return s_SphereMesh; }
        static std::shared_ptr<Mesh> GetPlaneMesh() { return s_PlaneMesh; }
        static std::shared_ptr<Mesh> GetCylinderMesh() { return s_CylinderMesh; }

        // Sanal geometri (Nanite benzeri): kümeleri ekran uzayı hatasına göre seç ve çiz; çizilen üçgen sayısı
        static uint32_t DrawClusterMesh(const class ClusterMesh& clusters, const glm::mat4& transform, const std::shared_ptr<Material>& material,
                                        float thresholdPixels, bool debugColors = false, uint32_t* outClusterCount = nullptr);
        // Görüş-izdüşüm matrisinden 6 kesik koni düzlemi (nokta·xyz + w >= 0 içeride)
        static void ExtractFrustum(const glm::mat4& viewProjection, glm::vec4 planes[6]);
        // Görüş hacmi kırpma (Unreal / Unity görünürlük kırpması): kameranın kesik konisi dışındaki meshler çizilmez, gölge
        // kademeleri de kendi ışık hacimleriyle kırpar. r.FrustumCulling 0: her şey çizilir (hata ayıklama / ölçüm)
        // Temporal AA hareket maskesi: bu görünümde (PostProcessStack) son karede hareket eden nesnelerin ekran
        // dikdörtgenleri (uv: minx, miny, maxx, maxy), önceki karenin dikdörtgenleriyle birlikte
        static void GetMovingRects(const void* view, std::vector<glm::vec4>& out);
        // Hareket eden opak nesnelerin / animasyonlu modellerin dünya kutuları (bu + önceki kare; çift: min, max; gölge yumuşaklığı
        // payıyla büyütülmüş) ve güneşe doğru yön. TAA pikselin dünya noktasından güneşe ışın atar: kutulardan birine çarpıyorsa
        // nokta hareket eden bir nesnenin gölge hacmindedir. Güneş yoksa false
        static bool GetMovingShadowBoxes(const void* view, std::vector<glm::vec4>& minMax, glm::vec3& towardSun);
        // Temporal AA nesne hareket vektörleri: bu görünümde bu karede hareket eden opak meshleri / animasyonlu modelleri
        // bağlı çerçeve tamponuna (renk: RGBA16F hız, derinlik: sahne derinliği; derinlik yazılmaz) çizer. Çizim yoksa false
        static bool RenderVelocity(const void* view, const glm::mat4& jitteredViewProj, const glm::mat4& viewProj, const glm::mat4& prevViewProj);
        static bool HasVelocityDraws(const void* view);
        // GBAA / SRAA: bu görünümün bu karedeki opak (Opaque kipli) mesh çizimlerini bağlı çerçeve tamponuna yeniden çizer.
        // GBAAEdges: RG16F kenar uzaklığı (sahne derinliğine LEQUAL, derinlik yazılmaz); SRAANormalDepth: çok örnekli normal +
        // derinlik (derinlik yazılır). Çizim yoksa false
        enum class GeometryPass { GBAAEdges, SRAANormalDepth };
        static bool RenderGeometry(const void* view, GeometryPass pass, const glm::mat4& viewProj, const glm::vec2& viewportSize);
        // Bu görünümde nokta / spot ışıkların toplam gücü önceki çağrıdan beri değişti mi (namlu alevi ışığı yanıp söner):
        // TAA yalnız böyle karelerde aydınlığı değişen piksellerde geçmişe az güvenir
        static bool ConsumeLightingChange(const void* view);
        // Sonraki çizimlerin nesnesi gölge düşürüyor mu (Mesh Renderer / Model Cast Shadows): düşürmeyenin gölge kutusu yok
        static void SetDrawCastsShadows(bool castsShadows);
        // Sonraki çizimler View Model mi (first person kollar / silah): kameraya doğru bu oranda yaklaştırılmış konumla
        // rasterleştirilir (1 = kapalı)
        // Mesh dışı çizimler (parçacıklar vb.) için: bu dünya kutusu bu karede hareketli (TAA iz bırakmasın)
        static void MarkMovingBounds(const glm::vec3& center, const glm::vec3& extents);
        // TAA hareket takibi: sonraki çizimlerin kararlı nesne kimliği (varlığın UUID'si). 0 = kimlik yok (mesh + çizim sırası)
        static void SetDrawIdentity(uint64_t identity);
        static void SetFrustumCulling(bool enabled);
        static bool GetFrustumCulling();
        // Dünya uzayı eksen hizalı kutu (merkez, yarı boyut) düzlemlerin içinde ya da onları kesiyor mu
        static bool BoxInFrustum(const glm::vec4 planes[6], const glm::vec3& center, const glm::vec3& extents);
        // Yerel kutuyu dönüşümden sonra saran dünya kutusu (eğme ve eşit olmayan ölçekte de tam)
        static void TransformBox(const glm::mat4& transform, const glm::vec3& center, const glm::vec3& extents,
                                 glm::vec3& outCenter, glm::vec3& outExtents);
        // İskeletli modelin bu kemik paletindeki pozunu saran yerel kutu (kemik başına bağlama kutularından, tutucu).
        // Kemik i < boneCount: bones[i]; boneCount <= i < palette: birim matris; palette dışı kemik kullanılıyorsa false
        static bool SkinnedModelBounds(const struct Model& model, const glm::mat4* bones, uint32_t boneCount, uint32_t palette,
                                       glm::vec3& outCenter, glm::vec3& outExtents);

        // Otomatik LOD: mesh'in bu dönüşümde çizilecek seviyesi (kamera uzaklığı / ekran boyutu, kalite LOD Bias)
        static int SelectMeshLOD(const Mesh& mesh, const glm::mat4& transform);
        // r.ForceLOD: >= 0 tüm LOD'lu meshlerde bu seviye (hata ayıklama)
        static void SetForcedLOD(int level);
        static int GetForcedLOD();

        static std::shared_ptr<Shader> GetPBRShader() { return s_PBRShader; }
        static const SceneEnvironment& GetEnvironment();
        static const RenderCamera& GetCamera();
        static std::shared_ptr<EnvironmentMap> GetActiveEnvironmentMap();

        // Planar Reflection: aynalanmış sahne dokusu ve düzlem (n, d); params x açık, y düzleme uzaklık sınırı (m), z bozulma, w en çok pürüzlülük.
        // Dokuyu çizerken kapatılır (kendini yansıtmasın)
        static void SetPlanarReflection(uint32_t texture, const glm::vec4& plane, const glm::vec4& params);
        // Özel çizimler (su, ...): kare verisi (kamera, güneş, ortam, sis) ve IBL / gölge / prob dokuları bu shader'a bağlanır
        static void PrepareCustomShader(const std::shared_ptr<Shader>& shader);

        // Editör: shader dosyaları değiştiyse yeniden derle
        static void ReloadShaders();
        static void CheckShaderHotReload();

    private:
        static void UploadFrameData();
        static void BindFrameResources(const std::shared_ptr<Shader>& shader);
        static void UpdateEnvironmentMap();
        static void DrawMeshInternal(const std::shared_ptr<Mesh>& mesh, const glm::mat4& transform,
                                     const std::shared_ptr<Material>& material, bool doubleSided,
                                     bool fromQueue);
        static void ApplyCulling(const std::shared_ptr<Material>& material, bool doubleSided);

        static ShadowPass s_ShadowPass;
        static std::shared_ptr<Mesh> s_CubeMesh;
        static std::shared_ptr<Mesh> s_PlaneMesh;
        static std::shared_ptr<Mesh> s_CylinderMesh;
        static std::shared_ptr<Mesh> s_SphereMesh;
        static std::shared_ptr<Mesh> s_ConeMesh;
        static std::shared_ptr<Mesh> s_ArrowMesh;
        static std::shared_ptr<Shader> s_PBRShader;
        static std::shared_ptr<Shader> s_PBRSkinnedShader;
        static std::shared_ptr<Shader> s_UnlitShader;
        static std::shared_ptr<Shader> s_SkyboxShader;
        static std::shared_ptr<Shader> s_TerrainShader;
    };
}
