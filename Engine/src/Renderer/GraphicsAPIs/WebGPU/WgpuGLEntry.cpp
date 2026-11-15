#ifdef CQ_WEBGPU_ENABLED

// WebGPU çeviri katmanının GL giriş noktaları: arka uçtan bağımsız ön yüz (GLEmu/GLFrontEnd.inl) + WebGPU kancaları.

#include "WgpuGL.h"
#include "Core/Logging/Log.h"
#include "Renderer/ShaderCompiler/ShaderCompiler.h"
#include "../OpenGL/GLMissingStubs.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <string>
#include <utility>

namespace Conqueror::WgpuGL
{
#include "../GLEmu/GLFrontEnd.inl"

    bool InitDrawHelpers();     // WgpuGLDraw.cpp
    void ReleaseSamplers();     // WgpuGLObjects.cpp

    namespace
    {
        Limits s_Limits;

        uint64_t NowNs()
        {
            return (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
        }
    }

    bool Live() { return Ctx().Live; }

    const Limits& GetLimits() { return s_Limits; }

    uint32_t ClampSamples(GLsizei samples, bool depth)
    {
        (void)depth;
        return samples > 1 ? 4u : 1u;   // WebGPU: 1 ya da 4
    }

    // WebGPU'da komut arası zaman damgası her yerde yok: CPU zamanı (profil çizelgesi CPU tarafını gösterir)
    void WriteTimestamp(QueryObject& q)
    {
        q.Ns = NowNs();
        q.Written = true;
    }

    bool ReadTimestamp(QueryObject& q, bool wait, uint64_t& ns)
    {
        (void)wait;
        ns = q.Ns;
        return true;
    }

    // Örtüşme sorguları: bu sürümde her nesne görünür sayılır (örtüşme ayıklaması kapalı gibi davranır, çizim doğru kalır)
    void BeginOcclusion(QueryObject& q) { q.Frame = FrameSerial(); }
    void EndOcclusion(QueryObject& q) { (void)q; }

    bool ReadOcclusion(QueryObject& q, bool wait, uint64_t& samples)
    {
        (void)q;
        (void)wait;
        samples = 1;
        return true;
    }

    namespace
    {
        // Ön yüzde olmayan, motorun kullandığı iki giriş: doku kopyası (TAA geçmişi ...) ve çerçeve arabelleği durumu
        void APIENTRY W_CopyImageSubData(GLuint src, GLenum srcTarget, GLint srcLevel, GLint srcX, GLint srcY, GLint srcZ, GLuint dst,
                                         GLenum dstTarget, GLint dstLevel, GLint dstX, GLint dstY, GLint dstZ, GLsizei w, GLsizei h, GLsizei d)
        {
            Texture* s = srcTarget == GL_RENDERBUFFER ? GetRenderbuffer(src) : GetTexture(src);
            Texture* t = dstTarget == GL_RENDERBUFFER ? GetRenderbuffer(dst) : GetTexture(dst);
            if (!s || !t || !s->Image || !t->Image || s->Samples > 1 || t->Samples > 1 || w <= 0 || h <= 0)
                return;
            EndRendering();
            WGPUTexelCopyTextureInfo a{};
            a.texture = s->Image;
            a.mipLevel = (uint32_t)srcLevel;
            a.origin = { (uint32_t)srcX, (uint32_t)srcY, (uint32_t)srcZ };
            a.aspect = WGPUTextureAspect_All;
            WGPUTexelCopyTextureInfo b{};
            b.texture = t->Image;
            b.mipLevel = (uint32_t)dstLevel;
            b.origin = { (uint32_t)dstX, (uint32_t)dstY, (uint32_t)dstZ };
            b.aspect = WGPUTextureAspect_All;
            WGPUExtent3D size{ (uint32_t)w, (uint32_t)h, (uint32_t)std::max(1, (int)d) };
            WGPU::W().CommandEncoderCopyTextureToTexture(Encoder(), &a, &b, &size);
            t->DefinedLevels |= 1u << dstLevel;
        }

        GLenum APIENTRY W_CheckNamedFramebufferStatus(GLuint, GLenum) { return GL_FRAMEBUFFER_COMPLETE; }

        void APIENTRY W_ObjectLabel(GLenum identifier, GLuint name, GLsizei length, const GLchar* label)
        {
            if (identifier != GL_PROGRAM || !label)
                return;
            if (Program* p = GetProgram(name))
                p->Label = length < 0 ? std::string(label) : std::string(label, (size_t)length);
        }

