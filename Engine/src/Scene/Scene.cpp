#include "Scene.h"
#include "Renderer/Renderer.h"
#include "Components.h"
#include "Entity.h"
#include "Scripting/ScriptableEntity.h"
#include "Scripting/ScriptEngine.h"
#include "Scripting/ConquerorScript/CQSEngine.h"
#include "Renderer/Renderer2D.h"
#include "Renderer/TextRenderer.h"
#include "Renderer/Utilities/Renderer3D/Renderer3D.h"
#include "Renderer/Utilities/Renderer3D/Material.h"
#include "Core/Project/Project.h"
#include "Renderer/Utilities/Renderer3D/ModelLoader.h"
#include "Core/Input/Input.h"
#include "Core/Audio/AudioEngine.h"
#include "Core/Audio/AudioSource.h"
#include "Core/Audio/AudioFilter.h"
#include "Core/PhysicsSystem/Physics2D/PhysicsWorld2D.h"
#include "Core/PhysicsSystem/Physics3D/PhysicsWorld3D.h"
#include "Core/Debug/DebugDraw.h"
#include "Core/Debug/DebugMetrics.h"
#include "Core/Debug/DebugGridDraw.h"
#include "Core/Debug/DebugPalette.h"
#include "Core/Animation/Animator.h"
#include "Core/Animation/AnimationController.h"

#include <glm/glm.hpp>
#include <glad/glad.h>
#include <chrono>
#include <unordered_map>

namespace Conqueror
{
    // Predefined tag listesi
    std::vector<std::string> Scene::s_AvailableTags = {
        "Untagged",
        "Player",
        "Enemy",
        "Respawn",
        "Finish",
        "EditorOnly",
        "MainCamera",
        "GameController"
    };

    Scene::Scene()
    {
        // Physics world'leri oluştur
        m_PhysicsWorld2D = std::make_unique<PhysicsWorld2D>();
        m_PhysicsWorld3D = std::make_unique<PhysicsWorld3D>();
        m_PhysicsWorld2D->m_Scene = this;
        m_PhysicsWorld3D->m_Scene = this;
        m_PhysicsWorld2D->Initialize();
        m_PhysicsWorld3D->Initialize();

        // Default layer'ları oluştur
        m_Layers = {
            { 0, "Default" },
            { 1, "TransparentFX" },
            { 2, "Ignore Raycast" },
            { 3, "User Layer" },
            { 4, "Water" },
            { 5, "UI" }
        };
        
        // Default flare elements (3 adet)
        m_FlareElements.resize(3);
        m_FlareElements[0] = { glm::vec3(1.0f), 0.08f, 0.3f, 1.0f };
        m_FlareElements[1] = { glm::vec3(1.0f), 0.06f, 0.6f, 0.8f };
        m_FlareElements[2] = { glm::vec3(1.0f), 0.05f, 0.9f, 0.6f };
    }

    Scene::~Scene()
    {
        // Audio source'ları temizle
        auto audioView = m_Registry.view<AudioSourceComponent>();
        for (auto entity : audioView)
        {
            auto& audioSource = audioView.get<AudioSourceComponent>(entity);
            if (audioSource.RuntimeSound)
            {
                AudioSource* source = (AudioSource*)audioSource.RuntimeSound;
                delete source;
                audioSource.RuntimeSound = nullptr;
            }
        }
        
        m_PhysicsWorld2D->Shutdown();
        m_PhysicsWorld3D->Shutdown();
    }

    Entity Scene::CreateEntity(const std::string& name)
    {
        return CreateEntityWithUUID(0, name); // UUID will be generated
    }

    Entity Scene::CreateEntityWithUUID(uint64_t uuid, const std::string& name)
    {
        Entity entity = { m_Registry.create(), this };
        
        // UUID 0 ise generate et (timestamp + random)
        if (uuid == 0)
        {
            auto now = std::chrono::high_resolution_clock::now();
            auto timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count();
            uuid = (uint64_t)timestamp ^ ((uint64_t)rand() << 32) ^ (uint64_t)rand();
        }
        
        entity.AddComponent<IDComponent>(uuid);
        entity.AddComponent<TransformComponent>();
        entity.AddComponent<LayerComponent>(); // Default layer component
        auto& tag = entity.AddComponent<TagComponent>();
        tag.Tag = name.empty() ? "Entity" : name;
        return entity;
    }

    void Scene::DestroyEntity(Entity entity)
    {
        entt::entity entityHandle = entity;
        
        // Child'ları da sil
        auto children = GetEntityChildren(entity);
        for (auto child : children)
        {
            DestroyEntity(child);
        }

        // Parent ilişkisini kaldır
        RemoveEntityParent(entity);
        
        // Child listesini temizle
        m_EntityChildren.erase(entityHandle);

        // Registry'den sil
        m_Registry.destroy(entityHandle);
    }

    Entity Scene::DuplicateEntity(Entity source, const std::string& newName)
    {
        if (!source)
            return {};

        auto& srcTag = source.GetComponent<TagComponent>().Tag;
        std::string destName = newName.empty() ? srcTag + " (Copy)" : newName;
        Entity dest = CreateEntity(destName);

        if (source.HasComponent<TransformComponent>())
            dest.GetComponent<TransformComponent>() = source.GetComponent<TransformComponent>();

        if (source.HasComponent<LayerComponent>())
            dest.GetComponent<LayerComponent>() = source.GetComponent<LayerComponent>();

        if (source.HasComponent<SpriteRendererComponent>())
            dest.AddComponent<SpriteRendererComponent>() = source.GetComponent<SpriteRendererComponent>();

        if (source.HasComponent<MeshRendererComponent>())
            dest.AddComponent<MeshRendererComponent>() = source.GetComponent<MeshRendererComponent>();

        if (source.HasComponent<CameraComponent>())
            dest.AddComponent<CameraComponent>() = source.GetComponent<CameraComponent>();

        if (source.HasComponent<RigidBody2DComponent>())
        {
            auto& src = source.GetComponent<RigidBody2DComponent>();
            auto& dst = dest.AddComponent<RigidBody2DComponent>();
            dst.Type = src.Type;
            dst.FixedRotation = src.FixedRotation;
        }

        if (source.HasComponent<BoxCollider2DComponent>())
        {
            auto& src = source.GetComponent<BoxCollider2DComponent>();
            auto& dst = dest.AddComponent<BoxCollider2DComponent>();
            dst.Offset = src.Offset;
            dst.Size = src.Size;
            dst.Density = src.Density;
            dst.Friction = src.Friction;
            dst.Restitution = src.Restitution;
            dst.RestitutionThreshold = src.RestitutionThreshold;
        }

        if (source.HasComponent<CircleCollider2DComponent>())
        {
            auto& src = source.GetComponent<CircleCollider2DComponent>();
            auto& dst = dest.AddComponent<CircleCollider2DComponent>();
            dst.Offset = src.Offset;
            dst.Radius = src.Radius;
            dst.Density = src.Density;
            dst.Friction = src.Friction;
            dst.Restitution = src.Restitution;
            dst.RestitutionThreshold = src.RestitutionThreshold;
        }

        if (source.HasComponent<RigidbodyComponent>())
        {
            auto& src = source.GetComponent<RigidbodyComponent>();
            auto& dst = dest.AddComponent<RigidbodyComponent>();
            dst.Type = src.Type;
            dst.Mass = src.Mass;
            dst.LinearDrag = src.LinearDrag;
            dst.AngularDrag = src.AngularDrag;
            dst.GravityScale = src.GravityScale;
            dst.FreezeRotation = src.FreezeRotation;
            dst.Friction = src.Friction;
            dst.Restitution = src.Restitution;
        }

        if (source.HasComponent<BoxColliderComponent>())
            dest.AddComponent<BoxColliderComponent>() = source.GetComponent<BoxColliderComponent>();

        if (source.HasComponent<SphereColliderComponent>())
            dest.AddComponent<SphereColliderComponent>() = source.GetComponent<SphereColliderComponent>();

        if (source.HasComponent<CapsuleColliderComponent>())
            dest.AddComponent<CapsuleColliderComponent>() = source.GetComponent<CapsuleColliderComponent>();

        if (source.HasComponent<MeshColliderComponent>())
        {
            auto& src = source.GetComponent<MeshColliderComponent>();
            auto& dst = dest.AddComponent<MeshColliderComponent>();
            dst.IsConvex = src.IsConvex;
            dst.IsTrigger = src.IsTrigger;
        }

        if (source.HasComponent<TextRendererComponent>())
            dest.AddComponent<TextRendererComponent>() = source.GetComponent<TextRendererComponent>();

        if (source.HasComponent<ImageComponent>())
            dest.AddComponent<ImageComponent>() = source.GetComponent<ImageComponent>();

        if (source.HasComponent<ButtonComponent>())
        {
            auto& src = source.GetComponent<ButtonComponent>();
            auto& dst = dest.AddComponent<ButtonComponent>();
            dst.Interactable = src.Interactable;
            dst.Navigation = src.Navigation;
            dst.BackgroundNormalColor = src.BackgroundNormalColor;
            dst.BackgroundHoverColor = src.BackgroundHoverColor;
            dst.BackgroundPressedColor = src.BackgroundPressedColor;
            dst.BackgroundDisabledColor = src.BackgroundDisabledColor;
            dst.BackgroundHighlightedColor = src.BackgroundHighlightedColor;
            dst.TextNormalColor = src.TextNormalColor;
            dst.TextHoverColor = src.TextHoverColor;
            dst.TextPressedColor = src.TextPressedColor;
            dst.TextDisabledColor = src.TextDisabledColor;
            dst.TextHighlightedColor = src.TextHighlightedColor;
            dst.Use9Slice = src.Use9Slice;
            dst.BorderSize = src.BorderSize;
        }

        if (source.HasComponent<CanvasComponent>())
            dest.AddComponent<CanvasComponent>() = source.GetComponent<CanvasComponent>();

        if (source.HasComponent<CanvasScalerComponent>())
            dest.AddComponent<CanvasScalerComponent>() = source.GetComponent<CanvasScalerComponent>();

        if (source.HasComponent<GraphicRaycasterComponent>())
            dest.AddComponent<GraphicRaycasterComponent>() = source.GetComponent<GraphicRaycasterComponent>();

        if (source.HasComponent<ModelComponent>())
        {
            auto& src = source.GetComponent<ModelComponent>();
            auto& dst = dest.AddComponent<ModelComponent>();
            dst.ModelData = src.ModelData;
            dst.FilePath = src.FilePath;
        }

        if (source.HasComponent<AnimatorComponent>())
            dest.AddComponent<AnimatorComponent>() = source.GetComponent<AnimatorComponent>();

        if (source.HasComponent<DirectionalLightComponent>())
            dest.AddComponent<DirectionalLightComponent>() = source.GetComponent<DirectionalLightComponent>();

        if (source.HasComponent<PointLightComponent>())
            dest.AddComponent<PointLightComponent>() = source.GetComponent<PointLightComponent>();

        if (source.HasComponent<SpotLightComponent>())
            dest.AddComponent<SpotLightComponent>() = source.GetComponent<SpotLightComponent>();

        if (source.HasComponent<AudioSourceComponent>())
        {
            auto& src = source.GetComponent<AudioSourceComponent>();
            auto& dst = dest.AddComponent<AudioSourceComponent>();
            dst.FilePath = src.FilePath;
            dst.PlayOnAwake = src.PlayOnAwake;
            dst.Loop = src.Loop;
            dst.Mute = src.Mute;
            dst.Volume = src.Volume;
            dst.Pitch = src.Pitch;
            dst.Pan = src.Pan;
            dst.Is3D = src.Is3D;
            dst.MinDistance = src.MinDistance;
            dst.MaxDistance = src.MaxDistance;
            dst.DopplerLevel = src.DopplerLevel;
            dst.Attenuation = src.Attenuation;
            dst.RolloffFactor = src.RolloffFactor;
            dst.GraphNodes = src.GraphNodes;
            dst.GraphLinks = src.GraphLinks;
            dst.NextGraphNodeID = src.NextGraphNodeID;
            dst.NextGraphLinkID = src.NextGraphLinkID;
        }

        if (source.HasComponent<AudioListenerComponent>())
            dest.AddComponent<AudioListenerComponent>() = source.GetComponent<AudioListenerComponent>();

        if (source.HasComponent<AudioChorusFilterComponent>())
            dest.AddComponent<AudioChorusFilterComponent>() = source.GetComponent<AudioChorusFilterComponent>();

        if (source.HasComponent<AudioDistortionFilterComponent>())
            dest.AddComponent<AudioDistortionFilterComponent>() = source.GetComponent<AudioDistortionFilterComponent>();

        if (source.HasComponent<AudioEchoFilterComponent>())
            dest.AddComponent<AudioEchoFilterComponent>() = source.GetComponent<AudioEchoFilterComponent>();

        if (source.HasComponent<AudioHighPassFilterComponent>())
            dest.AddComponent<AudioHighPassFilterComponent>() = source.GetComponent<AudioHighPassFilterComponent>();

        if (source.HasComponent<AudioLowPassFilterComponent>())
            dest.AddComponent<AudioLowPassFilterComponent>() = source.GetComponent<AudioLowPassFilterComponent>();

        if (source.HasComponent<AudioReverbFilterComponent>())
            dest.AddComponent<AudioReverbFilterComponent>() = source.GetComponent<AudioReverbFilterComponent>();

        if (source.HasComponent<AudioReverbZoneComponent>())
            dest.AddComponent<AudioReverbZoneComponent>() = source.GetComponent<AudioReverbZoneComponent>();

        if (source.HasComponent<AudioGainComponent>())
            dest.AddComponent<AudioGainComponent>() = source.GetComponent<AudioGainComponent>();

        if (source.HasComponent<AudioPanComponent>())
            dest.AddComponent<AudioPanComponent>() = source.GetComponent<AudioPanComponent>();

        if (source.HasComponent<ConquerorScriptComponent>())
        {
            auto& src = source.GetComponent<ConquerorScriptComponent>();
            auto& dst = dest.AddComponent<ConquerorScriptComponent>();
            for (auto& script : src.Scripts)
            {
                ConquerorScriptData data;
                data.ScriptPath = script.ScriptPath;
                data.ClassName = script.ClassName;
                dst.Scripts.push_back(data);
            }
        }

        if (source.HasComponent<NavMeshAgentComponent>())
            dest.AddComponent<NavMeshAgentComponent>() = source.GetComponent<NavMeshAgentComponent>();

        if (source.HasComponent<NavMeshObstacleComponent>())
            dest.AddComponent<NavMeshObstacleComponent>() = source.GetComponent<NavMeshObstacleComponent>();

        if (source.HasComponent<NavMeshSurfaceComponent>())
            dest.AddComponent<NavMeshSurfaceComponent>() = source.GetComponent<NavMeshSurfaceComponent>();

        dest.GetComponent<TagComponent>().GameTag = source.GetComponent<TagComponent>().GameTag;

        return dest;
    }

