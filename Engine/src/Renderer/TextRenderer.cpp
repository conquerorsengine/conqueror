#include "TextRenderer.h"
#include "Renderer.h"
#include "RHI/Shader.h"
#include "RHI/VertexArray.h"
#include "RHI/RenderCommand.h"

#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <vector>

namespace Conqueror
{
    struct TextVertex
    {
        glm::vec3 Position;
        glm::vec4 Color;
        glm::vec2 TexCoord;
    };

    struct TextRendererData
    {
        std::shared_ptr<Shader> TextShader;
        std::shared_ptr<VertexArray> QuadVA;
        std::shared_ptr<VertexBuffer> QuadVB;
        std::shared_ptr<Font> DefaultFont;

        // Toplu çizim: bir metnin tüm glifleri (atlas) tek köşe tamponunda, tek çizim çağrısında
        static constexpr uint32_t MaxGlyphs = 2048;
        std::shared_ptr<VertexArray> BatchVA;
        std::shared_ptr<VertexBuffer> BatchVB;
        std::vector<float> BatchVertices;   // glif başına 4 köşe: konum (3) + uv (2)
        
        glm::mat4 ViewProjection{ 1.0f };
    };

    static TextRendererData s_Data;

    static bool s_DepthTestEnabled = false;

    void TextRenderer::Init()
    {
        // Quad vertex array (bottom-left origin for proper baseline)
        s_Data.QuadVA = VertexArray::Create();
        
        float quadVertices[] = {
            0.0f, 1.0f, 0.0f,  0.0f, 0.0f,  // Top-left
            1.0f, 1.0f, 0.0f,  1.0f, 0.0f,  // Top-right
            1.0f, 0.0f, 0.0f,  1.0f, 1.0f,  // Bottom-right
            0.0f, 0.0f, 0.0f,  0.0f, 1.0f   // Bottom-left
        };
        
        s_Data.QuadVB = VertexBuffer::Create(quadVertices, sizeof(quadVertices));
        s_Data.QuadVB->SetLayout({
            { ShaderDataType::Float3, "a_Position" },
            { ShaderDataType::Float2, "a_TexCoord" }
        });
        s_Data.QuadVA->AddVertexBuffer(s_Data.QuadVB);
        
        uint32_t quadIndices[] = { 0, 1, 2, 2, 3, 0 };
        std::shared_ptr<IndexBuffer> quadIB = IndexBuffer::Create(quadIndices, 6);
        s_Data.QuadVA->SetIndexBuffer(quadIB);
        
        // Toplu glif tamponu (dörtgen indeksleri sabit)
        s_Data.BatchVA = VertexArray::Create();
        s_Data.BatchVB = VertexBuffer::Create(TextRendererData::MaxGlyphs * 4 * 5 * (uint32_t)sizeof(float));
        s_Data.BatchVB->SetLayout({
            { ShaderDataType::Float3, "a_Position" },
            { ShaderDataType::Float2, "a_TexCoord" }
        });
        s_Data.BatchVA->AddVertexBuffer(s_Data.BatchVB);
        {
            std::vector<uint32_t> indices(TextRendererData::MaxGlyphs * 6);
            for (uint32_t g = 0; g < TextRendererData::MaxGlyphs; ++g)
            {
                const uint32_t v = g * 4;
                const uint32_t quad[6] = { v + 0, v + 1, v + 2, v + 2, v + 3, v + 0 };
                std::copy(quad, quad + 6, indices.begin() + g * 6);
            }
            s_Data.BatchVA->SetIndexBuffer(IndexBuffer::Create(indices.data(), (uint32_t)indices.size()));
        }
        s_Data.BatchVertices.reserve(TextRendererData::MaxGlyphs * 4 * 5);

        // Text shader
        s_Data.TextShader = Shader::Create("Engine/src/Shaders/2D/Text.cqsh");
        
        // Default font
        s_Data.DefaultFont = Font::GetDefault();
        
        CQ_CORE_INFO("TextRenderer initialized");
    }

    void TextRenderer::Shutdown()
    {
        // GL nesneleri bağlam kapanmadan bırakılır
        s_Data = TextRendererData{};
        Font::ReleaseDefault();
    }

    void TextRenderer::BeginScene(const EditorCamera& camera, bool enableDepthTest)
    {
        s_DepthTestEnabled = enableDepthTest;
        s_Data.ViewProjection = camera.GetViewProjection();
    }

    void TextRenderer::BeginScene(const SceneCamera& camera, const glm::mat4& transform, bool enableDepthTest)
    {
        s_DepthTestEnabled = enableDepthTest;
        s_Data.ViewProjection = camera.GetProjection() * glm::inverse(transform);
    }

    void TextRenderer::BeginScene(const glm::mat4& viewProjection, bool enableDepthTest)
    {
        s_DepthTestEnabled = enableDepthTest;
        s_Data.ViewProjection = viewProjection;
    }



    void TextRenderer::EndScene()
    {
    }

    void TextRenderer::DrawText(const std::string& text, const glm::vec3& position, float scale, const glm::vec4& color)
    {
        glm::mat4 transform = glm::translate(glm::mat4(1.0f), position);
        DrawText(text, transform, scale, color);
    }