        // glClearBuffer*: çizim çerçeve arabelleğinin tek eki (renk eki i, derinlik, şablon) kendi geçişinde temizlenir
        void ClearAttachment(GLenum buffer, GLint drawbuffer, const float color[4], float depth, int stencil, bool hasDepth, bool hasStencil)
        {
            auto& c = Ctx();
            Framebuffer* fb = GetFramebuffer(c.S.DrawFBO);
            if (!fb)
                return;
            Texture* t = nullptr;
            int level = 0, layer = 0;
            if (buffer == GL_COLOR)
            {
                if (drawbuffer < 0 || drawbuffer >= fb->NumDrawBuffers || fb->DrawBuffers[drawbuffer] == GL_NONE)
                    return;
                const int idx = c.S.DrawFBO == 0 ? 0 : (int)(fb->DrawBuffers[drawbuffer] - GL_COLOR_ATTACHMENT0);
                if (idx < 0 || idx >= 8)
                    return;
                t = AttachmentTexture(fb->Color[idx]);
                level = fb->Color[idx].Level;
                layer = fb->Color[idx].Layer;
            }
            else
            {
                const Attachment& a = fb->Depth.Valid() ? fb->Depth : fb->Stencil;
                t = AttachmentTexture(a);
                level = a.Level;
                layer = a.Layer;
            }
            if (!t || !t->Image)
                return;
            EndRendering();
            WGPUTextureView view = TargetView(*t, level, layer);
            WGPURenderPassColorAttachment ca{};
            WGPURenderPassDepthStencilAttachment da{};
            WGPURenderPassDescriptor rp{};
            if (buffer == GL_COLOR)
            {
                ca.view = view;
                ca.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
                ca.loadOp = WGPULoadOp_Clear;
                ca.storeOp = WGPUStoreOp_Store;
                ca.clearValue = { color[0], color[1], color[2], color[3] };
                rp.colorAttachmentCount = 1;
                rp.colorAttachments = &ca;
            }
            else
            {
                da.view = view;
                da.depthLoadOp = hasDepth && t->Depth ? WGPULoadOp_Clear : WGPULoadOp_Load;
                da.depthStoreOp = WGPUStoreOp_Store;
                da.depthClearValue = std::clamp(depth, 0.0f, 1.0f);
                if (t->Stencil)
                {
                    da.stencilLoadOp = hasStencil ? WGPULoadOp_Clear : WGPULoadOp_Load;
                    da.stencilStoreOp = WGPUStoreOp_Store;
                    da.stencilClearValue = (uint32_t)(stencil & 0xFF);
                }
                rp.depthStencilAttachment = &da;
            }
            BeginPass(rp);
            EndPass();
            WGPU::W().TextureViewRelease(view);
            t->DefinedLevels |= 1u << level;
        }

        void APIENTRY W_ClearBufferfv(GLenum buffer, GLint drawbuffer, const GLfloat* v)
        {
            if (!v)
                return;
            const float zero[4] = { 0, 0, 0, 0 };
            if (buffer == GL_COLOR)
                ClearAttachment(buffer, drawbuffer, v, 1.0f, 0, false, false);
            else if (buffer == GL_DEPTH)
                ClearAttachment(buffer, drawbuffer, zero, v[0], 0, true, false);
        }
        void APIENTRY W_ClearBufferiv(GLenum buffer, GLint drawbuffer, const GLint* v)
        {
            if (!v)
                return;
            const float c[4] = { (float)v[0], (float)v[1], (float)v[2], (float)v[3] };
            if (buffer == GL_COLOR)
                ClearAttachment(buffer, drawbuffer, c, 1.0f, 0, false, false);
            else if (buffer == GL_STENCIL)
                ClearAttachment(buffer, drawbuffer, c, 1.0f, v[0], false, true);
        }
        void APIENTRY W_ClearBufferuiv(GLenum buffer, GLint drawbuffer, const GLuint* v)
        {
            if (!v || buffer != GL_COLOR)
                return;
            const float c[4] = { (float)v[0], (float)v[1], (float)v[2], (float)v[3] };
            ClearAttachment(buffer, drawbuffer, c, 1.0f, 0, false, false);
        }
        void APIENTRY W_ClearBufferfi(GLenum buffer, GLint drawbuffer, GLfloat depth, GLint stencil)
        {
            const float zero[4] = { 0, 0, 0, 0 };
            ClearAttachment(buffer, drawbuffer, zero, depth, stencil, true, true);
        }
    }

