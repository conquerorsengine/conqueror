#pragma once

#include "Core/Base/Base.h"
#include "Core/Time/Timestep.h"
#include "AISystem/Navigation/NavMesh.h"
#include "Renderer/RenderSettings.h"

#include <glm/glm.hpp>
#include <entt/entt.hpp>

#include <unordered_map>
#include <unordered_set>

namespace Conqueror::CQS
{
    class Value;
}

namespace Conqueror::Scripting
{
    class ScriptRuntime;
}

namespace Conqueror
{
    class Entity;
    class EditorCamera;
    class Cubemap;
    struct IDComponent;
    struct TransformComponent;
    struct TagComponent;
    struct ActiveComponent;
    struct CameraComponent;
    struct SpriteRendererComponent;
    struct MeshRendererComponent;
    struct RigidBody2DComponent;
    struct BoxCollider2DComponent;
    struct CircleCollider2DComponent;
    struct RigidbodyComponent;
    struct BoxColliderComponent;
    struct SphereColliderComponent;
    struct CapsuleColliderComponent;
    struct MeshColliderComponent;
    struct TextRendererComponent;
    struct LayerComponent;
    struct ButtonComponent;
    struct CanvasComponent;
    struct CanvasScalerComponent;
    struct GraphicRaycasterComponent;
    struct AnimatorComponent;
    struct AnimationComponent;
    struct DirectionalLightComponent;
    struct PointLightComponent;
    struct SpotLightComponent;
    struct NativeScriptComponent;
    struct ConquerorScriptComponent;
    struct AudioSourceComponent;
    struct AudioListenerComponent;
    struct AudioChorusFilterComponent;
    struct AudioDistortionFilterComponent;
    struct AudioEchoFilterComponent;
    struct AudioHighPassFilterComponent;
    struct AudioLowPassFilterComponent;
    struct AudioReverbFilterComponent;
    struct AudioReverbZoneComponent;
    struct AudioGainComponent;
    struct AudioPanComponent;
    struct NavMeshAgentComponent;
    struct NavMeshObstacleComponent;
    struct NavMeshSurfaceComponent;
    struct ReflectionProbeComponent;
    struct AdaptiveProbeVolumeComponent;
    struct TerrainComponent;
    struct ParticleSystemComponent;
    struct CharacterControllerComponent; struct FixedJointComponent; struct HingeJointComponent; struct SliderJointComponent;
    struct SpringJointComponent; struct CharacterJointComponent; struct BallJointComponent; struct ConstantForceComponent;
    struct CapsuleCollider2DComponent; struct PolygonCollider2DComponent; struct EdgeCollider2DComponent; struct DistanceJoint2DComponent;
    struct SpringJoint2DComponent; struct HingeJoint2DComponent; struct SliderJoint2DComponent; struct WheelJoint2DComponent;
    struct FixedJoint2DComponent; struct PlatformEffector2DComponent; struct AreaEffector2DComponent; struct PointEffector2DComponent; struct CompositeCollider2DComponent; struct ClothComponent;
    struct SurfaceEffector2DComponent; struct BuoyancyEffector2DComponent; struct WheelColliderComponent; struct ConfigurableJointComponent;
    struct FrictionJoint2DComponent; struct RelativeJoint2DComponent; struct TargetJoint2DComponent; struct ConstantForce2DComponent;
    struct LineRendererComponent; struct TrailRendererComponent; struct LODGroupComponent; struct LookAtConstraintComponent;
    struct AimConstraintComponent; struct PositionConstraintComponent; struct RotationConstraintComponent; struct ScaleConstraintComponent;
    struct ParentConstraintComponent; struct WindZoneComponent; struct CameraFollowComponent; struct RotatorComponent;
    struct ParticleForceFieldComponent; struct SplineContainerComponent; struct SplineAnimateComponent; struct SplineExtrudeComponent; struct SplineInstantiateComponent;
    struct VirtualCameraComponent; struct CameraBrainComponent; struct ImpulseSourceComponent; struct PostProcessVolumeComponent; struct DecalProjectorComponent;
    struct Light2DComponent; struct ShadowCaster2DComponent; struct SpriteMaskComponent; struct PixelPerfectCameraComponent;
    struct BillboardRendererComponent; struct HaloComponent; struct LensFlareComponent; struct CameraSkyboxComponent; struct AreaLightComponent;
    struct PlayerInputComponent; struct PlayerInputManagerComponent;
    struct NavMeshModifierComponent; struct NavMeshModifierVolumeComponent; struct NavMeshLinkComponent;
    struct VideoPlayerComponent; struct FullScreenPassComponent;
    struct GameplayTagsComponent; struct AbilitySystemComponent;
    struct PlayerStartComponent; struct GameModeComponent; struct SpringArmComponent; struct BoneAttachmentComponent;
    struct StreamedLevelComponent; struct LevelStreamingVolumeComponent; struct AnimationPlayerComponent;
    struct AutoLODComponent;
    struct SoundOcclusionComponent; struct SoundConcurrencyComponent;
    struct VertexPaintComponent; struct FractureComponent; struct RagdollComponent; struct VirtualGeometryComponent; struct CharacterCustomizerComponent; struct SignalConnectionsComponent; struct SignalReceiverComponent; struct CSGShapeComponent; struct MultiMeshComponent; struct RemoteTransformComponent; struct RayCastComponent; struct VisibleOnScreenNotifierComponent; struct Parallax2DComponent; struct TimerComponent; struct ShapeCastComponent; struct VisibleOnScreenEnablerComponent; struct CharacterBody2DComponent; struct FogVolumeComponent; struct NavigationRegion2DComponent; struct NavigationAgent2DComponent; struct NavigationObstacle2DComponent; struct NavigationLink2DComponent; struct RenderTargetComponent; struct Bone2DComponent; struct IK2DComponent;
    struct RigComponent; struct TwoBoneIKConstraintComponent; struct MultiAimConstraintComponent; struct ChainIKConstraintComponent; struct DampedTransformComponent;
    struct ProjectileMovementComponent; struct SortingGroupComponent; struct PrefabInstanceComponent;
    struct GridComponent; struct TilemapComponent; struct TilemapCollider2DComponent;
    struct NetworkManagerComponent; struct NetworkObjectComponent; struct NetworkTransformComponent; struct NetworkRigidbodyComponent; struct NetworkAnimatorComponent;
    struct PlayableDirectorComponent;
    struct RectTransformComponent; struct UIImageComponent; struct UITextComponent; struct UIButtonComponent; struct UIToggleComponent;
    struct UISliderComponent; struct UIInputFieldComponent; struct UIDropdownComponent; struct UIScrollRectComponent; struct UILayoutGroupComponent;
    struct UICanvasGroupComponent; struct UIRectMask2DComponent; struct UIMaskComponent; struct UIRawImageComponent; struct UIContentSizeFitterComponent;
    struct UIAspectRatioFitterComponent; struct UILayoutElementComponent; struct UIScrollbarComponent; struct UIEventTriggerComponent; struct UIEventSystemComponent; struct UIShadowComponent; struct UIOutlineComponent; struct UIPositionAsUV1Component;

