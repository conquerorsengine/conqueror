#pragma once

#include <map>
#include "Scripting/ScriptTypes.h"

#include "Terrain/TerrainComponent.h"
#include "Particles/ParticleSystem.h"
#include "UI/UIComponents.h"

#include "SceneCamera.h"
#include "Core/Base/Base.h"
#include "Core/Animation/AnimationController.h"
#include "Core/Animation/Animator.h"
#include "../AISystem/Components/NavComponents.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>

#include <entt/entt.hpp>
#include <string>
#include <memory>
#include <functional>
#include <vector>

namespace Conqueror
{
    class Texture2D;
    class ScriptableEntity;
    class Material;
    class Mesh;
    class EnvironmentMap;

    struct IDComponent
    {
        uint64_t ID;

        IDComponent() = default;
        IDComponent(const IDComponent&) = default;
        IDComponent(uint64_t id) : ID(id) {}
    };

    struct TagComponent
    {
        std::string Tag;
        std::string GameTag = "Untagged";
        bool IsStatic = false;

        TagComponent() = default;
        TagComponent(const TagComponent&) = default;
        TagComponent(const std::string& tag) : Tag(tag) {}
    };

    // Unity-style GameObject activeness: a closed object is drawn by neither
    // Viewport nor game camera, and casts no shadow. Absent component = visible.
    struct ActiveComponent
    {
        bool Active = true;

        ActiveComponent() = default;
        ActiveComponent(const ActiveComponent&) = default;
        ActiveComponent(bool active) : Active(active) {}
    };

    struct TransformComponent
    {
        glm::vec3 Position = { 0.0f, 0.0f, 0.0f };
        glm::vec3 Rotation = { 0.0f, 0.0f, 0.0f };
        glm::vec3 Scale = { 1.0f, 1.0f, 1.0f };
        
        glm::mat4 WorldTransform = glm::mat4(1.0f); // Hierarchy ile hesaplanan world transform

        TransformComponent() = default;
        TransformComponent(const TransformComponent&) = default;
        TransformComponent(const glm::vec3& position) : Position(position) {}

        glm::mat4 GetTransform() const
        {
            glm::mat4 rotation = glm::toMat4(glm::quat(Rotation));

            return glm::translate(glm::mat4(1.0f), Position)
                * rotation
                * glm::scale(glm::mat4(1.0f), Scale);
        }
    };

    enum class SpriteDrawMode : int { Simple = 0, Sliced, Tiled };

    // Sprite Mask ile etkileşim (Unity SpriteRenderer.maskInteraction)
    enum class SpriteMaskInteraction : int { None = 0, VisibleInsideMask, VisibleOutsideMask };

    struct SpriteRendererComponent
    {
        glm::vec4 Color{ 1.0f, 1.0f, 1.0f, 1.0f };
        std::shared_ptr<Texture2D> Texture;
        float TilingFactor = 1.0f;

        // Sprite sayfası (Sprite Editor) — boşsa tüm doku kullanılır
        std::string SpriteName;
        // SpriteName yoksa: özel alt dikdörtgen (piksel, sol-alt köken). z==0 => tüm doku
        glm::vec4 Rect{ 0.0f };
        glm::vec2 Pivot{ 0.5f, 0.5f };
        glm::vec4 Border{ 0.0f };          // 9-slice (piksel): sol, alt, sağ, üst
        float PixelsPerUnit = 100.0f;      // sayfa yoksa
        bool FlipX = false;
        bool FlipY = false;
        SpriteDrawMode DrawMode = SpriteDrawMode::Simple;
        glm::vec2 Size{ 1.0f, 1.0f };      // Sliced/Tiled modunda dünya boyutu
        SpriteMaskInteraction MaskInteraction = SpriteMaskInteraction::None;

        // Her sprite için kendi render kaynakları
        std::shared_ptr<class VertexArray> VertexArray;
        std::shared_ptr<class VertexBuffer> VertexBuffer;
        std::shared_ptr<class IndexBuffer> IndexBuffer;
        bool RenderResourcesInitialized = false;

        SpriteRendererComponent() = default;
        SpriteRendererComponent(const SpriteRendererComponent&) = default;
        SpriteRendererComponent(const glm::vec4& color) : Color(color) {}
    };

    enum class MeshType : int { Cube = 0, Sphere, Plane, Cylinder };

    struct MeshRendererComponent
    {
        MeshType Type = MeshType::Cube;
        glm::vec4 Color{ 1.0f, 1.0f, 1.0f, 1.0f };
        