    bool Install(std::string& error)
    {
        auto& c = Ctx();
        if (!Device())
        {
            error = "WebGPU device is not initialized";
            return false;
        }
        c.Vendor = "WebGPU";
        c.Renderer = DeviceName() + " (WebGPU)";
        c.Version = "4.6.0 Conqueror WgpuGL";
        c.Extensions = { "GL_ARB_clip_control", "GL_ARB_direct_state_access", "GL_ARB_texture_storage", "GL_ARB_texture_filter_anisotropic",
                         "GL_EXT_texture_filter_anisotropic" };
        const WGPULimits& dl = DeviceLimits();
        s_Limits.MaxTextureSize = (int32_t)std::max<uint32_t>(8192, dl.maxTextureDimension2D);
        s_Limits.MaxCubeMapSize = s_Limits.MaxTextureSize;
        s_Limits.MaxTextureUnits = (int32_t)std::max<uint32_t>(16, dl.maxSampledTexturesPerShaderStage);
        s_Limits.MaxVertexAttribs = 16;
        s_Limits.MaxUniformBufferBindings = (int32_t)std::max<uint32_t>(12, dl.maxUniformBuffersPerShaderStage);
        s_Limits.MaxUniformBlockSize = 65536;
        s_Limits.UniformBufferAlignment = 256;
        s_Limits.MaxColorAttachments = 8;
        s_Limits.MaxViewport[0] = s_Limits.MaxViewport[1] = s_Limits.MaxTextureSize;
        s_Limits.MaxAnisotropy = 16.0f;
        s_Limits.LineWidthRange[0] = s_Limits.LineWidthRange[1] = 1.0f;
        s_Limits.VideoMemoryKB = 0;

        ShaderCompiler::SetRelaxedTargetVulkan11(true);   // Tint: SPIR-V 1.3
        ShaderCompiler::SetTextureBudget16(DeviceLimits().maxSampledTexturesPerShaderStage < 24);
        if (!InitDrawHelpers())
        {
            error = "Cannot create the default vertex attribute / uniform buffers";
            return false;
        }
        c.Live = true;
        const uint8_t black[4] = { 0, 0, 0, 255 };
        const int32_t zero = 0;
        const float one = 1.0f;
        c.NullTexture2D = MakeInternalTexture(GL_TEXTURE_2D, GL_RGBA8, black, GL_RGBA, GL_UNSIGNED_BYTE);
        c.NullTextureCube = MakeInternalTexture(GL_TEXTURE_CUBE_MAP, GL_RGBA8, black, GL_RGBA, GL_UNSIGNED_BYTE);
        c.NullTextureInt = MakeInternalTexture(GL_TEXTURE_2D, GL_R32I, &zero, GL_RED_INTEGER, GL_INT);
        {
            // Gölge örnekleyicisi için boş derinlik dokusu (1.0 = gölgesiz)
            auto t = std::make_unique<Texture>();
            t->MinFilter = t->MagFilter = GL_NEAREST;
            AllocateTexture(*t, GL_TEXTURE_2D, GL_DEPTH_COMPONENT32F, 1, 1, 1, 1);
            t->Immutable = true;
            t->DefinedLevels = 1;
            ClearTexture(*t, 0, GL_DEPTH_COMPONENT, GL_FLOAT, &one);
            c.InternalTextures.push_back(std::move(t));
            c.NullTextureDepth = c.InternalTextures.back().get();
        }

        c.DefaultFB.Color[0].Texture = 0xFFFFFFF0u;
        c.DefaultFB.Depth.Texture = 0xFFFFFFF1u;
        c.DefaultFB.Stencil.Texture = 0xFFFFFFF1u;
        c.DefaultFB.ReadBuffer = GL_COLOR_ATTACHMENT0;
        BeginFrameResources();
        const GLint w = (GLint)c.BackColor.Width, h = (GLint)c.BackColor.Height;
        c.S.Viewport[2] = c.S.Scissor[2] = w;
        c.S.Viewport[3] = c.S.Scissor[3] = h;

        if (!gladLoadGLLoader((GLADloadproc)&GetProc))
        {
            error = "glad could not load the GL translation layer";
            return false;
        }
        if (!glad_glCopyImageSubData)
            glad_glCopyImageSubData = &W_CopyImageSubData;
        if (!glad_glCheckNamedFramebufferStatus)
            glad_glCheckNamedFramebufferStatus = &W_CheckNamedFramebufferStatus;
        if (!glad_glObjectLabel)
            glad_glObjectLabel = &W_ObjectLabel;
        if (!glad_glClearBufferfv) glad_glClearBufferfv = &W_ClearBufferfv;
        if (!glad_glClearBufferiv) glad_glClearBufferiv = &W_ClearBufferiv;
        if (!glad_glClearBufferuiv) glad_glClearBufferuiv = &W_ClearBufferuiv;
        if (!glad_glClearBufferfi) glad_glClearBufferfi = &W_ClearBufferfi;
        const int missing = GLProfile::InstallMissingStubs();   // Web: uygulanmamış girişler için imzası aynı saplamalar
        CQ_CORE_INFO("[WgpuGL] GL 4.6 translation layer ready on {0} ({1}, {2} unimplemented entry points stubbed)", DeviceName(),
                     WGPU::LibraryName(), g_StubCount + (size_t)missing);
        return true;
    }

    void Shutdown()
    {
        auto& c = Ctx();
        if (!Device())
            return;
        EndRendering();
        c.Live = false;
        for (auto& [n, t] : c.Textures) DestroyTextureImage(*t);
        for (auto& [n, t] : c.Renderbuffers) DestroyTextureImage(*t);
        for (auto& t : c.InternalTextures) DestroyTextureImage(*t);
        DestroyTextureImage(c.BackColor);
        DestroyTextureImage(c.BackDepth);
        for (auto& [n, b] : c.Buffers) ReleaseBuffer(*b);
        for (auto& [n, p] : c.Programs) DestroyProgram(*p);
        ReleaseDrawHelpers();
        ReleaseSamplers();
        c = Context{};
    }
}

#endif
