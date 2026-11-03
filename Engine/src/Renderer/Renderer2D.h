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
        // -1: kare ayarı (HDR hattında doğrusal), 0: sRGB çıktı (post-process sonrası UI), 1: doğrusal
        static void SetOutputMode(int mode);
        // 2D ışık dokusu (0 = kapalı) ve görünüm kökeni (gl_FragCoord -> doku pikseli)
        static void SetLighting(uint32_t lightTexture, const glm::vec2& viewportOrigin);
        // Sonraki çizimler ışıkla çarpılsın mı (sprite / karo: evet, UI / metin: hayır)
        static void SetLit(bool lit);
        // Sprite Mask etkileşimi: 0 yok, 1 maskenin içinde görünür, 2 dışında görünür
        static void SetMaskInteraction(int mode);

        // Primitives - Textured (Her sprite için ayrı render)
        static void DrawSprite(const glm::mat4& transform, const std::shared_ptr<Texture2D>& texture, float tilingFactor, const glm::vec4& tintColor, struct SpriteRendererComponent& sprite);
        
        // 9-Slice rendering
        static void DrawSprite9Slice(const glm::mat4& transform, const std::shared_ptr<Texture2D>& texture, float borderSize, const glm::vec4& tintColor, struct SpriteRendererComponent& sprite);

        // ---- Toplu (batched) çizim temel öğeleri
        static void DrawQuad(const glm::mat4& transform, const glm::vec4& color);
        static void DrawQuad(const glm::mat4& transform, const std::shared_ptr<Texture2D>& texture, const glm::vec2& uvMin, const glm::vec2& uvMax,
                             const glm::vec4& tintColor = glm::vec4(1.0f));
        // Yerel dikdörtgen (min/max) + dönüşüm
        static void DrawRect(const glm::mat4& transform, const glm::vec2& localMin, const glm::vec2& localMax, const std::shared_ptr<Texture2D>& texture,
                             const glm::vec2& uvMin, const glm::vec2& uvMax, const glm::vec4& tintColor);
        // tilingFactor < 0: SDF metin modu (doku alfası mesafe alanıdır)
        static void DrawQuadCorners(const glm::vec3 positions[4], const glm::vec2 uvs[4], const std::shared_ptr<Texture2D>& texture, const glm::vec4& tintColor,
                                    float tilingFactor = 1.0f);

        // Sprite'ın yerel uzaydaki sınırları (pivot, PPU, sayfa dilimi, çizim modu dahil)
        static bool GetSpriteLocalBounds(const struct SpriteRendererComponent& sprite, glm::vec2& outMin, glm::vec2& outMax);
        // Doku atandığında (Unity: sprite içe aktarma ayarları): .meta'daki Pixels Per Unit ve dokunun tek dilimi (9-slice) uygulanır
        static void ApplySpriteImport(struct SpriteRendererComponent& sprite);

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
    };
}