    class PhysicsWorld2D;
    class PhysicsWorld3D;
    class AdaptiveProbeVolume;
    struct OcclusionData;
    struct RenderCamera;
    struct SceneEnvironment;

    // Editör (Scene) görünümüne özel çizim seçenekleri; oyun görünümünü etkilemez.
    struct EditorViewOptions
    {
        bool Grid = true;
        bool Gizmos = true;        // ışık/kamera simgeleri
        bool Skybox = true;
        bool Fog = true;
        bool Shadows = true;
        bool PostEffects = true;
        bool Sprites2D = true;     // sprite/görsel/yazı
        bool UI = true;            // ekran üstü canvas'lar (sahne görünümü)
        bool Colliders = true;     // seçili varlıkların çarpıştırıcı/eklem/karakter gizmo'ları
        bool AllColliders = false; // tüm çarpıştırıcılar (fizik hata ayıklama)
        // Unreal "Realtime" / Unity "Always Refresh": tüm parçacıklar ve zamana bağlı malzemeler düzenleyicide canlı.
        // Kapalıyken (varsayılan) yalnızca seçili parçacık sistemi önizlenir, malzeme zamanı durur.
        bool Realtime = false;
    };

    class CQ_API Scene
    {
    public:
        Scene();
        ~Scene();

        Entity CreateEntity(const std::string& name = std::string());
        Entity CreateEntityWithUUID(uint64_t uuid, const std::string& name = std::string());
        void DestroyEntity(Entity entity);
        // Alt ağacı (çocuklarla) kopyalar; kopya kaynağın ebeveyni altında kardeş olur, EntityRef alanları kopyaya eşlenir
        Entity DuplicateEntity(Entity source, const std::string& newName = "");
        // Tüm bileşenleri kopyala (sahneler arası da çalışır; çalışma zamanı tutamaçları kopyalanmaz)
        void CopyEntityComponents(Entity source, Entity destination, bool shareRuntimeResources = false);
        // Alt ağacı bu sahneye kopyala (isim korunur, newName yalnızca kök için); uuidMap: eski -> yeni UUID
        Entity DuplicateEntityTree(Entity source, const std::string& newName, Entity parent, std::unordered_map<uint64_t, uint64_t>* uuidMap = nullptr);
        // Alt ağaçtaki EntityRef alanlarını eşleme tablosuna göre yeniden yönlendir
        void RemapEntityReferences(Entity root, const std::unordered_map<uint64_t, uint64_t>& uuidMap);
        // Oyun sırasında oluşturulan alt ağaç: fizik gövdelerini kurulmak üzere işaretle
        void OnEntityTreeSpawned(Entity root);
        // Tüm varlıkları sil (sahne anlık görüntüsünü geri yüklemeden önce; ortam ayarları korunur)
        void ClearEntities();
        // reference'ta olmayan bileşenleri target'tan kaldır (anlık görüntüden geri yükleme: eklenen bileşenler geri alınır)
        void RemoveComponentsNotIn(Entity reference, Entity target);
        void CloneSceneFrom(const std::shared_ptr<Scene>& source);

        void OnUpdateRuntime(Timestep ts, bool renderOnly = false);
        // Oyun modu başlangıç/bitiş (betik VM'i, OnDestroy)
        // newPlaySession=false: oyun içi sahne geçişi (betik çalışma zamanları sıfırlanmaz, statik durum korunur)
        void OnRuntimeStart(bool newPlaySession = true);
        void OnRuntimeStop(bool endPlaySession = true);
        // Herhangi bir bileşen kaldırıldığında (fizik yeniden değerlendirir)
        void OnComponentRemovedAny(Entity entity);
        // Bir varlığın tüm betik örneklerinde yöntemi çağır (yoksa yok sayılır)
        void InvokeScriptMethod(uint32_t entity, const char* method, const CQS::Value* args, int argc);
        // Etkin betik örnekleri, Project Settings > Script Execution Order sırasıyla
        void CollectScriptInstances(std::vector<std::pair<Scripting::ScriptRuntime*, void*>>& instances);
        // Unity OnMouseEnter/Over/Exit/Down/Drag/Up/UpAsButton: oyun kamerasından fare ışınıyla çarpıştırıcılı varlıklara
        void UpdateMouseEvents();
        uint32_t GetMouseHoverEntity() const { return m_MouseHoverEntity; }
        void OnUpdateSimulation(Timestep ts);
        void OnUpdateEditor(Timestep ts, EditorCamera& camera);
        void OnUpdateEditor(Timestep ts, EditorCamera& camera, const glm::vec2* viewportBounds);
        // Düzenleyicide çalışan sistemler (dönüşüm hiyerarşisi, kısıtlar, eğriler, rüzgâr, parçacık önizlemesi, ses): sahne
        // görünümü çizilmese de (Game sekmesi öndeyken) her karede
        void UpdateEditorSystems(Timestep ts);
        // Sahne görünümünün çizimi (OnUpdateEditor'ın çizim kısmı)
        void RenderEditorView(Timestep ts, EditorCamera& camera, const glm::vec2* viewportBounds);
        // Işık / kamera simgeleri işlenmiş son görüntünün üstüne (viewport, post-process sonrası)
        void RenderEditorIcons(EditorCamera& camera, const glm::vec2* viewportBounds);
        // Editör seçimi (çarpıştırıcı gizmo'ları için)
        void SetEditorSelection(std::vector<uint32_t> selection) { m_EditorSelection = std::move(selection); }
        void DrawPhysicsGizmos();
        
        void StopAllAudio();

        void OnViewportResize(uint32_t width, uint32_t height);

        // 3D sahneyi verilen kameradan çizer (gölge, gökyüzü, ışıklar, meshler, şeffaflar).
        // editorView: editör kamerası (gizmo'lar ayrıca çizilir)
        void RenderScene3D(const RenderCamera& camera, bool editorView);
        // Render hedefi kamerası: 3D sahneyi o kameradan (arka planı dahil) bağlı çerçeve tamponuna çizer
        void RenderSceneFromCamera(entt::entity cameraEntity, uint32_t width, uint32_t height);
        // Sahnenin ortam ayarlarından renderer ortamını oluşturur
        SceneEnvironment BuildRenderEnvironment(bool hdrOutput) const;

