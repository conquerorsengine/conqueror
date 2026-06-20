#include "PostProcess.h"
#include "Core/Tiering/QualitySettings.h"
#include "RHI/RenderCommand.h"
#include "RHI/Texture.h"
#include "Scene/Scene.h"
#include <glad/glad.h>

namespace Conqueror
{
    std::shared_ptr<Framebuffer> PostProcess::s_SceneFramebuffer = nullptr;
    std::shared_ptr<Framebuffer> PostProcess::s_ResolvedFramebuffer = nullptr;
    std::shared_ptr<Framebuffer> PostProcess::s_IntermediateFramebuffer = nullptr;
    std::shared_ptr<Framebuffer> PostProcess::s_SMAAEdgeFramebuffer = nullptr;
    std::shared_ptr<Shader> PostProcess::s_FXAAShader = nullptr;
    std::shared_ptr<Shader> PostProcess::s_SMAAEdgeShader = nullptr;
    std::shared_ptr<Shader> PostProcess::s_SMAABlendShader = nullptr;
    std::shared_ptr<Shader> PostProcess::s_HaloFlareShader = nullptr;
    std::shared_ptr<VertexArray> PostProcess::s_QuadVA = nullptr;
    std::shared_ptr<VertexBuffer> PostProcess::s_QuadVB = nullptr;
    std::shared_ptr<IndexBuffer> PostProcess::s_QuadIB = nullptr;
    uint32_t PostProcess::s_Width = 0;
    uint32_t PostProcess::s_Height = 0;

    void PostProcess::Init()
    {
        // Quad mesh
        CreateQuad();

        // FXAA shader
        s_FXAAShader = Shader::Create("Engine/src/Shaders/PostProcess/FXAA.cqsh");
        
        // SMAA shaders
        s_SMAAEdgeShader = Shader::Create("Engine/src/Shaders/PostProcess/SMAA_Edge.cqsh");
        s_SMAABlendShader = Shader::Create("Engine/src/Shaders/PostProcess/SMAA_Blend.cqsh");
        
        // Halo/Flare shader
        s_HaloFlareShader = Shader::Create("Engine/src/Shaders/PostProcess/HaloFlare.cqsh");
    }

    void PostProcess::Shutdown()
    {
        s_SceneFramebuffer.reset();
        s_ResolvedFramebuffer.reset();
        s_IntermediateFramebuffer.reset();
        s_SMAAEdgeFramebuffer.reset();
        s_FXAAShader.reset();
        s_SMAAEdgeShader.reset();
        s_SMAABlendShader.reset();
        s_HaloFlareShader.reset();
        s_QuadVA.reset();
        s_QuadVB.reset();
        s_QuadIB.reset();
    }

    void PostProcess::BeginScene(uint32_t width, uint32_t height)
    {
        uint32_t msaaSamples = QualitySettings::GetPreset().AntiAliasing;
        if (msaaSamples < 1) msaaSamples = 1;

        if (!s_SceneFramebuffer || s_Width != width || s_Height != height || s_SceneFramebuffer->GetSpecification().Samples != msaaSamples)
        {
            s_Width = width;
            s_Height = height;

            FramebufferSpecification spec;
            spec.Width = width;
            spec.Height = height;
            spec.Attachments = { FramebufferTextureFormat::RGBA8, FramebufferTextureFormat::Depth };
            spec.Samples = msaaSamples;

            s_SceneFramebuffer = Framebuffer::Create(spec);

            FramebufferSpecification nonMSAA = spec;
            nonMSAA.Samples = 1;

            if (msaaSamples > 1)
                s_ResolvedFramebuffer = Framebuffer::Create(nonMSAA);

            s_IntermediateFramebuffer = Framebuffer::Create(nonMSAA);
            s_SMAAEdgeFramebuffer = Framebuffer::Create(nonMSAA);
        }

        s_SceneFramebuffer->Bind();
    }

