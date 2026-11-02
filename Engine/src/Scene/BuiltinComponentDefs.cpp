#include <sstream>
#include "Renderer/Utilities/Renderer3D/Material.h"
#include "Renderer/Utilities/Renderer3D/MaterialAsset.h"
#include "Scene/MeshRendererMaterial.h"
#include "Scene/ComponentRegistry.h"
#include "Scene/Components.h"
#include "Renderer/RHI/Texture.h"
#include "Core/Serialization/SceneSerializer.h"
#include "Renderer/Renderer2D.h"
#include "Renderer/Utilities/Renderer3D/ModelLoader.h"
#include "Core/Animation/AnimationController.h"

#include <IconsFontAwesome6.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>

// Elle yazılmış (özel serileştirme/denetçi) bileşenlerin betik erişimi için kaydı.
// Builtin(): genel serileştirme/kopya/denetçi bunları atlar; yalnızca ad/alan erişimi sağlanır
// (HasComponent("Rigidbody"), SetComponentField("Rigidbody", "Mass", 5) ...).
namespace Conqueror
{
    namespace
    {
        // "0:1,1,1,1; 1:1,1,1,0" <-> ColorGradient
        std::string FormatColorKeys(const ColorGradient& g)
        {
            std::string out;
            char buf[128];
            for (const auto& k : g.Keys)
            {
                std::snprintf(buf, sizeof(buf), "%s%g:%g,%g,%g,%g", out.empty() ? "" : "; ", k.Time, k.Color.r, k.Color.g, k.Color.b, k.Color.a);
                out += buf;
            }
            return out;
        }

        void ParseColorKeys(const std::string& text, ColorGradient& g)
        {
            std::vector<ColorGradient::Key> keys;
            size_t start = 0;
            while (start <= text.size())
            {
                size_t end = text.find(';', start);
                std::string part = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
                float t, r, gr, b, a = 1.0f;
                int n = std::sscanf(part.c_str(), " %f : %f , %f , %f , %f", &t, &r, &gr, &b, &a);
                if (n >= 4)
                    keys.push_back({ std::clamp(t, 0.0f, 1.0f), glm::vec4(r, gr, b, n >= 5 ? a : 1.0f) });
                if (end == std::string::npos)
                    break;
                start = end + 1;
            }
            if (!keys.empty())
            {
                g.Keys = std::move(keys);
                g.Sort();
            }
        }

        // "0:1; 1:0" <-> FloatCurve
        std::string FormatFloatKeys(const FloatCurve& c)
        {
            std::string out;
            char buf[64];
            for (const auto& k : c.Keys)
            {
                std::snprintf(buf, sizeof(buf), "%s%g:%g", out.empty() ? "" : "; ", k.Time, k.Value);
                out += buf;
            }
            return out;
        }

        void ParseFloatKeys(const std::string& text, FloatCurve& c)
        {
            std::vector<FloatCurve::Key> keys;
            size_t start = 0;
            while (start <= text.size())
            {
                size_t end = text.find(';', start);
                std::string part = text.substr(start, end == std::string::npos ? std::string::npos : end - start);
                float t, v;
                if (std::sscanf(part.c_str(), " %f : %f", &t, &v) == 2)
                    keys.push_back({ std::clamp(t, 0.0f, 1.0f), v });
                if (end == std::string::npos)
                    break;
                start = end + 1;
            }
            if (!keys.empty())
            {
                c.Keys = std::move(keys);
                c.Sort();
            }
        }
    }