    void Scene::CloneSceneFrom(const std::shared_ptr<Scene>& source)
    {
        if (!source) return;

        std::unordered_map<uint64_t, entt::entity> uuidMap;
        std::vector<entt::entity> physicsEntities;

        source->m_Registry.view<IDComponent>().each([&](auto entityID, auto& idComp)
        {
            Entity srcEntity{ entityID, source.get() };
            std::string name = srcEntity.GetComponent<TagComponent>().Tag;
            Entity newEntity = CreateEntityWithUUID(idComp.ID, name);
            uuidMap[idComp.ID] = newEntity;
        });

        auto& reg = m_Registry;

        source->m_Registry.view<IDComponent>().each([&](auto entityID, auto& idComp)
        {
            Entity srcEntity{ entityID, source.get() };
            entt::entity dstHandle = uuidMap[idComp.ID];

            if (srcEntity.HasComponent<TransformComponent>())
                reg.get<TransformComponent>(dstHandle) = srcEntity.GetComponent<TransformComponent>();

            if (srcEntity.HasComponent<LayerComponent>())
                reg.get<LayerComponent>(dstHandle) = srcEntity.GetComponent<LayerComponent>();

            if (srcEntity.HasComponent<TagComponent>())
                reg.get<TagComponent>(dstHandle).GameTag = srcEntity.GetComponent<TagComponent>().GameTag;

            if (srcEntity.HasComponent<SpriteRendererComponent>())
                reg.emplace<SpriteRendererComponent>(dstHandle) = srcEntity.GetComponent<SpriteRendererComponent>();

            if (srcEntity.HasComponent<MeshRendererComponent>())
                reg.emplace<MeshRendererComponent>(dstHandle) = srcEntity.GetComponent<MeshRendererComponent>();

            if (srcEntity.HasComponent<CameraComponent>())
                reg.emplace<CameraComponent>(dstHandle) = srcEntity.GetComponent<CameraComponent>();

            if (srcEntity.HasComponent<TextRendererComponent>())
                reg.emplace<TextRendererComponent>(dstHandle) = srcEntity.GetComponent<TextRendererComponent>();

            if (srcEntity.HasComponent<ImageComponent>())
                reg.emplace<ImageComponent>(dstHandle) = srcEntity.GetComponent<ImageComponent>();

            if (srcEntity.HasComponent<ButtonComponent>())
                reg.emplace<ButtonComponent>(dstHandle) = srcEntity.GetComponent<ButtonComponent>();

            if (srcEntity.HasComponent<CanvasComponent>())
                reg.emplace<CanvasComponent>(dstHandle) = srcEntity.GetComponent<CanvasComponent>();

            if (srcEntity.HasComponent<CanvasScalerComponent>())
                reg.emplace<CanvasScalerComponent>(dstHandle) = srcEntity.GetComponent<CanvasScalerComponent>();

            if (srcEntity.HasComponent<GraphicRaycasterComponent>())
                reg.emplace<GraphicRaycasterComponent>(dstHandle) = srcEntity.GetComponent<GraphicRaycasterComponent>();

            if (srcEntity.HasComponent<ModelComponent>())
            {
                auto& src = srcEntity.GetComponent<ModelComponent>();
                auto& dst = reg.emplace<ModelComponent>(dstHandle);
                dst.ModelData = src.ModelData;
                dst.FilePath = src.FilePath;
            }

            if (srcEntity.HasComponent<AnimatorComponent>())
                reg.emplace<AnimatorComponent>(dstHandle) = srcEntity.GetComponent<AnimatorComponent>();

            if (srcEntity.HasComponent<AnimationComponent>())
                reg.emplace<AnimationComponent>(dstHandle) = srcEntity.GetComponent<AnimationComponent>();

            if (srcEntity.HasComponent<DirectionalLightComponent>())
                reg.emplace<DirectionalLightComponent>(dstHandle) = srcEntity.GetComponent<DirectionalLightComponent>();

            if (srcEntity.HasComponent<PointLightComponent>())
                reg.emplace<PointLightComponent>(dstHandle) = srcEntity.GetComponent<PointLightComponent>();

            if (srcEntity.HasComponent<SpotLightComponent>())
                reg.emplace<SpotLightComponent>(dstHandle) = srcEntity.GetComponent<SpotLightComponent>();

            if (srcEntity.HasComponent<RigidBody2DComponent>())
            {
                auto& src = srcEntity.GetComponent<RigidBody2DComponent>();
                auto& dst = reg.emplace<RigidBody2DComponent>(dstHandle);
                dst.Type = src.Type;
                dst.FixedRotation = src.FixedRotation;
            }

            if (srcEntity.HasComponent<BoxCollider2DComponent>())
                reg.emplace<BoxCollider2DComponent>(dstHandle) = srcEntity.GetComponent<BoxCollider2DComponent>();

            if (srcEntity.HasComponent<CircleCollider2DComponent>())
                reg.emplace<CircleCollider2DComponent>(dstHandle) = srcEntity.GetComponent<CircleCollider2DComponent>();

            if (srcEntity.HasComponent<RigidbodyComponent>())
            {
                auto& src = srcEntity.GetComponent<RigidbodyComponent>();
                auto& dst = reg.emplace<RigidbodyComponent>(dstHandle);
                dst.Type = src.Type;
                dst.Mass = src.Mass;
                dst.LinearDrag = src.LinearDrag;
                dst.AngularDrag = src.AngularDrag;
                dst.GravityScale = src.GravityScale;
                dst.FreezeRotation = src.FreezeRotation;
                dst.Friction = src.Friction;
                dst.Restitution = src.Restitution;
                physicsEntities.push_back(dstHandle);
            }

            if (srcEntity.HasComponent<BoxColliderComponent>())
                reg.emplace<BoxColliderComponent>(dstHandle) = srcEntity.GetComponent<BoxColliderComponent>();

            if (srcEntity.HasComponent<SphereColliderComponent>())
                reg.emplace<SphereColliderComponent>(dstHandle) = srcEntity.GetComponent<SphereColliderComponent>();

            if (srcEntity.HasComponent<CapsuleColliderComponent>())
                reg.emplace<CapsuleColliderComponent>(dstHandle) = srcEntity.GetComponent<CapsuleColliderComponent>();

            if (srcEntity.HasComponent<MeshColliderComponent>())
            {
                auto& src = srcEntity.GetComponent<MeshColliderComponent>();
                auto& dst = reg.emplace<MeshColliderComponent>(dstHandle);
                dst.IsConvex = src.IsConvex;
                dst.IsTrigger = src.IsTrigger;
            }

            if (srcEntity.HasComponent<AudioSourceComponent>())
            {
                auto& src = srcEntity.GetComponent<AudioSourceComponent>();
                auto& dst = reg.emplace<AudioSourceComponent>(dstHandle);
                dst.FilePath = src.FilePath;
                dst.PlayOnAwake = src.PlayOnAwake;
                dst.Loop = src.Loop;
                dst.Mute = src.Mute;
                dst.Volume = src.Volume;
                dst.Pitch = src.Pitch;
                dst.Pan = src.Pan;
                dst.Is3D = src.Is3D;
                dst.MinDistance = src.MinDistance;
                dst.MaxDistance = src.MaxDistance;
                dst.DopplerLevel = src.DopplerLevel;
                dst.Attenuation = src.Attenuation;
                dst.RolloffFactor = src.RolloffFactor;
                dst.GraphNodes = src.GraphNodes;
                dst.GraphLinks = src.GraphLinks;
                dst.NextGraphNodeID = src.NextGraphNodeID;
                dst.NextGraphLinkID = src.NextGraphLinkID;
            }

            if (srcEntity.HasComponent<AudioListenerComponent>())
                reg.emplace<AudioListenerComponent>(dstHandle) = srcEntity.GetComponent<AudioListenerComponent>();

            if (srcEntity.HasComponent<AudioChorusFilterComponent>())
                reg.emplace<AudioChorusFilterComponent>(dstHandle) = srcEntity.GetComponent<AudioChorusFilterComponent>();

            if (srcEntity.HasComponent<AudioDistortionFilterComponent>())
                reg.emplace<AudioDistortionFilterComponent>(dstHandle) = srcEntity.GetComponent<AudioDistortionFilterComponent>();

            if (srcEntity.HasComponent<AudioEchoFilterComponent>())
                reg.emplace<AudioEchoFilterComponent>(dstHandle) = srcEntity.GetComponent<AudioEchoFilterComponent>();

            if (srcEntity.HasComponent<AudioHighPassFilterComponent>())
                reg.emplace<AudioHighPassFilterComponent>(dstHandle) = srcEntity.GetComponent<AudioHighPassFilterComponent>();

            if (srcEntity.HasComponent<AudioLowPassFilterComponent>())
                reg.emplace<AudioLowPassFilterComponent>(dstHandle) = srcEntity.GetComponent<AudioLowPassFilterComponent>();

            if (srcEntity.HasComponent<AudioReverbFilterComponent>())
                reg.emplace<AudioReverbFilterComponent>(dstHandle) = srcEntity.GetComponent<AudioReverbFilterComponent>();

            if (srcEntity.HasComponent<AudioReverbZoneComponent>())
                reg.emplace<AudioReverbZoneComponent>(dstHandle) = srcEntity.GetComponent<AudioReverbZoneComponent>();

            if (srcEntity.HasComponent<AudioGainComponent>())
                reg.emplace<AudioGainComponent>(dstHandle) = srcEntity.GetComponent<AudioGainComponent>();

            if (srcEntity.HasComponent<AudioPanComponent>())
                reg.emplace<AudioPanComponent>(dstHandle) = srcEntity.GetComponent<AudioPanComponent>();

            if (srcEntity.HasComponent<ConquerorScriptComponent>())
            {
                auto& src = srcEntity.GetComponent<ConquerorScriptComponent>();
                auto& dst = reg.emplace<ConquerorScriptComponent>(dstHandle);
                for (auto& script : src.Scripts)
                {
                    ConquerorScriptData data;
                    data.ScriptPath = script.ScriptPath;
                    data.ClassName = script.ClassName;
                    dst.Scripts.push_back(data);
                }
            }

            if (srcEntity.HasComponent<NavMeshAgentComponent>())
                reg.emplace<NavMeshAgentComponent>(dstHandle) = srcEntity.GetComponent<NavMeshAgentComponent>();

            if (srcEntity.HasComponent<NavMeshObstacleComponent>())
                reg.emplace<NavMeshObstacleComponent>(dstHandle) = srcEntity.GetComponent<NavMeshObstacleComponent>();

            if (srcEntity.HasComponent<NavMeshSurfaceComponent>())
                reg.emplace<NavMeshSurfaceComponent>(dstHandle) = srcEntity.GetComponent<NavMeshSurfaceComponent>();

            if (srcEntity.HasComponent<NativeScriptComponent>())
            {
                auto& src = srcEntity.GetComponent<NativeScriptComponent>();
                auto& dst = reg.emplace<NativeScriptComponent>(dstHandle);
                for (auto& script : src.Scripts)
                {
                    NativeScriptData data;
                    data.ModuleName = script.ModuleName;
                    data.ClassName = script.ClassName;
                    data.ScriptPath = script.ScriptPath;

                    if (!data.ScriptPath.empty() && !ScriptEngine::IsModuleLoaded(data.ModuleName))
                        ScriptEngine::LoadModule(data.ModuleName, data.ScriptPath);

                    if (!data.ModuleName.empty() && !data.ClassName.empty() && ScriptEngine::IsModuleLoaded(data.ModuleName))
                    {
                        data.InstantiateScript = [moduleName = data.ModuleName, className = data.ClassName]() {
                            return ScriptEngine::CreateScriptInstance(moduleName, className);
                        };
                    }
                    dst.Scripts.push_back(data);
                }
            }
        });

        for (auto& [srcChild, srcParent] : source->m_EntityParent)
        {
            Entity srcChildEntity{ srcChild, source.get() };
            uint64_t childUUID = srcChildEntity.GetComponent<IDComponent>().ID;

            Entity srcParentEntity{ srcParent, source.get() };
            uint64_t parentUUID = srcParentEntity.GetComponent<IDComponent>().ID;

            if (uuidMap.count(childUUID) && uuidMap.count(parentUUID))
                SetEntityParent({ uuidMap[childUUID], this }, { uuidMap[parentUUID], this });
        }

        for (auto handle : physicsEntities)
        {
            Entity e{ handle, this };
            if (m_PhysicsWorld3D) m_PhysicsWorld3D->CreateBody(e);
            if (m_PhysicsWorld2D) m_PhysicsWorld2D->CreateBody(e);
        }

        SetSkybox(source->GetSkybox());
        SetSkyboxExposure(source->GetSkyboxExposure());
        SetSkyboxRotation(source->GetSkyboxRotation());
        SetSkyboxTint(source->GetSkyboxTint());
        SetEnvironmentLightingSource(source->GetEnvironmentLightingSource());
        SetAmbientColor(source->GetAmbientColor());
        SetAmbientSkyColor(source->GetAmbientSkyColor());
        SetAmbientEquatorColor(source->GetAmbientEquatorColor());
        SetAmbientGroundColor(source->GetAmbientGroundColor());
        SetAmbientIntensity(source->GetAmbientIntensity());
        SetFogEnabled(source->IsFogEnabled());
        SetFogColor(source->GetFogColor());
        SetFogDensity(source->GetFogDensity());
        SetFogStart(source->GetFogStart());
        SetFogEnd(source->GetFogEnd());
        SetSunSource(source->GetSunSource());
        SetHaloEnabled(source->IsHaloEnabled());
        SetHaloStrength(source->GetHaloStrength());
        SetFlareEnabled(source->IsFlareEnabled());
        SetFlareFadeSpeed(source->GetFlareFadeSpeed());
        SetFlareStrength(source->GetFlareStrength());

        m_Layers = source->GetLayers();
    }