        // PBR Material parametreleri (fallback, Material atanmadıysa kullanılır)
        float Metallic = 0.0f;
        float Roughness = 0.5f;
        float AO = 1.0f;
        // Işıma (neon / ekran / lamba): renk * güç, HDR (bloom)
        glm::vec3 EmissionColor{ 0.0f };
        float EmissionStrength = 0.0f;
        // Işıma dokuyla çarpılır (ör. yalnız pencereler parlar); kapalıysa tüm yüzey eşit ışır
        bool EmissionFromTexture = false;
        // Doku döşeme / kaydırma (büyük yüzeylerde dokunun gerilmemesi için)
        glm::vec2 Tiling{ 1.0f, 1.0f };
        glm::vec2 Offset{ 0.0f, 0.0f };
        // Dünya hizalı UV (Unreal WorldAlignedTexture): doku ağın UV'sinden değil dünya konumundan, üç eksenden
        // izdüşümle örneklenir; Tiling = metre başına tekrar. Ölçeklenen bloklarda ızgara hep aynı boyda kalır.
        bool WorldAlignedUV = false;
        // WorldAlignedUV açıkken: izdüşüm nesnenin kendi uzayında (metre) — doku hareket eden / dönen nesneyle gider
        // (fizik küpleri). Kapalıysa dünyada sabit (zemin, duvar).
        bool AlignToObject = false;

        // Material referansı (Unity tarzı): MaterialPath doluysa .cqmat varlığı (paylaşılan nesne), boşsa sahneye gömülü.
        // Malzeme atanmışsa çizimde YALNIZ o kullanılır (doku / renk / ışıma malzemenin içinde); aşağıdaki alanlar
        // malzemesiz nesnenin gömülü varsayılan malzemesidir.
        std::shared_ptr<Material> MaterialInstance;
        std::string MaterialPath;

        // Texture
        std::shared_ptr<Texture2D> Texture;
        std::string TexturePath;
        // Normal haritası (OpenGL / +Y) ve maske haritası (glTF ORM: R = ortam kapatma, G = pürüzlülük, B = metallik;
        // Roughness / Metallic / AO değerleri çarpan olur). Asfalt, beton gibi ayrıntılı yüzeyler için.
        std::shared_ptr<Texture2D> NormalMap;
        std::string NormalMapPath;
        std::shared_ptr<Texture2D> MaskMap;
        std::string MaskMapPath;

        // Realtime GI
        bool RealtimeGI = false;

        // Gölgeler
        bool CastShadows = true;
        bool ReceiveShadows = true;

        // Işıklandırmasız (Unity Unlit / Unreal Unlit): renk = doku x renk + ışıma; ekranlar, LED duvarlar için
        bool Unlit = false;

        // Alfa kırpma (Unity "Alpha Clipping"): dokunun alfası eşiğin altındaysa piksel çizilmez, gerisi opak geçişte
        // çizilir (yuvarlak kâğıt hedef, yaprak, çit). Yarı saydam sıralama sorunları olmaz; öndeki saydam izler görünür.
        bool AlphaClip = false;
        float AlphaClipThreshold = 0.5f;

        // Prosedürel ağ (serileştirilmez; ör. Spline Extrude üretir): varsa Type yerine çizilir
        std::shared_ptr<Mesh> CustomMesh;

        // Çalışma zamanı önbelleği (serileştirilmez): MaterialInstance yokken kare
        // başına yeni Material üretmemek için
        std::shared_ptr<Material> RuntimeMaterial;
        const Material* RuntimeMaterialSource = nullptr;
        // Oyunda Color değişince (betik, tween) malzeme bu nesneye özel kopyaya döner (Unity renderer.material)
        glm::vec4 RuntimeTrackedColor{ -1.0f };
        bool RuntimeMaterialInstanced = false;

        MeshRendererComponent() = default;
        MeshRendererComponent(const MeshRendererComponent&) = default;
        MeshRendererComponent(const glm::vec4& color) : Color(color) {}
    };

    struct CameraComponent
    {
        // Unity "Background Type": Auto = perspektifte gökyüzü, ortografikte düz renk (Unity'nin 3D/2D şablon varsayılanları)
        enum class BackgroundMode : int { Auto = 0, Skybox, SolidColor };

        SceneCamera Camera;
        bool Primary = true;
        bool FixedAspectRatio = false;
        BackgroundMode Background = BackgroundMode::Auto;
        glm::vec4 BackgroundColor = { 0.192f, 0.302f, 0.475f, 1.0f };   // Unity varsayılanı (49, 77, 121), sRGB

