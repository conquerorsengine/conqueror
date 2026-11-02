#include "Renderer2D.h"
#include "Renderer.h"
#include "SpriteSheet.h"
#include "SpriteAtlas.h"
#include "RHI/TextureSettings.h"
#include "RHI/VertexArray.h"
#include "RHI/Shader.h"
#include "RHI/RenderCommand.h"
#include "Scene/Components.h"
#include "Font.h"

#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>

#include <array>
#include <vector>

namespace Conqueror
{
    struct QuadVertex
    {
        glm::vec3 Position;
        glm::vec4 Color;
        glm::vec2 TexCoord;
        float TexIndex;
        float TilingFactor;
    };

    // Toplu quad çizici: tek vertex buffer, 32 doku yuvası. Çizim sırası korunur
    // (aynı draw çağrısında üçgenler sırayla rasterize edilir) -> saydamlık doğru.
    struct Renderer2DData
    {
        static constexpr uint32_t MaxQuads = 20000;
        static constexpr uint32_t MaxVertices = MaxQuads * 4;
        static constexpr uint32_t MaxIndices = MaxQuads * 6;
        static constexpr uint32_t MaxTextureSlots = 31;   // 31. birim: 2D ışık dokusu
        static constexpr uint32_t LightTextureSlot = 31;

        std::shared_ptr<Shader> TextureShader;
        std::shared_ptr<Texture2D> WhiteTexture;
        std::shared_ptr<VertexArray> QuadVertexArray;
        std::shared_ptr<VertexBuffer> QuadVertexBuffer;
        std::vector<QuadVertex> Vertices;
        uint32_t IndexCount = 0;
        std::array<std::shared_ptr<Texture2D>, MaxTextureSlots> TextureSlots;
        uint32_t TextureSlotIndex = 1; // 0 = beyaz doku
        int OutputMode = -1;
        // 2D ışıklandırma (Light 2D) ve Sprite Mask etkileşimi: çizim anında uygulanır
        uint32_t LightTexture = 0;
        glm::vec2 LightOrigin{ 0.0f };
        bool Lit = false;
        int MaskMode = 0;
        glm::mat4 ViewProjection{ 1.0f };
        glm::vec4 QuadVertexPositions[4]{};
        Renderer2D::Statistics Stats;
    };

    static Renderer2DData s_Data;

    void Renderer2D::Init()
    {
        s_Data.WhiteTexture = Texture2D::Create(1, 1);
        uint32_t whiteTextureData = 0xffffffff;
        s_Data.WhiteTexture->SetData(&whiteTextureData, sizeof(uint32_t));

        s_Data.TextureShader = Shader::Create("Engine/src/Shaders/2D/Texture.cqsh");
        s_Data.TextureShader->Bind();
        int32_t samplers[Renderer2DData::MaxTextureSlots];
        for (int32_t i = 0; i < (int32_t)Renderer2DData::MaxTextureSlots; ++i)
            samplers[i] = i;
        s_Data.TextureShader->SetIntArray("u_Textures", samplers, Renderer2DData::MaxTextureSlots);

        s_Data.QuadVertexArray = VertexArray::Create();
        s_Data.QuadVertexBuffer = VertexBuffer::Create(Renderer2DData::MaxVertices * sizeof(QuadVertex));
        s_Data.QuadVertexBuffer->SetLayout({
            { ShaderDataType::Float3, "a_Position"     },
            { ShaderDataType::Float4, "a_Color"        },
            { ShaderDataType::Float2, "a_TexCoord"     },
            { ShaderDataType::Float,  "a_TexIndex"     },
            { ShaderDataType::Float,  "a_TilingFactor" }
        });
        s_Data.QuadVertexArray->AddVertexBuffer(s_Data.QuadVertexBuffer);

        std::vector<uint32_t> indices(Renderer2DData::MaxIndices);
        for (uint32_t i = 0, offset = 0; i < Renderer2DData::MaxIndices; i += 6, offset += 4)
        {
            indices[i + 0] = offset + 0;
            indices[i + 1] = offset + 1;
            indices[i + 2] = offset + 2;
            indices[i + 3] = offset + 2;
            indices[i + 4] = offset + 3;
            indices[i + 5] = offset + 0;
        }
        s_Data.QuadVertexArray->SetIndexBuffer(IndexBuffer::Create(indices.data(), Renderer2DData::MaxIndices));
        s_Data.Vertices.reserve(Renderer2DData::MaxVertices);
        s_Data.TextureSlots[0] = s_Data.WhiteTexture;

        s_Data.QuadVertexPositions[0] = { -0.5f, -0.5f, 0.0f, 1.0f };
        s_Data.QuadVertexPositions[1] = {  0.5f, -0.5f, 0.0f, 1.0f };
        s_Data.QuadVertexPositions[2] = {  0.5f,  0.5f, 0.0f, 1.0f };
        s_Data.QuadVertexPositions[3] = { -0.5f,  0.5f, 0.0f, 1.0f };
    }