    void Scene::OnUpdateRuntime([[maybe_unused]] Timestep ts, bool renderOnly)
    {
        if (!renderOnly)
        {
        // Update Native Scripts - OnCreate
        {
            auto view = m_Registry.view<NativeScriptComponent>();
            
            for (auto entity : view)
            {
                auto& sc = view.get<NativeScriptComponent>(entity);
                for (auto& script : sc.Scripts)
                {
                    if (!script.Instance && script.InstantiateScript)
                    {
                        script.Instance = script.InstantiateScript();
                        script.Instance->m_Entity = Entity{ entity, this };
                        script.Instance->OnCreate();
                    }
                }
            }
        }

        // Update Conqueror Scripts - OnCreate
        {
            auto view = m_Registry.view<ConquerorScriptComponent>();
            for (auto entity : view)
            {
                auto& sc = view.get<ConquerorScriptComponent>(entity);
                for (auto& script : sc.Scripts)
                {
                    if (!script.Instance && !script.ScriptPath.empty() && !script.bLoaded)
                    {
                        script.bLoaded = true; // Sadece bir kez dene
                        if (CQS::CQSEngine::ExecuteFile(script.ScriptPath))
                        {
                            script.Instance = CQS::CQSEngine::Instantiate(script.ClassName, (uint32_t)entity);
                            if (script.Instance)
                                CQS::CQSEngine::Invoke(script.Instance, "OnCreate", 0.0f, this);
                        }
                    }
                }
            }
        }

        // Update audio system
        UpdateAudioSystem(ts);

        // Update physics
        if (m_PhysicsWarmedUp)
        {
            m_PhysicsWorld2D->Step(ts);
            m_PhysicsWorld3D->Step(ts);
        }
        else
        {
            m_PhysicsWarmedUp = true;
        }

        // Animasyon: ViewportPanel her karede OnUpdateEditor ile çiziyor; burada da güncellenirse zaman 2× hızlanır.

        // Update Native Scripts - OnUpdate
        {
            auto view = m_Registry.view<NativeScriptComponent>();
            for (auto entity : view)
            {
                auto& sc = view.get<NativeScriptComponent>(entity);
                for (auto& script : sc.Scripts)
                {
                    if (script.Instance)
                        script.Instance->OnUpdate(ts);
                }
            }
        }

        // Update Conqueror Scripts - OnUpdate
        {
            auto view = m_Registry.view<ConquerorScriptComponent>();
            for (auto entity : view)
            {
                auto& sc = view.get<ConquerorScriptComponent>(entity);
                for (auto& script : sc.Scripts)
                {
                    if (script.Instance)
                        CQS::CQSEngine::Invoke(script.Instance, "OnUpdate", (float)ts, this);
                }
            }
        }

        // Update AI / NavMesh Agents
        {
            auto view = m_Registry.view<TransformComponent, NavMeshAgentComponent>();
            for (auto entity : view)
            {
                auto [transform, agent] = view.get<TransformComponent, NavMeshAgentComponent>(entity);
                if (!agent.IsStopped && agent.HasPath && !agent.Path.empty())
                {
                    // Mock steering logic
                    glm::vec3 target = agent.Path[agent.CurrentPathIndex];
                    glm::vec3 dir = target - transform.Position;
                    float dist = glm::length(dir);
                    
                    if (dist > 0.05f)
                    {
                        dir = glm::normalize(dir);
                        transform.Position += dir * agent.Speed * (float)ts;
                        
                        // Rotasyonu hedefe döndür (basit y ekseni etrafında)
                        float angle = atan2(dir.x, dir.z);
                        transform.Rotation.y = angle;
                    }
                    else
                    {
                        agent.CurrentPathIndex++;
                        if ((size_t)agent.CurrentPathIndex >= agent.Path.size())
                        {
                            agent.HasPath = false; // Hedefe ulaştı
                            agent.IsStopped = true;
                        }
                    }
                }
            }
        }

        // Animators
        UpdateAnimators(ts);

        } // !renderOnly

        // Render Scene (Runtime)
        Entity cameraEntity = GetPrimaryCameraEntity();
        if (!cameraEntity)
        {
            auto view = m_Registry.view<CameraComponent>();
            if (!view.empty())
                cameraEntity = Entity{ view.front(), this };
        }

        if (cameraEntity)
        {
            auto& camera = cameraEntity.GetComponent<CameraComponent>().Camera;
            glm::mat4 cameraTransform = cameraEntity.GetComponent<TransformComponent>().GetTransform();

            // Stats'ı sıfırla
            Renderer::ResetStats();

            // Transform hierarchy'yi güncelle (root entity'lerden başla)
            auto transformView = m_Registry.view<TransformComponent>();
            for (auto entity : transformView)
            {
                Entity e{ entity, this };
                if (!GetEntityParent(e))
                {
                    UpdateTransformHierarchy(e, glm::mat4(1.0f));
                }
            }

            // 3D Rendering
            Renderer3D::BeginScene(camera, cameraTransform, (int)m_EnvironmentLightingSource,
                                   m_AmbientColor, m_AmbientSkyColor, m_AmbientEquatorColor, m_AmbientGroundColor,
                                   m_AmbientIntensity, m_FogEnabled, m_FogColor, m_FogStart, m_FogEnd);
            
            // Skybox'ı en önce çiz (arka plan)
            if (m_Skybox)
            {
                Renderer3D::DrawSkybox(m_Skybox, m_SkyboxExposure, m_SkyboxRotation, m_SkyboxTint);
            }
            
            // Light'ları topla ve gönder
            Renderer3D::ClearLights();
            
            uint32_t lightCount = 0;
            
            // Directional light (Sun Source)
            Entity sunSource = GetSunSourceEntity();
            if (sunSource && sunSource.HasComponent<DirectionalLightComponent>())
            {
                auto& dirLight = sunSource.GetComponent<DirectionalLightComponent>();
                Renderer3D::SetDirectionalLight(dirLight);
                lightCount++;
            }
            else
            {
                // Scene'de directional light yoksa default'a dön
                DirectionalLightComponent defaultLight;
                defaultLight.Direction = glm::vec3(-0.5f, -1.0f, -0.3f);
                defaultLight.Color = glm::vec3(1.0f, 1.0f, 1.0f);
                defaultLight.Intensity = 0.5f;
                Renderer3D::SetDirectionalLight(defaultLight);
            }
            
            // Point lights
            auto pointLightView = m_Registry.view<TransformComponent, PointLightComponent>();
            for (auto entity : pointLightView)
            {
                auto [transform, pointLight] = pointLightView.get<TransformComponent, PointLightComponent>(entity);
                Renderer3D::AddPointLight(transform.Position, pointLight);
                lightCount++;
            }
            
            // Spot lights
            auto spotLightView = m_Registry.view<TransformComponent, SpotLightComponent>();
            for (auto entity : spotLightView)
            {
                auto [transform, spotLight] = spotLightView.get<TransformComponent, SpotLightComponent>(entity);
                Renderer3D::AddSpotLight(transform.Position, spotLight);
                lightCount++;
            }
            
            // Light count'u stats'a ekle
            Renderer::GetStats().LightCount = lightCount;

            // Shadow pass
            if (m_ShadowEnabled && Renderer3D::GetShadowPass().IsReady())
            {
                Entity sunSrc = GetSunSourceEntity();
                if (sunSrc && sunSrc.HasComponent<DirectionalLightComponent>())
                {
                    auto& dl = sunSrc.GetComponent<DirectionalLightComponent>();
                    glm::vec3 camPos = glm::vec3(cameraTransform[3]);
                    glm::mat4 camVP = camera.GetProjection() * glm::inverse(cameraTransform);
                    Renderer3D::ExecuteShadowPass(this, dl, dl.Direction, camPos, camVP);
                }
            }

            // 3D objeleri render et (MeshRendererComponent)
            auto meshView = m_Registry.view<TransformComponent, MeshRendererComponent>();
            for (auto entity : meshView)
            {
                auto [transform, meshRenderer] = meshView.get<TransformComponent, MeshRendererComponent>(entity);
                
                std::shared_ptr<Material> material;
                
                if (meshRenderer.MaterialInstance)
                {
                    // Atanmış material'ı kullan
                    material = meshRenderer.MaterialInstance;
                }
                else
                {
                    // Fallback: component parametrelerinden geçici material oluştur
                    material = std::make_shared<Material>();
                    material->Albedo = glm::vec3(meshRenderer.Color);
                    material->Metallic = meshRenderer.Metallic;
                    material->Roughness = meshRenderer.Roughness;
                    material->AO = meshRenderer.AO;
                }

                if (meshRenderer.Texture && !material->AlbedoMap)
                    material->AlbedoMap = meshRenderer.Texture;
                
                switch (meshRenderer.Type)
                {
                    case MeshType::Sphere:   Renderer3D::DrawSphere(transform.WorldTransform, material); break;
                    case MeshType::Plane:    Renderer3D::DrawPlane(transform.WorldTransform, material); break;
                    case MeshType::Cylinder: Renderer3D::DrawCylinder(transform.WorldTransform, material); break;
                    default:                 Renderer3D::DrawCube(transform.WorldTransform, material); break;
                }
            }

            // Model'leri render et (ModelComponent)
            auto modelView = m_Registry.view<TransformComponent, ModelComponent>();
            for (auto entity : modelView)
            {
                auto [transform, modelComp] = modelView.get<TransformComponent, ModelComponent>(entity);
                
                if (modelComp.ModelData)
                {
                    Entity ren{ entity, this };
                    if (modelComp.ModelData->IsSkinned && modelComp.ModelData->SkeletonData)
                    {
                        const std::vector<glm::mat4>* bones = nullptr;
                        if (ren.HasComponent<AnimatorComponent>())
                            bones = &ren.GetComponent<AnimatorComponent>().BoneMatricesGPU;
                        else if (ren.HasComponent<AnimationComponent>())
                            bones = &ren.GetComponent<AnimationComponent>().BoneMatricesGPU;
                        if (bones && bones->size() == modelComp.ModelData->SkeletonData->GetBoneCount())
                            Renderer3D::DrawSkinnedModel(transform.WorldTransform, modelComp.ModelData, *bones);
                        else
                        {
                            static thread_local std::vector<glm::mat4> s_IdPalette;
                            s_IdPalette.assign(modelComp.ModelData->SkeletonData->GetBoneCount(), glm::mat4(1.f));
                            Renderer3D::DrawSkinnedModel(transform.WorldTransform, modelComp.ModelData, s_IdPalette);
                        }
                    }
                    else
                        Renderer3D::DrawModel(transform.WorldTransform, modelComp.ModelData);
                }
            }
            
            Renderer3D::EndScene();

            // 2D Rendering - Depth test AÇIK, depth write AÇIK
            Renderer2D::BeginScene(camera, cameraTransform, true, true);
            TextRenderer::BeginScene(camera, cameraTransform, true);

            // Tüm render edilebilir entity'leri topla (sprite + image + text)
            struct RenderItem
            {
                enum class Type { Sprite, Image, Text };
                Type ItemType;
                entt::entity Entity;
                TransformComponent* Transform;
                void* Component;
                int32_t Layer;
                int32_t OrderInLayer;
                float ZPosition;
            };

            std::vector<RenderItem> renderItems;

            // Sprite'ları topla
            auto spriteGroup = m_Registry.group<TransformComponent>(entt::get<SpriteRendererComponent>);
            for (auto entity : spriteGroup)
            {
                auto [transform, sprite] = spriteGroup.get<TransformComponent, SpriteRendererComponent>(entity);

                if (!sprite.Texture)
                    continue;

                int32_t layer = 0;
                int32_t orderInLayer = 0;
                if (m_Registry.all_of<LayerComponent>(entity))
                {
                    auto& layerComp = m_Registry.get<LayerComponent>(entity);
                    layer = layerComp.Layer;
                    orderInLayer = layerComp.OrderInLayer;
                }

                renderItems.push_back({ RenderItem::Type::Sprite, entity, &transform, (void*)&sprite, layer, orderInLayer, transform.WorldTransform[3][2] });
            }

            // Image'ları topla
            auto imageView = m_Registry.view<TransformComponent, ImageComponent>();
            for (auto entity : imageView)
            {
                auto [transform, image] = imageView.get<TransformComponent, ImageComponent>(entity);

                if (!image.Texture)
                    continue;

                int32_t layer = 5;
                int32_t orderInLayer = 0;
                if (m_Registry.all_of<LayerComponent>(entity))
                {
                    auto& layerComp = m_Registry.get<LayerComponent>(entity);
                    layer = layerComp.Layer;
                    orderInLayer = layerComp.OrderInLayer;
                }

                renderItems.push_back({ RenderItem::Type::Image, entity, &transform, (void*)&image, layer, orderInLayer, transform.WorldTransform[3][2] });
            }

            // Text'leri topla
            auto textView = m_Registry.view<TransformComponent, TextRendererComponent>();
            for (auto entity : textView)
            {
                auto [transform, text] = textView.get<TransformComponent, TextRendererComponent>(entity);

                int32_t layer = 5;
                int32_t orderInLayer = 0;
                if (m_Registry.all_of<LayerComponent>(entity))
                {
                    auto& layerComp = m_Registry.get<LayerComponent>(entity);
                    layer = layerComp.Layer;
                    orderInLayer = layerComp.OrderInLayer;
                }

                renderItems.push_back({ RenderItem::Type::Text, entity, &transform, (void*)&text, layer, orderInLayer, transform.WorldTransform[3][2] });
            }

            // Layer ve OrderInLayer'a göre sırala
            std::sort(renderItems.begin(), renderItems.end(), 
                [this](const RenderItem& a, const RenderItem& b) {
                    int32_t aSortOrder = 0;
                    int32_t bSortOrder = 0;
                    
                    Entity aEntity{ a.Entity, this };
                    Entity bEntity{ b.Entity, this };
                    
                    if (IsCanvasOrCanvasChild(aEntity))
                    {
                        Entity canvasA = aEntity;
                        while (canvasA && !canvasA.HasComponent<CanvasComponent>())
                        {
                            canvasA = GetEntityParent(canvasA);
                        }
                        if (canvasA && canvasA.HasComponent<CanvasComponent>())
                        {
                            aSortOrder = canvasA.GetComponent<CanvasComponent>().SortOrder;
                        }
                    }
                    
                    if (IsCanvasOrCanvasChild(bEntity))
                    {
                        Entity canvasB = bEntity;
                        while (canvasB && !canvasB.HasComponent<CanvasComponent>())
                        {
                            canvasB = GetEntityParent(canvasB);
                        }
                        if (canvasB && canvasB.HasComponent<CanvasComponent>())
                        {
                            bSortOrder = canvasB.GetComponent<CanvasComponent>().SortOrder;
                        }
                    }
                    
                    if (aSortOrder != bSortOrder) return aSortOrder < bSortOrder;
                    if (a.Layer != b.Layer) return a.Layer < b.Layer;
                    if (a.OrderInLayer != b.OrderInLayer) return a.OrderInLayer < b.OrderInLayer;
                    return a.ZPosition < b.ZPosition;
                });

            // TÜM objeleri render et (Canvas kontrolü ile depth test)
            for (auto& item : renderItems)
            {
                Entity itemEntity{ item.Entity, this };
                bool isCanvasChild = IsCanvasOrCanvasChild(itemEntity);
                
                bool depthTestEnabled = true;
                if (isCanvasChild)
                {
                    Entity canvasEntity = itemEntity;
                    while (canvasEntity && !canvasEntity.HasComponent<CanvasComponent>())
                    {
                        canvasEntity = GetEntityParent(canvasEntity);
                    }
                    
                    if (canvasEntity && canvasEntity.HasComponent<CanvasComponent>())
                    {
                        auto& canvas = canvasEntity.GetComponent<CanvasComponent>();
                        if (canvas.Mode == CanvasComponent::RenderMode::WorldSpace)
                            depthTestEnabled = true;
                        else
                            depthTestEnabled = false;
                    }
                }
                
                if (!depthTestEnabled)
                {
                    Renderer2D::EndScene();
                    TextRenderer::EndScene();
                    Renderer2D::BeginScene(camera, cameraTransform, false, true);
                    TextRenderer::BeginScene(camera, cameraTransform, false);
                }
                
                glm::mat4 finalTransform = item.Transform->WorldTransform;
                if (isCanvasChild)
                {
                    Entity canvasEntity = itemEntity;
                    while (canvasEntity && !canvasEntity.HasComponent<CanvasComponent>())
                    {
                        canvasEntity = GetEntityParent(canvasEntity);
                    }
                    
                    if (canvasEntity && canvasEntity.HasComponent<CanvasComponent>())
                    {
                        auto& canvas = canvasEntity.GetComponent<CanvasComponent>();
                        
                        if (canvasEntity.HasComponent<CanvasScalerComponent>())
                        {
                            auto& scaler = canvasEntity.GetComponent<CanvasScalerComponent>();
                            float pixelToWorldScale = 100.0f / scaler.ReferencePixelsPerUnit;
                            
                            if (scaler.ScaleMode == CanvasScalerComponent::UIScaleMode::ConstantPixelSize)
                            {
                                glm::vec3 scale = glm::vec3(finalTransform[0][0], finalTransform[1][1], finalTransform[2][2]);
                                scale *= scaler.ScaleFactor * pixelToWorldScale;
                                finalTransform[0][0] = scale.x; finalTransform[1][1] = scale.y; finalTransform[2][2] = scale.z;
                            }
                            else if (scaler.ScaleMode == CanvasScalerComponent::UIScaleMode::ScaleWithScreenSize)
                            {
                                float screenScale = (float)m_ViewportWidth / 1920.0f;
                                screenScale *= scaler.ScaleFactor * pixelToWorldScale;
                                glm::vec3 scale = glm::vec3(finalTransform[0][0], finalTransform[1][1], finalTransform[2][2]);
                                scale *= screenScale;
                                finalTransform[0][0] = scale.x; finalTransform[1][1] = scale.y; finalTransform[2][2] = scale.z;
                            }
                            else if (scaler.ScaleMode == CanvasScalerComponent::UIScaleMode::ConstantPhysicalSize)
                            {
                                float dpi = 96.0f;
                                float dpiScale = dpi / 96.0f;
                                dpiScale *= scaler.ScaleFactor * pixelToWorldScale;
                                glm::vec3 scale = glm::vec3(finalTransform[0][0], finalTransform[1][1], finalTransform[2][2]);
                                scale *= dpiScale;
                                finalTransform[0][0] = scale.x; finalTransform[1][1] = scale.y; finalTransform[2][2] = scale.z;
                            }
                        }
                        
                        if (canvas.PixelPerfect)
                        {
                            finalTransform[3][0] = glm::round(finalTransform[3][0]);
                            finalTransform[3][1] = glm::round(finalTransform[3][1]);
                        }
                    }
                }
                
                if (item.ItemType == RenderItem::Type::Sprite)
                {
                    auto* sprite = (SpriteRendererComponent*)item.Component;
                    Renderer2D::DrawSprite(finalTransform, sprite->Texture, sprite->TilingFactor, sprite->Color, *sprite);
                }
                else if (item.ItemType == RenderItem::Type::Image)
                {
                    auto* image = (ImageComponent*)item.Component;
                    glm::vec4 finalColor = image->Color;
                    bool use9Slice = false;
                    float borderSize = 10.0f;
                    
                    if (m_Registry.all_of<ButtonComponent>(item.Entity))
                    {
                        auto& button = m_Registry.get<ButtonComponent>(item.Entity);
                        use9Slice = button.Use9Slice;
                        borderSize = button.BorderSize;
                        finalColor = button.CurrentBackgroundColor;
                    }
                    
                    if (use9Slice)
                        Renderer2D::DrawSprite9Slice(finalTransform, image->Texture, borderSize, finalColor, *image);
                    else
                        Renderer2D::DrawSprite(finalTransform, image->Texture, image->TilingFactor, finalColor, *image);
                }
                else if (item.ItemType == RenderItem::Type::Text)
                {
                    auto* text = (TextRendererComponent*)item.Component;
                    glm::vec4 finalTextColor = text->Color;
                    glm::mat4 textTransform = finalTransform;
                    
                    Entity textEntity{ item.Entity, this };
                    Entity parent = GetEntityParent(textEntity);
                    
                    if (parent && parent.HasComponent<ButtonComponent>() && parent.HasComponent<ImageComponent>())
                    {
                        auto& button = parent.GetComponent<ButtonComponent>();
                        auto& parentImage = parent.GetComponent<ImageComponent>();
                        finalTextColor = button.CurrentTextColor;
                        
                        if (parentImage.Texture)
                        {
                            float halfWidth = (float)parentImage.Texture->GetWidth() / 200.0f;
                            float halfHeight = (float)parentImage.Texture->GetHeight() / 200.0f;
                            glm::vec2 offset = glm::vec2(-halfWidth * 0.93f, -halfHeight * 0.5f);
                            textTransform[3][0] += offset.x;
                            textTransform[3][1] += offset.y;
                        }
                    }
                    
                    TextRenderer::DrawText(text->Text, textTransform, text->FontSize / 100.0f, finalTextColor);
                }
                
                if (!isCanvasChild)
                {
                    Renderer2D::EndScene();
                    TextRenderer::EndScene();
                    Renderer2D::BeginScene(camera, cameraTransform, true, true);
                    TextRenderer::BeginScene(camera, cameraTransform, true);
                }
                else if (depthTestEnabled)
                {
                    Renderer2D::EndScene();
                    TextRenderer::EndScene();
                    Renderer2D::BeginScene(camera, cameraTransform, true, true);
                    TextRenderer::BeginScene(camera, cameraTransform, true);
                }
            }

            Renderer2D::EndScene();
            TextRenderer::EndScene();
        }
    }

    void Scene::OnUpdateSimulation([[maybe_unused]] Timestep ts)
    {
        // Update physics only
        m_PhysicsWorld2D->Step(ts);
        m_PhysicsWorld3D->Step(ts);
    }

    void Scene::OnUpdateEditor([[maybe_unused]] Timestep ts, EditorCamera& camera)
    {
        OnUpdateEditor(ts, camera, nullptr);
    }

