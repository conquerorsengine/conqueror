#include "Renderer2D.h"
#include "Renderer.h"
#include "RHI/VertexArray.h"
#include "RHI/Shader.h"
#include "RHI/RenderCommand.h"
#include "Scene/Components.h"
#include "Font.h"

#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>

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

    struct Renderer2DData
    {
        std::shared_ptr<Shader> TextureShader;
        std::shared_ptr<Texture2D> WhiteTexture;
        glm::vec4 QuadVertexPositions[4];
        Renderer2D::Statistics Stats;
    };

    static Renderer2DData s_Data;

    void Renderer2D::Init()
    {
        // White texture
        s_Data.WhiteTexture = Texture2D::Create(1, 1);
        uint32_t whiteTextureData = 0xffffffff;
        s_Data.WhiteTexture->SetData(&whiteTextureData, sizeof(uint32_t));

        // Shader
        s_Data.TextureShader = Shader::Create("Engine/src/Shaders/2D/Texture.cqsh");
        s_Data.TextureShader->Bind();
        int32_t sampler = 0;
        s_Data.TextureShader->SetIntArray("u_Textures", &sampler, 1);

        // Quad vertex positions
        s_Data.QuadVertexPositions[0] = { -0.5f, -0.5f, 0.0f, 1.0f };
        s_Data.QuadVertexPositions[1] = {  0.5f, -0.5f, 0.0f, 1.0f };
        s_Data.QuadVertexPositions[2] = {  0.5f,  0.5f, 0.0f, 1.0f };
        s_Data.QuadVertexPositions[3] = { -0.5f,  0.5f, 0.0f, 1.0f };
    }

    void Renderer2D::Shutdown()
    {
    }

    void Renderer2D::BeginScene(const EditorCamera& camera, bool enableDepthTest, bool enableDepthWrite)
    {
        ResetStats();
        
        if (enableDepthTest)
        {
            glEnable(GL_DEPTH_TEST);
            glDepthFunc(GL_LEQUAL);
        }
        else
        {
            glDisable(GL_DEPTH_TEST);
        }
        
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        
        s_Data.TextureShader->Bind();
        s_Data.TextureShader->SetMat4("u_ViewProjection", camera.GetViewProjection());
    }

    void Renderer2D::BeginScene(const SceneCamera& camera, const glm::mat4& transform, bool enableDepthTest, bool enableDepthWrite)
    {
        ResetStats();
        
        if (enableDepthTest)
        {
            glEnable(GL_DEPTH_TEST);
            glDepthFunc(GL_LEQUAL);
        }
        else
        {
            glDisable(GL_DEPTH_TEST);
        }
        
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        
        glm::mat4 viewProj = camera.GetProjection() * glm::inverse(transform);
        
        s_Data.TextureShader->Bind();
        s_Data.TextureShader->SetMat4("u_ViewProjection", viewProj);
    }

    void Renderer2D::BeginScene(const glm::mat4& viewProjection, bool enableDepthTest)
    {
        // Depth test kapalı (UI her zaman en önde)
        if (enableDepthTest)
        {
            glEnable(GL_DEPTH_TEST);
            glDepthFunc(GL_LEQUAL);
        }
        else
        {
            glDisable(GL_DEPTH_TEST);
        }
        
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        
        s_Data.TextureShader->Bind();
        s_Data.TextureShader->SetMat4("u_ViewProjection", viewProjection);
    }



    void Renderer2D::EndScene()
    {
        // Stats'ı Renderer'a aktar
        auto& rendererStats = Renderer::GetStats();
        rendererStats.DrawCalls += s_Data.Stats.DrawCalls;
        rendererStats.Vertices += s_Data.Stats.Vertices;
        rendererStats.Triangles += s_Data.Stats.Triangles;
        rendererStats.QuadCount += s_Data.Stats.QuadCount;
        rendererStats.SpriteCount += s_Data.Stats.SpriteCount;
        
        glEnable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);
    }

    void Renderer2D::Flush()
    {
        // Artık kullanılmıyor - her sprite kendi render'ını yapıyor
    }

    void Renderer2D::InitializeSpriteResources(SpriteRendererComponent& sprite)
    {
        if (sprite.RenderResourcesInitialized)
            return;

        // Vertex Array oluştur
        sprite.VertexArray = VertexArray::Create();

        // Vertex Buffer oluştur
        sprite.VertexBuffer = VertexBuffer::Create(4 * sizeof(QuadVertex));
        sprite.VertexBuffer->SetLayout({
            { ShaderDataType::Float3, "a_Position"     },
            { ShaderDataType::Float4, "a_Color"        },
            { ShaderDataType::Float2, "a_TexCoord"     },
            { ShaderDataType::Float,  "a_TexIndex"     },
            { ShaderDataType::Float,  "a_TilingFactor" }
        });
        sprite.VertexArray->AddVertexBuffer(sprite.VertexBuffer);

        // Index Buffer oluştur
        uint32_t indices[6] = { 0, 1, 2, 2, 3, 0 };
        sprite.IndexBuffer = IndexBuffer::Create(indices, 6);
        sprite.VertexArray->SetIndexBuffer(sprite.IndexBuffer);

        sprite.RenderResourcesInitialized = true;
    }

    void Renderer2D::InitializeImageResources(ImageComponent& image)
    {
        if (image.RenderResourcesInitialized)
            return;

        // Vertex Array oluştur
        image.VertexArray = VertexArray::Create();

        // Vertex Buffer oluştur
        image.VertexBuffer = VertexBuffer::Create(4 * sizeof(QuadVertex));
        image.VertexBuffer->SetLayout({
            { ShaderDataType::Float3, "a_Position"     },
            { ShaderDataType::Float4, "a_Color"        },
            { ShaderDataType::Float2, "a_TexCoord"     },
            { ShaderDataType::Float,  "a_TexIndex"     },
            { ShaderDataType::Float,  "a_TilingFactor" }
        });
        image.VertexArray->AddVertexBuffer(image.VertexBuffer);

        // Index Buffer oluştur
        uint32_t indices[6] = { 0, 1, 2, 2, 3, 0 };
        image.IndexBuffer = IndexBuffer::Create(indices, 6);
        image.VertexArray->SetIndexBuffer(image.IndexBuffer);

        image.RenderResourcesInitialized = true;
    }

    void Renderer2D::DrawSprite(const glm::mat4& transform, const std::shared_ptr<Texture2D>& texture, float tilingFactor, const glm::vec4& tintColor, SpriteRendererComponent& sprite)
    {
        InitializeSpriteResources(sprite);

        // PNG'nin piksel boyutlarını 100'e bölerek dünya birimine çevir
        float halfWidth = (float)texture->GetWidth() / 200.0f;  // 2'ye bölüyoruz çünkü yarısı lazım
        float halfHeight = (float)texture->GetHeight() / 200.0f;
        
        glm::vec4 vertexPositions[4] = {
            { -halfWidth, -halfHeight, 0.0f, 1.0f },
            {  halfWidth, -halfHeight, 0.0f, 1.0f },
            {  halfWidth,  halfHeight, 0.0f, 1.0f },
            { -halfWidth,  halfHeight, 0.0f, 1.0f }
        };

        constexpr glm::vec2 textureCoords[] = { { 0.0f, 0.0f }, { 1.0f, 0.0f }, { 1.0f, 1.0f }, { 0.0f, 1.0f } };
        QuadVertex vertices[4];

        for (size_t i = 0; i < 4; i++)
        {
            vertices[i].Position = transform * vertexPositions[i];
            vertices[i].Color = tintColor;
            vertices[i].TexCoord = textureCoords[i];
            vertices[i].TexIndex = 0.0f;
            vertices[i].TilingFactor = tilingFactor;
        }

        sprite.VertexBuffer->SetData(vertices, sizeof(vertices));
        texture->Bind(0);
        s_Data.TextureShader->Bind();
        RenderCommand::DrawIndexed(sprite.VertexArray, 6);

        s_Data.Stats.DrawCalls++;
        s_Data.Stats.SpriteCount++;
        s_Data.Stats.Vertices += 4;
        s_Data.Stats.Triangles += 2;
        s_Data.Stats.QuadCount++;
    }

    void Renderer2D::DrawSprite(const glm::mat4& transform, const std::shared_ptr<Texture2D>& texture, float tilingFactor, const glm::vec4& tintColor, ImageComponent& image)
    {
        InitializeImageResources(image);

        // PNG'nin piksel boyutlarını 100'e bölerek dünya birimine çevir
        float halfWidth = (float)texture->GetWidth() / 200.0f;
        float halfHeight = (float)texture->GetHeight() / 200.0f;
        
        glm::vec4 vertexPositions[4] = {
            { -halfWidth, -halfHeight, 0.0f, 1.0f },
            {  halfWidth, -halfHeight, 0.0f, 1.0f },
            {  halfWidth,  halfHeight, 0.0f, 1.0f },
            { -halfWidth,  halfHeight, 0.0f, 1.0f }
        };

        constexpr glm::vec2 textureCoords[] = { { 0.0f, 0.0f }, { 1.0f, 0.0f }, { 1.0f, 1.0f }, { 0.0f, 1.0f } };
        QuadVertex vertices[4];

        for (size_t i = 0; i < 4; i++)
        {
            vertices[i].Position = transform * vertexPositions[i];
            vertices[i].Color = tintColor;
            vertices[i].TexCoord = textureCoords[i];
            vertices[i].TexIndex = 0.0f;
            vertices[i].TilingFactor = tilingFactor;
        }

        image.VertexBuffer->SetData(vertices, sizeof(vertices));
        texture->Bind(0);
        s_Data.TextureShader->Bind();
        RenderCommand::DrawIndexed(image.VertexArray, 6);

        s_Data.Stats.DrawCalls++;
        s_Data.Stats.SpriteCount++;
        s_Data.Stats.Vertices += 4;
        s_Data.Stats.Triangles += 2;
        s_Data.Stats.QuadCount++;
    }

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
        InitializeImageResources(image);

        // Texture boyutları
        float texWidth = (float)texture->GetWidth();
        float texHeight = (float)texture->GetHeight();
        
        // Dünya birimlerine çevir (normal rendering ile aynı)
        float halfWidth = texWidth / 200.0f;
        float halfHeight = texHeight / 200.0f;
        float targetWidth = halfWidth * 2.0f;
        float targetHeight = halfHeight * 2.0f;
        
        // Border boyutu (dünya biriminde)
        float borderWorld = borderSize / 100.0f;
        
        // 9 bölge için boyutlar
        float leftWidth = borderWorld;
        float rightWidth = borderWorld;
        float topHeight = borderWorld;
        float bottomHeight = borderWorld;
        
        // Texture koordinatları (normalized)
        float borderU = borderSize / texWidth;
        float borderV = borderSize / texHeight;
        
        // 9 slice için vertex ve UV tanımları
        struct SliceQuad
        {
            glm::vec2 posMin, posMax;
            glm::vec2 uvMin, uvMax;
        };
        
        SliceQuad slices[9];
        
        // Pozisyon offset'leri (sol-alt köşeden başla, normal rendering ile aynı)
        float halfW = targetWidth * 0.5f;
        float halfH = targetHeight * 0.5f;
        
        // Sol-alt köşe
        slices[0] = { 
            {-halfW, -halfH}, 
            {-halfW + leftWidth, -halfH + bottomHeight},
            {0.0f, 0.0f}, 
            {borderU, borderV} 
        };
        
        // Alt-orta
        slices[1] = { 
            {-halfW + leftWidth, -halfH}, 
            {halfW - rightWidth, -halfH + bottomHeight},
            {borderU, 0.0f}, 
            {1.0f - borderU, borderV} 
        };
        
        // Sağ-alt köşe
        slices[2] = { 
            {halfW - rightWidth, -halfH}, 
            {halfW, -halfH + bottomHeight},
            {1.0f - borderU, 0.0f}, 
            {1.0f, borderV} 
        };
        
        // Sol-orta
        slices[3] = { 
            {-halfW, -halfH + bottomHeight}, 
            {-halfW + leftWidth, halfH - topHeight},
            {0.0f, borderV}, 
            {borderU, 1.0f - borderV} 
        };
        
        // Merkez
        slices[4] = { 
            {-halfW + leftWidth, -halfH + bottomHeight}, 
            {halfW - rightWidth, halfH - topHeight},
            {borderU, borderV}, 
            {1.0f - borderU, 1.0f - borderV} 
        };
        
        // Sağ-orta
        slices[5] = { 
            {halfW - rightWidth, -halfH + bottomHeight}, 
            {halfW, halfH - topHeight},
            {1.0f - borderU, borderV}, 
            {1.0f, 1.0f - borderV} 
        };
        
        // Sol-üst köşe
        slices[6] = { 
            {-halfW, halfH - topHeight}, 
            {-halfW + leftWidth, halfH},
            {0.0f, 1.0f - borderV}, 
            {borderU, 1.0f} 
        };
        
        // Üst-orta
        slices[7] = { 
            {-halfW + leftWidth, halfH - topHeight}, 
            {halfW - rightWidth, halfH},
            {borderU, 1.0f - borderV}, 
            {1.0f - borderU, 1.0f} 
        };
        
        // Sağ-üst köşe
        slices[8] = { 
            {halfW - rightWidth, halfH - topHeight}, 
            {halfW, halfH},
            {1.0f - borderU, 1.0f - borderV}, 
            {1.0f, 1.0f} 
        };
        
        // Her slice için render
        texture->Bind(0);
        s_Data.TextureShader->Bind();
        
        for (int i = 0; i < 9; i++)
        {
            const auto& slice = slices[i];
            
            glm::vec4 vertexPositions[4] = {
                { slice.posMin.x, slice.posMin.y, 0.0f, 1.0f },
                { slice.posMax.x, slice.posMin.y, 0.0f, 1.0f },
                { slice.posMax.x, slice.posMax.y, 0.0f, 1.0f },
                { slice.posMin.x, slice.posMax.y, 0.0f, 1.0f }
            };
            
            glm::vec2 textureCoords[4] = {
                { slice.uvMin.x, slice.uvMin.y },
                { slice.uvMax.x, slice.uvMin.y },
                { slice.uvMax.x, slice.uvMax.y },
                { slice.uvMin.x, slice.uvMax.y }
            };
            
            QuadVertex vertices[4];
            for (size_t j = 0; j < 4; j++)
            {
                vertices[j].Position = transform * vertexPositions[j];
                vertices[j].Color = tintColor;
                vertices[j].TexCoord = textureCoords[j];
                vertices[j].TexIndex = 0.0f;
                vertices[j].TilingFactor = 1.0f;
            }
            
            image.VertexBuffer->SetData(vertices, sizeof(vertices));
            RenderCommand::DrawIndexed(image.VertexArray, 6);
            
            s_Data.Stats.DrawCalls++;
            s_Data.Stats.Vertices += 4;
            s_Data.Stats.Triangles += 2;
        }
        
        s_Data.Stats.SpriteCount++;
    }
}