        CameraComponent() = default;
        CameraComponent(const CameraComponent&) = default;
    };

    struct RigidBody2DComponent
    {
        enum class BodyType { Static = 0, Dynamic, Kinematic };
        BodyType Type = BodyType::Dynamic;
        bool FixedRotation = false;
        bool UseAutoMass = false;          // kütle çarpıştırıcı yoğunluklarından
        float Mass = 1.0f;
        float LinearDrag = 0.0f;
        float AngularDrag = 0.05f;
        float GravityScale = 1.0f;
        bool Interpolate = true;
        bool ContinuousCollision = false;  // hızlı cisimler (mermi)
        bool AllowSleep = true;

        void* RuntimeBody = nullptr;

        RigidBody2DComponent() = default;
        RigidBody2DComponent(const RigidBody2DComponent&) = default;
    };

    struct BoxCollider2DComponent
    {
        glm::vec2 Offset = { 0.0f, 0.0f };
        glm::vec2 Size = { 1.0f, 1.0f };   // tam boyut (Unity gibi)
        bool IsTrigger = false;
        float EdgeRadius = 0.0f;

        float Density = 1.0f;
        float Friction = 0.5f;
        float Restitution = 0.0f;
        float RestitutionThreshold = 0.5f;

        void* RuntimeFixture = nullptr;

        BoxCollider2DComponent() = default;
        BoxCollider2DComponent(const BoxCollider2DComponent&) = default;
    };

    struct CircleCollider2DComponent
    {
        glm::vec2 Offset = { 0.0f, 0.0f };
        float Radius = 0.5f;
        bool IsTrigger = false;

        float Density = 1.0f;
        float Friction = 0.5f;
        float Restitution = 0.0f;
        float RestitutionThreshold = 0.5f;

        void* RuntimeFixture = nullptr;

        CircleCollider2DComponent() = default;
        CircleCollider2DComponent(const CircleCollider2DComponent&) = default;
    };

    // 3D Physics Components
    struct RigidbodyComponent
    {
        enum class BodyType { Static = 0, Dynamic, Kinematic };
        enum class CollisionDetectionMode { Discrete = 0, Continuous };
        BodyType Type = BodyType::Dynamic;

        float Mass = 1.0f;
        float LinearDrag = 0.05f;
        float AngularDrag = 0.05f;
        float GravityScale = 1.0f;
        bool UseGravity = true;
        bool FreezeRotation = false;          // eski: tüm dönme eksenleri
        bool FreezePositionX = false, FreezePositionY = false, FreezePositionZ = false;
        bool FreezeRotationX = false, FreezeRotationY = false, FreezeRotationZ = false;
        bool Interpolate = true;              // görsel dönüşümü sabit adımlar arasında yumuşat
        CollisionDetectionMode CollisionDetection = CollisionDetectionMode::Discrete;
        float MaxAngularVelocity = 50.0f;     // rad/s
        bool AllowSleep = true;

        float Friction = 0.5f;
        float Restitution = 0.0f;

        void* RuntimeBody = nullptr;

        RigidbodyComponent() = default;
        RigidbodyComponent(const RigidbodyComponent&) = default;
    };

    struct BoxColliderComponent
    {
        glm::vec3 Offset = { 0.0f, 0.0f, 0.0f };
        glm::vec3 Size = { 1.0f, 1.0f, 1.0f };
        bool IsTrigger = false;
        std::string PhysicsMaterial;          // .cqphysmat (sürtünme / sekme); boşsa Rigidbody değerleri

        BoxColliderComponent() = default;
        BoxColliderComponent(const BoxColliderComponent&) = default;
    };

    struct SphereColliderComponent
    {
        glm::vec3 Offset = { 0.0f, 0.0f, 0.0f };
        float Radius = 0.5f;
        bool IsTrigger = false;
        std::string PhysicsMaterial;          // .cqphysmat (sürtünme / sekme); boşsa Rigidbody değerleri

        SphereColliderComponent() = default;
        SphereColliderComponent(const SphereColliderComponent&) = default;
    };

    struct CapsuleColliderComponent
    {
        glm::vec3 Offset = { 0.0f, 0.0f, 0.0f };
        float Radius = 0.5f;
        float Height = 2.0f;
        int Direction = 1;                    // 0 = X, 1 = Y, 2 = Z ekseni
        bool IsTrigger = false;
        std::string PhysicsMaterial;          // .cqphysmat (sürtünme / sekme); boşsa Rigidbody değerleri

