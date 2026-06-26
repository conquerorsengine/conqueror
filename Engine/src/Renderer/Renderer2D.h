#pragma once

#include "RHI/Texture.h"
#include "Scene/EditorCamera.h"
#include "Scene/SceneCamera.h"

#include <glm/glm.hpp>

namespace Conqueror
{
    class Renderer2D
    {
    public:
        static void Init();
        static void Shutdown();

        static void BeginScene(const EditorCamera& camera, bool enableDepthTest = false, bool enableDepthWrite = true);
        static void BeginScene(const SceneCamera& camera, const glm::mat4& transform, bool enableDepthTest = false, bool enableDepthWrite = true);
        // Ham ViewProjection matrisi ile BeginScene (Screen-Space UI render için)
        static void BeginScene(const glm::mat4& viewProjection, bool enableDepthTest = false);
        static void EndScene();
        static void Flush();

        // Primitives - Textured (Her sprite için ayrı render)
        static void DrawSprite(const glm::mat4& transform, const std::shared_ptr<Texture2D>& texture, float tilingFactor, const glm::vec4& tintColor, struct SpriteRendererComponent& sprite);
        static void DrawSprite(const glm::mat4& transform, const std::shared_ptr<Texture2D>& texture, float tilingFactor, const glm::vec4& tintColor, struct ImageComponent& image);
        
        // 9-Slice rendering
        static void DrawSprite9Slice(const glm::mat4& transform, const std::shared_ptr<Texture2D>& texture, float borderSize, const glm::vec4& tintColor, struct ImageComponent& image);

        // Stats
        struct Statistics
        {
            uint32_t DrawCalls = 0;
            uint32_t SpriteCount = 0;
            uint32_t Vertices = 0;
            uint32_t Triangles = 0;
            uint32_t QuadCount = 0;

            uint32_t GetTotalVertexCount() const { return SpriteCount * 4; }
            uint32_t GetTotalIndexCount() const { return SpriteCount * 6; }
        };

        static void ResetStats();
        static Statistics GetStats();

        // Built-in Textures
        static std::shared_ptr<Texture2D> GetWhiteTexture();
        static std::shared_ptr<Texture2D> GetCircleTexture();

        // Text Rendering
        static void DrawText(const std::string& text, const glm::vec2& position, float scale, const glm::vec4& color);
        static void DrawText(const std::string& text, const glm::vec3& position, float scale, const glm::vec4& color);
        static void DrawText(const std::string& text, const glm::mat4& transform, float scale, const glm::vec4& color);

    private:
        static void InitializeSpriteResources(struct SpriteRendererComponent& sprite);
        static void InitializeImageResources(struct ImageComponent& image);
    };
}
