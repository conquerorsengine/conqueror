#include "PostProcess.h"
#include "Core/Console/ConsoleVariables.h"
#include "Core/Profiling/FrameProfiler.h"
#include "Core/Logging/Log.h"
#include "Renderer/Renderer.h"
#include "Renderer/Utilities/Renderer3D/Renderer3D.h"

#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <limits>
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
                          &m_GI, &m_GITemp, &m_GIHistory[0], &m_GIHistory[1], &m_CloudHistory[0], &m_CloudHistory[1], &m_TAAState[0], &m_TAAState[1],
                          &m_Velocity, &m_TAADebug, &m_Reactive, &m_StabHistory[0], &m_StabHistory[1], &m_StabState[0], &m_StabState[1], &m_FSR2Reactive, &m_FSR2PrevShadow })
            DestroyTarget(*rt);
        m_StabValid = false;
        if (m_VelocityFramebuffer)
            glDeleteFramebuffers(1, &m_VelocityFramebuffer);
        m_VelocityFramebuffer = 0;
        if (m_ReactiveFramebuffer)
            glDeleteFramebuffers(1, &m_ReactiveFramebuffer);
        m_ReactiveFramebuffer = 0;
        for (int i = 0; i < 2; ++i)
        {
            if (m_TAAFramebuffer[i])
                glDeleteFramebuffers(1, &m_TAAFramebuffer[i]);
            m_TAAFramebuffer[i] = 0;
            if (m_StabFramebuffer[i])
                glDeleteFramebuffers(1, &m_StabFramebuffer[i]);
            m_StabFramebuffer[i] = 0;
        }
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
        if (!clouds)
            m_CloudHistoryValid = false;
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
        uint32_t cloudsTexture = 0;
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
                // Işın gürültüsü her karede değişir (TAA birikimi 64 deseni ortalar). TAA olmayan kipte (FXAA, SMAA, MSAA ...)
                // bu değişim ortalanmıyordu: kamera dururken bile küplerin arası ve köşeler (dolaylı ışığın en etkili olduğu
                // yerler) kare kare yanıp sönüyordu. Titreşimsiz kipte kamera dururken desen sabit: görüntü kare kare aynı
                bool cameraStill = m_HasPrevViewProj;
                {
                    const glm::mat4 now = camera.Projection * camera.View;
                    for (int c = 0; c < 4 && cameraStill; ++c)
                        for (int r = 0; r < 4; ++r)
                            if (std::abs(now[c][r] - m_PrevViewProj[c][r]) > 1e-6f * std::max(1.0f, std::abs(now[c][r])))
                            {
                                cameraStill = false;
                                break;
                            }
                }
                const bool fixedNoise = !m_JitterActive && cameraStill;
                shader->SetFloat("u_Frame", fixedNoise ? 0.0f : (float)(m_FrameIndex % 64u));
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
                cloudsTexture = m_Clouds.Texture;

                // Zamansal birikim: önceki karenin bulutları yön uzayında yeniden izdüşürülüp harmanlanır (kamera
                // dönerken / yürürken ufka yakın bulutlar kendi kendine oynamaz, ışın yürütme gürültüsü ortalanır)
                auto resolve = PostProcess::GetShader("CloudsResolve");
                if (Valid(resolve))
                {
                    const int next = 1 - m_CloudHistoryIndex;
                    EnsureTarget(m_CloudHistory[0], m_Clouds.Width, m_Clouds.Height, GL_RGBA16F);
                    EnsureTarget(m_CloudHistory[1], m_Clouds.Width, m_Clouds.Height, GL_RGBA16F);
                    glBindFramebuffer(GL_FRAMEBUFFER, m_CloudHistory[next].Framebuffer);
                    glViewport(0, 0, m_CloudHistory[next].Width, m_CloudHistory[next].Height);
                    resolve->Bind();
                    resolve->SetInt("u_Current", 0);
                    resolve->SetInt("u_History", 1);
                    resolve->SetMat4("u_InvViewProj", invViewProj);
                    resolve->SetMat4("u_PrevViewProj", m_PrevCloudViewProj);
                    resolve->SetFloat3("u_CameraPos", camera.Position);
                    resolve->SetFloat2("u_Texel", glm::vec2(1.0f / (float)m_Clouds.Width, 1.0f / (float)m_Clouds.Height));
                    resolve->SetInt("u_HistoryValid", m_CloudHistoryValid ? 1 : 0);
                    resolve->SetFloat("u_Feedback", 0.9f);
                    BindTexture(0, m_Clouds.Texture);
                    BindTexture(1, m_CloudHistory[m_CloudHistoryIndex].Texture);
                    PostProcess::DrawFullscreenQuad();
                    Renderer::GetStats().PostProcessPasses++;
                    m_CloudHistoryIndex = next;
                    m_CloudHistoryValid = true;
                    m_PrevCloudViewProj = projection * camera.View;
                    cloudsTexture = m_CloudHistory[next].Texture;
                }
            }
        }
        if (!cloudsDone)
            m_CloudHistoryValid = false;
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
        BindTexture(5, cloudsDone ? cloudsTexture : 0);
        BindTexture(6, fogDone ? m_Fog.Texture : 0);
        BindTexture(7, giDone ? m_GI.Texture : 0);
        BindTexture(8, giDone ? GetSceneAlbedoTexture() : 0);
        PostProcess::DrawFullscreenQuad();
        Renderer::GetStats().PostProcessPasses++;
        m_Source = out.Texture;
        m_AOApplied = ao;
    }

    // ------------------------------------------------------------------ Temporal AA

    // Nesne hareket vektörleri: bu karede hareket eden opak nesneler sahne derinliğine kendi önceki konumlarıyla çizilir.
    // Kameraya bağlı silah / eller kamera hareketiyle değil kendi hareketleriyle yeniden izdüşürülür (zıplarken, dönerken
    // dalgalanmaz); düşen küp, hedef gibi nesneler geçmişini doğru yerden alır. Yazılmayan piksel: kamera hareketi (derinlik).
    // TAA ve SMAA T2x çözümü kullanır
    bool PostProcessStack::RenderObjectVelocity(const RenderCamera& camera)
    {
        bool hasVelocity = false;
        const glm::mat4 jitteredViewProj = JitteredProjection(camera) * camera.View;
        const glm::mat4 viewProj = camera.Projection * camera.View;
        const uint32_t sceneDepth = GetSceneDepthTexture();
        if (sceneDepth && m_HasPrevViewProj && Renderer3D::HasVelocityDraws(this))
        {
            EnsureTarget(m_Velocity, m_Width, m_Height, GL_RGBA16F);
            if (!m_VelocityFramebuffer)
                glCreateFramebuffers(1, &m_VelocityFramebuffer);
            glNamedFramebufferTexture(m_VelocityFramebuffer, GL_COLOR_ATTACHMENT0, m_Velocity.Texture, 0);
            glNamedFramebufferTexture(m_VelocityFramebuffer, GL_DEPTH_STENCIL_ATTACHMENT, sceneDepth, 0);
            if (glCheckNamedFramebufferStatus(m_VelocityFramebuffer, GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE)
            {
                glBindFramebuffer(GL_FRAMEBUFFER, m_VelocityFramebuffer);
                glViewport(0, 0, m_Width, m_Height);
                const float none[4] = { 1.0e4f, 1.0e4f, 0.0f, 0.0f };   // hız yok: derinlikten kamera hareketi kullanılır
                glClearBufferfv(GL_COLOR, 0, none);
                hasVelocity = Renderer3D::RenderVelocity(this, jitteredViewProj, viewProj, m_PrevViewProj);
            }
        }
        return hasVelocity;
    }

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
        RenderTarget& prevState = m_TAAState[m_HistoryIndex];
        RenderTarget& nextState = m_TAAState[1 - m_HistoryIndex];
        EnsureTarget(prevState, m_Width, m_Height, GL_RGBA16F);
        EnsureTarget(nextState, m_Width, m_Height, GL_RGBA16F);
        const bool hasVelocity = RenderObjectVelocity(camera);
        // Geçmiş + durum tek geçişte (iki renk eki). Ekler her karede bağlanır: boyut değişince dokular yeniden oluşur ve
        // OpenGL silinen dokunun numarasını yeni dokuya verebilir; numaraya bakan önbellek eski (yetim) dokuya çizdirip her
        // ikinci kareyi siyah bırakıyordu
        const int ni = 1 - m_HistoryIndex;
        if (!m_TAAFramebuffer[ni])
            glCreateFramebuffers(1, &m_TAAFramebuffer[ni]);
        const bool taaDebug = ConsoleSystem::Overrides().TAADebug == 1;
        if (taaDebug)
            EnsureTarget(m_TAADebug, m_Width, m_Height, GL_RGBA16F);
        {
            const GLenum buffers[3] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2 };
            glNamedFramebufferDrawBuffers(m_TAAFramebuffer[ni], taaDebug ? 3 : 2, buffers);
        }
        glNamedFramebufferTexture(m_TAAFramebuffer[ni], GL_COLOR_ATTACHMENT0, next.Texture, 0);
        glNamedFramebufferTexture(m_TAAFramebuffer[ni], GL_COLOR_ATTACHMENT1, nextState.Texture, 0);
        glNamedFramebufferTexture(m_TAAFramebuffer[ni], GL_COLOR_ATTACHMENT2, taaDebug ? m_TAADebug.Texture : 0, 0);
        glBindFramebuffer(GL_FRAMEBUFFER, m_TAAFramebuffer[ni]);
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
        shader->SetInt("u_Debug", taaDebug ? 1 : 0);
        shader->SetInt("u_HistoryValid", (m_HistoryValid && m_HasPrevViewProj) ? 1 : 0);
        // Kamera kıpırdamıyor (titreşimsiz görüş-izdüşüm önceki kareyle aynı): sabit yüzeylerin hareketi tam sıfır. Derinlikten
        // yeniden izdüşüm uzak yüzeylerde kayan nokta hatasıyla piksel altı "hareket" üretiyor, piksel durağan sayılmıyor,
        // geçmiş birikmiyor ve uzak kenarlar (duvar üstü, küplerin arası) titreşim döngüsüyle yanıp sönüyordu
        bool cameraStill = m_HasPrevViewProj;
        {
            const glm::mat4 now = camera.Projection * camera.View;
            for (int c = 0; c < 4 && cameraStill; ++c)
                for (int r = 0; r < 4; ++r)
                    if (std::abs(now[c][r] - m_PrevViewProj[c][r]) > 1e-6f * std::max(1.0f, std::abs(now[c][r])))
                    {
                        cameraStill = false;
                        break;
                    }
        }
        m_CameraStill = cameraStill;
        shader->SetInt("u_CameraStill", cameraStill ? 1 : 0);
        // Nokta / spot ışığın gücü bu karede değişti (namlu alevi): TAA aydınlığı değişen piksellerde geçmişe az güvenir
        shader->SetInt("u_LightingChanged", Renderer3D::ConsumeLightingChange(this) ? 1 : 0);
        // Zamansal yöntem çeşidi (TSAA / TSSAA); TXAA ve TAA ana yolu kullanır
        shader->SetInt("u_Variant", m_TAAVariant);
        // Hareket eden nesnelerin ekran dikdörtgenleri (fazlası son dikdörtgende birleşir)
        {
            static std::vector<glm::vec4> s_Rects;
            Renderer3D::GetMovingRects(this, s_Rects);
            constexpr size_t kMax = 32;
            if (s_Rects.size() > kMax)
            {
                for (size_t i = kMax; i < s_Rects.size(); ++i)
                {
                    s_Rects[kMax - 1].x = std::min(s_Rects[kMax - 1].x, s_Rects[i].x);
                    s_Rects[kMax - 1].y = std::min(s_Rects[kMax - 1].y, s_Rects[i].y);
                    s_Rects[kMax - 1].z = std::max(s_Rects[kMax - 1].z, s_Rects[i].z);
                    s_Rects[kMax - 1].w = std::max(s_Rects[kMax - 1].w, s_Rects[i].w);
                }
                s_Rects.resize(kMax);
            }
            shader->SetInt("u_MovingCount", (int)s_Rects.size());
            for (size_t i = 0; i < s_Rects.size(); ++i)
                shader->SetFloat4("u_MovingRects[" + std::to_string(i) + "]", s_Rects[i]);
        }
        // Hareket eden nesnelerin dünya kutuları (gölge hacmi testi; fazlası son kutuda birleşir)
        {
            static std::vector<glm::vec4> s_ShadowBoxes;
            glm::vec3 towardSun(0.0f, 1.0f, 0.0f);
            if (!Renderer3D::GetMovingShadowBoxes(this, s_ShadowBoxes, towardSun))
                s_ShadowBoxes.clear();
            constexpr size_t kMaxBoxes = 32;
            if (s_ShadowBoxes.size() > kMaxBoxes * 2)
            {
                // Fazla kutular birbirine en yakın olanlar birleştirilerek azaltılır: her adımda birleşimi en az hacim
                // ekleyen çift (aynı nesnenin parçaları, bu ve önceki karedeki yeri). Fazlası eskiden hep son kutuda
                // birleşiyordu: uzaktaki hareketli hedeflerle oyuncunun silahı arasındaki bütün arena tek kutu oluyor, zemin
                // ve küpler "hareket eden gölgenin içinde" sayılıp geçmişe güvenilmiyor, oyunda yürürken küplerin arası
                // yanıp sönüyordu (Viewport'ta hareketli nesne olmadığı için orada olmuyordu)
                struct Box { glm::vec3 Lo, Hi; };
                static std::vector<Box> s_Merge;
                s_Merge.clear();
                for (size_t i = 0; i + 1 < s_ShadowBoxes.size(); i += 2)
                    s_Merge.push_back({ glm::vec3(s_ShadowBoxes[i]), glm::vec3(s_ShadowBoxes[i + 1]) });
                auto volume = [](const glm::vec3& lo, const glm::vec3& hi) {
                    const glm::vec3 d = glm::max(hi - lo, glm::vec3(0.05f));
                    return d.x * d.y * d.z;
                };
                while (s_Merge.size() > kMaxBoxes)
                {
                    size_t bestA = 0, bestB = 1;
                    float bestCost = std::numeric_limits<float>::max();
                    for (size_t a = 0; a < s_Merge.size(); ++a)
                    {
                        const float va = volume(s_Merge[a].Lo, s_Merge[a].Hi);
                        for (size_t b = a + 1; b < s_Merge.size(); ++b)
                        {
                            const float cost = volume(glm::min(s_Merge[a].Lo, s_Merge[b].Lo), glm::max(s_Merge[a].Hi, s_Merge[b].Hi))
                                             - va - volume(s_Merge[b].Lo, s_Merge[b].Hi);
                            if (cost < bestCost)
                            {
                                bestCost = cost;
                                bestA = a;
                                bestB = b;
                            }
                        }
                    }
                    s_Merge[bestA].Lo = glm::min(s_Merge[bestA].Lo, s_Merge[bestB].Lo);
                    s_Merge[bestA].Hi = glm::max(s_Merge[bestA].Hi, s_Merge[bestB].Hi);
                    s_Merge[bestB] = s_Merge.back();
                    s_Merge.pop_back();
                }
                s_ShadowBoxes.clear();
                for (const Box& b : s_Merge)
                {
                    s_ShadowBoxes.push_back(glm::vec4(b.Lo, 0.0f));
                    s_ShadowBoxes.push_back(glm::vec4(b.Hi, 0.0f));
                }
            }
            shader->SetInt("u_ShadowBoxCount", (int)(s_ShadowBoxes.size() / 2));
            shader->SetFloat3("u_TowardSun", towardSun);
            for (size_t i = 0; i < s_ShadowBoxes.size(); ++i)
                shader->SetFloat4("u_ShadowBoxes[" + std::to_string(i) + "]", s_ShadowBoxes[i]);
        }
        // Hareket bayrağı (normal ekinin w'si): TAA açıkken normal eki her zaman var
        const uint32_t normals = GetSceneNormalTexture();
        shader->SetInt("u_GNormal", 3);
        shader->SetInt("u_HasMotionFlag", normals ? 1 : 0);
        BindTexture(0, m_Source);
        BindTexture(1, prev.Texture);
        BindTexture(2, GetSceneDepthTexture());
        BindTexture(3, normals);
        shader->SetInt("u_State", 4);
        BindTexture(4, prevState.Texture);
        // Güneş gölgesi eki: gölgesi değişen yüzey (hareket eden nesnenin gölgesi) geçmişe güvenmez
        const uint32_t shadowMask = GetSceneShadowMaskTexture();
        shader->SetInt("u_GShadow", 5);
        shader->SetInt("u_HasShadowMask", shadowMask ? 1 : 0);
        BindTexture(5, shadowMask);
        shader->SetInt("u_Velocity", 6);
        shader->SetInt("u_HasVelocity", hasVelocity ? 1 : 0);
        BindTexture(6, hasVelocity ? m_Velocity.Texture : 0);
        // Parçacık tepki maskesi: bu karede duman / toz / kıvılcımın kapladığı pikseller
        shader->SetInt("u_Reactive", 7);
        shader->SetInt("u_HasReactive", m_ReactiveUsed ? 1 : 0);
        BindTexture(7, m_ReactiveUsed ? m_Reactive.Texture : 0);
        PostProcess::DrawFullscreenQuad();
        Renderer::GetStats().PostProcessPasses++;
        // Hata ayıklama görünümü: geçmiş etkilenmez, yalnız gösterilen görüntü renkli yol haritasıdır
        m_Source = taaDebug ? m_TAADebug.Texture : next.Texture;
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
