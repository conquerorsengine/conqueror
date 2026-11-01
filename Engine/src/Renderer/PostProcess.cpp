#include "PostProcess.h"
#include "Core/Profiling/FrameProfiler.h"
#include "Core/Tiering/QualitySettings.h"
#include "Core/Logging/Log.h"
#include "RHI/RenderCommand.h"
#include "RHI/Texture.h"
#include "Renderer/Renderer.h"
#include "Renderer/Utilities/Renderer3D/Renderer3D.h"
#include "Scene/Scene.h"

#include <glad/glad.h>

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace Conqueror
{
    namespace
    {
        std::unordered_map<std::string, std::shared_ptr<Shader>> s_Shaders;
        std::shared_ptr<VertexArray> s_QuadVA;
        std::shared_ptr<VertexBuffer> s_QuadVB;
        std::shared_ptr<IndexBuffer> s_QuadIB;
        PostProcessStack* s_Default = nullptr;

        void LoadShader(const char* name, const char* path)
        {
            s_Shaders[name] = Shader::Create(path);
        }

        bool Valid(const std::shared_ptr<Shader>& s) { return s && s->IsValid(); }

        // Şu an sahnesi çizilen yığın (Temporal AA titreşimi için)
        PostProcessStack* s_Current = nullptr;

        float Halton(uint32_t index, uint32_t base)
        {
            float f = 1.0f, r = 0.0f;
            while (index > 0)
            {
                f /= (float)base;
                r += f * (float)(index % base);
                index /= base;
            }
            return r;
        }

        void BindTexture(uint32_t unit, uint32_t texture)
        {
            glActiveTexture(GL_TEXTURE0 + unit);
            glBindTexture(GL_TEXTURE_2D, texture);
        }

        // Kelvin benzeri sıcaklık/renk tonu -> beyaz dengesi çarpanı (Unity'nin basit yaklaşımı)
        glm::vec3 WhiteBalance(float temperature, float tint)
        {
            float t = temperature * 0.6f;
            float g = tint * 0.6f;
            glm::vec3 warm(1.0f + t * 0.35f, 1.0f + t * 0.05f - g * 0.25f, 1.0f - t * 0.45f);
            warm.g *= 1.0f - g * 0.2f;
            warm.r *= 1.0f + g * 0.1f;
            warm.b *= 1.0f + g * 0.1f;
            // parlaklığı koru
            float lum = 0.2126f * warm.r + 0.7152f * warm.g + 0.0722f * warm.b;
            return glm::max(warm / std::max(lum, 0.01f), glm::vec3(0.0f));
        }
    }

    // ================================================================== PostProcess (paylaşılan)

    void PostProcess::Init()
    {
        float quadVertices[] = {
            -1.0f,  1.0f,  0.0f, 1.0f,
            -1.0f, -1.0f,  0.0f, 0.0f,
             1.0f, -1.0f,  1.0f, 0.0f,
             1.0f,  1.0f,  1.0f, 1.0f
        };
        uint32_t quadIndices[] = { 0, 1, 2, 2, 3, 0 };
        s_QuadVA = VertexArray::Create();
        s_QuadVB = VertexBuffer::Create(quadVertices, sizeof(quadVertices));
        s_QuadVB->SetLayout({ { ShaderDataType::Float2, "a_Position" }, { ShaderDataType::Float2, "a_TexCoord" } });
        s_QuadVA->AddVertexBuffer(s_QuadVB);
        s_QuadIB = IndexBuffer::Create(quadIndices, 6);
        s_QuadVA->SetIndexBuffer(s_QuadIB);

        LoadShader("FXAA", "Engine/src/Shaders/PostProcess/FXAA.cqsh");
        LoadShader("SMAAEdge", "Engine/src/Shaders/PostProcess/SMAA_Edge.cqsh");
        LoadShader("SMAABlend", "Engine/src/Shaders/PostProcess/SMAA_Blend.cqsh");
        LoadShader("HaloFlare", "Engine/src/Shaders/PostProcess/HaloFlare.cqsh");
        LoadShader("Bloom", "Engine/src/Shaders/PostProcess/Bloom.cqsh");
        LoadShader("SSAO", "Engine/src/Shaders/PostProcess/SSAO.cqsh");
        LoadShader("SSAOBlur", "Engine/src/Shaders/PostProcess/SSAOBlur.cqsh");
        LoadShader("Composite", "Engine/src/Shaders/PostProcess/Composite.cqsh");
        LoadShader("Sharpen", "Engine/src/Shaders/PostProcess/Sharpen.cqsh");
        LoadShader("SSR", "Engine/src/Shaders/PostProcess/SSR.cqsh");
        LoadShader("VolumetricFog", "Engine/src/Shaders/PostProcess/VolumetricFog.cqsh");
        LoadShader("Clouds", "Engine/src/Shaders/PostProcess/Clouds.cqsh");
        LoadShader("CloudNoise", "Engine/src/Shaders/PostProcess/CloudNoise.cqsh");
        LoadShader("SceneFX", "Engine/src/Shaders/PostProcess/SceneFX.cqsh");
        LoadShader("TAA", "Engine/src/Shaders/PostProcess/TAA.cqsh");
        LoadShader("DepthOfField", "Engine/src/Shaders/PostProcess/DepthOfField.cqsh");
        LoadShader("MotionBlur", "Engine/src/Shaders/PostProcess/MotionBlur.cqsh");
        LoadShader("AutoExposure", "Engine/src/Shaders/PostProcess/AutoExposure.cqsh");
        LoadShader("SSGI", "Engine/src/Shaders/PostProcess/SSGI.cqsh");

        s_Default = new PostProcessStack();
    }

    void PostProcess::Shutdown()
    {
        delete s_Default;
        s_Default = nullptr;
        s_Shaders.clear();
        s_QuadVA.reset();
        s_QuadVB.reset();
        s_QuadIB.reset();
    }

    PostProcessStack& PostProcess::Default()
    {
        if (!s_Default)
            s_Default = new PostProcessStack();
        return *s_Default;
    }

    void PostProcess::BeginScene(uint32_t width, uint32_t height) { Default().BeginScene(width, height); }
    void PostProcess::EndScene() { Default().EndScene(); }
    void PostProcess::ApplyFXAA() { Default().ApplyFXAA(); }
    void PostProcess::ApplySMAA() { Default().ApplySMAA(); }
    void PostProcess::ApplyHaloFlare(const glm::vec2& lightScreenPos, const glm::vec3& lightColor,
                                     bool lightVisible, bool haloEnabled, std::shared_ptr<Texture2D> haloTexture,
                                     float haloStrength, bool flareEnabled, float flareStrength,
                                     const std::vector<Scene::FlareElement>& flareElements)
    {
        Default().ApplyHaloFlare(lightScreenPos, lightColor, lightVisible, haloEnabled, haloTexture, haloStrength, flareEnabled, flareStrength, flareElements);
    }

    void PostProcess::DrawFullscreenQuad()
    {
        if (!s_QuadVA)
            return;
        s_QuadVA->Bind();
        RenderCommand::DrawIndexed(s_QuadVA, 6);
    }

    std::shared_ptr<Shader> PostProcess::GetShader(const char* name)
    {
        auto it = s_Shaders.find(name);
        return it != s_Shaders.end() ? it->second : nullptr;
    }

    // ================================================================== PostProcessStack

    PostProcessStack::~PostProcessStack()
    {
        DestroyTarget(m_AO);
        DestroyTarget(m_AOBlur);
        for (auto& rt : m_BloomMips) DestroyTarget(rt);
        for (auto& rt : m_BloomUp) DestroyTarget(rt);
        DestroyTarget(m_LDR);
        DestroyTarget(m_SMAAEdges);
        DestroyTarget(m_Temp);
        ReleaseEffectTargets();
        if (s_Current == this)
            s_Current = nullptr;
    }

    void PostProcessStack::CreateTarget(RenderTarget& rt, uint32_t width, uint32_t height, uint32_t internalFormat)
    {
        DestroyTarget(rt);
        rt.Width = std::max(width, 1u);
        rt.Height = std::max(height, 1u);
        glGenTextures(1, &rt.Texture);
        glBindTexture(GL_TEXTURE_2D, rt.Texture);
        glTexStorage2D(GL_TEXTURE_2D, 1, internalFormat, (GLsizei)rt.Width, (GLsizei)rt.Height);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glGenFramebuffers(1, &rt.Framebuffer);
        glBindFramebuffer(GL_FRAMEBUFFER, rt.Framebuffer);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, rt.Texture, 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            CQ_CORE_ERROR("PostProcess: incomplete render target {0}x{1}", rt.Width, rt.Height);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    void PostProcessStack::DestroyTarget(RenderTarget& rt)
    {
        if (rt.Framebuffer) glDeleteFramebuffers(1, &rt.Framebuffer);
        if (rt.Texture) glDeleteTextures(1, &rt.Texture);
        rt = RenderTarget{};
    }

    void PostProcessStack::EnsureTargets(uint32_t width, uint32_t height)
    {
        uint32_t samples = std::max(1u, (uint32_t)QualitySettings::GetPreset().AntiAliasing);
        // Temporal AA kendi kenar yumuşatmasını yapar: MSAA ile birlikte gereksiz maliyet
        if (m_WantTAA)
            samples = 1;
        // Albedo eki normal ekini gerektirir (ek sırası sabit: 1 normal, 2 albedo)
        const bool wantNormals = m_WantNormals || m_WantAlbedo;
        if (m_SceneFramebuffer && m_Width == width && m_Height == height && m_Samples == samples && m_HasNormals == wantNormals && m_HasAlbedo == m_WantAlbedo)
            return;

        if (m_Width != width || m_Height != height)
        {
            m_HistoryValid = false;
            m_HasPrevViewProj = false;
            m_GIHistoryValid = false;
        }
        m_Width = width;
        m_Height = height;
        m_Samples = samples;
        m_HasNormals = wantNormals;
        m_HasAlbedo = m_WantAlbedo;

        FramebufferSpecification spec;
        spec.Width = width;
        spec.Height = height;
        // SSR / SSGI açıkken ek renk ekleri: PBR shader'ları normal / pürüzlülük / metaliklik ve albedo yazar
        if (m_HasAlbedo)
            spec.Attachments = { FramebufferTextureFormat::RGBA16F, FramebufferTextureFormat::RGBA16F, FramebufferTextureFormat::RGBA8, FramebufferTextureFormat::Depth };
        else if (m_HasNormals)
            spec.Attachments = { FramebufferTextureFormat::RGBA16F, FramebufferTextureFormat::RGBA16F, FramebufferTextureFormat::Depth };
        else
            spec.Attachments = { FramebufferTextureFormat::RGBA16F, FramebufferTextureFormat::Depth };
        spec.Samples = samples;
        m_SceneFramebuffer = Framebuffer::Create(spec);

        m_ResolvedFramebuffer.reset();
        if (samples > 1)
        {
            FramebufferSpecification resolved = spec;
            resolved.Samples = 1;
            m_ResolvedFramebuffer = Framebuffer::Create(resolved);
        }

        const uint32_t halfW = std::max(width / 2, 1u);
        const uint32_t halfH = std::max(height / 2, 1u);
        CreateTarget(m_AO, halfW, halfH, GL_R8);
        CreateTarget(m_AOBlur, halfW, halfH, GL_R8);

        for (auto& rt : m_BloomMips) DestroyTarget(rt);
        for (auto& rt : m_BloomUp) DestroyTarget(rt);
        m_BloomMips.clear();
        m_BloomUp.clear();
        uint32_t bw = halfW, bh = halfH;
        for (int i = 0; i < 7 && bw >= 4 && bh >= 4; ++i)
        {
            RenderTarget down, up;
            CreateTarget(down, bw, bh, GL_R11F_G11F_B10F);
            m_BloomMips.push_back(down);
            CreateTarget(up, bw, bh, GL_R11F_G11F_B10F);
            m_BloomUp.push_back(up);
            bw /= 2;
            bh /= 2;
        }

        CreateTarget(m_LDR, width, height, GL_RGBA8);
        CreateTarget(m_SMAAEdges, width, height, GL_RGBA8);
        CreateTarget(m_Temp, width, height, GL_RGBA8);
    }

    void PostProcessStack::BeginScene(uint32_t width, uint32_t height)
    {
        width = std::max(width, 1u);
        height = std::max(height, 1u);
        EnsureTargets(width, height);
        m_SceneFramebuffer->Bind();
        s_Current = this;
        // Temporal AA: Halton(2,3) dizisiyle 8 karelik alt piksel titreşimi
        m_JitterActive = m_WantTAA;
        if (m_JitterActive)
        {
            const uint32_t i = (m_FrameIndex % 8u) + 1u;
            const glm::vec2 pixel(Halton(i, 2) - 0.5f, Halton(i, 3) - 0.5f);
            m_JitterNDC = pixel * 2.0f / glm::vec2((float)m_Width, (float)m_Height);
        }
        else
            m_JitterNDC = glm::vec2(0.0f);
    }

    bool PostProcessStack::GetCurrentJitter(glm::vec2& outNDC)
    {
        if (!s_Current || !s_Current->m_JitterActive || !s_Current->m_SceneFramebuffer)
            return false;
        GLint drawFBO = 0;
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFBO);
        if ((uint32_t)drawFBO != s_Current->m_SceneFramebuffer->GetRendererID())
            return false;
        outNDC = s_Current->m_JitterNDC;
        return true;
    }

    uint32_t PostProcessStack::GetSceneNormalTexture() const
    {
        if (!m_HasNormals)
            return 0;
        if (m_Samples > 1 && m_ResolvedFramebuffer)
            return m_ResolvedFramebuffer->GetColorAttachmentRendererID(1);
        return m_SceneFramebuffer ? m_SceneFramebuffer->GetColorAttachmentRendererID(1) : 0;
    }

    uint32_t PostProcessStack::GetSceneAlbedoTexture() const
    {
        if (!m_HasAlbedo)
            return 0;
        if (m_Samples > 1 && m_ResolvedFramebuffer)
            return m_ResolvedFramebuffer->GetColorAttachmentRendererID(2);
        return m_SceneFramebuffer ? m_SceneFramebuffer->GetColorAttachmentRendererID(2) : 0;
    }

    void PostProcessStack::EndScene()
    {
        m_SceneFramebuffer->Unbind();
        if (m_Samples > 1 && m_ResolvedFramebuffer)
        {
            glBindFramebuffer(GL_READ_FRAMEBUFFER, m_SceneFramebuffer->GetRendererID());
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_ResolvedFramebuffer->GetRendererID());
            glBlitFramebuffer(0, 0, m_Width, m_Height, 0, 0, m_Width, m_Height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
            glBlitFramebuffer(0, 0, m_Width, m_Height, 0, 0, m_Width, m_Height, GL_DEPTH_BUFFER_BIT, GL_NEAREST);
            if (m_HasNormals)
            {
                // SSR / SSGI ekleri de çözülür (en yakın örnek)
                const GLuint src = m_SceneFramebuffer->GetRendererID(), dst = m_ResolvedFramebuffer->GetRendererID();
                const int extra = m_HasAlbedo ? 2 : 1;
                for (int a = 1; a <= extra; ++a)
                {
                    glNamedFramebufferReadBuffer(src, GL_COLOR_ATTACHMENT0 + a);
                    glNamedFramebufferDrawBuffer(dst, GL_COLOR_ATTACHMENT0 + a);
                    glBlitNamedFramebuffer(src, dst, 0, 0, m_Width, m_Height, 0, 0, m_Width, m_Height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
                }
                glNamedFramebufferReadBuffer(src, GL_COLOR_ATTACHMENT0);
                const GLenum all[3] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2 };
                glNamedFramebufferDrawBuffers(dst, extra + 1, all);
            }
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
        }
    }

    uint32_t PostProcessStack::GetSceneColorTexture() const
    {
        if (m_Samples > 1 && m_ResolvedFramebuffer)
            return m_ResolvedFramebuffer->GetColorAttachmentRendererID();
        return m_SceneFramebuffer ? m_SceneFramebuffer->GetColorAttachmentRendererID() : 0;
    }

    uint32_t PostProcessStack::GetSceneDepthTexture() const
    {
        if (m_Samples > 1 && m_ResolvedFramebuffer)
            return m_ResolvedFramebuffer->GetDepthAttachmentRendererID();
        return m_SceneFramebuffer ? m_SceneFramebuffer->GetDepthAttachmentRendererID() : 0;
    }

    void PostProcessStack::RunSSAO(const PostProcessSettings& settings, const RenderCamera& camera, float time)
    {
        auto ssao = PostProcess::GetShader("SSAO");
        auto blur = PostProcess::GetShader("SSAOBlur");
        m_AOValid = false;
        if (!Valid(ssao) || !Valid(blur) || camera.Orthographic)
            return;

        glBindFramebuffer(GL_FRAMEBUFFER, m_AO.Framebuffer);
        glViewport(0, 0, m_AO.Width, m_AO.Height);
        ssao->Bind();
        ssao->SetInt("u_Depth", 0);
        ssao->SetMat4("u_Projection", camera.Projection);
        ssao->SetMat4("u_InvProjection", glm::inverse(camera.Projection));
        ssao->SetFloat2("u_DepthTexel", glm::vec2(1.0f / m_Width, 1.0f / m_Height));
        ssao->SetFloat("u_Radius", settings.AORadius);
        ssao->SetFloat("u_Intensity", settings.AOIntensity);
        ssao->SetFloat("u_Power", settings.AOPower);
        ssao->SetFloat("u_Time", time);
        BindTexture(0, GetSceneDepthTexture());
        PostProcess::DrawFullscreenQuad();

        glBindFramebuffer(GL_FRAMEBUFFER, m_AOBlur.Framebuffer);
        blur->Bind();
        blur->SetInt("u_AO", 0);
        blur->SetInt("u_Depth", 1);
        blur->SetFloat2("u_TexelSize", glm::vec2(1.0f / m_AO.Width, 1.0f / m_AO.Height));
        BindTexture(0, m_AO.Texture);
        BindTexture(1, GetSceneDepthTexture());
        PostProcess::DrawFullscreenQuad();
        Renderer::GetStats().PostProcessPasses += 2;
        m_AOValid = true;
    }

    void PostProcessStack::RunBloom(const PostProcessSettings& settings)
    {
        m_BloomResult = 0;
        auto bloom = PostProcess::GetShader("Bloom");
        if (!Valid(bloom) || m_BloomMips.empty())
            return;

        bloom->Bind();
        bloom->SetInt("u_Source", 0);
        bloom->SetInt("u_Higher", 1);

        // Ön filtre: sahne -> mip0
        float knee = std::max(settings.BloomThreshold * settings.BloomKnee, 1e-4f);
        bloom->SetFloat4("u_Threshold", glm::vec4(settings.BloomThreshold, settings.BloomThreshold - knee, 2.0f * knee, 0.25f / knee));
        bloom->SetInt("u_Mode", 0);
        glBindFramebuffer(GL_FRAMEBUFFER, m_BloomMips[0].Framebuffer);
        glViewport(0, 0, m_BloomMips[0].Width, m_BloomMips[0].Height);
        bloom->SetFloat2("u_TexelSize", glm::vec2(1.0f / m_Width, 1.0f / m_Height));
        BindTexture(0, m_Source ? m_Source : GetSceneColorTexture());
        PostProcess::DrawFullscreenQuad();

        // Aşağı örnekleme
        bloom->SetInt("u_Mode", 1);
        for (size_t i = 1; i < m_BloomMips.size(); ++i)
        {
            const auto& src = m_BloomMips[i - 1];
            const auto& dst = m_BloomMips[i];
            glBindFramebuffer(GL_FRAMEBUFFER, dst.Framebuffer);
            glViewport(0, 0, dst.Width, dst.Height);
            bloom->SetFloat2("u_TexelSize", glm::vec2(1.0f / src.Width, 1.0f / src.Height));
            BindTexture(0, src.Texture);
            PostProcess::DrawFullscreenQuad();
        }

        // Yukarı örnekleme (en küçükten başlayarak)
        bloom->SetInt("u_Mode", 2);
        bloom->SetFloat("u_Scatter", glm::clamp(settings.BloomScatter, 0.0f, 1.0f));
        uint32_t current = m_BloomMips.back().Texture;
        uint32_t currentW = m_BloomMips.back().Width, currentH = m_BloomMips.back().Height;
        for (int i = (int)m_BloomMips.size() - 2; i >= 0; --i)
        {
            const auto& dst = m_BloomUp[i];
            glBindFramebuffer(GL_FRAMEBUFFER, dst.Framebuffer);
            glViewport(0, 0, dst.Width, dst.Height);
            bloom->SetFloat2("u_TexelSize", glm::vec2(1.0f / currentW, 1.0f / currentH));
            BindTexture(0, current);
            BindTexture(1, m_BloomMips[i].Texture);
            PostProcess::DrawFullscreenQuad();
            current = dst.Texture;
            currentW = dst.Width;
            currentH = dst.Height;
        }
        m_BloomResult = current;
        Renderer::GetStats().PostProcessPasses += (uint32_t)(m_BloomMips.size() * 2);
    }

    void PostProcessStack::RunComposite(const PostProcessSettings& settings, float time)
    {
        auto composite = PostProcess::GetShader("Composite");
        glBindFramebuffer(GL_FRAMEBUFFER, m_LDR.Framebuffer);
        glViewport(0, 0, m_LDR.Width, m_LDR.Height);
        if (!Valid(composite))
            return;

        const bool on = settings.Enabled;
        composite->Bind();
        composite->SetInt("u_Scene", 0);
        composite->SetInt("u_Bloom", 1);
        composite->SetInt("u_AO", 2);
        composite->SetInt("u_Depth", 3);
        composite->SetInt("u_LitScene", m_LitScene ? 1 : 0);
        composite->SetInt("u_UseBloom", (on && settings.Bloom && m_BloomResult) ? 1 : 0);
        // AO yalnızca bu karede hesaplandıysa (ortografik kamerada atlanır; eski sonuç kullanılmaz)
        composite->SetInt("u_UseAO", (on && settings.AmbientOcclusion && m_AOBlur.Texture && m_AOValid && !m_AOApplied) ? 1 : 0);
        composite->SetInt("u_AutoExposure", 4);
        composite->SetInt("u_UseAutoExposure", m_UseAutoExposure ? 1 : 0);
        composite->SetFloat2("u_AutoExposureRange", glm::vec2(settings.AutoExposureMin, std::max(settings.AutoExposureMax, settings.AutoExposureMin)));
        composite->SetFloat("u_AutoExposureCompensation", settings.AutoExposureCompensation);
        composite->SetFloat("u_BloomIntensity", settings.BloomIntensity);
        composite->SetFloat3("u_BloomTint", settings.BloomTint);
        composite->SetFloat("u_Exposure", on ? std::exp2(settings.Exposure) : 1.0f);
        composite->SetInt("u_Tonemapper", on ? (int)settings.Tonemap : 0);
        composite->SetInt("u_UseGrading", (on && settings.ColorGrading) ? 1 : 0);
        composite->SetFloat("u_Contrast", settings.Contrast);
        composite->SetFloat("u_Saturation", settings.Saturation);
        composite->SetFloat3("u_WhiteBalance", WhiteBalance(settings.Temperature, settings.Tint));
        composite->SetFloat3("u_Lift", settings.Lift);
        composite->SetFloat3("u_Gamma", settings.Gamma);
        composite->SetFloat3("u_Gain", settings.Gain);
        composite->SetInt("u_UseVignette", (on && settings.Vignette) ? 1 : 0);
        composite->SetFloat("u_VignetteIntensity", settings.VignetteIntensity);
        composite->SetFloat("u_VignetteSmoothness", settings.VignetteSmoothness);
        composite->SetFloat3("u_VignetteColor", settings.VignetteColor);
        composite->SetInt("u_UseCA", (on && settings.ChromaticAberration) ? 1 : 0);
        composite->SetFloat("u_CAIntensity", settings.ChromaticAberrationIntensity);
        composite->SetInt("u_UseGrain", (on && settings.FilmGrain) ? 1 : 0);
        composite->SetFloat("u_GrainIntensity", settings.FilmGrainIntensity);
        composite->SetFloat("u_Time", time);
        composite->SetFloat2("u_ScreenSize", glm::vec2((float)m_Width, (float)m_Height));

        BindTexture(0, m_Source ? m_Source : GetSceneColorTexture());
        BindTexture(1, m_BloomResult);
        BindTexture(2, m_AOBlur.Texture);
        BindTexture(3, GetSceneDepthTexture());
        BindTexture(4, m_UseAutoExposure ? m_Adapt[m_AdaptIndex].Texture : 0);
        PostProcess::DrawFullscreenQuad();
        Renderer::GetStats().PostProcessPasses++;
    }

    void PostProcessStack::RunFinal(const PostProcessSettings& settings, uint32_t outputFramebuffer, const int outputViewport[4])
    {
        auto fxaa = PostProcess::GetShader("FXAA");
        auto smaaEdge = PostProcess::GetShader("SMAAEdge");
        auto smaaBlend = PostProcess::GetShader("SMAABlend");
        auto sharpen = PostProcess::GetShader("Sharpen");
        const glm::vec2 invSize(1.0f / m_Width, 1.0f / m_Height);

        AntiAliasingMode aa = settings.Enabled ? settings.AntiAliasing : AntiAliasingMode::FXAA;
        // Piksel sanatı büyütmesi: kenar yumuşatma / keskinleştirme pikselleri bulanıklaştırır
        if (m_PointUpscale)
            aa = AntiAliasingMode::None;
        // Temporal AA HDR'de çözüldü: burada uzamsal AA yok, TAA yumuşaklığını keskinleştirme dengeler
        // (ortografik kamerada TAA yok: FXAA'ya düşer)
        const bool taa = aa == AntiAliasingMode::TAA && m_TAAResolved;
        if (aa == AntiAliasingMode::TAA)
            aa = m_TAAResolved ? AntiAliasingMode::None : AntiAliasingMode::FXAA;
        const float sharpenAmount = (taa ? settings.TAASharpness : 0.0f) + (settings.Sharpen ? settings.SharpenAmount : 0.0f);
        const bool doSharpen = !m_PointUpscale && settings.Enabled && sharpenAmount > 0.0f && Valid(sharpen);
        const GLint ldrFilter = m_PointUpscale ? GL_NEAREST : GL_LINEAR;
        glTextureParameteri(m_LDR.Texture, GL_TEXTURE_MIN_FILTER, ldrFilter);
        glTextureParameteri(m_LDR.Texture, GL_TEXTURE_MAG_FILTER, ldrFilter);

        // AA sonucu: keskinleştirme varsa geçici hedefe, yoksa doğrudan çıktıya
        uint32_t aaTarget = doSharpen ? m_Temp.Framebuffer : outputFramebuffer;
        auto bindAATarget = [&]()
        {
            glBindFramebuffer(GL_FRAMEBUFFER, aaTarget);
            if (doSharpen)
                glViewport(0, 0, m_Width, m_Height);
            else
                glViewport(outputViewport[0], outputViewport[1], outputViewport[2], outputViewport[3]);
        };

        uint32_t aaResult = m_LDR.Texture;
        if (aa == AntiAliasingMode::SMAA && Valid(smaaEdge) && Valid(smaaBlend))
        {
            glBindFramebuffer(GL_FRAMEBUFFER, m_SMAAEdges.Framebuffer);
            glViewport(0, 0, m_Width, m_Height);
            glClearColor(0, 0, 0, 1);
            glClear(GL_COLOR_BUFFER_BIT);
            smaaEdge->Bind();
            smaaEdge->SetInt("u_ScreenTexture", 0);
            smaaEdge->SetFloat2("u_InverseScreenSize", invSize);
            BindTexture(0, m_LDR.Texture);
            PostProcess::DrawFullscreenQuad();

            bindAATarget();
            smaaBlend->Bind();
            smaaBlend->SetInt("u_ScreenTexture", 0);
            smaaBlend->SetInt("u_EdgeTexture", 1);
            smaaBlend->SetFloat2("u_InverseScreenSize", invSize);
            BindTexture(0, m_LDR.Texture);
            BindTexture(1, m_SMAAEdges.Texture);
            PostProcess::DrawFullscreenQuad();
            aaResult = m_Temp.Texture;
        }
        else if (aa == AntiAliasingMode::FXAA && Valid(fxaa))
        {
            bindAATarget();
            fxaa->Bind();
            fxaa->SetInt("u_ScreenTexture", 0);
            fxaa->SetFloat2("u_InverseScreenSize", invSize);
            BindTexture(0, m_LDR.Texture);
            PostProcess::DrawFullscreenQuad();
            aaResult = m_Temp.Texture;
        }
        else if (Valid(sharpen))
        {
            // AA yok: kopya (keskinleştirme ayrıca uygulanacaksa doğrudan son hedefe)
            if (!doSharpen)
            {
                bindAATarget();
                sharpen->Bind();
                sharpen->SetInt("u_ScreenTexture", 0);
                sharpen->SetFloat2("u_InverseScreenSize", invSize);
                sharpen->SetFloat("u_Amount", 0.0f);
                BindTexture(0, m_LDR.Texture);
                PostProcess::DrawFullscreenQuad();
            }
            aaResult = m_LDR.Texture;
        }

        if (doSharpen)
        {
            glBindFramebuffer(GL_FRAMEBUFFER, outputFramebuffer);
            glViewport(outputViewport[0], outputViewport[1], outputViewport[2], outputViewport[3]);
            sharpen->Bind();
            sharpen->SetInt("u_ScreenTexture", 0);
            sharpen->SetFloat2("u_InverseScreenSize", invSize);
            sharpen->SetFloat("u_Amount", std::min(sharpenAmount, 1.0f));
            BindTexture(0, aaResult);
            PostProcess::DrawFullscreenQuad();
        }
        Renderer::GetStats().PostProcessPasses++;
    }

    void PostProcessStack::Render(const PostProcessSettings& settings, const RenderCamera& camera, float time, bool litScene)
    {
        if (!m_SceneFramebuffer)
            return;
        CQ_PROFILE_SAMPLE("Render.PostProcess", Rendering);
        CQ_PROFILE_GPU("Post Processing");
        m_LitScene = litScene;

        GLint outputFBO = 0;
        GLint outputViewport[4];
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &outputFBO);
        glGetIntegerv(GL_VIEWPORT, outputViewport);
        GLboolean depthTest = glIsEnabled(GL_DEPTH_TEST);
        GLboolean blend = glIsEnabled(GL_BLEND);
        GLboolean cull = glIsEnabled(GL_CULL_FACE);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);
        glDisable(GL_CULL_FACE);

        m_AOValid = false;
        const QualityPreset& quality = QualitySettings::GetPreset();
        if (settings.Enabled && settings.AmbientOcclusion && quality.EnableSSAO)
            RunSSAO(settings, camera, time);

        // HDR efekt zinciri: sahne -> (SSR + bulut + hacimsel sis) -> TAA -> alan derinliği -> hareket bulanıklığı
        const float deltaTime = m_LastTime < 0.0f ? 0.0f : std::clamp(time - m_LastTime, 0.0f, 0.5f);
        if (m_LastTime >= 0.0f && time - m_LastTime > 0.5f)
            m_HistoryValid = false;   // görünüm bir süre çizilmedi: eski geçmiş güvenilmez
        m_LastTime = time;
        m_DeltaTime = deltaTime;
        m_Source = GetSceneColorTexture();
        m_AOApplied = false;
        const bool fx = settings.Enabled && !m_PointUpscale;
        if (fx)
            RunSceneEffects(settings, camera, time);
        m_TAAResolved = false;
        if (fx && settings.AntiAliasing == AntiAliasingMode::TAA && !camera.Orthographic)
            RunTAA(settings, camera);
        else
            m_HistoryValid = false;
        if (fx && settings.DepthOfField)
            RunDepthOfField(settings, camera);
        if (fx && settings.MotionBlur)
            RunMotionBlur(settings, camera);
        m_UseAutoExposure = false;
        if (fx && settings.AutoExposure)
            RunAutoExposure(settings, deltaTime);
        else
            m_AdaptValid = false;

        if (settings.Enabled && settings.Bloom && quality.EnableBloom)
            RunBloom(settings);
        else
            m_BloomResult = 0;
        RunComposite(settings, time);
        RunFinal(settings, (uint32_t)outputFBO, outputViewport);

        // Sonraki kare için: titreşimsiz görüş-izdüşüm (TAA / hareket bulanıklığı yeniden izdüşümü), istekler
        m_PrevViewProj = camera.Projection * camera.View;
        m_HasPrevViewProj = true;
        ++m_FrameIndex;
        m_WantTAA = fx && settings.AntiAliasing == AntiAliasingMode::TAA && !camera.Orthographic;
        m_WantNormals = fx && (settings.ScreenSpaceReflections || settings.ScreenSpaceGI) && !camera.Orthographic;
        m_WantAlbedo = fx && settings.ScreenSpaceGI && !camera.Orthographic;
        if (!m_WantAlbedo)
            m_GIHistoryValid = false;

        glActiveTexture(GL_TEXTURE0);
        if (depthTest) glEnable(GL_DEPTH_TEST);
        if (blend) glEnable(GL_BLEND);
        if (cull) glEnable(GL_CULL_FACE);
        glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)outputFBO);
        glViewport(outputViewport[0], outputViewport[1], outputViewport[2], outputViewport[3]);
    }

    void PostProcessStack::ApplyFXAA()
    {
        auto fxaa = PostProcess::GetShader("FXAA");
        if (!Valid(fxaa) || !m_SceneFramebuffer)
            return;
        fxaa->Bind();
        fxaa->SetInt("u_ScreenTexture", 0);
        fxaa->SetFloat2("u_InverseScreenSize", glm::vec2(1.0f / m_Width, 1.0f / m_Height));
        BindTexture(0, GetSceneColorTexture());
        PostProcess::DrawFullscreenQuad();
    }

    void PostProcessStack::ApplySMAA()
    {
        ApplyFXAA();
    }

    void PostProcessStack::ApplyHaloFlare(const glm::vec2& lightScreenPos, const glm::vec3& lightColor,
                                          bool lightVisible, bool haloEnabled, std::shared_ptr<Texture2D> haloTexture,
                                          float haloStrength, bool flareEnabled, float flareStrength,
                                          const std::vector<Scene::FlareElement>& flareElements)
    {
        auto shader = PostProcess::GetShader("HaloFlare");
        if (!Valid(shader) || !m_SceneFramebuffer || (!haloEnabled && !flareEnabled))
            return;

        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE);
        glDisable(GL_DEPTH_TEST);

        shader->Bind();
        shader->SetInt("u_SceneTexture", 0);
        shader->SetInt("u_DepthTexture", 1);
        shader->SetInt("u_HaloTexture", 2);
        shader->SetInt("u_HasHaloTexture", haloTexture != nullptr ? 1 : 0);
        shader->SetInt("u_HaloEnabled", haloEnabled ? 1 : 0);
        shader->SetFloat("u_HaloStrength", haloStrength);
        shader->SetInt("u_FlareEnabled", flareEnabled ? 1 : 0);
        shader->SetFloat("u_FlareStrength", flareStrength);
        shader->SetFloat2("u_LightScreenPos", lightScreenPos);
        shader->SetFloat3("u_LightColor", lightColor);
        shader->SetInt("u_LightVisible", lightVisible ? 1 : 0);
        shader->SetFloat2("u_ViewportSize", glm::vec2(m_Width, m_Height));

        int elementCount = std::min((int)flareElements.size(), 10);
        shader->SetInt("u_FlareElementCount", elementCount);
        for (int i = 0; i < elementCount; i++)
        {
            std::string idx = "[" + std::to_string(i) + "]";
            shader->SetFloat3("u_FlareColors" + idx, flareElements[i].ColorMultiplier);
            shader->SetFloat("u_FlareSizes" + idx, flareElements[i].Size);
            shader->SetFloat("u_FlareOffsets" + idx, flareElements[i].Offset);
            shader->SetFloat("u_FlareIntensities" + idx, flareElements[i].Intensity);
        }

        BindTexture(0, m_LDR.Texture ? m_LDR.Texture : GetSceneColorTexture());
        BindTexture(1, GetSceneDepthTexture());
        if (haloTexture)
            haloTexture->Bind(2);

        PostProcess::DrawFullscreenQuad();
        glDisable(GL_BLEND);
        glActiveTexture(GL_TEXTURE0);
    }
}