    void Renderer2D::Shutdown()
    {
        // GL nesneleri bağlam kapanmadan bırakılır
        s_Data = Renderer2DData{};
    }

    static void ApplySceneState(bool enableDepthTest, bool enableDepthWrite)
    {
        if (enableDepthTest)
        {
            glEnable(GL_DEPTH_TEST);
            glDepthFunc(GL_LEQUAL);
        }
        else
        {
            glDisable(GL_DEPTH_TEST);
        }
        glDepthMask(enableDepthWrite ? GL_TRUE : GL_FALSE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }

    static void StartBatch()
    {
        s_Data.Vertices.clear();
        s_Data.IndexCount = 0;
        s_Data.TextureSlotIndex = 1;
        for (uint32_t i = 1; i < Renderer2DData::MaxTextureSlots; ++i)
            s_Data.TextureSlots[i].reset();
    }

    void Renderer2D::BeginScene(const EditorCamera& camera, bool enableDepthTest, bool enableDepthWrite)
    {
        ResetStats();
        ApplySceneState(enableDepthTest, enableDepthWrite);
        s_Data.ViewProjection = camera.GetViewProjection();
        StartBatch();
    }

    void Renderer2D::BeginScene(const SceneCamera& camera, const glm::mat4& transform, bool enableDepthTest, bool enableDepthWrite)
    {
        ResetStats();
        ApplySceneState(enableDepthTest, enableDepthWrite);
        s_Data.ViewProjection = camera.GetProjection() * glm::inverse(transform);
        StartBatch();
    }

    void Renderer2D::BeginScene(const glm::mat4& viewProjection, bool enableDepthTest)
    {
        // UI: depth test kapalı (her zaman en önde)
        ApplySceneState(enableDepthTest, true);
        s_Data.ViewProjection = viewProjection;
        StartBatch();
    }

    void Renderer2D::Flush()
    {
        if (s_Data.IndexCount == 0 || !s_Data.TextureShader)
            return;

        s_Data.QuadVertexBuffer->SetData(s_Data.Vertices.data(), (uint32_t)(s_Data.Vertices.size() * sizeof(QuadVertex)));
        for (uint32_t i = 0; i < s_Data.TextureSlotIndex; ++i)
            if (s_Data.TextureSlots[i])
                s_Data.TextureSlots[i]->Bind(i);

        s_Data.TextureShader->Bind();
        s_Data.TextureShader->SetMat4("u_ViewProjection", s_Data.ViewProjection);
        s_Data.TextureShader->SetInt("u_OutputMode", s_Data.OutputMode);
        const bool lit = s_Data.Lit && s_Data.LightTexture != 0;
        s_Data.TextureShader->SetInt("u_Lit", lit ? 1 : 0);
        if (lit)
        {
            glBindTextureUnit(Renderer2DData::LightTextureSlot, s_Data.LightTexture);
            s_Data.TextureShader->SetInt("u_LightTex", (int)Renderer2DData::LightTextureSlot);
            s_Data.TextureShader->SetFloat2("u_LightOrigin", s_Data.LightOrigin);
        }
        // Sprite Mask: stencil bit 0 (Lighting2D yazar); başka kullanıcıların stencil durumuna dokunulmaz
        if (s_Data.MaskMode != 0)
        {
            glEnable(GL_STENCIL_TEST);
            glStencilFunc(s_Data.MaskMode == 1 ? GL_EQUAL : GL_NOTEQUAL, 1, 0x01);
            glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
            glStencilMask(0x00);
        }
        RenderCommand::DrawIndexed(s_Data.QuadVertexArray, s_Data.IndexCount);
        if (s_Data.MaskMode != 0)
        {
            glStencilMask(0xFF);
            glDisable(GL_STENCIL_TEST);
        }
        s_Data.Stats.DrawCalls++;
        StartBatch();
    }

    void Renderer2D::SetLighting(uint32_t lightTexture, const glm::vec2& viewportOrigin)
    {
        if (lightTexture != s_Data.LightTexture || viewportOrigin != s_Data.LightOrigin)
        {
            Flush();
            s_Data.LightTexture = lightTexture;
            s_Data.LightOrigin = viewportOrigin;
        }
    }

    void Renderer2D::SetLit(bool lit)
    {
        if (lit != s_Data.Lit)
        {
            Flush();
            s_Data.Lit = lit;
        }
    }

    void Renderer2D::SetMaskInteraction(int mode)
    {
        if (mode != s_Data.MaskMode)
        {
            Flush();
            s_Data.MaskMode = mode;
        }
    }

    void Renderer2D::SetOutputMode(int mode)
    {
        if (mode != s_Data.OutputMode)
        {
            Flush();
            s_Data.OutputMode = mode;
        }
    }

    void Renderer2D::EndScene()
    {
        Flush();

        // Stats'ı Renderer'a aktar
        auto& rendererStats = Renderer::GetStats();
        rendererStats.DrawCalls += s_Data.Stats.DrawCalls;
        rendererStats.Vertices += s_Data.Stats.Vertices;
        rendererStats.Triangles += s_Data.Stats.Triangles;
        rendererStats.QuadCount += s_Data.Stats.QuadCount;
        rendererStats.SpriteCount += s_Data.Stats.SpriteCount;
        s_Data.Stats = Statistics();

        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
    }

    static float GetTextureSlot(const std::shared_ptr<Texture2D>& texture)
    {
        if (!texture || texture == s_Data.WhiteTexture)
            return 0.0f;
        for (uint32_t i = 1; i < s_Data.TextureSlotIndex; ++i)
            if (s_Data.TextureSlots[i] == texture || (s_Data.TextureSlots[i] && s_Data.TextureSlots[i]->GetRendererID() == texture->GetRendererID()))
                return (float)i;
        if (s_Data.TextureSlotIndex >= Renderer2DData::MaxTextureSlots)
            Renderer2D::Flush();
        uint32_t slot = s_Data.TextureSlotIndex++;
        s_Data.TextureSlots[slot] = texture;
        return (float)slot;
    }

    void Renderer2D::DrawQuadCorners(const glm::vec3 positions[4], const glm::vec2 uvs[4], const std::shared_ptr<Texture2D>& texture, const glm::vec4& tintColor,
                                     float tilingFactor)
    {
        if (!s_Data.TextureShader)
            return;
        if (s_Data.IndexCount + 6 > Renderer2DData::MaxIndices)
            Flush();
        const float texIndex = GetTextureSlot(texture);
        for (int i = 0; i < 4; ++i)
            s_Data.Vertices.push_back({ positions[i], tintColor, uvs[i], texIndex, tilingFactor });
        s_Data.IndexCount += 6;
        s_Data.Stats.QuadCount++;
        s_Data.Stats.Vertices += 4;
        s_Data.Stats.Triangles += 2;
    }

    void Renderer2D::DrawRect(const glm::mat4& transform, const glm::vec2& mn, const glm::vec2& mx, const std::shared_ptr<Texture2D>& texture,
                              const glm::vec2& uvMin, const glm::vec2& uvMax, const glm::vec4& tintColor)
    {
        glm::vec3 p[4] = {
            glm::vec3(transform * glm::vec4(mn.x, mn.y, 0.0f, 1.0f)),
            glm::vec3(transform * glm::vec4(mx.x, mn.y, 0.0f, 1.0f)),
            glm::vec3(transform * glm::vec4(mx.x, mx.y, 0.0f, 1.0f)),
            glm::vec3(transform * glm::vec4(mn.x, mx.y, 0.0f, 1.0f)),
        };
        glm::vec2 uv[4] = { { uvMin.x, uvMin.y }, { uvMax.x, uvMin.y }, { uvMax.x, uvMax.y }, { uvMin.x, uvMax.y } };
        DrawQuadCorners(p, uv, texture, tintColor);
    }

    void Renderer2D::DrawQuad(const glm::mat4& transform, const glm::vec4& color)
    {
        DrawRect(transform, glm::vec2(-0.5f), glm::vec2(0.5f), s_Data.WhiteTexture, glm::vec2(0.0f), glm::vec2(1.0f), color);
    }

    void Renderer2D::DrawQuad(const glm::mat4& transform, const std::shared_ptr<Texture2D>& texture, const glm::vec2& uvMin, const glm::vec2& uvMax, const glm::vec4& tintColor)
    {
        DrawRect(transform, glm::vec2(-0.5f), glm::vec2(0.5f), texture, uvMin, uvMax, tintColor);
    }

    // ------------------------------------------------------------------ Sprite geometri çözümü

    namespace
    {
        struct SpriteGeometry
        {
            glm::vec4 RectPx;   // x, y, w, h
            glm::vec2 Pivot;
            glm::vec4 BorderPx;
            float PPU;
            glm::vec2 TexSize;
        };

        bool ResolveSprite(const SpriteRendererComponent& sprite, SpriteGeometry& g)
        {
            if (!sprite.Texture || sprite.Texture->GetWidth() == 0 || sprite.Texture->GetHeight() == 0)
                return false;
            g.TexSize = glm::vec2((float)sprite.Texture->GetWidth(), (float)sprite.Texture->GetHeight());
            g.RectPx = sprite.Rect.z > 0.0f && sprite.Rect.w > 0.0f ? sprite.Rect : glm::vec4(0.0f, 0.0f, g.TexSize.x, g.TexSize.y);
            g.Pivot = sprite.Pivot;
            g.BorderPx = sprite.Border;
            g.PPU = sprite.PixelsPerUnit > 0.0f ? sprite.PixelsPerUnit : 100.0f;
            if (!sprite.SpriteName.empty() && !sprite.Texture->GetPath().empty())
            {
                auto sheet = SpriteSheet::Get(sprite.Texture->GetPath());
                if (const SpriteDef* def = sheet ? sheet->Find(sprite.SpriteName) : nullptr)
                {
                    g.RectPx = def->Rect;
                    g.Pivot = def->Pivot;
                    g.BorderPx = def->Border;
                    g.PPU = sheet->PixelsPerUnit > 0.0f ? sheet->PixelsPerUnit : 100.0f;
                }
            }
            return g.RectPx.z > 0.0f && g.RectPx.w > 0.0f;
        }
    }

    void Renderer2D::ApplySpriteImport(SpriteRendererComponent& sprite)
    {
        if (!sprite.Texture || sprite.Texture->GetPath().empty())
            return;
        TextureImportSettings settings;
        if (TextureImportSettings::Load(sprite.Texture->GetPath(), settings) &&
            (settings.Type == TextureType::Sprite || settings.Type == TextureType::UI))
            sprite.PixelsPerUnit = settings.PixelsPerUnit;
        // Tek dilimli sayfa (ör. Create > 2D > Sprites > 9-Sliced): dilim seçilir, kenarlıkları Sliced / Tiled modda kullanılır
        if (auto sheet = SpriteSheet::Get(sprite.Texture->GetPath()); sheet && sheet->Sprites.size() == 1 && sprite.SpriteName.empty())
            sprite.SpriteName = sheet->Sprites.front().Name;
    }

    bool Renderer2D::GetSpriteLocalBounds(const SpriteRendererComponent& sprite, glm::vec2& outMin, glm::vec2& outMax)
    {
        SpriteGeometry g;
        if (!sprite.Texture)
        {
            // Dokusuz sprite: düz renkli birim kare (Sliced/Tiled: Size)
            const glm::vec2 size = sprite.DrawMode == SpriteDrawMode::Simple ? glm::vec2(1.0f) : glm::max(sprite.Size, glm::vec2(0.0001f));
            outMin = -sprite.Pivot * size;
            outMax = outMin + size;
            return true;
        }
        if (!ResolveSprite(sprite, g))
            return false;
        glm::vec2 size = sprite.DrawMode == SpriteDrawMode::Simple ? glm::vec2(g.RectPx.z, g.RectPx.w) / g.PPU : sprite.Size;
        outMin = -g.Pivot * size;
        outMax = outMin + size;
        return true;
    }

    void Renderer2D::DrawSprite(const glm::mat4& transform, const std::shared_ptr<Texture2D>& texture, float tilingFactor, const glm::vec4& tintColor, SpriteRendererComponent& sprite)
    {
        (void)tilingFactor;
        if (!texture)
        {
            // Dokusuz sprite düz renkli dikdörtgen olarak çizilir (sahne ve oyun görünümü aynı)
            glm::vec2 mn, mx;
            GetSpriteLocalBounds(sprite, mn, mx);
            DrawRect(transform, mn, mx, s_Data.WhiteTexture, glm::vec2(0.0f), glm::vec2(1.0f), tintColor);
            s_Data.Stats.SpriteCount++;
            return;
        }
        SpriteGeometry g;
        if (!ResolveSprite(sprite, g))
            return;

        // Sprite Atlas: görsel bir atlasa paketlenmişse atlas dokusu ve oradaki yeri kullanılır
        std::shared_ptr<Texture2D> drawTexture = texture;
        glm::vec2 atlasOffset(0.0f), uvSpace = g.TexSize;
        SpriteAtlases::Lookup atlas;
        if (texture == sprite.Texture && SpriteAtlases::Find(texture->GetPath(), atlas))
        {
            drawTexture = atlas.Texture;
            atlasOffset = atlas.Offset;
            uvSpace = atlas.Size;
        }
        const std::shared_ptr<Texture2D>& tex = drawTexture;

        // UV: piksel dikdörtgeni -> [0,1] (doku sol-alt köken; yükleyici dikey çevirir)
        glm::vec2 uvMin((atlasOffset.x + g.RectPx.x) / uvSpace.x, (atlasOffset.y + g.RectPx.y) / uvSpace.y);
        glm::vec2 uvMax((atlasOffset.x + g.RectPx.x + g.RectPx.z) / uvSpace.x, (atlasOffset.y + g.RectPx.y + g.RectPx.w) / uvSpace.y);
        if (sprite.FlipX) std::swap(uvMin.x, uvMax.x);
        if (sprite.FlipY) std::swap(uvMin.y, uvMax.y);

        const glm::vec2 nativeSize = glm::vec2(g.RectPx.z, g.RectPx.w) / g.PPU;
        s_Data.Stats.SpriteCount++;

        if (sprite.DrawMode == SpriteDrawMode::Simple)
        {
            glm::vec2 mn = -g.Pivot * nativeSize;
            DrawRect(transform, mn, mn + nativeSize, tex, uvMin, uvMax, tintColor);
            return;
        }

        const glm::vec2 size = glm::max(sprite.Size, glm::vec2(0.0001f));
        const glm::vec2 origin = -g.Pivot * size;

        if (sprite.DrawMode == SpriteDrawMode::Tiled)
        {
            // Dikdörtgeni doğal boyutta karolarla doldur (son karolar kırpılır)
            int nx = std::min(256, (int)std::ceil(size.x / nativeSize.x));
            int ny = std::min(256, (int)std::ceil(size.y / nativeSize.y));
            for (int y = 0; y < ny; ++y)
            {
                for (int x = 0; x < nx; ++x)
                {
                    glm::vec2 mn = origin + glm::vec2(x, y) * nativeSize;
                    glm::vec2 mx = glm::min(mn + nativeSize, origin + size);
                    glm::vec2 frac = (mx - mn) / nativeSize;
                    glm::vec2 uv1 = uvMin + (uvMax - uvMin) * frac;
                    DrawRect(transform, mn, mx, tex, uvMin, uv1, tintColor);
                }
            }
            return;
        }

        // Sliced (9-slice): köşeler sabit, kenarlar/merkez esner
        glm::vec4 b = g.BorderPx / g.PPU; // sol, alt, sağ, üst (dünya)
        // Kenarlıklar toplam boyutu aşarsa orantılı küçült
        float sx = (b.x + b.z) > size.x ? size.x / (b.x + b.z) : 1.0f;
        float sy = (b.y + b.w) > size.y ? size.y / (b.y + b.w) : 1.0f;
        b.x *= sx; b.z *= sx; b.y *= sy; b.w *= sy;
        const float xs[4] = { origin.x, origin.x + b.x, origin.x + size.x - b.z, origin.x + size.x };
        const float ys[4] = { origin.y, origin.y + b.y, origin.y + size.y - b.w, origin.y + size.y };
        const glm::vec2 duv = uvMax - uvMin;
        const float us[4] = { uvMin.x, uvMin.x + duv.x * (g.BorderPx.x / g.RectPx.z), uvMax.x - duv.x * (g.BorderPx.z / g.RectPx.z), uvMax.x };
        const float vs[4] = { uvMin.y, uvMin.y + duv.y * (g.BorderPx.y / g.RectPx.w), uvMax.y - duv.y * (g.BorderPx.w / g.RectPx.w), uvMax.y };
        for (int y = 0; y < 3; ++y)
            for (int x = 0; x < 3; ++x)
                if (xs[x + 1] > xs[x] && ys[y + 1] > ys[y])
                    DrawRect(transform, glm::vec2(xs[x], ys[y]), glm::vec2(xs[x + 1], ys[y + 1]), tex,
                             glm::vec2(us[x], vs[y]), glm::vec2(us[x + 1], vs[y + 1]), tintColor);
    }

    void Renderer2D::DrawSprite(const glm::mat4& transform, const std::shared_ptr<Texture2D>& texture, float tilingFactor, const glm::vec4& tintColor, ImageComponent& image)
    {
        (void)image;
        if (!texture)
            return;
        // PNG'nin piksel boyutlarını 100'e bölerek dünya birimine çevir
        float halfWidth = (float)texture->GetWidth() / 200.0f;
        float halfHeight = (float)texture->GetHeight() / 200.0f;
        glm::vec2 uvMax(std::max(1.0f, tilingFactor));
        DrawRect(transform, glm::vec2(-halfWidth, -halfHeight), glm::vec2(halfWidth, halfHeight), texture, glm::vec2(0.0f), uvMax, tintColor);
        s_Data.Stats.SpriteCount++;
    }

    void Renderer2D::InitializeSpriteResources(SpriteRendererComponent&) {}
    void Renderer2D::InitializeImageResources(ImageComponent&) {}

    void Renderer2D::ResetStats()
    {
        s_Data.Stats = Statistics();
    }

    Renderer2D::Statistics Renderer2D::GetStats()
    {
        return s_Data.Stats;
    }

    std::shared_ptr<Texture2D> Renderer2D::GetWhiteTexture()
    {
        return s_Data.WhiteTexture;
    }

    std::shared_ptr<Texture2D> Renderer2D::GetCircleTexture()
    {
        // TODO: Circle texture oluştur
        return s_Data.WhiteTexture;
    }

    void Renderer2D::DrawText(const std::string& text, const glm::vec2& position, float scale, const glm::vec4& color)
    {
        DrawText(text, glm::vec3(position.x, position.y, 0.0f), scale, color);
    }

    void Renderer2D::DrawText(const std::string& text, const glm::vec3& position, float scale, const glm::vec4& color)
    {
        glm::mat4 transform = glm::translate(glm::mat4(1.0f), position);
        DrawText(text, transform, scale, color);
    }

    void Renderer2D::DrawText(const std::string& text, const glm::mat4& transform, float scale, const glm::vec4& color)
    {
        // Text rendering için ayrı bir sistem gerekiyor
        // Mevcut Renderer2D yapısı sprite-based, text için uygun değil
        // TODO: TextRenderer sistemi kur
        (void)text;
        (void)transform;
        (void)scale;
        (void)color;
    }

    void Renderer2D::DrawSprite9Slice(const glm::mat4& transform, const std::shared_ptr<Texture2D>& texture, float borderSize, const glm::vec4& tintColor, ImageComponent& image)
    {
        (void)image;
        if (!texture)
            return;
        const float texW = (float)texture->GetWidth(), texH = (float)texture->GetHeight();
        const float halfW = texW / 200.0f, halfH = texH / 200.0f;
        const float bw = std::min(borderSize / 100.0f, halfW), bh = std::min(borderSize / 100.0f, halfH);
        const float bu = std::min(borderSize / texW, 0.5f), bv = std::min(borderSize / texH, 0.5f);
        const float xs[4] = { -halfW, -halfW + bw, halfW - bw, halfW };
        const float ys[4] = { -halfH, -halfH + bh, halfH - bh, halfH };
        const float us[4] = { 0.0f, bu, 1.0f - bu, 1.0f };
        const float vs[4] = { 0.0f, bv, 1.0f - bv, 1.0f };
        for (int y = 0; y < 3; ++y)
            for (int x = 0; x < 3; ++x)
                if (xs[x + 1] > xs[x] && ys[y + 1] > ys[y])
                    DrawRect(transform, glm::vec2(xs[x], ys[y]), glm::vec2(xs[x + 1], ys[y + 1]), texture,
                             glm::vec2(us[x], vs[y]), glm::vec2(us[x + 1], vs[y + 1]), tintColor);
        s_Data.Stats.SpriteCount++;
    }
}