    void Scene::OnUpdateEditor([[maybe_unused]] Timestep ts, EditorCamera& camera, const glm::vec2* viewportBounds)
    {
        // Stats'ı sıfırla
        Renderer::ResetStats();
        
        // Transform hierarchy'yi güncelle (root entity'lerden başla)
        auto view = m_Registry.view<TransformComponent>();
        for (auto entity : view)
        {
            Entity e{ entity, this };
            
            // Sadece root entity'ler için (parent'ı olmayan)
            if (!GetEntityParent(e))
            {
                UpdateTransformHierarchy(e, glm::mat4(1.0f));
            }
        }

        // Buton input sistemi
        UpdateButtonInput(camera, ts, viewportBounds);

        UpdateAudioSystem(ts);

        // 3D Rendering
        Renderer3D::BeginScene(camera, (int)m_EnvironmentLightingSource,
                               m_AmbientColor, m_AmbientSkyColor, m_AmbientEquatorColor, m_AmbientGroundColor,
                               m_AmbientIntensity, m_FogEnabled, m_FogColor, m_FogStart, m_FogEnd);
        
        // Skybox'ı en önce çiz (arka plan)
        if (m_Skybox)
        {
            Renderer3D::DrawSkybox(m_Skybox, m_SkyboxExposure, m_SkyboxRotation, m_SkyboxTint);
        }
        
        // Light'ları topla ve gönder
        Renderer3D::ClearLights();
        
        uint32_t lightCount = 0;
        
        // Directional light (Sun Source)
        Entity sunSource = GetSunSourceEntity();
        if (sunSource && sunSource.HasComponent<DirectionalLightComponent>())
        {
            auto& dirLight = sunSource.GetComponent<DirectionalLightComponent>();
            Renderer3D::SetDirectionalLight(dirLight);
            lightCount++;
        }
        else
        {
            // Scene'de directional light yoksa default'a dön
            DirectionalLightComponent defaultLight;
            defaultLight.Direction = glm::vec3(-0.5f, -1.0f, -0.3f);
            defaultLight.Color = glm::vec3(1.0f, 1.0f, 1.0f);
            defaultLight.Intensity = 0.5f;
            Renderer3D::SetDirectionalLight(defaultLight);
        }
        
        // Point lights
        auto pointLightView = m_Registry.view<TransformComponent, PointLightComponent>();
        for (auto entity : pointLightView)
        {
            auto [transform, pointLight] = pointLightView.get<TransformComponent, PointLightComponent>(entity);
            Renderer3D::AddPointLight(transform.Position, pointLight);
            lightCount++;
        }
        
        // Spot lights
        auto spotLightView = m_Registry.view<TransformComponent, SpotLightComponent>();
        for (auto entity : spotLightView)
        {
            auto [transform, spotLight] = spotLightView.get<TransformComponent, SpotLightComponent>(entity);
            Renderer3D::AddSpotLight(transform.Position, spotLight);
            lightCount++;
        }
        
        // Light count'u stats'a ekle
        Renderer::GetStats().LightCount = lightCount;

        // Shadow pass (editor)
        if (m_ShadowEnabled && Renderer3D::GetShadowPass().IsReady())
        {
            Entity sunSrc = GetSunSourceEntity();
            if (sunSrc && sunSrc.HasComponent<DirectionalLightComponent>())
            {
                auto& dl = sunSrc.GetComponent<DirectionalLightComponent>();
                glm::vec3 camPos = camera.GetPosition();
                glm::mat4 camVP = camera.GetViewProjection();
                Renderer3D::ExecuteShadowPass(this, dl, dl.Direction, camPos, camVP);
            }
        }
        
        // 3D objeleri render et (MeshRendererComponent)
        auto meshView = m_Registry.view<TransformComponent, MeshRendererComponent>();
        for (auto entity : meshView)
        {
            auto [transform, meshRenderer] = meshView.get<TransformComponent, MeshRendererComponent>(entity);
            
            std::shared_ptr<Material> material;
            
            if (meshRenderer.MaterialInstance)
            {
                material = meshRenderer.MaterialInstance;
            }
            else
            {
                material = std::make_shared<Material>();
                material->Albedo = glm::vec3(meshRenderer.Color);
                material->Metallic = meshRenderer.Metallic;
                material->Roughness = meshRenderer.Roughness;
                material->AO = meshRenderer.AO;
            }

            if (meshRenderer.Texture && !material->AlbedoMap)
                material->AlbedoMap = meshRenderer.Texture;
            
            switch (meshRenderer.Type)
            {
                case MeshType::Sphere:   Renderer3D::DrawSphere(transform.WorldTransform, material); break;
                case MeshType::Plane:    Renderer3D::DrawPlane(transform.WorldTransform, material); break;
                case MeshType::Cylinder: Renderer3D::DrawCylinder(transform.WorldTransform, material); break;
                default:                 Renderer3D::DrawCube(transform.WorldTransform, material); break;
            }
        }

        // Model'leri render et (ModelComponent)
        auto modelView = m_Registry.view<TransformComponent, ModelComponent>();
        for (auto entity : modelView)
        {
            auto [transform, modelComp] = modelView.get<TransformComponent, ModelComponent>(entity);
            
            if (modelComp.ModelData)
            {
                Entity ren{ entity, this };
                if (modelComp.ModelData->IsSkinned && modelComp.ModelData->SkeletonData)
                {
                    const std::vector<glm::mat4>* bones = nullptr;
                    if (ren.HasComponent<AnimatorComponent>())
                        bones = &ren.GetComponent<AnimatorComponent>().BoneMatricesGPU;
                    else if (ren.HasComponent<AnimationComponent>())
                        bones = &ren.GetComponent<AnimationComponent>().BoneMatricesGPU;
                    if (bones && bones->size() == modelComp.ModelData->SkeletonData->GetBoneCount())
                        Renderer3D::DrawSkinnedModel(transform.WorldTransform, modelComp.ModelData, *bones);
                    else
                    {
                        static thread_local std::vector<glm::mat4> s_IdPalette;
                        s_IdPalette.assign(modelComp.ModelData->SkeletonData->GetBoneCount(), glm::mat4(1.f));
                        Renderer3D::DrawSkinnedModel(transform.WorldTransform, modelComp.ModelData, s_IdPalette);
                    }
                }
                else
                    Renderer3D::DrawModel(transform.WorldTransform, modelComp.ModelData);
            }
        }
        
        Renderer3D::EndScene();

        // Light gizmo'larını çiz (editor-only)
        Renderer3D::BeginScene(camera);
        
        // Point light gizmo'ları
        auto pointLightGizmoView = m_Registry.view<TransformComponent, PointLightComponent>();
        for (auto entity : pointLightGizmoView)
        {
            auto [transform, pointLight] = pointLightGizmoView.get<TransformComponent, PointLightComponent>(entity);
            Renderer3D::DrawLightGizmo(transform.Position, pointLight.Color, 0.2f);
        }
        
        // Spot light gizmo'ları
        auto spotLightGizmoView = m_Registry.view<TransformComponent, SpotLightComponent>();
        for (auto entity : spotLightGizmoView)
        {
            auto [transform, spotLight] = spotLightGizmoView.get<TransformComponent, SpotLightComponent>(entity);
            Renderer3D::DrawSpotLightGizmo(transform.Position, spotLight.Direction, spotLight.Color, 0.2f);
        }
        
        // Directional light gizmo'ları (position'dan bağımsız, sadece gösterge)
        auto dirLightGizmoView = m_Registry.view<TransformComponent, DirectionalLightComponent>();
        for (auto entity : dirLightGizmoView)
        {
            auto [transform, dirLight] = dirLightGizmoView.get<TransformComponent, DirectionalLightComponent>(entity);
            Renderer3D::DrawDirectionalLightGizmo(transform.Position, dirLight.Direction, dirLight.Color, 0.3f);
        }

        // Camera gizmo'ları
        auto cameraGizmoView = m_Registry.view<TransformComponent, CameraComponent>();
        for (auto entity : cameraGizmoView)
        {
            auto [transform, camComp] = cameraGizmoView.get<TransformComponent, CameraComponent>(entity);
            glm::vec3 forward = glm::rotate(glm::quat(transform.Rotation), glm::vec3(0.0f, 0.0f, -1.0f));
            glm::vec3 color = camComp.Primary ? glm::vec3(1.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 1.0f, 1.0f);
            Renderer3D::DrawLightGizmo(transform.Position, color, 0.2f);
            Renderer3D::DrawDirectionalLightGizmo(transform.Position, forward, color, 0.4f);
        }
        
        Renderer3D::EndScene();

        // 2D Rendering - Depth test AÇIK, depth write AÇIK
        Renderer2D::BeginScene(camera, true, true); // Depth test enabled, depth write enabled
        TextRenderer::BeginScene(camera, true); // Depth test enabled

        // Tüm render edilebilir entity'leri topla (sprite + image + text)
        struct RenderItem
        {
            enum class Type { Sprite, Image, Text };
            Type ItemType;
            entt::entity Entity;
            TransformComponent* Transform;
            void* Component; // SpriteRendererComponent*, ImageComponent* veya TextRendererComponent*
            int32_t Layer;
            int32_t OrderInLayer;
            float ZPosition; // Z-depth sorting için
        };

        std::vector<RenderItem> renderItems;

        // Sprite'ları topla
        auto spriteGroup = m_Registry.group<TransformComponent>(entt::get<SpriteRendererComponent>);
        for (auto entity : spriteGroup)
        {
            auto [transform, sprite] = spriteGroup.get<TransformComponent, SpriteRendererComponent>(entity);

            if (!sprite.Texture)
                continue;

            int32_t layer = 0;
            int32_t orderInLayer = 0;
            if (m_Registry.all_of<LayerComponent>(entity))
            {
                auto& layerComp = m_Registry.get<LayerComponent>(entity);
                layer = layerComp.Layer;
                orderInLayer = layerComp.OrderInLayer;
            }

            renderItems.push_back({ 
                RenderItem::Type::Sprite, 
                entity, 
                &transform, 
                (void*)&sprite, 
                layer, 
                orderInLayer,
                transform.WorldTransform[3][2] // Z pozisyonu
            });
        }

        // Image'ları topla
        auto imageView = m_Registry.view<TransformComponent, ImageComponent>();
        for (auto entity : imageView)
        {
            auto [transform, image] = imageView.get<TransformComponent, ImageComponent>(entity);

            if (!image.Texture)
                continue;

            int32_t layer = 5; // Default UI layer
            int32_t orderInLayer = 0;
            if (m_Registry.all_of<LayerComponent>(entity))
            {
                auto& layerComp = m_Registry.get<LayerComponent>(entity);
                layer = layerComp.Layer;
                orderInLayer = layerComp.OrderInLayer;
            }

            renderItems.push_back({ 
                RenderItem::Type::Image, 
                entity, 
                &transform, 
                (void*)&image, 
                layer, 
                orderInLayer,
                transform.WorldTransform[3][2] // Z pozisyonu
            });
        }

        // Text'leri topla
        auto textView = m_Registry.view<TransformComponent, TextRendererComponent>();
        for (auto entity : textView)
        {
            auto [transform, text] = textView.get<TransformComponent, TextRendererComponent>(entity);

            int32_t layer = 5; // Default UI layer
            int32_t orderInLayer = 0;
            if (m_Registry.all_of<LayerComponent>(entity))
            {
                auto& layerComp = m_Registry.get<LayerComponent>(entity);
                layer = layerComp.Layer;
                orderInLayer = layerComp.OrderInLayer;
            }

            renderItems.push_back({ 
                RenderItem::Type::Text, 
                entity, 
                &transform, 
                (void*)&text, 
                layer, 
                orderInLayer,
                transform.WorldTransform[3][2] // Z pozisyonu
            });
        }

        // Layer ve OrderInLayer'a göre sırala
        std::sort(renderItems.begin(), renderItems.end(), 
            [this](const RenderItem& a, const RenderItem& b) {
                // Canvas Sort Order kontrolü
                int32_t aSortOrder = 0;
                int32_t bSortOrder = 0;
                
                Entity aEntity{ a.Entity, this };
                Entity bEntity{ b.Entity, this };
                
                // A için Canvas Sort Order
                if (IsCanvasOrCanvasChild(aEntity))
                {
                    Entity canvasA = aEntity;
                    while (canvasA && !canvasA.HasComponent<CanvasComponent>())
                    {
                        canvasA = GetEntityParent(canvasA);
                    }
                    if (canvasA && canvasA.HasComponent<CanvasComponent>())
                    {
                        aSortOrder = canvasA.GetComponent<CanvasComponent>().SortOrder;
                    }
                }
                
                // B için Canvas Sort Order
                if (IsCanvasOrCanvasChild(bEntity))
                {
                    Entity canvasB = bEntity;
                    while (canvasB && !canvasB.HasComponent<CanvasComponent>())
                    {
                        canvasB = GetEntityParent(canvasB);
                    }
                    if (canvasB && canvasB.HasComponent<CanvasComponent>())
                    {
                        bSortOrder = canvasB.GetComponent<CanvasComponent>().SortOrder;
                    }
                }
                
                // Canvas Sort Order farklıysa ona göre sırala
                if (aSortOrder != bSortOrder)
                    return aSortOrder < bSortOrder;
                
                // Canvas Sort Order aynıysa Layer'a göre
                if (a.Layer != b.Layer)
                    return a.Layer < b.Layer;
                
                // Layer aynıysa OrderInLayer'a göre
                if (a.OrderInLayer != b.OrderInLayer)
                    return a.OrderInLayer < b.OrderInLayer;
                
                // OrderInLayer da aynıysa Z pozisyonuna göre (back-to-front, uzak objeler önce)
                return a.ZPosition < b.ZPosition;
            });

        // TÜM objeleri render et (Canvas kontrolü ile depth test)
        for (auto& item : renderItems)
        {
            Entity itemEntity{ item.Entity, this };
            bool isCanvasChild = IsCanvasOrCanvasChild(itemEntity);
            
            // Canvas child ama World Space ise depth test AÇIK
            bool depthTestEnabled = true;
            if (isCanvasChild)
            {
                // Canvas'ın Render Mode'unu kontrol et
                Entity canvasEntity = itemEntity;
                while (canvasEntity && !canvasEntity.HasComponent<CanvasComponent>())
                {
                    canvasEntity = GetEntityParent(canvasEntity);
                }
                
                if (canvasEntity && canvasEntity.HasComponent<CanvasComponent>())
                {
                    auto& canvas = canvasEntity.GetComponent<CanvasComponent>();
                    if (canvas.Mode == CanvasComponent::RenderMode::WorldSpace)
                    {
                        depthTestEnabled = true; // World Space: depth test AÇIK
                    }
                    else
                    {
                        depthTestEnabled = false; // Overlay/Camera: depth test KAPALI
                    }
                }
            }
            
            if (!depthTestEnabled)
            {
                Renderer2D::EndScene();
                TextRenderer::EndScene();
                Renderer2D::BeginScene(camera, false);
                TextRenderer::BeginScene(camera, false);
            }
            
            // Pixel Perfect: Canvas child ise ve Pixel Perfect açıksa pozisyonu yuvarla
            glm::mat4 finalTransform = item.Transform->WorldTransform;
            if (isCanvasChild)
            {
                Entity canvasEntity = itemEntity;
                while (canvasEntity && !canvasEntity.HasComponent<CanvasComponent>())
                {
                    canvasEntity = GetEntityParent(canvasEntity);
                }
                
                if (canvasEntity && canvasEntity.HasComponent<CanvasComponent>())
                {
                    auto& canvas = canvasEntity.GetComponent<CanvasComponent>();
                    
                    // UI Scale Mode: Canvas Scaler varsa scale uygula
                    if (canvasEntity.HasComponent<CanvasScalerComponent>())
                    {
                        auto& scaler = canvasEntity.GetComponent<CanvasScalerComponent>();
                        
                        // Reference Pixels Per Unit: Sprite boyutunu dünya birimine çevir
                        float pixelToWorldScale = 100.0f / scaler.ReferencePixelsPerUnit;
                        
                        if (scaler.ScaleMode == CanvasScalerComponent::UIScaleMode::ConstantPixelSize)
                        {
                            // Scale Factor uygula
                            glm::vec3 scale = glm::vec3(finalTransform[0][0], finalTransform[1][1], finalTransform[2][2]);
                            scale *= scaler.ScaleFactor * pixelToWorldScale;
                            finalTransform[0][0] = scale.x;
                            finalTransform[1][1] = scale.y;
                            finalTransform[2][2] = scale.z;
                        }
                        else if (scaler.ScaleMode == CanvasScalerComponent::UIScaleMode::ScaleWithScreenSize)
                        {
                            // Viewport boyutuna göre scale
                            float screenScale = (float)m_ViewportWidth / 1920.0f; // Reference: 1920x1080
                            screenScale *= scaler.ScaleFactor * pixelToWorldScale;
                            
                            glm::vec3 scale = glm::vec3(finalTransform[0][0], finalTransform[1][1], finalTransform[2][2]);
                            scale *= screenScale;
                            finalTransform[0][0] = scale.x;
                            finalTransform[1][1] = scale.y;
                            finalTransform[2][2] = scale.z;
                        }
                        else if (scaler.ScaleMode == CanvasScalerComponent::UIScaleMode::ConstantPhysicalSize)
                        {
                            // DPI bazlı scale (96 DPI referans)
                            float dpi = 96.0f; // TODO: Platform'dan gerçek DPI al
                            float dpiScale = dpi / 96.0f;
                            dpiScale *= scaler.ScaleFactor * pixelToWorldScale;
                            
                            glm::vec3 scale = glm::vec3(finalTransform[0][0], finalTransform[1][1], finalTransform[2][2]);
                            scale *= dpiScale;
                            finalTransform[0][0] = scale.x;
                            finalTransform[1][1] = scale.y;
                            finalTransform[2][2] = scale.z;
                        }
                    }
                    
                    // Pixel Perfect
                    if (canvas.PixelPerfect)
                    {
                        // Pozisyonu tam piksele yuvarla
                        finalTransform[3][0] = glm::round(finalTransform[3][0]);
                        finalTransform[3][1] = glm::round(finalTransform[3][1]);
                    }
                }
            }
            
            if (item.ItemType == RenderItem::Type::Sprite)
            {
                auto* sprite = (SpriteRendererComponent*)item.Component;
                Renderer2D::DrawSprite(finalTransform, sprite->Texture, 
                                       sprite->TilingFactor, sprite->Color, *sprite);
            }
            else if (item.ItemType == RenderItem::Type::Image)
            {
                auto* image = (ImageComponent*)item.Component;
                
                glm::vec4 finalColor = image->Color;
                bool use9Slice = false;
                float borderSize = 10.0f;
                
                if (m_Registry.all_of<ButtonComponent>(item.Entity))
                {
                    auto& button = m_Registry.get<ButtonComponent>(item.Entity);
                    use9Slice = button.Use9Slice;
                    borderSize = button.BorderSize;
                    finalColor = button.CurrentBackgroundColor;
                }
                
                if (use9Slice)
                {
                    Renderer2D::DrawSprite9Slice(finalTransform, image->Texture, 
                                                 borderSize, finalColor, *image);
                }
                else
                {
                    Renderer2D::DrawSprite(finalTransform, image->Texture, 
                                           image->TilingFactor, finalColor, *image);
                }
            }
            else if (item.ItemType == RenderItem::Type::Text)
            {
                auto* text = (TextRendererComponent*)item.Component;
                glm::vec4 finalTextColor = text->Color;
                glm::mat4 textTransform = finalTransform;
                
                Entity textEntity{ item.Entity, this };
                Entity parent = GetEntityParent(textEntity);
                
                if (parent && parent.HasComponent<ButtonComponent>() && parent.HasComponent<ImageComponent>())
                {
                    auto& button = parent.GetComponent<ButtonComponent>();
                    auto& parentImage = parent.GetComponent<ImageComponent>();
                    
                    finalTextColor = button.CurrentTextColor;
                    
                    if (parentImage.Texture)
                    {
                        float halfWidth = (float)parentImage.Texture->GetWidth() / 200.0f;
                        float halfHeight = (float)parentImage.Texture->GetHeight() / 200.0f;
                        
                        float offsetMultiplierX = 0.93f;
                        float offsetMultiplierY = 0.5f;
                        glm::vec2 offset = glm::vec2(-halfWidth * offsetMultiplierX, -halfHeight * offsetMultiplierY);
                        
                        textTransform[3][0] += offset.x;
                        textTransform[3][1] += offset.y;
                    }
                }
                
                TextRenderer::DrawText(text->Text, textTransform, 
                                       text->FontSize / 100.0f, finalTextColor);
            }
            
            if (!isCanvasChild)
            {
                Renderer2D::EndScene();
                TextRenderer::EndScene();
                Renderer2D::BeginScene(camera, true);
                TextRenderer::BeginScene(camera, true);
            }
            else if (depthTestEnabled)
            {
                Renderer2D::EndScene();
                TextRenderer::EndScene();
                Renderer2D::BeginScene(camera, true);
                TextRenderer::BeginScene(camera, true);
            }
        }

        DebugDraw::SetCameraPosition(camera.GetPosition());
        DebugDraw::Update(ts);

        float camDist = camera.Is2D() ? camera.GetOrthographicSize() : camera.GetDistance();
        float gridStep = glm::max(1.0f, (camDist * camDist) / 500.0f);
        
        float fadeRadius = glm::max(35.0f, camDist * 4.0f);
        DebugDraw::SetGridFadeRadius(fadeRadius);
        DebugDraw::SetGridStep(gridStep);

        int lineCount = static_cast<int>(fadeRadius / gridStep) + 2;

        glm::vec3 camPos = camera.GetPosition();
        if (camera.Is2D())
        {
            glm::vec3 originXY = glm::vec3(std::floor(camPos.x / gridStep) * gridStep, std::floor(camPos.y / gridStep) * gridStep, 0.0f);
            DebugGridDraw::InfiniteXY(originXY, gridStep, lineCount, DebugPalette::GridColor, 0.0f, true);
        }
        else
        {
            glm::vec3 originXZ = glm::vec3(std::floor(camPos.x / gridStep) * gridStep, 0.0f, std::floor(camPos.z / gridStep) * gridStep);
            DebugGridDraw::InfiniteXZ(originXZ, gridStep, lineCount, DebugPalette::GridColor, 0.0f, true);
        }

        DebugDraw::Render(camera.GetViewProjection());

        Renderer2D::EndScene();
        TextRenderer::EndScene();
        DebugMetrics::SyncToRendererStats(Renderer::GetStats());
        DebugDraw::Clear();
    }

    void Scene::OnViewportResize(uint32_t width, uint32_t height)
    {
        m_ViewportWidth = width;
        m_ViewportHeight = height;

        // Resize cameras
        auto view = m_Registry.view<CameraComponent>();
        for (auto entity : view)
        {
            auto& cameraComponent = view.get<CameraComponent>(entity);
            if (!cameraComponent.FixedAspectRatio)
                cameraComponent.Camera.SetViewportSize(width, height);
        }
    }

    Entity Scene::GetPrimaryCameraEntity()
    {
        auto view = m_Registry.view<CameraComponent>();
        for (auto entity : view)
        {
            const auto& camera = view.get<CameraComponent>(entity);
            if (camera.Primary)
                return Entity{ entity, this };
        }
        return {};
    }

    Entity Scene::GetSunSourceEntity()
    {
        // Sun Source UUID varsa onu bul
        if (m_SunSourceUUID != 0)
        {
            auto view = m_Registry.view<IDComponent, DirectionalLightComponent>();
            for (auto entity : view)
            {
                const auto& id = view.get<IDComponent>(entity);
                if (id.ID == m_SunSourceUUID)
                    return Entity{ entity, this };
            }
        }
        
        // Yoksa ilk directional light'ı döndür
        auto view = m_Registry.view<DirectionalLightComponent>();
        if (!view.empty())
            return Entity{ *view.begin(), this };
        
        return {};
    }

    Entity Scene::FindEntityByTag(const std::string& tag)
    {
        auto view = m_Registry.view<TagComponent>();
        for (auto entity : view)
        {
            const auto& tagComp = view.get<TagComponent>(entity);
            if (tagComp.GameTag == tag)
                return Entity{ entity, this };
        }
        return {};
    }

    std::vector<Entity> Scene::FindEntitiesWithTag(const std::string& tag)
    {
        std::vector<Entity> entities;
        auto view = m_Registry.view<TagComponent>();
        for (auto entity : view)
        {
            const auto& tagComp = view.get<TagComponent>(entity);
            if (tagComp.GameTag == tag)
                entities.push_back(Entity{ entity, this });
        }
        return entities;
    }

    Entity Scene::FindEntityByUUID(uint64_t uuid)
    {
        auto view = m_Registry.view<IDComponent>();
        for (auto entity : view)
        {
            const auto& id = view.get<IDComponent>(entity);
            if (id.ID == uuid)
                return Entity{ entity, this };
        }
        return {};
    }

    const std::vector<std::string>& Scene::GetAvailableTags()
    {
        return s_AvailableTags;
    }

    int32_t Scene::AddLayer(const std::string& name)
    {
        int32_t newID = GetNextLayerID();
        m_Layers.push_back({ newID, name });
        return newID;
    }

    void Scene::RemoveLayer(int32_t layerID)
    {
        if (layerID == 0) return; // Default layer silinemez
        
        auto it = std::find_if(m_Layers.begin(), m_Layers.end(),
            [layerID](const LayerInfo& layer) { return layer.ID == layerID; });
        
        if (it != m_Layers.end())
            m_Layers.erase(it);
    }

    void Scene::RenameLayer(int32_t layerID, const std::string& newName)
    {
        auto it = std::find_if(m_Layers.begin(), m_Layers.end(),
            [layerID](const LayerInfo& layer) { return layer.ID == layerID; });
        
        if (it != m_Layers.end())
            it->Name = newName;
    }

    std::string Scene::GetLayerName(int32_t layerID) const
    {
        auto it = std::find_if(m_Layers.begin(), m_Layers.end(),
            [layerID](const LayerInfo& layer) { return layer.ID == layerID; });
        
        if (it != m_Layers.end())
            return it->Name;
        
        return "Unknown Layer";
    }

    int32_t Scene::GetNextLayerID() const
    {
        if (m_Layers.empty())
            return 0;
        
        int32_t maxID = 0;
        for (const auto& layer : m_Layers)
        {
            if (layer.ID > maxID)
                maxID = layer.ID;
        }
        return maxID + 1;
    }
    
    void Scene::SetFlareElementCount(int count)
    {
        if (count < 1) count = 1;
        if (count > 10) count = 10; // Max 10 element
        
        int oldCount = (int)m_FlareElements.size();
        m_FlareElements.resize(count);
        
        // Yeni eklenen elementlere default değerler
        for (int i = oldCount; i < count; i++)
        {
            float offset = 0.3f + (float)i * 0.3f;
            if (offset > 1.0f) offset = 1.0f;
            
            m_FlareElements[i] = { 
                glm::vec3(1.0f), 
                0.08f - (float)i * 0.01f, 
                offset, 
                1.0f - (float)i * 0.2f 
            };
        }
    }
    
    void Scene::SetFlareElement(int index, const FlareElement& element)
    {
        if (index >= 0 && index < (int)m_FlareElements.size())
        {
            m_FlareElements[index] = element;
        }
    }
    
