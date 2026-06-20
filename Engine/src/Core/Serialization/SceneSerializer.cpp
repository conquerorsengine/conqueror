#include "SceneSerializer.h"
#include "Scene/Entity.h"
#include "Scene/Components.h"
#include "Scene/EditorCamera.h"
#include "Core/Logging/Log.h"
#include "Renderer/RHI/Texture.h"
#include "Renderer/RHI/Cubemap.h"
#include "Renderer/Utilities/Renderer3D/ModelLoader.h"
#include "Scripting/ScriptEngine.h"
#include "AISystem/Navigation/NavMesh.h"

#include <yaml-cpp/yaml.h>
#include <fstream>

namespace YAML
{
    template<>
    struct convert<glm::vec2>
    {
        static Node encode(const glm::vec2& rhs)
        {
            Node node;
            node.push_back(rhs.x);
            node.push_back(rhs.y);
            return node;
        }

        static bool decode(const Node& node, glm::vec2& rhs)
        {
            if (!node.IsSequence() || node.size() != 2)
                return false;

            rhs.x = node[0].as<float>();
            rhs.y = node[1].as<float>();
            return true;
        }
    };

    template<>
    struct convert<glm::vec3>
    {
        static Node encode(const glm::vec3& rhs)
        {
            Node node;
            node.push_back(rhs.x);
            node.push_back(rhs.y);
            node.push_back(rhs.z);
            return node;
        }

        static bool decode(const Node& node, glm::vec3& rhs)
        {
            if (!node.IsSequence() || node.size() != 3)
                return false;

            rhs.x = node[0].as<float>();
            rhs.y = node[1].as<float>();
            rhs.z = node[2].as<float>();
            return true;
        }
    };

    template<>
    struct convert<glm::vec4>
    {
        static Node encode(const glm::vec4& rhs)
        {
            Node node;
            node.push_back(rhs.x);
            node.push_back(rhs.y);
            node.push_back(rhs.z);
            node.push_back(rhs.w);
            return node;
        }

        static bool decode(const Node& node, glm::vec4& rhs)
        {
            if (!node.IsSequence() || node.size() != 4)
                return false;

            rhs.x = node[0].as<float>();
            rhs.y = node[1].as<float>();
            rhs.z = node[2].as<float>();
            rhs.w = node[3].as<float>();
            return true;
        }
    };
}

namespace Conqueror
{
    YAML::Emitter& operator<<(YAML::Emitter& out, const glm::vec2& v)
    {
        out << YAML::Flow;
        out << YAML::BeginSeq << v.x << v.y << YAML::EndSeq;
        return out;
    }

    YAML::Emitter& operator<<(YAML::Emitter& out, const glm::vec3& v)
    {
        out << YAML::Flow;
        out << YAML::BeginSeq << v.x << v.y << v.z << YAML::EndSeq;
        return out;
    }

    YAML::Emitter& operator<<(YAML::Emitter& out, const glm::vec4& v)
    {
        out << YAML::Flow;
        out << YAML::BeginSeq << v.x << v.y << v.z << v.w << YAML::EndSeq;
        return out;
    }

    SceneSerializer::SceneSerializer(const std::shared_ptr<Scene>& scene)
        : m_Scene(scene)
    {
    }