    void PostProcess::EndScene()
    {
        s_SceneFramebuffer->Unbind();

        if (s_SceneFramebuffer->GetSpecification().Samples > 1 && s_ResolvedFramebuffer)
        {
            glBindFramebuffer(GL_READ_FRAMEBUFFER, s_SceneFramebuffer->GetRendererID());
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, s_ResolvedFramebuffer->GetRendererID());
            glBlitFramebuffer(0, 0, s_Width, s_Height, 0, 0, s_Width, s_Height, GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT, GL_NEAREST);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
        }
    }

    void PostProcess::ApplyFXAA()
    {
        if (!s_FXAAShader || !s_SceneFramebuffer)
            return;

        s_FXAAShader->Bind();
        s_FXAAShader->SetInt("u_ScreenTexture", 0);
        s_FXAAShader->SetFloat2("u_InverseScreenSize", glm::vec2(1.0f / s_Width, 1.0f / s_Height));

        glActiveTexture(GL_TEXTURE0);
        uint32_t colorTex = s_SceneFramebuffer->GetSpecification().Samples > 1 ? s_ResolvedFramebuffer->GetColorAttachmentRendererID() : s_SceneFramebuffer->GetColorAttachmentRendererID();
        glBindTexture(GL_TEXTURE_2D, colorTex);

        s_QuadVA->Bind();
        RenderCommand::DrawIndexed(s_QuadVA, 6);
    }

    void PostProcess::ApplySMAA()
    {
        if (!s_SMAAEdgeShader || !s_SMAABlendShader || !s_SceneFramebuffer || !s_SMAAEdgeFramebuffer)
            return;

        // 1. Edge Detection Pass
        s_SMAAEdgeFramebuffer->Bind();
        RenderCommand::SetClearColor({ 0.0f, 0.0f, 0.0f, 1.0f });
        RenderCommand::Clear();

        s_SMAAEdgeShader->Bind();
        s_SMAAEdgeShader->SetInt("u_ScreenTexture", 0);
        s_SMAAEdgeShader->SetFloat2("u_InverseScreenSize", glm::vec2(1.0f / s_Width, 1.0f / s_Height));

        glActiveTexture(GL_TEXTURE0);
        uint32_t colorTex = s_SceneFramebuffer->GetSpecification().Samples > 1 ? s_ResolvedFramebuffer->GetColorAttachmentRendererID() : s_SceneFramebuffer->GetColorAttachmentRendererID();
        glBindTexture(GL_TEXTURE_2D, colorTex);

        s_QuadVA->Bind();
        RenderCommand::DrawIndexed(s_QuadVA, 6);
        s_SMAAEdgeFramebuffer->Unbind();

        // 2. Blending Pass (Renders to currently bound framebuffer)
        s_SMAABlendShader->Bind();
        s_SMAABlendShader->SetInt("u_ScreenTexture", 0);
        s_SMAABlendShader->SetInt("u_EdgeTexture", 1);
        s_SMAABlendShader->SetFloat2("u_InverseScreenSize", glm::vec2(1.0f / s_Width, 1.0f / s_Height));

        glActiveTexture(GL_TEXTURE0);
        colorTex = s_SceneFramebuffer->GetSpecification().Samples > 1 ? s_ResolvedFramebuffer->GetColorAttachmentRendererID() : s_SceneFramebuffer->GetColorAttachmentRendererID();
        glBindTexture(GL_TEXTURE_2D, colorTex);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, s_SMAAEdgeFramebuffer->GetColorAttachmentRendererID());

        s_QuadVA->Bind();
        RenderCommand::DrawIndexed(s_QuadVA, 6);
    }
    
    void PostProcess::ApplyHaloFlare(const glm::vec2& lightScreenPos, const glm::vec3& lightColor,
                                      bool lightVisible, bool haloEnabled, std::shared_ptr<Texture2D> haloTexture,
                                      float haloStrength, bool flareEnabled, float flareStrength,
                                      const std::vector<Scene::FlareElement>& flareElements)
    {
        if (!s_HaloFlareShader || !s_SceneFramebuffer)
            return;
        
        if (!haloEnabled && !flareEnabled)
            return;
        
        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE);
        
        s_HaloFlareShader->Bind();
        s_HaloFlareShader->SetInt("u_SceneTexture", 0);
        s_HaloFlareShader->SetInt("u_DepthTexture", 1);
        s_HaloFlareShader->SetInt("u_HaloTexture", 2);
        s_HaloFlareShader->SetInt("u_HasHaloTexture", haloTexture != nullptr ? 1 : 0);
        s_HaloFlareShader->SetInt("u_HaloEnabled", haloEnabled ? 1 : 0);
        s_HaloFlareShader->SetFloat("u_HaloStrength", haloStrength);
        s_HaloFlareShader->SetInt("u_FlareEnabled", flareEnabled ? 1 : 0);
        s_HaloFlareShader->SetFloat("u_FlareStrength", flareStrength);
        s_HaloFlareShader->SetFloat2("u_LightScreenPos", lightScreenPos);
        s_HaloFlareShader->SetFloat3("u_LightColor", lightColor);
        s_HaloFlareShader->SetInt("u_LightVisible", lightVisible ? 1 : 0);
        s_HaloFlareShader->SetFloat2("u_ViewportSize", glm::vec2(s_Width, s_Height));
        
        // Flare Elements
        int elementCount = (int)flareElements.size();
        if (elementCount > 10) elementCount = 10; // Max 10
        s_HaloFlareShader->SetInt("u_FlareElementCount", elementCount);
        
        for (int i = 0; i < elementCount; i++)
        {
            std::string indexStr = std::to_string(i);
            s_HaloFlareShader->SetFloat3("u_FlareColors[" + indexStr + "]", flareElements[i].ColorMultiplier);
            s_HaloFlareShader->SetFloat("u_FlareSizes[" + indexStr + "]", flareElements[i].Size);
            s_HaloFlareShader->SetFloat("u_FlareOffsets[" + indexStr + "]", flareElements[i].Offset);
            s_HaloFlareShader->SetFloat("u_FlareIntensities[" + indexStr + "]", flareElements[i].Intensity);
        }
        
        glActiveTexture(GL_TEXTURE0);
        uint32_t colorTex = s_SceneFramebuffer->GetSpecification().Samples > 1 ? s_ResolvedFramebuffer->GetColorAttachmentRendererID() : s_SceneFramebuffer->GetColorAttachmentRendererID();
        glBindTexture(GL_TEXTURE_2D, colorTex);
        
        // Depth texture bind et
        glActiveTexture(GL_TEXTURE1);
        uint32_t depthTex = s_SceneFramebuffer->GetSpecification().Samples > 1 ? s_ResolvedFramebuffer->GetDepthAttachmentRendererID() : s_SceneFramebuffer->GetDepthAttachmentRendererID();
        glBindTexture(GL_TEXTURE_2D, depthTex);
        
        if (haloTexture)
        {
            glActiveTexture(GL_TEXTURE2);
            haloTexture->Bind(2);
        }
        
        s_QuadVA->Bind();
        RenderCommand::DrawIndexed(s_QuadVA, 6);
        
        glDisable(GL_BLEND);
    }

    uint32_t PostProcess::GetFinalTextureID()
    {
        if (!s_SceneFramebuffer) return 0;
        return s_SceneFramebuffer->GetSpecification().Samples > 1 ? s_ResolvedFramebuffer->GetColorAttachmentRendererID() : s_SceneFramebuffer->GetColorAttachmentRendererID();
    }

    void PostProcess::CreateQuad()
    {
        float quadVertices[] = {
            // positions   // texCoords
            -1.0f,  1.0f,  0.0f, 1.0f,
            -1.0f, -1.0f,  0.0f, 0.0f,
             1.0f, -1.0f,  1.0f, 0.0f,
             1.0f,  1.0f,  1.0f, 1.0f
        };

        uint32_t quadIndices[] = {
            0, 1, 2,
            2, 3, 0
        };

        s_QuadVA = VertexArray::Create();

        s_QuadVB = VertexBuffer::Create(quadVertices, sizeof(quadVertices));
        s_QuadVB->SetLayout({
            { ShaderDataType::Float2, "a_Position" },
            { ShaderDataType::Float2, "a_TexCoord" }
        });
        s_QuadVA->AddVertexBuffer(s_QuadVB);

        s_QuadIB = IndexBuffer::Create(quadIndices, 6);
        s_QuadVA->SetIndexBuffer(s_QuadIB);
    }
}