        // Oyun görünümünün arka planı (Unity Camera "Background Type"): gökyüzü mü, düz renk mi.
        // Color sRGB'dir. Ortam ayarı "Solid Color" ise her kamerayı geçersiz kılar.
        struct CameraBackground { bool DrawSky = true; glm::vec3 Color{ 0.1f }; };
        CameraBackground ResolveCameraBackground(Entity cameraEntity);
        // Editör: 2D görünümde gökyüzü çizilmez (Unity Scene görünümü gibi düz arka plan)
        static glm::vec3 EditorBackground2D() { return glm::vec3(0.278f); }
        // Yansıma problarını bir sonraki karede yeniden yakala
        void InvalidateReflectionProbes();
        // Birincil oyun kamerasından render kamerası (yoksa false)
        bool GetRuntimeRenderCamera(uint32_t width, uint32_t height, RenderCamera& out);
        bool HasRenderable3D() const;
        // Tüm dünya matrislerini hiyerarşiden yeniden hesapla
        void UpdateWorldTransforms();
        // Işığın dünya ileri yönü (-Z)
        static glm::vec3 LightForward(const glm::mat4& world);
        void UpdateParticles(float dt, bool editorPreviewOnly);
        // Oyun modu kopyası mı (CloneSceneFrom ile oluşturulan çalışma zamanı sahnesi)
        bool IsRuntimeScene() const { return m_IsRuntimeScene; }
        // Yerel dönüşüm zincirinden taze dünya matrisi (önbellek beklemeden)
        glm::mat4 ComputeWorldMatrix(entt::entity entity) const;
        // Dünya konumu (ve isteğe bağlı dönüşü) ebeveyne göre yerel dönüşüme çevirerek yazar; ölçek korunur
        void SetEntityWorldPose(Entity entity, const glm::vec3& position, const glm::quat* rotation = nullptr);
        void SetRuntimeScene(bool runtime) { m_IsRuntimeScene = runtime; }
        // UI canvas'larının editörde/oyunda kullandığı ekran boyutu (Game görünümü çözünürlüğü)
        void SetUIScreenSize(const glm::vec2& size) { m_UIScreenSize = glm::max(size, glm::vec2(1.0f)); }
        const glm::vec2& GetUIScreenSize() const { return m_UIScreenSize; }

        void SetAdaptiveProbeVolume(std::shared_ptr<AdaptiveProbeVolume> apv) { m_APV = apv; }
        // Pişirilmiş Occlusion Culling verisi (sahneyle kaydedilir)
        void SetOcclusionData(std::shared_ptr<OcclusionData> data) { m_OcclusionData = std::move(data); }
        std::shared_ptr<OcclusionData> GetOcclusionData() const { return m_OcclusionData; }
        // Düzenleyici sahne görünümünde de ayıkla (Occlusion Culling penceresi: Visualize)
        void SetOcclusionVisualize(bool on) { m_OcclusionVisualize = on; }
        bool GetOcclusionVisualize() const { return m_OcclusionVisualize; }
        size_t GetOcclusionHiddenCount() const { return m_OcclusionHidden.size(); }
        std::shared_ptr<AdaptiveProbeVolume> GetAdaptiveProbeVolume() const { return m_APV; }

        Entity GetPrimaryCameraEntity();

        // Tag sistemi
        Entity FindEntityByTag(const std::string& tag);
        std::vector<Entity> FindEntitiesWithTag(const std::string& tag);
        Entity FindEntityByUUID(uint64_t uuid);
        static const std::vector<std::string>& GetAvailableTags();
        // Proje etiketleri: yerleşikler (Untagged, Player, ...) + Project Settings > Tags & Layers'ta eklenenler
        static void SetCustomTags(const std::vector<std::string>& custom);
        static size_t GetBuiltinTagCount();

        // Layer sistemi
        struct LayerInfo
        {
            int32_t ID;
            std::string Name;
        };

        int32_t AddLayer(const std::string& name);
        void RemoveLayer(int32_t layerID);
        void RenameLayer(int32_t layerID, const std::string& newName);
        const std::vector<LayerInfo>& GetLayers() const { return m_Layers; }
        std::string GetLayerName(int32_t layerID) const;
        int32_t GetNextLayerID() const;

        // Hierarchy sistemi
        // keepWorldTransform: çocuğun dünya konumunu koru (düzenleyicide sürükle-bırak). false: yerel değerler
        // zaten yeni ebeveyne göredir (sahne yükleme, oyun modu kopyası)
        void SetEntityParent(Entity entity, Entity parent, bool keepWorldTransform = true);
        void RemoveEntityParent(Entity entity);
        Entity GetEntityParent(Entity entity) const;
        std::vector<Entity> GetEntityChildren(Entity entity) const;
        bool HasChildren(Entity entity) const;
        // Visibility (Inspector eye toggle): visible when the component is absent
        bool IsEntityActive(entt::entity entity) const;
        // Godot process_mode = DISABLED (Visible On Screen Enabler): nesne ve çocukları çizilir ama işlenmez (betiklerin Update /
        // FixedUpdate'i, Animator, parçacıklar, Timer)
        void SetProcessDisabled(uint32_t entity, bool disabled);
        bool IsProcessDisabled(entt::entity entity) const;
        // Unity DontDestroyOnLoad / Godot Autoload: oyun içi sahne değişiminde (LoadScene) yeni sahneye alt ağacıyla taşınan kök nesne
        // (yalnız kök nesneler, Unity gibi). Betik örnekleri (alan değerleri), çalan sesler ve animasyon durumu korunur
        bool SetPersistent(Entity entity, bool persistent = true);
        bool IsPersistent(entt::entity entity) const;
        // Yeni ve henüz yüklenmemiş sahne (Deserialize'dan önce): önceki sahnenin kalıcı nesnelerini aynı tanıtıcılarla ve UUID'lerle,
        // betik örnekleri ve çalan sesleriyle alır (yükleme başarılıysa FinishAdoption). Taşınan sayısı
        int AdoptPersistent(Scene& previous);
        // Yükleme başarılı: önceki sahnedeki kopyaların betik / ses bağlarını çözer ve UUID çakışmalarını giderir (Deserialize'dan sonra)
        void FinishAdoption(Scene& previous);
        // Taşınan nesneyle aynı UUID'yi taşıyan sahne nesnesi (aynı sahne yeniden yüklendi) yeni UUID alır
        void ResolvePersistentDuplicates();

        // Environment
        void SetSkybox(std::shared_ptr<Cubemap> skybox) { m_Skybox = skybox; }
        std::shared_ptr<Cubemap> GetSkybox() const { return m_Skybox; }
        