    static void SerializeEntity(YAML::Emitter& out, Entity entity)
    {
        out << YAML::BeginMap; // Entity
        out << YAML::Key << "Entity" << YAML::Value << entity.GetComponent<IDComponent>().ID;

        if (entity.HasComponent<TagComponent>())
        {
            out << YAML::Key << "TagComponent";
            out << YAML::BeginMap;

            auto& tag = entity.GetComponent<TagComponent>();
            out << YAML::Key << "Tag" << YAML::Value << tag.Tag;
            out << YAML::Key << "GameTag" << YAML::Value << tag.GameTag;

            out << YAML::EndMap;
        }

        if (entity.HasComponent<TransformComponent>())
        {
            out << YAML::Key << "TransformComponent";
            out << YAML::BeginMap;

            auto& tc = entity.GetComponent<TransformComponent>();
            out << YAML::Key << "Position" << YAML::Value << tc.Position;
            out << YAML::Key << "Rotation" << YAML::Value << tc.Rotation;
            out << YAML::Key << "Scale" << YAML::Value << tc.Scale;

            out << YAML::EndMap;
        }

        if (entity.HasComponent<CameraComponent>())
        {
            out << YAML::Key << "CameraComponent";
            out << YAML::BeginMap;

            auto& cameraComponent = entity.GetComponent<CameraComponent>();
            auto& camera = cameraComponent.Camera;

            out << YAML::Key << "Camera" << YAML::Value;
            out << YAML::BeginMap;
            out << YAML::Key << "ProjectionType" << YAML::Value << (int)camera.GetProjectionType();
            out << YAML::Key << "PerspectiveFOV" << YAML::Value << camera.GetPerspectiveVerticalFOV();
            out << YAML::Key << "PerspectiveNear" << YAML::Value << camera.GetPerspectiveNearClip();
            out << YAML::Key << "PerspectiveFar" << YAML::Value << camera.GetPerspectiveFarClip();
            out << YAML::Key << "OrthographicSize" << YAML::Value << camera.GetOrthographicSize();
            out << YAML::Key << "OrthographicNear" << YAML::Value << camera.GetOrthographicNearClip();
            out << YAML::Key << "OrthographicFar" << YAML::Value << camera.GetOrthographicFarClip();
            out << YAML::EndMap;

            out << YAML::Key << "Primary" << YAML::Value << cameraComponent.Primary;
            out << YAML::Key << "FixedAspectRatio" << YAML::Value << cameraComponent.FixedAspectRatio;

            out << YAML::EndMap;
        }

        if (entity.HasComponent<SpriteRendererComponent>())
        {
            out << YAML::Key << "SpriteRendererComponent";
            out << YAML::BeginMap;

            auto& spriteRenderer = entity.GetComponent<SpriteRendererComponent>();
            out << YAML::Key << "Color" << YAML::Value << spriteRenderer.Color;
            out << YAML::Key << "TilingFactor" << YAML::Value << spriteRenderer.TilingFactor;
            
            // Texture path kaydet
            if (spriteRenderer.Texture)
            {
                out << YAML::Key << "TexturePath" << YAML::Value << spriteRenderer.Texture->GetPath();
            }

            out << YAML::EndMap;
        }

        if (entity.HasComponent<LayerComponent>())
        {
            out << YAML::Key << "LayerComponent";
            out << YAML::BeginMap;

            auto& layer = entity.GetComponent<LayerComponent>();
            out << YAML::Key << "Layer" << YAML::Value << layer.Layer;
            out << YAML::Key << "OrderInLayer" << YAML::Value << layer.OrderInLayer;

            out << YAML::EndMap;
        }

        if (entity.HasComponent<TextRendererComponent>())
        {
            out << YAML::Key << "TextRendererComponent";
            out << YAML::BeginMap;

            auto& textRenderer = entity.GetComponent<TextRendererComponent>();
            out << YAML::Key << "Text" << YAML::Value << textRenderer.Text;
            out << YAML::Key << "Color" << YAML::Value << textRenderer.Color;
            out << YAML::Key << "FontSize" << YAML::Value << textRenderer.FontSize;

            out << YAML::EndMap;
        }

        if (entity.HasComponent<NavMeshAgentComponent>())
        {
            out << YAML::Key << "NavMeshAgentComponent";
            out << YAML::BeginMap;
            auto& comp = entity.GetComponent<NavMeshAgentComponent>();
            out << YAML::Key << "Speed" << YAML::Value << comp.Speed;
            out << YAML::Key << "AngularSpeed" << YAML::Value << comp.AngularSpeed;
            out << YAML::Key << "Acceleration" << YAML::Value << comp.Acceleration;
            out << YAML::Key << "StoppingDistance" << YAML::Value << comp.StoppingDistance;
            out << YAML::Key << "Radius" << YAML::Value << comp.Radius;
            out << YAML::Key << "Height" << YAML::Value << comp.Height;
            out << YAML::Key << "AutoBraking" << YAML::Value << comp.AutoBraking;
            out << YAML::EndMap;
        }

        if (entity.HasComponent<NavMeshObstacleComponent>())
        {
            out << YAML::Key << "NavMeshObstacleComponent";
            out << YAML::BeginMap;
            auto& comp = entity.GetComponent<NavMeshObstacleComponent>();
            out << YAML::Key << "Carve" << YAML::Value << comp.Carve;
            out << YAML::Key << "Radius" << YAML::Value << comp.Radius;
            out << YAML::Key << "Size" << YAML::Value << comp.Size;
            out << YAML::Key << "Center" << YAML::Value << comp.Center;
            out << YAML::EndMap;
        }

        if (entity.HasComponent<NavMeshSurfaceComponent>())
        {
            out << YAML::Key << "NavMeshSurfaceComponent";
            out << YAML::BeginMap;
            auto& comp = entity.GetComponent<NavMeshSurfaceComponent>();
            out << YAML::Key << "AreaType" << YAML::Value << comp.AreaType;
            out << YAML::Key << "OverrideVoxelSize" << YAML::Value << comp.OverrideVoxelSize;
            out << YAML::Key << "VoxelSize" << YAML::Value << comp.VoxelSize;
            out << YAML::EndMap;
        }

        if (entity.HasComponent<MeshRendererComponent>())
        {
            out << YAML::Key << "MeshRendererComponent";
            out << YAML::BeginMap;

            auto& meshRenderer = entity.GetComponent<MeshRendererComponent>();
            out << YAML::Key << "MeshType" << YAML::Value << static_cast<int>(meshRenderer.Type);
            out << YAML::Key << "Color" << YAML::Value << meshRenderer.Color;
            out << YAML::Key << "Metallic" << YAML::Value << meshRenderer.Metallic;
            out << YAML::Key << "Roughness" << YAML::Value << meshRenderer.Roughness;
            out << YAML::Key << "AO" << YAML::Value << meshRenderer.AO;
            out << YAML::Key << "TexturePath" << YAML::Value << meshRenderer.TexturePath;

            if (meshRenderer.MaterialInstance)
            {
                out << YAML::Key << "Material";
                out << YAML::BeginMap;
                auto& mat = meshRenderer.MaterialInstance;
                out << YAML::Key << "Name" << YAML::Value << mat->Name;
                out << YAML::Key << "Albedo" << YAML::Value << mat->Albedo;
                out << YAML::Key << "Metallic" << YAML::Value << mat->Metallic;
                out << YAML::Key << "Roughness" << YAML::Value << mat->Roughness;
                out << YAML::Key << "AO" << YAML::Value << mat->AO;
                out << YAML::Key << "EmissionColor" << YAML::Value << mat->EmissionColor;
                out << YAML::Key << "EmissionStrength" << YAML::Value << mat->EmissionStrength;
                out << YAML::Key << "Tiling" << YAML::Value << mat->Tiling;
                out << YAML::Key << "Offset" << YAML::Value << mat->Offset;
                
                // Shader Graph
                out << YAML::Key << "NextGraphNodeID" << YAML::Value << mat->NextGraphNodeID;
                out << YAML::Key << "NextGraphLinkID" << YAML::Value << mat->NextGraphLinkID;
                out << YAML::Key << "GraphInitialized" << YAML::Value << mat->GraphInitialized;

                out << YAML::Key << "GraphNodes" << YAML::Value << YAML::BeginSeq;
                for (const auto& node : mat->GraphNodes)
                {
                    out << YAML::BeginMap;
                    out << YAML::Key << "ID" << YAML::Value << node.ID;
                    out << YAML::Key << "Type" << YAML::Value << (int)node.Type;
                    out << YAML::Key << "EditorPosX" << YAML::Value << node.EditorPosition.x;
                    out << YAML::Key << "EditorPosY" << YAML::Value << node.EditorPosition.y;
                    
                    // Values
                    out << YAML::Key << "ColorValue" << YAML::Value << node.ColorValue;
                    out << YAML::Key << "FloatValue" << YAML::Value << node.FloatValue;
                    out << YAML::Key << "Vec2Value" << YAML::Value << node.Vec2Value;
                    out << YAML::Key << "Vec3Value" << YAML::Value << node.Vec3Value;
                    out << YAML::Key << "TexturePath" << YAML::Value << node.TexturePath;
                    
                    out << YAML::EndMap;
                }
                out << YAML::EndSeq;

                out << YAML::Key << "GraphLinks" << YAML::Value << YAML::BeginSeq;
                for (const auto& link : mat->GraphLinks)
                {
                    out << YAML::BeginMap;
                    out << YAML::Key << "ID" << YAML::Value << link.ID;
                    out << YAML::Key << "From" << YAML::Value << link.FromNodeID;
                    out << YAML::Key << "FromPin" << YAML::Value << link.FromPinIndex;
                    out << YAML::Key << "To" << YAML::Value << link.ToNodeID;
                    out << YAML::Key << "ToPin" << YAML::Value << link.ToPinIndex;
                    out << YAML::EndMap;
                }
                out << YAML::EndSeq;

                out << YAML::EndMap;
            }

            out << YAML::EndMap;
        }

        if (entity.HasComponent<DirectionalLightComponent>())
        {
            out << YAML::Key << "DirectionalLightComponent";
            out << YAML::BeginMap;

            auto& light = entity.GetComponent<DirectionalLightComponent>();
            out << YAML::Key << "Direction" << YAML::Value << light.Direction;
            out << YAML::Key << "Color" << YAML::Value << light.Color;
            out << YAML::Key << "Intensity" << YAML::Value << light.Intensity;
            out << YAML::Key << "CastShadows" << YAML::Value << light.CastShadows;

            out << YAML::EndMap;
        }

        if (entity.HasComponent<PointLightComponent>())
        {
            out << YAML::Key << "PointLightComponent";
            out << YAML::BeginMap;

            auto& light = entity.GetComponent<PointLightComponent>();
            out << YAML::Key << "Color" << YAML::Value << light.Color;
            out << YAML::Key << "Intensity" << YAML::Value << light.Intensity;
            out << YAML::Key << "Range" << YAML::Value << light.Range;
            out << YAML::Key << "Constant" << YAML::Value << light.Constant;
            out << YAML::Key << "Linear" << YAML::Value << light.Linear;
            out << YAML::Key << "Quadratic" << YAML::Value << light.Quadratic;
            out << YAML::Key << "CastShadows" << YAML::Value << light.CastShadows;

            out << YAML::EndMap;
        }

        if (entity.HasComponent<SpotLightComponent>())
        {
            out << YAML::Key << "SpotLightComponent";
            out << YAML::BeginMap;

            auto& light = entity.GetComponent<SpotLightComponent>();
            out << YAML::Key << "Direction" << YAML::Value << light.Direction;
            out << YAML::Key << "Color" << YAML::Value << light.Color;
            out << YAML::Key << "Intensity" << YAML::Value << light.Intensity;
            out << YAML::Key << "Range" << YAML::Value << light.Range;
            out << YAML::Key << "InnerConeAngle" << YAML::Value << light.InnerConeAngle;
            out << YAML::Key << "OuterConeAngle" << YAML::Value << light.OuterConeAngle;
            out << YAML::Key << "Constant" << YAML::Value << light.Constant;
            out << YAML::Key << "Linear" << YAML::Value << light.Linear;
            out << YAML::Key << "Quadratic" << YAML::Value << light.Quadratic;
            out << YAML::Key << "CastShadows" << YAML::Value << light.CastShadows;

            out << YAML::EndMap;
        }

        if (entity.HasComponent<NativeScriptComponent>())
        {
            auto& sc = entity.GetComponent<NativeScriptComponent>();
            for (const auto& script : sc.Scripts)
            {
                std::string keyName = script.ClassName.empty() ? "NativeScriptComponent" : script.ClassName + "_NativeScriptComponent";
                out << YAML::Key << keyName;
                out << YAML::BeginMap;
                out << YAML::Key << "ModuleName" << YAML::Value << script.ModuleName;
                out << YAML::Key << "ClassName" << YAML::Value << script.ClassName;
                out << YAML::Key << "ScriptPath" << YAML::Value << script.ScriptPath;
                out << YAML::EndMap;
            }
        }

        if (entity.HasComponent<ConquerorScriptComponent>())
        {
            auto& sc = entity.GetComponent<ConquerorScriptComponent>();
            for (const auto& script : sc.Scripts)
            {
                std::string keyName = script.ClassName.empty() ? "ConquerorScriptComponent" : script.ClassName + "_ConquerorScriptComponent";
                out << YAML::Key << keyName;
                out << YAML::BeginMap;
                out << YAML::Key << "ScriptPath" << YAML::Value << script.ScriptPath;
                out << YAML::Key << "ClassName" << YAML::Value << script.ClassName;
                out << YAML::EndMap;
            }
        }

        if (entity.HasComponent<ModelComponent>())
        {
            out << YAML::Key << "ModelComponent";
            out << YAML::BeginMap;

            auto& model = entity.GetComponent<ModelComponent>();
            out << YAML::Key << "FilePath" << YAML::Value << model.FilePath;

            out << YAML::EndMap;
        }

        if (entity.HasComponent<AnimatorComponent>())
        {
            out << YAML::Key << "AnimatorComponent";
            out << YAML::BeginMap;
            auto& anim = entity.GetComponent<AnimatorComponent>();
            out << YAML::Key << "Playing" << YAML::Value << anim.Playing;
            out << YAML::Key << "Speed" << YAML::Value << anim.Speed;
            out << YAML::Key << "ActiveClipIndex" << YAML::Value << anim.ActiveClipIndex;
            out << YAML::Key << "CurrentTime" << YAML::Value << anim.CurrentTime;
            out << YAML::EndMap;
        }

        if (entity.HasComponent<ImageComponent>())
        {
            out << YAML::Key << "ImageComponent";
            out << YAML::BeginMap;

            auto& image = entity.GetComponent<ImageComponent>();
            out << YAML::Key << "Color" << YAML::Value << image.Color;
            out << YAML::Key << "TilingFactor" << YAML::Value << image.TilingFactor;
            
            // Texture path kaydet
            if (image.Texture)
            {
                out << YAML::Key << "TexturePath" << YAML::Value << image.Texture->GetPath();
            }

            out << YAML::EndMap;
        }

        if (entity.HasComponent<ButtonComponent>())
        {
            out << YAML::Key << "ButtonComponent";
            out << YAML::BeginMap;

            auto& button = entity.GetComponent<ButtonComponent>();
            out << YAML::Key << "Interactable" << YAML::Value << button.Interactable;
            out << YAML::Key << "Navigation" << YAML::Value << (int)button.Navigation;
            out << YAML::Key << "Use9Slice" << YAML::Value << button.Use9Slice;
            out << YAML::Key << "BorderSize" << YAML::Value << button.BorderSize;
            out << YAML::Key << "ColorMultiplier" << YAML::Value << button.ColorMultiplier;
            out << YAML::Key << "TransitionSpeed" << YAML::Value << button.TransitionSpeed;
            
            // Renkler
            out << YAML::Key << "BackgroundNormalColor" << YAML::Value << button.BackgroundNormalColor;
            out << YAML::Key << "BackgroundHoverColor" << YAML::Value << button.BackgroundHoverColor;
            out << YAML::Key << "BackgroundPressedColor" << YAML::Value << button.BackgroundPressedColor;
            out << YAML::Key << "BackgroundDisabledColor" << YAML::Value << button.BackgroundDisabledColor;
            out << YAML::Key << "BackgroundHighlightedColor" << YAML::Value << button.BackgroundHighlightedColor;
            
            out << YAML::Key << "TextNormalColor" << YAML::Value << button.TextNormalColor;
            out << YAML::Key << "TextHoverColor" << YAML::Value << button.TextHoverColor;
            out << YAML::Key << "TextPressedColor" << YAML::Value << button.TextPressedColor;
            out << YAML::Key << "TextDisabledColor" << YAML::Value << button.TextDisabledColor;
            out << YAML::Key << "TextHighlightedColor" << YAML::Value << button.TextHighlightedColor;

            out << YAML::EndMap;
        }

        if (entity.HasComponent<CanvasComponent>())
        {
            out << YAML::Key << "CanvasComponent";
            out << YAML::BeginMap;

            auto& canvas = entity.GetComponent<CanvasComponent>();
            out << YAML::Key << "RenderMode" << YAML::Value << (int)canvas.Mode;
            out << YAML::Key << "PixelPerfect" << YAML::Value << canvas.PixelPerfect;
            out << YAML::Key << "SortOrder" << YAML::Value << canvas.SortOrder;

            out << YAML::EndMap;
        }

        if (entity.HasComponent<CanvasScalerComponent>())
        {
            out << YAML::Key << "CanvasScalerComponent";
            out << YAML::BeginMap;

            auto& scaler = entity.GetComponent<CanvasScalerComponent>();
            out << YAML::Key << "ScaleMode" << YAML::Value << (int)scaler.ScaleMode;
            out << YAML::Key << "ScaleFactor" << YAML::Value << scaler.ScaleFactor;
            out << YAML::Key << "ReferencePixelsPerUnit" << YAML::Value << scaler.ReferencePixelsPerUnit;

            out << YAML::EndMap;
        }

        if (entity.HasComponent<GraphicRaycasterComponent>())
        {
            out << YAML::Key << "GraphicRaycasterComponent";
            out << YAML::BeginMap;

            auto& raycaster = entity.GetComponent<GraphicRaycasterComponent>();
            out << YAML::Key << "IgnoreReversedGraphics" << YAML::Value << raycaster.IgnoreReversedGraphics;
            out << YAML::Key << "BlockingObjects" << YAML::Value << (int)raycaster.Blocking;
            out << YAML::Key << "BlockingMask" << YAML::Value << raycaster.BlockingMask;

            out << YAML::EndMap;
        }

        if (entity.HasComponent<RigidBody2DComponent>())
        {
            out << YAML::Key << "RigidBody2DComponent";
            out << YAML::BeginMap;

            auto& rb2d = entity.GetComponent<RigidBody2DComponent>();
            out << YAML::Key << "BodyType" << YAML::Value << (int)rb2d.Type;
            out << YAML::Key << "FixedRotation" << YAML::Value << rb2d.FixedRotation;

            out << YAML::EndMap;
        }

        if (entity.HasComponent<BoxCollider2DComponent>())
        {
            out << YAML::Key << "BoxCollider2DComponent";
            out << YAML::BeginMap;

            auto& bc2d = entity.GetComponent<BoxCollider2DComponent>();
            out << YAML::Key << "Offset" << YAML::Value << bc2d.Offset;
            out << YAML::Key << "Size" << YAML::Value << bc2d.Size;
            out << YAML::Key << "Density" << YAML::Value << bc2d.Density;
            out << YAML::Key << "Friction" << YAML::Value << bc2d.Friction;
            out << YAML::Key << "Restitution" << YAML::Value << bc2d.Restitution;
            out << YAML::Key << "RestitutionThreshold" << YAML::Value << bc2d.RestitutionThreshold;

            out << YAML::EndMap;
        }

        if (entity.HasComponent<CircleCollider2DComponent>())
        {
            out << YAML::Key << "CircleCollider2DComponent";
            out << YAML::BeginMap;

            auto& cc2d = entity.GetComponent<CircleCollider2DComponent>();
            out << YAML::Key << "Offset" << YAML::Value << cc2d.Offset;
            out << YAML::Key << "Radius" << YAML::Value << cc2d.Radius;
            out << YAML::Key << "Density" << YAML::Value << cc2d.Density;
            out << YAML::Key << "Friction" << YAML::Value << cc2d.Friction;
            out << YAML::Key << "Restitution" << YAML::Value << cc2d.Restitution;
            out << YAML::Key << "RestitutionThreshold" << YAML::Value << cc2d.RestitutionThreshold;

            out << YAML::EndMap;
        }

        // 3D Physics Components
        if (entity.HasComponent<RigidbodyComponent>())
        {
            out << YAML::Key << "RigidbodyComponent";
            out << YAML::BeginMap;

            auto& rb = entity.GetComponent<RigidbodyComponent>();
            out << YAML::Key << "BodyType" << YAML::Value << (int)rb.Type;
            out << YAML::Key << "Mass" << YAML::Value << rb.Mass;
            out << YAML::Key << "LinearDrag" << YAML::Value << rb.LinearDrag;
            out << YAML::Key << "AngularDrag" << YAML::Value << rb.AngularDrag;
            out << YAML::Key << "GravityScale" << YAML::Value << rb.GravityScale;
            out << YAML::Key << "FreezeRotation" << YAML::Value << rb.FreezeRotation;
            out << YAML::Key << "Friction" << YAML::Value << rb.Friction;
            out << YAML::Key << "Restitution" << YAML::Value << rb.Restitution;

            out << YAML::EndMap;
        }

        if (entity.HasComponent<BoxColliderComponent>())
        {
            out << YAML::Key << "BoxColliderComponent";
            out << YAML::BeginMap;

            auto& bc = entity.GetComponent<BoxColliderComponent>();
            out << YAML::Key << "Offset" << YAML::Value << bc.Offset;
            out << YAML::Key << "Size" << YAML::Value << bc.Size;
            out << YAML::Key << "IsTrigger" << YAML::Value << bc.IsTrigger;

            out << YAML::EndMap;
        }

        if (entity.HasComponent<SphereColliderComponent>())
        {
            out << YAML::Key << "SphereColliderComponent";
            out << YAML::BeginMap;

            auto& sc = entity.GetComponent<SphereColliderComponent>();
            out << YAML::Key << "Offset" << YAML::Value << sc.Offset;
            out << YAML::Key << "Radius" << YAML::Value << sc.Radius;
            out << YAML::Key << "IsTrigger" << YAML::Value << sc.IsTrigger;

            out << YAML::EndMap;
        }

        if (entity.HasComponent<CapsuleColliderComponent>())
        {
            out << YAML::Key << "CapsuleColliderComponent";
            out << YAML::BeginMap;

            auto& cc = entity.GetComponent<CapsuleColliderComponent>();
            out << YAML::Key << "Offset" << YAML::Value << cc.Offset;
            out << YAML::Key << "Radius" << YAML::Value << cc.Radius;
            out << YAML::Key << "Height" << YAML::Value << cc.Height;
            out << YAML::Key << "IsTrigger" << YAML::Value << cc.IsTrigger;

            out << YAML::EndMap;
        }

        if (entity.HasComponent<MeshColliderComponent>())
        {
            out << YAML::Key << "MeshColliderComponent";
            out << YAML::BeginMap;

            auto& mc = entity.GetComponent<MeshColliderComponent>();
            out << YAML::Key << "IsConvex" << YAML::Value << mc.IsConvex;
            out << YAML::Key << "IsTrigger" << YAML::Value << mc.IsTrigger;

            out << YAML::EndMap;
        }

        // Audio Components
        if (entity.HasComponent<AudioSourceComponent>())
        {
            out << YAML::Key << "AudioSourceComponent";
            out << YAML::BeginMap;

            auto& audio = entity.GetComponent<AudioSourceComponent>();
            out << YAML::Key << "FilePath" << YAML::Value << audio.FilePath;
            out << YAML::Key << "PlayOnAwake" << YAML::Value << audio.PlayOnAwake;
            out << YAML::Key << "Loop" << YAML::Value << audio.Loop;
            out << YAML::Key << "Mute" << YAML::Value << audio.Mute;
            out << YAML::Key << "Volume" << YAML::Value << audio.Volume;
            out << YAML::Key << "Pitch" << YAML::Value << audio.Pitch;
            out << YAML::Key << "Pan" << YAML::Value << audio.Pan;
            out << YAML::Key << "Is3D" << YAML::Value << audio.Is3D;
            out << YAML::Key << "MinDistance" << YAML::Value << audio.MinDistance;
            out << YAML::Key << "MaxDistance" << YAML::Value << audio.MaxDistance;
            out << YAML::Key << "DopplerLevel" << YAML::Value << audio.DopplerLevel;
            out << YAML::Key << "Attenuation" << YAML::Value << (int)audio.Attenuation;
            out << YAML::Key << "RolloffFactor" << YAML::Value << audio.RolloffFactor;

            out << YAML::Key << "NextGraphNodeID" << YAML::Value << audio.NextGraphNodeID;
            out << YAML::Key << "NextGraphLinkID" << YAML::Value << audio.NextGraphLinkID;
            out << YAML::Key << "GraphInitialized" << YAML::Value << audio.GraphInitialized;

            out << YAML::Key << "GraphNodes" << YAML::Value << YAML::BeginSeq;
            for (const auto& node : audio.GraphNodes)
            {
                out << YAML::BeginMap;
                out << YAML::Key << "ID" << YAML::Value << node.ID;
                out << YAML::Key << "Type" << YAML::Value << (int)node.Type;
                out << YAML::Key << "EditorPosX" << YAML::Value << node.EditorPosition.x;
                out << YAML::Key << "EditorPosY" << YAML::Value << node.EditorPosition.y;
                out << YAML::Key << "Bypassed" << YAML::Value << node.Bypassed;
                out << YAML::EndMap;
            }
            out << YAML::EndSeq;

            out << YAML::Key << "GraphLinks" << YAML::Value << YAML::BeginSeq;
            for (const auto& link : audio.GraphLinks)
            {
                out << YAML::BeginMap;
                out << YAML::Key << "ID" << YAML::Value << link.ID;
                out << YAML::Key << "From" << YAML::Value << link.FromNodeID;
                out << YAML::Key << "To" << YAML::Value << link.ToNodeID;
                out << YAML::EndMap;
            }
            out << YAML::EndSeq;

            out << YAML::EndMap;
        }

        if (entity.HasComponent<AudioListenerComponent>())
        {
            out << YAML::Key << "AudioListenerComponent";
            out << YAML::BeginMap;

            auto& listener = entity.GetComponent<AudioListenerComponent>();
            out << YAML::Key << "IsActive" << YAML::Value << listener.IsActive;

            out << YAML::EndMap;
        }

        if (entity.HasComponent<AudioEchoFilterComponent>())
        {
            out << YAML::Key << "AudioEchoFilterComponent";
            out << YAML::BeginMap;
            auto& comp = entity.GetComponent<AudioEchoFilterComponent>();
            out << YAML::Key << "Enabled" << YAML::Value << comp.Enabled;
            out << YAML::Key << "Delay" << YAML::Value << comp.Delay;
            out << YAML::Key << "Decay" << YAML::Value << comp.Decay;
            out << YAML::Key << "WetMix" << YAML::Value << comp.WetMix;
            out << YAML::Key << "DryMix" << YAML::Value << comp.DryMix;
            out << YAML::EndMap;
        }

        if (entity.HasComponent<AudioReverbFilterComponent>())
        {
            out << YAML::Key << "AudioReverbFilterComponent";
            out << YAML::BeginMap;
            auto& comp = entity.GetComponent<AudioReverbFilterComponent>();
            out << YAML::Key << "Enabled" << YAML::Value << comp.Enabled;
            out << YAML::Key << "DecayTime" << YAML::Value << comp.DecayTime;
            out << YAML::Key << "Room" << YAML::Value << comp.Room;
            out << YAML::Key << "ReverbLevel" << YAML::Value << comp.ReverbLevel;
            out << YAML::EndMap;
        }

        if (entity.HasComponent<AudioLowPassFilterComponent>())
        {
            out << YAML::Key << "AudioLowPassFilterComponent";
            out << YAML::BeginMap;
            auto& comp = entity.GetComponent<AudioLowPassFilterComponent>();
            out << YAML::Key << "Enabled" << YAML::Value << comp.Enabled;
            out << YAML::Key << "CutoffFrequency" << YAML::Value << comp.CutoffFrequency;
            out << YAML::Key << "Resonance" << YAML::Value << comp.Resonance;
            out << YAML::EndMap;
        }

        if (entity.HasComponent<AudioHighPassFilterComponent>())
        {
            out << YAML::Key << "AudioHighPassFilterComponent";
            out << YAML::BeginMap;
            auto& comp = entity.GetComponent<AudioHighPassFilterComponent>();
            out << YAML::Key << "Enabled" << YAML::Value << comp.Enabled;
            out << YAML::Key << "CutoffFrequency" << YAML::Value << comp.CutoffFrequency;
            out << YAML::Key << "Resonance" << YAML::Value << comp.Resonance;
            out << YAML::EndMap;
        }

        if (entity.HasComponent<AudioChorusFilterComponent>())
        {
            out << YAML::Key << "AudioChorusFilterComponent";
            out << YAML::BeginMap;
            auto& comp = entity.GetComponent<AudioChorusFilterComponent>();
            out << YAML::Key << "Enabled" << YAML::Value << comp.Enabled;
            out << YAML::Key << "Delay" << YAML::Value << comp.Delay;
            out << YAML::Key << "Depth" << YAML::Value << comp.Depth;
            out << YAML::Key << "Rate" << YAML::Value << comp.Rate;
            out << YAML::Key << "WetMix" << YAML::Value << comp.WetMix;
            out << YAML::Key << "DryMix" << YAML::Value << comp.DryMix;
            out << YAML::EndMap;
        }

        if (entity.HasComponent<AudioDistortionFilterComponent>())
        {
            out << YAML::Key << "AudioDistortionFilterComponent";
            out << YAML::BeginMap;
            auto& comp = entity.GetComponent<AudioDistortionFilterComponent>();
            out << YAML::Key << "Enabled" << YAML::Value << comp.Enabled;
            out << YAML::Key << "Level" << YAML::Value << comp.Level;
            out << YAML::EndMap;
        }

        if (entity.HasComponent<AudioGainComponent>())
        {
            out << YAML::Key << "AudioGainComponent";
            out << YAML::BeginMap;
            auto& comp = entity.GetComponent<AudioGainComponent>();
            out << YAML::Key << "Enabled" << YAML::Value << comp.Enabled;
            out << YAML::Key << "Gain" << YAML::Value << comp.Gain;
            out << YAML::EndMap;
        }

        if (entity.HasComponent<AudioPanComponent>())
        {
            out << YAML::Key << "AudioPanComponent";
            out << YAML::BeginMap;
            auto& comp = entity.GetComponent<AudioPanComponent>();
            out << YAML::Key << "Enabled" << YAML::Value << comp.Enabled;
            out << YAML::Key << "Pan" << YAML::Value << comp.Pan;
            out << YAML::EndMap;
        }

        if (entity.HasComponent<AudioReverbZoneComponent>())
        {
            out << YAML::Key << "AudioReverbZoneComponent";
            out << YAML::BeginMap;
            auto& comp = entity.GetComponent<AudioReverbZoneComponent>();
            out << YAML::Key << "Enabled" << YAML::Value << comp.Enabled;
            out << YAML::Key << "MinDistance" << YAML::Value << comp.MinDistance;
            out << YAML::Key << "MaxDistance" << YAML::Value << comp.MaxDistance;
            out << YAML::Key << "Preset" << YAML::Value << (int)comp.Preset;
            out << YAML::EndMap;
        }

        out << YAML::EndMap; // Entity
    }