    Scene::FlareElement Scene::GetFlareElement(int index) const
    {
        if (index >= 0 && index < (int)m_FlareElements.size())
        {
            return m_FlareElements[index];
        }
        return FlareElement();
    }

    template<typename T>
    void Scene::OnComponentAdded([[maybe_unused]] Entity entity, [[maybe_unused]] T& component)
    {
    }

    template<>
    void Scene::OnComponentAdded<IDComponent>([[maybe_unused]] Entity entity, [[maybe_unused]] IDComponent& component)
    {
    }

    template<>
    void Scene::OnComponentAdded<TransformComponent>([[maybe_unused]] Entity entity, [[maybe_unused]] TransformComponent& component)
    {
    }

    template<>
    void Scene::OnComponentAdded<TagComponent>([[maybe_unused]] Entity entity, [[maybe_unused]] TagComponent& component)
    {
    }

    template<>
    void Scene::OnComponentAdded<CameraComponent>([[maybe_unused]] Entity entity, CameraComponent& component)
    {
        if (m_ViewportWidth > 0 && m_ViewportHeight > 0)
            component.Camera.SetViewportSize(m_ViewportWidth, m_ViewportHeight);
    }

    template<>
    void Scene::OnComponentAdded<SpriteRendererComponent>([[maybe_unused]] Entity entity, [[maybe_unused]] SpriteRendererComponent& component)
    {
    }

    template<>
    void Scene::OnComponentAdded<ImageComponent>([[maybe_unused]] Entity entity, [[maybe_unused]] ImageComponent& component)
    {
    }

    template<>
    void Scene::OnComponentAdded<MeshRendererComponent>(Entity entity, MeshRendererComponent& component)
    {
        std::string defaultTex = "Resources/Textures/texture_grid.png";
        component.TexturePath = defaultTex;
        component.Texture = Texture2D::Create(defaultTex);
    }

    template<>
    void Scene::OnComponentAdded<RigidBody2DComponent>(Entity entity, [[maybe_unused]] RigidBody2DComponent& component)
    {
        CQ_CORE_INFO("Scene::OnComponentAdded<RigidBody2DComponent> - Entity: {0}", (uint32_t)entity);
        // Fizik body'yi oluştur
        if (m_PhysicsWorld2D)
        {
            CQ_CORE_INFO("Scene::OnComponentAdded<RigidBody2DComponent> - Calling CreateBody");
            m_PhysicsWorld2D->CreateBody(entity);
            CQ_CORE_INFO("Scene::OnComponentAdded<RigidBody2DComponent> - CreateBody completed");
        }
        else
        {
            CQ_CORE_ERROR("Scene::OnComponentAdded<RigidBody2DComponent> - PhysicsWorld2D is NULL!");
        }
    }

    template<>
    void Scene::OnComponentAdded<BoxCollider2DComponent>(Entity entity, [[maybe_unused]] BoxCollider2DComponent& component)
    {
        CQ_CORE_INFO("Scene::OnComponentAdded<BoxCollider2DComponent> - Entity: {0}", (uint32_t)entity);
        // Rigidbody varsa body'yi yeniden oluştur
        if (entity.HasComponent<RigidBody2DComponent>() && m_PhysicsWorld2D)
        {
            CQ_CORE_INFO("Scene::OnComponentAdded<BoxCollider2DComponent> - Recreating body");
            m_PhysicsWorld2D->DestroyBody(entity);
            m_PhysicsWorld2D->CreateBody(entity);
        }
        else
        {
            CQ_CORE_WARN("Scene::OnComponentAdded<BoxCollider2DComponent> - No RigidBody2D or PhysicsWorld2D is NULL");
        }
    }

    template<>
    void Scene::OnComponentAdded<CircleCollider2DComponent>(Entity entity, [[maybe_unused]] CircleCollider2DComponent& component)
    {
        // Rigidbody varsa body'yi yeniden oluştur
        if (entity.HasComponent<RigidBody2DComponent>() && m_PhysicsWorld2D)
        {
            m_PhysicsWorld2D->DestroyBody(entity);
            m_PhysicsWorld2D->CreateBody(entity);
        }
    }

    template<>
    void Scene::OnComponentAdded<RigidbodyComponent>(Entity entity, [[maybe_unused]] RigidbodyComponent& component)
    {
        // 3D fizik body'yi oluştur
        if (m_PhysicsWorld3D)
        {
            m_PhysicsWorld3D->CreateBody(entity);
        }
    }

    template<>
    void Scene::OnComponentAdded<BoxColliderComponent>(Entity entity, [[maybe_unused]] BoxColliderComponent& component)
    {
        // Rigidbody varsa body'yi yeniden oluştur
        if (entity.HasComponent<RigidbodyComponent>() && m_PhysicsWorld3D)
        {
            m_PhysicsWorld3D->DestroyBody(entity);
            m_PhysicsWorld3D->CreateBody(entity);
        }
    }

    template<>
    void Scene::OnComponentAdded<SphereColliderComponent>(Entity entity, [[maybe_unused]] SphereColliderComponent& component)
    {
        // Rigidbody varsa body'yi yeniden oluştur
        if (entity.HasComponent<RigidbodyComponent>() && m_PhysicsWorld3D)
        {
            m_PhysicsWorld3D->DestroyBody(entity);
            m_PhysicsWorld3D->CreateBody(entity);
        }
    }

    template<>
    void Scene::OnComponentAdded<CapsuleColliderComponent>(Entity entity, [[maybe_unused]] CapsuleColliderComponent& component)
    {
        // Rigidbody varsa body'yi yeniden oluştur
        if (entity.HasComponent<RigidbodyComponent>() && m_PhysicsWorld3D)
        {
            m_PhysicsWorld3D->DestroyBody(entity);
            m_PhysicsWorld3D->CreateBody(entity);
        }
    }

    template<>
    void Scene::OnComponentAdded<MeshColliderComponent>(Entity entity, [[maybe_unused]] MeshColliderComponent& component)
    {
        // Rigidbody varsa body'yi yeniden oluştur
        if (entity.HasComponent<RigidbodyComponent>() && m_PhysicsWorld3D)
        {
            m_PhysicsWorld3D->DestroyBody(entity);
            m_PhysicsWorld3D->CreateBody(entity);
        }
    }

    template<>
    void Scene::OnComponentAdded<TextRendererComponent>([[maybe_unused]] Entity entity, [[maybe_unused]] TextRendererComponent& component)
    {
    }

    template<>
    void Scene::OnComponentAdded<LayerComponent>([[maybe_unused]] Entity entity, [[maybe_unused]] LayerComponent& component)
    {
    }

    template<>
    void Scene::OnComponentAdded<ButtonComponent>([[maybe_unused]] Entity entity, [[maybe_unused]] ButtonComponent& component)
    {
    }

    template<>
    void Scene::OnComponentAdded<CanvasComponent>([[maybe_unused]] Entity entity, [[maybe_unused]] CanvasComponent& component)
    {
    }

    template<>
    void Scene::OnComponentAdded<CanvasScalerComponent>([[maybe_unused]] Entity entity, [[maybe_unused]] CanvasScalerComponent& component)
    {
    }

    template<>
    void Scene::OnComponentAdded<GraphicRaycasterComponent>([[maybe_unused]] Entity entity, [[maybe_unused]] GraphicRaycasterComponent& component)
    {
    }

    template<>
    void Scene::OnComponentAdded<ModelComponent>([[maybe_unused]] Entity entity, [[maybe_unused]] ModelComponent& component)
    {
    }

    template<>
    void Scene::OnComponentAdded<AnimatorComponent>([[maybe_unused]] Entity entity, [[maybe_unused]] AnimatorComponent& component)
    {
    }

    template<>
    void Scene::OnComponentAdded<AnimationComponent>([[maybe_unused]] Entity entity, [[maybe_unused]] AnimationComponent& component)
    {
    }

    template<>
    void Scene::OnComponentAdded<DirectionalLightComponent>([[maybe_unused]] Entity entity, [[maybe_unused]] DirectionalLightComponent& component)
    {
    }

    template<>
    void Scene::OnComponentAdded<PointLightComponent>([[maybe_unused]] Entity entity, [[maybe_unused]] PointLightComponent& component)
    {
    }

    template<>
    void Scene::OnComponentAdded<SpotLightComponent>([[maybe_unused]] Entity entity, [[maybe_unused]] SpotLightComponent& component)
    {
    }

    // Hierarchy sistem implementasyonu
    void Scene::SetEntityParent(Entity entity, Entity parent)
    {
        entt::entity entityHandle = entity;
        entt::entity parentHandle = parent;

        // Eski parent'tan çıkar
        RemoveEntityParent(entity);

        // Yeni parent'a ekle
        m_EntityParent[entityHandle] = parentHandle;
        m_EntityChildren[parentHandle].push_back(entityHandle);
    }

    void Scene::RemoveEntityParent(Entity entity)
    {
        entt::entity entityHandle = entity;

        auto it = m_EntityParent.find(entityHandle);
        if (it != m_EntityParent.end())
        {
            entt::entity parentHandle = it->second;

            // Parent'ın child listesinden çıkar
            auto& children = m_EntityChildren[parentHandle];
            children.erase(std::remove(children.begin(), children.end(), entityHandle), children.end());

            // Parent ilişkisini sil
            m_EntityParent.erase(it);
        }
    }

    Entity Scene::GetEntityParent(Entity entity) const
    {
        entt::entity entityHandle = entity;
        auto it = m_EntityParent.find(entityHandle);
        if (it != m_EntityParent.end())
            return Entity{ it->second, const_cast<Scene*>(this) };
        
        return Entity{}; // Null entity
    }

    std::vector<Entity> Scene::GetEntityChildren(Entity entity) const
    {
        std::vector<Entity> result;
        entt::entity entityHandle = entity;
        
        auto it = m_EntityChildren.find(entityHandle);
        if (it != m_EntityChildren.end())
        {
            for (auto childHandle : it->second)
            {
                result.push_back(Entity{ childHandle, const_cast<Scene*>(this) });
            }
        }
        
        return result;
    }

    bool Scene::HasChildren(Entity entity) const
    {
        entt::entity entityHandle = entity;
        auto it = m_EntityChildren.find(entityHandle);
        return it != m_EntityChildren.end() && !it->second.empty();
    }

    void Scene::UpdateTransformHierarchy(Entity entity, const glm::mat4& parentTransform)
    {
        if (!entity.HasComponent<TransformComponent>())
            return;

        auto& transform = entity.GetComponent<TransformComponent>();
        
        // Local transform'u hesapla
        glm::mat4 localTransform = transform.GetTransform();
        
        // World transform = parent * local
        transform.WorldTransform = parentTransform * localTransform;
        
        // Child'lar için recursive çağır
        auto children = GetEntityChildren(entity);
        for (auto child : children)
        {
            UpdateTransformHierarchy(child, transform.WorldTransform);
        }
    }

