#pragma once

#include "Core/Base/Base.h"
#include "AIProvider.h"
#include <string>
#include <vector>
#include <memory>

namespace Conqueror
{
    class Scene;
}

namespace Conqueror::AI
{
    class CQ_API AITools
    {
    public:
        static std::vector<ToolDefinition> GetToolDefinitions();

        static std::string ExecuteTool(Scene* activeScene, const std::string& toolName, const std::string& jsonArgs);

    private:
        // Tool implementation handlers
        static std::string CreateEntity(Scene* scene, const std::string& name, const std::string& type, const std::string& modelPath = "");
        static std::string DeleteEntity(Scene* scene, const std::string& identifier);
        static std::string SetTransform(Scene* scene, const std::string& nameOrUuid, float px, float py, float pz, float rx, float ry, float rz, float sx, float sy, float sz);
        static std::string AddComponent(Scene* scene, const std::string& nameOrUuid, const std::string& componentType, const std::string& scriptPath = "");
        static std::string SetLight(Scene* scene, const std::string& nameOrUuid, const std::string& colorHex, float intensity, float range, bool castShadows);
        static std::string SetPhysics(Scene* scene, const std::string& nameOrUuid, const std::string& bodyType, float mass, bool useGravity, bool freezeRotation);
        static std::string SetPhysics2D(Scene* scene, const std::string& nameOrUuid, const std::string& bodyType, bool fixedRotation);
        static std::string SetMaterial(Scene* scene, const std::string& nameOrUuid, const std::string& colorHex, float metallic, float roughness);
        static std::string SetParent(Scene* scene, const std::string& childIdentifier, const std::string& parentIdentifier);
        static std::string ListEntities(Scene* scene);

        // Asset Generation Handlers
        static std::string GenerateSVGAsset(const std::string& filename, const std::string& svgContent);
        static std::string GeneratePNGAsset(const std::string& filename, int width, int height, const std::string& colorHex);
        static std::string Generate3DModel(const std::string& filename, const std::string& objContent);
        static std::string GenerateAnimation(const std::string& filename, const std::string& animData);

        // Scripting & Analysis
        static std::string CreateScript(const std::string& scriptName);
        static std::string AnalyzeScene(Scene* scene);

        // ========== NEW TOOL HANDLERS ==========

        // Audio System Tools
        static std::string SetAudioSource(Scene* scene, const std::string& nameOrUuid, 
            const std::string& filePath, bool playOnAwake, bool loop, bool mute,
            float volume, float pitch, float pan, bool is3D, float minDistance, 
            float maxDistance, float dopplerLevel, const std::string& attenuationModel, float rolloffFactor);
        
        static std::string SetAudioFilter(Scene* scene, const std::string& nameOrUuid, 
            const std::string& filterType, bool enabled, float param1, float param2, 
            float param3, float param4);

        // Camera Tool
        static std::string SetCamera(Scene* scene, const std::string& nameOrUuid,
            bool isPrimary, float fov, float nearPlane, float farPlane, bool isOrthographic,
            float orthographicSize);

        // Lighting Tools
        static std::string SetPointLight(Scene* scene, const std::string& nameOrUuid,
            const std::string& colorHex, float intensity, float range,
            float constant, float linear, float quadratic, bool castShadows);

        static std::string SetSpotLight(Scene* scene, const std::string& nameOrUuid,
            const std::string& colorHex, float intensity, float range,
            float innerConeAngle, float outerConeAngle,
            float constant, float linear, float quadratic, bool castShadows);

        static std::string SetReflectionProbe(Scene* scene, const std::string& nameOrUuid,
            float offsetX, float offsetY, float offsetZ,
            float sizeX, float sizeY, float sizeZ,
            uint32_t resolution, float intensity, int bounces, bool realtimeUpdate);

        // UI Tools
        static std::string SetUICanvas(Scene* scene, const std::string& nameOrUuid,
            int renderMode, bool pixelPerfect, int sortOrder);

        static std::string SetUIButton(Scene* scene, const std::string& nameOrUuid,
            bool interactable, int navigationMode,
            const std::string& bgColorNormal, const std::string& bgColorHover,
            const std::string& bgColorPressed, const std::string& textColorNormal,
            float transitionSpeed, bool use9Slice, float borderSize);

        static std::string SetUIText(Scene* scene, const std::string& nameOrUuid,
            const std::string& text, const std::string& colorHex, float fontSize);

        static std::string SetUIImage(Scene* scene, const std::string& nameOrUuid,
            const std::string& colorHex, float tilingFactor);

        // Animation Tools
        static std::string SetAnimator(Scene* scene, const std::string& nameOrUuid,
            bool playing, float speed, int activeClipIndex);

        static std::string SetAnimation(Scene* scene, const std::string& nameOrUuid,
            const std::string& controllerPath, float speed, bool playOnAwake, int activeLayerIndex);
    };
}