    void SceneSerializer::Serialize(const std::filesystem::path& filepath)
    {
        YAML::Emitter out;
        out << YAML::BeginMap;
        out << YAML::Key << "Scene" << YAML::Value << "Untitled";
        
        // Entity'leri kaydet
        out << YAML::Key << "Entities" << YAML::Value << YAML::BeginSeq;

        m_Scene->m_Registry.view<entt::entity>().each([&](auto entityID)
        {
            Entity entity = { entityID, m_Scene.get() };
            if (!entity)
                return;

            SerializeEntity(out, entity);
        });

        out << YAML::EndSeq;
        
        // Scene Environment ayarları
        out << YAML::Key << "Environment" << YAML::Value << YAML::BeginMap;
        
        // Skybox
        if (m_Scene->GetSkybox())
        {
            out << YAML::Key << "SkyboxPath" << YAML::Value << m_Scene->GetSkybox()->GetPath();
        }
        out << YAML::Key << "SkyboxExposure" << YAML::Value << m_Scene->GetSkyboxExposure();
        out << YAML::Key << "SkyboxRotation" << YAML::Value << m_Scene->GetSkyboxRotation();
        out << YAML::Key << "SkyboxTint" << YAML::Value << m_Scene->GetSkyboxTint();
        
        // Ambient Light
        out << YAML::Key << "EnvironmentLightingSource" << YAML::Value << (int)m_Scene->GetEnvironmentLightingSource();
        out << YAML::Key << "AmbientColor" << YAML::Value << m_Scene->GetAmbientColor();
        out << YAML::Key << "AmbientSkyColor" << YAML::Value << m_Scene->GetAmbientSkyColor();
        out << YAML::Key << "AmbientEquatorColor" << YAML::Value << m_Scene->GetAmbientEquatorColor();
        out << YAML::Key << "AmbientGroundColor" << YAML::Value << m_Scene->GetAmbientGroundColor();
        out << YAML::Key << "AmbientIntensity" << YAML::Value << m_Scene->GetAmbientIntensity();
        
        // Fog
        out << YAML::Key << "FogEnabled" << YAML::Value << m_Scene->IsFogEnabled();
        out << YAML::Key << "FogColor" << YAML::Value << m_Scene->GetFogColor();
        out << YAML::Key << "FogDensity" << YAML::Value << m_Scene->GetFogDensity();
        out << YAML::Key << "FogStart" << YAML::Value << m_Scene->GetFogStart();
        out << YAML::Key << "FogEnd" << YAML::Value << m_Scene->GetFogEnd();
        
        // Sun Source
        out << YAML::Key << "SunSource" << YAML::Value << m_Scene->GetSunSource();
        
        // Halo/Flare
        out << YAML::Key << "HaloEnabled" << YAML::Value << m_Scene->IsHaloEnabled();
        out << YAML::Key << "HaloStrength" << YAML::Value << m_Scene->GetHaloStrength();
        out << YAML::Key << "FlareEnabled" << YAML::Value << m_Scene->IsFlareEnabled();
        out << YAML::Key << "FlareFadeSpeed" << YAML::Value << m_Scene->GetFlareFadeSpeed();
        out << YAML::Key << "FlareStrength" << YAML::Value << m_Scene->GetFlareStrength();
        
        // Flare Elements
        out << YAML::Key << "FlareElements" << YAML::Value << YAML::BeginSeq;
        for (int i = 0; i < m_Scene->GetFlareElementCount(); i++)
        {
            auto element = m_Scene->GetFlareElement(i);
            out << YAML::BeginMap;
            out << YAML::Key << "ColorMultiplier" << YAML::Value << element.ColorMultiplier;
            out << YAML::Key << "Size" << YAML::Value << element.Size;
            out << YAML::Key << "Offset" << YAML::Value << element.Offset;
            out << YAML::Key << "Intensity" << YAML::Value << element.Intensity;
            out << YAML::EndMap;
        }
        out << YAML::EndSeq;
        
        out << YAML::EndMap; // Environment
        
        // Editor Camera kaydet
        if (m_EditorCamera)
        {
            out << YAML::Key << "EditorCamera" << YAML::Value << YAML::BeginMap;
            out << YAML::Key << "CameraMode" << YAML::Value << (int)m_EditorCamera->GetCameraMode();
            out << YAML::Key << "Position" << YAML::Value << m_EditorCamera->GetPosition();
            out << YAML::Key << "Distance" << YAML::Value << m_EditorCamera->GetDistance();
            out << YAML::Key << "Pitch" << YAML::Value << m_EditorCamera->GetPitch();
            out << YAML::Key << "Yaw" << YAML::Value << m_EditorCamera->GetYaw();
            out << YAML::Key << "OrthographicSize" << YAML::Value << m_EditorCamera->GetOrthographicSize();
            out << YAML::EndMap;
        }
        
        // Hierarchy (parent-child ilişkileri) kaydet
        out << YAML::Key << "Hierarchy" << YAML::Value << YAML::BeginSeq;
        
        for (const auto& [child, parent] : m_Scene->m_EntityParent)
        {
            Entity childEntity = { child, m_Scene.get() };
            Entity parentEntity = { parent, m_Scene.get() };
            
            if (childEntity && parentEntity)
            {
                out << YAML::BeginMap;
                out << YAML::Key << "Child" << YAML::Value << childEntity.GetComponent<IDComponent>().ID;
                out << YAML::Key << "Parent" << YAML::Value << parentEntity.GetComponent<IDComponent>().ID;
                out << YAML::EndMap;
            }
        }
        
        out << YAML::EndSeq;

        // NavMesh kaydet
        auto& navMesh = m_Scene->GetNavMesh();
        out << YAML::Key << "NavMesh" << YAML::Value << YAML::BeginMap;
        out << YAML::Key << "Triangles" << YAML::Value << YAML::BeginSeq;
        for (const auto& tri : navMesh.GetTriangles())
        {
            out << YAML::BeginMap;
            out << YAML::Key << "V0" << YAML::Value << tri.Vertices[0];
            out << YAML::Key << "V1" << YAML::Value << tri.Vertices[1];
            out << YAML::Key << "V2" << YAML::Value << tri.Vertices[2];
            out << YAML::Key << "Area" << YAML::Value << tri.AreaType;
            out << YAML::EndMap;
        }
        out << YAML::EndSeq;
        out << YAML::EndMap;

        out << YAML::EndMap;

        std::ofstream fout(filepath);
        fout << out.c_str();
        fout.close();

        CQ_CORE_INFO("Scene serialized: {0}", filepath.string());
    }