        CapsuleColliderComponent() = default;
        CapsuleColliderComponent(const CapsuleColliderComponent&) = default;
    };

    struct MeshColliderComponent
    {
        bool IsConvex = false;
        bool IsTrigger = false;
        std::string PhysicsMaterial;          // .cqphysmat (sürtünme / sekme); boşsa Rigidbody değerleri

        void* RuntimeShape = nullptr;

        MeshColliderComponent() = default;
        MeshColliderComponent(const MeshColliderComponent&) = default;
    };

    struct LayerComponent
    {
        int32_t Layer = 0;
        int32_t OrderInLayer = 0;

        LayerComponent() = default;
        LayerComponent(const LayerComponent&) = default;
        LayerComponent(int32_t layer, int32_t order = 0) : Layer(layer), OrderInLayer(order) {}
    };

    // Satırların varlık konumuna göre yatay hizası (Unity TextMesh anchor / alignment)
    enum class TextAlignment : int { Left = 0, Center, Right };

    struct TextRendererComponent
    {
        std::string Text = "Text";
        glm::vec4 Color{ 1.0f, 1.0f, 1.0f, 1.0f };
        float FontSize = 1.0f;
        TextAlignment Alignment = TextAlignment::Left;
        // TODO: Font reference

        TextRendererComponent() = default;
        TextRendererComponent(const TextRendererComponent&) = default;
        TextRendererComponent(const std::string& text) : Text(text) {}
    };

    struct ImageComponent
    {
        glm::vec4 Color{ 1.0f, 1.0f, 1.0f, 1.0f };
        std::shared_ptr<Texture2D> Texture;
        float TilingFactor = 1.0f;

        // Her image için kendi render kaynakları
        std::shared_ptr<class VertexArray> VertexArray;
        std::shared_ptr<class VertexBuffer> VertexBuffer;
        std::shared_ptr<class IndexBuffer> IndexBuffer;
        bool RenderResourcesInitialized = false;

        ImageComponent() = default;
        ImageComponent(const ImageComponent&) = default;
        ImageComponent(const glm::vec4& color) : Color(color) {}
    };

    struct ButtonComponent
    {
        enum class State { Normal = 0, Hover, Pressed, Disabled, Highlighted };
        enum class NavigationMode { None = 0, Horizontal, Vertical, Automatic, Explicit };
        
        // Durum
        State CurrentState = State::Normal;
        bool Interactable = true;
        bool IsHovered = false;
        bool IsPressed = false;
        bool IsHighlighted = false; // Manuel olarak set edilir (seçili buton, default buton, vb.)
        bool IsBlockedByDepth = false; // Depth test ile engellenme durumu

        // Navigation
        NavigationMode Navigation = NavigationMode::Automatic;
        entt::entity NavigateUp = entt::null;    // Entity handle (Explicit mode için)
        entt::entity NavigateDown = entt::null;  // Entity handle (Explicit mode için)
        entt::entity NavigateLeft = entt::null;  // Entity handle (Explicit mode için)
        entt::entity NavigateRight = entt::null; // Entity handle (Explicit mode için)

        // Renk detayları göster/gizle
        bool ShowColorDetails = false;

        // Background Colors
        glm::vec4 BackgroundNormalColor{ 0.7f, 0.7f, 0.7f, 1.0f };
        glm::vec4 BackgroundHoverColor{ 0.5f, 0.5f, 0.5f, 1.0f };
        glm::vec4 BackgroundPressedColor{ 0.3f, 0.3f, 0.3f, 1.0f };
        glm::vec4 BackgroundDisabledColor{ 0.2f, 0.2f, 0.2f, 0.5f };
        glm::vec4 BackgroundHighlightedColor{ 1.0f, 0.78f, 0.2f, 1.0f };

        // Text Colors
        glm::vec4 TextNormalColor{ 1.0f, 1.0f, 1.0f, 1.0f };
        glm::vec4 TextHoverColor{ 1.0f, 1.0f, 0.9f, 1.0f };
        glm::vec4 TextPressedColor{ 0.8f, 0.8f, 0.8f, 1.0f };
        glm::vec4 TextDisabledColor{ 0.5f, 0.5f, 0.5f, 0.5f };
        glm::vec4 TextHighlightedColor{ 1.0f, 1.0f, 0.4f, 1.0f };