    void RegisterBuiltinComponents()
    {
        ComponentDef<TransformComponent>("Transform", "TransformComponent", "Core", ICON_FA_UP_DOWN_LEFT_RIGHT).Builtin()
            .Field("Position", "Position", &TransformComponent::Position)
            .Field("Rotation", "Rotation", &TransformComponent::Rotation)   // radyan
            .Field("Scale", "Scale", &TransformComponent::Scale);

        ComponentDef<RigidbodyComponent>("Rigidbody", "RigidbodyComponent", "Physics", ICON_FA_WEIGHT_HANGING).Builtin()
            .Field("BodyType", "Body Type", &RigidbodyComponent::Type).Options({ "Static", "Dynamic", "Kinematic" })
            .Field("Mass", "Mass", &RigidbodyComponent::Mass)
            .Field("LinearDrag", "Linear Drag", &RigidbodyComponent::LinearDrag)
            .Field("AngularDrag", "Angular Drag", &RigidbodyComponent::AngularDrag)
            .Field("GravityScale", "Gravity Scale", &RigidbodyComponent::GravityScale)
            .Field("UseGravity", "Use Gravity", &RigidbodyComponent::UseGravity)
            .Field("FreezeRotation", "Freeze Rotation", &RigidbodyComponent::FreezeRotation)
            .Field("FreezePositionX", "Freeze Position X", &RigidbodyComponent::FreezePositionX)
            .Field("FreezePositionY", "Freeze Position Y", &RigidbodyComponent::FreezePositionY)
            .Field("FreezePositionZ", "Freeze Position Z", &RigidbodyComponent::FreezePositionZ)
            .Field("FreezeRotationX", "Freeze Rotation X", &RigidbodyComponent::FreezeRotationX)
            .Field("FreezeRotationY", "Freeze Rotation Y", &RigidbodyComponent::FreezeRotationY)
            .Field("FreezeRotationZ", "Freeze Rotation Z", &RigidbodyComponent::FreezeRotationZ)
            .Field("Interpolate", "Interpolate", &RigidbodyComponent::Interpolate)
            .Field("CollisionDetection", "Collision Detection", &RigidbodyComponent::CollisionDetection).Options({ "Discrete", "Continuous" })
            .Field("MaxAngularVelocity", "Max Angular Velocity", &RigidbodyComponent::MaxAngularVelocity)
            .Field("AllowSleep", "Allow Sleep", &RigidbodyComponent::AllowSleep)
            .Field("Friction", "Friction", &RigidbodyComponent::Friction)
            .Field("Restitution", "Restitution", &RigidbodyComponent::Restitution);

        ComponentDef<BoxColliderComponent>("Box Collider", "BoxColliderComponent", "Physics", ICON_FA_CUBE).Builtin()
            .Field("Offset", "Center", &BoxColliderComponent::Offset)
            .Field("Size", "Size", &BoxColliderComponent::Size)
            .Field("IsTrigger", "Is Trigger", &BoxColliderComponent::IsTrigger);
        ComponentDef<SphereColliderComponent>("Sphere Collider", "SphereColliderComponent", "Physics", ICON_FA_CIRCLE).Builtin()
            .Field("Offset", "Center", &SphereColliderComponent::Offset)
            .Field("Radius", "Radius", &SphereColliderComponent::Radius)
            .Field("IsTrigger", "Is Trigger", &SphereColliderComponent::IsTrigger);
        ComponentDef<CapsuleColliderComponent>("Capsule Collider", "CapsuleColliderComponent", "Physics", ICON_FA_CAPSULES).Builtin()
            .Field("Offset", "Center", &CapsuleColliderComponent::Offset)
            .Field("Radius", "Radius", &CapsuleColliderComponent::Radius)
            .Field("Height", "Height", &CapsuleColliderComponent::Height)
            .Field("Direction", "Direction", &CapsuleColliderComponent::Direction)
            .Field("IsTrigger", "Is Trigger", &CapsuleColliderComponent::IsTrigger);
        ComponentDef<MeshColliderComponent>("Mesh Collider", "MeshColliderComponent", "Physics", ICON_FA_DRAW_POLYGON).Builtin()
            .Field("IsConvex", "Convex", &MeshColliderComponent::IsConvex)
            .Field("IsTrigger", "Is Trigger", &MeshColliderComponent::IsTrigger);

        ComponentDef<RigidBody2DComponent>("Rigidbody 2D", "RigidBody2DComponent", "Physics 2D", ICON_FA_WEIGHT_HANGING).Builtin().ScriptName("Rigidbody2D")
            .Field("BodyType", "Body Type", &RigidBody2DComponent::Type).Options({ "Static", "Dynamic", "Kinematic" })
            .Field("Mass", "Mass", &RigidBody2DComponent::Mass)
            .Field("UseAutoMass", "Use Auto Mass", &RigidBody2DComponent::UseAutoMass)
            .Field("LinearDrag", "Linear Drag", &RigidBody2DComponent::LinearDrag)
            .Field("AngularDrag", "Angular Drag", &RigidBody2DComponent::AngularDrag)
            .Field("GravityScale", "Gravity Scale", &RigidBody2DComponent::GravityScale)
            .Field("FixedRotation", "Freeze Rotation", &RigidBody2DComponent::FixedRotation)
            .Field("Interpolate", "Interpolate", &RigidBody2DComponent::Interpolate)
            .Field("ContinuousCollision", "Continuous", &RigidBody2DComponent::ContinuousCollision)
            .Field("AllowSleep", "Allow Sleep", &RigidBody2DComponent::AllowSleep)
            .Field("PhysicsMaterial", "Material", &RigidBody2DComponent::PhysicsMaterial, FieldKind::Asset).Filter(".cqphysmat2d");
        ComponentDef<BoxCollider2DComponent>("Box Collider 2D", "BoxCollider2DComponent", "Physics 2D", ICON_FA_SQUARE).Builtin().ScriptName("BoxCollider2D")
            .Field("Offset", "Offset", &BoxCollider2DComponent::Offset)
            .Field("Size", "Size", &BoxCollider2DComponent::Size)
            .Field("IsTrigger", "Is Trigger", &BoxCollider2DComponent::IsTrigger)
            .Field("Density", "Density", &BoxCollider2DComponent::Density)
            .Field("Friction", "Friction", &BoxCollider2DComponent::Friction)
            .Field("Restitution", "Bounciness", &BoxCollider2DComponent::Restitution)
            .Field("PhysicsMaterial", "Material", &BoxCollider2DComponent::PhysicsMaterial, FieldKind::Asset).Filter(".cqphysmat2d");
        ComponentDef<CircleCollider2DComponent>("Circle Collider 2D", "CircleCollider2DComponent", "Physics 2D", ICON_FA_CIRCLE).Builtin().ScriptName("CircleCollider2D")
            .Field("Offset", "Offset", &CircleCollider2DComponent::Offset)
            .Field("Radius", "Radius", &CircleCollider2DComponent::Radius)
            .Field("IsTrigger", "Is Trigger", &CircleCollider2DComponent::IsTrigger)
            .Field("Density", "Density", &CircleCollider2DComponent::Density)
            .Field("Friction", "Friction", &CircleCollider2DComponent::Friction)
            .Field("Restitution", "Bounciness", &CircleCollider2DComponent::Restitution)
            .Field("PhysicsMaterial", "Material", &CircleCollider2DComponent::PhysicsMaterial, FieldKind::Asset).Filter(".cqphysmat2d");

        // Mesh Renderer: renk / doku / metalik ... malzemede. Bu adlar betikler, tween'ler ve otomasyon için malzemeye
        // yönlendirilir (Unity renderer.material.color): paylaşılan malzemeye yazınca nesneye özel örnek çıkar.
        {
            using MRM = MeshRendererComponent;
            namespace RM = MeshRendererMaterial;
            auto num = [](float v) { char b[32]; snprintf(b, sizeof(b), "%g", v); return std::string(b); };
            auto vec = [num](const float* v, int n) { std::string s; for (int i = 0; i < n; ++i) s += (i ? " " : "") + num(v[i]); return s; };
            auto parse = [](const std::string& t, float* out, int n) {
                std::string c = t;
                std::replace(c.begin(), c.end(), ',', ' ');
                std::istringstream in(c);
                for (int i = 0; i < n; ++i)
                    if (!(in >> out[i])) return i;
                return n;
            };
            auto boolOf = [](const std::string& t) { return t == "true" || t == "1" || t == "yes" || t == "on" || t == "True"; };
            auto texPath = [](const std::shared_ptr<Texture2D>& t) { return t ? SceneSerializer::ToAssetPath(t->GetPath()) : std::string(); };
            auto loadTex = [](const std::string& path) -> std::shared_ptr<Texture2D> {
                return path.empty() ? nullptr : Texture2D::Create(SceneSerializer::ResolveAssetPath(path));
            };
            ComponentDef<MRM>("Mesh Renderer", "MeshRendererComponent", "Rendering", ICON_FA_CUBES).Builtin()
                .Field("Mesh", "Mesh", &MRM::Type).Options({ "Cube", "Sphere", "Plane", "Cylinder" })
                .Property("Material", "Material", FieldKind::Asset,
                    [](const MRM& m) { return m.MaterialPath.empty() ? std::string() : SceneSerializer::ToAssetPath(m.MaterialPath); },
                    [](MRM& m, const std::string& path) {
                        if (path.empty()) { m.MaterialInstance = nullptr; m.MaterialPath.clear(); return; }
                        const std::string abs = SceneSerializer::ResolveAssetPath(path);
                        if (auto mat = MaterialAsset::Load(abs)) { m.MaterialInstance = mat; m.MaterialPath = abs; }
                    })
                .Field("CastShadows", "Cast Shadows", &MRM::CastShadows)
                .Field("ReceiveShadows", "Receive Shadows", &MRM::ReceiveShadows)
                .Field("RealtimeGI", "Realtime GI", &MRM::RealtimeGI)
                // --- malzeme özellikleri (renderer.material)
                .Property("Color", "Color", FieldKind::Color4,
                    [vec](const MRM& m) { glm::vec4 c = RM::GetColor(m); return vec(&c.x, 4); },
                    [parse](MRM& m, const std::string& t) { glm::vec4 c = RM::GetColor(m); parse(t, &c.x, 4); RM::SetColor(m, c); })
                .Property("Metallic", "Metallic", FieldKind::Float,
                    [num](const MRM& m) { return num(RM::Shared(m).Metallic); },
                    [](MRM& m, const std::string& t) { float v = std::strtof(t.c_str(), nullptr); if (RM::Shared(m).Metallic != v) RM::Edit(m).Metallic = v; })
                .Property("Roughness", "Roughness", FieldKind::Float,
                    [num](const MRM& m) { return num(RM::Shared(m).Roughness); },
                    [](MRM& m, const std::string& t) { float v = std::strtof(t.c_str(), nullptr); if (RM::Shared(m).Roughness != v) RM::Edit(m).Roughness = v; })
                .Property("AO", "Occlusion", FieldKind::Float,
                    [num](const MRM& m) { return num(RM::Shared(m).AO); },
                    [](MRM& m, const std::string& t) { float v = std::strtof(t.c_str(), nullptr); if (RM::Shared(m).AO != v) RM::Edit(m).AO = v; })
                .Property("EmissionColor", "Emission Color", FieldKind::Color3,
                    [vec](const MRM& m) { glm::vec3 c = RM::Shared(m).EmissionColor; return vec(&c.x, 3); },
                    [parse](MRM& m, const std::string& t) { glm::vec3 c = RM::Shared(m).EmissionColor; parse(t, &c.x, 3); if (c != RM::Shared(m).EmissionColor) RM::Edit(m).EmissionColor = c; })
                .Property("EmissionStrength", "Emission Intensity", FieldKind::Float,
                    [num](const MRM& m) { return num(RM::Shared(m).EmissionStrength); },
                    [](MRM& m, const std::string& t) { float v = std::strtof(t.c_str(), nullptr); if (RM::Shared(m).EmissionStrength != v) RM::Edit(m).EmissionStrength = v; })
                .Property("EmissionFromTexture", "Emission Uses Base Map", FieldKind::Bool,
                    [](const MRM& m) { const Material& x = RM::Shared(m); return std::string(x.EmissionMap && x.EmissionMap == x.AlbedoMap ? "true" : "false"); },
                    [boolOf](MRM& m, const std::string& t) {
                        const bool on = boolOf(t);
                        const Material& x = RM::Shared(m);
                        if ((x.EmissionMap && x.EmissionMap == x.AlbedoMap) == on) return;
                        Material& e = RM::Edit(m);
                        e.EmissionMap = on ? e.AlbedoMap : nullptr;
                    })
                .Property("Tiling", "Tiling", FieldKind::Vec2,
                    [vec](const MRM& m) { glm::vec2 v = RM::Shared(m).Tiling; return vec(&v.x, 2); },
                    [parse](MRM& m, const std::string& t) { glm::vec2 v = RM::Shared(m).Tiling; parse(t, &v.x, 2); if (v != RM::Shared(m).Tiling) RM::Edit(m).Tiling = v; })
                .Property("Offset", "Offset", FieldKind::Vec2,
                    [vec](const MRM& m) { glm::vec2 v = RM::Shared(m).Offset; return vec(&v.x, 2); },
                    [parse](MRM& m, const std::string& t) { glm::vec2 v = RM::Shared(m).Offset; parse(t, &v.x, 2); if (v != RM::Shared(m).Offset) RM::Edit(m).Offset = v; })
                .Property("WorldAlignedUV", "World Aligned UV", FieldKind::Bool,
                    [](const MRM& m) { return std::string(RM::Shared(m).WorldAligned ? "true" : "false"); },
                    [boolOf](MRM& m, const std::string& t) { bool v = boolOf(t); if (RM::Shared(m).WorldAligned != v) RM::Edit(m).WorldAligned = v; })
                .Property("AlignToObject", "Align To Object", FieldKind::Bool,
                    [](const MRM& m) { return std::string(RM::Shared(m).AlignToObject ? "true" : "false"); },
                    [boolOf](MRM& m, const std::string& t) { bool v = boolOf(t); if (RM::Shared(m).AlignToObject != v) RM::Edit(m).AlignToObject = v; })
                .Property("Unlit", "Unlit", FieldKind::Bool,
                    [](const MRM& m) { return std::string(RM::Shared(m).Shading == Material::ShadingModel::Unlit ? "true" : "false"); },
                    [boolOf](MRM& m, const std::string& t) {
                        const auto want = boolOf(t) ? Material::ShadingModel::Unlit : Material::ShadingModel::DefaultLit;
                        if (RM::Shared(m).Shading != want) RM::Edit(m).Shading = want;
                    })
                .Property("AlphaClip", "Alpha Clipping", FieldKind::Bool,
                    [](const MRM& m) { return std::string(RM::Shared(m).Rendering == Material::RenderMode::Cutout ? "true" : "false"); },
                    [boolOf](MRM& m, const std::string& t) {
                        const bool on = boolOf(t);
                        if ((RM::Shared(m).Rendering == Material::RenderMode::Cutout) == on) return;
                        Material& e = RM::Edit(m);
                        e.Rendering = on ? Material::RenderMode::Cutout : (e.Opacity < 0.999f ? Material::RenderMode::Transparent : Material::RenderMode::Opaque);
                    })
                .Property("AlphaClipThreshold", "Alpha Clip Threshold", FieldKind::Float,
                    [num](const MRM& m) { return num(RM::Shared(m).AlphaClip); },
                    [](MRM& m, const std::string& t) { float v = std::strtof(t.c_str(), nullptr); if (RM::Shared(m).AlphaClip != v) RM::Edit(m).AlphaClip = v; })
                .Property("Texture", "Base Map", FieldKind::Asset,
                    [texPath](const MRM& m) { return texPath(RM::Shared(m).AlbedoMap); },
                    [loadTex](MRM& m, const std::string& path) { RM::SetTexture(m, loadTex(path)); })
                .Property("NormalMap", "Normal Map", FieldKind::Asset,
                    [texPath](const MRM& m) { return texPath(RM::Shared(m).NormalMap); },
                    [loadTex](MRM& m, const std::string& path) { auto t = loadTex(path); if (RM::Shared(m).NormalMap != t) RM::Edit(m).NormalMap = t; })
                .Property("MaskMap", "Mask Map (AO / Rough / Metal)", FieldKind::Asset,
                    [texPath](const MRM& m) { const Material& x = RM::Shared(m); return x.MapsScaleFactors ? texPath(x.AOMap) : std::string(); },
                    [loadTex](MRM& m, const std::string& path) {
                        auto t = loadTex(path);
                        Material& e = RM::Edit(m);
                        e.AOMap = e.RoughnessMap = e.MetallicMap = t;
                        e.MapsScaleFactors = t != nullptr;
                        e.RoughnessChannel = t ? 1 : 0;
                        e.MetallicChannel = t ? 2 : 0;
                    });
        }
        ComponentDef<SpriteRendererComponent>("Sprite Renderer", "SpriteRendererComponent", "Rendering", ICON_FA_IMAGE).Builtin()
            .Field("Color", "Color", &SpriteRendererComponent::Color, FieldKind::Color4)
            .Field("FlipX", "Flip X", &SpriteRendererComponent::FlipX)
            .Field("FlipY", "Flip Y", &SpriteRendererComponent::FlipY)
            .Field("Size", "Size", &SpriteRendererComponent::Size)
            .Field("SpriteName", "Sprite", &SpriteRendererComponent::SpriteName)
            .Field("DrawMode", "Draw Mode", &SpriteRendererComponent::DrawMode).Options({ "Simple", "Sliced", "Tiled" })
            .Field("Pivot", "Pivot", &SpriteRendererComponent::Pivot)
            .Field("PixelsPerUnit", "Pixels Per Unit", &SpriteRendererComponent::PixelsPerUnit)
            .Field("Border", "Border", &SpriteRendererComponent::Border)          // 9-slice piksel: sol, alt, sağ, üst
            .Field("Rect", "Rect", &SpriteRendererComponent::Rect)                // alt dikdörtgen (piksel)
            .Field("TilingFactor", "Tiling", &SpriteRendererComponent::TilingFactor)
            .Field("MaskInteraction", "Mask Interaction", &SpriteRendererComponent::MaskInteraction).Options({ "None", "Visible Inside Mask", "Visible Outside Mask" })
            .Property("Texture", "Texture", FieldKind::Asset,
                [](const SpriteRendererComponent& sr) { return sr.Texture ? sr.Texture->GetPath() : std::string(); },
                [](SpriteRendererComponent& sr, const std::string& path) {
                    sr.Texture = path.empty() ? nullptr : Texture2D::Create(SceneSerializer::ResolveAssetPath(path));
                    sr.SpriteName.clear();
                    Renderer2D::ApplySpriteImport(sr);
                });
        ComponentDef<TextRendererComponent>("Text Renderer", "TextRendererComponent", "Rendering", ICON_FA_FONT).Builtin()
            .Field("Text", "Text", &TextRendererComponent::Text)
            .Field("Color", "Color", &TextRendererComponent::Color, FieldKind::Color4)
            .Field("FontSize", "Font Size", &TextRendererComponent::FontSize)
            .Field("Alignment", "Alignment", &TextRendererComponent::Alignment).Options({ "Left", "Center", "Right" });

        // Kamera (SceneCamera erişimcileri üzerinden): EntitySetComponentField(cam, "Camera", "OrthographicSize", 25)
        auto toText = [](float v) { char b[32]; std::snprintf(b, sizeof(b), "%g", v); return std::string(b); };
        // Model / Animation / Audio Listener: özel inspector + serileştirici; betikler ve otomasyon için alanlar
        ComponentDef<ModelComponent>("Model", "ModelComponent", "Rendering", ICON_FA_CUBES).Builtin()
            .Field("Tint", "Tint", &ModelComponent::Tint, FieldKind::Color3)
            .Property("FilePath", "Model", FieldKind::Asset,
                [](const ModelComponent& m) { return m.FilePath; },
                [](ModelComponent& m, const std::string& path) {
                    m.FilePath = path;
                    m.ModelData = path.empty() ? nullptr : ModelLoader::Load(SceneSerializer::ResolveAssetPath(path));
                });
        // Basit klip oynatıcı: Timeline Animation parçası bunu sürer (betikten EntityAddComponent(e, "Animator"))
        ComponentDef<AnimatorComponent>("Animator", "AnimatorComponent", "Animation", ICON_FA_PERSON_WALKING).Builtin()
            .Field("Playing", "Playing", &AnimatorComponent::Playing)
            .Field("Speed", "Speed", &AnimatorComponent::Speed)
            .Field("ActiveClipIndex", "Clip", &AnimatorComponent::ActiveClipIndex)
            .Field("CurrentTime", "Time", &AnimatorComponent::CurrentTime);
        ComponentDef<AnimationComponent>("Animation", "AnimationComponent", "Animation", ICON_FA_PERSON_RUNNING).Builtin()
            .Field("Speed", "Speed", &AnimationComponent::Speed)
            .Field("PlayOnAwake", "Play On Awake", &AnimationComponent::PlayOnAwake)
            .Field("ApplyRootMotion", "Apply Root Motion", &AnimationComponent::ApplyRootMotion)
            .Property("ControllerPath", "Controller", FieldKind::Asset,
                [](const AnimationComponent& a) { return a.ControllerFilePath; },
                [](AnimationComponent& a, const std::string& path) {
                    a.ControllerFilePath = path;
                    a.Controller = path.empty() ? nullptr : AnimationController::Deserialize(SceneSerializer::ResolveAssetPath(path));
                    a.LayerRuntime.clear();
                    a.Parameters.clear();
                });
        ComponentDef<AudioListenerComponent>("Audio Listener", "AudioListenerComponent", "Audio", ICON_FA_EAR_LISTEN).Builtin()
            .Field("IsActive", "Active", &AudioListenerComponent::IsActive);

        ComponentDef<CameraComponent>("Camera", "CameraComponent", "Rendering", ICON_FA_VIDEO).Builtin()
            .Field("Primary", "Primary", &CameraComponent::Primary)
            .Field("FixedAspectRatio", "Fixed Aspect Ratio", &CameraComponent::FixedAspectRatio)
            .Field("Background", "Background", &CameraComponent::Background).Options({ "Auto", "Skybox", "SolidColor" })
            .Field("BackgroundColor", "Background Color", &CameraComponent::BackgroundColor, FieldKind::Color4)
            .Property("Projection", "Projection", FieldKind::String,
                [](const CameraComponent& c) { return std::string(c.Camera.GetProjectionType() == SceneCamera::ProjectionType::Orthographic ? "Orthographic" : "Perspective"); },
                [](CameraComponent& c, const std::string& v) {
                    c.Camera.SetProjectionType(v == "Orthographic" || v == "orthographic" || v == "1" ? SceneCamera::ProjectionType::Orthographic : SceneCamera::ProjectionType::Perspective);
                })
            .Property("OrthographicSize", "Size", FieldKind::Float,
                [toText](const CameraComponent& c) { return toText(c.Camera.GetOrthographicSize()); },
                [](CameraComponent& c, const std::string& v) { c.Camera.SetOrthographicSize(std::max(0.01f, (float)std::atof(v.c_str()))); })
            .Property("FieldOfView", "Field of View", FieldKind::Float,
                [toText](const CameraComponent& c) { return toText(glm::degrees(c.Camera.GetPerspectiveVerticalFOV())); },
                [](CameraComponent& c, const std::string& v) { c.Camera.SetPerspectiveVerticalFOV(glm::radians(std::clamp((float)std::atof(v.c_str()), 1.0f, 179.0f))); })
            .Property("NearClip", "Near", FieldKind::Float,
                [toText](const CameraComponent& c) { return toText(c.Camera.GetPerspectiveNearClip()); },
                [](CameraComponent& c, const std::string& v) { c.Camera.SetPerspectiveNearClip(std::max(0.0001f, (float)std::atof(v.c_str()))); })
            .Property("FarClip", "Far", FieldKind::Float,
                [toText](const CameraComponent& c) { return toText(c.Camera.GetPerspectiveFarClip()); },
                [](CameraComponent& c, const std::string& v) { c.Camera.SetPerspectiveFarClip(std::max(0.01f, (float)std::atof(v.c_str()))); });

        ComponentDef<DirectionalLightComponent>("Directional Light", "DirectionalLightComponent", "Lighting", ICON_FA_SUN).Builtin()
            .Field("Color", "Color", &DirectionalLightComponent::Color, FieldKind::Color3)
            .Field("Intensity", "Intensity", &DirectionalLightComponent::Intensity)
            .Field("CastShadows", "Cast Shadows", &DirectionalLightComponent::CastShadows)
            .Field("Direction", "Direction", &DirectionalLightComponent::Direction).ReadOnly()
                .Tooltip("World direction the light shines towards; set it by rotating the object.");
        ComponentDef<PointLightComponent>("Point Light", "PointLightComponent", "Lighting", ICON_FA_LIGHTBULB).Builtin()
            .Field("Color", "Color", &PointLightComponent::Color, FieldKind::Color3)
            .Field("Intensity", "Intensity", &PointLightComponent::Intensity)
            .Field("Range", "Range", &PointLightComponent::Range)
            .Field("CastShadows", "Cast Shadows", &PointLightComponent::CastShadows);
        ComponentDef<SpotLightComponent>("Spot Light", "SpotLightComponent", "Lighting", ICON_FA_LIGHTBULB).Builtin()
            .Field("Color", "Color", &SpotLightComponent::Color, FieldKind::Color3)
            .Field("Intensity", "Intensity", &SpotLightComponent::Intensity)
            .Field("Range", "Range", &SpotLightComponent::Range)
            .Field("InnerConeAngle", "Inner Angle", &SpotLightComponent::InnerConeAngle)
            .Field("OuterConeAngle", "Outer Angle", &SpotLightComponent::OuterConeAngle)
            .Field("CastShadows", "Cast Shadows", &SpotLightComponent::CastShadows);

        ComponentDef<AudioSourceComponent>("Audio Source", "AudioSourceComponent", "Audio", ICON_FA_VOLUME_HIGH).Builtin()
            .Field("Clip", "Clip", &AudioSourceComponent::FilePath)
            .Field("PlayOnAwake", "Play On Awake", &AudioSourceComponent::PlayOnAwake)
            .Field("Loop", "Loop", &AudioSourceComponent::Loop)
            .Field("Mute", "Mute", &AudioSourceComponent::Mute)
            .Field("Volume", "Volume", &AudioSourceComponent::Volume)
            .Field("Pitch", "Pitch", &AudioSourceComponent::Pitch)
            .Field("Pan", "Pan", &AudioSourceComponent::Pan)
            .Field("Is3D", "3D", &AudioSourceComponent::Is3D)
            .Field("MinDistance", "Min Distance", &AudioSourceComponent::MinDistance)
            .Field("MaxDistance", "Max Distance", &AudioSourceComponent::MaxDistance)
            .Field("OutputGroup", "Output", &AudioSourceComponent::OutputGroup);

        // ---- UI (UGUI): EntitySetComponentField(id, "UIText", "Text", "Score: 10") ve Timeline animasyonu için
        auto resolveAsset = [](const std::string& path) { return path.empty() ? std::string() : SceneSerializer::ResolveAssetPath(path); };
        ComponentDef<CanvasComponent>("Canvas", "CanvasComponent", "UI", ICON_FA_TABLE_CELLS_LARGE).Builtin()
            .Field("RenderMode", "Render Mode", &CanvasComponent::Mode).Options({ "ScreenSpaceOverlay", "ScreenSpaceCamera", "WorldSpace" })
            .Field("PixelPerfect", "Pixel Perfect", &CanvasComponent::PixelPerfect)
            .Field("SortOrder", "Sort Order", &CanvasComponent::SortOrder);
        ComponentDef<CanvasScalerComponent>("Canvas Scaler", "CanvasScalerComponent", "UI", ICON_FA_MAXIMIZE).Builtin()
            .Field("ScaleMode", "Scale Mode", &CanvasScalerComponent::ScaleMode).Options({ "ConstantPixelSize", "ScaleWithScreenSize", "ConstantPhysicalSize" })
            .Field("ScaleFactor", "Scale Factor", &CanvasScalerComponent::ScaleFactor)
            .Field("ReferenceResolution", "Reference Resolution", &CanvasScalerComponent::ReferenceResolution)
            .Field("MatchWidthOrHeight", "Match", &CanvasScalerComponent::MatchWidthOrHeight);
        ComponentDef<RectTransformComponent>("Rect Transform", "RectTransformComponent", "UI", ICON_FA_VECTOR_SQUARE).Builtin()
            .Field("AnchorMin", "Anchor Min", &RectTransformComponent::AnchorMin)
            .Field("AnchorMax", "Anchor Max", &RectTransformComponent::AnchorMax)
            .Field("Pivot", "Pivot", &RectTransformComponent::Pivot)
            .Field("AnchoredPosition", "Position", &RectTransformComponent::AnchoredPosition)
            .Field("SizeDelta", "Size", &RectTransformComponent::SizeDelta)
            .Field("Rotation", "Rotation", &RectTransformComponent::Rotation)   // derece
            .Field("Scale", "Scale", &RectTransformComponent::Scale);
        ComponentDef<UIImageComponent>("Image", "UIImageComponent", "UI", ICON_FA_IMAGE).Builtin()
            .Field("Color", "Color", &UIImageComponent::Color, FieldKind::Color4)
            .Property("Texture", "Texture", FieldKind::Asset,
                [](const UIImageComponent& img) { return img.TexturePath; },
                [resolveAsset](UIImageComponent& img, const std::string& path) { img.TexturePath = resolveAsset(path); })
            .Field("SpriteName", "Sprite", &UIImageComponent::SpriteName)
            .Field("ImageType", "Image Type", &UIImageComponent::Type).Options({ "Simple", "Sliced", "Tiled", "Filled" })
            .Field("FillMethod", "Fill Method", &UIImageComponent::FillMethod).Options({ "Horizontal", "Vertical", "Radial360" })
            .Field("FillAmount", "Fill Amount", &UIImageComponent::FillAmount)
            .Field("FillClockwise", "Clockwise", &UIImageComponent::FillClockwise)
            .Field("PreserveAspect", "Preserve Aspect", &UIImageComponent::PreserveAspect)
            .Field("RaycastTarget", "Raycast Target", &UIImageComponent::RaycastTarget)
            .Field("CornerRadius", "Corner Radius", &UIImageComponent::CornerRadius);
        ComponentDef<UITextComponent>("Text", "UITextComponent", "UI", ICON_FA_FONT).Builtin()
            .Field("Text", "Text", &UITextComponent::Text)
            .Field("RichText", "Rich Text (BBCode)", &UITextComponent::RichText)
                .Tooltip("[b] [i] [u] [s] [color=#ff8800] [size=40] [wave] [shake] [rainbow] (also <b> <color=red> <size=150%>)")
            .Property("Font", "Font", FieldKind::Asset,
                [](const UITextComponent& t) { return t.FontPath; },
                [resolveAsset](UITextComponent& t, const std::string& path) { t.FontPath = resolveAsset(path); })
            .Field("FontSize", "Font Size", &UITextComponent::FontSize)
            .Field("Color", "Color", &UITextComponent::Color, FieldKind::Color4)
            .Field("AlignH", "Horizontal Align", &UITextComponent::AlignH).Options({ "Left", "Center", "Right" })
            .Field("AlignV", "Vertical Align", &UITextComponent::AlignV).Options({ "Top", "Middle", "Bottom" })
            .Field("WordWrap", "Word Wrap", &UITextComponent::WordWrap)
            .Field("BestFit", "Best Fit", &UITextComponent::BestFit)
            .Field("LineSpacing", "Line Spacing", &UITextComponent::LineSpacing)
            .Field("Outline", "Outline", &UITextComponent::Outline)
            .Field("OutlineColor", "Outline Color", &UITextComponent::OutlineColor, FieldKind::Color4)
            .Field("Shadow", "Shadow", &UITextComponent::Shadow)
            .Field("ShadowOffset", "Shadow Offset", &UITextComponent::ShadowOffset)
            .Field("ShadowColor", "Shadow Color", &UITextComponent::ShadowColor, FieldKind::Color4);
        ComponentDef<UIButtonComponent>("Button", "UIButtonComponent", "UI", ICON_FA_HAND_POINTER).Builtin()
            .Field("OnClick", "On Click", &UIButtonComponent::OnClick)
            .Field("ClickCount", "Click Count", &UIButtonComponent::ClickCount, FieldKind::Int).ReadOnly();
        ComponentDef<UIToggleComponent>("Toggle", "UIToggleComponent", "UI", ICON_FA_SQUARE_CHECK).Builtin()
            .Field("IsOn", "Is On", &UIToggleComponent::IsOn)
            .Field("Group", "Group", &UIToggleComponent::Group)
            .Field("OnValueChanged", "On Value Changed", &UIToggleComponent::OnValueChanged);
        ComponentDef<UISliderComponent>("Slider", "UISliderComponent", "UI", ICON_FA_SLIDERS).Builtin()
            .Field("Value", "Value", &UISliderComponent::Value)
            .Field("MinValue", "Min Value", &UISliderComponent::MinValue)
            .Field("MaxValue", "Max Value", &UISliderComponent::MaxValue)
            .Field("WholeNumbers", "Whole Numbers", &UISliderComponent::WholeNumbers)
            .Field("Direction", "Direction", &UISliderComponent::Direction).Options({ "LeftToRight", "RightToLeft", "BottomToTop", "TopToBottom" })
            .Field("BackgroundColor", "Background", &UISliderComponent::BackgroundColor, FieldKind::Color4)
            .Field("FillColor", "Fill", &UISliderComponent::FillColor, FieldKind::Color4)
            .Field("HandleColor", "Handle", &UISliderComponent::HandleColor, FieldKind::Color4)
            .Field("HandleSize", "Handle Size", &UISliderComponent::HandleSize)
            .Field("OnValueChanged", "On Value Changed", &UISliderComponent::OnValueChanged);
        ComponentDef<UIInputFieldComponent>("Input Field", "UIInputFieldComponent", "UI", ICON_FA_I_CURSOR).Builtin()
            .Field("Text", "Text", &UIInputFieldComponent::Text)
            .Field("Placeholder", "Placeholder", &UIInputFieldComponent::Placeholder)
            .Field("CharacterLimit", "Character Limit", &UIInputFieldComponent::CharacterLimit)
            .Field("ContentType", "Content Type", &UIInputFieldComponent::ContentType).Options({ "Standard", "IntegerNumber", "DecimalNumber", "Alphanumeric", "Password" })
            .Field("FontSize", "Font Size", &UIInputFieldComponent::FontSize)
            .Field("TextColor", "Text Color", &UIInputFieldComponent::TextColor, FieldKind::Color4)
            .Field("BackgroundColor", "Background", &UIInputFieldComponent::BackgroundColor, FieldKind::Color4)
            .Field("OnSubmit", "On Submit", &UIInputFieldComponent::OnSubmit)
            .Field("Focused", "Focused", &UIInputFieldComponent::Focused).ReadOnly();
        ComponentDef<UIDropdownComponent>("Dropdown", "UIDropdownComponent", "UI", ICON_FA_CARET_DOWN).Builtin()
            .Field("Value", "Value", &UIDropdownComponent::Value)
            .Field("FontSize", "Font Size", &UIDropdownComponent::FontSize)
            .Field("OnValueChanged", "On Value Changed", &UIDropdownComponent::OnValueChanged);
        ComponentDef<UIScrollRectComponent>("Scroll Rect", "UIScrollRectComponent", "UI", ICON_FA_SCROLL).Builtin()
            .Field("Horizontal", "Horizontal", &UIScrollRectComponent::Horizontal)
            .Field("Vertical", "Vertical", &UIScrollRectComponent::Vertical)
            .Field("ScrollSensitivity", "Scroll Sensitivity", &UIScrollRectComponent::ScrollSensitivity)
            .Field("Mask", "Mask", &UIScrollRectComponent::Mask)
            .Field("NormalizedPosition", "Normalized Position", &UIScrollRectComponent::NormalizedPosition)
            .Field("ShowScrollbar", "Show Scrollbar", &UIScrollRectComponent::ShowScrollbar)
            .Field("HorizontalScrollbar", "Horizontal Scrollbar", &UIScrollRectComponent::HorizontalScrollbar)
            .Field("VerticalScrollbar", "Vertical Scrollbar", &UIScrollRectComponent::VerticalScrollbar);
        // ── Unity UGUI tamamlayıcıları ──
        ComponentDef<UICanvasGroupComponent>("Canvas Group", "UICanvasGroupComponent", "UI", ICON_FA_LAYER_GROUP)
            .Describe("Fades and enables / disables a whole UI subtree (menus, pop-ups).")
            .Field("Alpha", "Alpha", &UICanvasGroupComponent::Alpha).Range(0.0f, 1.0f, 0.01f)
            .Field("Interactable", "Interactable", &UICanvasGroupComponent::Interactable)
            .Field("BlocksRaycasts", "Blocks Raycasts", &UICanvasGroupComponent::BlocksRaycasts)
            .Field("IgnoreParentGroups", "Ignore Parent Groups", &UICanvasGroupComponent::IgnoreParentGroups);
        ComponentDef<UIRectMask2DComponent>("Rect Mask 2D", "UIRectMask2DComponent", "UI", ICON_FA_CROP_SIMPLE)
            .Describe("Clips children to this rectangle (scroll views, panels).")
            .Field("Padding", "Padding", &UIRectMask2DComponent::Padding).Tooltip("Left, right, top, bottom (pixels).");
        ComponentDef<UIMaskComponent>("Mask", "UIMaskComponent", "UI", ICON_FA_MASKS_THEATER)
            .Describe("Clips children to this element's rectangle; the mask image itself can be hidden.")
            .Field("ShowMaskGraphic", "Show Mask Graphic", &UIMaskComponent::ShowMaskGraphic);
        ComponentDef<UIRawImageComponent>("Raw Image", "UIRawImageComponent", "UI", ICON_FA_IMAGE)
            .Describe("Shows any texture with a UV rectangle (video, render texture, atlas region).")
            .Field("TexturePath", "Texture", &UIRawImageComponent::TexturePath, FieldKind::Asset).Filter(".png;.jpg;.jpeg;.tga;.bmp")
            .Field("Color", "Color", &UIRawImageComponent::Color, FieldKind::Color4)
            .Field("UVRect", "UV Rect", &UIRawImageComponent::UVRect).Tooltip("x, y, width, height in 0..1 texture space.")
            .Field("RaycastTarget", "Raycast Target", &UIRawImageComponent::RaycastTarget);
        ComponentDef<UIContentSizeFitterComponent>("Content Size Fitter", "UIContentSizeFitterComponent", "UI", ICON_FA_EXPAND)
            .Describe("Resizes a Text element to its content.")
            .Field("HorizontalFit", "Horizontal Fit", &UIContentSizeFitterComponent::HorizontalFit).Options({ "Unconstrained", "Min Size", "Preferred Size" })
            .Field("VerticalFit", "Vertical Fit", &UIContentSizeFitterComponent::VerticalFit).Options({ "Unconstrained", "Min Size", "Preferred Size" });
        ComponentDef<UIAspectRatioFitterComponent>("Aspect Ratio Fitter", "UIAspectRatioFitterComponent", "UI", ICON_FA_RULER_COMBINED)
            .Describe("Keeps a width / height ratio, or fits / envelopes the parent.")
            .Field("Mode", "Aspect Mode", &UIAspectRatioFitterComponent::Mode).Options({ "None", "Width Controls Height", "Height Controls Width", "Fit In Parent", "Envelope Parent" })
            .Field("AspectRatio", "Aspect Ratio", &UIAspectRatioFitterComponent::AspectRatio).Range(0.01f, 100.0f, 0.01f);
        ComponentDef<UILayoutElementComponent>("Layout Element", "UILayoutElementComponent", "UI", ICON_FA_TABLE_CELLS)
            .Describe("Overrides how a Layout Group sizes this element (-1 = not set).")
            .Field("IgnoreLayout", "Ignore Layout", &UILayoutElementComponent::IgnoreLayout)
            .Field("MinWidth", "Min Width", &UILayoutElementComponent::MinWidth).Range(-1.0f, 10000.0f, 1.0f)
            .Field("MinHeight", "Min Height", &UILayoutElementComponent::MinHeight).Range(-1.0f, 10000.0f, 1.0f)
            .Field("PreferredWidth", "Preferred Width", &UILayoutElementComponent::PreferredWidth).Range(-1.0f, 10000.0f, 1.0f)
            .Field("PreferredHeight", "Preferred Height", &UILayoutElementComponent::PreferredHeight).Range(-1.0f, 10000.0f, 1.0f)
            .Field("FlexibleWidth", "Flexible Width", &UILayoutElementComponent::FlexibleWidth).Range(-1.0f, 100.0f, 0.1f)
            .Field("FlexibleHeight", "Flexible Height", &UILayoutElementComponent::FlexibleHeight).Range(-1.0f, 100.0f, 0.1f);
        ComponentDef<UIScrollbarComponent>("Scrollbar", "UIScrollbarComponent", "UI", ICON_FA_GRIP_LINES_VERTICAL)
            .Describe("Draggable scrollbar; link it to a Scroll Rect (Horizontal / Vertical Scrollbar fields).")
            .FieldVia("Interactable", "Interactable", [](UIScrollbarComponent& c) -> bool& { return c.Selectable.Interactable; })
            .Field("Direction", "Direction", &UIScrollbarComponent::Direction).Options({ "Left To Right", "Right To Left", "Bottom To Top", "Top To Bottom" })
            .Field("Value", "Value", &UIScrollbarComponent::Value).Range(0.0f, 1.0f, 0.01f)
            .Field("Size", "Size", &UIScrollbarComponent::Size).Range(0.0f, 1.0f, 0.01f)
            .Field("NumberOfSteps", "Number Of Steps", &UIScrollbarComponent::NumberOfSteps).Range(0.0f, 100.0f, 1.0f)
            .Field("BackgroundColor", "Background", &UIScrollbarComponent::BackgroundColor, FieldKind::Color4)
            .Field("HandleColor", "Handle", &UIScrollbarComponent::HandleColor, FieldKind::Color4)
            .Field("OnValueChanged", "On Value Changed", &UIScrollbarComponent::OnValueChanged);
        ComponentDef<UIEventTriggerComponent>("Event Trigger", "UIEventTriggerComponent", "UI", ICON_FA_BOLT)
            .Describe("Calls methods of this entity's scripts on pointer / navigation events (empty = ignored).")
            .Field("PointerEnter", "Pointer Enter", &UIEventTriggerComponent::PointerEnter)
            .Field("PointerExit", "Pointer Exit", &UIEventTriggerComponent::PointerExit)
            .Field("PointerDown", "Pointer Down", &UIEventTriggerComponent::PointerDown)
            .Field("PointerUp", "Pointer Up", &UIEventTriggerComponent::PointerUp)
            .Field("PointerClick", "Pointer Click", &UIEventTriggerComponent::PointerClick)
            .Field("Drag", "Drag", &UIEventTriggerComponent::Drag)
            .Field("Scroll", "Scroll", &UIEventTriggerComponent::Scroll)
            .Field("Select", "Select", &UIEventTriggerComponent::Select).Header("Navigation")
            .Field("Deselect", "Deselect", &UIEventTriggerComponent::Deselect)
            .Field("Submit", "Submit", &UIEventTriggerComponent::Submit)
            .Field("Cancel", "Cancel", &UIEventTriggerComponent::Cancel);
        ComponentDef<UIShadowComponent>("Shadow", "UIShadowComponent", "UI", ICON_FA_CLONE)
            .Describe("Draws the image or text again behind itself, offset and tinted (drop shadow).")
            .Field("EffectColor", "Effect Color", &UIShadowComponent::EffectColor, FieldKind::Color4)
            .Field("EffectDistance", "Effect Distance", &UIShadowComponent::EffectDistance).Speed(0.1f)
            .Field("UseGraphicAlpha", "Use Graphic Alpha", &UIShadowComponent::UseGraphicAlpha);
        ComponentDef<UIOutlineComponent>("Outline", "UIOutlineComponent", "UI", ICON_FA_SQUARE)
            .Describe("Draws the image or text four more times around itself (outline).")
            .Field("EffectColor", "Effect Color", &UIOutlineComponent::EffectColor, FieldKind::Color4)
            .Field("EffectDistance", "Effect Distance", &UIOutlineComponent::EffectDistance).Speed(0.1f)
            .Field("UseGraphicAlpha", "Use Graphic Alpha", &UIOutlineComponent::UseGraphicAlpha);
        ComponentDef<UIEventSystemComponent>("Event System", "UIEventSystemComponent", "UI", ICON_FA_GAMEPAD)
            .Describe("Keyboard / gamepad UI navigation settings (arrows / d-pad move, Enter / A submit, Esc / B cancel).")
            .Field("FirstSelected", "First Selected", &UIEventSystemComponent::FirstSelected)
            .Field("SendNavigationEvents", "Send Navigation Events", &UIEventSystemComponent::SendNavigationEvents)
            .Field("RepeatDelay", "Repeat Delay", &UIEventSystemComponent::RepeatDelay).Range(0.05f, 2.0f, 0.01f).Format("%.2f s")
            .Field("RepeatRate", "Repeat Rate", &UIEventSystemComponent::RepeatRate).Range(0.02f, 1.0f, 0.01f).Format("%.2f s");

        ComponentDef<UILayoutGroupComponent>("Layout Group", "UILayoutGroupComponent", "UI", ICON_FA_BARS).Builtin()
            .Field("LayoutType", "Type", &UILayoutGroupComponent::Type).Options({ "Horizontal", "Vertical", "Grid" })
            .Field("Padding", "Padding", &UILayoutGroupComponent::Padding)
            .Field("Spacing", "Spacing", &UILayoutGroupComponent::Spacing)
            .Field("CellSize", "Cell Size", &UILayoutGroupComponent::CellSize)
            .Field("GridSpacing", "Grid Spacing", &UILayoutGroupComponent::GridSpacing)
            .Field("Constraint", "Columns", &UILayoutGroupComponent::Constraint)
            .Field("ChildAlignment", "Child Alignment", &UILayoutGroupComponent::ChildAlignment)
                .Options({ "UpperLeft", "UpperCenter", "UpperRight", "MiddleLeft", "MiddleCenter", "MiddleRight", "LowerLeft", "LowerCenter", "LowerRight" })
            .Field("FitContent", "Fit Content", &UILayoutGroupComponent::FitContent);

        // ---- Particle System (Shuriken): ana modül, emisyon, şekil, oluşturucu
        ComponentDef<ParticleSystemComponent>("Particle System", "ParticleSystemComponent", "Effects", ICON_FA_FIRE).Builtin()
            .Field("Duration", "Duration", &ParticleSystemComponent::Duration)
            .Field("Looping", "Looping", &ParticleSystemComponent::Looping)
            .Field("Prewarm", "Prewarm", &ParticleSystemComponent::Prewarm)
            .Field("PlayOnAwake", "Play On Awake", &ParticleSystemComponent::PlayOnAwake)
            .Field("StopAction", "Stop Action", &ParticleSystemComponent::StopAction).Options({ "None", "Disable", "Destroy" })
            .Field("StartDelay", "Start Delay", &ParticleSystemComponent::StartDelay)
            .Field("StartLifetime", "Start Lifetime", &ParticleSystemComponent::StartLifetime)
            .Field("StartSpeed", "Start Speed", &ParticleSystemComponent::StartSpeed)
            .Field("StartSize", "Start Size", &ParticleSystemComponent::StartSize)
            .Field("StartRotation", "Start Rotation", &ParticleSystemComponent::StartRotation)
            .Field("StartColorA", "Start Color A", &ParticleSystemComponent::StartColorA, FieldKind::Color4)
            .Field("StartColorB", "Start Color B", &ParticleSystemComponent::StartColorB, FieldKind::Color4)
            .Field("GravityModifier", "Gravity Modifier", &ParticleSystemComponent::GravityModifier)
            .Field("SimulationSpace", "Simulation Space", &ParticleSystemComponent::SimulationSpace).Options({ "Local", "World" })
            .Field("SimulationSpeed", "Simulation Speed", &ParticleSystemComponent::SimulationSpeed)
            .Field("MaxParticles", "Max Particles", &ParticleSystemComponent::MaxParticles)
            .Field("EmissionEnabled", "Emission", &ParticleSystemComponent::EmissionEnabled)
            .Field("RateOverTime", "Rate Over Time", &ParticleSystemComponent::RateOverTime)
            .Field("RateOverDistance", "Rate Over Distance", &ParticleSystemComponent::RateOverDistance)
            .Field("ShapeEnabled", "Shape", &ParticleSystemComponent::ShapeEnabled)
            .Field("Shape", "Shape Type", &ParticleSystemComponent::Shape).Options({ "Sphere", "Hemisphere", "Cone", "Box", "Circle", "Edge", "Point" })
            .Field("Radius", "Radius", &ParticleSystemComponent::Radius)
            .Field("Angle", "Angle", &ParticleSystemComponent::Angle)
            .Field("BoxSize", "Box Size", &ParticleSystemComponent::BoxSize)
            .Field("EmitFromShell", "Emit From Shell", &ParticleSystemComponent::EmitFromShell)
            .Field("RandomizeDirection", "Randomize Direction", &ParticleSystemComponent::RandomizeDirection)
            .Field("VelocityEnabled", "Velocity Over Lifetime", &ParticleSystemComponent::VelocityEnabled)
            .Field("VelocityOverLifetime", "Velocity", &ParticleSystemComponent::VelocityOverLifetime)
            .Field("Drag", "Drag", &ParticleSystemComponent::Drag)
            .Field("NoiseEnabled", "Noise", &ParticleSystemComponent::NoiseEnabled)
            .Field("NoiseStrength", "Noise Strength", &ParticleSystemComponent::NoiseStrength)
            .Field("ColorOverLifetimeEnabled", "Color Over Lifetime", &ParticleSystemComponent::ColorOverLifetimeEnabled)
            .Field("SizeOverLifetimeEnabled", "Size Over Lifetime", &ParticleSystemComponent::SizeOverLifetimeEnabled)
            .Field("RotationOverLifetimeEnabled", "Rotation Over Lifetime", &ParticleSystemComponent::RotationOverLifetimeEnabled)
            .Field("AngularVelocity", "Angular Velocity", &ParticleSystemComponent::AngularVelocity)
            .Field("CollisionEnabled", "Collision", &ParticleSystemComponent::CollisionEnabled)
            .Field("CollisionPlaneY", "Collision Plane Y", &ParticleSystemComponent::CollisionPlaneY)
            .Field("Bounce", "Bounce", &ParticleSystemComponent::Bounce)
            .Field("RenderMode", "Render Mode", &ParticleSystemComponent::RenderMode).Options({ "Billboard", "StretchedBillboard", "HorizontalBillboard", "VerticalBillboard" })
            .Field("Blend", "Blend", &ParticleSystemComponent::Blend).Options({ "Alpha", "Additive", "Premultiplied" })
            .Property("Texture", "Texture", FieldKind::Asset,
                [](const ParticleSystemComponent& ps) { return ps.TexturePath; },
                [resolveAsset](ParticleSystemComponent& ps, const std::string& path) { ps.TexturePath = resolveAsset(path); })
            .Field("SortByDistance", "Sort By Distance", &ParticleSystemComponent::SortByDistance)
            .Field("EmissiveIntensity", "Emissive Intensity", &ParticleSystemComponent::EmissiveIntensity)
            // Ömür eğrileri metin olarak (betik / Timeline): "zaman:r,g,b,a; ..." ve "zaman:değer; ..." (zaman 0..1)
            .Property("ColorOverLifetimeKeys", "Color Keys", FieldKind::String,
                [](const ParticleSystemComponent& ps) { return FormatColorKeys(ps.ColorOverLifetime); },
                [](ParticleSystemComponent& ps, const std::string& text) { ParseColorKeys(text, ps.ColorOverLifetime); })
            .Property("SizeOverLifetimeKeys", "Size Keys", FieldKind::String,
                [](const ParticleSystemComponent& ps) { return FormatFloatKeys(ps.SizeOverLifetime); },
                [](ParticleSystemComponent& ps, const std::string& text) { ParseFloatKeys(text, ps.SizeOverLifetime); });
    }
}