    void SceneSerializer::SerializeRuntime(const std::filesystem::path& filepath)
    {
        // Runtime serialization için aynı format
        Serialize(filepath);
    }

    void SceneSerializer::SerializeToStream(std::ostream& stream)
    {
        YAML::Emitter out;
        out << YAML::BeginMap;
        out << YAML::Key << "Scene" << YAML::Value << "Untitled";
        
        // Entity'leri kaydet
        out << YAML::Key << "Entities" << YAML::Value << YAML::BeginSeq;

        m_Scene->m_Registry.view<entt::entity>().each([&](auto entityID)
        {
            Entity entity = { entityID, m_Scene.get() };
            if (!entity)
                return;

            SerializeEntity(out, entity);
        });

        out << YAML::EndSeq;
        
        // Scene Environment ayarları
        out << YAML::Key << "Environment" << YAML::Value << YAML::BeginMap;
        
        // Skybox
        if (m_Scene->GetSkybox())
        {
            out << YAML::Key << "SkyboxPath" << YAML::Value << m_Scene->GetSkybox()->GetPath();
        }
        out << YAML::Key << "SkyboxExposure" << YAML::Value << m_Scene->GetSkyboxExposure();
        out << YAML::Key << "SkyboxRotation" << YAML::Value << m_Scene->GetSkyboxRotation();
        out << YAML::Key << "SkyboxTint" << YAML::Value << m_Scene->GetSkyboxTint();
        
        // Ambient Light
        out << YAML::Key << "EnvironmentLightingSource" << YAML::Value << (int)m_Scene->GetEnvironmentLightingSource();
        out << YAML::Key << "AmbientColor" << YAML::Value << m_Scene->GetAmbientColor();
        out << YAML::Key << "AmbientSkyColor" << YAML::Value << m_Scene->GetAmbientSkyColor();
        out << YAML::Key << "AmbientEquatorColor" << YAML::Value << m_Scene->GetAmbientEquatorColor();
        out << YAML::Key << "AmbientGroundColor" << YAML::Value << m_Scene->GetAmbientGroundColor();
        out << YAML::Key << "AmbientIntensity" << YAML::Value << m_Scene->GetAmbientIntensity();
        
        // Fog
        out << YAML::Key << "FogEnabled" << YAML::Value << m_Scene->IsFogEnabled();
        out << YAML::Key << "FogColor" << YAML::Value << m_Scene->GetFogColor();
        out << YAML::Key << "FogDensity" << YAML::Value << m_Scene->GetFogDensity();
        out << YAML::Key << "FogStart" << YAML::Value << m_Scene->GetFogStart();
        out << YAML::Key << "FogEnd" << YAML::Value << m_Scene->GetFogEnd();
        
        // Sun Source
        out << YAML::Key << "SunSource" << YAML::Value << m_Scene->GetSunSource();
        
        // Halo/Flare
        out << YAML::Key << "HaloEnabled" << YAML::Value << m_Scene->IsHaloEnabled();
        out << YAML::Key << "HaloStrength" << YAML::Value << m_Scene->GetHaloStrength();
        out << YAML::Key << "FlareEnabled" << YAML::Value << m_Scene->IsFlareEnabled();
        out << YAML::Key << "FlareFadeSpeed" << YAML::Value << m_Scene->GetFlareFadeSpeed();
        out << YAML::Key << "FlareStrength" << YAML::Value << m_Scene->GetFlareStrength();
        
        // Flare Elements
        out << YAML::Key << "FlareElements" << YAML::Value << YAML::BeginSeq;
        for (int i = 0; i < m_Scene->GetFlareElementCount(); i++)
        {
            auto element = m_Scene->GetFlareElement(i);
            out << YAML::BeginMap;
            out << YAML::Key << "ColorMultiplier" << YAML::Value << element.ColorMultiplier;
            out << YAML::Key << "Size" << YAML::Value << element.Size;
            out << YAML::Key << "Offset" << YAML::Value << element.Offset;
            out << YAML::Key << "Intensity" << YAML::Value << element.Intensity;
            out << YAML::EndMap;
        }
        out << YAML::EndSeq;
        
        out << YAML::EndMap; // Environment
        
        // Hierarchy (parent-child ilişkileri) kaydet
        out << YAML::Key << "Hierarchy" << YAML::Value << YAML::BeginSeq;
        
        for (const auto& [child, parent] : m_Scene->m_EntityParent)
        {
            Entity childEntity = { child, m_Scene.get() };
            Entity parentEntity = { parent, m_Scene.get() };
            
            if (childEntity && parentEntity)
            {
                out << YAML::BeginMap;
                out << YAML::Key << "Child" << YAML::Value << childEntity.GetComponent<IDComponent>().ID;
                out << YAML::Key << "Parent" << YAML::Value << parentEntity.GetComponent<IDComponent>().ID;
                out << YAML::EndMap;
            }
        }
        
        out << YAML::EndSeq;
        out << YAML::EndMap;

        stream << out.c_str();
    }

