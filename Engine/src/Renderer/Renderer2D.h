#pragma once

#include "RHI/Texture.h"
#include "Scene/EditorCamera.h"
#include "Scene/SceneCamera.h"

#include <glm/glm.hpp>

#include <functional>

namespace Conqueror
{
    class Shader;

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
        // tilingFactor < 0: SDF metin modu (doku alfası mesafe alanıdır). uv1: köşe başına ikinci UV kanalı (UI Position As UV1),
        // nullptr = 0
        static void DrawQuadCorners(const glm::vec3 positions[4], const glm::vec2 uvs[4], const std::shared_ptr<Texture2D>& texture, const glm::vec4& tintColor,
                                    float tilingFactor = 1.0f, const glm::vec2* uv1 = nullptr);

        // Köşe başına renk (metin renk geçişleri): sıra positions ile aynı
        static void DrawQuadCornersColors(const glm::vec3 positions[4], const glm::vec2 uvs[4], const std::shared_ptr<Texture2D>& texture, const glm::vec4 colors[4],
                                          float tilingFactor = 1.0f, const glm::vec2* uv1 = nullptr);

        // Özel shader (Unity Graphic.material, UI Image > Material): sonraki çizimler bu shader'la yapılır (önceki toplu çizim
        // boşaltılır); nullptr = varsayılan 2D shader. Shader 2D köşe düzenini (a_Position, a_Color, a_TexCoord, a_TexIndex,
        // a_TilingFactor, a_UV1) ve u_ViewProjection, u_Textures[31], u_OutputMode'u kullanır; uniforms ek değerleri yazar.
        // BeginScene varsayılana döner
        static void SetCustomShader(const std::shared_ptr<Shader>& shader, const std::function<void(Shader&)>& uniforms = {});

        // Sprite'ın çizileceği doku ve UV dikdörtgeni (sayfa dilimi, Sprite Atlas, FlipX/FlipY dahil); Sprite Skin gibi özel çizimler için
        static bool GetSpriteUV(const struct SpriteRendererComponent& sprite, std::shared_ptr<Texture2D>& outTexture, glm::vec2& uvMin, glm::vec2& uvMax);
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