    void TextRenderer::DrawText(const std::string& text, const glm::mat4& transform, float scale, const glm::vec4& color, int alignment)
    {
        if (!s_Data.DefaultFont)
        {
            return;
        }

        // OpenGL state setup for text rendering
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        
        if (s_DepthTestEnabled)
        {
            glEnable(GL_DEPTH_TEST);
            glDepthFunc(GL_LEQUAL);
        }
        else
        {
            glDisable(GL_DEPTH_TEST);
        }

        // UTF-8 to wchar_t conversion
        std::wstring wtext;
        size_t i = 0;
        while (i < text.length())
        {
            unsigned char c = (unsigned char)text[i];
            wchar_t wc = 0;

            if (c < 0x80) // ASCII (1 byte)
            {
                wc = c;
                i++;
            }
            else if ((c & 0xE0) == 0xC0) // 2 bytes (110xxxxx 10xxxxxx)
            {
                if (i + 1 < text.length())
                {
                    unsigned char c1 = (unsigned char)text[i];
                    unsigned char c2 = (unsigned char)text[i + 1];
                    wc = ((c1 & 0x1F) << 6) | (c2 & 0x3F);
                    i += 2;
                }
                else break;
            }
            else if ((c & 0xF0) == 0xE0) // 3 bytes (1110xxxx 10xxxxxx 10xxxxxx)
            {
                if (i + 2 < text.length())
                {
                    unsigned char c1 = (unsigned char)text[i];
                    unsigned char c2 = (unsigned char)text[i + 1];
                    unsigned char c3 = (unsigned char)text[i + 2];
                    wc = ((c1 & 0x0F) << 12) | ((c2 & 0x3F) << 6) | (c3 & 0x3F);
                    i += 3;
                }
                else break;
            }
            else if ((c & 0xF8) == 0xF0) // 4 bytes (11110xxx 10xxxxxx 10xxxxxx 10xxxxxx)
            {
                if (i + 3 < text.length())
                {
                    // 4-byte UTF-8 için surrogate pair gerekir, şimdilik skip
                    i += 4;
                    continue;
                }
                else break;
            }
            else
            {
                i++;
                continue;
            }

            wtext += wc;
        }

        // Hizalama: satır genişliği (ilerleme toplamı) ile satır başı kaydırılır
        auto lineStart = [&](size_t from) -> float {
            if (alignment <= 0)
                return 0.0f;
            float width = 0.0f;
            for (size_t k = from; k < wtext.size() && wtext[k] != L'\n'; ++k)
                width += (s_Data.DefaultFont->GetGlyph(wtext[k]).Advance >> 6) * scale;
            return alignment == 1 ? -width * 0.5f : -width;
        };
        float x = lineStart(0);
        float y = 0.0f;
        float lineHeight = 48.0f * scale; // Font size * scale

        s_Data.TextShader->Bind();
        s_Data.TextShader->SetFloat4("u_Color", color);
        // Metnin dönüşümü bir kez: glif köşeleri metnin yerel düzleminde
        s_Data.TextShader->SetMat4("u_ViewProjection", s_Data.ViewProjection * transform);
        s_Data.TextShader->SetInt("u_Texture", 0);

        // Aynı dokudaki glifler tek çağrıda (atlas: genelde metnin tamamı)
        std::shared_ptr<Texture2D> batchTexture;
        auto flush = [&]() {
            if (s_Data.BatchVertices.empty() || !batchTexture)
                return;
            const uint32_t glyphs = (uint32_t)(s_Data.BatchVertices.size() / 20);
            s_Data.BatchVB->SetData(s_Data.BatchVertices.data(), (uint32_t)(s_Data.BatchVertices.size() * sizeof(float)));
            batchTexture->Bind(0);
            RenderCommand::DrawIndexed(s_Data.BatchVA, glyphs * 6);
            Renderer::GetStats().DrawCalls++;
            s_Data.BatchVertices.clear();
        };

        for (size_t ci = 0; ci < wtext.size(); ++ci)
        {
            const wchar_t wc = wtext[ci];
            // Newline desteği
            if (wc == L'\n')
            {
                x = lineStart(ci + 1);
                y -= lineHeight; // Aşağı git (negatif yön)
                continue;
            }

            const Glyph& glyph = s_Data.DefaultFont->GetGlyph(wc);
            if (glyph.Texture && glyph.Size.x > 0 && glyph.Size.y > 0)
            {
                if (glyph.Texture != batchTexture || s_Data.BatchVertices.size() >= (size_t)TextRendererData::MaxGlyphs * 20)
                {
                    flush();
                    batchTexture = glyph.Texture;
                }
                const float x0 = x + glyph.Bearing.x * scale;
                const float y0 = y - (glyph.Size.y - glyph.Bearing.y) * scale;
                const float x1 = x0 + glyph.Size.x * scale;
                const float y1 = y0 + glyph.Size.y * scale;
                const glm::vec2 a = glyph.UV0, b = glyph.UV1;
                const float quad[20] = {
                    x0, y1, 0.0f, a.x, a.y,     // sol üst
                    x1, y1, 0.0f, b.x, a.y,     // sağ üst
                    x1, y0, 0.0f, b.x, b.y,     // sağ alt
                    x0, y0, 0.0f, a.x, b.y      // sol alt
                };
                s_Data.BatchVertices.insert(s_Data.BatchVertices.end(), quad, quad + 20);
            }
            x += (glyph.Advance >> 6) * scale;
        }
        flush();

        // Stats güncelle
        Renderer::GetStats().TextCount++;

        // Restore OpenGL state (depth test zaten doğru durumda)
    }

    void TextRenderer::FlushText()
    {
    }
}