    bool SceneSerializer::DeserializeFromStream(std::istream& stream)
    {
        std::stringstream buffer;
        buffer << stream.rdbuf();
        std::string yamlStr = buffer.str();
        
        YAML::Node data;
        try
        {
            data = YAML::Load(yamlStr);
        }
        catch (const YAML::ParserException& e)
        {
            CQ_CORE_ERROR("Failed to load scene from stream: {0}", e.what());
            return false;
        }

        if (!data["Scene"])
            return false;

        std::string sceneName = data["Scene"].as<std::string>();
        CQ_CORE_TRACE("Deserializing scene from stream: {0}", sceneName);

        // Entity UUID -> entt::entity mapping
        std::unordered_map<uint64_t, entt::entity> entityMap;

        auto entities = data["Entities"];
        if (entities)
        {
            for (auto entity : entities)
            {
                uint64_t uuid = entity["Entity"].as<uint64_t>();

                std::string name;
                auto tagComponent = entity["TagComponent"];
                if (tagComponent)
                    name = tagComponent["Tag"].as<std::string>();

                CQ_CORE_TRACE("Deserialized entity: {0} with ID {1}", name, uuid);

                Entity deserializedEntity = m_Scene->CreateEntityWithUUID(uuid, name);
                entityMap[uuid] = deserializedEntity;

                // TagComponent
                if (tagComponent)
                {
                    auto& tag = deserializedEntity.GetComponent<TagComponent>();
                    tag.Tag = tagComponent["Tag"].as<std::string>();
                    if (tagComponent["GameTag"])
                        tag.GameTag = tagComponent["GameTag"].as<std::string>();
                }

                // TransformComponent
                auto transformComponent = entity["TransformComponent"];
                if (transformComponent)
                {
                    auto& tc = deserializedEntity.GetComponent<TransformComponent>();
                    tc.Position = transformComponent["Position"].as<glm::vec3>();
                    tc.Rotation = transformComponent["Rotation"].as<glm::vec3>();
                    tc.Scale = transformComponent["Scale"].as<glm::vec3>();
                }

                // Diğer componentler için deserialize kodu buraya eklenecek
                // (CameraComponent, SpriteRendererComponent, vs.)
                // Tam deserialize kodu için Deserialize() metoduna bakın
                
                // Kısa versiyon - sadece temel componentler
                auto cameraComponent = entity["CameraComponent"];
                if (cameraComponent)
                {
                    auto& cc = deserializedEntity.AddComponent<CameraComponent>();
                    auto cameraProps = cameraComponent["Camera"];
                    cc.Camera.SetProjectionType((SceneCamera::ProjectionType)cameraProps["ProjectionType"].as<int>());
                    cc.Camera.SetPerspectiveVerticalFOV(cameraProps["PerspectiveFOV"].as<float>());
                    cc.Camera.SetPerspectiveNearClip(cameraProps["PerspectiveNear"].as<float>());
                    cc.Camera.SetPerspectiveFarClip(cameraProps["PerspectiveFar"].as<float>());
                    cc.Camera.SetOrthographicSize(cameraProps["OrthographicSize"].as<float>());
                    cc.Camera.SetOrthographicNearClip(cameraProps["OrthographicNear"].as<float>());
                    cc.Camera.SetOrthographicFarClip(cameraProps["OrthographicFar"].as<float>());
                    cc.Primary = cameraComponent["Primary"].as<bool>();
                    cc.FixedAspectRatio = cameraComponent["FixedAspectRatio"].as<bool>();
                }

                // Physics components
                auto rigidbodyComponent = entity["RigidbodyComponent"];
                if (rigidbodyComponent)
                {
                    auto& rb = deserializedEntity.AddComponent<RigidbodyComponent>();
                    rb.Type = (RigidbodyComponent::BodyType)rigidbodyComponent["BodyType"].as<int>();
                    rb.Mass = rigidbodyComponent["Mass"].as<float>();
                    rb.LinearDrag = rigidbodyComponent["LinearDrag"].as<float>();
                    rb.AngularDrag = rigidbodyComponent["AngularDrag"].as<float>();
                    rb.GravityScale = rigidbodyComponent["GravityScale"].as<float>();
                    rb.FreezeRotation = rigidbodyComponent["FreezeRotation"].as<bool>();
                    rb.Friction = rigidbodyComponent["Friction"].as<float>();
                    rb.Restitution = rigidbodyComponent["Restitution"].as<float>();
                }

                auto boxColliderComponent = entity["BoxColliderComponent"];
                if (boxColliderComponent)
                {
                    auto& bc = deserializedEntity.AddComponent<BoxColliderComponent>();
                    bc.Offset = boxColliderComponent["Offset"].as<glm::vec3>();
                    bc.Size = boxColliderComponent["Size"].as<glm::vec3>();
                    bc.IsTrigger = boxColliderComponent["IsTrigger"].as<bool>();
                }

                auto sphereColliderComponent = entity["SphereColliderComponent"];
                if (sphereColliderComponent)
                {
                    auto& sc = deserializedEntity.AddComponent<SphereColliderComponent>();
                    sc.Offset = sphereColliderComponent["Offset"].as<glm::vec3>();
                    sc.Radius = sphereColliderComponent["Radius"].as<float>();
                    sc.IsTrigger = sphereColliderComponent["IsTrigger"].as<bool>();
                }
            }
        }

        // Hierarchy yükle
        auto hierarchy = data["Hierarchy"];
        if (hierarchy)
        {
            for (auto relation : hierarchy)
            {
                uint64_t childUUID = relation["Child"].as<uint64_t>();
                uint64_t parentUUID = relation["Parent"].as<uint64_t>();
                
                if (entityMap.find(childUUID) != entityMap.end() && entityMap.find(parentUUID) != entityMap.end())
                {
                    Entity childEntity = { entityMap[childUUID], m_Scene.get() };
                    Entity parentEntity = { entityMap[parentUUID], m_Scene.get() };
                    m_Scene->SetEntityParent(childEntity, parentEntity);
                }
            }
        }

        return true;
    }