        void SetSkyboxExposure(float exposure) { m_SkyboxExposure = exposure; }
        float GetSkyboxExposure() const { return m_SkyboxExposure; }
        
        void SetSkyboxRotation(float rotation) { m_SkyboxRotation = rotation; }
        float GetSkyboxRotation() const { return m_SkyboxRotation; }
        
        void SetSkyboxTint(const glm::vec3& tint) { m_SkyboxTint = tint; }
        glm::vec3 GetSkyboxTint() const { return m_SkyboxTint; }
        
        // Ambient Light
        enum class EnvironmentLightingSource { Skybox = 0, Color, Gradient };
        
        // Environment Reflections
        enum class ReflectionSource { Skybox = 0, Custom };

        void SetEnvironmentLightingSource(EnvironmentLightingSource source) { m_EnvironmentLightingSource = source; }
        EnvironmentLightingSource GetEnvironmentLightingSource() const { return m_EnvironmentLightingSource; }
        
        void SetAmbientColor(const glm::vec3& color) { m_AmbientColor = color; }
        glm::vec3 GetAmbientColor() const { return m_AmbientColor; }
        
        void SetAmbientSkyColor(const glm::vec3& color) { m_AmbientSkyColor = color; }
        glm::vec3 GetAmbientSkyColor() const { return m_AmbientSkyColor; }
        
        void SetAmbientEquatorColor(const glm::vec3& color) { m_AmbientEquatorColor = color; }
        glm::vec3 GetAmbientEquatorColor() const { return m_AmbientEquatorColor; }
        
        void SetAmbientGroundColor(const glm::vec3& color) { m_AmbientGroundColor = color; }
        glm::vec3 GetAmbientGroundColor() const { return m_AmbientGroundColor; }
        
        void SetAmbientIntensity(float intensity) { m_AmbientIntensity = intensity; }
        float GetAmbientIntensity() const { return m_AmbientIntensity; }
        
        // Render ayarları (gölge, sis, post-process, ortam)
        RenderSettings& GetRenderSettings() { return m_RenderSettings; }
        const RenderSettings& GetRenderSettings() const { return m_RenderSettings; }
        // Editör görünümü seçenekleri (her OnUpdateEditor çağrısından önce ayarlanır)
        void SetEditorViewOptions(const EditorViewOptions& options) { m_EditorViewOptions = options; }
        const EditorViewOptions& GetEditorViewOptions() const { return m_EditorViewOptions; }

        // Fog (RenderSettings.Fog'a yönlendirilir)
        void SetFogEnabled(bool enabled) { m_RenderSettings.Fog.Enabled = enabled; }
        bool IsFogEnabled() const { return m_RenderSettings.Fog.Enabled; }
        
        void SetFogColor(const glm::vec3& color) { m_RenderSettings.Fog.Color = color; }
        glm::vec3 GetFogColor() const { return m_RenderSettings.Fog.Color; }
        
        void SetFogDensity(float density) { m_RenderSettings.Fog.Density = density; }
        float GetFogDensity() const { return m_RenderSettings.Fog.Density; }
        
        void SetFogStart(float start) { m_RenderSettings.Fog.Start = start; }
        float GetFogStart() const { return m_RenderSettings.Fog.Start; }
        
        void SetFogEnd(float end) { m_RenderSettings.Fog.End = end; }
        float GetFogEnd() const { return m_RenderSettings.Fog.End; }
        
        // Sun Source
        void SetSunSource(uint64_t uuid) { m_SunSourceUUID = uuid; }
        uint64_t GetSunSource() const { return m_SunSourceUUID; }
        Entity GetSunSourceEntity();
        
        // Halo/Flare
        void SetHaloEnabled(bool enabled) { m_HaloEnabled = enabled; }
        bool IsHaloEnabled() const { return m_HaloEnabled; }
        
        void SetHaloTexture(std::shared_ptr<class Texture2D> texture) { m_HaloTexture = texture; }
        std::shared_ptr<class Texture2D> GetHaloTexture() const { return m_HaloTexture; }
        
        void SetHaloStrength(float strength) { m_HaloStrength = strength; }
        float GetHaloStrength() const { return m_HaloStrength; }
        
        void SetFlareEnabled(bool enabled) { m_FlareEnabled = enabled; }
        bool IsFlareEnabled() const { return m_FlareEnabled; }
        
        void SetFlareFadeSpeed(float speed) { m_FlareFadeSpeed = speed; }
        float GetFlareFadeSpeed() const { return m_FlareFadeSpeed; }
        
        void SetFlareStrength(float strength) { m_FlareStrength = strength; }
        float GetFlareStrength() const { return m_FlareStrength; }
        
        // Flare Elements
        struct FlareElement
        {
            glm::vec3 ColorMultiplier = glm::vec3(1.0f);
            float Size = 0.08f;
            float Offset = 0.3f; // 0=light, 1=center
            float Intensity = 1.0f;
        };
        
        void SetFlareElementCount(int count);
        int GetFlareElementCount() const { return (int)m_FlareElements.size(); }
        
        const std::vector<FlareElement>& GetFlareElements() const { return m_FlareElements; }
        void SetFlareElement(int index, const FlareElement& element);
        FlareElement GetFlareElement(int index) const;
        
        void SetSpotCookie(std::shared_ptr<class Texture2D> texture) { m_SpotCookie = texture; }
        std::shared_ptr<class Texture2D> GetSpotCookie() const { return m_SpotCookie; }

        // Shadow settings
        void SetShadowEnabled(bool enabled) { m_RenderSettings.Shadows.Enabled = enabled; }
        bool IsShadowEnabled() const { return m_RenderSettings.Shadows.Enabled; }
        
        void SetShadowIntensity(float intensity) { m_RenderSettings.Shadows.Strength = intensity; }
        float GetShadowIntensity() const { return m_RenderSettings.Shadows.Strength; }

        // Environment Reflections
        void SetReflectionSource(ReflectionSource source) { m_ReflectionSource = source; }
        ReflectionSource GetReflectionSource() const { return m_ReflectionSource; }
        
        void SetReflectionResolution(int resolution) { m_ReflectionResolution = resolution; }
        int GetReflectionResolution() const { return m_ReflectionResolution; }
        
        void SetReflectionCompression(int compression) { m_ReflectionCompression = compression; }
        int GetReflectionCompression() const { return m_ReflectionCompression; }
        
        void SetReflectionIntensityMultiplier(float multiplier) { m_RenderSettings.Environment.ReflectionIntensity = multiplier; }
        float GetReflectionIntensityMultiplier() const { return m_RenderSettings.Environment.ReflectionIntensity; }
        
