#pragma once

#include "Core/Base/Base.h"
#include "Core/Time/Timestep.h"
#include "AISystem/Navigation/NavMesh.h"

#include <glm/glm.hpp>
#include <entt/entt.hpp>

namespace Conqueror
{
    class Entity;
    class EditorCamera;
    class Cubemap;
    struct AnimationComponent;

    class PhysicsWorld2D;
    class PhysicsWorld3D;

    class CQ_API Scene
    {
    public:
        Scene();
        ~Scene();

        Entity CreateEntity(const std::string& name = std::string());
        Entity CreateEntityWithUUID(uint64_t uuid, const std::string& name = std::string());
        void DestroyEntity(Entity entity);
        Entity DuplicateEntity(Entity source, const std::string& newName = "");
        void CloneSceneFrom(const std::shared_ptr<Scene>& source);

        void OnUpdateRuntime(Timestep ts, bool renderOnly = false);
        void OnUpdateSimulation(Timestep ts);
        void OnUpdateEditor(Timestep ts, EditorCamera& camera);
        void OnUpdateEditor(Timestep ts, EditorCamera& camera, const glm::vec2* viewportBounds);
        
        void StopAllAudio();

        void OnViewportResize(uint32_t width, uint32_t height);

        Entity GetPrimaryCameraEntity();

        // Tag sistemi
        Entity FindEntityByTag(const std::string& tag);
        std::vector<Entity> FindEntitiesWithTag(const std::string& tag);
        Entity FindEntityByUUID(uint64_t uuid);
        static const std::vector<std::string>& GetAvailableTags();

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
        void SetEntityParent(Entity entity, Entity parent);
        void RemoveEntityParent(Entity entity);
        Entity GetEntityParent(Entity entity) const;
        std::vector<Entity> GetEntityChildren(Entity entity) const;
        bool HasChildren(Entity entity) const;

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
        
        // Fog
        void SetFogEnabled(bool enabled) { m_FogEnabled = enabled; }
        bool IsFogEnabled() const { return m_FogEnabled; }
        
        void SetFogColor(const glm::vec3& color) { m_FogColor = color; }
        glm::vec3 GetFogColor() const { return m_FogColor; }
        
        void SetFogDensity(float density) { m_FogDensity = density; }
        float GetFogDensity() const { return m_FogDensity; }
        
        void SetFogStart(float start) { m_FogStart = start; }
        float GetFogStart() const { return m_FogStart; }
        
        void SetFogEnd(float end) { m_FogEnd = end; }
        float GetFogEnd() const { return m_FogEnd; }
        
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
        void SetShadowEnabled(bool enabled) { m_ShadowEnabled = enabled; }
        bool IsShadowEnabled() const { return m_ShadowEnabled; }
        
        void SetShadowIntensity(float intensity) { m_ShadowIntensity = intensity; }
        float GetShadowIntensity() const { return m_ShadowIntensity; }

        // Environment Reflections
        void SetReflectionSource(ReflectionSource source) { m_ReflectionSource = source; }
        ReflectionSource GetReflectionSource() const { return m_ReflectionSource; }
        
        void SetReflectionResolution(int resolution) { m_ReflectionResolution = resolution; }
        int GetReflectionResolution() const { return m_ReflectionResolution; }
        
        void SetReflectionCompression(int compression) { m_ReflectionCompression = compression; }
        int GetReflectionCompression() const { return m_ReflectionCompression; }
        
        void SetReflectionIntensityMultiplier(float multiplier) { m_ReflectionIntensityMultiplier = multiplier; }
        float GetReflectionIntensityMultiplier() const { return m_ReflectionIntensityMultiplier; }
        
        void SetReflectionBounces(int bounces) { m_ReflectionBounces = bounces; }
        int GetReflectionBounces() const { return m_ReflectionBounces; }

        // Physics
        PhysicsWorld2D* GetPhysicsWorld2D() const { return m_PhysicsWorld2D.get(); }
        PhysicsWorld3D* GetPhysicsWorld3D() const { return m_PhysicsWorld3D.get(); }

        // AI Navigation
        NavMesh& GetNavMesh() { return m_NavMesh; }

    private:
        template<typename T>
        void OnComponentAdded(Entity entity, T& component);

        void UpdateTransformHierarchy(Entity entity, const glm::mat4& parentTransform);
        void UpdateButtonInput(class EditorCamera& camera, Timestep ts, const glm::vec2* viewportBounds);
        Entity FindNextButton(bool up, bool down, bool left, bool right);
        bool IsCanvasOrCanvasChild(Entity entity) const;
        void UpdateAudioSystem(Timestep ts);
        void UpdateAnimators(Timestep ts);

    public:
        entt::registry m_Registry;
        uint32_t m_ViewportWidth = 0, m_ViewportHeight = 0;
        
    private:
        static std::vector<std::string> s_AvailableTags;
        std::vector<LayerInfo> m_Layers;

        // Hierarchy data
        std::unordered_map<entt::entity, entt::entity> m_EntityParent; // child -> parent
        std::unordered_map<entt::entity, std::vector<entt::entity>> m_EntityChildren; // parent -> children

        // UI Navigation
        entt::entity m_SelectedButton = entt::null; // Klavye ile seçili buton

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
        
        // Fog
        bool m_FogEnabled = false;
        glm::vec3 m_FogColor = glm::vec3(0.5f, 0.6f, 0.7f);
        float m_FogDensity = 0.02f;
        float m_FogStart = 10.0f;
        float m_FogEnd = 100.0f;
        
        // Sun Source
        uint64_t m_SunSourceUUID = 0; // 0 = None (ilk directional light kullanılır)
        
        // Halo/Flare
        bool m_HaloEnabled = false;
        std::shared_ptr<class Texture2D> m_HaloTexture;
        float m_HaloStrength = 0.5f;
        bool m_FlareEnabled = false;
        float m_FlareFadeSpeed = 3.0f;
        float m_FlareStrength = 1.0f;
        std::vector<FlareElement> m_FlareElements;
        std::shared_ptr<class Texture2D> m_SpotCookie;

        // Shadow settings
        bool m_ShadowEnabled = true;
        float m_ShadowIntensity = 1.0f;

        // Environment Reflections
        ReflectionSource m_ReflectionSource = ReflectionSource::Skybox;
        int m_ReflectionResolution = 128;
        int m_ReflectionCompression = 0; // Auto
        float m_ReflectionIntensityMultiplier = 1.0f;
        int m_ReflectionBounces = 1;

        // Physics
        std::unique_ptr<PhysicsWorld2D> m_PhysicsWorld2D;
        std::unique_ptr<PhysicsWorld3D> m_PhysicsWorld3D;
        bool m_PhysicsWarmedUp = false;

        // AI
        NavMesh m_NavMesh;

        friend class Entity;
        friend class SceneSerializer;
    };

    // Explicit specialization declaration for AnimationComponent
    template<>
    void Scene::OnComponentAdded<AnimationComponent>(Entity entity, AnimationComponent& component);
}
