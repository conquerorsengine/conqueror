#include "TextRenderer.h"
#include "Renderer.h"
#include "RHI/Shader.h"
#include "RHI/VertexArray.h"
#include "RHI/RenderCommand.h"

#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>

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
        
        glm::mat4 ViewProjection;
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
        
        // Text shader
        s_Data.TextShader = Shader::Create("Engine/src/Shaders/2D/Text.cqsh");
        
        // Default font
        s_Data.DefaultFont = Font::GetDefault();
        
        CQ_CORE_INFO("TextRenderer initialized");
    }

    void TextRenderer::Shutdown()
    {
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

    void TextRenderer::EndScene()
    {
    }

    void TextRenderer::DrawText(const std::string& text, const glm::vec3& position, float scale, const glm::vec4& color)
    {
        glm::mat4 transform = glm::translate(glm::mat4(1.0f), position);
        DrawText(text, transform, scale, color);
    }

    void TextRenderer::DrawText(const std::string& text, const glm::mat4& transform, float scale, const glm::vec4& color)
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

        s_Data.TextShader->Bind();
        s_Data.TextShader->SetFloat4("u_Color", color);
        s_Data.QuadVA->Bind();

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

        float x = 0.0f;
        float y = 0.0f;
        float lineHeight = 48.0f * scale; // Font size * scale
        
        for (wchar_t wc : wtext)
        {
            // Newline desteği
            if (wc == L'\n')
            {
                x = 0.0f;
                y -= lineHeight; // Aşağı git (negatif yön)
                continue;
            }

            const Glyph& glyph = s_Data.DefaultFont->GetGlyph(wc);
            
            if (!glyph.Texture)
                continue;

            float xpos = x + glyph.Bearing.x * scale;
            float ypos = y - (glyph.Size.y - glyph.Bearing.y) * scale;
            
            float w = glyph.Size.x * scale;
            float h = glyph.Size.y * scale;
            
            glm::mat4 model = transform 
                * glm::translate(glm::mat4(1.0f), glm::vec3(xpos, ypos, 0.0f))
                * glm::scale(glm::mat4(1.0f), glm::vec3(w, h, 1.0f));
            
            s_Data.TextShader->SetMat4("u_ViewProjection", s_Data.ViewProjection * model);
            
            glyph.Texture->Bind(0);
            
            RenderCommand::DrawIndexed(s_Data.QuadVA, 6);
            
            x += (glyph.Advance >> 6) * scale;
        }

        // Stats güncelle
        Renderer::GetStats().DrawCalls++;
        Renderer::GetStats().TextCount++;

        // Restore OpenGL state (depth test zaten doğru durumda)
    }

    void TextRenderer::FlushText()
    {
    }
}