        // Transition (smooth color change)
        glm::vec4 CurrentBackgroundColor{ 0.7f, 0.7f, 0.7f, 1.0f };
        glm::vec4 CurrentTextColor{ 1.0f, 1.0f, 1.0f, 1.0f };
        float ColorMultiplier = 1.0f; // 1.0 - 5.0
        float TransitionSpeed = 10.0f;

        // 9-Slice
        bool Use9Slice = false;
        float BorderSize = 10.0f;

        // Callback
        std::function<void()> OnClick;

        ButtonComponent() = default;
        ButtonComponent(const ButtonComponent&) = default;
    };

    struct CanvasComponent
    {
        enum class RenderMode { ScreenSpaceOverlay = 0, ScreenSpaceCamera, WorldSpace };
        
        RenderMode Mode = RenderMode::ScreenSpaceOverlay;
        bool PixelPerfect = false;
        int32_t SortOrder = 0; // Canvas'lar arası render sırası
        
        CanvasComponent() = default;
        CanvasComponent(const CanvasComponent&) = default;
    };

    struct CanvasScalerComponent
    {
        enum class UIScaleMode { ConstantPixelSize = 0, ScaleWithScreenSize, ConstantPhysicalSize };
        
        UIScaleMode ScaleMode = UIScaleMode::ScaleWithScreenSize;
        float ScaleFactor = 1.0f;
        float ReferencePixelsPerUnit = 100.0f;
        glm::vec2 ReferenceResolution{ 1920.0f, 1080.0f };
        float MatchWidthOrHeight = 0.5f;       // 0 genişlik, 1 yükseklik
        
        CanvasScalerComponent() = default;
        CanvasScalerComponent(const CanvasScalerComponent&) = default;
    };

    struct GraphicRaycasterComponent
    {
        enum class BlockingObjects { None = 0, TwoD, ThreeD, All };
        
        bool IgnoreReversedGraphics = true;
        BlockingObjects Blocking = BlockingObjects::None;
        uint32_t BlockingMask = 0xFFFFFFFF;
        
        GraphicRaycasterComponent() = default;
        GraphicRaycasterComponent(const GraphicRaycasterComponent&) = default;
    };

    struct ModelComponent
    {
        std::shared_ptr<struct Model> ModelData;
        std::string FilePath;
        bool RealtimeGI = false;
        // Örneğe özel renk çarpanı (Unity MaterialPropertyBlock / Unreal malzeme örneği parametresi): paylaşılan
        // malzemeyi değiştirmeden bu nesnenin taban rengini çarpar (ör. kalabalıkta her yayaya farklı kıyafet rengi)
        glm::vec3 Tint{ 1.0f };

        ModelComponent() = default;
        ModelComponent(const ModelComponent&) = default;
    };

    struct AnimatorComponent
    {
        bool Playing = true;
        float Speed = 1.f;
        int ActiveClipIndex = 0;
        float CurrentTime = 0.f;
        std::string MultiplierParameter;
        std::string MotionTimeParameter;
        std::vector<glm::mat4> BoneMatricesGPU;
        Pose FinalPose;                   // son karenin yerel düğüm pozu (ragdoll başlangıcı, araçlar)
        // Zaman çizelgesi geçişi (Animation parçasında örtüşen klipler): ikinci klip, zamanı ve ağırlığı (0 = yok)
        int BlendClipIndex = -1;
        float BlendClipTime = 0.f;
        float BlendWeight = 0.f;

        AnimatorComponent() = default;
        AnimatorComponent(const AnimatorComponent&) = default;
    };

    struct AnimParameterRuntimeValue
    {
        std::string Name;
        AnimParameterType Type = AnimParameterType::Float;
        float FloatValue = 0.f;
        bool BoolValue = false;
        int IntValue = 0;
    };

    struct AnimationComponent
    {
        std::string ControllerFilePath;
        std::shared_ptr<AnimationController> Controller;

        float Speed = 1.f;
        bool PlayOnAwake = true;
        bool Playing = false;

        int ActiveLayerIndex = 0;
        std::string CurrentStateName;
        std::string nextStateName;
        float TransitionProgress = 0.f;
        float TransitionDuration = 0.f;
        bool IsTransitioning = false;

        float CurrentTime = 0.f;
        std::vector<AnimParameterRuntimeValue> Parameters;
        std::vector<glm::mat4> BoneMatricesGPU;

