#include "PostProcess.h"
#include "Core/Profiling/FrameProfiler.h"
#include "Core/Logging/Log.h"
#include "Renderer/Renderer.h"
#include "Renderer/Utilities/Renderer3D/Renderer3D.h"

#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

// Unreal render paketi: ekran uzayı yansımalar, hacimsel sis (gölgeli güneş huzmeleri), hacimsel bulutlar,
// Temporal AA, alan derinliği (bokeh), kamera hareket bulanıklığı ve otomatik pozlama. Hepsi HDR'de, ton
// eşlemeden önce çalışır; kapalı olanlar hiç hedef ayırmaz ve maliyetsizdir.
namespace Conqueror
{
    namespace
    {
        bool Valid(const std::shared_ptr<Shader>& s) { return s && s->IsValid(); }

        void BindTexture(uint32_t unit, uint32_t texture)
        {
            glActiveTexture(GL_TEXTURE0 + unit);
            glBindTexture(GL_TEXTURE_2D, texture);
        }

        // Bulut gürültüsü: 64³ döşenebilir hacim, 8x8 karolu 2B atlas (karo başına 1 texel sarmalı kenar). 3B doku
        // yerine 2B: GL çeviri katmanları (Vulkan / D3D11 / D3D12), GLES ve Metal'de aynı yol. İlk kullanımda GPU'da üretilir.
        uint32_t s_CloudNoise = 0;

        uint32_t CloudNoiseTexture()
        {
            if (s_CloudNoise)
                return s_CloudNoise;
            auto shader = PostProcess::GetShader("CloudNoise");
            if (!Valid(shader))
                return 0;
            const int size = 64, atlas = 8 * (size + 2);
            glGenTextures(1, &s_CloudNoise);
            glBindTexture(GL_TEXTURE_2D, s_CloudNoise);
            glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, atlas, atlas);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