    bool SceneSerializer::Deserialize(const std::filesystem::path& filepath)
    {
        std::ifstream stream(filepath);
        if (!stream.is_open())
        {
            CQ_CORE_ERROR("Failed to open scene file: {0}", filepath.string());
            return false;
        }

        std::stringstream strStream;
        strStream << stream.rdbuf();

        YAML::Node data = YAML::Load(strStream.str());
        if (!data["Scene"])
            return false;

        std::string sceneName = data["Scene"].as<std::string>();
        CQ_CORE_INFO("Deserializing scene '{0}'", sceneName);

        auto entities = data["Entities"];
        if (entities)
        {
            for (auto entity : entities)
            {
                uint64_t uuid = entity["Entity"].as<uint64_t>();

                std::string name;
                auto tagComponent = entity["TagComponent"];
                if (tagComponent)
                    name = tagComponent["Tag"].as<std::string>();

                CQ_CORE_TRACE("Deserialized entity with ID = {0}, name = {1}", uuid, name);

                Entity deserializedEntity = m_Scene->CreateEntityWithUUID(uuid, name);

                if (tagComponent)
                {
                    auto& tag = deserializedEntity.GetComponent<TagComponent>();
                    tag.GameTag = tagComponent["GameTag"].as<std::string>();
                }

                auto transformComponent = entity["TransformComponent"];
                if (transformComponent)
                {
                    auto& tc = deserializedEntity.GetComponent<TransformComponent>();
                    tc.Position = transformComponent["Position"].as<glm::vec3>();
                    tc.Rotation = transformComponent["Rotation"].as<glm::vec3>();
                    tc.Scale = transformComponent["Scale"].as<glm::vec3>();
                }

                auto cameraComponent = entity["CameraComponent"];
                if (cameraComponent)
                {
                    auto& cc = deserializedEntity.AddComponent<CameraComponent>();

                    const YAML::Node& cameraProps = cameraComponent["Camera"];
                    cc.Camera.SetProjectionType((SceneCamera::ProjectionType)cameraProps["ProjectionType"].as<int>());

                    cc.Camera.SetPerspectiveVerticalFOV(cameraProps["PerspectiveFOV"].as<float>());
                    cc.Camera.SetPerspectiveNearClip(cameraProps["PerspectiveNear"].as<float>());
                    cc.Camera.SetPerspectiveFarClip(cameraProps["PerspectiveFar"].as<float>());

                    cc.Camera.SetOrthographicSize(cameraProps["OrthographicSize"].as<float>());
                    cc.Camera.SetOrthographicNearClip(cameraProps["OrthographicNear"].as<float>());
                    cc.Camera.SetOrthographicFarClip(cameraProps["OrthographicFar"].as<float>());

                    cc.Primary = cameraComponent["Primary"].as<bool>();
                    cc.FixedAspectRatio = cameraComponent["FixedAspectRatio"].as<bool>();
                }

                auto spriteRendererComponent = entity["SpriteRendererComponent"];
                if (spriteRendererComponent)
                {
                    auto& src = deserializedEntity.AddComponent<SpriteRendererComponent>();
                    src.Color = spriteRendererComponent["Color"].as<glm::vec4>();
                    src.TilingFactor = spriteRendererComponent["TilingFactor"].as<float>();
                    
                    // Texture yükle
                    if (spriteRendererComponent["TexturePath"])
                    {
                        std::string texturePath = spriteRendererComponent["TexturePath"].as<std::string>();
                        if (!texturePath.empty())
                        {
                            src.Texture = Texture2D::Create(texturePath);
                        }
                    }
                }

                auto layerComponent = entity["LayerComponent"];
                if (layerComponent)
                {
                    auto& lc = deserializedEntity.GetComponent<LayerComponent>();
                    lc.Layer = layerComponent["Layer"].as<int32_t>();
                    lc.OrderInLayer = layerComponent["OrderInLayer"].as<int32_t>();
                }

                auto textRendererComponent = entity["TextRendererComponent"];
                if (textRendererComponent)
                {
                    auto& trc = deserializedEntity.AddComponent<TextRendererComponent>();
                    trc.Text = textRendererComponent["Text"].as<std::string>();
                    trc.Color = textRendererComponent["Color"].as<glm::vec4>();
                    trc.FontSize = textRendererComponent["FontSize"].as<float>();
                }

                auto navMeshAgentComponent = entity["NavMeshAgentComponent"];
                if (navMeshAgentComponent)
                {
                    auto& comp = deserializedEntity.AddComponent<NavMeshAgentComponent>();
                    comp.Speed = navMeshAgentComponent["Speed"].as<float>();
                    comp.AngularSpeed = navMeshAgentComponent["AngularSpeed"].as<float>();
                    comp.Acceleration = navMeshAgentComponent["Acceleration"].as<float>();
                    comp.StoppingDistance = navMeshAgentComponent["StoppingDistance"].as<float>();
                    comp.Radius = navMeshAgentComponent["Radius"].as<float>();
                    comp.Height = navMeshAgentComponent["Height"].as<float>();
                    comp.AutoBraking = navMeshAgentComponent["AutoBraking"].as<bool>();
                }

                auto navMeshObstacleComponent = entity["NavMeshObstacleComponent"];
                if (navMeshObstacleComponent)
                {
                    auto& comp = deserializedEntity.AddComponent<NavMeshObstacleComponent>();
                    comp.Carve = navMeshObstacleComponent["Carve"].as<bool>();
                    comp.Radius = navMeshObstacleComponent["Radius"].as<float>();
                    comp.Size = navMeshObstacleComponent["Size"].as<glm::vec3>();
                    comp.Center = navMeshObstacleComponent["Center"].as<glm::vec3>();
                }

                auto navMeshSurfaceComponent = entity["NavMeshSurfaceComponent"];
                if (navMeshSurfaceComponent)
                {
                    auto& comp = deserializedEntity.AddComponent<NavMeshSurfaceComponent>();
                    comp.AreaType = navMeshSurfaceComponent["AreaType"].as<int>();
                    comp.OverrideVoxelSize = navMeshSurfaceComponent["OverrideVoxelSize"].as<bool>();
                    comp.VoxelSize = navMeshSurfaceComponent["VoxelSize"].as<float>();
                }

                auto meshRendererComponent = entity["MeshRendererComponent"];
                if (meshRendererComponent)
                {
                    auto& mrc = deserializedEntity.AddComponent<MeshRendererComponent>();
                    if (meshRendererComponent["MeshType"])
                        mrc.Type = static_cast<MeshType>(meshRendererComponent["MeshType"].as<int>());
                    mrc.Color = meshRendererComponent["Color"].as<glm::vec4>();
                    mrc.Metallic = meshRendererComponent["Metallic"].as<float>();
                    mrc.Roughness = meshRendererComponent["Roughness"].as<float>();
                    mrc.AO = meshRendererComponent["AO"].as<float>();

                    if (meshRendererComponent["TexturePath"])
                    {
                        mrc.TexturePath = meshRendererComponent["TexturePath"].as<std::string>();
                        if (!mrc.TexturePath.empty())
                            mrc.Texture = Texture2D::Create(mrc.TexturePath);
                        else
                        {
                            mrc.Texture = nullptr;
                        }
                    }
                    else
                    {
                        mrc.TexturePath.clear();
                        mrc.Texture = nullptr;
                    }

                    auto materialNode = meshRendererComponent["Material"];
                    if (materialNode)
                    {
                        mrc.MaterialInstance = Material::CreateDefault();
                        auto& mat = mrc.MaterialInstance;
                        
                        if (materialNode["Name"]) mat->Name = materialNode["Name"].as<std::string>();
                        if (materialNode["Albedo"]) mat->Albedo = materialNode["Albedo"].as<glm::vec3>();
                        if (materialNode["Metallic"]) mat->Metallic = materialNode["Metallic"].as<float>();
                        if (materialNode["Roughness"]) mat->Roughness = materialNode["Roughness"].as<float>();
                        if (materialNode["AO"]) mat->AO = materialNode["AO"].as<float>();
                        if (materialNode["EmissionColor"]) mat->EmissionColor = materialNode["EmissionColor"].as<glm::vec3>();
                        if (materialNode["EmissionStrength"]) mat->EmissionStrength = materialNode["EmissionStrength"].as<float>();
                        if (materialNode["Tiling"]) mat->Tiling = materialNode["Tiling"].as<glm::vec2>();
                        if (materialNode["Offset"]) mat->Offset = materialNode["Offset"].as<glm::vec2>();

                        if (materialNode["NextGraphNodeID"]) mat->NextGraphNodeID = materialNode["NextGraphNodeID"].as<int>();
                        if (materialNode["NextGraphLinkID"]) mat->NextGraphLinkID = materialNode["NextGraphLinkID"].as<int>();
                        if (materialNode["GraphInitialized"]) mat->GraphInitialized = materialNode["GraphInitialized"].as<bool>();

                        auto graphNodes = materialNode["GraphNodes"];
                        if (graphNodes)
                        {
                            for (auto node : graphNodes)
                            {
                                ShaderGraphNode sNode;
                                sNode.ID = node["ID"].as<int>();
                                sNode.Type = (ShaderGraphNodeType)node["Type"].as<int>();
                                sNode.EditorPosition = { node["EditorPosX"].as<float>(), node["EditorPosY"].as<float>() };
                                
                                if (node["ColorValue"]) sNode.ColorValue = node["ColorValue"].as<glm::vec4>();
                                if (node["FloatValue"]) sNode.FloatValue = node["FloatValue"].as<float>();
                                if (node["Vec2Value"]) sNode.Vec2Value = node["Vec2Value"].as<glm::vec2>();
                                if (node["Vec3Value"]) sNode.Vec3Value = node["Vec3Value"].as<glm::vec3>();
                                if (node["TexturePath"]) sNode.TexturePath = node["TexturePath"].as<std::string>();

                                mat->GraphNodes.push_back(sNode);
                            }
                        }

                        auto graphLinks = materialNode["GraphLinks"];
                        if (graphLinks)
                        {
                            for (auto link : graphLinks)
                            {
                                ShaderGraphLink sLink;
                                sLink.ID = link["ID"].as<int>();
                                sLink.FromNodeID = link["From"].as<int>();
                                sLink.FromPinIndex = link["FromPin"].as<int>();
                                sLink.ToNodeID = link["To"].as<int>();
                                sLink.ToPinIndex = link["ToPin"].as<int>();
                                mat->GraphLinks.push_back(sLink);
                            }
                        }
                    }
                }

                auto directionalLightComponent = entity["DirectionalLightComponent"];
                if (directionalLightComponent)
                {
                    auto& dlc = deserializedEntity.AddComponent<DirectionalLightComponent>();
                    dlc.Direction = directionalLightComponent["Direction"].as<glm::vec3>();
                    dlc.Color = directionalLightComponent["Color"].as<glm::vec3>();
                    dlc.Intensity = directionalLightComponent["Intensity"].as<float>();
                    dlc.CastShadows = directionalLightComponent["CastShadows"].as<bool>();
                }

                auto pointLightComponent = entity["PointLightComponent"];
                if (pointLightComponent)
                {
                    auto& plc = deserializedEntity.AddComponent<PointLightComponent>();
                    plc.Color = pointLightComponent["Color"].as<glm::vec3>();
                    plc.Intensity = pointLightComponent["Intensity"].as<float>();
                    plc.Range = pointLightComponent["Range"].as<float>();
                    plc.Constant = pointLightComponent["Constant"].as<float>();
                    plc.Linear = pointLightComponent["Linear"].as<float>();
                    plc.Quadratic = pointLightComponent["Quadratic"].as<float>();
                    plc.CastShadows = pointLightComponent["CastShadows"].as<bool>();
                }

                auto spotLightComponent = entity["SpotLightComponent"];
                if (spotLightComponent)
                {
                    auto& slc = deserializedEntity.AddComponent<SpotLightComponent>();
                    slc.Direction = spotLightComponent["Direction"].as<glm::vec3>();
                    slc.Color = spotLightComponent["Color"].as<glm::vec3>();
                    slc.Intensity = spotLightComponent["Intensity"].as<float>();
                    slc.Range = spotLightComponent["Range"].as<float>();
                    slc.InnerConeAngle = spotLightComponent["InnerConeAngle"].as<float>();
                    slc.OuterConeAngle = spotLightComponent["OuterConeAngle"].as<float>();
                    slc.Constant = spotLightComponent["Constant"].as<float>();
                    slc.Linear = spotLightComponent["Linear"].as<float>();
                    slc.Quadratic = spotLightComponent["Quadratic"].as<float>();
                    slc.CastShadows = spotLightComponent["CastShadows"].as<bool>();
                }

                auto modelComponent = entity["ModelComponent"];
                if (modelComponent)
                {
                    auto& mc = deserializedEntity.AddComponent<ModelComponent>();
                    mc.FilePath = modelComponent["FilePath"].as<std::string>();
                    
                    if (!mc.FilePath.empty())
                    {
                        mc.ModelData = ModelLoader::Load(mc.FilePath);
                        if (!mc.ModelData)
                            CQ_CORE_WARN("Deserialize: Failed to load model (Play/runtime): {0}", mc.FilePath);
                    }
                }

                auto animatorComponent = entity["AnimatorComponent"];
                if (animatorComponent)
                {
                    auto& ac = deserializedEntity.AddComponent<AnimatorComponent>();
                    if (animatorComponent["Playing"])
                        ac.Playing = animatorComponent["Playing"].as<bool>();
                    if (animatorComponent["Speed"])
                        ac.Speed = animatorComponent["Speed"].as<float>();
                    if (animatorComponent["ActiveClipIndex"])
                        ac.ActiveClipIndex = animatorComponent["ActiveClipIndex"].as<int>();
                    if (animatorComponent["CurrentTime"])
                        ac.CurrentTime = animatorComponent["CurrentTime"].as<float>();
                }

                // Deserializing dynamic script keys
                for (auto component : entity)
                {
                    std::string compName = component.first.as<std::string>();
                    
                    if (compName.find("_NativeScriptComponent") != std::string::npos || compName == "NativeScriptComponent")
                    {
                        auto nativeScriptComponent = component.second;
                        auto& sc = deserializedEntity.HasComponent<NativeScriptComponent>() ? 
                                   deserializedEntity.GetComponent<NativeScriptComponent>() : 
                                   deserializedEntity.AddComponent<NativeScriptComponent>();
                        
                        NativeScriptData data;
                        data.ModuleName = nativeScriptComponent["ModuleName"].as<std::string>();
                        data.ClassName = nativeScriptComponent["ClassName"].as<std::string>();
                        
                        if (nativeScriptComponent["ScriptPath"])
                            data.ScriptPath = nativeScriptComponent["ScriptPath"].as<std::string>();
                            
                        if (data.ClassName.empty() && !data.ModuleName.empty())
                        {
                            data.ClassName = data.ModuleName;
                            if (data.ClassName.find("lib") == 0)
                                data.ClassName = data.ClassName.substr(3);
                        }
                        
                        if (!data.ScriptPath.empty() && !ScriptEngine::IsModuleLoaded(data.ModuleName))
                        {
                            ScriptEngine::LoadModule(data.ModuleName, data.ScriptPath);
                        }
                        
                        if (!data.ModuleName.empty() && !data.ClassName.empty() && ScriptEngine::IsModuleLoaded(data.ModuleName))
                        {
                            data.InstantiateScript = [moduleName = data.ModuleName, className = data.ClassName]() {
                                return ScriptEngine::CreateScriptInstance(moduleName, className);
                            };
                        }
                        
                        sc.Scripts.push_back(data);
                    }
                    else if (compName.find("_ConquerorScriptComponent") != std::string::npos || compName == "ConquerorScriptComponent")
                    {
                        auto conquerorScriptComponent = component.second;
                        auto& sc = deserializedEntity.HasComponent<ConquerorScriptComponent>() ? 
                                   deserializedEntity.GetComponent<ConquerorScriptComponent>() : 
                                   deserializedEntity.AddComponent<ConquerorScriptComponent>();
                        
                        ConquerorScriptData data;
                        data.ScriptPath = conquerorScriptComponent["ScriptPath"].as<std::string>();
                        data.ClassName = conquerorScriptComponent["ClassName"].as<std::string>();
                        
                        sc.Scripts.push_back(data);
                    }
                }

                auto imageComponent = entity["ImageComponent"];
                if (imageComponent)
                {
                    auto& ic = deserializedEntity.AddComponent<ImageComponent>();
                    ic.Color = imageComponent["Color"].as<glm::vec4>();
                    ic.TilingFactor = imageComponent["TilingFactor"].as<float>();
                    
                    // Texture yükle
                    if (imageComponent["TexturePath"])
                    {
                        std::string texturePath = imageComponent["TexturePath"].as<std::string>();
                        if (!texturePath.empty())
                        {
                            ic.Texture = Texture2D::Create(texturePath);
                        }
                    }
                }

                auto buttonComponent = entity["ButtonComponent"];
                if (buttonComponent)
                {
                    auto& bc = deserializedEntity.AddComponent<ButtonComponent>();
                    bc.Interactable = buttonComponent["Interactable"].as<bool>();
                    bc.Navigation = (ButtonComponent::NavigationMode)buttonComponent["Navigation"].as<int>();
                    bc.Use9Slice = buttonComponent["Use9Slice"].as<bool>();
                    bc.BorderSize = buttonComponent["BorderSize"].as<float>();
                    bc.ColorMultiplier = buttonComponent["ColorMultiplier"].as<float>();
                    bc.TransitionSpeed = buttonComponent["TransitionSpeed"].as<float>();
                    
                    // Renkler
                    bc.BackgroundNormalColor = buttonComponent["BackgroundNormalColor"].as<glm::vec4>();
                    bc.BackgroundHoverColor = buttonComponent["BackgroundHoverColor"].as<glm::vec4>();
                    bc.BackgroundPressedColor = buttonComponent["BackgroundPressedColor"].as<glm::vec4>();
                    bc.BackgroundDisabledColor = buttonComponent["BackgroundDisabledColor"].as<glm::vec4>();
                    bc.BackgroundHighlightedColor = buttonComponent["BackgroundHighlightedColor"].as<glm::vec4>();
                    
                    bc.TextNormalColor = buttonComponent["TextNormalColor"].as<glm::vec4>();
                    bc.TextHoverColor = buttonComponent["TextHoverColor"].as<glm::vec4>();
                    bc.TextPressedColor = buttonComponent["TextPressedColor"].as<glm::vec4>();
                    bc.TextDisabledColor = buttonComponent["TextDisabledColor"].as<glm::vec4>();
                    bc.TextHighlightedColor = buttonComponent["TextHighlightedColor"].as<glm::vec4>();
                }

                auto canvasComponent = entity["CanvasComponent"];
                if (canvasComponent)
                {
                    auto& cc = deserializedEntity.AddComponent<CanvasComponent>();
                    cc.Mode = (CanvasComponent::RenderMode)canvasComponent["RenderMode"].as<int>();
                    cc.PixelPerfect = canvasComponent["PixelPerfect"].as<bool>();
                    cc.SortOrder = canvasComponent["SortOrder"].as<int32_t>();
                }

                auto canvasScalerComponent = entity["CanvasScalerComponent"];
                if (canvasScalerComponent)
                {
                    auto& csc = deserializedEntity.AddComponent<CanvasScalerComponent>();
                    csc.ScaleMode = (CanvasScalerComponent::UIScaleMode)canvasScalerComponent["ScaleMode"].as<int>();
                    csc.ScaleFactor = canvasScalerComponent["ScaleFactor"].as<float>();
                    csc.ReferencePixelsPerUnit = canvasScalerComponent["ReferencePixelsPerUnit"].as<float>();
                }

                auto graphicRaycasterComponent = entity["GraphicRaycasterComponent"];
                if (graphicRaycasterComponent)
                {
                    auto& grc = deserializedEntity.AddComponent<GraphicRaycasterComponent>();
                    grc.IgnoreReversedGraphics = graphicRaycasterComponent["IgnoreReversedGraphics"].as<bool>();
                    grc.Blocking = (GraphicRaycasterComponent::BlockingObjects)graphicRaycasterComponent["BlockingObjects"].as<int>();
                    grc.BlockingMask = graphicRaycasterComponent["BlockingMask"].as<uint32_t>();
                }

                auto rigidBody2DComponent = entity["RigidBody2DComponent"];
                if (rigidBody2DComponent)
                {
                    auto& rb2d = deserializedEntity.AddComponent<RigidBody2DComponent>();
                    rb2d.Type = (RigidBody2DComponent::BodyType)rigidBody2DComponent["BodyType"].as<int>();
                    rb2d.FixedRotation = rigidBody2DComponent["FixedRotation"].as<bool>();
                }

                auto boxCollider2DComponent = entity["BoxCollider2DComponent"];
                if (boxCollider2DComponent)
                {
                    auto& bc2d = deserializedEntity.AddComponent<BoxCollider2DComponent>();
                    bc2d.Offset = boxCollider2DComponent["Offset"].as<glm::vec2>();
                    bc2d.Size = boxCollider2DComponent["Size"].as<glm::vec2>();
                    bc2d.Density = boxCollider2DComponent["Density"].as<float>();
                    bc2d.Friction = boxCollider2DComponent["Friction"].as<float>();
                    bc2d.Restitution = boxCollider2DComponent["Restitution"].as<float>();
                    bc2d.RestitutionThreshold = boxCollider2DComponent["RestitutionThreshold"].as<float>();
                }

                auto circleCollider2DComponent = entity["CircleCollider2DComponent"];
                if (circleCollider2DComponent)
                {
                    auto& cc2d = deserializedEntity.AddComponent<CircleCollider2DComponent>();
                    cc2d.Offset = circleCollider2DComponent["Offset"].as<glm::vec2>();
                    cc2d.Radius = circleCollider2DComponent["Radius"].as<float>();
                    cc2d.Density = circleCollider2DComponent["Density"].as<float>();
                    cc2d.Friction = circleCollider2DComponent["Friction"].as<float>();
                    cc2d.Restitution = circleCollider2DComponent["Restitution"].as<float>();
                    cc2d.RestitutionThreshold = circleCollider2DComponent["RestitutionThreshold"].as<float>();
                }

                // 3D Physics Components
                auto rigidbodyComponent = entity["RigidbodyComponent"];
                if (rigidbodyComponent)
                {
                    auto& rb = deserializedEntity.AddComponent<RigidbodyComponent>();
                    rb.Type = (RigidbodyComponent::BodyType)rigidbodyComponent["BodyType"].as<int>();
                    rb.Mass = rigidbodyComponent["Mass"].as<float>();
                    rb.LinearDrag = rigidbodyComponent["LinearDrag"].as<float>();
                    rb.AngularDrag = rigidbodyComponent["AngularDrag"].as<float>();
                    rb.GravityScale = rigidbodyComponent["GravityScale"].as<float>();
                    rb.FreezeRotation = rigidbodyComponent["FreezeRotation"].as<bool>();
                    rb.Friction = rigidbodyComponent["Friction"].as<float>();
                    rb.Restitution = rigidbodyComponent["Restitution"].as<float>();
                }

                auto boxColliderComponent = entity["BoxColliderComponent"];
                if (boxColliderComponent)
                {
                    auto& bc = deserializedEntity.AddComponent<BoxColliderComponent>();
                    bc.Offset = boxColliderComponent["Offset"].as<glm::vec3>();
                    bc.Size = boxColliderComponent["Size"].as<glm::vec3>();
                    bc.IsTrigger = boxColliderComponent["IsTrigger"].as<bool>();
                }

                auto sphereColliderComponent = entity["SphereColliderComponent"];
                if (sphereColliderComponent)
                {
                    auto& sc = deserializedEntity.AddComponent<SphereColliderComponent>();
                    sc.Offset = sphereColliderComponent["Offset"].as<glm::vec3>();
                    sc.Radius = sphereColliderComponent["Radius"].as<float>();
                    sc.IsTrigger = sphereColliderComponent["IsTrigger"].as<bool>();
                }

                auto capsuleColliderComponent = entity["CapsuleColliderComponent"];
                if (capsuleColliderComponent)
                {
                    auto& cc = deserializedEntity.AddComponent<CapsuleColliderComponent>();
                    cc.Offset = capsuleColliderComponent["Offset"].as<glm::vec3>();
                    cc.Radius = capsuleColliderComponent["Radius"].as<float>();
                    cc.Height = capsuleColliderComponent["Height"].as<float>();
                    cc.IsTrigger = capsuleColliderComponent["IsTrigger"].as<bool>();
                }

                auto meshColliderComponent = entity["MeshColliderComponent"];
                if (meshColliderComponent)
                {
                    auto& mc = deserializedEntity.AddComponent<MeshColliderComponent>();
                    mc.IsConvex = meshColliderComponent["IsConvex"].as<bool>();
                    mc.IsTrigger = meshColliderComponent["IsTrigger"].as<bool>();
                }

                // Audio Components
                auto audioSourceComponent = entity["AudioSourceComponent"];
                if (audioSourceComponent)
                {
                    auto& audio = deserializedEntity.AddComponent<AudioSourceComponent>();
                    audio.FilePath = audioSourceComponent["FilePath"].as<std::string>();
                    audio.PlayOnAwake = audioSourceComponent["PlayOnAwake"].as<bool>();
                    audio.Loop = audioSourceComponent["Loop"].as<bool>();
                    audio.Mute = audioSourceComponent["Mute"].as<bool>();
                    audio.Volume = audioSourceComponent["Volume"].as<float>();
                    audio.Pitch = audioSourceComponent["Pitch"].as<float>();
                    audio.Pan = audioSourceComponent["Pan"].as<float>();
                    audio.Is3D = audioSourceComponent["Is3D"].as<bool>();
                    audio.MinDistance = audioSourceComponent["MinDistance"].as<float>();
                    audio.MaxDistance = audioSourceComponent["MaxDistance"].as<float>();
                    audio.DopplerLevel = audioSourceComponent["DopplerLevel"].as<float>();
                    audio.Attenuation = (AudioSourceComponent::AttenuationModel)audioSourceComponent["Attenuation"].as<int>();
                    audio.RolloffFactor = audioSourceComponent["RolloffFactor"].as<float>();

                    if (audioSourceComponent["NextGraphNodeID"])
                        audio.NextGraphNodeID = audioSourceComponent["NextGraphNodeID"].as<int>();
                    if (audioSourceComponent["NextGraphLinkID"])
                        audio.NextGraphLinkID = audioSourceComponent["NextGraphLinkID"].as<int>();
                    if (audioSourceComponent["GraphInitialized"])
                        audio.GraphInitialized = audioSourceComponent["GraphInitialized"].as<bool>();

                    auto graphNodes = audioSourceComponent["GraphNodes"];
                    if (graphNodes)
                    {
                        for (auto node : graphNodes)
                        {
                            AudioGraphNode graphNode(
                                node["ID"].as<int>(),
                                (AudioGraphNodeType)node["Type"].as<int>(),
                                { node["EditorPosX"].as<float>(), node["EditorPosY"].as<float>() }
                            );
                            graphNode.Bypassed = node["Bypassed"].as<bool>();
                            audio.GraphNodes.push_back(graphNode);
                        }
                    }

                    auto graphLinks = audioSourceComponent["GraphLinks"];
                    if (graphLinks)
                    {
                        for (auto link : graphLinks)
                        {
                            audio.GraphLinks.emplace_back(
                                link["ID"].as<int>(),
                                link["From"].as<int>(),
                                link["To"].as<int>()
                            );
                        }
                    }
                    
                    audio.NeedsReconnection = true;
                }

                auto audioListenerComponent = entity["AudioListenerComponent"];
                if (audioListenerComponent)
                {
                    auto& listener = deserializedEntity.AddComponent<AudioListenerComponent>();
                    listener.IsActive = audioListenerComponent["IsActive"].as<bool>();
                }

                auto audioEchoFilterComponent = entity["AudioEchoFilterComponent"];
                if (audioEchoFilterComponent)
                {
                    auto& comp = deserializedEntity.AddComponent<AudioEchoFilterComponent>();
                    comp.Enabled = audioEchoFilterComponent["Enabled"].as<bool>();
                    comp.Delay = audioEchoFilterComponent["Delay"].as<float>();
                    comp.Decay = audioEchoFilterComponent["Decay"].as<float>();
                    comp.WetMix = audioEchoFilterComponent["WetMix"].as<float>();
                    comp.DryMix = audioEchoFilterComponent["DryMix"].as<float>();
                }

                auto audioReverbFilterComponent = entity["AudioReverbFilterComponent"];
                if (audioReverbFilterComponent)
                {
                    auto& comp = deserializedEntity.AddComponent<AudioReverbFilterComponent>();
                    comp.Enabled = audioReverbFilterComponent["Enabled"].as<bool>();
                    comp.DecayTime = audioReverbFilterComponent["DecayTime"].as<float>();
                    comp.Room = audioReverbFilterComponent["Room"].as<float>();
                    comp.ReverbLevel = audioReverbFilterComponent["ReverbLevel"].as<float>();
                }

                auto audioLowPassFilterComponent = entity["AudioLowPassFilterComponent"];
                if (audioLowPassFilterComponent)
                {
                    auto& comp = deserializedEntity.AddComponent<AudioLowPassFilterComponent>();
                    comp.Enabled = audioLowPassFilterComponent["Enabled"].as<bool>();
                    comp.CutoffFrequency = audioLowPassFilterComponent["CutoffFrequency"].as<float>();
                    comp.Resonance = audioLowPassFilterComponent["Resonance"].as<float>();
                }

                auto audioHighPassFilterComponent = entity["AudioHighPassFilterComponent"];
                if (audioHighPassFilterComponent)
                {
                    auto& comp = deserializedEntity.AddComponent<AudioHighPassFilterComponent>();
                    comp.Enabled = audioHighPassFilterComponent["Enabled"].as<bool>();
                    comp.CutoffFrequency = audioHighPassFilterComponent["CutoffFrequency"].as<float>();
                    comp.Resonance = audioHighPassFilterComponent["Resonance"].as<float>();
                }

                auto audioChorusFilterComponent = entity["AudioChorusFilterComponent"];
                if (audioChorusFilterComponent)
                {
                    auto& comp = deserializedEntity.AddComponent<AudioChorusFilterComponent>();
                    comp.Enabled = audioChorusFilterComponent["Enabled"].as<bool>();
                    comp.Delay = audioChorusFilterComponent["Delay"].as<float>();
                    comp.Depth = audioChorusFilterComponent["Depth"].as<float>();
                    comp.Rate = audioChorusFilterComponent["Rate"].as<float>();
                    comp.WetMix = audioChorusFilterComponent["WetMix"].as<float>();
                    comp.DryMix = audioChorusFilterComponent["DryMix"].as<float>();
                }

                auto audioDistortionFilterComponent = entity["AudioDistortionFilterComponent"];
                if (audioDistortionFilterComponent)
                {
                    auto& comp = deserializedEntity.AddComponent<AudioDistortionFilterComponent>();
                    comp.Enabled = audioDistortionFilterComponent["Enabled"].as<bool>();
                    comp.Level = audioDistortionFilterComponent["Level"].as<float>();
                }

                auto audioGainComponent = entity["AudioGainComponent"];
                if (audioGainComponent)
                {
                    auto& comp = deserializedEntity.AddComponent<AudioGainComponent>();
                    comp.Enabled = audioGainComponent["Enabled"].as<bool>();
                    comp.Gain = audioGainComponent["Gain"].as<float>();
                }

                auto audioPanComponent = entity["AudioPanComponent"];
                if (audioPanComponent)
                {
                    auto& comp = deserializedEntity.AddComponent<AudioPanComponent>();
                    comp.Enabled = audioPanComponent["Enabled"].as<bool>();
                    comp.Pan = audioPanComponent["Pan"].as<float>();
                }

                auto audioReverbZoneComponent = entity["AudioReverbZoneComponent"];
                if (audioReverbZoneComponent)
                {
                    auto& comp = deserializedEntity.AddComponent<AudioReverbZoneComponent>();
                    comp.Enabled = audioReverbZoneComponent["Enabled"].as<bool>();
                    comp.MinDistance = audioReverbZoneComponent["MinDistance"].as<float>();
                    comp.MaxDistance = audioReverbZoneComponent["MaxDistance"].as<float>();
                    comp.Preset = (AudioReverbZoneComponent::ReverbPreset)audioReverbZoneComponent["Preset"].as<int>();
                }
            }
        }
        
        // Scene Environment ayarlarını yükle
        auto environment = data["Environment"];
        if (environment)
        {
            // Skybox
            if (environment["SkyboxPath"])
            {
                std::string skyboxPath = environment["SkyboxPath"].as<std::string>();
                if (!skyboxPath.empty())
                {
                    // HDR skybox yükle (default 512 resolution)
                    auto skybox = Cubemap::CreateFromEquirectangular(skyboxPath, 512);
                    m_Scene->SetSkybox(skybox);
                }
            }
            if (environment["SkyboxExposure"])
                m_Scene->SetSkyboxExposure(environment["SkyboxExposure"].as<float>());
            if (environment["SkyboxRotation"])
                m_Scene->SetSkyboxRotation(environment["SkyboxRotation"].as<float>());
            if (environment["SkyboxTint"])
                m_Scene->SetSkyboxTint(environment["SkyboxTint"].as<glm::vec3>());
            
            // Ambient Light
            if (environment["EnvironmentLightingSource"])
                m_Scene->SetEnvironmentLightingSource((Scene::EnvironmentLightingSource)environment["EnvironmentLightingSource"].as<int>());
            if (environment["AmbientColor"])
                m_Scene->SetAmbientColor(environment["AmbientColor"].as<glm::vec3>());
            if (environment["AmbientSkyColor"])
                m_Scene->SetAmbientSkyColor(environment["AmbientSkyColor"].as<glm::vec3>());
            if (environment["AmbientEquatorColor"])
                m_Scene->SetAmbientEquatorColor(environment["AmbientEquatorColor"].as<glm::vec3>());
            if (environment["AmbientGroundColor"])
                m_Scene->SetAmbientGroundColor(environment["AmbientGroundColor"].as<glm::vec3>());
            if (environment["AmbientIntensity"])
                m_Scene->SetAmbientIntensity(environment["AmbientIntensity"].as<float>());
            
            // Fog
            if (environment["FogEnabled"])
                m_Scene->SetFogEnabled(environment["FogEnabled"].as<bool>());
            if (environment["FogColor"])
                m_Scene->SetFogColor(environment["FogColor"].as<glm::vec3>());
            if (environment["FogDensity"])
                m_Scene->SetFogDensity(environment["FogDensity"].as<float>());
            if (environment["FogStart"])
                m_Scene->SetFogStart(environment["FogStart"].as<float>());
            if (environment["FogEnd"])
                m_Scene->SetFogEnd(environment["FogEnd"].as<float>());
            
            // Sun Source
            if (environment["SunSource"])
                m_Scene->SetSunSource(environment["SunSource"].as<uint64_t>());
            
            // Halo/Flare
            if (environment["HaloEnabled"])
                m_Scene->SetHaloEnabled(environment["HaloEnabled"].as<bool>());
            if (environment["HaloStrength"])
                m_Scene->SetHaloStrength(environment["HaloStrength"].as<float>());
            if (environment["FlareEnabled"])
                m_Scene->SetFlareEnabled(environment["FlareEnabled"].as<bool>());
            if (environment["FlareFadeSpeed"])
                m_Scene->SetFlareFadeSpeed(environment["FlareFadeSpeed"].as<float>());
            if (environment["FlareStrength"])
                m_Scene->SetFlareStrength(environment["FlareStrength"].as<float>());
            
            // Flare Elements
            auto flareElements = environment["FlareElements"];
            if (flareElements)
            {
                m_Scene->SetFlareElementCount(flareElements.size());
                int index = 0;
                for (auto elementNode : flareElements)
                {
                    Scene::FlareElement element;
                    element.ColorMultiplier = elementNode["ColorMultiplier"].as<glm::vec3>();
                    element.Size = elementNode["Size"].as<float>();
                    element.Offset = elementNode["Offset"].as<float>();
                    element.Intensity = elementNode["Intensity"].as<float>();
                    m_Scene->SetFlareElement(index++, element);
                }
            }
        }
        
        // Editor Camera yükle
        auto editorCamera = data["EditorCamera"];
        if (editorCamera && m_EditorCamera)
        {
            if (editorCamera["CameraMode"])
                m_EditorCamera->SetCameraMode((EditorCameraMode)editorCamera["CameraMode"].as<int>());
            if (editorCamera["Position"])
                m_EditorCamera->SetPosition(editorCamera["Position"].as<glm::vec3>());
            if (editorCamera["Distance"])
                m_EditorCamera->SetDistance(editorCamera["Distance"].as<float>());
            if (editorCamera["Pitch"])
                m_EditorCamera->SetPitch(editorCamera["Pitch"].as<float>());
            if (editorCamera["Yaw"])
                m_EditorCamera->SetYaw(editorCamera["Yaw"].as<float>());
            if (editorCamera["OrthographicSize"])
                m_EditorCamera->SetOrthographicSize(editorCamera["OrthographicSize"].as<float>());
        }
        
        // Hierarchy (parent-child ilişkileri) yükle
        auto hierarchy = data["Hierarchy"];
        if (hierarchy)
        {
            // UUID -> entt::entity map'i oluştur
            std::unordered_map<uint64_t, entt::entity> uuidToEntity;
            m_Scene->m_Registry.view<IDComponent>().each([&](auto entity, auto& idComponent)
            {
                uuidToEntity[idComponent.ID] = entity;
            });
            
            // Parent-child ilişkilerini kur
            for (auto relation : hierarchy)
            {
                uint64_t childUUID = relation["Child"].as<uint64_t>();
                uint64_t parentUUID = relation["Parent"].as<uint64_t>();
                
                if (uuidToEntity.find(childUUID) != uuidToEntity.end() && 
                    uuidToEntity.find(parentUUID) != uuidToEntity.end())
                {
                    Entity childEntity = { uuidToEntity[childUUID], m_Scene.get() };
                    Entity parentEntity = { uuidToEntity[parentUUID], m_Scene.get() };
                    
                    m_Scene->SetEntityParent(childEntity, parentEntity);
                }
            }
        }
        
        // NavMesh yükle
        auto navMeshNode = data["NavMesh"];
        if (navMeshNode)
        {
            auto triangles = navMeshNode["Triangles"];
            if (triangles)
            {
                auto& navMesh = m_Scene->GetNavMesh();
                navMesh.Clear();
                for (auto tri : triangles)
                {
                    glm::vec3 v0 = tri["V0"].as<glm::vec3>();
                    glm::vec3 v1 = tri["V1"].as<glm::vec3>();
                    glm::vec3 v2 = tri["V2"].as<glm::vec3>();
                    int area = tri["Area"] ? tri["Area"].as<int>() : 0;
                    navMesh.AddTriangle(v0, v1, v2, area);
                }
                navMesh.BuildConnectivity();
                CQ_CORE_INFO("NavMesh deserialized with {0} triangles", navMesh.GetTriangles().size());
            }
        }

        return true;
    }

    bool SceneSerializer::DeserializeRuntime(const std::filesystem::path& filepath)
    {
        // Runtime deserialization için aynı format
        return Deserialize(filepath);
    }
}