        // Katman başına çalışma zamanı durumu (AnimationStateMachine yönetir)
        std::vector<AnimLayerRuntime> LayerRuntime;
        bool ApplyRootMotion = false;
        int CullingMode = 0;              // 0 Always Animate, 1 Cull Update Transforms, 2 Cull Completely
        const AnimationController* RuntimeController = nullptr; // değişince durum sıfırlanır

        // Montaj (AnimMontagePlay) + bu karede üretilen notify / montaj olayları (Scene betiklere dağıtır)
        AnimMontageInstance Montage;
        std::vector<AnimNotifyEvent> PendingNotifies;
        uint32_t NotifyCount = 0;         // toplam ateşlenen notify (hata ayıklama / testler)
        Pose FinalPose;                   // son karenin yerel düğüm pozu (ragdoll başlangıcı, araçlar)

        AnimationComponent() = default;
        AnimationComponent(const AnimationComponent&) = default;
    };

    enum class LightMode { Realtime = 0, Mixed = 1, Baked = 2 };

    // Light Component'leri
    struct DirectionalLightComponent
    {
        glm::vec3 Direction = glm::vec3(0.0f, -1.0f, 0.0f);
        glm::vec3 Color = glm::vec3(1.0f, 1.0f, 1.0f);
        float Intensity = 1.0f;
        bool CastShadows = true;
        LightMode Mode = LightMode::Realtime;

        DirectionalLightComponent() = default;
        DirectionalLightComponent(const DirectionalLightComponent&) = default;
    };

    struct PointLightComponent
    {
        glm::vec3 Color = glm::vec3(1.0f, 1.0f, 1.0f);
        float Intensity = 1.0f;
        float Range = 10.0f;
        
        // Attenuation (mesafe ile azalma)
        float Constant = 1.0f;
        float Linear = 0.09f;
        float Quadratic = 0.032f;
        
        bool CastShadows = false;
        LightMode Mode = LightMode::Realtime;

        PointLightComponent() = default;
        PointLightComponent(const PointLightComponent&) = default;
    };

    struct SpotLightComponent
    {
        glm::vec3 Direction = glm::vec3(0.0f, -1.0f, 0.0f);
        glm::vec3 Color = glm::vec3(1.0f, 1.0f, 1.0f);
        float Intensity = 1.0f;
        float Range = 10.0f;
        
        // Spot açıları (derece)
        float InnerConeAngle = 12.5f;
        float OuterConeAngle = 17.5f;
        
        // Attenuation
        float Constant = 1.0f;
        float Linear = 0.09f;
        float Quadratic = 0.032f;
        
        bool CastShadows = false;
        LightMode Mode = LightMode::Realtime;

        SpotLightComponent() = default;
        SpotLightComponent(const SpotLightComponent&) = default;
    };

    // Reflection Probe Component
    struct ReflectionProbeComponent
    {
        glm::vec3 BoxOffset = glm::vec3(-1.0f, -1.0f, -1.0f);
        glm::vec3 BoxSize = glm::vec3(1.0f, 1.0f, 1.0f);
        uint32_t Resolution = 128;
        float Intensity = 1.0f;
        int Bounces = 1;
        bool RealtimeUpdate = false;

        // Captured cubemap path
        std::string CubemapPath;

        // Kutu karışım mesafesi (kenarlarda gökyüzüne yumuşak geçiş)
        float BlendDistance = 1.0f;
        float UpdateInterval = 0.5f; // RealtimeUpdate açıksa (saniye)

        // Çalışma zamanı (serileştirilmez)
        std::shared_ptr<EnvironmentMap> Captured;
        bool NeedsCapture = true;
        float TimeSinceCapture = 0.0f;

        ReflectionProbeComponent() = default;
        ReflectionProbeComponent(const ReflectionProbeComponent&) = default;
    };

    // Adaptive Probe Volume Component
    struct AdaptiveProbeVolumeComponent
    {
        glm::vec3 ProbeOffset = glm::vec3(0.0f);
        float MinSpacing = 1.0f;
        float MaxSpacing = 9.0f;
        int BakingMode = 0; // 0 = Single Scene, 1 = Multi Scene

        // Probe invalidity settings
        bool Dilation = false;
        bool VirtualOffset = true;
        float ValidityThreshold = 0.75f;
        float SearchDistanceMultiplier = 0.2f;
        float GeometryBias = 0.01f;
        float RayOriginBias = -0.001f;

        AdaptiveProbeVolumeComponent() = default;
        AdaptiveProbeVolumeComponent(const AdaptiveProbeVolumeComponent&) = default;
    };