    void Scene::UpdateButtonInput(EditorCamera& camera, Timestep ts, const glm::vec2* viewportBounds)
    {
        // Mouse pozisyonunu al
        glm::vec2 mousePos = Input::GetMousePosition();

        // Viewport bounds kontrolü
        if (viewportBounds)
        {
            // Mouse viewport içinde mi?
            if (mousePos.x < viewportBounds[0].x || mousePos.x > viewportBounds[1].x ||
                mousePos.y < viewportBounds[0].y || mousePos.y > viewportBounds[1].y)
            {
                // Mouse viewport dışında, butonları deaktif et
                auto buttonView = m_Registry.view<ButtonComponent>();
                for (auto entity : buttonView)
                {
                    auto& button = buttonView.get<ButtonComponent>(entity);
                    button.IsHovered = false;
                    button.IsPressed = false;
                    button.CurrentState = button.Interactable ? ButtonComponent::State::Normal : ButtonComponent::State::Disabled;
                }
                return;
            }
            
        }
        
        // Klavye navigation input
        static bool wasUpPressed = false;
        static bool wasDownPressed = false;
        static bool wasLeftPressed = false;
        static bool wasRightPressed = false;
        static bool wasEnterPressed = false;
        static bool wasSpacePressed = false;
        
        bool upPressed = Input::IsKeyPressed(Key::Up) || Input::IsKeyPressed(Key::W);
        bool downPressed = Input::IsKeyPressed(Key::Down) || Input::IsKeyPressed(Key::S);
        bool leftPressed = Input::IsKeyPressed(Key::Left) || Input::IsKeyPressed(Key::A);
        bool rightPressed = Input::IsKeyPressed(Key::Right) || Input::IsKeyPressed(Key::D);
        bool enterPressed = Input::IsKeyPressed(Key::Enter);
        bool spacePressed = Input::IsKeyPressed(Key::Space);
        
        bool upClicked = upPressed && !wasUpPressed;
        bool downClicked = downPressed && !wasDownPressed;
        bool leftClicked = leftPressed && !wasLeftPressed;
        bool rightClicked = rightPressed && !wasRightPressed;
        bool enterClicked = enterPressed && !wasEnterPressed;
        bool spaceClicked = spacePressed && !wasSpacePressed;
        
        wasUpPressed = upPressed;
        wasDownPressed = downPressed;
        wasLeftPressed = leftPressed;
        wasRightPressed = rightPressed;
        wasEnterPressed = enterPressed;
        wasSpacePressed = spacePressed;
        
        // Seçili buton varsa Enter/Space ile tetikle
        if ((enterClicked || spaceClicked) && m_SelectedButton != entt::null)
        {
            if (m_Registry.valid(m_SelectedButton) && m_Registry.all_of<ButtonComponent>(m_SelectedButton))
            {
                auto& selectedBtn = m_Registry.get<ButtonComponent>(m_SelectedButton);
                if (selectedBtn.Interactable && selectedBtn.OnClick)
                {
                    selectedBtn.OnClick();
                }
            }
        }
        
        // Navigation (arrow keys ile buton değiştir)
        if (upClicked || downClicked || leftClicked || rightClicked)
        {
            Entity nextButton = FindNextButton(upClicked, downClicked, leftClicked, rightClicked);
            if (nextButton)
            {
                m_SelectedButton = nextButton;
            }
        }
        
        // Screen space'den world space'e dönüştür (Ray casting ile)
        glm::mat4 viewProj = camera.GetViewProjection();
        glm::mat4 invViewProj = glm::inverse(viewProj);
        
        // NDC koordinatlarına dönüştür
        glm::vec2 ndc;
        ndc.x = (2.0f * mousePos.x) / m_ViewportWidth - 1.0f;
        ndc.y = 1.0f - (2.0f * mousePos.y) / m_ViewportHeight; // Y ters
        
        // Ray'in başlangıç ve bitiş noktalarını hesapla (near ve far plane)
        glm::vec4 rayStartNDC = glm::vec4(ndc.x, ndc.y, -1.0f, 1.0f); // Near plane
        glm::vec4 rayEndNDC = glm::vec4(ndc.x, ndc.y, 1.0f, 1.0f);    // Far plane
        
        glm::vec4 rayStartWorld = invViewProj * rayStartNDC;
        glm::vec4 rayEndWorld = invViewProj * rayEndNDC;
        
        rayStartWorld /= rayStartWorld.w;
        rayEndWorld /= rayEndWorld.w;
        
        // Ray direction
        glm::vec3 rayDir = glm::normalize(glm::vec3(rayEndWorld - rayStartWorld));
        glm::vec3 rayOrigin = glm::vec3(rayStartWorld);
        
        // Ray'in z=0 plane ile kesişimini bul
        // Ray: P = rayOrigin + t * rayDir
        // Plane: z = 0
        // Çözüm: t = -rayOrigin.z / rayDir.z
        float t = -rayOrigin.z / rayDir.z;
        glm::vec3 intersectionPoint = rayOrigin + t * rayDir;
        
        glm::vec2 mouseWorldPos = glm::vec2(intersectionPoint.x, intersectionPoint.y);
        
        // Mouse button durumu
        bool mousePressed = Input::IsMouseButtonPressed(Mouse::ButtonLeft);
        static bool wasPressed = false;
        bool mouseClicked = mousePressed && !wasPressed;
        wasPressed = mousePressed;
        
        // Tüm butonları tara
        auto buttonView = m_Registry.view<TransformComponent, ButtonComponent, ImageComponent>();
        for (auto entity : buttonView)
        {
            auto [transform, button, image] = buttonView.get<TransformComponent, ButtonComponent, ImageComponent>(entity);
            
            if (!button.Interactable)
            {
                button.CurrentState = ButtonComponent::State::Disabled;
                button.IsHovered = false;
                button.IsPressed = false;
                
                // Disabled için hedef renk
                glm::vec4 targetBackgroundColor;
                if (button.ShowColorDetails)
                {
                    targetBackgroundColor = button.BackgroundDisabledColor * button.ColorMultiplier;
                }
                else
                {
                    targetBackgroundColor = image.Color * 0.5f * button.ColorMultiplier;
                    targetBackgroundColor.a = image.Color.a * 0.5f;
                }
                
                // Smooth transition
                float lerpFactor = glm::min(1.0f, button.TransitionSpeed * ts);
                button.CurrentBackgroundColor = glm::mix(button.CurrentBackgroundColor, targetBackgroundColor, lerpFactor);
                
                continue;
            }
            
            // Highlighted state (en yüksek öncelik, interactable olsa bile)
            if (button.IsHighlighted)
            {
                button.CurrentState = ButtonComponent::State::Highlighted;
                
                glm::vec4 targetBackgroundColor;
                glm::vec4 targetTextColor;
                
                if (button.ShowColorDetails)
                {
                    targetBackgroundColor = button.BackgroundHighlightedColor * button.ColorMultiplier;
                    targetTextColor = button.TextHighlightedColor;
                }
                else
                {
                    targetBackgroundColor = image.Color * 1.5f * button.ColorMultiplier;
                    targetBackgroundColor.a = image.Color.a;
                    targetTextColor = glm::vec4(1.0f, 1.0f, 0.4f, 1.0f);
                }
                
                // Smooth transition
                float lerpFactor = glm::min(1.0f, button.TransitionSpeed * ts);
                button.CurrentBackgroundColor = glm::mix(button.CurrentBackgroundColor, targetBackgroundColor, lerpFactor);
                button.CurrentTextColor = glm::mix(button.CurrentTextColor, targetTextColor, lerpFactor);
                
                continue;
            }
            



            // AABB bounds hesapla (ImageComponent texture boyutundan)
            if (!image.Texture)
                continue;
            
            // Rendering ile aynı hesaplama (piksel -> dünya birimi)
            float halfWidth = (float)image.Texture->GetWidth() / 200.0f;
            float halfHeight = (float)image.Texture->GetHeight() / 200.0f;


            
            // Vertex pozisyonlarını rendering ile aynı şekilde hesapla
            glm::vec4 vertexPositions[4] = {
                { -halfWidth, -halfHeight, 0.0f, 1.0f },
                {  halfWidth, -halfHeight, 0.0f, 1.0f },
                {  halfWidth,  halfHeight, 0.0f, 1.0f },
                { -halfWidth,  halfHeight, 0.0f, 1.0f }
            };
            
            // Transform'u uygula (rendering ile aynı)
            glm::vec4 transformedVerts[4];
            for (int i = 0; i < 4; i++)
            {
                transformedVerts[i] = transform.WorldTransform * vertexPositions[i];
            }


            // Offset düzeltmesi: Sol-üste kaydır
            float offsetMultiplierX = -3.32f; // X için çarpan (sola/sağa)
            float offsetMultiplierY = 1.72f; // Y için çarpan (yukarı/aşağı)
            glm::vec2 offset = glm::vec2(-halfWidth * offsetMultiplierX, -halfHeight * offsetMultiplierY);
            for (int i = 0; i < 4; i++)
            {
                transformedVerts[i].x += offset.x;
                transformedVerts[i].y += offset.y;
            }
            
            // AABB bounds (transformed vertex'lerden min/max bul)
            glm::vec2 min = glm::vec2(transformedVerts[0].x, transformedVerts[0].y);
            glm::vec2 max = min;
            
            for (int i = 1; i < 4; i++)
            {
                min.x = glm::min(min.x, transformedVerts[i].x);
                min.y = glm::min(min.y, transformedVerts[i].y);
                max.x = glm::max(max.x, transformedVerts[i].x);
                max.y = glm::max(max.y, transformedVerts[i].y);
            }
            
            // Mouse bounds içinde mi?
            bool isInside = mouseWorldPos.x >= min.x && mouseWorldPos.x <= max.x &&
                           mouseWorldPos.y >= min.y && mouseWorldPos.y <= max.y;
            
            // GraphicRaycaster kontrolü
            if (isInside)
            {
                Entity canvasEntity = Entity{ entity, this };
                while (canvasEntity && !canvasEntity.HasComponent<CanvasComponent>())
                {
                    canvasEntity = GetEntityParent(canvasEntity);
                }
                
                if (canvasEntity && canvasEntity.HasComponent<GraphicRaycasterComponent>())
                {
                    auto& raycaster = canvasEntity.GetComponent<GraphicRaycasterComponent>();
                    
                    // Blocking Objects kontrolü
                    if (raycaster.IgnoreReversedGraphics)
                    {
                        glm::vec3 scale = glm::vec3(transform.WorldTransform[0][0], 
                                                    transform.WorldTransform[1][1], 
                                                    transform.WorldTransform[2][2]);
                        
                        if (scale.x < 0.0f || scale.y < 0.0f)
                        {
                            isInside = false;
                        }
                    }
                    
                    if (isInside && raycaster.Blocking != GraphicRaycasterComponent::BlockingObjects::None)
                    {
                        // Mouse tıklandığında blocking durumunu sıfırla
                        if (mouseClicked)
                        {
                            button.IsBlockedByDepth = false;
                        }
                        
                        // Butonun depth'ini hesapla (TÜM MODLAR İÇİN)
                        glm::vec4 buttonWorldPos = transform.WorldTransform * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
                        glm::vec4 buttonClipPos = viewProj * buttonWorldPos;
                        float buttonDepth = (buttonClipPos.z / buttonClipPos.w) * 0.5f + 0.5f;
                        
                        CQ_CORE_INFO("--- Blocking Check ---");
                        CQ_CORE_INFO("Blocking Mode: {0}", (int)raycaster.Blocking);
                        CQ_CORE_INFO("BlockingMask: 0x{0:X}", raycaster.BlockingMask);
                        CQ_CORE_INFO("buttonDepth: {0}", buttonDepth);
                        
                        // TÜM mesh'lerin depth'ini hesapla ve logla
                        auto meshView = m_Registry.view<TransformComponent, MeshRendererComponent, LayerComponent>();
                        int meshIdx = 0;
                        for (auto meshEntity : meshView)
                        {
                            auto [meshTransform, meshRenderer, meshLayer] = 
                                meshView.get<TransformComponent, MeshRendererComponent, LayerComponent>(meshEntity);
                            
                            glm::vec3 meshPos = glm::vec3(meshTransform.WorldTransform[3]);
                            glm::vec3 meshScale = glm::vec3(
                                glm::length(glm::vec3(meshTransform.WorldTransform[0])),
                                glm::length(glm::vec3(meshTransform.WorldTransform[1])),
                                glm::length(glm::vec3(meshTransform.WorldTransform[2]))
                            );
                            
                            glm::vec4 meshWorldPos = meshTransform.WorldTransform * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
                            glm::vec4 meshClipPos = viewProj * meshWorldPos;
                            float meshDepth = (meshClipPos.z / meshClipPos.w) * 0.5f + 0.5f;
                            
                            CQ_CORE_INFO("Mesh #{0}:", meshIdx);
                            CQ_CORE_INFO("  Pos: ({0}, {1}, {2})", meshPos.x, meshPos.y, meshPos.z);
                            CQ_CORE_INFO("  Scale: ({0}, {1}, {2})", meshScale.x, meshScale.y, meshScale.z);
                            CQ_CORE_INFO("  Layer: {0}", meshLayer.Layer);
                            CQ_CORE_INFO("  Depth: {0}", meshDepth);
                            CQ_CORE_INFO("  In Mask: {0}", (raycaster.BlockingMask & (1 << meshLayer.Layer)) != 0);
                            
                            meshIdx++;
                        }
                        
                        if (raycaster.Blocking == GraphicRaycasterComponent::BlockingObjects::All)
                        {
                            // Depth buffer oku (HER FRAME)
                            float mouseDepth;
                            int screenX = (int)mousePos.x;
                            int screenY = (int)(m_ViewportHeight - mousePos.y);
                            glReadPixels(screenX, screenY, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &mouseDepth);
                            
                            // Butonun depth'ini hesapla
                            glm::vec4 buttonWorldPos = transform.WorldTransform * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
                            glm::vec4 buttonClipPos = viewProj * buttonWorldPos;
                            float buttonDepth = (buttonClipPos.z / buttonClipPos.w) * 0.5f + 0.5f;
                            
                            const float epsilon = 0.0000001f; // Maksimum hassasiyet
                            if (mouseDepth < buttonDepth - epsilon)
                            {
                                // Önde bir şey var, hem 2D hem 3D objeleri kontrol et
                                bool shouldBlock = false;
                                
                                // 2D sprite kontrolü
                                auto spriteView = m_Registry.view<TransformComponent, SpriteRendererComponent, LayerComponent>();
                                for (auto spriteEntity : spriteView)
                                {
                                    auto [spriteTransform, spriteRenderer, spriteLayer] = 
                                        spriteView.get<TransformComponent, SpriteRendererComponent, LayerComponent>(spriteEntity);
                                    
                                    // Layer mask kontrolü
                                    if (!(raycaster.BlockingMask & (1 << spriteLayer.Layer)))
                                        continue;
                                    
                                    if (!spriteRenderer.Texture)
                                        continue;
                                    
                                    // AABB bounds hesapla
                                    float spriteHalfWidth = (float)spriteRenderer.Texture->GetWidth() / 200.0f;
                                    float spriteHalfHeight = (float)spriteRenderer.Texture->GetHeight() / 200.0f;
                                    
                                    glm::vec4 spriteVertexPositions[4] = {
                                        { -spriteHalfWidth, -spriteHalfHeight, 0.0f, 1.0f },
                                        {  spriteHalfWidth, -spriteHalfHeight, 0.0f, 1.0f },
                                        {  spriteHalfWidth,  spriteHalfHeight, 0.0f, 1.0f },
                                        { -spriteHalfWidth,  spriteHalfHeight, 0.0f, 1.0f }
                                    };
                                    
                                    glm::vec4 spriteTransformedVerts[4];
                                    for (int i = 0; i < 4; i++)
                                    {
                                        spriteTransformedVerts[i] = spriteTransform.WorldTransform * spriteVertexPositions[i];
                                    }
                                    
                                    glm::vec2 spriteMin = glm::vec2(spriteTransformedVerts[0].x, spriteTransformedVerts[0].y);
                                    glm::vec2 spriteMax = spriteMin;
                                    
                                    for (int i = 1; i < 4; i++)
                                    {
                                        spriteMin.x = glm::min(spriteMin.x, spriteTransformedVerts[i].x);
                                        spriteMin.y = glm::min(spriteMin.y, spriteTransformedVerts[i].y);
                                        spriteMax.x = glm::max(spriteMax.x, spriteTransformedVerts[i].x);
                                        spriteMax.y = glm::max(spriteMax.y, spriteTransformedVerts[i].y);
                                    }
                                    
                                    bool spriteContainsMouse = mouseWorldPos.x >= spriteMin.x && mouseWorldPos.x <= spriteMax.x &&
                                                               mouseWorldPos.y >= spriteMin.y && mouseWorldPos.y <= spriteMax.y;
                                    
                                    if (!spriteContainsMouse)
                                        continue;
                                    
                                    // Depth kontrolü
                                    glm::vec4 spriteWorldPos = spriteTransform.WorldTransform * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
                                    glm::vec4 spriteClipPos = viewProj * spriteWorldPos;
                                    float spriteDepth = (spriteClipPos.z / spriteClipPos.w) * 0.5f + 0.5f;
                                    
                                    if (spriteDepth < buttonDepth - epsilon)
                                    {
                                        shouldBlock = true;
                                        break;
                                    }
                                }
                                
                                // 3D mesh kontrolü (sprite bulunamadıysa)
                                if (!shouldBlock)
                                {
                                    auto meshView = m_Registry.view<TransformComponent, MeshRendererComponent, LayerComponent>();
                                    for (auto meshEntity : meshView)
                                    {
                                        auto [meshTransform, meshRenderer, meshLayer] = 
                                            meshView.get<TransformComponent, MeshRendererComponent, LayerComponent>(meshEntity);
                                        
                                        // Layer mask kontrolü
                                        if (!(raycaster.BlockingMask & (1 << meshLayer.Layer)))
                                            continue;
                                        
                                        // AABB bounds hesapla
                                        float meshHalfSize = 0.5f;
                                        
                                        glm::vec4 meshVertexPositions[8] = {
                                            { -meshHalfSize, -meshHalfSize, -meshHalfSize, 1.0f },
                                            {  meshHalfSize, -meshHalfSize, -meshHalfSize, 1.0f },
                                            {  meshHalfSize,  meshHalfSize, -meshHalfSize, 1.0f },
                                            { -meshHalfSize,  meshHalfSize, -meshHalfSize, 1.0f },
                                            { -meshHalfSize, -meshHalfSize,  meshHalfSize, 1.0f },
                                            {  meshHalfSize, -meshHalfSize,  meshHalfSize, 1.0f },
                                            {  meshHalfSize,  meshHalfSize,  meshHalfSize, 1.0f },
                                            { -meshHalfSize,  meshHalfSize,  meshHalfSize, 1.0f }
                                        };
                                        
                                        glm::vec4 meshTransformedVerts[8];
                                        for (int i = 0; i < 8; i++)
                                        {
                                            meshTransformedVerts[i] = meshTransform.WorldTransform * meshVertexPositions[i];
                                        }
                                        
                                        glm::vec3 meshMin = glm::vec3(meshTransformedVerts[0].x, meshTransformedVerts[0].y, meshTransformedVerts[0].z);
                                        glm::vec3 meshMax = meshMin;
                                        
                                        for (int i = 1; i < 8; i++)
                                        {
                                            meshMin.x = glm::min(meshMin.x, meshTransformedVerts[i].x);
                                            meshMin.y = glm::min(meshMin.y, meshTransformedVerts[i].y);
                                            meshMin.z = glm::min(meshMin.z, meshTransformedVerts[i].z);
                                            meshMax.x = glm::max(meshMax.x, meshTransformedVerts[i].x);
                                            meshMax.y = glm::max(meshMax.y, meshTransformedVerts[i].y);
                                            meshMax.z = glm::max(meshMax.z, meshTransformedVerts[i].z);
                                        }
                                        
                                        bool meshContainsMouse = mouseWorldPos.x >= meshMin.x && mouseWorldPos.x <= meshMax.x &&
                                                                 mouseWorldPos.y >= meshMin.y && mouseWorldPos.y <= meshMax.y;
                                        
                                        if (!meshContainsMouse)
                                            continue;
                                        
                                        // Ray-AABB intersection
                                        float tMin = (meshMin.z - rayOrigin.z) / rayDir.z;
                                        float tMax = (meshMax.z - rayOrigin.z) / rayDir.z;
                                        
                                        if (tMin > tMax) std::swap(tMin, tMax);
                                        
                                        if (tMax < 0.0f || tMin > 1000.0f)
                                            continue;
                                        
                                        // Depth kontrolü
                                        glm::vec4 meshWorldPos = meshTransform.WorldTransform * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
                                        glm::vec4 meshClipPos = viewProj * meshWorldPos;
                                        float meshDepth = (meshClipPos.z / meshClipPos.w) * 0.5f + 0.5f;
                                        
                                        if (meshDepth < buttonDepth - epsilon)
                                        {
                                            shouldBlock = true;
                                            break;
                                        }
                                    }
                                }
                                
                                if (shouldBlock)
                                {
                                    button.IsBlockedByDepth = true;
                                    isInside = false;
                                }
                            }
                        }
                        else
                        {
                            // Depth buffer oku (HER FRAME)
                            float mouseDepth;
                            int screenX = (int)mousePos.x;
                            int screenY = (int)(m_ViewportHeight - mousePos.y);
                            glReadPixels(screenX, screenY, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &mouseDepth);
                            
                            // Butonun depth'ini hesapla
                            glm::vec4 buttonWorldPos = transform.WorldTransform * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
                            glm::vec4 buttonClipPos = viewProj * buttonWorldPos;
                            float buttonDepth = (buttonClipPos.z / buttonClipPos.w) * 0.5f + 0.5f;
                            
                            if (mouseClicked)
                            {
                                CQ_CORE_INFO("Blocking check - mouseDepth: {0}, buttonDepth: {1}", mouseDepth, buttonDepth);
                            }
                            
                            const float epsilon = 0.0000001f; // Maksimum hassasiyet
                            if (mouseDepth < buttonDepth - epsilon)
                            {
                                // Önde bir şey var, şimdi mouse pozisyonundaki objeleri kontrol et
                                bool shouldBlock = false;
                                
                                if (raycaster.Blocking == GraphicRaycasterComponent::BlockingObjects::TwoD)
                                {
                                    // 2D sprite kontrolü - SADECE mouse pozisyonundaki sprite'lar
                                    auto spriteView = m_Registry.view<TransformComponent, SpriteRendererComponent, LayerComponent>();
                                    for (auto spriteEntity : spriteView)
                                    {
                                        auto [spriteTransform, spriteRenderer, spriteLayer] = 
                                            spriteView.get<TransformComponent, SpriteRendererComponent, LayerComponent>(spriteEntity);
                                        
                                        // Layer mask kontrolü - bu sprite'ın layer'ı mask'te var mı?
                                        if (!(raycaster.BlockingMask & (1 << spriteLayer.Layer)))
                                            continue; // Bu layer mask'te yok, atla
                                        
                                        // Sprite'ın texture'ı var mı?
                                        if (!spriteRenderer.Texture)
                                            continue;
                                        
                                        // Sprite'ın AABB bounds'unu hesapla (rendering ile aynı mantık)
                                        float spriteHalfWidth = (float)spriteRenderer.Texture->GetWidth() / 200.0f;
                                        float spriteHalfHeight = (float)spriteRenderer.Texture->GetHeight() / 200.0f;
                                        
                                        // Vertex pozisyonları (local space)
                                        glm::vec4 spriteVertexPositions[4] = {
                                            { -spriteHalfWidth, -spriteHalfHeight, 0.0f, 1.0f },
                                            {  spriteHalfWidth, -spriteHalfHeight, 0.0f, 1.0f },
                                            {  spriteHalfWidth,  spriteHalfHeight, 0.0f, 1.0f },
                                            { -spriteHalfWidth,  spriteHalfHeight, 0.0f, 1.0f }
                                        };
                                        
                                        // Transform uygula (world space'e çevir)
                                        glm::vec4 spriteTransformedVerts[4];
                                        for (int i = 0; i < 4; i++)
                                        {
                                            spriteTransformedVerts[i] = spriteTransform.WorldTransform * spriteVertexPositions[i];
                                        }
                                        
                                        // AABB bounds hesapla (min/max)
                                        glm::vec2 spriteMin = glm::vec2(spriteTransformedVerts[0].x, spriteTransformedVerts[0].y);
                                        glm::vec2 spriteMax = spriteMin;
                                        
                                        for (int i = 1; i < 4; i++)
                                        {
                                            spriteMin.x = glm::min(spriteMin.x, spriteTransformedVerts[i].x);
                                            spriteMin.y = glm::min(spriteMin.y, spriteTransformedVerts[i].y);
                                            spriteMax.x = glm::max(spriteMax.x, spriteTransformedVerts[i].x);
                                            spriteMax.y = glm::max(spriteMax.y, spriteTransformedVerts[i].y);
                                        }
                                        
                                        // Mouse sprite bounds içinde mi?
                                        bool spriteContainsMouse = mouseWorldPos.x >= spriteMin.x && mouseWorldPos.x <= spriteMax.x &&
                                                                   mouseWorldPos.y >= spriteMin.y && mouseWorldPos.y <= spriteMax.y;
                                        
                                        if (!spriteContainsMouse)
                                            continue; // Mouse bu sprite'ın üzerinde değil
                                        
                                        // Sprite'ın depth'ini hesapla
                                        glm::vec4 spriteWorldPos = spriteTransform.WorldTransform * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
                                        glm::vec4 spriteClipPos = viewProj * spriteWorldPos;
                                        float spriteDepth = (spriteClipPos.z / spriteClipPos.w) * 0.5f + 0.5f;
                                        
                                        // Sprite butonun önünde mi? (depth karşılaştırması)
                                        if (spriteDepth < buttonDepth - epsilon)
                                        {
                                            shouldBlock = true;
                                            break;
                                        }
                                    }
                                }
                                else if (raycaster.Blocking == GraphicRaycasterComponent::BlockingObjects::ThreeD)
                                {
                                    CQ_CORE_INFO("ThreeD mode - Checking meshes...");
                                    
                                    // Butonun AABB bounds'unu hesapla
                                    float buttonHalfWidth = (float)image.Texture->GetWidth() / 200.0f;
                                    float buttonHalfHeight = (float)image.Texture->GetHeight() / 200.0f;
                                    
                                    glm::vec4 buttonVertexPositions[4] = {
                                        { -buttonHalfWidth, -buttonHalfHeight, 0.0f, 1.0f },
                                        {  buttonHalfWidth, -buttonHalfHeight, 0.0f, 1.0f },
                                        {  buttonHalfWidth,  buttonHalfHeight, 0.0f, 1.0f },
                                        { -buttonHalfWidth,  buttonHalfHeight, 0.0f, 1.0f }
                                    };
                                    
                                    glm::vec4 buttonTransformedVerts[4];
                                    for (int i = 0; i < 4; i++)
                                    {
                                        buttonTransformedVerts[i] = transform.WorldTransform * buttonVertexPositions[i];
                                    }
                                    
                                    // Buton AABB
                                    glm::vec2 buttonMin = glm::vec2(buttonTransformedVerts[0].x, buttonTransformedVerts[0].y);
                                    glm::vec2 buttonMax = buttonMin;
                                    
                                    for (int i = 1; i < 4; i++)
                                    {
                                        buttonMin.x = glm::min(buttonMin.x, buttonTransformedVerts[i].x);
                                        buttonMin.y = glm::min(buttonMin.y, buttonTransformedVerts[i].y);
                                        buttonMax.x = glm::max(buttonMax.x, buttonTransformedVerts[i].x);
                                        buttonMax.y = glm::max(buttonMax.y, buttonTransformedVerts[i].y);
                                    }
                                    
                                    CQ_CORE_INFO("  Button AABB: ({0}, {1}) to ({2}, {3})", buttonMin.x, buttonMin.y, buttonMax.x, buttonMax.y);
                                    
                                    // 3D mesh kontrolü
                                    auto meshView = m_Registry.view<TransformComponent, MeshRendererComponent, LayerComponent>();
                                    for (auto meshEntity : meshView)
                                    {
                                        auto [meshTransform, meshRenderer, meshLayer] = 
                                            meshView.get<TransformComponent, MeshRendererComponent, LayerComponent>(meshEntity);
                                        
                                        // Layer mask kontrolü
                                        if (!(raycaster.BlockingMask & (1 << meshLayer.Layer)))
                                        {
                                            CQ_CORE_INFO("  Mesh layer {0} NOT in mask", meshLayer.Layer);
                                            continue;
                                        }
                                        
                                        CQ_CORE_INFO("  Mesh layer {0} in mask, checking depth...", meshLayer.Layer);
                                        
                                        // Mesh'in depth'ini hesapla
                                        glm::vec4 meshWorldPos = meshTransform.WorldTransform * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
                                        glm::vec4 meshClipPos = viewProj * meshWorldPos;
                                        float meshDepth = (meshClipPos.z / meshClipPos.w) * 0.5f + 0.5f;
                                        
                                        CQ_CORE_INFO("  meshDepth: {0}, buttonDepth: {1}", meshDepth, buttonDepth);
                                        
                                        // Mesh butonun önünde mi?
                                        if (meshDepth < buttonDepth - epsilon)
                                        {
                                            CQ_CORE_INFO("  Mesh is IN FRONT of button, checking overlap...");
                                            
                                            // Mesh AABB hesapla
                                            float meshHalfSize = 0.5f;
                                            
                                            glm::vec4 meshVertexPositions[8] = {
                                                { -meshHalfSize, -meshHalfSize, -meshHalfSize, 1.0f },
                                                {  meshHalfSize, -meshHalfSize, -meshHalfSize, 1.0f },
                                                {  meshHalfSize,  meshHalfSize, -meshHalfSize, 1.0f },
                                                { -meshHalfSize,  meshHalfSize, -meshHalfSize, 1.0f },
                                                { -meshHalfSize, -meshHalfSize,  meshHalfSize, 1.0f },
                                                {  meshHalfSize, -meshHalfSize,  meshHalfSize, 1.0f },
                                                {  meshHalfSize,  meshHalfSize,  meshHalfSize, 1.0f },
                                                { -meshHalfSize,  meshHalfSize,  meshHalfSize, 1.0f }
                                            };
                                            
                                            glm::vec4 meshTransformedVerts[8];
                                            for (int i = 0; i < 8; i++)
                                            {
                                                meshTransformedVerts[i] = meshTransform.WorldTransform * meshVertexPositions[i];
                                            }
                                            
                                            // Mesh AABB
                                            glm::vec2 meshMin = glm::vec2(meshTransformedVerts[0].x, meshTransformedVerts[0].y);
                                            glm::vec2 meshMax = meshMin;
                                            
                                            for (int i = 1; i < 8; i++)
                                            {
                                                meshMin.x = glm::min(meshMin.x, meshTransformedVerts[i].x);
                                                meshMin.y = glm::min(meshMin.y, meshTransformedVerts[i].y);
                                                meshMax.x = glm::max(meshMax.x, meshTransformedVerts[i].x);
                                                meshMax.y = glm::max(meshMax.y, meshTransformedVerts[i].y);
                                            }
                                            
                                            CQ_CORE_INFO("  Mesh AABB: ({0}, {1}) to ({2}, {3})", meshMin.x, meshMin.y, meshMax.x, meshMax.y);
                                            
                                            // AABB overlap kontrolü (2D)
                                            bool overlaps = !(buttonMax.x < meshMin.x || buttonMin.x > meshMax.x ||
                                                             buttonMax.y < meshMin.y || buttonMin.y > meshMax.y);
                                            
                                            CQ_CORE_INFO("  Button and Mesh overlap: {0}", overlaps);
                                            
                                            if (overlaps)
                                            {
                                                // Mouse mesh'in içinde mi kontrol et
                                                bool mouseInMesh = mouseWorldPos.x >= meshMin.x && mouseWorldPos.x <= meshMax.x &&
                                                                   mouseWorldPos.y >= meshMin.y && mouseWorldPos.y <= meshMax.y;
                                                
                                                CQ_CORE_INFO("  Mouse in mesh: {0}", mouseInMesh);
                                                
                                                if (mouseInMesh)
                                                {
                                                    CQ_CORE_INFO("  BLOCKING! Mouse is in overlapping area");
                                                    shouldBlock = true;
                                                    break;
                                                }
                                                else
                                                {
                                                    CQ_CORE_INFO("  Overlap exists but mouse NOT in mesh area, not blocking");
                                                }
                                            }
                                            else
                                            {
                                                CQ_CORE_INFO("  Mesh is in front but NOT overlapping, not blocking");
                                            }
                                        }
                                        else
                                        {
                                            CQ_CORE_INFO("  Mesh is BEHIND button, not blocking");
                                        }
                                    }
                                }
                                
                                if (shouldBlock)
                                {
                                    button.IsBlockedByDepth = true;
                                    isInside = false;
                                }
                            }
                        }
                    }
                }
            }
            
            // State güncelle
            button.IsHovered = isInside && !button.IsBlockedByDepth;
            
            // Blocking kontrolünden sonra isInside'ı güncelle
            if (button.IsBlockedByDepth)
            {
                isInside = false;
            }
            
            // Debug log: Butona tıklandığında detaylı bilgi
            if (mouseClicked && isInside)
            {
                // Buton bilgileri
                glm::vec3 buttonPos = glm::vec3(transform.WorldTransform[3]);
                glm::vec3 buttonScale = glm::vec3(
                    glm::length(glm::vec3(transform.WorldTransform[0])),
                    glm::length(glm::vec3(transform.WorldTransform[1])),
                    glm::length(glm::vec3(transform.WorldTransform[2]))
                );
                
                CQ_CORE_INFO("=== BUTTON CLICK DEBUG ===");
                CQ_CORE_INFO("isInside: {0}", isInside);
                CQ_CORE_INFO("Mouse World Pos: ({0}, {1})", mouseWorldPos.x, mouseWorldPos.y);
                CQ_CORE_INFO("Button Pos: ({0}, {1}, {2})", buttonPos.x, buttonPos.y, buttonPos.z);
                CQ_CORE_INFO("Button Scale: ({0}, {1}, {2})", buttonScale.x, buttonScale.y, buttonScale.z);
                
                // Cube bilgileri (eğer varsa)
                auto meshView = m_Registry.view<TransformComponent, MeshRendererComponent>();
                for (auto meshEntity : meshView)
                {
                    auto& meshTransform = meshView.get<TransformComponent>(meshEntity);
                    
                    glm::vec3 cubePos = glm::vec3(meshTransform.WorldTransform[3]);
                    glm::vec3 cubeScale = glm::vec3(
                        glm::length(glm::vec3(meshTransform.WorldTransform[0])),
                        glm::length(glm::vec3(meshTransform.WorldTransform[1])),
                        glm::length(glm::vec3(meshTransform.WorldTransform[2]))
                    );
                    
                    CQ_CORE_INFO("Cube Pos: ({0}, {1}, {2})", cubePos.x, cubePos.y, cubePos.z);
                    CQ_CORE_INFO("Cube Scale: ({0}, {1}, {2})", cubeScale.x, cubeScale.y, cubeScale.z);
                }
                CQ_CORE_INFO("=========================");
            }
            
            // Blocked ise pressed olamaz
            if (button.IsBlockedByDepth && button.IsPressed)
            {
                button.IsPressed = false;
            }
            
            // Klavye ile seçili buton mu?
            bool isSelected = (entity == m_SelectedButton);
            
            // Hedef renkleri belirle
            glm::vec4 targetBackgroundColor;
            glm::vec4 targetTextColor;
            
            // Blocked ise normal state
            if (button.IsBlockedByDepth)
            {
                button.CurrentState = ButtonComponent::State::Normal;
                
                if (button.ShowColorDetails)
                {
                    targetBackgroundColor = button.BackgroundNormalColor * button.ColorMultiplier;
                    targetTextColor = button.TextNormalColor;
                }
                else
                {
                    targetBackgroundColor = image.Color * button.ColorMultiplier;
                    targetTextColor = glm::vec4(1.0f);
                }
            }
            // Klavye ile seçili ise ve mouse hover yoksa selected göster
            else if (isSelected && !button.IsHighlighted && !isInside)
            {
                button.CurrentState = ButtonComponent::State::Hover;
                
                if (button.ShowColorDetails)
                {
                    targetBackgroundColor = button.BackgroundHoverColor * button.ColorMultiplier;
                    targetTextColor = button.TextHoverColor;
                }
                else
                {
                    targetBackgroundColor = image.Color * 1.2f * button.ColorMultiplier;
                    targetBackgroundColor.a = image.Color.a;
                    targetTextColor = glm::vec4(1.0f, 1.0f, 0.8f, 1.0f);
                }
            }
            else if (isInside)
            {
                if (mousePressed)
                {
                    button.IsPressed = true;
                    button.CurrentState = ButtonComponent::State::Pressed;
                    
                    if (button.ShowColorDetails)
                    {
                        targetBackgroundColor = button.BackgroundPressedColor * button.ColorMultiplier;
                        targetTextColor = button.TextPressedColor;
                    }
                    else
                    {
                        targetBackgroundColor = image.Color * 0.7f * button.ColorMultiplier;
                        targetBackgroundColor.a = image.Color.a;
                        targetTextColor = glm::vec4(0.8f, 0.8f, 0.8f, 1.0f);
                    }
                }
                else
                {
                    if (button.IsPressed && mouseClicked == false) // Mouse bırakıldı
                    {
                        // OnClick callback tetikle
                        if (button.OnClick)
                            button.OnClick();
                    }
                    
                    button.IsPressed = false;
                    button.CurrentState = ButtonComponent::State::Hover;
                    
                    if (button.ShowColorDetails)
                    {
                        targetBackgroundColor = button.BackgroundHoverColor * button.ColorMultiplier;
                        targetTextColor = button.TextHoverColor;
                    }
                    else
                    {
                        targetBackgroundColor = image.Color * 1.2f * button.ColorMultiplier;
                        targetBackgroundColor.a = image.Color.a;
                        targetTextColor = glm::vec4(1.0f, 1.0f, 0.9f, 1.0f);
                    }
                }
            }
            else
            {
                button.IsPressed = false;
                button.CurrentState = ButtonComponent::State::Normal;
                
                if (button.ShowColorDetails)
                {
                    targetBackgroundColor = button.BackgroundNormalColor * button.ColorMultiplier;
                    targetTextColor = button.TextNormalColor;
                }
                else
                {
                    targetBackgroundColor = image.Color * button.ColorMultiplier;
                    targetTextColor = glm::vec4(1.0f);
                }
            }
            
            // Smooth transition
            float lerpFactor = glm::min(1.0f, button.TransitionSpeed * ts);
            button.CurrentBackgroundColor = glm::mix(button.CurrentBackgroundColor, targetBackgroundColor, lerpFactor);
            button.CurrentTextColor = glm::mix(button.CurrentTextColor, targetTextColor, lerpFactor);
        }
    }