        void SetReflectionBounces(int bounces) { m_ReflectionBounces = bounces; }
        int GetReflectionBounces() const { return m_ReflectionBounces; }

        // Baked Lightmap
        void SetBakedLightmapPath(const std::string& path) { m_BakedLightmapPath = path; }
        const std::string& GetBakedLightmapPath() const { return m_BakedLightmapPath; }
        void SetBakedLightmapTexture(std::shared_ptr<class Texture2D> tex) { m_BakedLightmapTexture = tex; }
        std::shared_ptr<class Texture2D> GetBakedLightmapTexture() const { return m_BakedLightmapTexture; }
        void SetLightmapResolution(int r) { m_LightmapResolution = r; }
        int GetLightmapResolution() const { return m_LightmapResolution; }
        void SetMaxLightmapSize(int s) { m_MaxLightmapSize = s; }
        int GetMaxLightmapSize() const { return m_MaxLightmapSize; }
        void SetMaxBounces(int b) { m_MaxBounces = b; }
        int GetMaxBounces() const { return m_MaxBounces; }
        void SetDirectSamples(int s) { m_DirectSamples = s; }
        int GetDirectSamples() const { return m_DirectSamples; }
        void SetIndirectSamples(int s) { m_IndirectSamples = s; }
        int GetIndirectSamples() const { return m_IndirectSamples; }
        void SetAmbientOcclusion(bool ao) { m_AmbientOcclusion = ao; }
        bool IsAmbientOcclusion() const { return m_AmbientOcclusion; }
        void SetFiltering(int f) { m_Filtering = f; }
        int GetFiltering() const { return m_Filtering; }

        // Physics
        PhysicsWorld2D* GetPhysicsWorld2D() const { return m_PhysicsWorld2D.get(); }
        PhysicsWorld3D* GetPhysicsWorld3D() const { return m_PhysicsWorld3D.get(); }

        // AI Navigation
        NavMesh& GetNavMesh() { return m_NavMesh; }
        // Editör: pişmiş gezinme ağını sahne görünümünde göster (Navigation penceresi açıkken)
        void SetShowNavMesh(bool show) { m_ShowNavMesh = show; }
        bool IsShowingNavMesh() const { return m_ShowNavMesh; }

    private:
        template<typename T>
        void OnComponentAdded(Entity entity, T& component);

        void UpdateTransformHierarchy(Entity entity, const glm::mat4& parentTransform);
        void UpdateButtonInput(class EditorCamera& camera, Timestep ts, const glm::vec2* viewportBounds);
        Entity FindNextButton(bool up, bool down, bool left, bool right);
        bool IsCanvasOrCanvasChild(Entity entity) const;
        void UpdateAudioSystem(Timestep ts);
        void UpdateAnimators(Timestep ts);
        // Editörde (oynamıyorken) animasyonlu karakterlerin çizim pozu: bekleme durumu (Unreal gibi, T-poz yerine)
        void UpdateEditorAnimationPreview(float dt);

    public:
        entt::registry m_Registry;
        std::unordered_set<uint32_t> m_ProcessDisabled;   // SetProcessDisabled
        std::unordered_set<entt::entity> m_Persistent;   // SetPersistent (kök nesneler)
        std::vector<entt::entity> m_Adopted;             // AdoptPersistent ile alınan, FinishAdoption bekleyen
        uint32_t m_ViewportWidth = 0, m_ViewportHeight = 0;

        // Viewport bounds for scripts (set by editor)
        glm::vec2 m_ViewportBoundsMin = { 0.0f, 0.0f };
        glm::vec2 m_ViewportBoundsMax = { 0.0f, 0.0f };
        bool m_ViewportHovered = false;
        glm::mat4 m_EditorViewProjection{1.0f};
        bool m_HasEditorCamera = false;
        EditorViewOptions m_EditorViewOptions;
        float m_EditorClock = 0.0f;   // düzenleyicideki çizim zamanı (yalnızca Realtime açıkken ilerler)
        uint32_t m_MouseHoverEntity = 0xFFFFFFFFu;  // OnMouse*: imlecin üstünde olduğu çarpıştırıcı
        uint32_t m_MouseDownEntity = 0xFFFFFFFFu;   // OnMouse*: basılı tutulan
        bool m_IsRuntimeScene = false;

        void DestroyScriptInstances(bool callOnDestroy);
        // Silinen varlığın betikleri: OnDestroy + örneği kare sonunda yok edilmek üzere emekliye ayır
        void RetireEntityScripts(entt::entity entity);
        // Betik örneği yüklenip Awake/OnCreate/Start çağrılmamış betikler (oyun sırasında eklenen/oluşturulan varlıklar dahil)
        void StartPendingScripts();
        void DispatchUIEventsToScripts(const std::vector<std::pair<Scripting::ScriptRuntime*, void*>>& instances);
        void InvokeScriptsFixedUpdate(float dt);
        void DispatchPhysicsEvents();
        void DispatchPhysicsEvents2D();
        glm::vec2 m_UIScreenSize{ 1920.0f, 1080.0f };
        bool m_UseEditorCameraForScripts = true;

        void SetViewportBounds(const glm::vec2& min, const glm::vec2& max) { m_ViewportBoundsMin = min; m_ViewportBoundsMax = max; }
        void SetViewportHovered(bool hovered) { m_ViewportHovered = hovered; }
        bool IsViewportHovered() const { return m_ViewportHovered; }
        void SetEditorViewProjection(const glm::mat4& vp) { m_EditorViewProjection = vp; m_HasEditorCamera = true; }
        void SetUseEditorCameraForScripts(bool use) { m_UseEditorCameraForScripts = use; }
        bool GetUseEditorCameraForScripts() const { return m_UseEditorCameraForScripts; }
        
    private:
        static std::vector<std::string> s_AvailableTags;
        std::vector<LayerInfo> m_Layers;

        // Hierarchy data
        std::unordered_map<entt::entity, entt::entity> m_EntityParent; // child -> parent
        std::unordered_map<entt::entity, std::vector<entt::entity>> m_EntityChildren; // parent -> children

        // UI Navigation
        entt::entity m_SelectedButton = entt::null; // Klavye ile seçili buton
        std::vector<uint32_t> m_EditorSelection;
        std::unordered_set<uint32_t> m_LODHidden;
        // DestroyEntity içinde olan varlıklar (OnDestroy içinden tekrar silmeye karşı)
        std::unordered_set<uint32_t> m_DestroyingEntities;

