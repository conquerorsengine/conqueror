#include <sstream>
#include <fstream>
#include <filesystem>
#include "Core/Serialization/SceneSerializer.h"
#include "PostProcess.h"
#include "Core/Profiling/FrameProfiler.h"
#include "Core/Tiering/QualitySettings.h"
#include "Core/Logging/Log.h"
#include "RHI/RenderCommand.h"
#include "RHI/Texture.h"
#include "Renderer/Renderer.h"
#include "Renderer/Utilities/Renderer3D/Renderer3D.h"
#include "Renderer/AntiAliasing.h"
#include "Renderer/FSR2/FSR2Upscaler.h"
#include "Scene/Scene.h"

#include <glad/glad.h>
#include <glm/gtc/packing.hpp>
// SMAA alan / arama dokuları (Jimenez ve ark., MIT): iryoku/smaa Textures/*.h
#include "../../../External/smaa/Textures/AreaTex.h"
#include "../../../External/smaa/Textures/SearchTex.h"
#include "../../../External/NVIDIAImageScaling/NIS/NIS_Config.h"
#include "Renderer/TextureSlots.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdlib>
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

        // SMAA T2x titreşimi ve SMAA alt örnek indisleri. SMAA.hlsl: jitters { (0.25, -0.25), (-0.25, 0.25) } ile
        // subsampleIndices { (1, 1, 1, 0), (2, 2, 2, 0) } D3D doku uzayında (y aşağı); OpenGL doku uzayında y yukarı olduğundan
        // titreşimin y bileşeni ters: (0.25, 0.25) -> S0, (-0.25, -0.25) -> S1
        glm::vec2 SMAAT2xJitter(uint32_t frame)
        {
            return (frame & 1u) ? glm::vec2(-0.25f, -0.25f) : glm::vec2(0.25f, 0.25f);
        }

        glm::vec4 SMAAT2xSubsampleIndices(uint32_t frame)
        {
            return (frame & 1u) ? glm::vec4(2.0f, 2.0f, 2.0f, 0.0f) : glm::vec4(1.0f, 1.0f, 1.0f, 0.0f);
        }

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
        LoadShader("SMAAWeights", "Engine/src/Shaders/PostProcess/SMAA_Weights.cqsh");
        LoadShader("SMAABlend", "Engine/src/Shaders/PostProcess/SMAA_Blend.cqsh");
        LoadShader("SMAAT2xResolve", "Engine/src/Shaders/PostProcess/SMAA_T2xResolve.cqsh");
        LoadShader("TAAU", "Engine/src/Shaders/PostProcess/TAAU.cqsh");
        LoadShader("MotionVectors", "Engine/src/Shaders/PostProcess/MotionVectors.cqsh");
        LoadShader("GBAAResolve", "Engine/src/Shaders/PostProcess/GBAA_Resolve.cqsh");
        LoadShader("SRAAResolve", "Engine/src/Shaders/PostProcess/SRAA_Resolve.cqsh");
        LoadShader("MLAAEdge", "Engine/src/Shaders/PostProcess/MLAA_Edge.cqsh");
        LoadShader("MLAAWeights", "Engine/src/Shaders/PostProcess/MLAA_Weights.cqsh");
        LoadShader("DEAA", "Engine/src/Shaders/PostProcess/DEAA.cqsh");
        LoadShader("MSAAResolve", "Engine/src/Shaders/PostProcess/MSAAResolve.cqsh");
        LoadShader("Downsample", "Engine/src/Shaders/PostProcess/Downsample.cqsh");
        LoadShader("FSR_EASU", "Engine/src/Shaders/PostProcess/FSR_EASU.cqsh");
        LoadShader("FSR_RCAS", "Engine/src/Shaders/PostProcess/FSR_RCAS.cqsh");
        LoadShader("CAS", "Engine/src/Shaders/PostProcess/CAS.cqsh");
        LoadShader("CAS_Scale", "Engine/src/Shaders/PostProcess/CAS_Scale.cqsh");
        LoadShader("NIS", "Engine/src/Shaders/PostProcess/NIS.cqsh");
        LoadShader("CMAA2Edges", "Engine/src/Shaders/PostProcess/CMAA2_Edges.cqsh");
        LoadShader("CMAA2Args", "Engine/src/Shaders/PostProcess/CMAA2_Args.cqsh");
        LoadShader("CMAA2Process", "Engine/src/Shaders/PostProcess/CMAA2_Process.cqsh");
        LoadShader("CMAA2Apply", "Engine/src/Shaders/PostProcess/CMAA2_Apply.cqsh");
        LoadShader("HaloFlare", "Engine/src/Shaders/PostProcess/HaloFlare.cqsh");
        LoadShader("Bloom", "Engine/src/Shaders/PostProcess/Bloom.cqsh");
        LoadShader("SSAO", "Engine/src/Shaders/PostProcess/SSAO.cqsh");
        LoadShader("SSAOBlur", "Engine/src/Shaders/PostProcess/SSAOBlur.cqsh");
        LoadShader("Composite", "Engine/src/Shaders/PostProcess/Composite.cqsh");
        LoadShader("Sharpen", "Engine/src/Shaders/PostProcess/Sharpen.cqsh");
        LoadShader("SSR", "Engine/src/Shaders/PostProcess/SSR.cqsh");
        LoadShader("VolumetricFog", "Engine/src/Shaders/PostProcess/VolumetricFog.cqsh");
        LoadShader("Clouds", "Engine/src/Shaders/PostProcess/Clouds.cqsh");
        LoadShader("CloudsResolve", "Engine/src/Shaders/PostProcess/CloudsResolve.cqsh");
        LoadShader("CloudNoise", "Engine/src/Shaders/PostProcess/CloudNoise.cqsh");
        LoadShader("SceneFX", "Engine/src/Shaders/PostProcess/SceneFX.cqsh");
        LoadShader("TAA", "Engine/src/Shaders/PostProcess/TAA.cqsh");
        LoadShader("Stabilize", "Engine/src/Shaders/PostProcess/Stabilize.cqsh");
        LoadShader("RejectMix", "Engine/src/Shaders/PostProcess/RejectMix.cqsh");
        LoadShader("FSR2Reactive", "Engine/src/Shaders/PostProcess/FSR2Reactive.cqsh");
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

    PostProcessStack::PostProcessStack() = default;

    PostProcessStack::~PostProcessStack()
    {
        DestroyTarget(m_AO);
        DestroyTarget(m_AfterPass[0]);
        DestroyTarget(m_AfterPass[1]);
        DestroyTarget(m_AOBlur);
        for (auto& rt : m_BloomMips) DestroyTarget(rt);
        for (auto& rt : m_BloomUp) DestroyTarget(rt);
        DestroyTarget(m_LDR);
        DestroyTarget(m_SMAAEdges);
        DestroyTarget(m_SMAAWeights);
        DestroyTarget(m_Temp);
        DestroyTarget(m_Temp2);
        DestroyTarget(m_Upscale);
        DestroyTarget(m_T2xHistory[0]);
        DestroyTarget(m_T2xHistory[1]);
        DestroyTarget(m_TAAUHistory[0]);
        DestroyTarget(m_TAAUHistory[1]);
        DestroyTarget(m_MotionVectors);
        DestroyTarget(m_FSR2Output);
        m_FSR2.reset();
        DestroyTarget(m_GBAAEdges);
        if (m_GeometryFramebuffer) glDeleteFramebuffers(1, &m_GeometryFramebuffer);
        DestroySRAATargets();
        DestroyCoverageFramebuffer();
        if (m_SMAAAreaTex) glDeleteTextures(1, &m_SMAAAreaTex);
        if (m_SMAASearchTex) glDeleteTextures(1, &m_SMAASearchTex);
        if (m_NISCoefScaler) glDeleteTextures(1, &m_NISCoefScaler);
        if (m_NISCoefUSM) glDeleteTextures(1, &m_NISCoefUSM);
        if (m_NISConstants) glDeleteBuffers(1, &m_NISConstants);
        DestroyCMAA2Resources();
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
        uint32_t samples = m_WantSamples > 0 ? m_WantSamples : 1;
        // Temporal AA kendi kenar yumuşatmasını yapar: MSAA ile birlikte gereksiz maliyet (TXAA hariç: MSAA + zamansal süzgeç)
        if (m_WantTAA && !m_WantTXAA && !m_WantMultisampleTAA)
            samples = 1;
        // EQAA / CSAA: sahne ayrı (kapsam örnekli renderbuffer) çerçeve tamponuna çizilir; sahne hedefi çözülmüş tek örnek
        if (m_WantCoverageSamples > 0)
            samples = 1;
        samples = std::min<uint32_t>(samples, (uint32_t)std::max(1, AntiAliasing::GetCaps().MaxSamples));
        // Albedo eki normal ekini gerektirir (ek sırası sabit: 1 normal, 2 albedo). Temporal AA da normal ekinin w'sindeki
        // hareket bayrağını okur (hareket eden nesne iz bırakmasın)
        const bool wantNormals = m_WantNormals || m_WantAlbedo || m_WantTAA || m_WantGeometry;
        // TAA: güneş gölgesi eki (3) de yazılır; ek sırası sabit olduğundan albedo eki (2) de oluşturulur
        const bool wantShadowMask = m_WantTAA;
        const bool wantAlbedo = m_WantAlbedo || wantShadowMask;
        const bool coverageOk = m_CoverageSamples == m_WantCoverageSamples;
        if (m_SceneFramebuffer && m_Width == width && m_Height == height && m_Samples == samples && m_HasNormals == wantNormals &&
            m_HasAlbedo == wantAlbedo && m_HasShadowMask == wantShadowMask && coverageOk)
            return;

        if (m_Width != width || m_Height != height)
        {
            m_HistoryValid = false;
            m_StabValid = false;
            m_HasPrevViewProj = false;
            m_GIHistoryValid = false;
            m_CloudHistoryValid = false;
        }
        m_Width = width;
        m_Height = height;
        m_Samples = samples;
        m_HasNormals = wantNormals;
        m_HasAlbedo = wantAlbedo;
        m_HasShadowMask = wantShadowMask;

        FramebufferSpecification spec;
        spec.Width = width;
        spec.Height = height;
        // SSR / SSGI açıkken ek renk ekleri: PBR shader'ları normal / pürüzlülük / metaliklik ve albedo yazar
        if (m_HasShadowMask)
            spec.Attachments = { FramebufferTextureFormat::RGBA16F, FramebufferTextureFormat::RGBA16F, FramebufferTextureFormat::RGBA8, FramebufferTextureFormat::R8,
                                 FramebufferTextureFormat::Depth };
        else if (m_HasAlbedo)
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
        CreateTarget(m_SMAAWeights, width, height, GL_RGBA8);
        CreateTarget(m_Temp, width, height, GL_RGBA8);
        CreateTarget(m_Temp2, width, height, GL_RGBA8);

        // EQAA / CSAA çerçeve tamponu (sahne hedefinin eklerinin kapsam örnekli kopyası)
        DestroyCoverageFramebuffer();
        m_CoverageSamples = 0;
        if (m_WantCoverageSamples > 0)
        {
            if (CreateCoverageFramebuffer(width, height))
                m_CoverageSamples = m_WantCoverageSamples;
            else
            {
                CQ_CORE_WARN("PostProcess: could not create the coverage-sampled (EQAA / CSAA) framebuffer; anti-aliasing is off");
                m_WantCoverageSamples = 0;
            }
        }
    }

    bool PostProcessStack::CreateCoverageFramebuffer(uint32_t width, uint32_t height)
    {
        const AntiAliasing::Caps& caps = AntiAliasing::GetCaps();
        const uint32_t coverage = m_WantCoverageSamples;
        // Renk depolama örneği kapsamın yarısı (EQAA 8 kapsam / 4 renk; CSAA 16 kapsam / 8 renk)
        const uint32_t color = std::max(1u, coverage / 2);
        m_CoverageNV = !caps.AMDAdvancedMS && caps.NVCoverage;
        if (!caps.AMDAdvancedMS && !caps.NVCoverage)
            return false;
        std::vector<GLenum> formats = { GL_RGBA16F };
        if (m_HasNormals) formats.push_back(GL_RGBA16F);
        if (m_HasAlbedo) formats.push_back(GL_RGBA8);
        if (m_HasShadowMask) formats.push_back(GL_R8);
        glCreateFramebuffers(1, &m_CoverageFramebuffer);
        std::vector<GLenum> drawBuffers;
        for (size_t i = 0; i <= formats.size(); ++i)
        {
            const bool depth = i == formats.size();
            GLuint rb = 0;
            glCreateRenderbuffers(1, &rb);
            const GLenum fmt = depth ? GL_DEPTH24_STENCIL8 : formats[i];
            if (m_CoverageNV)
            {
                glBindRenderbuffer(GL_RENDERBUFFER, rb);
                glRenderbufferStorageMultisampleCoverageNV(GL_RENDERBUFFER, (GLsizei)coverage, (GLsizei)(depth ? coverage : color), fmt, (GLsizei)width, (GLsizei)height);
                glBindRenderbuffer(GL_RENDERBUFFER, 0);
            }
            else
            {
                // Derinlik / şablonda depolama örneği kapsama eşit olmalı (AMD_framebuffer_multisample_advanced)
                glNamedRenderbufferStorageMultisampleAdvancedAMD(rb, (GLsizei)coverage, (GLsizei)(depth ? coverage : color), fmt, (GLsizei)width, (GLsizei)height);
            }
            glNamedFramebufferRenderbuffer(m_CoverageFramebuffer, depth ? GL_DEPTH_STENCIL_ATTACHMENT : (GLenum)(GL_COLOR_ATTACHMENT0 + i), GL_RENDERBUFFER, rb);
            if (!depth)
                drawBuffers.push_back((GLenum)(GL_COLOR_ATTACHMENT0 + i));
            m_CoverageRenderbuffers.push_back(rb);
        }
        glNamedFramebufferDrawBuffers(m_CoverageFramebuffer, (GLsizei)drawBuffers.size(), drawBuffers.data());
        const GLenum status = glCheckNamedFramebufferStatus(m_CoverageFramebuffer, GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE)
        {
            CQ_CORE_WARN("PostProcess: {0} framebuffer incomplete (0x{1:x}), coverage {2} / color {3}", m_CoverageNV ? "CSAA" : "EQAA", (unsigned)status, coverage, color);
            DestroyCoverageFramebuffer();
            return false;
        }
        m_CoverageColorSamples = color;
        CQ_CORE_INFO("PostProcess: {0} {1} coverage / {2} color samples ({3}x{4})", m_CoverageNV ? "CSAA" : "EQAA", coverage, color, width, height);
        return true;
    }

    void PostProcessStack::DestroyCoverageFramebuffer()
    {
        if (!m_CoverageRenderbuffers.empty())
            glDeleteRenderbuffers((GLsizei)m_CoverageRenderbuffers.size(), m_CoverageRenderbuffers.data());
        m_CoverageRenderbuffers.clear();
        if (m_CoverageFramebuffer)
            glDeleteFramebuffers(1, &m_CoverageFramebuffer);
        m_CoverageFramebuffer = 0;
    }

    void PostProcessStack::EnsureSMAATextures()
    {
        if (m_SMAAAreaTex && m_SMAASearchTex)
            return;
        // Satırlar bellekteki sırayla yüklenir (v = 0 ilk satır); SMAA kodu bu düzeni kendi koordinat hesabıyla bekler
        glCreateTextures(GL_TEXTURE_2D, 1, &m_SMAAAreaTex);
        glTextureStorage2D(m_SMAAAreaTex, 1, GL_RG8, AREATEX_WIDTH, AREATEX_HEIGHT);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTextureSubImage2D(m_SMAAAreaTex, 0, 0, 0, AREATEX_WIDTH, AREATEX_HEIGHT, GL_RG, GL_UNSIGNED_BYTE, areaTexBytes);
        glTextureParameteri(m_SMAAAreaTex, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTextureParameteri(m_SMAAAreaTex, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTextureParameteri(m_SMAAAreaTex, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTextureParameteri(m_SMAAAreaTex, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glCreateTextures(GL_TEXTURE_2D, 1, &m_SMAASearchTex);
        glTextureStorage2D(m_SMAASearchTex, 1, GL_R8, SEARCHTEX_WIDTH, SEARCHTEX_HEIGHT);
        glTextureSubImage2D(m_SMAASearchTex, 0, 0, 0, SEARCHTEX_WIDTH, SEARCHTEX_HEIGHT, GL_RED, GL_UNSIGNED_BYTE, searchTexBytes);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glTextureParameteri(m_SMAASearchTex, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTextureParameteri(m_SMAASearchTex, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTextureParameteri(m_SMAASearchTex, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTextureParameteri(m_SMAASearchTex, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }

    void PostProcessStack::EnsureNISResources()
    {
        if (m_NISCoefScaler && m_NISCoefUSM && m_NISConstants)
            return;
        // Satır başına bir evre (64), satırda 8 katsayı = 2 RGBA32F texel (NIS örneğindeki düzen)
        auto makeCoef = [](uint32_t& tex, const float (*coef)[kFilterSize]) {
            glCreateTextures(GL_TEXTURE_2D, 1, &tex);
            glTextureStorage2D(tex, 1, GL_RGBA32F, (GLsizei)(kFilterSize / 4), (GLsizei)kPhaseCount);
            glTextureSubImage2D(tex, 0, 0, 0, (GLsizei)(kFilterSize / 4), (GLsizei)kPhaseCount, GL_RGBA, GL_FLOAT, coef);
            glTextureParameteri(tex, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTextureParameteri(tex, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTextureParameteri(tex, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTextureParameteri(tex, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        };
        makeCoef(m_NISCoefScaler, coef_scale);
        makeCoef(m_NISCoefUSM, coef_usm);
        glCreateBuffers(1, &m_NISConstants);
        glNamedBufferStorage(m_NISConstants, sizeof(NISConfig), nullptr, GL_DYNAMIC_STORAGE_BIT);
    }

    void PostProcessStack::DestroyCMAA2Resources()
    {
        if (m_CMAA2Edges) glDeleteTextures(1, &m_CMAA2Edges);
        if (m_CMAA2Heads) glDeleteTextures(1, &m_CMAA2Heads);
        for (uint32_t& b : m_CMAA2Buffers)
            if (b) glDeleteBuffers(1, &b);
        m_CMAA2Edges = m_CMAA2Heads = 0;
        for (uint32_t& b : m_CMAA2Buffers)
            b = 0;
        m_CMAA2Width = m_CMAA2Height = 0;
    }

    void PostProcessStack::EnsureCMAA2Resources(uint32_t width, uint32_t height)
    {
        if (m_CMAA2Edges && m_CMAA2Width == width && m_CMAA2Height == height)
            return;
        DestroyCMAA2Resources();
        m_CMAA2Width = width;
        m_CMAA2Height = height;
        // Kenarlar: iki pikselin 4 bitlik kenar bilgisi bir texelde (yarım genişlik); liste başları: 2x2 dörtlü başına
        glCreateTextures(GL_TEXTURE_2D, 1, &m_CMAA2Edges);
        glTextureStorage2D(m_CMAA2Edges, 1, GL_R32UI, (GLsizei)((width + 1) / 2), (GLsizei)height);
        glCreateTextures(GL_TEXTURE_2D, 1, &m_CMAA2Heads);
        glTextureStorage2D(m_CMAA2Heads, 1, GL_R32UI, (GLsizei)((width + 1) / 2), (GLsizei)((height + 1) / 2));
        const uint32_t clearHead = 0xFFFFFFFFu;
        glClearTexImage(m_CMAA2Heads, 0, GL_RED_INTEGER, GL_UNSIGNED_INT, &clearHead);
        const GLsizeiptr pixels = (GLsizeiptr)width * (GLsizeiptr)height;
        const GLsizeiptr quads = (GLsizeiptr)((width + 1) / 2) * (GLsizeiptr)((height + 1) / 2);
        const GLsizeiptr sizes[5] = {
            pixels * 4,    // şekil adayları (piksel başına en çok bir)
            quads * 4,     // ertelenmiş karışım konumları (dörtlü başına en çok bir)
            pixels * 8,    // karışım öğeleri (uvec2; taşarsa gölgelendirici atlar)
            16 * 4,        // sayaçlar
            4 * 4          // dolaylı gönderim argümanları
        };
        glCreateBuffers(5, m_CMAA2Buffers);
        for (int i = 0; i < 5; ++i)
        {
            glNamedBufferStorage(m_CMAA2Buffers[i], sizes[i], nullptr, GL_DYNAMIC_STORAGE_BIT);
            const uint32_t zero = 0;
            glClearNamedBufferData(m_CMAA2Buffers[i], GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_INT, &zero);
        }
    }

    void PostProcessStack::RunCMAA2(uint32_t texture)
    {
        auto edges = PostProcess::GetShader("CMAA2Edges");
        auto args = PostProcess::GetShader("CMAA2Args");
        auto process = PostProcess::GetShader("CMAA2Process");
        auto apply = PostProcess::GetShader("CMAA2Apply");
        if (!Valid(edges) || !Valid(args) || !Valid(process) || !Valid(apply))
            return;
        EnsureCMAA2Resources(m_Width, m_Height);
        // Bağlamalar (HLSL kayıtlarıyla aynı): görüntüler u0 renk, u1 kenarlar, u5 liste başları; tamponlar u2 adaylar,
        // u3 konumlar, u4 öğeler, u6 sayaçlar, u7 dolaylı argümanlar; renk dokusu t0 -> doku birimi 8
        glBindImageTexture(0, texture, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA8);
        glBindImageTexture(1, m_CMAA2Edges, 0, GL_FALSE, 0, GL_READ_WRITE, GL_R32UI);
        glBindImageTexture(5, m_CMAA2Heads, 0, GL_FALSE, 0, GL_READ_WRITE, GL_R32UI);
        const GLuint ssboBinding[5] = { 2, 3, 4, 6, 7 };
        for (int i = 0; i < 5; ++i)
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, ssboBinding[i], m_CMAA2Buffers[i]);
        glBindTextureUnit(8, texture);
        glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, m_CMAA2Buffers[4]);
        // Motorun gölgelendirici yolu görüntü / örnekleyici layout(binding)'ini siler (ada göre atanır): birimler her çekirdekte
        auto bindUnits = [](const std::shared_ptr<Shader>& sh)
        {
            sh->SetInt("g_inoutColorWriteonly", 0);
            sh->SetInt("g_workingEdges", 1);
            sh->SetInt("g_workingDeferredBlendItemListHeads", 5);
            sh->SetInt("SPIRV_Cross_Combinedg_inoutColorReadonlySPIRV_Cross_DummySampler", 8);
        };
        const GLbitfield storage = GL_SHADER_STORAGE_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT;
        // 1) Kenar algılama: 16x16 iş parçacığı, kenar şeridi hariç 14x14'ü 2x2 piksel işler (28x28 piksel / grup)
        edges->Bind();
        bindUnits(edges);
        glDispatchCompute((m_Width + 27) / 28, (m_Height + 27) / 28, 1);
        glMemoryBarrier(storage);
        // 2) Şekil adayları için dolaylı gönderim argümanları
        args->Bind();
        glDispatchCompute(2, 1, 1);
        glMemoryBarrier(storage | GL_COMMAND_BARRIER_BIT);
        // 3) Adaylar: şekiller izlenir, karışım renkleri ertelenmiş listeye
        process->Bind();
        bindUnits(process);
        glDispatchComputeIndirect(0);
        glMemoryBarrier(storage);
        // 4) Ertelenmiş uygulama için argümanlar (sayaçlar sonraki kare için sıfırlanır)
        args->Bind();
        glDispatchCompute(1, 2, 1);
        glMemoryBarrier(storage | GL_COMMAND_BARRIER_BIT);
        // 5) Karışım renkleri görüntüye yazılır (değişmeyen pikseller olduğu gibi kalır)
        apply->Bind();
        bindUnits(apply);
        glDispatchComputeIndirect(0);
        glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT | GL_FRAMEBUFFER_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
        glBindBuffer(GL_DISPATCH_INDIRECT_BUFFER, 0);
        for (GLuint unit : { 0u, 1u, 5u })
            glBindImageTexture(unit, 0, 0, GL_FALSE, 0, GL_READ_ONLY, GL_R32UI);
        for (int i = 0; i < 5; ++i)
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, ssboBinding[i], 0);
        glBindTextureUnit(8, 0);
        Renderer::GetStats().PostProcessPasses += 5;
    }

    void PostProcessStack::BeginScene(uint32_t width, uint32_t height)
    {
        width = std::max(width, 1u);
        height = std::max(height, 1u);
        // İç çözünürlük: süper örneklemede (SSAA / OGSSAA / FSAA) büyük, yükselticilerde (FSR / NIS / CAS) küçük; son geçiş çıktıya
        // süzer (küçültme ya da EASU / CAS büyütme)
        m_OutputWidth = width;
        m_OutputHeight = height;
        m_RenderScale = m_PointUpscale ? 1.0f : m_WantRenderScale;
        GLint maxTex = 8192;
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTex);
        const uint32_t limit = (uint32_t)std::min(maxTex, 8192);
        const uint32_t iw = std::clamp((uint32_t)std::lround((double)width * m_RenderScale), 1u, limit);
        const uint32_t ih = std::clamp((uint32_t)std::lround((double)height * m_RenderScale), 1u, limit);
        EnsureTargets(iw, ih);
        if (m_CoverageFramebuffer && m_CoverageSamples > 0)
        {
            glBindFramebuffer(GL_FRAMEBUFFER, m_CoverageFramebuffer);
            glViewport(0, 0, (GLsizei)m_Width, (GLsizei)m_Height);
        }
        else
            m_SceneFramebuffer->Bind();
        // SGSSAA / RGSS: parça gölgelendirici her MSAA örneğinde ayrı çalışır (gerçek süper örnekleme)
        m_SampleShading = m_WantSampleShading && m_Samples > 1 && AntiAliasing::GetCaps().SampleShading;
        if (m_SampleShading)
        {
            glEnable(GL_SAMPLE_SHADING);
            glMinSampleShading(1.0f);
        }
        m_TXAA = m_WantTXAA && m_Samples > 1;
        m_MultisampleTAA = m_WantMultisampleTAA && (m_Samples > 1 || m_CoverageSamples > 0);
        m_TAAVariant = m_WantTAAVariant;
        s_Current = this;
        m_ReactiveUsed = false;
        // Temporal AA: Halton(2,3) dizisiyle 16 karelik alt piksel titreşimi. 8 konum küçük nesnelerin / küçük pencerede
        // kenarların örtüsünü 8 kaba basamağa bölüyordu (hafif merdiven); durunca birikim uzun olduğundan 16'nın ortalaması alınır
        m_JitterActive = m_WantTAA;
        m_StabilityActive = m_WantStability;
        if (!m_StabilityActive)
            m_StabValid = false;
        if (m_JitterActive)
        {
            glm::vec2 pixel;
            if (m_TAAVariant == 1)
            {
                // TSAA: iki konum (çapraz beşli düzen), karede bir değişir
                pixel = (m_FrameIndex & 1u) ? glm::vec2(0.25f, -0.25f) : glm::vec2(-0.25f, 0.25f);
            }
            else if (m_TAAVariant == 3)
            {
                // SMAA T2x: iki konum (±0.25 piksel çapraz), SMAA'nın alt örnek indisleriyle eşleşir (RunFinal)
                pixel = SMAAT2xJitter(m_FrameIndex);
            }
            else if (m_TAAVariant == 4)
            {
                // FSR 2: SDK'nın titreşim dizisi (evre sayısı 8 x (çıktı / iç)²)
                pixel = FSR2Upscaler::JitterOffset(m_FrameIndex, m_Width, std::max(m_OutputWidth, m_Width));
            }
            else
            {
                // TAA: Halton(2,3) 16 konum; TSSAA (8TX): ilk 8 konum
                const uint32_t count = m_TAAVariant == 2 ? 8u : 16u;
                const uint32_t i = (m_FrameIndex % count) + 1u;
                pixel = glm::vec2(Halton(i, 2) - 0.5f, Halton(i, 3) - 0.5f);
            }
            m_JitterNDC = pixel * 2.0f / glm::vec2((float)m_Width, (float)m_Height);
        }
        else
            m_JitterNDC = glm::vec2(0.0f);
    }

    bool PostProcessStack::IsSceneDrawTarget(uint32_t framebuffer) const
    {
        return framebuffer == m_SceneFramebuffer->GetRendererID() || (m_CoverageSamples > 0 && framebuffer == m_CoverageFramebuffer);
    }

    bool PostProcessStack::GetCurrentJitter(glm::vec2& outNDC)
    {
        if (!s_Current || !s_Current->m_JitterActive || !s_Current->m_SceneFramebuffer || (s_Current->m_Samples > 1 && !s_Current->m_TXAA && !s_Current->m_MultisampleTAA))
            return false;
        GLint drawFBO = 0;
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFBO);
        if (!s_Current->IsSceneDrawTarget((uint32_t)drawFBO))
            return false;
        outNDC = s_Current->m_JitterNDC;
        return true;
    }

    bool PostProcessStack::WantsMotionData()
    {
        if (!s_Current || !s_Current->m_SceneFramebuffer)
            return false;
        const bool temporal = s_Current->m_JitterActive && (s_Current->m_Samples <= 1 || s_Current->m_TXAA || s_Current->m_MultisampleTAA);
        if (!temporal && !s_Current->m_StabilityActive)
            return false;
        GLint drawFBO = 0;
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFBO);
        return s_Current->IsSceneDrawTarget((uint32_t)drawFBO);
    }

    bool PostProcessStack::IsStabilityActive()
    {
        if (!s_Current || !s_Current->m_StabilityActive || !s_Current->m_SceneFramebuffer)
            return false;
        GLint drawFBO = 0;
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFBO);
        return (uint32_t)drawFBO == s_Current->m_SceneFramebuffer->GetRendererID();
    }

    float PostProcessStack::GetCurrentTextureLodBias()
    {
        if (!s_Current || !s_Current->m_SceneFramebuffer || s_Current->m_RenderScale >= 0.999f)
            return 0.0f;
        GLint drawFBO = 0;
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFBO);
        if ((uint32_t)drawFBO != s_Current->m_SceneFramebuffer->GetRendererID())
            return 0.0f;
        return std::log2(std::max(s_Current->m_RenderScale, 0.25f));
    }

    const void* PostProcessStack::GetCurrentStack()
    {
        return s_Current;
    }

    bool PostProcessStack::WantsGeometry()
    {
        PostProcessStack* st = s_Current;
        if (!st || !st->m_WantGeometry || !st->m_SceneFramebuffer)
            return false;
        GLint drawFBO = 0;
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFBO);
        return (uint32_t)drawFBO == st->m_SceneFramebuffer->GetRendererID();
    }

    void PostProcessStack::DestroySRAATargets()
    {
        if (m_SRAAFramebuffer) glDeleteFramebuffers(1, &m_SRAAFramebuffer);
        if (m_SRAANormalMS) glDeleteTextures(1, &m_SRAANormalMS);
        if (m_SRAADepthMS) glDeleteTextures(1, &m_SRAADepthMS);
        m_SRAAFramebuffer = m_SRAANormalMS = m_SRAADepthMS = 0;
        m_SRAAWidth = m_SRAAHeight = 0;
    }

    void PostProcessStack::RenderAAGeometry(const RenderCamera& camera)
    {
        m_GeometryReady = false;
        const uint32_t depth = GetSceneDepthTexture();
        const glm::mat4 viewProj = camera.Projection * camera.View;   // uzamsal yöntemler: titreşimsiz
        const glm::vec2 size((float)m_Width, (float)m_Height);
        // Kamera bilgisi (SRAA derinliği doğrusallaştırır)
        const glm::mat4& P = camera.Projection;
        if (!camera.Orthographic)
        {
            m_FinalNear = P[3][2] / (P[2][2] - 1.0f);
            m_FinalFar = P[3][2] / (P[2][2] + 1.0f);
        }
        if (m_AAMode == AntiAliasingMode::GBAA)
        {
            if (!depth)
                return;
            EnsureTarget(m_GBAAEdges, m_Width, m_Height, GL_RG16F);
            if (!m_GeometryFramebuffer)
                glCreateFramebuffers(1, &m_GeometryFramebuffer);
            glNamedFramebufferTexture(m_GeometryFramebuffer, GL_COLOR_ATTACHMENT0, m_GBAAEdges.Texture, 0);
            glNamedFramebufferTexture(m_GeometryFramebuffer, GL_DEPTH_STENCIL_ATTACHMENT, depth, 0);
            if (glCheckNamedFramebufferStatus(m_GeometryFramebuffer, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
                return;
            glBindFramebuffer(GL_FRAMEBUFFER, m_GeometryFramebuffer);
            glViewport(0, 0, (GLsizei)m_Width, (GLsizei)m_Height);
            const float none[4] = { 9.0f, 9.0f, 0.0f, 0.0f };   // kenar bilgisi yok
            glClearBufferfv(GL_COLOR, 0, none);
            Renderer3D::RenderGeometry(this, Renderer3D::GeometryPass::GBAAEdges, viewProj, size);
            m_GeometryReady = true;
        }
        else if (m_AAMode == AntiAliasingMode::SRAA)
        {
            if (m_SRAAWidth != m_Width || m_SRAAHeight != m_Height || !m_SRAAFramebuffer)
            {
                DestroySRAATargets();
                glCreateTextures(GL_TEXTURE_2D_MULTISAMPLE, 1, &m_SRAANormalMS);
                glTextureStorage2DMultisample(m_SRAANormalMS, 4, GL_RG16F, (GLsizei)m_Width, (GLsizei)m_Height, GL_TRUE);
                glCreateTextures(GL_TEXTURE_2D_MULTISAMPLE, 1, &m_SRAADepthMS);
                glTextureStorage2DMultisample(m_SRAADepthMS, 4, GL_DEPTH_COMPONENT32F, (GLsizei)m_Width, (GLsizei)m_Height, GL_TRUE);
                glCreateFramebuffers(1, &m_SRAAFramebuffer);
                glNamedFramebufferTexture(m_SRAAFramebuffer, GL_COLOR_ATTACHMENT0, m_SRAANormalMS, 0);
                glNamedFramebufferTexture(m_SRAAFramebuffer, GL_DEPTH_ATTACHMENT, m_SRAADepthMS, 0);
                m_SRAAWidth = m_Width;
                m_SRAAHeight = m_Height;
            }
            if (glCheckNamedFramebufferStatus(m_SRAAFramebuffer, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
                return;
            glBindFramebuffer(GL_FRAMEBUFFER, m_SRAAFramebuffer);
            glViewport(0, 0, (GLsizei)m_Width, (GLsizei)m_Height);
            const float zero[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
            glClearBufferfv(GL_COLOR, 0, zero);
            const float one = 1.0f;
            glClearBufferfv(GL_DEPTH, 0, &one);
            Renderer3D::RenderGeometry(this, Renderer3D::GeometryPass::SRAANormalDepth, viewProj, size);
            m_GeometryReady = true;
        }
    }

    bool PostProcessStack::CanDrawReactive()
    {
        const PostProcessStack* st = s_Current;
        return st && (st->m_JitterActive || st->m_StabilityActive) && st->m_SceneFramebuffer && st->m_Samples <= 1;
    }

    uint32_t PostProcessStack::BeginReactiveDraw()
    {
        PostProcessStack* st = s_Current;
        if (!st || !(st->m_JitterActive || st->m_StabilityActive) || !st->m_SceneFramebuffer || st->m_Samples > 1)
            return 0;
        GLint drawFBO = 0;
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFBO);
        if ((uint32_t)drawFBO != st->m_SceneFramebuffer->GetRendererID())
            return 0;
        st->EnsureTarget(st->m_Reactive, st->m_Width, st->m_Height, GL_R8);
        if (!st->m_ReactiveFramebuffer)
            glCreateFramebuffers(1, &st->m_ReactiveFramebuffer);
        // Ekler her seferinde: boyut değişince dokular yeniden oluşur
        glNamedFramebufferTexture(st->m_ReactiveFramebuffer, GL_COLOR_ATTACHMENT0, st->m_Reactive.Texture, 0);
        glNamedFramebufferTexture(st->m_ReactiveFramebuffer, GL_DEPTH_ATTACHMENT, st->m_SceneFramebuffer->GetDepthAttachmentRendererID(), 0);
        if (glCheckNamedFramebufferStatus(st->m_ReactiveFramebuffer, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            return 0;
        glBindFramebuffer(GL_FRAMEBUFFER, st->m_ReactiveFramebuffer);
        if (!st->m_ReactiveUsed)
        {
            const float zero[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
            glClearBufferfv(GL_COLOR, 0, zero);
            st->m_ReactiveUsed = true;
        }
        return st->m_ReactiveFramebuffer;
    }

    void PostProcessStack::EndReactiveDraw()
    {
        if (s_Current && s_Current->m_SceneFramebuffer)
            glBindFramebuffer(GL_FRAMEBUFFER, s_Current->m_SceneFramebuffer->GetRendererID());
    }

    uint32_t PostProcessStack::GetCurrentSceneDepth()
    {
        if (!s_Current || !s_Current->m_SceneFramebuffer || s_Current->m_Samples > 1)
            return 0;
        GLint drawFBO = 0;
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFBO);
        if ((uint32_t)drawFBO != s_Current->m_SceneFramebuffer->GetRendererID())
            return 0;
        return s_Current->m_SceneFramebuffer->GetDepthAttachmentRendererID();
    }

    uint32_t PostProcessStack::GetSceneShadowMaskTexture() const
    {
        if (!m_HasShadowMask)
            return 0;
        if (m_ResolvedFramebuffer)
            return m_ResolvedFramebuffer->GetColorAttachmentRendererID(3);
        return m_SceneFramebuffer ? m_SceneFramebuffer->GetColorAttachmentRendererID(3) : 0;
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
        if (m_SampleShading)
        {
            glDisable(GL_SAMPLE_SHADING);
            m_SampleShading = false;
        }
        if (m_CoverageFramebuffer && m_CoverageSamples > 0)
        {
            // EQAA / CSAA çözümü: kapsam örnekli renderbuffer'lardan sahne hedefinin (tek örnek) eklerine
            const GLuint src = m_CoverageFramebuffer, dst = m_SceneFramebuffer->GetRendererID();
            const int count = 1 + (m_HasNormals ? 1 : 0) + (m_HasAlbedo ? 1 : 0) + (m_HasShadowMask ? 1 : 0);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            for (int a = 0; a < count; ++a)
            {
                glNamedFramebufferReadBuffer(src, GL_COLOR_ATTACHMENT0 + a);
                glNamedFramebufferDrawBuffer(dst, GL_COLOR_ATTACHMENT0 + a);
                glBlitNamedFramebuffer(src, dst, 0, 0, m_Width, m_Height, 0, 0, m_Width, m_Height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
            }
            glBlitNamedFramebuffer(src, dst, 0, 0, m_Width, m_Height, 0, 0, m_Width, m_Height, GL_DEPTH_BUFFER_BIT, GL_NEAREST);
            glNamedFramebufferReadBuffer(src, GL_COLOR_ATTACHMENT0);
            const GLenum all[4] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2, GL_COLOR_ATTACHMENT3 };
            glNamedFramebufferDrawBuffers(dst, count, all);
            return;
        }
        m_SceneFramebuffer->Unbind();
        if (m_Samples > 1 && m_ResolvedFramebuffer)
        {
            glBindFramebuffer(GL_READ_FRAMEBUFFER, m_SceneFramebuffer->GetRendererID());
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_ResolvedFramebuffer->GetRendererID());
            // Renk: ton eşleme ağırlıklı çözüm (shader yoksa donanım ortalaması)
            ResolveMultisampleColor();
            glBindFramebuffer(GL_READ_FRAMEBUFFER, m_SceneFramebuffer->GetRendererID());
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_ResolvedFramebuffer->GetRendererID());
            glBlitFramebuffer(0, 0, m_Width, m_Height, 0, 0, m_Width, m_Height, GL_DEPTH_BUFFER_BIT, GL_NEAREST);
            if (m_HasNormals)
            {
                // SSR / SSGI ekleri de çözülür (en yakın örnek)
                const GLuint src = m_SceneFramebuffer->GetRendererID(), dst = m_ResolvedFramebuffer->GetRendererID();
                const int extra = m_HasShadowMask ? 3 : (m_HasAlbedo ? 2 : 1);
                for (int a = 1; a <= extra; ++a)
                {
                    glNamedFramebufferReadBuffer(src, GL_COLOR_ATTACHMENT0 + a);
                    glNamedFramebufferDrawBuffer(dst, GL_COLOR_ATTACHMENT0 + a);
                    glBlitNamedFramebuffer(src, dst, 0, 0, m_Width, m_Height, 0, 0, m_Width, m_Height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
                }
                glNamedFramebufferReadBuffer(src, GL_COLOR_ATTACHMENT0);
                const GLenum all[4] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1, GL_COLOR_ATTACHMENT2, GL_COLOR_ATTACHMENT3 };
                glNamedFramebufferDrawBuffers(dst, extra + 1, all);
            }
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
        }
    }

    void PostProcessStack::ResolveMultisampleColor()
    {
        auto shader = PostProcess::GetShader("MSAAResolve");
        const GLuint src = m_SceneFramebuffer->GetRendererID(), dst = m_ResolvedFramebuffer->GetRendererID();
        if (!Valid(shader))
        {
            glBlitNamedFramebuffer(src, dst, 0, 0, m_Width, m_Height, 0, 0, m_Width, m_Height, GL_COLOR_BUFFER_BIT, GL_NEAREST);
            return;
        }
        if (m_SamplePositionsFor != m_Samples)
        {
            glBindFramebuffer(GL_FRAMEBUFFER, src);
            for (uint32_t i = 0; i < m_Samples && i < 16; ++i)
                glGetMultisamplefv(GL_SAMPLE_POSITION, i, &m_SamplePositions[2 * i]);
            m_SamplePositionsFor = m_Samples;
        }
        const GLboolean depthTest = glIsEnabled(GL_DEPTH_TEST), blend = glIsEnabled(GL_BLEND), cull = glIsEnabled(GL_CULL_FACE);
        GLboolean depthMask = GL_TRUE;
        glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);
        glDisable(GL_CULL_FACE);
        glDepthMask(GL_FALSE);
        glBindFramebuffer(GL_FRAMEBUFFER, dst);
        glNamedFramebufferDrawBuffer(dst, GL_COLOR_ATTACHMENT0);
        glViewport(0, 0, (GLsizei)m_Width, (GLsizei)m_Height);
        shader->Bind();
        shader->SetInt("u_Color", 0);
        shader->SetInt("u_Samples", (int)std::min<uint32_t>(m_Samples, 16));
        shader->SetInt("u_Wide", m_TXAA ? 1 : 0);
        for (uint32_t i = 0; i < m_Samples && i < 16; ++i)
            shader->SetFloat2("u_SamplePos[" + std::to_string(i) + "]", glm::vec2(m_SamplePositions[2 * i], m_SamplePositions[2 * i + 1]));
        glBindTextureUnit(0, m_SceneFramebuffer->GetColorAttachmentRendererID(0));
        PostProcess::DrawFullscreenQuad();
        glBindTextureUnit(0, 0);
        if (depthTest) glEnable(GL_DEPTH_TEST);
        if (blend) glEnable(GL_BLEND);
        if (cull) glEnable(GL_CULL_FACE);
        glDepthMask(depthMask);
        Renderer::GetStats().PostProcessPasses++;
    }

    namespace
    {
        // Hareket eden gölge düşürücülerin dünya kutularını (min, max çiftleri) en çok maxBoxes kutuya indirir: her adımda
        // birleşimi en az hacim ekleyen çift birleşir (aynı nesnenin parçaları, bu ve önceki karedeki yeri). Hepsini tek
        // kutuda birleştirmek uzaktaki hedeflerle silah arasındaki bütün arenayı "gölge hacmi" yapardı (TAA'daki birleştirmeyle aynı)
        void MergeShadowBoxes(std::vector<glm::vec4>& minMax, size_t maxBoxes)
        {
            if (minMax.size() <= maxBoxes * 2)
                return;
            struct Box { glm::vec3 Lo, Hi; };
            std::vector<Box> boxes;
            for (size_t i = 0; i + 1 < minMax.size(); i += 2)
                boxes.push_back({ glm::vec3(minMax[i]), glm::vec3(minMax[i + 1]) });
            auto volume = [](const glm::vec3& lo, const glm::vec3& hi) {
                const glm::vec3 d = glm::max(hi - lo, glm::vec3(0.05f));
                return d.x * d.y * d.z;
            };
            while (boxes.size() > maxBoxes)
            {
                size_t bestA = 0, bestB = 1;
                float bestCost = std::numeric_limits<float>::max();
                for (size_t a = 0; a < boxes.size(); ++a)
                {
                    const float va = volume(boxes[a].Lo, boxes[a].Hi);
                    for (size_t b = a + 1; b < boxes.size(); ++b)
                    {
                        const float cost = volume(glm::min(boxes[a].Lo, boxes[b].Lo), glm::max(boxes[a].Hi, boxes[b].Hi))
                                         - va - volume(boxes[b].Lo, boxes[b].Hi);
                        if (cost < bestCost)
                        {
                            bestCost = cost;
                            bestA = a;
                            bestB = b;
                        }
                    }
                }
                boxes[bestA].Lo = glm::min(boxes[bestA].Lo, boxes[bestB].Lo);
                boxes[bestA].Hi = glm::max(boxes[bestA].Hi, boxes[bestB].Hi);
                boxes[bestB] = boxes.back();
                boxes.pop_back();
            }
            minMax.clear();
            for (const Box& b : boxes)
            {
                minMax.push_back(glm::vec4(b.Lo, 0.0f));
                minMax.push_back(glm::vec4(b.Hi, 0.0f));
            }
        }

        // Zamansal birikim (TAA geçişi) kullanan yöntemler
        bool UsesTemporalAA(AntiAliasingMode aa)
        {
            switch (aa)
            {
                case AntiAliasingMode::TAA:
                case AntiAliasingMode::TAA_FXAA:
                case AntiAliasingMode::TXAA:
                case AntiAliasingMode::TSAA:
                case AntiAliasingMode::TSSAA:
                    return true;
                default:
                    return false;
            }
        }

        // Bu kipte HDR TAA çözümü çalışır mı: zamansal yöntemler ve "Temporal Stability" açıkken bütün tek kare yöntemleri (FXAA, SMAA,
        // MLAA, CMAA, DEAA, GBAA, SRAA; MSAA, EQAA, RGSS, SGSSAA, SSAA ailesi kendi çoklu örnekli sonucuyla TAA'ya girer, duvar üstü
        // merdiveni de birikimle kapanır). Tek kareye bakan bu yöntemlerde pikselden ince ayrıntı (küplerin arası, uzak basamak)
        // kamera kaydıkça örnek noktasına bir denk gelip bir gelmiyor, yürürken yanıp sönüyordu; önceki karelerle ortalamak
        // yanıp sönmeyi tam kesmiyor, aralıkları da açıyordu. Alt piksel titreşimi + birikim ayrıntıyı her karede pikselin farklı
        // bir noktasından örnekler (TAA); yöntem yalnız TAA'nın geçmişi olmayan (yeni açılan) piksellerde uygulanır
        bool UsesTAAResolve(AntiAliasingMode aa, const PostProcessSettings& settings)
        {
            // SMAA T2x: Temporal Stability açıkken TAA çözümüne girer (SMAA + birikim); kapalıyken kendi iki karelik çözümü
            return UsesTemporalAA(aa) || (settings.TemporalStability && (AntiAliasing::IsSingleFrame(aa) || aa == AntiAliasingMode::SMAA_T2X));
        }

        uint32_t FloatBits(float f)
        {
            uint32_t u = 0;
            std::memcpy(&u, &f, sizeof(u));
            return u;
        }

        void SetUInt4(const std::shared_ptr<Shader>& shader, const char* name, const glm::uvec4& v)
        {
            const GLuint program = shader->GetRendererID();
            const GLint loc = glGetUniformLocation(program, name);
            if (loc >= 0)
                glProgramUniform4ui(program, loc, v.x, v.y, v.z, v.w);
        }
    }

    void PostProcessStack::ResolveAntiAliasing(const PostProcessSettings& settings, const RenderCamera& camera)
    {
        m_Upscaler = UpscalerMode::None;
        m_CASSharpen = false;
        m_CASStrength = 0.0f;
        m_UseFSR2 = false;
        if (m_PointUpscale)
        {
            m_AAMode = AntiAliasingMode::None;
            return;
        }
        // Eski sahnelerin AA listesindeki yükselticiler (FSR, DLSS, CAS ...) planda TAA + yükseltici / keskinleştirme olur
        const AntiAliasing::Plan plan = AntiAliasing::GetPlan(settings);
        AntiAliasingMode aa = AntiAliasing::ResolveForThisSystem(settings.Enabled ? plan.AA : AntiAliasingMode::FXAA);
        // Ortografik kamerada zamansal yöntem yok (TAA FXAA'ya, TXAA MSAA'ya düşer)
        if (camera.Orthographic)
        {
            if (aa == AntiAliasingMode::TXAA)
                aa = AntiAliasingMode::MSAA;
            else if (aa == AntiAliasingMode::TAA || aa == AntiAliasingMode::TAA_FXAA || aa == AntiAliasingMode::TSAA || aa == AntiAliasingMode::TSSAA)
                aa = AntiAliasingMode::FXAA;
            else if (aa == AntiAliasingMode::SMAA_T2X || aa == AntiAliasingMode::FSRAA)
                aa = AntiAliasingMode::SMAA;   // titreşim yok: yalnız SMAA 1x
        }
        m_AAMode = aa;
        if (!settings.Enabled)
            return;
        // Süper örnekleme yerelden yüksek çözünürlükte çizer: o sırada yükseltici kullanılmaz
        if (!AntiAliasing::IsSupersampling(aa))
            m_Upscaler = AntiAliasing::ResolveUpscalerForThisSystem(plan.Upscaler);
        // Ortografik kamerada titreşim yok: zamansal yükseltici yerine uzamsal FSR 1
        if (camera.Orthographic && (m_Upscaler == UpscalerMode::TAAU || m_Upscaler == UpscalerMode::FSR2))
            m_Upscaler = UpscalerMode::FSR1;
        // FSR 2 bağlamı kurulamadıysa (gölgelendirici / sürücü): yükseltici FSR 1, FSR AA yerine TAA
        if (m_FSR2 && m_FSR2->HasFailed())
        {
            if (m_Upscaler == UpscalerMode::FSR2)
                m_Upscaler = UpscalerMode::FSR1;
            if (m_AAMode == AntiAliasingMode::FSRAA)
                m_AAMode = AntiAliasingMode::TAA;
        }
        m_UseFSR2 = !camera.Orthographic && (m_Upscaler == UpscalerMode::FSR2 || m_AAMode == AntiAliasingMode::FSRAA);
        m_CASSharpen = plan.CASSharpen;
        m_CASStrength = std::clamp(AntiAliasing::IsLegacyUpscaler(settings.AntiAliasing) ? settings.UpscaleSharpness : settings.SharpenAmount, 0.0f, 1.0f);
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

    // TAA hareket keskinliği: kamera hareket ederken TAA keskinleştirmesine eklenen miktar (Sharpen.cqsh, 0..1)
    static constexpr float kTAAMotionSharpen = 0.6f;
    // NIS keskinliği yükseltme keskinliğinin yarısı: düşük çözünürlüklü görüntüyü büyütürken tam keskinlik basamakları (duvar üstü,
    // ince ızgara çizgileri) yerel çözünürlükte olmayan merdivenlere çeviriyordu
    static constexpr float kNISSharpnessScale = 0.5f;

    void PostProcessStack::RunFinal(const PostProcessSettings& settings, uint32_t outputFramebuffer, const int outputViewport[4])
    {
        auto fxaa = PostProcess::GetShader("FXAA");
        auto sharpen = PostProcess::GetShader("Sharpen");
        const glm::vec2 invSize(1.0f / m_Width, 1.0f / m_Height);
        const AntiAliasingMode aa = m_AAMode;
        // TAA bu karede HDR'de çözüldü (zamansal yöntemler); ortografik kamerada TAA yok
        const bool taa = m_TAAResolved;
        const uint32_t outW = (uint32_t)std::max(outputViewport[2], 1), outH = (uint32_t)std::max(outputViewport[3], 1);
        // İç çözünürlük çıktıdan farklı: süper örnekleme küçültmesi ya da yükseltme (piksel sanatı büyütmesi hariç)
        const bool resample = !m_PointUpscale && (outW != m_Width || outH != m_Height);
        const GLint ldrFilter = m_PointUpscale ? GL_NEAREST : GL_LINEAR;
        glTextureParameteri(m_LDR.Texture, GL_TEXTURE_MIN_FILTER, ldrFilter);
        glTextureParameteri(m_LDR.Texture, GL_TEXTURE_MAG_FILTER, ldrFilter);
        const glm::vec2 outOrigin((float)outputViewport[0], (float)outputViewport[1]);

        uint32_t cur = m_LDR.Texture;
        // İç çözünürlükte bir geçiş: sonuç sıradaki ara hedefe (m_Temp / m_Temp2)
        auto internalPass = [&]() -> RenderTarget&
        {
            RenderTarget& dst = (cur == m_Temp.Texture) ? m_Temp2 : m_Temp;
            glBindFramebuffer(GL_FRAMEBUFFER, dst.Framebuffer);
            glViewport(0, 0, (GLsizei)m_Width, (GLsizei)m_Height);
            return dst;
        };
        auto toOutput = [&]()
        {
            glBindFramebuffer(GL_FRAMEBUFFER, outputFramebuffer);
            glViewport(outputViewport[0], outputViewport[1], outputViewport[2], outputViewport[3]);
        };

        // 1) Uzamsal kenar yumuşatma (iç çözünürlükte, ton eşlenmiş görüntüde)
        // Temporal Stability: TAA çözümünün üstünde SMAA / MLAA / CMAA / DEAA yalnız TAA'nın geçmişini reddettiği piksellerde
        // kalır (aşağıda); FXAA bunu kendi içinde yapar (u_RejectOnly)
        const uint32_t beforeSpatial = cur;
        const bool rejectOnlySpatial = taa && (AntiAliasing::IsPostProcess(aa) || aa == AntiAliasingMode::SMAA_T2X || aa == AntiAliasingMode::GBAA || aa == AntiAliasingMode::SRAA) && aa != AntiAliasingMode::FXAA;
        if (aa == AntiAliasingMode::SMAA || aa == AntiAliasingMode::MLAA || aa == AntiAliasingMode::SMAA_T2X)
        {
            const bool mlaa = aa == AntiAliasingMode::MLAA;
            const bool t2x = aa == AntiAliasingMode::SMAA_T2X && m_JitterActive && !taa;
            auto edge = PostProcess::GetShader(mlaa ? "MLAAEdge" : "SMAAEdge");
            auto weights = PostProcess::GetShader(mlaa ? "MLAAWeights" : "SMAAWeights");
            auto neighborhood = PostProcess::GetShader("SMAABlend");
            if (Valid(edge) && Valid(weights) && Valid(neighborhood))
            {
                EnsureSMAATextures();
                const glm::vec4 metrics(1.0f / m_Width, 1.0f / m_Height, (float)m_Width, (float)m_Height);
                glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
                // Kenarlar (kenarsız piksel atılır: hedef temiz başlar)
                glBindFramebuffer(GL_FRAMEBUFFER, m_SMAAEdges.Framebuffer);
                glViewport(0, 0, (GLsizei)m_Width, (GLsizei)m_Height);
                glClear(GL_COLOR_BUFFER_BIT);
                edge->Bind();
                edge->SetFloat4("u_RTMetrics", metrics);
                edge->SetInt("u_ColorTex", 0);
                BindTexture(0, cur);
                PostProcess::DrawFullscreenQuad();
                // Karışım ağırlıkları (alan + arama dokuları)
                glBindFramebuffer(GL_FRAMEBUFFER, m_SMAAWeights.Framebuffer);
                glClear(GL_COLOR_BUFFER_BIT);
                weights->Bind();
                weights->SetFloat4("u_RTMetrics", metrics);
                weights->SetInt("u_EdgesTex", 0);
                weights->SetInt("u_AreaTex", 1);
                weights->SetInt("u_SearchTex", 2);
                // SMAA T2x: bu karenin titreşim konumuna karşılık gelen alt örnek indisleri
                weights->SetFloat4("u_SubsampleIndices", t2x ? SMAAT2xSubsampleIndices(m_FrameIndex) : glm::vec4(0.0f));
                BindTexture(0, m_SMAAEdges.Texture);
                BindTexture(1, m_SMAAAreaTex);
                BindTexture(2, m_SMAASearchTex);
                PostProcess::DrawFullscreenQuad();
                // Komşu karışımı
                RenderTarget& dst = internalPass();
                neighborhood->Bind();
                neighborhood->SetFloat4("u_RTMetrics", metrics);
                neighborhood->SetInt("u_ColorTex", 0);
                neighborhood->SetInt("u_BlendTex", 1);
                BindTexture(0, cur);
                BindTexture(1, m_SMAAWeights.Texture);
                PostProcess::DrawFullscreenQuad();
                cur = dst.Texture;
                Renderer::GetStats().PostProcessPasses += 3;
                auto resolve = PostProcess::GetShader("SMAAT2xResolve");
                if (t2x && Valid(resolve))
                {
                    // İki karenin SMAA çıktısının yeniden izdüşümlü birleşimi; bu karenin çıktısı (alfa: hız) sonraki kareye
                    RenderTarget& out = internalPass();
                    const int ni = 1 - m_T2xIndex;
                    EnsureTarget(m_T2xHistory[0], m_Width, m_Height, GL_RGBA8);
                    EnsureTarget(m_T2xHistory[1], m_Width, m_Height, GL_RGBA8);
                    static GLuint s_T2xFramebuffer = 0;
                    if (!s_T2xFramebuffer)
                        glCreateFramebuffers(1, &s_T2xFramebuffer);
                    glNamedFramebufferTexture(s_T2xFramebuffer, GL_COLOR_ATTACHMENT0, out.Texture, 0);
                    glNamedFramebufferTexture(s_T2xFramebuffer, GL_COLOR_ATTACHMENT1, m_T2xHistory[ni].Texture, 0);
                    const GLenum buffers[2] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
                    glNamedFramebufferDrawBuffers(s_T2xFramebuffer, 2, buffers);
                    glBindFramebuffer(GL_FRAMEBUFFER, s_T2xFramebuffer);
                    glViewport(0, 0, (GLsizei)m_Width, (GLsizei)m_Height);
                    resolve->Bind();
                    resolve->SetInt("u_Current", 0);
                    resolve->SetInt("u_Previous", 1);
                    resolve->SetInt("u_Depth", 2);
                    resolve->SetInt("u_GNormal", 3);
                    resolve->SetInt("u_Velocity", 4);
                    const uint32_t normals = GetSceneNormalTexture();
                    resolve->SetInt("u_HasMotionFlag", normals ? 1 : 0);
                    resolve->SetInt("u_HasVelocity", m_FinalHasVelocity ? 1 : 0);
                    resolve->SetInt("u_HistoryValid", (m_T2xValid && m_HasPrevViewProj) ? 1 : 0);
                    resolve->SetInt("u_CameraStill", m_CameraStill ? 1 : 0);
                    resolve->SetMat4("u_InvViewProj", m_FinalInvViewProj);
                    resolve->SetMat4("u_PrevViewProj", m_PrevViewProj);
                    resolve->SetFloat2("u_Texel", invSize);
                    resolve->SetFloat2("u_JitterUV", m_JitterNDC * 0.5f);
                    BindTexture(0, cur);
                    BindTexture(1, m_T2xHistory[m_T2xIndex].Texture);
                    BindTexture(2, GetSceneDepthTexture());
                    BindTexture(3, normals);
                    BindTexture(4, m_FinalHasVelocity ? m_Velocity.Texture : 0);
                    PostProcess::DrawFullscreenQuad();
                    cur = out.Texture;
                    m_T2xIndex = ni;
                    m_T2xValid = true;
                    Renderer::GetStats().PostProcessPasses++;
                }
            }
        }
        else if (aa == AntiAliasingMode::CMAA)
        {
            // CMAA2 yerinde çalışır: giriş ara hedefe kopyalanır, yalnız kenar pikselleri değişir
            RenderTarget& dst = internalPass();
            glCopyImageSubData(cur, GL_TEXTURE_2D, 0, 0, 0, 0, dst.Texture, GL_TEXTURE_2D, 0, 0, 0, 0, (GLsizei)m_Width, (GLsizei)m_Height, 1);
            RunCMAA2(dst.Texture);
            cur = dst.Texture;
        }
        else if (aa == AntiAliasingMode::GBAA && m_GeometryReady && Valid(PostProcess::GetShader("GBAAResolve")))
        {
            auto gbaa = PostProcess::GetShader("GBAAResolve");
            RenderTarget& dst = internalPass();
            gbaa->Bind();
            gbaa->SetInt("u_Color", 0);
            gbaa->SetInt("u_Edges", 1);
            gbaa->SetFloat2("u_Texel", invSize);
            BindTexture(0, cur);
            BindTexture(1, m_GBAAEdges.Texture);
            PostProcess::DrawFullscreenQuad();
            cur = dst.Texture;
            Renderer::GetStats().PostProcessPasses++;
        }
        else if (aa == AntiAliasingMode::SRAA && m_GeometryReady && Valid(PostProcess::GetShader("SRAAResolve")))
        {
            auto sraa = PostProcess::GetShader("SRAAResolve");
            RenderTarget& dst = internalPass();
            const uint32_t normals = GetSceneNormalTexture();
            sraa->Bind();
            sraa->SetInt("u_Color", 0);
            sraa->SetInt("u_Depth", 1);
            sraa->SetInt("u_Normal", 2);
            sraa->SetInt("u_SubNormal", 3);
            sraa->SetInt("u_SubDepth", 4);
            sraa->SetInt("u_HasNormal", normals ? 1 : 0);
            sraa->SetInt("u_Samples", 4);
            sraa->SetFloat("u_Near", m_FinalNear);
            sraa->SetFloat("u_Far", m_FinalFar);
            BindTexture(0, cur);
            BindTexture(1, GetSceneDepthTexture());
            BindTexture(2, normals);
            glBindTextureUnit(3, m_SRAANormalMS);
            glBindTextureUnit(4, m_SRAADepthMS);
            PostProcess::DrawFullscreenQuad();
            glBindTextureUnit(3, 0);
            glBindTextureUnit(4, 0);
            cur = dst.Texture;
            Renderer::GetStats().PostProcessPasses++;
        }
        else if (aa == AntiAliasingMode::DEAA)
        {
            auto deaa = PostProcess::GetShader("DEAA");
            if (Valid(deaa))
            {
                RenderTarget& dst = internalPass();
                deaa->Bind();
                deaa->SetInt("u_ScreenTexture", 0);
                deaa->SetFloat2("u_InverseScreenSize", invSize);
                BindTexture(0, cur);
                PostProcess::DrawFullscreenQuad();
                cur = dst.Texture;
                Renderer::GetStats().PostProcessPasses++;
            }
        }
        else if (Valid(fxaa) && (aa == AntiAliasingMode::FXAA || taa))
        {
            // FXAA: tüm görüntüde. TAA + FXAA: hareket eden ve geçmişi olmayan piksellerde. Diğer zamansal yöntemlerde yalnız
            // TAA'nın geçmişini reddettiği (yeni açılan) piksellerde: orada TAA'nın tek karesi basamaklı / dalgalı kalıyordu
            // (duvarın arkasından çıkınca öndeki duvarın üst ve alt kenarı)
            RenderTarget& dst = internalPass();
            fxaa->Bind();
            fxaa->SetInt("u_ScreenTexture", 0);
            fxaa->SetFloat2("u_InverseScreenSize", invSize);
            fxaa->SetInt("u_MotionMask", 1);
            fxaa->SetInt("u_UseMotionMask", taa ? 1 : 0);
            fxaa->SetInt("u_CameraStill", (taa && m_CameraStill) ? 1 : 0);
            fxaa->SetInt("u_RejectOnly", (taa && aa != AntiAliasingMode::TAA_FXAA) ? 1 : 0);
            BindTexture(0, cur);
            BindTexture(1, taa ? m_TAAState[m_HistoryIndex].Texture : 0);
            PostProcess::DrawFullscreenQuad();
            cur = dst.Texture;
            Renderer::GetStats().PostProcessPasses++;
        }

        auto rejectMix = PostProcess::GetShader("RejectMix");
        if (rejectOnlySpatial && cur != beforeSpatial && Valid(rejectMix))
        {
            RenderTarget& dst = internalPass();
            rejectMix->Bind();
            rejectMix->SetInt("u_Base", 0);
            rejectMix->SetInt("u_Processed", 1);
            rejectMix->SetInt("u_State", 2);
            BindTexture(0, beforeSpatial);
            BindTexture(1, cur);
            BindTexture(2, m_TAAState[m_HistoryIndex].Texture);
            PostProcess::DrawFullscreenQuad();
            cur = dst.Texture;
            Renderer::GetStats().PostProcessPasses++;
        }

        // 1b) Tek kare yöntemlerinde zamansal kararlılık: yürürken yanıp sönen ince ayrıntılar önceki çıktıyla ortalanır
        auto stab = PostProcess::GetShader("Stabilize");
        if (m_StabilityActive && Valid(stab) && GetSceneDepthTexture())
        {
            const uint32_t si = 1 - m_StabIndex;
            RenderTarget& prevOut = m_StabHistory[m_StabIndex];
            RenderTarget& nextOut = m_StabHistory[si];
            EnsureTarget(prevOut, m_Width, m_Height, GL_RGBA16F);
            EnsureTarget(nextOut, m_Width, m_Height, GL_RGBA16F);
            EnsureTarget(m_StabState[m_StabIndex], m_Width, m_Height, GL_RGBA16F);
            EnsureTarget(m_StabState[si], m_Width, m_Height, GL_RGBA16F);
            if (!m_StabFramebuffer[si])
                glCreateFramebuffers(1, &m_StabFramebuffer[si]);
            const GLenum buffers[2] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
            glNamedFramebufferDrawBuffers(m_StabFramebuffer[si], 2, buffers);
            glNamedFramebufferTexture(m_StabFramebuffer[si], GL_COLOR_ATTACHMENT0, nextOut.Texture, 0);
            glNamedFramebufferTexture(m_StabFramebuffer[si], GL_COLOR_ATTACHMENT1, m_StabState[si].Texture, 0);
            glBindFramebuffer(GL_FRAMEBUFFER, m_StabFramebuffer[si]);
            glViewport(0, 0, (GLsizei)m_Width, (GLsizei)m_Height);
            stab->Bind();
            stab->SetInt("u_Current", 0);
            stab->SetInt("u_History", 1);
            stab->SetInt("u_State", 2);
            stab->SetInt("u_Depth", 3);
            stab->SetInt("u_Velocity", 4);
            stab->SetInt("u_Reactive", 5);
            stab->SetInt("u_HasVelocity", m_FinalHasVelocity ? 1 : 0);
            stab->SetInt("u_HasReactive", m_ReactiveUsed ? 1 : 0);
            stab->SetInt("u_HistoryValid", (m_StabValid && m_HasPrevViewProj) ? 1 : 0);
            stab->SetInt("u_CameraStill", m_CameraStill ? 1 : 0);
            stab->SetMat4("u_InvViewProj", m_StabInvViewProj);
            stab->SetMat4("u_PrevViewProj", m_PrevViewProj);
            stab->SetFloat2("u_Texel", invSize);
            // Tepki maskesi yazılamıyorsa (MSAA) hareket eden saydam nesnelerin / parçacıkların bu ve önceki karedeki ekran
            // dikdörtgenleri: katman orada kendini kapatır (namlu alevi kaybolduktan sonra iz kalmaz). Fazlası sonuncuda birleşir
            {
                static std::vector<glm::vec4> s_StabRects;
                Renderer3D::GetMovingRects(this, s_StabRects);
                constexpr size_t kMaxRects = 32;
                if (s_StabRects.size() > kMaxRects)
                {
                    glm::vec4& last = s_StabRects[kMaxRects - 1];
                    for (size_t i = kMaxRects; i < s_StabRects.size(); ++i)
                    {
                        last.x = std::min(last.x, s_StabRects[i].x);
                        last.y = std::min(last.y, s_StabRects[i].y);
                        last.z = std::max(last.z, s_StabRects[i].z);
                        last.w = std::max(last.w, s_StabRects[i].w);
                    }
                    s_StabRects.resize(kMaxRects);
                }
                stab->SetInt("u_MovingCount", (int)s_StabRects.size());
                for (size_t i = 0; i < s_StabRects.size(); ++i)
                    stab->SetFloat4("u_MovingRects[" + std::to_string(i) + "]", s_StabRects[i]);
            }
            // Hareket eden nesnelerin gölge hacimleri (dünya kutuları + güneş yönü): gölge duran yüzeyin üstünde kayar, katman
            // orada kendini kapatır (yoksa gölgenin kenarı beneklenip lekeleniyordu)
            {
                static std::vector<glm::vec4> s_StabShadowBoxes;
                glm::vec3 towardSun(0.0f, 1.0f, 0.0f);
                if (!Renderer3D::GetMovingShadowBoxes(this, s_StabShadowBoxes, towardSun))
                    s_StabShadowBoxes.clear();
                MergeShadowBoxes(s_StabShadowBoxes, 32);
                stab->SetInt("u_ShadowBoxCount", (int)(s_StabShadowBoxes.size() / 2));
                stab->SetFloat3("u_TowardSun", towardSun);
                for (size_t i = 0; i < s_StabShadowBoxes.size(); ++i)
                    stab->SetFloat4("u_ShadowBoxes[" + std::to_string(i) + "]", s_StabShadowBoxes[i]);
            }
            BindTexture(0, cur);
            BindTexture(1, prevOut.Texture);
            BindTexture(2, m_StabState[m_StabIndex].Texture);
            BindTexture(3, GetSceneDepthTexture());
            BindTexture(4, m_FinalHasVelocity ? m_Velocity.Texture : 0);
            BindTexture(5, m_ReactiveUsed ? m_Reactive.Texture : 0);
            PostProcess::DrawFullscreenQuad();
            Renderer::GetStats().PostProcessPasses++;
            cur = nextOut.Texture;
            m_StabIndex = si;
            m_StabValid = true;
        }

        // 2) Çıktı: keskinleştirme, süper örnekleme küçültmesi ya da yükseltme (Upscaling bölümündeki yükseltici)
        const UpscalerMode up = m_Upscaler;
        auto cas = PostProcess::GetShader("CAS");
        // Yerel çözünürlükte CAS: Sharpening > CAS ya da ölçeksiz (Render Scale 1) CAS yükselticisi
        const bool casNative = !resample && (m_CASSharpen || up == UpscalerMode::CAS) && Valid(cas);
        // FSR (RCAS) ve CAS kendi keskinleştirmesini yapar: TAA keskinliği eklenmez
        const bool ownSharpen = casNative || (resample && (up == UpscalerMode::FSR1 || up == UpscalerMode::CAS));
        float sharpenAmount = ((taa && !ownSharpen) ? settings.TAASharpness : 0.0f) + ((settings.Sharpen && !m_CASSharpen) ? settings.SharpenAmount : 0.0f);
        if (!settings.Enabled || m_PointUpscale)
            sharpenAmount = 0.0f;
        const float upSharp = std::clamp(settings.UpscaleSharpness, 0.0f, 1.0f);

        auto motion = PostProcess::GetShader("MotionVectors");
        if (m_UseFSR2 && m_JitterActive && Valid(motion) && Valid(sharpen) && !(m_FSR2 && m_FSR2->HasFailed()))
        {
            // FSR 2: tam ekran hareket vektörleri (iç çözünürlük) -> FSR 2 (çıktı çözünürlüğünde görüntü) -> çıktıya kopya
            EnsureTarget(m_MotionVectors, m_Width, m_Height, GL_RG16F);
            glBindFramebuffer(GL_FRAMEBUFFER, m_MotionVectors.Framebuffer);
            glViewport(0, 0, (GLsizei)m_Width, (GLsizei)m_Height);
            motion->Bind();
            motion->SetInt("u_Depth", 0);
            motion->SetInt("u_Velocity", 1);
            motion->SetInt("u_HasVelocity", m_FinalHasVelocity ? 1 : 0);
            motion->SetInt("u_CameraStill", m_CameraStill ? 1 : 0);
            motion->SetMat4("u_InvViewProj", m_FinalInvViewProj);
            motion->SetMat4("u_PrevViewProj", m_PrevViewProj);
            motion->SetFloat2("u_JitterUV", m_JitterNDC * 0.5f);
            BindTexture(0, GetSceneDepthTexture());
            BindTexture(1, m_FinalHasVelocity ? m_Velocity.Texture : 0);
            PostProcess::DrawFullscreenQuad();
            EnsureTarget(m_FSR2Output, outW, outH, GL_RGBA8);
            if (!m_FSR2)
                m_FSR2 = std::make_unique<FSR2Upscaler>();
            FSR2Upscaler::Params prm;
            prm.Color = cur;
            prm.ColorFormat = GL_RGBA8;
            prm.Depth = GetSceneDepthTexture();
            prm.DepthFormat = GL_DEPTH24_STENCIL8;
            prm.Motion = m_MotionVectors.Texture;
            // Tepki maskesi: parçacıklar / saydam hareketli nesneler + hareket eden nesnelerin gölge hacimleri. Gölge duran yüzeyin
            // üstünde kayar (yüzey aynı, hız sıfır); FSR 2 eski gölgeyi geçmişte tutup kenarını benekli bırakıyordu
            uint32_t fsrReactive = m_ReactiveUsed ? m_Reactive.Texture : 0;
            auto fsrReactivePass = PostProcess::GetShader("FSR2Reactive");
            if (Valid(fsrReactivePass))
            {
                static std::vector<glm::vec4> s_FSRShadowBoxes;
                glm::vec3 towardSun(0.0f, 1.0f, 0.0f);
                if (!Renderer3D::GetMovingShadowBoxes(this, s_FSRShadowBoxes, towardSun))
                    s_FSRShadowBoxes.clear();
                MergeShadowBoxes(s_FSRShadowBoxes, 32);
                const uint32_t shadowMask = GetSceneShadowMaskTexture();
                const bool hasPrevShadow = shadowMask && m_FSR2PrevShadow.Texture && m_FSR2PrevShadow.Width == m_Width && m_FSR2PrevShadow.Height == m_Height;
                if (!s_FSRShadowBoxes.empty() && hasPrevShadow)
                {
                    EnsureTarget(m_FSR2Reactive, m_Width, m_Height, GL_R8);
                    glBindFramebuffer(GL_FRAMEBUFFER, m_FSR2Reactive.Framebuffer);
                    glViewport(0, 0, (GLsizei)m_Width, (GLsizei)m_Height);
                    fsrReactivePass->Bind();
                    fsrReactivePass->SetInt("u_Depth", 0);
                    fsrReactivePass->SetInt("u_Reactive", 1);
                    fsrReactivePass->SetInt("u_Shadow", 2);
                    fsrReactivePass->SetInt("u_PrevShadow", 3);
                    fsrReactivePass->SetInt("u_Motion", 4);
                    fsrReactivePass->SetInt("u_HasPrevShadow", 1);
                    fsrReactivePass->SetInt("u_HasReactive", m_ReactiveUsed ? 1 : 0);
                    fsrReactivePass->SetMat4("u_InvViewProj", m_FinalInvViewProj);
                    fsrReactivePass->SetInt("u_ShadowBoxCount", (int)(s_FSRShadowBoxes.size() / 2));
                    fsrReactivePass->SetFloat3("u_TowardSun", towardSun);
                    for (size_t i = 0; i < s_FSRShadowBoxes.size(); ++i)
                        fsrReactivePass->SetFloat4("u_ShadowBoxes[" + std::to_string(i) + "]", s_FSRShadowBoxes[i]);
                    BindTexture(0, GetSceneDepthTexture());
                    BindTexture(1, m_ReactiveUsed ? m_Reactive.Texture : 0);
                    BindTexture(2, shadowMask);
                    BindTexture(3, m_FSR2PrevShadow.Texture);
                    BindTexture(4, m_MotionVectors.Texture);
                    PostProcess::DrawFullscreenQuad();
                    Renderer::GetStats().PostProcessPasses++;
                    fsrReactive = m_FSR2Reactive.Texture;
                }
                // Sonraki kare için bu karenin güneş gölgesi
                if (shadowMask)
                {
                    EnsureTarget(m_FSR2PrevShadow, m_Width, m_Height, GL_R8);
                    glCopyImageSubData(shadowMask, GL_TEXTURE_2D, 0, 0, 0, 0, m_FSR2PrevShadow.Texture, GL_TEXTURE_2D, 0, 0, 0, 0,
                                       (GLsizei)m_Width, (GLsizei)m_Height, 1);
                }
            }
            prm.Reactive = fsrReactive;
            prm.Output = m_FSR2Output.Texture;
            prm.OutputFormat = GL_RGBA8;
            prm.RenderWidth = m_Width;
            prm.RenderHeight = m_Height;
            prm.DisplayWidth = outW;
            prm.DisplayHeight = outH;
            prm.JitterPixels = m_JitterNDC * 0.5f * glm::vec2((float)m_Width, (float)m_Height);
            prm.Near = m_FinalNear;
            prm.Far = m_FinalFar;
            prm.FovY = m_FinalFovY;
            prm.FrameTimeMs = m_DeltaTime * 1000.0f;
            prm.Reset = !m_FSR2Valid;
            // FSR AA (yerel çözünürlük): RCAS birikmiş görüntüdeki ince çizgileri (duvar ızgarası, uzak kenar) noktalı / benekli
            // keskinleştiriyordu; yerel çözünürlükte keskinlik TAA'nınkiyle aynı (TAA Sharpness + hareket ederken artış)
            const bool nativeFSR = m_AAMode == AntiAliasingMode::FSRAA && outW == m_Width && outH == m_Height;
            prm.Sharpen = !nativeFSR && upSharp > 0.0f;
            prm.Sharpness = upSharp;
            if (m_FSR2->Dispatch(prm))
            {
                m_FSR2Valid = true;
                toOutput();
                sharpen->Bind();
                sharpen->SetInt("u_ScreenTexture", 0);
                sharpen->SetFloat2("u_InverseScreenSize", glm::vec2(1.0f / (float)outW, 1.0f / (float)outH));
                sharpen->SetFloat("u_Amount", std::min((nativeFSR ? settings.TAASharpness : 0.0f) + ((settings.Sharpen && !m_CASSharpen) ? settings.SharpenAmount : 0.0f), 1.0f));
                sharpen->SetInt("u_HasMotionState", (nativeFSR && !m_CameraStill) ? 2 : 0);
                sharpen->SetFloat("u_MotionAmount", kTAAMotionSharpen);
                BindTexture(0, m_FSR2Output.Texture);
                PostProcess::DrawFullscreenQuad();
                sharpen->SetInt("u_HasMotionState", 0);
                Renderer::GetStats().PostProcessPasses += 2;
                return;
            }
        }

        auto taau = PostProcess::GetShader("TAAU");
        if (up == UpscalerMode::TAAU && m_JitterActive && Valid(taau) && Valid(sharpen))
        {
            // TAAU: iç çözünürlükteki titreşimli kare + çıktı çözünürlüğündeki geçmiş -> yeni geçmiş, sonra çıktıya keskinleştirerek
            const int ni = 1 - m_TAAUIndex;
            EnsureTarget(m_TAAUHistory[0], outW, outH, GL_RGBA16F);
            EnsureTarget(m_TAAUHistory[1], outW, outH, GL_RGBA16F);
            glBindFramebuffer(GL_FRAMEBUFFER, m_TAAUHistory[ni].Framebuffer);
            glViewport(0, 0, (GLsizei)outW, (GLsizei)outH);
            taau->Bind();
            taau->SetInt("u_Current", 0);
            taau->SetInt("u_History", 1);
            taau->SetInt("u_Depth", 2);
            taau->SetInt("u_GNormal", 3);
            taau->SetInt("u_Velocity", 4);
            const uint32_t normals = GetSceneNormalTexture();
            taau->SetInt("u_HasMotionFlag", normals ? 1 : 0);
            taau->SetInt("u_HasVelocity", m_FinalHasVelocity ? 1 : 0);
            taau->SetInt("u_HistoryValid", (m_TAAUValid && m_HasPrevViewProj && m_TAAUHistory[m_TAAUIndex].Width == outW &&
                                            m_TAAUHistory[m_TAAUIndex].Height == outH) ? 1 : 0);
            taau->SetInt("u_CameraStill", m_CameraStill ? 1 : 0);
            taau->SetMat4("u_InvViewProj", m_FinalInvViewProj);
            {
                // Bu ve önceki karenin göz noktası (kırpma uzayında (0, 0, k, 0)): yüzeye bakış yönünün karede ne kadar döndüğü için
                const auto eyeOf = [](const glm::vec4& e) { return std::abs(e.w) > 1e-12f ? glm::vec3(e) / e.w : glm::vec3(0.0f); };
                taau->SetFloat3("u_CameraPos", eyeOf(m_FinalInvViewProj[2]));
                taau->SetFloat3("u_PrevCameraPos", eyeOf(glm::inverse(m_PrevViewProj)[2]));
            }
            taau->SetMat4("u_PrevViewProj", m_PrevViewProj);
            taau->SetFloat2("u_InTexel", invSize);
            taau->SetFloat2("u_OutTexel", glm::vec2(1.0f / (float)outW, 1.0f / (float)outH));
            taau->SetFloat2("u_JitterUV", m_JitterNDC * 0.5f);
            taau->SetFloat("u_Feedback", std::clamp(settings.TAAFeedback, 0.5f, 0.97f));
            taau->SetInt("u_Reactive", 5);
            taau->SetInt("u_HasReactive", m_ReactiveUsed ? 1 : 0);
            BindTexture(5, m_ReactiveUsed ? m_Reactive.Texture : 0);
            glTextureParameteri(cur, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            BindTexture(0, cur);
            BindTexture(1, m_TAAUHistory[m_TAAUIndex].Texture);
            BindTexture(2, GetSceneDepthTexture());
            BindTexture(3, normals);
            BindTexture(4, m_FinalHasVelocity ? m_Velocity.Texture : 0);
            PostProcess::DrawFullscreenQuad();
            glTextureParameteri(cur, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            m_TAAUIndex = ni;
            m_TAAUValid = true;
            toOutput();
            sharpen->Bind();
            sharpen->SetInt("u_ScreenTexture", 0);
            sharpen->SetFloat2("u_InverseScreenSize", glm::vec2(1.0f / (float)outW, 1.0f / (float)outH));
            sharpen->SetFloat("u_Amount", std::min(0.5f * upSharp + ((settings.Sharpen && !m_CASSharpen) ? settings.SharpenAmount : 0.0f), 1.0f));
            // Hareket ederken geçmiş her karede yeniden örneklenip yumuşar (TAA'daki gibi): kamera kıpırdadıysa keskinlik artar
            sharpen->SetInt("u_HasMotionState", m_CameraStill ? 0 : 2);
            sharpen->SetFloat("u_MotionAmount", kTAAMotionSharpen);
            BindTexture(0, m_TAAUHistory[ni].Texture);
            PostProcess::DrawFullscreenQuad();
            sharpen->SetInt("u_HasMotionState", 0);
            Renderer::GetStats().PostProcessPasses += 2;
            return;
        }

        if (!resample)
        {
            if (casNative)
            {
                toOutput();
                cas->Bind();
                cas->SetInt("u_Source", 0);
                cas->SetFloat2("u_OutOrigin", outOrigin);
                const float w = (float)m_Width, h = (float)m_Height;
                const float strength = up == UpscalerMode::CAS ? upSharp : m_CASStrength;
                const float sharp = -1.0f / glm::mix(8.0f, 5.0f, strength);
                SetUInt4(cas, "u_Const0", glm::uvec4(FloatBits(1.0f), FloatBits(1.0f), FloatBits(0.0f), FloatBits(0.0f)));
                SetUInt4(cas, "u_Const1", glm::uvec4(FloatBits(sharp), glm::packHalf2x16(glm::vec2(sharp, 0.0f)), FloatBits(8.0f * w / w), 0u));
                BindTexture(0, cur);
                PostProcess::DrawFullscreenQuad();
            }
            else if (Valid(sharpen))
            {
                // Keskinleştirme (yoksa kopya)
                toOutput();
                sharpen->Bind();
                sharpen->SetInt("u_ScreenTexture", 0);
                sharpen->SetFloat2("u_InverseScreenSize", invSize);
                sharpen->SetFloat("u_Amount", std::min(sharpenAmount, 1.0f));
                // TAA: hareket ederken geçmiş her karede piksel altı kaymayla yeniden örneklenip yumuşar (dokular, küplerin arası);
                // o piksellerde keskinlik artar, durunca (yeniden birikimle geçmiş keskinleşince) eski değerine döner
                const bool motionSharpen = taa && !ownSharpen;
                sharpen->SetInt("u_MotionState", 1);
                sharpen->SetInt("u_HasMotionState", motionSharpen ? 1 : 0);
                sharpen->SetFloat("u_MotionAmount", motionSharpen ? kTAAMotionSharpen : 0.0f);
                BindTexture(0, cur);
                BindTexture(1, motionSharpen ? m_TAAState[m_HistoryIndex].Texture : 0);
                PostProcess::DrawFullscreenQuad();
                // Diğer keskinleştirme geçişleri bu tekdüzeyi kurmaz: program durumunda açık kalmasın
                sharpen->SetInt("u_HasMotionState", 0);
            }
        }
        else
        {
            const float inW = (float)m_Width, inH = (float)m_Height, oW = (float)outW, oH = (float)outH;
            auto down = PostProcess::GetShader("Downsample");
            auto easu = PostProcess::GetShader("FSR_EASU");
            auto rcas = PostProcess::GetShader("FSR_RCAS");
            auto casScale = PostProcess::GetShader("CAS_Scale");
            auto nis = PostProcess::GetShader("NIS");
            // NVIDIA Image Scaling: hesaplama gölgelendiricisi iç çözünürlük görüntüsünü m_Upscale'e (çıktı boyutu) yazar
            auto NISUpscale = [&](uint32_t source, uint32_t w, uint32_t h, float sharpness) -> bool
            {
                NISConfig config{};
                if (!Valid(sharpen) || !NVScalerUpdateConfig(config, sharpness, 0, 0, m_Width, m_Height, m_Width, m_Height, 0, 0, w, h, w, h, NISHDRMode::None))
                    return false;
                EnsureNISResources();
                EnsureTarget(m_Upscale, w, h, GL_RGBA8);
                glNamedBufferSubData(m_NISConstants, 0, sizeof(NISConfig), &config);
                nis->Bind();
                nis->SetInt("in_texture", 0);
                nis->SetInt("coef_scaler", 1);
                nis->SetInt("coef_usm", 2);
                nis->SetInt("out_texture", 0);   // görüntü birimi (motor layout(binding)'i siler, ada göre atanır)
                glBindBufferBase(GL_UNIFORM_BUFFER, UniformBinding::PostProcessConstants, m_NISConstants);
                BindTexture(0, source);
                BindTexture(1, m_NISCoefScaler);
                BindTexture(2, m_NISCoefUSM);
                glBindImageTexture(0, m_Upscale.Texture, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);
                glDispatchCompute((w + 31) / 32, (h + 23) / 24, 1);
                glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT | GL_FRAMEBUFFER_BARRIER_BIT);
                glBindImageTexture(0, 0, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);
                Renderer::GetStats().PostProcessPasses++;
                return true;
            };
            if (AntiAliasing::IsSupersampling(aa) && Valid(down))
            {
                toOutput();
                down->Bind();
                down->SetInt("u_Source", 0);
                down->SetFloat2("u_OutOrigin", outOrigin);
                down->SetFloat2("u_Scale", glm::vec2(inW / oW, inH / oH));
                down->SetInt("u_Mode", aa == AntiAliasingMode::OGSSAA ? 1 : aa == AntiAliasingMode::FSAA ? 2 : 0);
                BindTexture(0, cur);
                PostProcess::DrawFullscreenQuad();
            }
            else if (up == UpscalerMode::FSR1 && Valid(easu) && Valid(rcas))
            {
                // EASU: iç çözünürlük -> çıktı boyutunda ara hedef
                EnsureTarget(m_Upscale, outW, outH, GL_RGBA8);
                glBindFramebuffer(GL_FRAMEBUFFER, m_Upscale.Framebuffer);
                glViewport(0, 0, (GLsizei)outW, (GLsizei)outH);
                easu->Bind();
                easu->SetInt("u_Source", 0);
                easu->SetFloat2("u_OutOrigin", glm::vec2(0.0f));
                SetUInt4(easu, "u_Con0", glm::uvec4(FloatBits(inW / oW), FloatBits(inH / oH), FloatBits(0.5f * inW / oW - 0.5f), FloatBits(0.5f * inH / oH - 0.5f)));
                SetUInt4(easu, "u_Con1", glm::uvec4(FloatBits(1.0f / inW), FloatBits(1.0f / inH), FloatBits(1.0f / inW), FloatBits(-1.0f / inH)));
                SetUInt4(easu, "u_Con2", glm::uvec4(FloatBits(-1.0f / inW), FloatBits(2.0f / inH), FloatBits(1.0f / inW), FloatBits(2.0f / inH)));
                SetUInt4(easu, "u_Con3", glm::uvec4(FloatBits(0.0f), FloatBits(4.0f / inH), 0u, 0u));
                BindTexture(0, cur);
                PostProcess::DrawFullscreenQuad();
                // RCAS: keskinlik (0 = en keskin .. 2 durak azaltma)
                toOutput();
                rcas->Bind();
                rcas->SetInt("u_Source", 0);
                rcas->SetFloat2("u_OutOrigin", outOrigin);
                const float stops = (1.0f - upSharp) * 2.0f;
                const float rs = std::exp2(-stops);
                SetUInt4(rcas, "u_Con", glm::uvec4(FloatBits(rs), glm::packHalf2x16(glm::vec2(rs, rs)), 0u, 0u));
                BindTexture(0, m_Upscale.Texture);
                PostProcess::DrawFullscreenQuad();
                Renderer::GetStats().PostProcessPasses++;
            }
            else if (up == UpscalerMode::NIS && Valid(nis) && NISUpscale(cur, outW, outH, kNISSharpnessScale * upSharp))
            {
                // NIS çıktısı çıktı boyutunda ara hedefte: çıktıya kopya (keskinleştirmesiz)
                toOutput();
                sharpen->Bind();
                sharpen->SetInt("u_ScreenTexture", 0);
                sharpen->SetFloat2("u_InverseScreenSize", glm::vec2(1.0f / oW, 1.0f / oH));
                sharpen->SetFloat("u_Amount", 0.0f);
                BindTexture(0, m_Upscale.Texture);
                PostProcess::DrawFullscreenQuad();
                Renderer::GetStats().PostProcessPasses++;
            }
            else if (up == UpscalerMode::CAS && Valid(casScale))
            {
                toOutput();
                casScale->Bind();
                casScale->SetInt("u_Source", 0);
                casScale->SetFloat2("u_OutOrigin", outOrigin);
                const float sharp = -1.0f / glm::mix(8.0f, 5.0f, upSharp);
                SetUInt4(casScale, "u_Const0", glm::uvec4(FloatBits(inW / oW), FloatBits(inH / oH), FloatBits(0.5f * inW / oW - 0.5f), FloatBits(0.5f * inH / oH - 0.5f)));
                SetUInt4(casScale, "u_Const1", glm::uvec4(FloatBits(sharp), glm::packHalf2x16(glm::vec2(sharp, 0.0f)), FloatBits(8.0f * inW / oW), 0u));
                BindTexture(0, cur);
                PostProcess::DrawFullscreenQuad();
            }
            else if (Valid(sharpen))
            {
                // Genel: çift doğrusal ölçekleme (+ keskinleştirme)
                toOutput();
                sharpen->Bind();
                sharpen->SetInt("u_ScreenTexture", 0);
                sharpen->SetFloat2("u_InverseScreenSize", invSize);
                sharpen->SetFloat("u_Amount", std::min(sharpenAmount, 1.0f));
                BindTexture(0, cur);
                PostProcess::DrawFullscreenQuad();
            }
        }
        Renderer::GetStats().PostProcessPasses++;
    }

    namespace { GLuint CompileStage(GLenum type, const std::string& src, std::string& error); }

    void PostProcessStack::Render(const PostProcessSettings& settings, const RenderCamera& camera, float time, bool litScene)
    {
        if (!m_SceneFramebuffer)
            return;
        CQ_PROFILE_SAMPLE("Render.PostProcess", Rendering);
        CQ_PROFILE_GPU("Post Processing");
        m_LitScene = litScene;
        ResolveAntiAliasing(settings, camera);

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
        // Kamera kesmesi (ışınlanma, yeniden doğma): önceki karenin görüntüsünün bu kareyle ilgisi yok, zamansal yöntemler baştan başlar.
        // FSR 2 eski konumdan kalan ince ayrıntı kilitlerini koruyup dururken bile görüntüyü bozuk bırakıyordu (aralıklar soluk, kenarlar koyu)
        if (m_HasPrevViewProj && !camera.Orthographic)
        {
            const glm::vec4 e = glm::inverse(m_PrevViewProj)[2];
            if (std::abs(e.w) > 1e-12f && glm::distance(glm::vec3(e) / e.w, camera.Position) > 8.0f)
            {
                m_HistoryValid = false;
                m_FSR2Valid = false;
                m_TAAUValid = false;
                m_T2xValid = false;
                m_StabValid = false;
            }
        }
        m_DeltaTime = deltaTime;
        m_Source = GetSceneColorTexture();
        m_AOApplied = false;
        const bool fx = settings.Enabled && !m_PointUpscale;
        if (fx)
            RunSceneEffects(settings, camera, time);
        m_TAAResolved = false;
        // TAAU seçiliyken zamansal kenar yumuşatma TAAU'nun kendisidir (Unreal'daki gibi zamansal yükseltici TAA'nın yerini alır)
        if (fx && UsesTAAResolve(m_AAMode, settings) && !camera.Orthographic && m_Upscaler != UpscalerMode::TAAU && !m_UseFSR2)
            RunTAA(settings, camera);
        else
            m_HistoryValid = false;
        // SMAA T2x / TAAU: çözüm RunFinal'da (LDR); hız vektörleri ve bu karenin titreşimli ters matrisi şimdi
        const bool t2xSeparate = m_AAMode == AntiAliasingMode::SMAA_T2X && !UsesTAAResolve(m_AAMode, settings);
        if (fx && (t2xSeparate || m_Upscaler == UpscalerMode::TAAU || m_UseFSR2) && !camera.Orthographic && m_JitterActive)
        {
            m_FinalHasVelocity = RenderObjectVelocity(camera);
            m_FinalInvViewProj = glm::inverse(JitteredProjection(camera) * camera.View);
            bool still = m_HasPrevViewProj;
            const glm::mat4 now = camera.Projection * camera.View;
            for (int c = 0; c < 4 && still; ++c)
                for (int r = 0; r < 4; ++r)
                    if (std::abs(now[c][r] - m_PrevViewProj[c][r]) > 1e-6f * std::max(1.0f, std::abs(now[c][r])))
                    {
                        still = false;
                        break;
                    }
            m_CameraStill = still;
            if (m_AAMode != AntiAliasingMode::SMAA_T2X)
                m_T2xValid = false;
            if (m_Upscaler != UpscalerMode::TAAU)
                m_TAAUValid = false;
            if (!m_UseFSR2)
                m_FSR2Valid = false;
            // FSR 2 kamera bilgisi (OpenGL perspektif matrisinden)
            const glm::mat4& P = camera.Projection;
            m_FinalFovY = 2.0f * std::atan(1.0f / std::max(std::abs(P[1][1]), 1e-6f));
            m_FinalNear = P[3][2] / (P[2][2] - 1.0f);
            m_FinalFar = P[3][2] / (P[2][2] + 1.0f);
        }
        else
        {
            m_T2xValid = false;
            m_TAAUValid = false;
            m_FSR2Valid = false;
        }
        // Tek kare yöntemlerinde zamansal kararlılık (RunFinal'da, kenar yumuşatmasından sonra): nesne hareket vektörleri, bu
        // karenin (titreşimsiz) ters matrisi ve kameranın kıpırdayıp kıpırdamadığı şimdi
        if (fx && m_StabilityActive && !camera.Orthographic)
        {
            // SMAA T2x: hız vektörleri ve titreşimli ters matris yukarıda (T2x çözümü için) hazırlandı; katman titreşimsiz matrisi kullanır
            const bool t2xPrepared = t2xSeparate && m_JitterActive;
            if (!t2xPrepared)
                m_FinalHasVelocity = RenderObjectVelocity(camera);
            m_StabInvViewProj = glm::inverse(camera.Projection * camera.View);
            bool still = m_HasPrevViewProj;
            const glm::mat4 now = camera.Projection * camera.View;
            for (int c = 0; c < 4 && still; ++c)
                for (int r = 0; r < 4; ++r)
                    if (std::abs(now[c][r] - m_PrevViewProj[c][r]) > 1e-6f * std::max(1.0f, std::abs(now[c][r])))
                    {
                        still = false;
                        break;
                    }
            m_CameraStill = still;
        }
        else
            m_StabValid = false;
        if (fx && settings.DepthOfField)
            RunDepthOfField(settings, camera);
        if (fx && settings.MotionBlur)
            RunMotionBlur(settings, camera);
        // Full Screen Pass (Before Post Processing): HDR sahne rengi üzerinde
        for (const auto& pass : settings.FullScreenPasses)
            if (!pass.AfterPostProcess)
            {
                RenderTarget& out = NextHDRTarget();
                if (RunFullScreenPass(pass, m_Source, out, time))
                    m_Source = out.Texture;
            }
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
        // GBAA / SRAA: kenar uzaklığı ya da çok örnekli geometri hedefi (RunFinal'daki çözüm için)
        m_GeometryReady = false;
        if (m_AAMode == AntiAliasingMode::GBAA || m_AAMode == AntiAliasingMode::SRAA)
            RenderAAGeometry(camera);
        const bool afterPasses = std::any_of(settings.FullScreenPasses.begin(), settings.FullScreenPasses.end(),
                                             [](const FullScreenPassEntry& p) { return p.AfterPostProcess; });
        if (!afterPasses)
            RunFinal(settings, (uint32_t)outputFBO, outputViewport);
        else
        {
            // Full Screen Pass (After Post Processing): son görüntü ara hedefe, geçişler, sonra çıktıya kopya
            const uint32_t w = (uint32_t)std::max(outputViewport[2], 1), h = (uint32_t)std::max(outputViewport[3], 1);
            EnsureTarget(m_AfterPass[0], w, h, GL_RGBA8);
            EnsureTarget(m_AfterPass[1], w, h, GL_RGBA8);
            const int local[4] = { 0, 0, (int)w, (int)h };
            RunFinal(settings, m_AfterPass[0].Framebuffer, local);
            int cur = 0;
            for (const auto& pass : settings.FullScreenPasses)
                if (pass.AfterPostProcess && RunFullScreenPass(pass, m_AfterPass[cur].Texture, m_AfterPass[1 - cur], time))
                    cur = 1 - cur;
            // Çıktıya tam ekran kopya (çıktı tamponunda tamsayı ekler olabilir: blit yerine çizim)
            static GLuint copyProgram = 0;
            if (!copyProgram)
            {
                std::string err;
                const char* vs = "#version 450 core\nlayout(location = 0) in vec2 a_Position;\nlayout(location = 1) in vec2 a_TexCoord;\n"
                                 "out vec2 v_UV;\nvoid main() { v_UV = a_TexCoord; gl_Position = vec4(a_Position, 0.0, 1.0); }\n";
                const char* fs = "#version 450 core\nin vec2 v_UV;\nlayout(location = 0) out vec4 o_Color;\nuniform sampler2D u_Source;\n"
                                 "void main() { o_Color = texture(u_Source, v_UV); }\n";
                GLuint v = CompileStage(GL_VERTEX_SHADER, vs, err), f = CompileStage(GL_FRAGMENT_SHADER, fs, err);
                copyProgram = glCreateProgram();
                glAttachShader(copyProgram, v);
                glAttachShader(copyProgram, f);
                glLinkProgram(copyProgram);
                glDeleteShader(v);
                glDeleteShader(f);
            }
            glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)outputFBO);
            glViewport(outputViewport[0], outputViewport[1], outputViewport[2], outputViewport[3]);
            glUseProgram(copyProgram);
            glBindTextureUnit(0, m_AfterPass[cur].Texture);
            glUniform1i(glGetUniformLocation(copyProgram, "u_Source"), 0);
            PostProcess::DrawFullscreenQuad();
        }

        // Sonraki kare için: titreşimsiz görüş-izdüşüm (TAA / hareket bulanıklığı yeniden izdüşümü), istekler
        m_PrevViewProj = camera.Projection * camera.View;
        m_HasPrevViewProj = true;
        ++m_FrameIndex;
        {
            // Kenar yumuşatma istekleri (sonraki karenin sahne hedefi ve titreşimi)
            const AntiAliasingMode next = m_AAMode;
            const bool temporal = fx && !camera.Orthographic && UsesTAAResolve(next, settings);
            const bool t2x = fx && !camera.Orthographic && next == AntiAliasingMode::SMAA_T2X && !temporal;
            const bool taau = fx && !camera.Orthographic && m_Upscaler == UpscalerMode::TAAU;
            const bool fsr2 = fx && m_UseFSR2;
            m_WantTAA = temporal || t2x || taau || fsr2;   // titreşim (+ hareket bayrağı için normal eki)
            m_WantTXAA = temporal && next == AntiAliasingMode::TXAA && !fsr2;
            // Temporal Stability: çoklu örnekli yöntemlerde de titreşim sahneye uygulanır (TXAA gibi); uygulanmayınca TAA olmayan bir
            // titreşimi geri alıyor, görüntüyü yarım piksel oynatıp yumuşatıyordu
            m_WantMultisampleTAA = temporal && !taau && !fsr2 && (next == AntiAliasingMode::MSAA || next == AntiAliasingMode::CSAA || next == AntiAliasingMode::EQAA
                                                                  || next == AntiAliasingMode::RGSS || next == AntiAliasingMode::SGSSAA);
            // TAAU: 16 konumlu Halton titreşimi; FSR 2: SDK'nın dizisi (zamansal yükseltici kenar yumuşatmanın yerini alır)
            m_WantTAAVariant = fsr2 ? 4 : taau ? 0 : next == AntiAliasingMode::TSAA ? 1 : next == AntiAliasingMode::TSSAA ? 2 : t2x ? 3 : 0;
            switch (next)
            {
                case AntiAliasingMode::MSAA:   m_WantSamples = (uint32_t)std::clamp(settings.MSAASamples, 2, 8); break;
                case AntiAliasingMode::RGSS:   m_WantSamples = 4; break;
                case AntiAliasingMode::SGSSAA: m_WantSamples = 8; break;
                case AntiAliasingMode::TXAA:   m_WantSamples = 4; break;
                default:                       m_WantSamples = 1; break;
            }
            m_WantSampleShading = next == AntiAliasingMode::RGSS || next == AntiAliasingMode::SGSSAA;
            if (next == AntiAliasingMode::EQAA)
                m_WantCoverageSamples = settings.EQAASamples >= 16 ? 16u : settings.EQAASamples >= 8 ? 8u : 4u;
            else if (next == AntiAliasingMode::CSAA && AntiAliasing::GetCaps().NVCoverage)
                m_WantCoverageSamples = 16;
            else
                m_WantCoverageSamples = 0;
            m_WantRenderScale = (fx && !m_PointUpscale) ? AntiAliasing::RenderScale(settings, next, m_Upscaler) : 1.0f;
            m_WantGeometry = next == AntiAliasingMode::GBAA || next == AntiAliasingMode::SRAA;
            // Tek kare yöntemlerinde zamansal kararlılık: hareket verisi (nesne hareket vektörleri, önceki konum) toplanır
            // SMAA T2x: iki titreşim konumunu birleştirir, hareket ederken pikselden ince ayrıntı yine yanıp sönüyordu; katman T2x'in
            // birleşik çıktısında çalışır (dururken çıktı sabit, katman dokunmaz)
            m_WantStability = fx && !camera.Orthographic && settings.TemporalStability && !temporal && !taau && !fsr2
                              && (AntiAliasing::IsSingleFrame(next) || t2x);
        }
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

    namespace
    {
        // Image Effect Shader programları (dosya değişince yeniden derlenir)
        struct FullScreenProgram { GLuint Program = 0; std::filesystem::file_time_type Time{}; std::string Error; };
        std::unordered_map<std::string, FullScreenProgram>& FullScreenPrograms()
        {
            static auto* m = new std::unordered_map<std::string, FullScreenProgram>();
            return *m;
        }

        GLuint CompileStage(GLenum type, const std::string& src, std::string& error)
        {
            GLuint s = glCreateShader(type);
            const char* c = src.c_str();
            glShaderSource(s, 1, &c, nullptr);
            glCompileShader(s);
            GLint ok = 0;
            glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
            if (!ok)
            {
                char log[2048];
                glGetShaderInfoLog(s, sizeof(log), nullptr, log);
                error = log;
                glDeleteShader(s);
                return 0;
            }
            return s;
        }

        GLuint FullScreenProgramFor(const std::string& path)
        {
            const std::string file = SceneSerializer::ResolveAssetPath(path);
            std::error_code ec;
            const auto t = std::filesystem::last_write_time(file, ec);
            if (ec)
                return 0;
            auto& p = FullScreenPrograms()[file];
            if (p.Time == t && (p.Program || !p.Error.empty()))
                return p.Program;
            p.Time = t;
            if (p.Program)
                glDeleteProgram(p.Program);
            p.Program = 0;
            p.Error.clear();
            std::ifstream f(file);
            std::stringstream ss;
            ss << f.rdbuf();
            const char* vs =
                "#version 450 core\n"
                "layout(location = 0) in vec2 a_Position;\nlayout(location = 1) in vec2 a_TexCoord;\nout vec2 v_UV;\n"
                "void main() { v_UV = a_TexCoord; gl_Position = vec4(a_Position, 0.0, 1.0); }\n";
            const std::string fs =
                "#version 450 core\nin vec2 v_UV;\nlayout(location = 0) out vec4 o_Color;\n"
                "uniform sampler2D u_Source;\nuniform sampler2D u_Depth;\nuniform float u_Time;\nuniform vec2 u_Resolution;\nuniform float u_Intensity;\n"
                "#line 1\n" + ss.str() +
                "\nvoid main() { vec4 src = texture(u_Source, v_UV); vec4 fx = Effect(v_UV); o_Color = mix(src, fx, u_Intensity); }\n";
            GLuint v = CompileStage(GL_VERTEX_SHADER, vs, p.Error);
            GLuint fr = v ? CompileStage(GL_FRAGMENT_SHADER, fs, p.Error) : 0;
            if (!v || !fr)
            {
                if (v) glDeleteShader(v);
                CQ_CORE_ERROR("[Full Screen Pass] {0}: {1}", std::filesystem::path(file).filename().string(), p.Error);
                return 0;
            }
            p.Program = glCreateProgram();
            glAttachShader(p.Program, v);
            glAttachShader(p.Program, fr);
            glLinkProgram(p.Program);
            glDeleteShader(v);
            glDeleteShader(fr);
            GLint ok = 0;
            glGetProgramiv(p.Program, GL_LINK_STATUS, &ok);
            if (!ok)
            {
                char log[2048];
                glGetProgramInfoLog(p.Program, sizeof(log), nullptr, log);
                p.Error = log;
                glDeleteProgram(p.Program);
                p.Program = 0;
            }
            return p.Program;
        }
    }

    bool PostProcessStack::RunFullScreenPass(const FullScreenPassEntry& pass, uint32_t source, RenderTarget& target, float time)
    {
        GLuint program = FullScreenProgramFor(pass.Shader);
        if (!program || !source || !target.Framebuffer)
            return false;
        glBindFramebuffer(GL_FRAMEBUFFER, target.Framebuffer);
        glViewport(0, 0, (GLsizei)target.Width, (GLsizei)target.Height);
        glUseProgram(program);
        glBindTextureUnit(0, source);
        glUniform1i(glGetUniformLocation(program, "u_Source"), 0);
        glBindTextureUnit(1, GetSceneDepthTexture());
        glUniform1i(glGetUniformLocation(program, "u_Depth"), 1);
        glUniform1f(glGetUniformLocation(program, "u_Time"), time);
        glUniform2f(glGetUniformLocation(program, "u_Resolution"), (float)target.Width, (float)target.Height);
        glUniform1f(glGetUniformLocation(program, "u_Intensity"), pass.Intensity);
        PostProcess::DrawFullscreenQuad();
        Renderer::GetStats().PostProcessPasses += 1;
        return true;
    }

    std::string PostProcess::GetFullScreenPassError(const std::string& path)
    {
        auto it = FullScreenPrograms().find(SceneSerializer::ResolveAssetPath(path));
        return it == FullScreenPrograms().end() ? std::string() : it->second.Error;
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