    bool Scene::IsCanvasOrCanvasChild(Entity entity) const
    {
        if (!entity)
            return false;
        
        // Kendisi Canvas mı?
        if (m_Registry.all_of<CanvasComponent>((entt::entity)entity))
            return true;
        
        // Parent'ı Canvas mı? (recursive)
        Entity parent = GetEntityParent(entity);
        if (parent)
            return IsCanvasOrCanvasChild(parent);
        
        return false;
    }

    Entity Scene::FindNextButton(bool up, bool down, bool left, bool right)
    {
        // Seçili buton yoksa ilk interactable butonu seç
        if (m_SelectedButton == entt::null || !m_Registry.valid(m_SelectedButton))
        {
            auto buttonView = m_Registry.view<ButtonComponent, TransformComponent>();
            for (auto entity : buttonView)
            {
                auto& button = buttonView.get<ButtonComponent>(entity);
                if (button.Interactable)
                {
                    return Entity{ entity, this };
                }
            }
            return Entity{};
        }
        
        auto& currentButton = m_Registry.get<ButtonComponent>(m_SelectedButton);
        auto& currentTransform = m_Registry.get<TransformComponent>(m_SelectedButton);
        
        // Explicit mode: Manuel olarak belirlenmiş butonlara git
        if (currentButton.Navigation == ButtonComponent::NavigationMode::Explicit)
        {
            entt::entity targetEntity = entt::null;
            
            // Her yön için ayrı kontrol (else if yerine if kullan)
            if (up) targetEntity = currentButton.NavigateUp;
            if (down) targetEntity = currentButton.NavigateDown;
            if (left) targetEntity = currentButton.NavigateLeft;
            if (right) targetEntity = currentButton.NavigateRight;
            
            // Hedef entity geçerli mi kontrol et
            if (targetEntity != entt::null && m_Registry.valid(targetEntity))
            {
                // Hedef buton interactable mı kontrol et
                if (m_Registry.all_of<ButtonComponent>(targetEntity))
                {
                    auto& targetButton = m_Registry.get<ButtonComponent>(targetEntity);
                    if (targetButton.Interactable)
                    {
                        return Entity{ targetEntity, this };
                    }
                }
            }
            
            // Hedef bulunamadı veya geçersiz, mevcut butonda kal
            return Entity{};
        }
        
        // None mode: Navigation kapalı
        if (currentButton.Navigation == ButtonComponent::NavigationMode::None)
        {
            return Entity{};
        }
        
        // Horizontal mode: Sadece yatay
        if (currentButton.Navigation == ButtonComponent::NavigationMode::Horizontal)
        {
            if (up || down) return Entity{}; // Dikey hareket yasak
            if (!left && !right) return Entity{};
        }
        
        // Vertical mode: Sadece dikey
        if (currentButton.Navigation == ButtonComponent::NavigationMode::Vertical)
        {
            if (left || right) return Entity{}; // Yatay hareket yasak
            if (!up && !down) return Entity{};
        }
        
        // Automatic mode: En yakın butonu bul
        glm::vec3 currentPos = glm::vec3(currentTransform.WorldTransform[3]);
        
        Entity bestCandidate;
        float bestDistance = FLT_MAX;
        
        auto buttonView = m_Registry.view<ButtonComponent, TransformComponent>();
        for (auto entity : buttonView)
        {
            if (entity == m_SelectedButton) continue; // Kendini atla
            
            auto& button = buttonView.get<ButtonComponent>(entity);
            if (!button.Interactable) continue;
            
            auto& transform = buttonView.get<TransformComponent>(entity);
            glm::vec3 pos = glm::vec3(transform.WorldTransform[3]);
            
            glm::vec3 dir = pos - currentPos;
            float distance = glm::length(dir);
            
            if (distance < 0.01f) continue; // Çok yakın
            
            dir = glm::normalize(dir);
            
            // Yön kontrolü
            bool validDirection = false;
            if (up && dir.y > 0.5f) validDirection = true;
            if (down && dir.y < -0.5f) validDirection = true;
            if (left && dir.x < -0.5f) validDirection = true;
            if (right && dir.x > 0.5f) validDirection = true;
            
            if (validDirection && distance < bestDistance)
            {
                bestDistance = distance;
                bestCandidate = Entity{ entity, this };
            }
        }
        
        return bestCandidate;
    }

    template<>
    void Scene::OnComponentAdded<NativeScriptComponent>([[maybe_unused]] Entity entity, [[maybe_unused]] NativeScriptComponent& component)
    {
    }

    template<>
    void Scene::OnComponentAdded<ConquerorScriptComponent>([[maybe_unused]] Entity entity, [[maybe_unused]] ConquerorScriptComponent& component)
    {
    }

    template<>
    void Scene::OnComponentAdded<AudioSourceComponent>([[maybe_unused]] Entity entity, [[maybe_unused]] AudioSourceComponent& component)
    {
    }

    template<>
    void Scene::OnComponentAdded<AudioListenerComponent>([[maybe_unused]] Entity entity, [[maybe_unused]] AudioListenerComponent& component)
    {
    }

    template<>
    void Scene::OnComponentAdded<AudioChorusFilterComponent>([[maybe_unused]] Entity entity, [[maybe_unused]] AudioChorusFilterComponent& component)
    {
    }

    template<>
    void Scene::OnComponentAdded<AudioDistortionFilterComponent>([[maybe_unused]] Entity entity, [[maybe_unused]] AudioDistortionFilterComponent& component)
    {
    }

    template<>
    void Scene::OnComponentAdded<AudioEchoFilterComponent>([[maybe_unused]] Entity entity, [[maybe_unused]] AudioEchoFilterComponent& component)
    {
    }

    template<>
    void Scene::OnComponentAdded<AudioHighPassFilterComponent>([[maybe_unused]] Entity entity, [[maybe_unused]] AudioHighPassFilterComponent& component)
    {
    }

    template<>
    void Scene::OnComponentAdded<AudioLowPassFilterComponent>([[maybe_unused]] Entity entity, [[maybe_unused]] AudioLowPassFilterComponent& component)
    {
    }

    template<>
    void Scene::OnComponentAdded<AudioReverbFilterComponent>([[maybe_unused]] Entity entity, [[maybe_unused]] AudioReverbFilterComponent& component)
    {
    }

    template<>
    void Scene::OnComponentAdded<AudioReverbZoneComponent>([[maybe_unused]] Entity entity, [[maybe_unused]] AudioReverbZoneComponent& component)
    {
    }

    template<>
    void Scene::OnComponentAdded<AudioGainComponent>([[maybe_unused]] Entity entity, [[maybe_unused]] AudioGainComponent& component)
    {
    }

    template<>
    void Scene::OnComponentAdded<AudioPanComponent>([[maybe_unused]] Entity entity, [[maybe_unused]] AudioPanComponent& component)
    {
    }

    template<>
    void Scene::OnComponentAdded<NavMeshAgentComponent>([[maybe_unused]] Entity entity, [[maybe_unused]] NavMeshAgentComponent& component)
    {
    }

    template<>
    void Scene::OnComponentAdded<NavMeshObstacleComponent>([[maybe_unused]] Entity entity, [[maybe_unused]] NavMeshObstacleComponent& component)
    {
    }

    template<>
    void Scene::OnComponentAdded<NavMeshSurfaceComponent>([[maybe_unused]] Entity entity, [[maybe_unused]] NavMeshSurfaceComponent& component)
    {
    }