        // Environment
        std::shared_ptr<Cubemap> m_Skybox;
        float m_SkyboxExposure = 1.0f;
        float m_SkyboxRotation = 0.0f;
        glm::vec3 m_SkyboxTint = glm::vec3(1.0f);
        
        // Ambient Light
        EnvironmentLightingSource m_EnvironmentLightingSource = EnvironmentLightingSource::Skybox;
        glm::vec3 m_AmbientColor = glm::vec3(0.3f); // Skybox/Color mode için
        glm::vec3 m_AmbientSkyColor = glm::vec3(0.5f, 0.6f, 0.7f); // Gradient: Sky
        glm::vec3 m_AmbientEquatorColor = glm::vec3(0.3f, 0.3f, 0.3f); // Gradient: Equator
        glm::vec3 m_AmbientGroundColor = glm::vec3(0.2f, 0.15f, 0.1f); // Gradient: Ground
        float m_AmbientIntensity = 1.0f;
        
        // Render ayarları (sis, gölge, post, ortam)
        RenderSettings m_RenderSettings;
        
        // Sun Source
        uint64_t m_SunSourceUUID = 0; // 0 = None (ilk directional light kullanılır)
        
        // Halo/Flare
        bool m_ShowNavMesh = false;
        bool m_HaloEnabled = false;
        std::shared_ptr<class Texture2D> m_HaloTexture;
        float m_HaloStrength = 0.5f;
        bool m_FlareEnabled = false;
        float m_FlareFadeSpeed = 3.0f;
        float m_FlareStrength = 1.0f;
        std::vector<FlareElement> m_FlareElements;
        std::shared_ptr<class Texture2D> m_SpotCookie;


        // Environment Reflections
        ReflectionSource m_ReflectionSource = ReflectionSource::Skybox;
        int m_ReflectionResolution = 128;
        int m_ReflectionCompression = 0; // Auto
        int m_ReflectionBounces = 1;

        // Baked Lightmap
        std::string m_BakedLightmapPath;
        std::shared_ptr<class Texture2D> m_BakedLightmapTexture;
        int m_LightmapResolution = 40;
        int m_MaxLightmapSize = 1024;
        int m_MaxBounces = 2;
        int m_DirectSamples = 32;
        int m_IndirectSamples = 512;
        bool m_AmbientOcclusion = true;
        int m_Filtering = 0;

        // Physics
        std::unique_ptr<PhysicsWorld2D> m_PhysicsWorld2D;
        std::unique_ptr<PhysicsWorld3D> m_PhysicsWorld3D;
        bool m_PhysicsWarmedUp = false;

        // AI
        NavMesh m_NavMesh;

        // Işık verisi (sahneye bağlı; sahne değişince eski lightmap kalmaz)
        std::shared_ptr<AdaptiveProbeVolume> m_APV;
        std::shared_ptr<OcclusionData> m_OcclusionData;
        bool m_OcclusionVisualize = false;
        std::unordered_set<uint32_t> m_OcclusionHidden;
        bool m_RenderingPlanar = false;          // aynalanmış kameradan çizim sürüyor (iç içe yansıma yok)
        void RenderPlanarReflection(const RenderCamera& camera);
        bool m_CapturingProbes = false;
        bool m_EditorView2D = false;                     // editör kamerası 2D modunda
        entt::entity m_BackgroundCamera = entt::null;    // oyun görünümünün arka planını belirleyen kamera
        void CaptureReflectionProbes();
        void CaptureSceneCubes();

        friend class Entity;
        friend class SceneSerializer;
    };