    struct NativeScriptData
    {
        ScriptableEntity* Instance = nullptr;
        std::string ModuleName; // DLL adı (örn: "libPlayerScript")
        std::string ClassName;  // Script class adı (örn: "PlayerController")
        std::string ScriptPath; // DLL dosya yolu (örn: "/path/to/libPlayerScript.so")

        // Script lifecycle yönetimi
        std::function<ScriptableEntity*()> InstantiateScript;
        std::function<void(NativeScriptData*)> DestroyScript;
        
        template<typename T>
        void Bind()
        {
            InstantiateScript = []() { return static_cast<ScriptableEntity*>(new T()); };
            DestroyScript = [](NativeScriptData* sc) { delete sc->Instance; sc->Instance = nullptr; };
        }
    };

    struct NativeScriptComponent
    {
        std::vector<NativeScriptData> Scripts;

        NativeScriptComponent() = default;
        NativeScriptComponent(const NativeScriptComponent&) = default;
    };

    // Denetçiden düzenlenen betik alanı değeri (sahneyle kaydedilir)
    using ScriptFieldOverride = Scripting::ScriptValue;

    struct ConquerorScriptData
    {
        std::string ScriptPath;
        std::string ClassName;
        std::map<std::string, ScriptFieldOverride> FieldOverrides;
        
        // Çalışma zamanı: dilin örnek tutamacı ve onu yöneten Scripting::ScriptRuntime
        void* Instance = nullptr;
        void* Runtime = nullptr;
        bool bLoaded = false;
    };

    struct ConquerorScriptComponent
    {
        std::vector<ConquerorScriptData> Scripts;

        ConquerorScriptComponent() = default;
        ConquerorScriptComponent(const ConquerorScriptComponent&) = default;
    };

    // Audio Graph definitions
    enum class AudioGraphNodeType
    {
        Source, Master, LowPass, HighPass, Echo, Reverb, Chorus, Distortion, Gain, Pan
    };

    struct AudioGraphNode
    {
        int ID;
        AudioGraphNodeType Type;
        glm::vec2 EditorPosition;
        bool Bypassed = false;

        AudioGraphNode(int id, AudioGraphNodeType type, const glm::vec2& pos = {0.0f, 0.0f})
            : ID(id), Type(type), EditorPosition(pos) {}
    };

    struct AudioGraphLink
    {
        int ID;
        int FromNodeID;
        int ToNodeID;

        AudioGraphLink(int id, int from, int to)
            : ID(id), FromNodeID(from), ToNodeID(to) {}
    };

    // Audio Components
    struct AudioSourceComponent
    {
        std::string FilePath;
        bool PlayOnAwake = false;
        bool Loop = false;
        bool Mute = false;
        
        float Volume = 1.0f;      // 0.0 - 1.0
        float Pitch = 1.0f;       // 0.5 - 2.0
        float Pan = 0.0f;         // -1.0 (sol) - 1.0 (sağ)
        
        // 3D Ses ayarları
        bool Is3D = true;
        float MinDistance = 1.0f;  // Ses tam volume olduğu mesafe
        float MaxDistance = 100.0f; // Ses duyulmaz olduğu mesafe
        float DopplerLevel = 1.0f; // Doppler effect şiddeti
        
        // Attenuation model
        enum class AttenuationModel { None = 0, Inverse, Linear, Exponential };
        AttenuationModel Attenuation = AttenuationModel::Inverse;
        float RolloffFactor = 1.0f;
        
        // Mikser çıkışı (AudioMixer grup adı)
        std::string OutputGroup = "Master";

        // Runtime data
        std::string LoadedPath;           // çalışma zamanı: yüklü klibin yolu (Clip değişince yeniden yüklenir)
        void* RuntimeSound = nullptr;
        bool IsPlaying = false;
        uint32_t RoutingGeneration = 0;
        
        // Node Graph Data
        std::vector<AudioGraphNode> GraphNodes;
        std::vector<AudioGraphLink> GraphLinks;
        int NextGraphNodeID = 1;
        int NextGraphLinkID = 1;
        bool GraphInitialized = false;
        bool NeedsReconnection = false;
        
        AudioSourceComponent() = default;
        AudioSourceComponent(const AudioSourceComponent&) = default;
    };

    struct AudioListenerComponent
    {
        // Sadece bir tane aktif listener olmalı
        bool IsActive = true;
        
        AudioListenerComponent() = default;
        AudioListenerComponent(const AudioListenerComponent&) = default;
    };