    void Scene::UpdateAudioSystem([[maybe_unused]] Timestep ts)
    {
        // Audio Listener pozisyonunu güncelle
        auto listenerView = m_Registry.view<TransformComponent, AudioListenerComponent>();
        for (auto entity : listenerView)
        {
            auto [transform, listener] = listenerView.get<TransformComponent, AudioListenerComponent>(entity);
            
            if (listener.IsActive)
            {
                AudioEngine::Get().SetListenerPosition(transform.Position);
                break; // Sadece ilk aktif listener'ı kullan
            }
        }
        
        // Audio Source'ları güncelle
        auto audioView = m_Registry.view<TransformComponent, AudioSourceComponent>();
        for (auto entityID : audioView)
        {
            Entity entity{ entityID, this };
            auto& transform = entity.GetComponent<TransformComponent>();
            auto& audioSource = entity.GetComponent<AudioSourceComponent>();
            
            // RuntimeSound yoksa ve dosya yolu varsa yükle
            if (!audioSource.RuntimeSound && !audioSource.FilePath.empty())
            {
                std::string loadPath = audioSource.FilePath;
                if (loadPath.size() > 3 && loadPath[0] == 'C' && loadPath[1] == 'Q' && loadPath[2] == ':')
                {
                    std::string relative = loadPath.substr(3);
                    auto engineDir = std::filesystem::current_path();
                    loadPath = (engineDir / std::filesystem::path(relative)).string();
                }
                else if (std::filesystem::path(loadPath).is_relative())
                {
                    static std::unordered_map<std::string, std::string> s_AudioPathCache;
                    auto it = s_AudioPathCache.find(loadPath);
                    if (it != s_AudioPathCache.end())
                    {
                        loadPath = it->second;
                    }
                    else
                    {
                        auto projectDir = Project::GetActiveProjectDirectory();
                        if (!projectDir.empty())
                        {
                            loadPath = (projectDir / loadPath).string();
                            s_AudioPathCache[audioSource.FilePath] = loadPath;
                        }
                    }
                }

                AudioSource* source = new AudioSource();
                if (source->LoadFromFile(loadPath))
                {
                    audioSource.RuntimeSound = source;
                    audioSource.NeedsReconnection = true; // İlk yüklemede bağla
                    
                    if (audioSource.PlayOnAwake)
                    {
                        source->Play();
                        audioSource.IsPlaying = true;
                    }
                }
                else
                {
                    delete source;
                }
            }
            
            if (!audioSource.RuntimeSound) continue;
            
            AudioSource* source = (AudioSource*)audioSource.RuntimeSound;
            
            // Temel Ayarlar
            source->SetVolume(audioSource.Volume);
            source->SetPitch(audioSource.Pitch);
            source->SetPan(audioSource.Pan);
            source->SetLooping(audioSource.Loop);
            source->SetMute(audioSource.Mute);
            if (source->IsSpatialized() != audioSource.Is3D)
            {
                source->SetSpatialization(audioSource.Is3D);
                audioSource.NeedsReconnection = true;
            }
            
            if (audioSource.Is3D)
            {
                source->SetPosition(transform.Position);
                source->SetMinDistance(audioSource.MinDistance);
                source->SetMaxDistance(audioSource.MaxDistance);
                source->SetDopplerFactor(audioSource.DopplerLevel);
                source->SetAttenuationModel((AudioAttenuationModel)audioSource.Attenuation);
                source->SetRolloff(audioSource.RolloffFactor);
            }
            
            audioSource.IsPlaying = source->IsPlaying();
            
            // Eğer graph oluşturulmamışsa (Editor'de değilsek veya Editor açılmamışsa) varsayılan bir GraphLinks oluştur
            if (audioSource.GraphNodes.empty())
            {
                if (audioSource.NeedsReconnection)
                {
                    // Fallback: Sadece Source -> Master bağla
                    source->ConnectToEndpoint();
                    audioSource.NeedsReconnection = false;
                }
                continue;
            }

            // Node ID'den filtre nesnesini bulan/yaratan lambda
            auto getNodeInstance = [&](int nodeID) -> AudioNode* {
                AudioGraphNodeType type = AudioGraphNodeType::Master;
                bool found = false;
                for (auto& n : audioSource.GraphNodes) {
                    if (n.ID == nodeID) { type = n.Type; found = true; break; }
                }
                if (!found) return nullptr;
                
                if (type == AudioGraphNodeType::Source) return source;
                if (type == AudioGraphNodeType::Master) return nullptr;

                #define HANDLE_FILTER(NodeType, ComponentType, AudioClass, ...) \
                if (type == NodeType && entity.HasComponent<ComponentType>()) { \
                    auto& comp = entity.GetComponent<ComponentType>(); \
                    if (!comp.RuntimeFilter) comp.RuntimeFilter = new AudioClass(__VA_ARGS__); \
                    else ((AudioClass*)comp.RuntimeFilter)->SetParams(__VA_ARGS__); \
                    return (AudioNode*)comp.RuntimeFilter; \
                }

                HANDLE_FILTER(AudioGraphNodeType::LowPass, AudioLowPassFilterComponent, AudioLowPassFilter, comp.CutoffFrequency, 8);
                HANDLE_FILTER(AudioGraphNodeType::HighPass, AudioHighPassFilterComponent, AudioHighPassFilter, comp.CutoffFrequency, 8);
                HANDLE_FILTER(AudioGraphNodeType::Echo, AudioEchoFilterComponent, AudioEchoFilter, comp.Delay, comp.Decay, comp.WetMix, comp.DryMix);
                HANDLE_FILTER(AudioGraphNodeType::Reverb, AudioReverbFilterComponent, AudioReverbFilter, 1.0f, comp.DecayTime, 0.5f, 1.0f);
                HANDLE_FILTER(AudioGraphNodeType::Chorus, AudioChorusFilterComponent, AudioChorusFilter, comp.Delay, comp.Depth, comp.Rate, comp.WetMix, comp.DryMix);
                HANDLE_FILTER(AudioGraphNodeType::Distortion, AudioDistortionFilterComponent, AudioDistortionFilter, comp.Level);
                HANDLE_FILTER(AudioGraphNodeType::Gain, AudioGainComponent, AudioGainNode, comp.Gain);
                HANDLE_FILTER(AudioGraphNodeType::Pan, AudioPanComponent, AudioPanNode, comp.Pan);
                #undef HANDLE_FILTER

                return nullptr;
            };

            // Her node için parametreleri güncelle (Reconnection gerekmese bile slider'lar etki etsin diye)
            for (auto& n : audioSource.GraphNodes) {
                if (n.Type != AudioGraphNodeType::Source && n.Type != AudioGraphNodeType::Master) {
                    getNodeInstance(n.ID); 
                }
            }

            // Bağlantıları güncelle
            if (audioSource.NeedsReconnection)
            {
                // Tüm bağlantıları kes
                source->DetachAllOutputs();
                for (auto& n : audioSource.GraphNodes) {
                    if (n.Type != AudioGraphNodeType::Source && n.Type != AudioGraphNodeType::Master) {
                        AudioNode* node = getNodeInstance(n.ID);
                        if (node) node->DetachAllOutputs();
                    }
                }

                // Yeni bağlantıları kur
                for (auto& link : audioSource.GraphLinks)
                {
                    bool fromBypassed = false;
                    for (auto& n : audioSource.GraphNodes) {
                        if (n.ID == link.FromNodeID && n.Bypassed) { fromBypassed = true; break; }
                    }
                    if (fromBypassed) continue; // Bypassed bir node çıkış yapamaz
                    
                    AudioNode* fromNode = getNodeInstance(link.FromNodeID);
                    if (!fromNode) continue;
                    
                    int targetID = link.ToNodeID;
                    // Eğer hedef bypassed ise, sonrakine atla
                    while (true) {
                        bool targetBypassed = false;
                        for (auto& n : audioSource.GraphNodes) {
                            if (n.ID == targetID && n.Bypassed) { targetBypassed = true; break; }
                        }
                        if (!targetBypassed) break;
                        
                        int nextID = -1;
                        for (auto& l : audioSource.GraphLinks) {
                            if (l.FromNodeID == targetID) { nextID = l.ToNodeID; break; }
                        }
                        if (nextID == -1) break;
                        targetID = nextID;
                    }
                    
                    bool isTargetMaster = false;
                    for (auto& n : audioSource.GraphNodes) {
                        if (n.ID == targetID && n.Type == AudioGraphNodeType::Master) {
                            isTargetMaster = true; break;
                        }
                    }
                    
                    if (isTargetMaster)
                    {
                        fromNode->ConnectToEndpoint();
                    }
                    else
                    {
                        AudioNode* toNode = getNodeInstance(targetID);
                        if (toNode) fromNode->ConnectTo(toNode);
                    }
                }
                
                audioSource.NeedsReconnection = false;
            }
        }
    }

    void Scene::UpdateAnimators(Timestep ts)
    {
        auto view = m_Registry.view<AnimatorComponent, ModelComponent>();
        for (auto entity : view)
        {
            auto& anim = view.get<AnimatorComponent>(entity);
            auto& mc = view.get<ModelComponent>(entity);

            if (!mc.ModelData || !mc.ModelData->IsSkinned || !mc.ModelData->SkeletonData || mc.ModelData->Animations.empty())
                continue;

            const int clipCount = static_cast<int>(mc.ModelData->Animations.size());
            int idx = anim.ActiveClipIndex;
            if (idx < 0)
                idx = 0;
            if (idx >= clipCount)
                idx = clipCount - 1;

            const auto& clipPtr = mc.ModelData->Animations[static_cast<size_t>(idx)];
            if (!clipPtr)
                continue;

            if (anim.Playing)
                anim.CurrentTime += ts.GetSeconds() * anim.Speed;

            Animator::ComputeBoneMatrices(*mc.ModelData->SkeletonData, *clipPtr, anim.CurrentTime, anim.BoneMatricesGPU);
        }

        // AnimationComponent state machine
        auto animView = m_Registry.view<AnimationComponent, ModelComponent>();
        for (auto entity : animView)
        {
            auto& ac = animView.get<AnimationComponent>(entity);
            auto& mc = animView.get<ModelComponent>(entity);

            if (!ac.Controller)
                continue;

            if (!ac.Playing && ac.PlayOnAwake)
                ac.Playing = true;

            if (!ac.Playing)
                continue;

            if (!mc.ModelData || !mc.ModelData->IsSkinned || !mc.ModelData->SkeletonData || mc.ModelData->Animations.empty())
                continue;

            if (ac.Controller->Layers.empty())
                continue;

            auto& layer = ac.Controller->Layers[0];

            // Initialize current state if empty
            if (ac.CurrentStateName.empty())
            {
                ac.CurrentStateName = layer.DefaultState;
                ac.CurrentTime = 0.f;
            }

            // Find current state
            AnimState* currentState = ac.Controller->GetState(layer, ac.CurrentStateName);
            if (!currentState)
            {
                ac.CurrentStateName = layer.DefaultState;
                currentState = ac.Controller->GetState(layer, ac.CurrentStateName);
                if (!currentState) continue;
                ac.CurrentTime = 0.f;
            }

            // Check transitions
            if (!ac.IsTransitioning)
            {
                auto checkTransitions = [&](const std::vector<AnimTransition>& transitions) -> bool
                {
                    for (const auto& trans : transitions)
                    {
                        if (trans.FromState != ac.CurrentStateName)
                            continue;

                        bool conditionsMet = true;
                        for (const auto& cond : trans.Conditions)
                        {
                            bool found = false;
                            for (const auto& param : ac.Parameters)
                            {
                                if (param.Name == cond.ParameterName)
                                {
                                    found = true;
                                    switch (param.Type)
                                    {
                                    case AnimParameterType::Float:
                                        switch (cond.Mode)
                                        {
                                        case AnimConditionMode::Greater: conditionsMet = param.FloatValue > cond.Threshold; break;
                                        case AnimConditionMode::Less: conditionsMet = param.FloatValue < cond.Threshold; break;
                                        case AnimConditionMode::Equals: conditionsMet = std::abs(param.FloatValue - cond.Threshold) < 0.001f; break;
                                        case AnimConditionMode::NotEquals: conditionsMet = std::abs(param.FloatValue - cond.Threshold) >= 0.001f; break;
                                        }
                                        break;
                                    case AnimParameterType::Bool:
                                        conditionsMet = param.BoolValue == (cond.Threshold > 0.5f);
                                        break;
                                    case AnimParameterType::Int:
                                        switch (cond.Mode)
                                        {
                                        case AnimConditionMode::Greater: conditionsMet = param.IntValue > static_cast<int>(cond.Threshold); break;
                                        case AnimConditionMode::Less: conditionsMet = param.IntValue < static_cast<int>(cond.Threshold); break;
                                        case AnimConditionMode::Equals: conditionsMet = param.IntValue == static_cast<int>(cond.Threshold); break;
                                        case AnimConditionMode::NotEquals: conditionsMet = param.IntValue != static_cast<int>(cond.Threshold); break;
                                        }
                                        break;
                                    case AnimParameterType::Trigger:
                                        conditionsMet = param.FloatValue > 0.5f;
                                        break;
                                    }
                                    break;
                                }
                            }
                            if (!found || !conditionsMet) break;
                        }

                        if (conditionsMet)
                        {
                            if (trans.HasExitTime && currentState)
                            {
                                float clipDuration = 0.f;
                                if (currentState->ClipIndex >= 0 && currentState->ClipIndex < static_cast<int>(mc.ModelData->Animations.size()))
                                {
                                    auto& clip = mc.ModelData->Animations[currentState->ClipIndex];
                                    if (clip)
                                        clipDuration = clip->DurationSeconds();
                                }

                                if (clipDuration > 0.f)
                                {
                                    float normalizedTime = ac.CurrentTime / clipDuration;
                                    if (normalizedTime < trans.ExitTime)
                                        continue;
                                }
                            }

                            ac.IsTransitioning = true;
                            ac.nextStateName = trans.ToState;
                            ac.TransitionProgress = 0.f;
                            ac.TransitionDuration = trans.Duration;

                            for (auto& param : ac.Parameters)
                            {
                                if (param.Type == AnimParameterType::Trigger)
                                    param.FloatValue = 0.f;
                            }

                            return true;
                        }
                    }
                    return false;
                };

                if (!checkTransitions(layer.Transitions))
                {
                    for (auto& ss : ac.Controller->SubStates)
                    {
                        if (!ss.Transitions.empty())
                        {
                            if (checkTransitions(ss.Transitions))
                                break;
                        }
                    }
                }
            }

            // Process transition
            if (ac.IsTransitioning)
            {
                ac.TransitionProgress += ts.GetSeconds();
                if (ac.TransitionProgress >= ac.TransitionDuration)
                {
                    std::string completedTarget = ac.nextStateName;
                    ac.IsTransitioning = false;
                    ac.TransitionProgress = 0.f;
                    ac.nextStateName.clear();

                    if (completedTarget == "Exit")
                    {
                        if (!ac.SubStateStack.empty())
                        {
                            auto& entry = ac.SubStateStack.back();
                            ac.CurrentStateName = entry.StateName;
                            ac.CurrentTime = entry.CurrentTime;
                            ac.SubStateStack.pop_back();
                        }
                        else
                        {
                            ac.CurrentStateName = layer.DefaultState;
                            ac.CurrentTime = 0.f;
                        }
                    }
                    else
                    {
                        bool isSubState = false;
                        for (const auto& ss : ac.Controller->SubStates)
                        {
                            if (ss.Name == completedTarget) { isSubState = true; break; }
                        }
                        if (!isSubState)
                        {
                            for (const auto& lyr : ac.Controller->Layers)
                            {
                                if (lyr.Name == completedTarget) { isSubState = true; break; }
                            }
                        }

                        if (isSubState)
                        {
                            AnimSubStateData* targetSS = nullptr;
                            for (auto& ss : ac.Controller->SubStates)
                            {
                                if (ss.Name == completedTarget) { targetSS = &ss; break; }
                            }

                            AnimationComponent::SubStateStackEntry stackEntry;
                            stackEntry.SubStateName = ac.CurrentStateName;
                            stackEntry.StateName = ac.CurrentStateName;
                            stackEntry.CurrentTime = ac.CurrentTime;
                            stackEntry.IsInSubState = !ac.SubStateStack.empty();
                            ac.SubStateStack.push_back(stackEntry);

                            if (targetSS && !targetSS->DefaultState.empty())
                            {
                                ac.CurrentStateName = targetSS->DefaultState;
                            }
                            else
                            {
                                ac.CurrentStateName = completedTarget;
                            }
                            ac.CurrentTime = 0.f;
                        }
                        else
                        {
                            ac.CurrentStateName = completedTarget;
                            ac.CurrentTime = 0.f;
                        }
                    }
                }
            }

            // Advance time
            currentState = ac.Controller->GetState(layer, ac.CurrentStateName);
            if (currentState && ac.Playing)
            {
                float stateSpeed = currentState->Speed * ac.Speed;
                ac.CurrentTime += ts.GetSeconds() * stateSpeed;

                // Loop or clamp
                if (currentState->ClipIndex >= 0 && currentState->ClipIndex < static_cast<int>(mc.ModelData->Animations.size()))
                {
                    auto& clip = mc.ModelData->Animations[currentState->ClipIndex];
                    if (clip)
                    {
                        float clipDuration = clip->DurationSeconds();
                        if (clipDuration > 0.f)
                        {
                            if (currentState->Loop)
                            {
                                while (ac.CurrentTime >= clipDuration)
                                    ac.CurrentTime -= clipDuration;
                            }
                            else
                            {
                                if (ac.CurrentTime > clipDuration)
                                    ac.CurrentTime = clipDuration;
                            }
                        }
                    }
                }
            }

            // Compute bone matrices
            currentState = ac.Controller->GetState(layer, ac.CurrentStateName);
            if (currentState && currentState->ClipIndex >= 0 && currentState->ClipIndex < static_cast<int>(mc.ModelData->Animations.size()))
            {
                auto& clip = mc.ModelData->Animations[currentState->ClipIndex];
                if (clip)
                {
                    if (ac.IsTransitioning && ac.TransitionDuration > 0.f)
                    {
                        // Crossfade: compute source and target, blend
                        AnimState* sourceState = ac.Controller->GetState(layer, ac.CurrentStateName);
                        AnimState* targetState = ac.Controller->GetState(layer, ac.nextStateName);

                        float blendFactor = ac.TransitionProgress / ac.TransitionDuration;
                        blendFactor = glm::clamp(blendFactor, 0.f, 1.f);

                        std::vector<glm::mat4> sourceMatrices, targetMatrices;

                        if (sourceState && sourceState->ClipIndex >= 0 && sourceState->ClipIndex < static_cast<int>(mc.ModelData->Animations.size()))
                        {
                            auto& sourceClip = mc.ModelData->Animations[sourceState->ClipIndex];
                            if (sourceClip)
                                Animator::ComputeBoneMatrices(*mc.ModelData->SkeletonData, *sourceClip, ac.CurrentTime, sourceMatrices);
                        }

                        if (targetState && targetState->ClipIndex >= 0 && targetState->ClipIndex < static_cast<int>(mc.ModelData->Animations.size()))
                        {
                            auto& targetClip = mc.ModelData->Animations[targetState->ClipIndex];
                            if (targetClip)
                            {
                                float targetTime = 0.f;
                                Animator::ComputeBoneMatrices(*mc.ModelData->SkeletonData, *targetClip, targetTime, targetMatrices);
                            }
                        }

                        // Blend matrices
                        uint32_t boneCount = mc.ModelData->SkeletonData->GetBoneCount();
                        ac.BoneMatricesGPU.resize(boneCount);
                        for (uint32_t i = 0; i < boneCount; i++)
                        {
                            glm::mat4 src = (i < sourceMatrices.size()) ? sourceMatrices[i] : glm::mat4(1.f);
                            glm::mat4 dst = (i < targetMatrices.size()) ? targetMatrices[i] : glm::mat4(1.f);
                            ac.BoneMatricesGPU[i] = src + blendFactor * (dst - src);
                        }
                    }
                    else
                    {
                        Animator::ComputeBoneMatrices(*mc.ModelData->SkeletonData, *clip, ac.CurrentTime, ac.BoneMatricesGPU);
                    }
                }
            }
        }
    }

    void Scene::StopAllAudio()
    {
        auto view = m_Registry.view<AudioSourceComponent>();
        for (auto entityID : view)
        {
            auto& audioSource = view.get<AudioSourceComponent>(entityID);
            if (audioSource.RuntimeSound)
            {
                AudioSource* source = (AudioSource*)audioSource.RuntimeSound;
                source->Stop();
                audioSource.IsPlaying = false;
            }
        }
    }
}