            GLint prevFBO = 0, prevViewport[4];
            glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);
            glGetIntegerv(GL_VIEWPORT, prevViewport);
            uint32_t fbo = 0;
            glGenFramebuffers(1, &fbo);
            glBindFramebuffer(GL_FRAMEBUFFER, fbo);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, s_CloudNoise, 0);
            glViewport(0, 0, atlas, atlas);
            shader->Bind();
            PostProcess::DrawFullscreenQuad();
            glDeleteFramebuffers(1, &fbo);
            glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prevFBO);
            glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
            CQ_CORE_INFO("[PostProcess] Cloud noise volume generated ({0}^3)", size);
            return s_CloudNoise;
        }
    }

    void PostProcessStack::EnsureTarget(RenderTarget& rt, uint32_t width, uint32_t height, uint32_t internalFormat)
    {
        width = std::max(width, 1u);
        height = std::max(height, 1u);
        if (rt.Texture && rt.Width == width && rt.Height == height)
            return;
        CreateTarget(rt, width, height, internalFormat);
    }

    void PostProcessStack::ReleaseEffectTargets()
    {
        for (auto* rt : { &m_HDR[0], &m_HDR[1], &m_SSR, &m_Fog, &m_Clouds, &m_DOFHalf, &m_DOFBlur, &m_History[0], &m_History[1], &m_Adapt[0], &m_Adapt[1],
                          &m_GI, &m_GITemp, &m_GIHistory[0], &m_GIHistory[1] })
            DestroyTarget(*rt);
        if (m_LumFramebuffer) glDeleteFramebuffers(1, &m_LumFramebuffer);
        if (m_LumTexture) glDeleteTextures(1, &m_LumTexture);
        m_LumFramebuffer = m_LumTexture = 0;
    }

    PostProcessStack::RenderTarget& PostProcessStack::NextHDRTarget()
    {
        RenderTarget& rt = (m_HDR[0].Texture && m_Source == m_HDR[0].Texture) ? m_HDR[1] : m_HDR[0];
        EnsureTarget(rt, m_Width, m_Height, GL_RGBA16F);
        return rt;
    }

    glm::mat4 PostProcessStack::JitteredProjection(const RenderCamera& camera) const
    {
        glm::mat4 p = camera.Projection;
        if (m_JitterActive)
            for (int c = 0; c < 4; ++c)
            {
                p[c][0] += m_JitterNDC.x * p[c][3];
                p[c][1] += m_JitterNDC.y * p[c][3];
            }
        return p;
    }

    // ------------------------------------------------------------------ SSR + bulutlar + hacimsel sis -> HDR

    void PostProcessStack::RunSceneEffects(const PostProcessSettings& settings, const RenderCamera& camera, float time)
    {
        const bool ssr = settings.ScreenSpaceReflections && !camera.Orthographic && GetSceneNormalTexture() != 0;
        const bool gi = settings.ScreenSpaceGI && !camera.Orthographic && GetSceneNormalTexture() != 0 && GetSceneAlbedoTexture() != 0;
        const bool clouds = settings.VolumetricClouds && !camera.Orthographic;
        const bool fog = settings.VolumetricFog && !camera.Orthographic;
        if (!ssr && !gi && !clouds && !fog)
            return;
        auto combine = PostProcess::GetShader("SceneFX");
        if (!Valid(combine))
            return;
        CQ_PROFILE_SAMPLE("Render.PostProcess.SceneFX", Rendering);

        const glm::mat4 projection = JitteredProjection(camera);
        const glm::mat4 invViewProj = glm::inverse(projection * camera.View);
        const uint32_t depth = GetSceneDepthTexture();
        const uint32_t halfW = std::max(m_Width / 2, 1u), halfH = std::max(m_Height / 2, 1u);

        bool ssrDone = false, cloudsDone = false, fogDone = false, giDone = false;
        if (gi)
        {
            auto shader = PostProcess::GetShader("SSGI");
            if (Valid(shader))
            {
                CQ_PROFILE_SAMPLE("Render.PostProcess.SSGI", Rendering);
                EnsureTarget(m_GI, halfW, halfH, GL_RGBA16F);
                EnsureTarget(m_GITemp, halfW, halfH, GL_RGBA16F);
                EnsureTarget(m_GIHistory[0], halfW, halfH, GL_RGBA16F);
                EnsureTarget(m_GIHistory[1], halfW, halfH, GL_RGBA16F);
                const glm::vec2 texel(1.0f / (float)halfW, 1.0f / (float)halfH);
                shader->Bind();
                shader->SetInt("u_Scene", 0);
                shader->SetInt("u_Depth", 1);
                shader->SetInt("u_GNormal", 2);
                shader->SetInt("u_Source", 3);
                shader->SetInt("u_History", 4);
                shader->SetMat4("u_Projection", projection);
                shader->SetMat4("u_InvProjection", glm::inverse(projection));
                shader->SetMat4("u_View", camera.View);
                shader->SetMat4("u_InvViewProj", invViewProj);
                shader->SetMat4("u_PrevViewProj", m_HasPrevViewProj ? m_PrevViewProj : camera.Projection * camera.View);
                shader->SetFloat("u_Radius", std::max(settings.SSGIRadius, 0.1f));
                shader->SetInt("u_Rays", std::clamp(settings.SSGIRays, 1, 16));
                shader->SetFloat("u_Frame", (float)(m_FrameIndex % 64u));
                shader->SetFloat("u_Feedback", 0.88f);
                shader->SetFloat2("u_Texel", texel);
                glViewport(0, 0, halfW, halfH);
                BindTexture(0, m_Source);
                BindTexture(1, depth);
                BindTexture(2, GetSceneNormalTexture());
                // 1) İz
                glBindFramebuffer(GL_FRAMEBUFFER, m_GITemp.Framebuffer);
                shader->SetInt("u_Mode", 0);
                PostProcess::DrawFullscreenQuad();
                // 2) Zamansal birikim (önceki karelerle)
                const int next = 1 - m_GIHistoryIndex;
                glBindFramebuffer(GL_FRAMEBUFFER, m_GIHistory[next].Framebuffer);
                shader->SetInt("u_Mode", 1);
                shader->SetInt("u_HistoryValid", (m_GIHistoryValid && m_HasPrevViewProj) ? 1 : 0);
                BindTexture(3, m_GITemp.Texture);
                BindTexture(4, m_GIHistory[m_GIHistoryIndex].Texture);
                PostProcess::DrawFullscreenQuad();
                m_GIHistoryIndex = next;
                m_GIHistoryValid = true;
                // 3) Uzamsal süzgeç
                glBindFramebuffer(GL_FRAMEBUFFER, m_GI.Framebuffer);
                shader->SetInt("u_Mode", 2);
                BindTexture(3, m_GIHistory[next].Texture);
                PostProcess::DrawFullscreenQuad();
                Renderer::GetStats().PostProcessPasses += 3;
                giDone = true;
            }
        }
        if (ssr)
        {
            auto shader = PostProcess::GetShader("SSR");
            if (Valid(shader))
            {
                EnsureTarget(m_SSR, halfW, halfH, GL_RGBA16F);
                glBindFramebuffer(GL_FRAMEBUFFER, m_SSR.Framebuffer);
                glViewport(0, 0, m_SSR.Width, m_SSR.Height);
                shader->Bind();
                shader->SetInt("u_Scene", 0);
                shader->SetInt("u_Depth", 1);
                shader->SetInt("u_GNormal", 2);
                shader->SetMat4("u_Projection", projection);
                shader->SetMat4("u_InvProjection", glm::inverse(projection));
                shader->SetMat4("u_View", camera.View);
                shader->SetFloat("u_MaxDistance", std::max(settings.SSRMaxDistance, 1.0f));
                shader->SetFloat("u_Thickness", std::max(settings.SSRThickness, 0.01f));
                shader->SetFloat("u_MaxRoughness", std::clamp(settings.SSRMaxRoughness, 0.01f, 1.0f));
                shader->SetFloat("u_Time", time);
                BindTexture(0, m_Source);
                BindTexture(1, depth);
                BindTexture(2, GetSceneNormalTexture());
                PostProcess::DrawFullscreenQuad();
                Renderer::GetStats().PostProcessPasses++;
                ssrDone = true;
            }
        }
        if (clouds)
        {
            auto shader = PostProcess::GetShader("Clouds");
            uint32_t noise = CloudNoiseTexture();
            if (Valid(shader) && noise)
            {
                EnsureTarget(m_Clouds, std::max(m_Width / 4, 1u), std::max(m_Height / 4, 1u), GL_RGBA16F);
                glBindFramebuffer(GL_FRAMEBUFFER, m_Clouds.Framebuffer);
                glViewport(0, 0, m_Clouds.Width, m_Clouds.Height);
                // Rüzgâr: kayma kare kare birikir (hız / yön değişince bulutlar sıçramaz)
                const float rad = glm::radians(settings.CloudWindDirection);
                m_CloudWind += glm::vec2(std::cos(rad), std::sin(rad)) * settings.CloudWindSpeed * m_DeltaTime;
                if (glm::length(m_CloudWind) > 1.0e6f)
                    m_CloudWind = glm::mod(m_CloudWind, glm::vec2(9000.0f * 64.0f));
                shader->Bind();
                shader->SetInt("u_Noise", 0);
                shader->SetInt("u_Depth", 1);
                shader->SetMat4("u_InvViewProj", invViewProj);
                shader->SetFloat3("u_CameraPos", camera.Position);
                shader->SetFloat("u_Coverage", std::clamp(settings.CloudCoverage, 0.0f, 1.0f));
                shader->SetFloat("u_Density", std::max(settings.CloudDensity, 0.0f));
                shader->SetFloat("u_Bottom", std::max(settings.CloudBottom, 10.0f));
                shader->SetFloat("u_Thickness", std::max(settings.CloudThickness, 50.0f));
                shader->SetFloat("u_Scale", std::max(settings.CloudScale, 0.05f));
                shader->SetFloat2("u_Wind", m_CloudWind);
                shader->SetFloat3("u_CloudColor", settings.CloudColor);
                shader->SetFloat("u_Time", time);
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, noise);
                BindTexture(1, depth);
                PostProcess::DrawFullscreenQuad();
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, 0);
                Renderer::GetStats().PostProcessPasses++;
                cloudsDone = true;
            }
        }
        if (fog)
        {
            auto shader = PostProcess::GetShader("VolumetricFog");
            if (Valid(shader))
            {
                EnsureTarget(m_Fog, halfW, halfH, GL_RGBA16F);
                glBindFramebuffer(GL_FRAMEBUFFER, m_Fog.Framebuffer);
                glViewport(0, 0, m_Fog.Width, m_Fog.Height);
                const ShadowPass& shadows = Renderer3D::GetShadowPass();
                const uint32_t shadowMap = shadows.HasValidData() ? shadows.GetAtlasTextureID() : 0;
                shader->Bind();
                shader->SetInt("u_Depth", 0);
                shader->SetInt("u_ShadowMap", 1);
                shader->SetMat4("u_InvViewProj", invViewProj);
                shader->SetMat4("u_View", camera.View);
                shader->SetFloat3("u_CameraPos", camera.Position);
                shader->SetFloat("u_Density", std::max(settings.VolumetricFogDensity, 0.0f));
                shader->SetFloat("u_HeightFalloff", std::max(settings.VolumetricFogHeightFalloff, 0.0f));
                shader->SetFloat("u_BaseHeight", settings.VolumetricFogBaseHeight);
                shader->SetFloat("u_Anisotropy", settings.VolumetricFogAnisotropy);
                shader->SetFloat("u_Intensity", std::max(settings.VolumetricFogIntensity, 0.0f));
                shader->SetFloat("u_MaxDistance", std::max(settings.VolumetricFogDistance, 1.0f));
                shader->SetFloat3("u_Albedo", settings.VolumetricFogAlbedo);
                shader->SetFloat("u_Ambient", std::max(settings.VolumetricFogAmbient, 0.0f));
                shader->SetInt("u_Steps", 32);
                shader->SetInt("u_UseShadows", shadowMap ? 1 : 0);
                shader->SetFloat("u_Time", time);
                BindTexture(0, depth);
                BindTexture(1, shadowMap);
                PostProcess::DrawFullscreenQuad();
                Renderer::GetStats().PostProcessPasses++;
                fogDone = true;
            }
        }

        RenderTarget& out = NextHDRTarget();
        glBindFramebuffer(GL_FRAMEBUFFER, out.Framebuffer);
        glViewport(0, 0, out.Width, out.Height);
        const bool ao = settings.AmbientOcclusion && m_AOValid;
        combine->Bind();
        combine->SetInt("u_Scene", 0);
        combine->SetInt("u_Depth", 1);
        combine->SetInt("u_AO", 2);
        combine->SetInt("u_SSR", 3);
        combine->SetInt("u_GNormal", 4);
        combine->SetInt("u_Clouds", 5);
        combine->SetInt("u_Fog", 6);
        combine->SetInt("u_GI", 7);
        combine->SetInt("u_GAlbedo", 8);
        combine->SetInt("u_UseGI", giDone ? 1 : 0);
        combine->SetFloat("u_GIIntensity", std::max(settings.SSGIIntensity, 0.0f));
        combine->SetInt("u_UseAO", ao ? 1 : 0);
        combine->SetInt("u_UseSSR", ssrDone ? 1 : 0);
        combine->SetInt("u_UseClouds", cloudsDone ? 1 : 0);
        combine->SetInt("u_UseFog", fogDone ? 1 : 0);
        combine->SetFloat("u_SSRIntensity", std::max(settings.SSRIntensity, 0.0f));
        combine->SetMat4("u_InvViewProj", invViewProj);
        combine->SetFloat3("u_CameraPos", camera.Position);
        combine->SetFloat2("u_FullSize", glm::vec2((float)m_Width, (float)m_Height));
        combine->SetFloat2("u_FogSize", glm::vec2((float)std::max(m_Fog.Width, 1u), (float)std::max(m_Fog.Height, 1u)));
        BindTexture(0, m_Source);
        BindTexture(1, depth);
        BindTexture(2, m_AOBlur.Texture);
        BindTexture(3, ssrDone ? m_SSR.Texture : 0);
        BindTexture(4, ssrDone ? GetSceneNormalTexture() : 0);
        BindTexture(5, cloudsDone ? m_Clouds.Texture : 0);
        BindTexture(6, fogDone ? m_Fog.Texture : 0);
        BindTexture(7, giDone ? m_GI.Texture : 0);
        BindTexture(8, giDone ? GetSceneAlbedoTexture() : 0);
        PostProcess::DrawFullscreenQuad();
        Renderer::GetStats().PostProcessPasses++;
        m_Source = out.Texture;
        m_AOApplied = ao;
    }

    // ------------------------------------------------------------------ Temporal AA

    void PostProcessStack::RunTAA(const PostProcessSettings& settings, const RenderCamera& camera)
    {
        auto shader = PostProcess::GetShader("TAA");
        if (!Valid(shader))
            return;
        CQ_PROFILE_SAMPLE("Render.PostProcess.TAA", Rendering);
        RenderTarget& prev = m_History[m_HistoryIndex];
        RenderTarget& next = m_History[1 - m_HistoryIndex];
        EnsureTarget(prev, m_Width, m_Height, GL_RGBA16F);
        EnsureTarget(next, m_Width, m_Height, GL_RGBA16F);
        glBindFramebuffer(GL_FRAMEBUFFER, next.Framebuffer);
        glViewport(0, 0, next.Width, next.Height);
        shader->Bind();
        shader->SetInt("u_Current", 0);
        shader->SetInt("u_History", 1);
        shader->SetInt("u_Depth", 2);
        shader->SetMat4("u_InvViewProj", glm::inverse(JitteredProjection(camera) * camera.View));
        shader->SetMat4("u_PrevViewProj", m_HasPrevViewProj ? m_PrevViewProj : camera.Projection * camera.View);
        shader->SetFloat2("u_Texel", glm::vec2(1.0f / (float)m_Width, 1.0f / (float)m_Height));
        shader->SetFloat2("u_JitterUV", m_JitterActive ? m_JitterNDC * 0.5f : glm::vec2(0.0f));
        shader->SetFloat("u_Feedback", std::clamp(settings.TAAFeedback, 0.5f, 0.97f));
        shader->SetInt("u_HistoryValid", (m_HistoryValid && m_HasPrevViewProj) ? 1 : 0);
        BindTexture(0, m_Source);
        BindTexture(1, prev.Texture);
        BindTexture(2, GetSceneDepthTexture());
        PostProcess::DrawFullscreenQuad();
        Renderer::GetStats().PostProcessPasses++;
        m_Source = next.Texture;
        m_HistoryIndex = 1 - m_HistoryIndex;
        m_HistoryValid = true;
        m_TAAResolved = true;
    }

    // ------------------------------------------------------------------ Alan derinliği

    void PostProcessStack::RunDepthOfField(const PostProcessSettings& settings, const RenderCamera& camera)
    {
        auto shader = PostProcess::GetShader("DepthOfField");
        if (!Valid(shader))
            return;
        CQ_PROFILE_SAMPLE("Render.PostProcess.DOF", Rendering);
        const uint32_t halfW = std::max(m_Width / 2, 1u), halfH = std::max(m_Height / 2, 1u);
        EnsureTarget(m_DOFHalf, halfW, halfH, GL_RGBA16F);
        EnsureTarget(m_DOFBlur, halfW, halfH, GL_RGBA16F);
        const uint32_t depth = GetSceneDepthTexture();
        shader->Bind();
        shader->SetInt("u_Source", 0);
        shader->SetInt("u_Blurred", 1);
        shader->SetInt("u_Depth", 2);
        shader->SetFloat("u_Near", camera.NearClip);
        shader->SetFloat("u_Far", camera.FarClip);
        shader->SetFloat("u_FocusDistance", std::max(settings.FocusDistance, 0.01f));
        shader->SetFloat("u_Aperture", std::max(settings.Aperture, 0.5f));
        shader->SetFloat("u_FocalLength", std::max(settings.FocalLength, 1.0f));
        shader->SetFloat("u_MaxBlur", std::max(settings.DOFMaxBlur, 0.0f));
        shader->SetFloat("u_ScreenHeight", (float)m_Height);
        shader->SetInt("u_Orthographic", camera.Orthographic ? 1 : 0);
        BindTexture(2, depth);

        // 1) CoC + yarım çözünürlüğe indirme
        glBindFramebuffer(GL_FRAMEBUFFER, m_DOFHalf.Framebuffer);
        glViewport(0, 0, halfW, halfH);
        shader->SetInt("u_Mode", 0);
        shader->SetFloat2("u_Texel", glm::vec2(1.0f / (float)m_Width, 1.0f / (float)m_Height));
        BindTexture(0, m_Source);
        PostProcess::DrawFullscreenQuad();
        // 2) Disk toplama bulanıklığı
        glBindFramebuffer(GL_FRAMEBUFFER, m_DOFBlur.Framebuffer);
        shader->SetInt("u_Mode", 1);
        shader->SetFloat2("u_Texel", glm::vec2(1.0f / (float)halfW, 1.0f / (float)halfH));
        BindTexture(0, m_DOFHalf.Texture);
        PostProcess::DrawFullscreenQuad();
        // 3) Keskin sahneyle CoC'ye göre birleştir
        RenderTarget& out = NextHDRTarget();
        glBindFramebuffer(GL_FRAMEBUFFER, out.Framebuffer);
        glViewport(0, 0, out.Width, out.Height);
        shader->SetInt("u_Mode", 2);
        shader->SetFloat2("u_Texel", glm::vec2(1.0f / (float)m_Width, 1.0f / (float)m_Height));
        BindTexture(0, m_Source);
        BindTexture(1, m_DOFBlur.Texture);
        PostProcess::DrawFullscreenQuad();
        Renderer::GetStats().PostProcessPasses += 3;
        m_Source = out.Texture;
    }

    // ------------------------------------------------------------------ Hareket bulanıklığı

    void PostProcessStack::RunMotionBlur(const PostProcessSettings& settings, const RenderCamera& camera)
    {
        auto shader = PostProcess::GetShader("MotionBlur");
        if (!Valid(shader) || !m_HasPrevViewProj || settings.MotionBlurIntensity <= 0.0f)
            return;
        CQ_PROFILE_SAMPLE("Render.PostProcess.MotionBlur", Rendering);
        RenderTarget& out = NextHDRTarget();
        glBindFramebuffer(GL_FRAMEBUFFER, out.Framebuffer);
        glViewport(0, 0, out.Width, out.Height);
        shader->Bind();
        shader->SetInt("u_Source", 0);
        shader->SetInt("u_Depth", 1);
        shader->SetMat4("u_InvViewProj", glm::inverse(JitteredProjection(camera) * camera.View));
        shader->SetMat4("u_PrevViewProj", m_PrevViewProj);
        shader->SetFloat("u_Intensity", std::clamp(settings.MotionBlurIntensity, 0.0f, 2.0f));
        shader->SetFloat("u_MaxPixels", std::max(settings.MotionBlurMax, 1.0f) * (float)m_Height / 1080.0f);
        shader->SetFloat2("u_Texel", glm::vec2(1.0f / (float)m_Width, 1.0f / (float)m_Height));
        BindTexture(0, m_Source);
        BindTexture(1, GetSceneDepthTexture());
        PostProcess::DrawFullscreenQuad();
        Renderer::GetStats().PostProcessPasses++;
        m_Source = out.Texture;
    }

    // ------------------------------------------------------------------ Otomatik pozlama

    void PostProcessStack::RunAutoExposure(const PostProcessSettings& settings, float deltaTime)
    {
        auto shader = PostProcess::GetShader("AutoExposure");
        if (!Valid(shader))
            return;
        CQ_PROFILE_SAMPLE("Render.PostProcess.AutoExposure", Rendering);
        const int lumSize = 128;
        const int lumMips = 8;   // 128 -> 1
        if (!m_LumTexture)
        {
            glGenTextures(1, &m_LumTexture);
            glBindTexture(GL_TEXTURE_2D, m_LumTexture);
            glTexStorage2D(GL_TEXTURE_2D, lumMips, GL_RG32F, lumSize, lumSize);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glGenFramebuffers(1, &m_LumFramebuffer);
            glBindFramebuffer(GL_FRAMEBUFFER, m_LumFramebuffer);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_LumTexture, 0);
        }
        EnsureTarget(m_Adapt[0], 1, 1, GL_R32F);
        EnsureTarget(m_Adapt[1], 1, 1, GL_R32F);

        shader->Bind();
        shader->SetInt("u_Source", 0);
        shader->SetInt("u_Previous", 1);
        // 1) Log parlaklık (128x128) + mip zinciriyle ortalama
        glBindFramebuffer(GL_FRAMEBUFFER, m_LumFramebuffer);
        glViewport(0, 0, lumSize, lumSize);
        shader->SetInt("u_Mode", 0);
        BindTexture(0, m_Source);
        PostProcess::DrawFullscreenQuad();
        glBindTexture(GL_TEXTURE_2D, m_LumTexture);
        glGenerateMipmap(GL_TEXTURE_2D);
        // 2) Zamanla uyum (1x1, önceki değerden)
        const int next = 1 - m_AdaptIndex;
        glBindFramebuffer(GL_FRAMEBUFFER, m_Adapt[next].Framebuffer);
        glViewport(0, 0, 1, 1);
        shader->SetInt("u_Mode", 1);
        shader->SetFloat("u_MaxLod", (float)(lumMips - 1));
        shader->SetFloat("u_DeltaTime", deltaTime);
        shader->SetFloat("u_SpeedUp", settings.AutoExposureSpeedUp);
        shader->SetFloat("u_SpeedDown", settings.AutoExposureSpeedDown);
        shader->SetInt("u_Reset", m_AdaptValid ? 0 : 1);
        BindTexture(0, m_LumTexture);
        BindTexture(1, m_Adapt[m_AdaptIndex].Texture);
        PostProcess::DrawFullscreenQuad();
        Renderer::GetStats().PostProcessPasses += 2;
        m_AdaptIndex = next;
        m_AdaptValid = true;
        m_UseAutoExposure = true;
    }
}