    // Audio Filter Components
    struct AudioChorusFilterComponent
    {
        bool Enabled = true;
        float WetMix = 0.5f;      // 0.0 - 1.0
        float DryMix = 0.5f;      // 0.0 - 1.0
        float Delay = 40.0f;      // ms
        float Depth = 0.03f;      // 0.0 - 1.0
        float Rate = 0.8f;        // Hz
        
        void* RuntimeFilter = nullptr;
        
        AudioChorusFilterComponent() = default;
        AudioChorusFilterComponent(const AudioChorusFilterComponent&) = default;
    };

    struct AudioDistortionFilterComponent
    {
        bool Enabled = true;
        float Level = 0.5f;       // 0.0 - 1.0
        
        void* RuntimeFilter = nullptr;
        
        AudioDistortionFilterComponent() = default;
        AudioDistortionFilterComponent(const AudioDistortionFilterComponent&) = default;
    };

    struct AudioEchoFilterComponent
    {
        bool Enabled = true;
        float Delay = 500.0f;     // ms
        float Decay = 0.5f;       // 0.0 - 1.0
        float WetMix = 1.0f;      // 0.0 - 1.0
        float DryMix = 1.0f;      // 0.0 - 1.0
        
        void* RuntimeFilter = nullptr;
        
        AudioEchoFilterComponent() = default;
        AudioEchoFilterComponent(const AudioEchoFilterComponent&) = default;
    };

    struct AudioHighPassFilterComponent
    {
        bool Enabled = true;
        float CutoffFrequency = 5000.0f; // Hz
        float Resonance = 1.0f;          // Q factor
        
        void* RuntimeFilter = nullptr;
        
        AudioHighPassFilterComponent() = default;
        AudioHighPassFilterComponent(const AudioHighPassFilterComponent&) = default;
    };

    struct AudioLowPassFilterComponent
    {
        bool Enabled = true;
        float CutoffFrequency = 5000.0f; // Hz
        float Resonance = 1.0f;          // Q factor
        
        void* RuntimeFilter = nullptr;
        
        AudioLowPassFilterComponent() = default;
        AudioLowPassFilterComponent(const AudioLowPassFilterComponent&) = default;
    };

    struct AudioReverbFilterComponent
    {
        bool Enabled = true;
        float DryLevel = 0.0f;           // dB
        float Room = -1000.0f;           // dB
        float RoomHF = -100.0f;          // dB
        float DecayTime = 1.49f;         // saniye
        float DecayHFRatio = 0.83f;
        float ReflectionsLevel = -2602.0f; // dB
        float ReflectionsDelay = 0.007f;   // saniye
        float ReverbLevel = 200.0f;        // dB
        float ReverbDelay = 0.011f;        // saniye
        float Diffusion = 100.0f;          // %
        float Density = 100.0f;            // %
        float HFReference = 5000.0f;       // Hz
        
        void* RuntimeFilter = nullptr;
        
        AudioReverbFilterComponent() = default;
        AudioReverbFilterComponent(const AudioReverbFilterComponent&) = default;
    };

    struct AudioReverbZoneComponent
    {
        bool Enabled = true;
        float MinDistance = 10.0f;
        float MaxDistance = 15.0f;
        
        // Reverb preset
        enum class ReverbPreset { 
            Generic = 0, PaddedCell, Room, Bathroom, LivingRoom, 
            StoneRoom, Auditorium, ConcertHall, Cave, Arena, 
            Hangar, CarpetedHallway, Hallway, StoneCorridor, 
            Alley, Forest, City, Mountains, Quarry, Plain, 
            ParkingLot, SewerPipe, Underwater, Custom 
        };
        ReverbPreset Preset = ReverbPreset::Generic;
        
        AudioReverbZoneComponent() = default;
        AudioReverbZoneComponent(const AudioReverbZoneComponent&) = default;
    };

    struct AudioGainComponent
    {
        bool Enabled = true;
        float Gain = 1.0f;
        void* RuntimeFilter = nullptr;
        AudioGainComponent() = default;
        AudioGainComponent(const AudioGainComponent&) = default;
    };

    struct AudioPanComponent
    {
        bool Enabled = true;
        float Pan = 0.0f; // -1.0 to 1.0
        void* RuntimeFilter = nullptr;
        AudioPanComponent() = default;
        AudioPanComponent(const AudioPanComponent&) = default;
    };
}

#include "Scene/PhysicsComponents.h"
#include "Scene/GameplayComponents.h"
#include "Grid/GridComponents.h"