    // Explicit specialization declarations — Editor Engine.dll'den import etsin (MSVC LNK2019)
    // Tanımlar Scene.cpp'de CQ_API (dllexport) ile.
    template<> void Scene::OnComponentAdded<IDComponent>(Entity entity, IDComponent& component);
    template<> void Scene::OnComponentAdded<TransformComponent>(Entity entity, TransformComponent& component);
        template<> void Scene::OnComponentAdded<TagComponent>(Entity entity, TagComponent& component);
        template<> void Scene::OnComponentAdded<ActiveComponent>(Entity entity, ActiveComponent& component);
    template<> void Scene::OnComponentAdded<CameraComponent>(Entity entity, CameraComponent& component);
    template<> void Scene::OnComponentAdded<SpriteRendererComponent>(Entity entity, SpriteRendererComponent& component);
    template<> void Scene::OnComponentAdded<MeshRendererComponent>(Entity entity, MeshRendererComponent& component);
    template<> void Scene::OnComponentAdded<RigidBody2DComponent>(Entity entity, RigidBody2DComponent& component);
    template<> void Scene::OnComponentAdded<BoxCollider2DComponent>(Entity entity, BoxCollider2DComponent& component);
    template<> void Scene::OnComponentAdded<CircleCollider2DComponent>(Entity entity, CircleCollider2DComponent& component);
    template<> void Scene::OnComponentAdded<RigidbodyComponent>(Entity entity, RigidbodyComponent& component);
    template<> void Scene::OnComponentAdded<BoxColliderComponent>(Entity entity, BoxColliderComponent& component);
    template<> void Scene::OnComponentAdded<SphereColliderComponent>(Entity entity, SphereColliderComponent& component);
    template<> void Scene::OnComponentAdded<CapsuleColliderComponent>(Entity entity, CapsuleColliderComponent& component);
    template<> void Scene::OnComponentAdded<MeshColliderComponent>(Entity entity, MeshColliderComponent& component);
    template<> void Scene::OnComponentAdded<TextRendererComponent>(Entity entity, TextRendererComponent& component);
    template<> void Scene::OnComponentAdded<LayerComponent>(Entity entity, LayerComponent& component);
    template<> void Scene::OnComponentAdded<ButtonComponent>(Entity entity, ButtonComponent& component);
    template<> void Scene::OnComponentAdded<CanvasComponent>(Entity entity, CanvasComponent& component);
    template<> void Scene::OnComponentAdded<CanvasScalerComponent>(Entity entity, CanvasScalerComponent& component);
    template<> void Scene::OnComponentAdded<GraphicRaycasterComponent>(Entity entity, GraphicRaycasterComponent& component);
    template<> void Scene::OnComponentAdded<AnimatorComponent>(Entity entity, AnimatorComponent& component);
    template<> void Scene::OnComponentAdded<AnimationComponent>(Entity entity, AnimationComponent& component);
    template<> void Scene::OnComponentAdded<DirectionalLightComponent>(Entity entity, DirectionalLightComponent& component);
    template<> void Scene::OnComponentAdded<PointLightComponent>(Entity entity, PointLightComponent& component);
    template<> void Scene::OnComponentAdded<SpotLightComponent>(Entity entity, SpotLightComponent& component);
    template<> void Scene::OnComponentAdded<NativeScriptComponent>(Entity entity, NativeScriptComponent& component);
    template<> void Scene::OnComponentAdded<ConquerorScriptComponent>(Entity entity, ConquerorScriptComponent& component);
    template<> void Scene::OnComponentAdded<AudioSourceComponent>(Entity entity, AudioSourceComponent& component);
    template<> void Scene::OnComponentAdded<AudioListenerComponent>(Entity entity, AudioListenerComponent& component);
    template<> void Scene::OnComponentAdded<AudioChorusFilterComponent>(Entity entity, AudioChorusFilterComponent& component);
    template<> void Scene::OnComponentAdded<AudioDistortionFilterComponent>(Entity entity, AudioDistortionFilterComponent& component);
    template<> void Scene::OnComponentAdded<AudioEchoFilterComponent>(Entity entity, AudioEchoFilterComponent& component);
    template<> void Scene::OnComponentAdded<AudioHighPassFilterComponent>(Entity entity, AudioHighPassFilterComponent& component);
    template<> void Scene::OnComponentAdded<AudioLowPassFilterComponent>(Entity entity, AudioLowPassFilterComponent& component);
    template<> void Scene::OnComponentAdded<AudioReverbFilterComponent>(Entity entity, AudioReverbFilterComponent& component);
    template<> void Scene::OnComponentAdded<AudioReverbZoneComponent>(Entity entity, AudioReverbZoneComponent& component);
    template<> void Scene::OnComponentAdded<AudioGainComponent>(Entity entity, AudioGainComponent& component);
    template<> void Scene::OnComponentAdded<AudioPanComponent>(Entity entity, AudioPanComponent& component);
    template<> void Scene::OnComponentAdded<NavMeshAgentComponent>(Entity entity, NavMeshAgentComponent& component);
    template<> void Scene::OnComponentAdded<NavMeshObstacleComponent>(Entity entity, NavMeshObstacleComponent& component);
    template<> void Scene::OnComponentAdded<NavMeshSurfaceComponent>(Entity entity, NavMeshSurfaceComponent& component);
    template<> void Scene::OnComponentAdded<ReflectionProbeComponent>(Entity entity, ReflectionProbeComponent& component);
    template<> void Scene::OnComponentAdded<AdaptiveProbeVolumeComponent>(Entity entity, AdaptiveProbeVolumeComponent& component);
    template<> void Scene::OnComponentAdded<TerrainComponent>(Entity entity, TerrainComponent& component);
    template<> void Scene::OnComponentAdded<ParticleSystemComponent>(Entity entity, ParticleSystemComponent& component);
    template<> void Scene::OnComponentAdded<CharacterControllerComponent>(Entity, CharacterControllerComponent&);
    template<> void Scene::OnComponentAdded<FixedJointComponent>(Entity, FixedJointComponent&);
    template<> void Scene::OnComponentAdded<HingeJointComponent>(Entity, HingeJointComponent&);
    template<> void Scene::OnComponentAdded<SliderJointComponent>(Entity, SliderJointComponent&);
    template<> void Scene::OnComponentAdded<SpringJointComponent>(Entity, SpringJointComponent&);
    template<> void Scene::OnComponentAdded<CharacterJointComponent>(Entity, CharacterJointComponent&);
    template<> void Scene::OnComponentAdded<BallJointComponent>(Entity, BallJointComponent&);
    template<> void Scene::OnComponentAdded<ConstantForceComponent>(Entity, ConstantForceComponent&);
#define CQ_DECL_ADDED(T) template<> void Scene::OnComponentAdded<T>(Entity, T&);
    CQ_DECL_ADDED(CapsuleCollider2DComponent) CQ_DECL_ADDED(PolygonCollider2DComponent) CQ_DECL_ADDED(EdgeCollider2DComponent)
    CQ_DECL_ADDED(DistanceJoint2DComponent) CQ_DECL_ADDED(SpringJoint2DComponent) CQ_DECL_ADDED(HingeJoint2DComponent)
    CQ_DECL_ADDED(SliderJoint2DComponent) CQ_DECL_ADDED(WheelJoint2DComponent) CQ_DECL_ADDED(FixedJoint2DComponent)
    CQ_DECL_ADDED(PlatformEffector2DComponent) CQ_DECL_ADDED(AreaEffector2DComponent) CQ_DECL_ADDED(PointEffector2DComponent) CQ_DECL_ADDED(CompositeCollider2DComponent) CQ_DECL_ADDED(ClothComponent)
    CQ_DECL_ADDED(SurfaceEffector2DComponent) CQ_DECL_ADDED(BuoyancyEffector2DComponent)
    CQ_DECL_ADDED(LineRendererComponent) CQ_DECL_ADDED(TrailRendererComponent) CQ_DECL_ADDED(LODGroupComponent)
    CQ_DECL_ADDED(LookAtConstraintComponent) CQ_DECL_ADDED(AimConstraintComponent) CQ_DECL_ADDED(PositionConstraintComponent)
    CQ_DECL_ADDED(RotationConstraintComponent) CQ_DECL_ADDED(ScaleConstraintComponent) CQ_DECL_ADDED(ParentConstraintComponent)
    CQ_DECL_ADDED(WindZoneComponent) CQ_DECL_ADDED(CameraFollowComponent) CQ_DECL_ADDED(RotatorComponent) CQ_DECL_ADDED(ParticleForceFieldComponent)
    CQ_DECL_ADDED(SplineContainerComponent) CQ_DECL_ADDED(SplineAnimateComponent) CQ_DECL_ADDED(SplineExtrudeComponent) CQ_DECL_ADDED(SplineInstantiateComponent)
    CQ_DECL_ADDED(VirtualCameraComponent) CQ_DECL_ADDED(CameraBrainComponent) CQ_DECL_ADDED(ImpulseSourceComponent) CQ_DECL_ADDED(PostProcessVolumeComponent) CQ_DECL_ADDED(DecalProjectorComponent)
    CQ_DECL_ADDED(Light2DComponent) CQ_DECL_ADDED(ShadowCaster2DComponent) CQ_DECL_ADDED(SpriteMaskComponent) CQ_DECL_ADDED(PixelPerfectCameraComponent)
    CQ_DECL_ADDED(BillboardRendererComponent) CQ_DECL_ADDED(HaloComponent) CQ_DECL_ADDED(LensFlareComponent) CQ_DECL_ADDED(CameraSkyboxComponent)
    CQ_DECL_ADDED(AreaLightComponent) CQ_DECL_ADDED(PlayerInputComponent) CQ_DECL_ADDED(PlayerInputManagerComponent)
    CQ_DECL_ADDED(NavMeshModifierComponent) CQ_DECL_ADDED(NavMeshModifierVolumeComponent) CQ_DECL_ADDED(NavMeshLinkComponent)
    CQ_DECL_ADDED(VideoPlayerComponent)
    CQ_DECL_ADDED(GameplayTagsComponent)
    CQ_DECL_ADDED(AbilitySystemComponent)
    CQ_DECL_ADDED(PlayerStartComponent)
    CQ_DECL_ADDED(GameModeComponent)
    CQ_DECL_ADDED(SpringArmComponent)
    CQ_DECL_ADDED(StreamedLevelComponent)
    CQ_DECL_ADDED(AnimationPlayerComponent)
    CQ_DECL_ADDED(LevelStreamingVolumeComponent)
    CQ_DECL_ADDED(AutoLODComponent)
    CQ_DECL_ADDED(SoundOcclusionComponent)
    CQ_DECL_ADDED(SoundConcurrencyComponent)
    CQ_DECL_ADDED(VertexPaintComponent)
    CQ_DECL_ADDED(FractureComponent)
    CQ_DECL_ADDED(RagdollComponent)
    CQ_DECL_ADDED(VirtualGeometryComponent)
    CQ_DECL_ADDED(CharacterCustomizerComponent)
    CQ_DECL_ADDED(SignalConnectionsComponent)
    CQ_DECL_ADDED(CSGShapeComponent)
    CQ_DECL_ADDED(MultiMeshComponent)
    CQ_DECL_ADDED(RemoteTransformComponent)
    CQ_DECL_ADDED(RayCastComponent)
    CQ_DECL_ADDED(VisibleOnScreenNotifierComponent)
    CQ_DECL_ADDED(Parallax2DComponent)
    CQ_DECL_ADDED(TimerComponent)
    CQ_DECL_ADDED(ShapeCastComponent)
    CQ_DECL_ADDED(VisibleOnScreenEnablerComponent)
    CQ_DECL_ADDED(CharacterBody2DComponent)
    CQ_DECL_ADDED(FogVolumeComponent)
    CQ_DECL_ADDED(NavigationRegion2DComponent)
    CQ_DECL_ADDED(NavigationAgent2DComponent)
    CQ_DECL_ADDED(NavigationObstacle2DComponent)
    CQ_DECL_ADDED(NavigationLink2DComponent)
    CQ_DECL_ADDED(RenderTargetComponent)
    CQ_DECL_ADDED(Bone2DComponent)
    CQ_DECL_ADDED(IK2DComponent)
    CQ_DECL_ADDED(RigComponent) CQ_DECL_ADDED(TwoBoneIKConstraintComponent) CQ_DECL_ADDED(MultiAimConstraintComponent) CQ_DECL_ADDED(ChainIKConstraintComponent) CQ_DECL_ADDED(DampedTransformComponent)
    CQ_DECL_ADDED(ProjectileMovementComponent) CQ_DECL_ADDED(SortingGroupComponent) CQ_DECL_ADDED(PrefabInstanceComponent)
    CQ_DECL_ADDED(GridComponent) CQ_DECL_ADDED(TilemapComponent) CQ_DECL_ADDED(TilemapCollider2DComponent)
    CQ_DECL_ADDED(NetworkManagerComponent) CQ_DECL_ADDED(NetworkObjectComponent) CQ_DECL_ADDED(NetworkTransformComponent) CQ_DECL_ADDED(NetworkRigidbodyComponent) CQ_DECL_ADDED(NetworkAnimatorComponent)
    CQ_DECL_ADDED(PlayableDirectorComponent) CQ_DECL_ADDED(WheelColliderComponent) CQ_DECL_ADDED(ConfigurableJointComponent)
    CQ_DECL_ADDED(FrictionJoint2DComponent) CQ_DECL_ADDED(RelativeJoint2DComponent) CQ_DECL_ADDED(TargetJoint2DComponent) CQ_DECL_ADDED(ConstantForce2DComponent)
#undef CQ_DECL_ADDED
    template<> void Scene::OnComponentAdded<RectTransformComponent>(Entity, RectTransformComponent&);
    template<> void Scene::OnComponentAdded<UIImageComponent>(Entity, UIImageComponent&);
    template<> void Scene::OnComponentAdded<UITextComponent>(Entity, UITextComponent&);
    template<> void Scene::OnComponentAdded<UIButtonComponent>(Entity, UIButtonComponent&);
    template<> void Scene::OnComponentAdded<UIToggleComponent>(Entity, UIToggleComponent&);
    template<> void Scene::OnComponentAdded<UISliderComponent>(Entity, UISliderComponent&);
    template<> void Scene::OnComponentAdded<UIInputFieldComponent>(Entity, UIInputFieldComponent&);
    template<> void Scene::OnComponentAdded<UIDropdownComponent>(Entity, UIDropdownComponent&);
    template<> void Scene::OnComponentAdded<UIScrollRectComponent>(Entity, UIScrollRectComponent&);
    template<> void Scene::OnComponentAdded<UILayoutGroupComponent>(Entity, UILayoutGroupComponent&);
    template<> void Scene::OnComponentAdded<UICanvasGroupComponent>(Entity, UICanvasGroupComponent&);
    template<> void Scene::OnComponentAdded<UIRectMask2DComponent>(Entity, UIRectMask2DComponent&);
    template<> void Scene::OnComponentAdded<UIMaskComponent>(Entity, UIMaskComponent&);
    template<> void Scene::OnComponentAdded<UIRawImageComponent>(Entity, UIRawImageComponent&);
    template<> void Scene::OnComponentAdded<UIContentSizeFitterComponent>(Entity, UIContentSizeFitterComponent&);
    template<> void Scene::OnComponentAdded<UIAspectRatioFitterComponent>(Entity, UIAspectRatioFitterComponent&);
    template<> void Scene::OnComponentAdded<UILayoutElementComponent>(Entity, UILayoutElementComponent&);
    template<> void Scene::OnComponentAdded<UIScrollbarComponent>(Entity, UIScrollbarComponent&);
    template<> void Scene::OnComponentAdded<UIEventTriggerComponent>(Entity, UIEventTriggerComponent&);
    template<> void Scene::OnComponentAdded<UIEventSystemComponent>(Entity, UIEventSystemComponent&);
    template<> void Scene::OnComponentAdded<UIShadowComponent>(Entity, UIShadowComponent&);
    template<> void Scene::OnComponentAdded<UIOutlineComponent>(Entity, UIOutlineComponent&);
    template<> void Scene::OnComponentAdded<UIPositionAsUV1Component>(Entity, UIPositionAsUV1Component&);
}
