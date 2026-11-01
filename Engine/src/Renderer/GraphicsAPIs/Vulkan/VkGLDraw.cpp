#ifdef CQ_VULKAN_ENABLED

#include "VkGL.h"
#include "Core/Logging/Log.h"

#include <algorithm>
#include <cstring>

namespace Conqueror::VkGL
{
    using VK::Core;

    namespace
    {
        struct Hasher
        {
            uint64_t H = 1469598103934665603ull;
            void Add(uint64_t v) { H = (H ^ v) * 1099511628211ull; }
        };

        VkBlendFactor ToBlend(GLenum f)
        {
            switch (f)
            {
                case GL_ZERO: return VK_BLEND_FACTOR_ZERO;
                case GL_ONE: return VK_BLEND_FACTOR_ONE;
                case GL_SRC_COLOR: return VK_BLEND_FACTOR_SRC_COLOR;
                case GL_ONE_MINUS_SRC_COLOR: return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
                case GL_DST_COLOR: return VK_BLEND_FACTOR_DST_COLOR;
                case GL_ONE_MINUS_DST_COLOR: return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
                case GL_SRC_ALPHA: return VK_BLEND_FACTOR_SRC_ALPHA;
                case GL_ONE_MINUS_SRC_ALPHA: return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
                case GL_DST_ALPHA: return VK_BLEND_FACTOR_DST_ALPHA;
                case GL_ONE_MINUS_DST_ALPHA: return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
                case GL_CONSTANT_COLOR: return VK_BLEND_FACTOR_CONSTANT_COLOR;
                case GL_ONE_MINUS_CONSTANT_COLOR: return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;
                case GL_SRC_ALPHA_SATURATE: return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
                default: return VK_BLEND_FACTOR_ONE;
            }
        }

        VkBlendOp ToBlendOp(GLenum e)
        {
            switch (e)
            {
                case GL_FUNC_SUBTRACT: return VK_BLEND_OP_SUBTRACT;
                case GL_FUNC_REVERSE_SUBTRACT: return VK_BLEND_OP_REVERSE_SUBTRACT;
                case GL_MIN: return VK_BLEND_OP_MIN;
                case GL_MAX: return VK_BLEND_OP_MAX;
                default: return VK_BLEND_OP_ADD;
            }
        }

        VkCompareOp ToCompare(GLenum f)
        {
            switch (f)
            {
                case GL_NEVER: return VK_COMPARE_OP_NEVER;
                case GL_LESS: return VK_COMPARE_OP_LESS;
                case GL_EQUAL: return VK_COMPARE_OP_EQUAL;
                case GL_LEQUAL: return VK_COMPARE_OP_LESS_OR_EQUAL;
                case GL_GREATER: return VK_COMPARE_OP_GREATER;
                case GL_NOTEQUAL: return VK_COMPARE_OP_NOT_EQUAL;
                case GL_GEQUAL: return VK_COMPARE_OP_GREATER_OR_EQUAL;
                default: return VK_COMPARE_OP_ALWAYS;
            }
        }

        VkStencilOp ToStencilOp(GLenum o)
        {
            switch (o)
            {
                case GL_ZERO: return VK_STENCIL_OP_ZERO;
                case GL_REPLACE: return VK_STENCIL_OP_REPLACE;
                case GL_INCR: return VK_STENCIL_OP_INCREMENT_AND_CLAMP;
                case GL_DECR: return VK_STENCIL_OP_DECREMENT_AND_CLAMP;
                case GL_INVERT: return VK_STENCIL_OP_INVERT;
                case GL_INCR_WRAP: return VK_STENCIL_OP_INCREMENT_AND_WRAP;
                case GL_DECR_WRAP: return VK_STENCIL_OP_DECREMENT_AND_WRAP;
                default: return VK_STENCIL_OP_KEEP;
            }
        }

        VkPrimitiveTopology ToTopology(GLenum mode)
        {
            switch (mode)
            {
                case GL_POINTS: return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
                case GL_LINES: return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
                case GL_LINE_STRIP: case GL_LINE_LOOP: return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
                case GL_TRIANGLE_STRIP: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
                case GL_TRIANGLE_FAN: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;
                default: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            }
        }

        VkFormat AttribFormat(const Attrib& a)
        {
            const int n = std::clamp(a.Size, 1, 4);
            switch (a.Type)
            {
                case GL_FLOAT:
                {
                    const VkFormat f[] = { VK_FORMAT_R32_SFLOAT, VK_FORMAT_R32G32_SFLOAT, VK_FORMAT_R32G32B32_SFLOAT, VK_FORMAT_R32G32B32A32_SFLOAT };
                    return f[n - 1];
                }
                case GL_HALF_FLOAT:
                {
                    const VkFormat f[] = { VK_FORMAT_R16_SFLOAT, VK_FORMAT_R16G16_SFLOAT, VK_FORMAT_R16G16B16_SFLOAT, VK_FORMAT_R16G16B16A16_SFLOAT };
                    return f[n - 1];
                }
                case GL_INT:
                {
                    if (a.Integer)
                    {
                        const VkFormat f[] = { VK_FORMAT_R32_SINT, VK_FORMAT_R32G32_SINT, VK_FORMAT_R32G32B32_SINT, VK_FORMAT_R32G32B32A32_SINT };
                        return f[n - 1];
                    }
                    const VkFormat f[] = { VK_FORMAT_R32_SFLOAT, VK_FORMAT_R32G32_SFLOAT, VK_FORMAT_R32G32B32_SFLOAT, VK_FORMAT_R32G32B32A32_SFLOAT };
                    return f[n - 1];
                }
                case GL_UNSIGNED_INT:
                {
                    const VkFormat f[] = { VK_FORMAT_R32_UINT, VK_FORMAT_R32G32_UINT, VK_FORMAT_R32G32B32_UINT, VK_FORMAT_R32G32B32A32_UINT };
                    return f[n - 1];
                }
                case GL_UNSIGNED_BYTE:
                {
                    if (a.Integer)
                    {
                        const VkFormat f[] = { VK_FORMAT_R8_UINT, VK_FORMAT_R8G8_UINT, VK_FORMAT_R8G8B8_UINT, VK_FORMAT_R8G8B8A8_UINT };
                        return f[n - 1];
                    }
                    if (a.Normalized)
                    {
                        const VkFormat f[] = { VK_FORMAT_R8_UNORM, VK_FORMAT_R8G8_UNORM, VK_FORMAT_R8G8B8_UNORM, VK_FORMAT_R8G8B8A8_UNORM };
                        return f[n - 1];
                    }
                    const VkFormat f[] = { VK_FORMAT_R8_USCALED, VK_FORMAT_R8G8_USCALED, VK_FORMAT_R8G8B8_USCALED, VK_FORMAT_R8G8B8A8_USCALED };
                    return f[n - 1];
                }
                case GL_SHORT:
                {
                    if (a.Normalized)
                    {
                        const VkFormat f[] = { VK_FORMAT_R16_SNORM, VK_FORMAT_R16G16_SNORM, VK_FORMAT_R16G16B16_SNORM, VK_FORMAT_R16G16B16A16_SNORM };
                        return f[n - 1];
                    }
                    const VkFormat f[] = { VK_FORMAT_R16_SINT, VK_FORMAT_R16G16_SINT, VK_FORMAT_R16G16B16_SINT, VK_FORMAT_R16G16B16A16_SINT };
                    return f[n - 1];
                }
                case GL_UNSIGNED_SHORT:
                {
                    const VkFormat f[] = { VK_FORMAT_R16_UINT, VK_FORMAT_R16G16_UINT, VK_FORMAT_R16G16B16_UINT, VK_FORMAT_R16G16B16A16_UINT };
                    return f[n - 1];
                }
                default:
                    return VK_FORMAT_R32G32B32A32_SFLOAT;
            }
        }

        uint32_t AttribSize(const Attrib& a)
        {
            uint32_t comp = a.Type == GL_UNSIGNED_BYTE || a.Type == GL_BYTE ? 1 : (a.Type == GL_SHORT || a.Type == GL_UNSIGNED_SHORT || a.Type == GL_HALF_FLOAT) ? 2 : 4;
            return comp * (uint32_t)std::clamp(a.Size, 1, 4);
        }

        bool IsDepthFormat(VkFormat f)
        {
            return f == VK_FORMAT_D32_SFLOAT || f == VK_FORMAT_D16_UNORM || f == VK_FORMAT_D24_UNORM_S8_UINT || f == VK_FORMAT_D32_SFLOAT_S8_UINT;
        }

        bool HasStencil(VkFormat f)
        {
            return f == VK_FORMAT_D24_UNORM_S8_UINT || f == VK_FORMAT_D32_SFLOAT_S8_UINT;
        }

        VkDescriptorSet AllocateSet(VkDescriptorSetLayout layout)
        {
            auto& c = Ctx();
            auto& core = Core::Get();
            core.BeginFrameIfNeeded();
            const uint32_t slot = core.FrameSlot();
            VkDevice dev = core.Device();
            if (c.PoolFrame[slot] != core.FrameNumber())
            {
                for (auto pool : c.Pools[slot])
                    vkResetDescriptorPool(dev, pool, 0);
                c.PoolFrame[slot] = core.FrameNumber();
                c.PoolIndex[slot] = 0;
            }
            for (;;)
            {
                if (c.PoolIndex[slot] >= c.Pools[slot].size())
                {
                    VkDescriptorPoolSize sizes[] = { { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 8192 }, { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 32768 },
                                                     { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 16384 }, { VK_DESCRIPTOR_TYPE_SAMPLER, 4096 } };
                    VkDescriptorPoolCreateInfo pi{ VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
                    pi.maxSets = 4096;
                    pi.poolSizeCount = 4;
                    pi.pPoolSizes = sizes;
                    VkDescriptorPool pool = VK_NULL_HANDLE;
                    if (vkCreateDescriptorPool(dev, &pi, nullptr, &pool) != VK_SUCCESS)
                        return VK_NULL_HANDLE;
                    c.Pools[slot].push_back(pool);
                }
                VkDescriptorSetAllocateInfo ai{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
                ai.descriptorPool = c.Pools[slot][c.PoolIndex[slot]];
                ai.descriptorSetCount = 1;
                ai.pSetLayouts = &layout;
                VkDescriptorSet set = VK_NULL_HANDLE;
                const VkResult r = vkAllocateDescriptorSets(dev, &ai, &set);
                if (r == VK_SUCCESS)
                    return set;
                ++c.PoolIndex[slot];   // dolu: sonraki havuz
            }
        }

        // Etkin çizim eklerini topla (GL çizim arabelleği eşlemesi dahil)
        struct Targets
        {
            Texture* Color[8] = {};
            int ColorLevel[8] = {}, ColorLayer[8] = {};
            int ColorCount = 0;
            Texture* Depth = nullptr;
            int DepthLevel = 0, DepthLayer = 0;
            Texture* Stencil = nullptr;
            int StencilLevel = 0, StencilLayer = 0;
        };

        Targets CollectTargets(uint32_t fboName)
        {
            auto& c = Ctx();
            Targets t;
            if (fboName == 0)
            {
                t.Color[0] = &c.BackColor;
                t.ColorCount = 1;
                t.Depth = &c.BackDepth;
                t.Stencil = &c.BackDepth;
                return t;
            }
            Framebuffer* fb = GetFramebuffer(fboName);
            if (!fb)
                return t;
            for (int i = 0; i < fb->NumDrawBuffers && i < 8; ++i)
            {
                const GLenum b = fb->DrawBuffers[i];
                if (b >= GL_COLOR_ATTACHMENT0 && b < GL_COLOR_ATTACHMENT0 + 8)
                {
                    const Attachment& a = fb->Color[b - GL_COLOR_ATTACHMENT0];
                    t.Color[i] = AttachmentTexture(a);
                    t.ColorLevel[i] = a.Level;
                    t.ColorLayer[i] = a.Layer;
                }
                t.ColorCount = i + 1;
            }
            // Sondaki boş ekleri at
            while (t.ColorCount > 0 && !(t.Color[t.ColorCount - 1] && t.Color[t.ColorCount - 1]->Image))
                --t.ColorCount;
            if (fb->Depth.Valid())
            {
                t.Depth = AttachmentTexture(fb->Depth);
                t.DepthLevel = fb->Depth.Level;
                t.DepthLayer = fb->Depth.Layer;
            }
            if (fb->Stencil.Valid())
            {
                t.Stencil = AttachmentTexture(fb->Stencil);
                t.StencilLevel = fb->Stencil.Level;
                t.StencilLayer = fb->Stencil.Layer;
            }
            return t;
        }
    }

    void EndRendering()
    {
        auto& c = Ctx();
        if (!c.Rendering)
            return;
        VkCommandBuffer cmd = Core::Get().Cmd();
        StopOcclusionSegment();   // sorgu kapsamı aşamaz
        vkCmdEndRendering(cmd);
        Core::Get().FullBarrier(cmd);
        c.Rendering = false;
        c.RenderingFBO = UINT32_MAX;
        c.BoundPipelineKey = 0;
    }

    VkCommandBuffer TransferCmd()
    {
        auto& core = Core::Get();
        VkCommandBuffer cmd = core.Cmd();
        if (Ctx().Rendering)
            EndRendering();
        else
            core.FullBarrier(cmd);
        return cmd;
    }

    bool EnsureRendering()
    {
        auto& c = Ctx();
        auto& core = Core::Get();
        Targets t = CollectTargets(c.S.DrawFBO);

        VkImageView colorViews[8] = {};
        Hasher h;
        h.Add(c.S.DrawFBO);
        uint32_t width = UINT32_MAX, height = UINT32_MAX, samples = 1;
        for (int i = 0; i < t.ColorCount; ++i)
        {
            if (t.Color[i] && t.Color[i]->Image)
            {
                colorViews[i] = GetAttachmentView(*t.Color[i], t.ColorLevel[i], t.ColorLayer[i]);
                width = std::min(width, std::max(1u, t.Color[i]->Width >> t.ColorLevel[i]));
                height = std::min(height, std::max(1u, t.Color[i]->Height >> t.ColorLevel[i]));
                samples = t.Color[i]->Samples;
            }
            h.Add((uint64_t)colorViews[i]);
        }
        VkImageView depthView = VK_NULL_HANDLE, stencilView = VK_NULL_HANDLE;
        VkFormat depthFormat = VK_FORMAT_UNDEFINED, stencilFormat = VK_FORMAT_UNDEFINED;
        if (t.Depth && t.Depth->Image && IsDepthFormat(t.Depth->Format))
        {
            depthView = GetAttachmentView(*t.Depth, t.DepthLevel, t.DepthLayer);
            depthFormat = t.Depth->Format;
            width = std::min(width, std::max(1u, t.Depth->Width >> t.DepthLevel));
            height = std::min(height, std::max(1u, t.Depth->Height >> t.DepthLevel));
            samples = t.Depth->Samples;
            if (HasStencil(t.Depth->Format))
            {
                stencilView = depthView;
                stencilFormat = depthFormat;
            }
        }
        if (!stencilView && t.Stencil && t.Stencil->Image && HasStencil(t.Stencil->Format))
        {
            stencilView = GetAttachmentView(*t.Stencil, t.StencilLevel, t.StencilLayer);
            stencilFormat = t.Stencil->Format;
        }
        h.Add((uint64_t)depthView);
        h.Add((uint64_t)stencilView);
        if (width == UINT32_MAX)
            return false;   // eksiz çerçeve arabelleği

        if (c.Rendering && c.RenderingKey == h.H)
            return true;
        EndRendering();

        VkRenderingAttachmentInfo colors[8]{};
        c.RenderingColorFormats.assign((size_t)t.ColorCount, VK_FORMAT_UNDEFINED);
        c.RenderingColorInteger.assign((size_t)t.ColorCount, false);
        for (int i = 0; i < t.ColorCount; ++i)
        {
            colors[i].sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
            colors[i].imageView = colorViews[i];
            colors[i].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
            colors[i].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
            colors[i].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            if (colorViews[i])
            {
                c.RenderingColorFormats[(size_t)i] = t.Color[i]->Format;
                c.RenderingColorInteger[(size_t)i] = t.Color[i]->Integer;
            }
        }
        VkRenderingAttachmentInfo depth{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
        depth.imageView = depthView;
        depth.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        depth.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        VkRenderingAttachmentInfo stencil = depth;
        stencil.imageView = stencilView;

        VkRenderingInfo ri{ VK_STRUCTURE_TYPE_RENDERING_INFO };
        ri.renderArea = { { 0, 0 }, { width, height } };
        ri.layerCount = 1;
        ri.colorAttachmentCount = (uint32_t)t.ColorCount;
        ri.pColorAttachments = colors;
        ri.pDepthAttachment = depthView ? &depth : nullptr;
        ri.pStencilAttachment = stencilView ? &stencil : nullptr;
        VkCommandBuffer cmd = core.Cmd();
        vkCmdBeginRendering(cmd, &ri);
        c.Rendering = true;
        c.RenderingFBO = c.S.DrawFBO;
        c.RenderingKey = h.H;
        c.RenderArea = { width, height };
        c.RenderingDepthFormat = depthView ? depthFormat : VK_FORMAT_UNDEFINED;
        c.RenderingStencilFormat = stencilView ? stencilFormat : VK_FORMAT_UNDEFINED;
        c.RenderingSamples = samples;
        c.BoundPipelineKey = 0;
        StartOcclusionSegment();
        return true;
    }

    void ClearCurrent(GLbitfield mask)
    {
        auto& c = Ctx();
        if (!EnsureRendering())
            return;
        std::vector<VkClearAttachment> clears;
        if (mask & GL_COLOR_BUFFER_BIT)
        {
            for (size_t i = 0; i < c.RenderingColorFormats.size(); ++i)
            {
                if (c.RenderingColorFormats[i] == VK_FORMAT_UNDEFINED)
                    continue;
                VkClearAttachment a{};
                a.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                a.colorAttachment = (uint32_t)i;
                if (c.RenderingColorInteger[i])
                    for (int k = 0; k < 4; ++k)
                        a.clearValue.color.int32[k] = (int32_t)c.S.ClearColor[k];
                else
                    for (int k = 0; k < 4; ++k)
                        a.clearValue.color.float32[k] = c.S.ClearColor[k];
                clears.push_back(a);
            }
        }
        VkImageAspectFlags ds = 0;
        if ((mask & GL_DEPTH_BUFFER_BIT) && c.RenderingDepthFormat != VK_FORMAT_UNDEFINED && c.S.DepthMask)
            ds |= VK_IMAGE_ASPECT_DEPTH_BIT;
        if ((mask & GL_STENCIL_BUFFER_BIT) && c.RenderingStencilFormat != VK_FORMAT_UNDEFINED)
            ds |= VK_IMAGE_ASPECT_STENCIL_BIT;
        if (ds)
        {
            VkClearAttachment a{};
            a.aspectMask = ds;
            a.clearValue.depthStencil = { std::clamp(c.S.ClearDepth, 0.0f, 1.0f), (uint32_t)c.S.ClearStencil };
            clears.push_back(a);
        }
        if (clears.empty())
            return;
        VkClearRect rect{};
        rect.layerCount = 1;
        rect.rect = { { 0, 0 }, c.RenderArea };
        if (c.S.ScissorTest)
        {
            const int x0 = std::clamp(c.S.Scissor[0], 0, (int)c.RenderArea.width);
            const int y0 = std::clamp(c.S.Scissor[1], 0, (int)c.RenderArea.height);
            const int x1 = std::clamp(c.S.Scissor[0] + c.S.Scissor[2], 0, (int)c.RenderArea.width);
            const int y1 = std::clamp(c.S.Scissor[1] + c.S.Scissor[3], 0, (int)c.RenderArea.height);
            if (x1 <= x0 || y1 <= y0)
                return;
            rect.rect = { { x0, y0 }, { (uint32_t)(x1 - x0), (uint32_t)(y1 - y0) } };
        }
        vkCmdClearAttachments(Core::Get().Cmd(), (uint32_t)clears.size(), clears.data(), 1, &rect);
    }

    void Draw(GLenum mode, GLint first, GLsizei count, GLsizei instances, bool indexed, GLenum indexType, const void* indices, GLint baseVertex)
    {
        auto& c = Ctx();
        auto& core = Core::Get();
        if (count <= 0 || instances <= 0)
            return;
        Program* p = GetProgram(c.S.Program);
        if (!p || !p->Linked)
            return;
        if (!EnsureRendering())
            return;
        VkCommandBuffer cmd = core.Cmd();
        VertexArray* vao = CurrentVAO();

        // ── Köşe girdileri ──
        // Çizim başına ayırma yapmamak için yeniden kullanılan geçici diziler
        static thread_local std::vector<VkVertexInputBindingDescription> bindings;
        static thread_local std::vector<VkVertexInputAttributeDescription> attributes;
        static thread_local std::vector<VkBuffer> vbufs;
        static thread_local std::vector<VkDeviceSize> voffs;
        bindings.clear();
        attributes.clear();
        vbufs.clear();
        voffs.clear();
        Hasher key;
        key.Add(p->Id);
        for (const auto& in : p->Inputs)
        {
            if (in.Location < 0 || in.Location >= 16)
                continue;
            const Attrib& a = vao->Attribs[in.Location];
            Buffer* buf = a.Enabled ? GetBuffer(a.Buffer) : nullptr;
            VkVertexInputBindingDescription bd{};
            bd.binding = (uint32_t)bindings.size();
            VkVertexInputAttributeDescription ad{};
            ad.location = (uint32_t)in.Location;
            ad.binding = bd.binding;
            if (buf)
            {
                bd.stride = a.Stride ? (uint32_t)a.Stride : AttribSize(a);
                bd.inputRate = a.Divisor ? VK_VERTEX_INPUT_RATE_INSTANCE : VK_VERTEX_INPUT_RATE_VERTEX;
                ad.format = AttribFormat(a);
                VkBuffer vb;
                VkDeviceSize off;
                BufferRef(*buf, vb, off);
                vbufs.push_back(vb);
                voffs.push_back(off + a.Offset);
            }
            else
            {
                // GL varsayılan özniteliği (0,0,0,1): sıfır adımlı boş arabellek
                bd.stride = 0;
                bd.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
                ad.format = in.Integer ? VK_FORMAT_R32G32B32A32_SINT : VK_FORMAT_R32G32B32A32_SFLOAT;
                vbufs.push_back(c.NullVertexBuffer);
                voffs.push_back(in.Integer ? 16 : 0);
            }
            bindings.push_back(bd);
            attributes.push_back(ad);
            key.Add(((uint64_t)in.Location << 40) | ((uint64_t)ad.format << 8) | bd.inputRate);
            key.Add(bd.stride);
        }

        // ── Boru hattı anahtarı ──
        const VkPrimitiveTopology topology = ToTopology(mode);
        key.Add(topology);
        key.Add(c.S.PolygonMode);
        key.Add(c.S.Blend);
        key.Add(c.S.BlendSrcRGB); key.Add(c.S.BlendDstRGB); key.Add(c.S.BlendSrcA); key.Add(c.S.BlendDstA);
        key.Add(c.S.BlendEqRGB); key.Add(c.S.BlendEqA);
        for (uint8_t m : c.S.ColorMaskBits) key.Add(m);
        for (auto f : c.RenderingColorFormats) key.Add(f);
        key.Add(c.RenderingDepthFormat);
        key.Add(c.RenderingStencilFormat);
        key.Add(c.RenderingSamples);
        key.Add(c.S.PrimitiveRestart && indexed);
        const bool dynamicBias = core.HasDynamicDepthBiasEnable();
        if (!dynamicBias)
            key.Add(c.S.PolygonOffsetFill ? 1 : 0);   // dinamik değilse boru hattına gömülür

        VkPipeline pipeline = VK_NULL_HANDLE;
        auto pit = c.Pipelines.find(key.H);
        if (pit != c.Pipelines.end())
            pipeline = pit->second;
        else
        {
            VkPipelineShaderStageCreateInfo stages[2]{};
            stages[0] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, p->Vert, "main", nullptr };
            stages[1] = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, p->Frag, "main", nullptr };
            VkPipelineVertexInputStateCreateInfo vi{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
            vi.vertexBindingDescriptionCount = (uint32_t)bindings.size();
            vi.pVertexBindingDescriptions = bindings.data();
            vi.vertexAttributeDescriptionCount = (uint32_t)attributes.size();
            vi.pVertexAttributeDescriptions = attributes.data();
            VkPipelineInputAssemblyStateCreateInfo ia{ VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
            ia.topology = topology;
            ia.primitiveRestartEnable = (c.S.PrimitiveRestart && indexed &&
                                         (topology == VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP || topology == VK_PRIMITIVE_TOPOLOGY_LINE_STRIP ||
                                          topology == VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN)) ? VK_TRUE : VK_FALSE;
            VkPipelineViewportStateCreateInfo vp{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
            vp.viewportCount = 1;
            vp.scissorCount = 1;
            VkPipelineRasterizationStateCreateInfo rs{ VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
            rs.polygonMode = (c.S.PolygonMode == GL_LINE && core.Features().fillModeNonSolid) ? VK_POLYGON_MODE_LINE :
                             (c.S.PolygonMode == GL_POINT && core.Features().fillModeNonSolid) ? VK_POLYGON_MODE_POINT : VK_POLYGON_MODE_FILL;
            rs.lineWidth = 1.0f;
            rs.depthClampEnable = VK_FALSE;
            rs.depthBiasEnable = (!dynamicBias && c.S.PolygonOffsetFill) ? VK_TRUE : VK_FALSE;
            VkPipelineMultisampleStateCreateInfo ms{ VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
            ms.rasterizationSamples = (VkSampleCountFlagBits)std::max(1u, c.RenderingSamples);
            VkPipelineDepthStencilStateCreateInfo ds{ VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
            ds.depthCompareOp = VK_COMPARE_OP_LESS;
            ds.maxDepthBounds = 1.0f;
            std::vector<VkPipelineColorBlendAttachmentState> blends(c.RenderingColorFormats.size());
            for (size_t i = 0; i < blends.size(); ++i)
            {
                auto& b = blends[i];
                const bool integer = c.RenderingColorInteger[i];
                b.blendEnable = (c.S.Blend && !integer && c.RenderingColorFormats[i] != VK_FORMAT_UNDEFINED) ? VK_TRUE : VK_FALSE;
                b.srcColorBlendFactor = ToBlend(c.S.BlendSrcRGB);
                b.dstColorBlendFactor = ToBlend(c.S.BlendDstRGB);
                b.colorBlendOp = ToBlendOp(c.S.BlendEqRGB);
                b.srcAlphaBlendFactor = ToBlend(c.S.BlendSrcA);
                b.dstAlphaBlendFactor = ToBlend(c.S.BlendDstA);
                b.alphaBlendOp = ToBlendOp(c.S.BlendEqA);
                b.colorWriteMask = i < 8 ? (VkColorComponentFlags)c.S.ColorMaskBits[i] : 0xF;   // R G B A bitleri Vulkan ile aynı
                // Gölgelendiricinin yazmadığı ek korunur (Vulkan'da yazılmayan çıktı tanımsızdır)
                if (i >= 32 || !(p->ColorOutputMask & (1u << i)))
                    b.colorWriteMask = 0;
            }
            // Bağımsız karıştırma yoksa tüm ekler aynı durumu kullanmalı
            if (!core.Features().independentBlend)
                for (size_t i = 1; i < blends.size(); ++i)
                    blends[i] = blends[0];
            VkPipelineColorBlendStateCreateInfo cb{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
            cb.attachmentCount = (uint32_t)blends.size();
            cb.pAttachments = blends.data();
            const VkDynamicState dyn[] = {
                VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_LINE_WIDTH, VK_DYNAMIC_STATE_DEPTH_BIAS,
                VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK, VK_DYNAMIC_STATE_STENCIL_WRITE_MASK, VK_DYNAMIC_STATE_STENCIL_REFERENCE,
                VK_DYNAMIC_STATE_CULL_MODE, VK_DYNAMIC_STATE_FRONT_FACE, VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE, VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE,
                VK_DYNAMIC_STATE_DEPTH_COMPARE_OP, VK_DYNAMIC_STATE_STENCIL_TEST_ENABLE, VK_DYNAMIC_STATE_STENCIL_OP, VK_DYNAMIC_STATE_DEPTH_BIAS_ENABLE,
            };
            VkPipelineDynamicStateCreateInfo dy{ VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
            // Son öğe (DEPTH_BIAS_ENABLE) yalnızca destekleniyorsa
            dy.dynamicStateCount = (uint32_t)(sizeof(dyn) / sizeof(dyn[0])) - (dynamicBias ? 0u : 1u);
            dy.pDynamicStates = dyn;
            VkPipelineRenderingCreateInfo rinfo{ VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO };
            rinfo.colorAttachmentCount = (uint32_t)c.RenderingColorFormats.size();
            rinfo.pColorAttachmentFormats = c.RenderingColorFormats.data();
            rinfo.depthAttachmentFormat = c.RenderingDepthFormat;
            rinfo.stencilAttachmentFormat = c.RenderingStencilFormat;
            VkGraphicsPipelineCreateInfo pci{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
            pci.pNext = &rinfo;
            pci.stageCount = 2;
            pci.pStages = stages;
            pci.pVertexInputState = &vi;
            pci.pInputAssemblyState = &ia;
            pci.pViewportState = &vp;
            pci.pRasterizationState = &rs;
            pci.pMultisampleState = &ms;
            pci.pDepthStencilState = &ds;
            pci.pColorBlendState = &cb;
            pci.pDynamicState = &dy;
            pci.layout = p->Layout;
            if (vkCreateGraphicsPipelines(core.Device(), core.PipelineCache(), 1, &pci, nullptr, &pipeline) != VK_SUCCESS)
            {
                CQ_CORE_ERROR("[VkGL] Pipeline creation failed");
                pipeline = VK_NULL_HANDLE;
            }
            c.Pipelines[key.H] = pipeline;
        }
        if (!pipeline)
            return;

        // ── Tanımlayıcılar ──
        Hasher setKey;
        setKey.Add(p->Id);
        static thread_local std::vector<VkDescriptorBufferInfo> bufferInfos;
        static thread_local std::vector<VkDescriptorImageInfo> imageInfos;
        bufferInfos.clear();
        imageInfos.clear();
        bufferInfos.reserve(p->Blocks.size() + 1);
        size_t imageCount = 0;
        for (const auto& s : p->Samplers)
            imageCount += s.Count + (s.SharedSampler ? 1 : 0);
        imageInfos.reserve(imageCount);
        struct WriteRef { uint32_t Binding; VkDescriptorType Type; uint32_t Count; size_t First; };
        static thread_local std::vector<WriteRef> refs;
        refs.clear();
        const VkDeviceSize uboAlign = core.Properties().limits.minUniformBufferOffsetAlignment;
        for (const auto& blk : p->Blocks)
        {
            VkDescriptorBufferInfo bi{};
            const uint32_t glb = blk.GLBinding < 64 ? blk.GLBinding : 0;
            Buffer* ub = GetBuffer(c.S.UBOBindings[glb]);
            if (ub)
            {
                BufferRef(*ub, bi.buffer, bi.offset);
                bi.offset += (VkDeviceSize)c.S.UBOOffsets[glb];
                const VkDeviceSize avail = ub->Shadow.size() > (size_t)c.S.UBOOffsets[glb] ? ub->Shadow.size() - (size_t)c.S.UBOOffsets[glb] : 0;
                bi.range = c.S.UBOSizes[glb] > 0 ? (VkDeviceSize)c.S.UBOSizes[glb] : std::min<VkDeviceSize>(avail, blk.Size);
                bi.range = std::max<VkDeviceSize>(std::min<VkDeviceSize>(bi.range, avail), 16);
            }
            else
            {
                // Bağlı arabellek yok: sıfırlı geçici veri
                VK::RingAlloc a = core.AllocRing(std::max(blk.Size, 16u), uboAlign);
                std::memset(a.Ptr, 0, std::max(blk.Size, 16u));
                bi.buffer = a.Buffer;
                bi.offset = a.Offset;
                bi.range = std::max(blk.Size, 16u);
            }
            setKey.Add((uint64_t)bi.buffer); setKey.Add(bi.offset); setKey.Add(bi.range);
            refs.push_back({ blk.Binding, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, bufferInfos.size() });
            bufferInfos.push_back(bi);
        }
        if (p->GlobalsBinding >= 0)
        {
            if (p->GlobalsDirty || p->GlobalsFrame != core.FrameNumber() || !p->GlobalsBuffer)
            {
                VK::RingAlloc a = core.AllocRing(p->Globals.size(), uboAlign);
                std::memcpy(a.Ptr, p->Globals.data(), p->Globals.size());
                p->GlobalsBuffer = a.Buffer;
                p->GlobalsOffset = a.Offset;
                p->GlobalsFrame = core.FrameNumber();
                p->GlobalsDirty = false;
            }
            VkDescriptorBufferInfo bi{ p->GlobalsBuffer, p->GlobalsOffset, (VkDeviceSize)p->Globals.size() };
            setKey.Add((uint64_t)bi.buffer); setKey.Add(bi.offset);
            refs.push_back({ (uint32_t)p->GlobalsBinding, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, bufferInfos.size() });
            bufferInfos.push_back(bi);
        }
        for (const auto& s : p->Samplers)
        {
            const size_t first = imageInfos.size();
            const Texture* shared = nullptr;
            for (uint32_t e = 0; e < s.Count; ++e)
            {
                const int unit = std::clamp(e < s.Units.size() ? s.Units[e] : 0, 0, 63);
                const uint32_t texName = s.Target == GL_TEXTURE_CUBE_MAP ? c.S.TexCube[(size_t)unit] :
                                         s.Target == GL_TEXTURE_2D_MULTISAMPLE ? c.S.Tex2DMS[(size_t)unit] : c.S.Tex2D[(size_t)unit];
                Texture* tex = GetTexture(texName);
                if (!tex || !tex->Image || (s.Target == GL_TEXTURE_CUBE_MAP) != (tex->Target == GL_TEXTURE_CUBE_MAP) || tex->Integer != s.Integer)
                    tex = s.Target == GL_TEXTURE_CUBE_MAP ? c.NullTextureCube : s.Integer ? c.NullTextureInt : c.NullTexture2D;
                const Texture* params = tex;
                if (auto so = c.SamplerObjects.find(c.S.Samplers[(size_t)unit]); so != c.SamplerObjects.end() && c.S.Samplers[(size_t)unit])
                    params = so->second.get();
                VkDescriptorImageInfo ii{};
                ii.imageView = GetSampledView(*tex);
                ii.sampler = s.SharedSampler ? VK_NULL_HANDLE : GetSampler(*params, tex->Integer);
                ii.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
                setKey.Add((uint64_t)ii.imageView); setKey.Add((uint64_t)ii.sampler);
                imageInfos.push_back(ii);
                if (s.SharedSampler && !shared && tex != c.NullTexture2D && tex != c.NullTextureCube && tex != c.NullTextureInt)
                    shared = params;
            }
            refs.push_back({ s.Binding, s.SharedSampler ? VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE : VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, s.Count, first });
            if (s.SharedSampler)
            {
                // Dizinin tamamı için tek örnekleyici: ilk geçerli dokunun parametreleri
                VkDescriptorImageInfo si{};
                si.sampler = GetSampler(shared ? *shared : *c.NullTexture2D, s.Integer);
                setKey.Add((uint64_t)si.sampler);
                refs.push_back({ s.SamplerSlot, VK_DESCRIPTOR_TYPE_SAMPLER, 1, imageInfos.size() });
                imageInfos.push_back(si);
            }
        }

        VkDescriptorSet set = VK_NULL_HANDLE;
        if (c.LastSetKey == setKey.H && c.LastSetFrame == core.FrameNumber() && c.LastSet)
            set = c.LastSet;
        else if (!refs.empty())
        {
            set = AllocateSet(p->SetLayout);
            if (!set)
                return;
            static thread_local std::vector<VkWriteDescriptorSet> writes;
            writes.clear();
            writes.reserve(refs.size());
            for (const auto& r : refs)
            {
                VkWriteDescriptorSet w{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
                w.dstSet = set;
                w.dstBinding = r.Binding;
                w.descriptorCount = r.Count;
                w.descriptorType = r.Type;
                if (r.Type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
                    w.pBufferInfo = &bufferInfos[r.First];
                else
                    w.pImageInfo = &imageInfos[r.First];
                writes.push_back(w);
            }
            vkUpdateDescriptorSets(core.Device(), (uint32_t)writes.size(), writes.data(), 0, nullptr);
            c.LastSetKey = setKey.H;
            c.LastSet = set;
            c.LastSetFrame = core.FrameNumber();
        }

        // ── Kayıt ──
        if (c.BoundPipelineKey != key.H)
        {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
            c.BoundPipelineKey = key.H;
        }
        if (set)
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, p->Layout, 0, 1, &set, 0, nullptr);
        if (!vbufs.empty())
            vkCmdBindVertexBuffers(cmd, 0, (uint32_t)vbufs.size(), vbufs.data(), voffs.data());

        VkViewport viewport{ (float)c.S.Viewport[0], (float)c.S.Viewport[1], (float)std::max(1, c.S.Viewport[2]), (float)std::max(1, c.S.Viewport[3]), 0.0f, 1.0f };
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        VkRect2D scissor{ { 0, 0 }, c.RenderArea };
        if (c.S.ScissorTest)
        {
            const int x0 = std::clamp(c.S.Scissor[0], 0, (int)c.RenderArea.width);
            const int y0 = std::clamp(c.S.Scissor[1], 0, (int)c.RenderArea.height);
            const int x1 = std::clamp(c.S.Scissor[0] + c.S.Scissor[2], 0, (int)c.RenderArea.width);
            const int y1 = std::clamp(c.S.Scissor[1] + c.S.Scissor[3], 0, (int)c.RenderArea.height);
            scissor = { { x0, y0 }, { (uint32_t)std::max(0, x1 - x0), (uint32_t)std::max(0, y1 - y0) } };
            if (scissor.extent.width == 0 || scissor.extent.height == 0)
                return;
        }
        vkCmdSetScissor(cmd, 0, 1, &scissor);
        const auto& limits = core.Properties().limits;
        vkCmdSetLineWidth(cmd, core.Features().wideLines ? std::clamp(c.S.LineWidth, limits.lineWidthRange[0], limits.lineWidthRange[1]) : 1.0f);
        if (dynamicBias)
            VK::g_Fn.CmdSetDepthBiasEnable(cmd, c.S.PolygonOffsetFill ? VK_TRUE : VK_FALSE);
        vkCmdSetDepthBias(cmd, c.S.PolygonOffsetUnits, 0.0f, c.S.PolygonOffsetFactor);
        VkCullModeFlags cull = VK_CULL_MODE_NONE;
        if (c.S.CullFace)
            cull = c.S.CullMode == GL_FRONT ? VK_CULL_MODE_FRONT_BIT : c.S.CullMode == GL_FRONT_AND_BACK ? VK_CULL_MODE_FRONT_AND_BACK : VK_CULL_MODE_BACK_BIT;
        vkCmdSetCullMode(cmd, cull);
        // GL ve Vulkan alan formüllerinin işareti farklı: sarım ters çevrilir (bkz. VkGL.h)
        vkCmdSetFrontFace(cmd, c.S.FrontFace == GL_CCW ? VK_FRONT_FACE_CLOCKWISE : VK_FRONT_FACE_COUNTER_CLOCKWISE);
        const bool hasDepth = c.RenderingDepthFormat != VK_FORMAT_UNDEFINED;
        vkCmdSetDepthTestEnable(cmd, (c.S.DepthTest && hasDepth) ? VK_TRUE : VK_FALSE);
        vkCmdSetDepthWriteEnable(cmd, (c.S.DepthTest && hasDepth && c.S.DepthMask) ? VK_TRUE : VK_FALSE);
        vkCmdSetDepthCompareOp(cmd, ToCompare(c.S.DepthFunc));
        const bool hasStencil = c.RenderingStencilFormat != VK_FORMAT_UNDEFINED;
        vkCmdSetStencilTestEnable(cmd, (c.S.StencilTest && hasStencil) ? VK_TRUE : VK_FALSE);
        vkCmdSetStencilOp(cmd, VK_STENCIL_FACE_FRONT_AND_BACK, ToStencilOp(c.S.StencilFail), ToStencilOp(c.S.StencilZPass), ToStencilOp(c.S.StencilZFail),
                          ToCompare(c.S.StencilFunc));
        vkCmdSetStencilCompareMask(cmd, VK_STENCIL_FACE_FRONT_AND_BACK, c.S.StencilValueMask);
        vkCmdSetStencilWriteMask(cmd, VK_STENCIL_FACE_FRONT_AND_BACK, c.S.StencilWriteMask);
        vkCmdSetStencilReference(cmd, VK_STENCIL_FACE_FRONT_AND_BACK, (uint32_t)c.S.StencilRef);

        if (indexed)
        {
            Buffer* eb = GetBuffer(vao->ElementBuffer);
            if (!eb)
                return;
            VkBuffer ib;
            VkDeviceSize ioff;
            if (indexType == GL_UNSIGNED_BYTE)
            {
                // 8 bit indeks Vulkan çekirdeğinde yok: 16 bit'e genişlet
                const size_t start = (size_t)(uintptr_t)indices;
                if (start + (size_t)count > eb->Shadow.size())
                    return;
                VK::RingAlloc a = core.AllocRing((VkDeviceSize)count * 2, 4);
                uint16_t* dst = reinterpret_cast<uint16_t*>(a.Ptr);
                for (GLsizei i = 0; i < count; ++i)
                    dst[i] = eb->Shadow[start + (size_t)i];
                vkCmdBindIndexBuffer(cmd, a.Buffer, a.Offset, VK_INDEX_TYPE_UINT16);
            }
            else
            {
                BufferRef(*eb, ib, ioff);
                const VkIndexType it = indexType == GL_UNSIGNED_SHORT ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32;
                vkCmdBindIndexBuffer(cmd, ib, ioff + (VkDeviceSize)(uintptr_t)indices, it);
            }
            vkCmdDrawIndexed(cmd, (uint32_t)count, (uint32_t)instances, 0, baseVertex, 0);
        }
        else
        {
            vkCmdDraw(cmd, (uint32_t)count, (uint32_t)instances, (uint32_t)first, 0);
        }
    }

    VertexArray* CurrentVAO()
    {
        auto& c = Ctx();
        if (c.S.VAO)
            if (auto it = c.VertexArrays.find(c.S.VAO); it != c.VertexArrays.end())
                return it->second.get();
        return &c.DefaultVAO;
    }

    void FlushAndWaitGL()
    {
        auto& c = Ctx();
        EndRendering();
        Core::Get().FlushAndWait();
        c.BoundPipelineKey = 0;
        c.LastSet = VK_NULL_HANDLE;
    }

    void GenerateMipmaps(Texture& t)
    {
        auto& core = Core::Get();
        if (!t.Image || t.Samples > 1)
            return;
        uint32_t full = 1;
        for (uint32_t m = std::max(t.Width, t.Height); m > 1; m >>= 1)
            ++full;
        const uint32_t wanted = std::min<uint32_t>(full, (uint32_t)std::max(1, t.MaxLevel + 1));
        if (!t.Immutable && t.Levels < wanted)
            GrowLevels(t, wanted);
        if (t.Levels < 2)
            return;
        VkFormatProperties fp;
        vkGetPhysicalDeviceFormatProperties(core.Physical(), t.Format, &fp);
        if (!(fp.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT) || !(fp.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT))
        {
            CQ_CORE_WARN("[VkGL] Format {0} does not support blits; mipmaps not generated", (int)t.Format);
            return;
        }
        const bool depth = (t.Aspect & VK_IMAGE_ASPECT_DEPTH_BIT) != 0;
        const bool linear = !t.Integer && !depth && (fp.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT);
        VkCommandBuffer cmd = TransferCmd();
        for (uint32_t l = 1; l < t.Levels; ++l)
        {
            VkImageBlit b{};
            b.srcSubresource = { t.Aspect, l - 1, 0, t.Layers };
            b.srcOffsets[1] = { (int32_t)std::max(1u, t.Width >> (l - 1)), (int32_t)std::max(1u, t.Height >> (l - 1)), 1 };
            b.dstSubresource = { t.Aspect, l, 0, t.Layers };
            b.dstOffsets[1] = { (int32_t)std::max(1u, t.Width >> l), (int32_t)std::max(1u, t.Height >> l), 1 };
            vkCmdBlitImage(cmd, t.Image, VK_IMAGE_LAYOUT_GENERAL, t.Image, VK_IMAGE_LAYOUT_GENERAL, 1, &b, linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST);
            core.FullBarrier(cmd);
        }
        t.DefinedLevels = t.Levels >= 32 ? ~0u : ((1u << t.Levels) - 1);
    }

    void BlitFramebuffer(GLint sx0, GLint sy0, GLint sx1, GLint sy1, GLint dx0, GLint dy0, GLint dx1, GLint dy1, GLbitfield mask, GLenum filter)
    {
        auto& c = Ctx();
        auto& core = Core::Get();
        Framebuffer* rf = GetFramebuffer(c.S.ReadFBO);
        Framebuffer* df = GetFramebuffer(c.S.DrawFBO);
        if (!rf || !df)
            return;
        VkCommandBuffer cmd = TransferCmd();

        auto blitOne = [&](Texture* src, int sLevel, int sLayer, Texture* dst, int dLevel, int dLayer, VkImageAspectFlags aspect, bool linear) {
            if (!src || !dst || !src->Image || !dst->Image || src == dst)
                return;
            const int sw = (int)std::max(1u, src->Width >> sLevel), sh = (int)std::max(1u, src->Height >> sLevel);
            const int dw = (int)std::max(1u, dst->Width >> dLevel), dh = (int)std::max(1u, dst->Height >> dLevel);
            auto clampX = [](int v, int m) { return std::clamp(v, 0, m); };
            const int csx0 = clampX(sx0, sw), csx1 = clampX(sx1, sw), csy0 = clampX(sy0, sh), csy1 = clampX(sy1, sh);
            const int cdx0 = clampX(dx0, dw), cdx1 = clampX(dx1, dw), cdy0 = clampX(dy0, dh), cdy1 = clampX(dy1, dh);
            if (csx0 == csx1 || csy0 == csy1 || cdx0 == cdx1 || cdy0 == cdy1)
                return;
            const bool sameSize = (csx1 - csx0) == (cdx1 - cdx0) && (csy1 - csy0) == (cdy1 - cdy0) && csx1 > csx0 && csy1 > csy0 && cdx1 > cdx0 && cdy1 > cdy0;
            const bool depth = (aspect & VK_IMAGE_ASPECT_DEPTH_BIT) != 0;
            if (src->Samples > 1 && dst->Samples == 1)
            {
                if (!sameSize || (depth && src->Format != dst->Format))
                {
                    CQ_CORE_WARN("[VkGL] Unsupported multisample blit (scaled resolve or format change)");
                    return;
                }
                if (depth)
                {
                    // Derinlik/şablon çözümlemesi yalnızca render geçişinde: boş bir geçiş, örnek 0 çözümlemesiyle
                    VkRenderingAttachmentInfo ds{ VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO };
                    ds.imageView = GetAttachmentView(*src, sLevel, sLayer);
                    ds.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
                    ds.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
                    ds.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
                    ds.resolveMode = VK_RESOLVE_MODE_SAMPLE_ZERO_BIT;
                    ds.resolveImageView = GetAttachmentView(*dst, dLevel, dLayer);
                    ds.resolveImageLayout = VK_IMAGE_LAYOUT_GENERAL;
                    VkRenderingInfo ri{ VK_STRUCTURE_TYPE_RENDERING_INFO };
                    ri.renderArea = { { std::min(csx0, csx1), std::min(csy0, csy1) }, { (uint32_t)std::abs(csx1 - csx0), (uint32_t)std::abs(csy1 - csy0) } };
                    ri.layerCount = 1;
                    const bool stencil = HasStencil(src->Format);
                    ri.pDepthAttachment = &ds;
                    ri.pStencilAttachment = stencil ? &ds : nullptr;
                    vkCmdBeginRendering(cmd, &ri);
                    vkCmdEndRendering(cmd);
                    return;
                }
                VkImageResolve r{};
                r.srcSubresource = { aspect, (uint32_t)sLevel, (uint32_t)sLayer, 1 };
                r.srcOffset = { csx0, csy0, 0 };
                r.dstSubresource = { aspect, (uint32_t)dLevel, (uint32_t)dLayer, 1 };
                r.dstOffset = { cdx0, cdy0, 0 };
                r.extent = { (uint32_t)(csx1 - csx0), (uint32_t)(csy1 - csy0), 1 };
                vkCmdResolveImage(cmd, src->Image, VK_IMAGE_LAYOUT_GENERAL, dst->Image, VK_IMAGE_LAYOUT_GENERAL, 1, &r);
                return;
            }
            if (sameSize && src->Format == dst->Format && src->Samples == dst->Samples)
            {
                VkImageCopy r{};
                r.srcSubresource = { aspect, (uint32_t)sLevel, (uint32_t)sLayer, 1 };
                r.srcOffset = { csx0, csy0, 0 };
                r.dstSubresource = { aspect, (uint32_t)dLevel, (uint32_t)dLayer, 1 };
                r.dstOffset = { cdx0, cdy0, 0 };
                r.extent = { (uint32_t)(csx1 - csx0), (uint32_t)(csy1 - csy0), 1 };
                vkCmdCopyImage(cmd, src->Image, VK_IMAGE_LAYOUT_GENERAL, dst->Image, VK_IMAGE_LAYOUT_GENERAL, 1, &r);
                return;
            }
            if (src->Samples > 1 || dst->Samples > 1 || (depth && src->Format != dst->Format) || src->Integer != dst->Integer)
            {
                CQ_CORE_WARN("[VkGL] Unsupported framebuffer blit (formats {0} -> {1})", (int)src->Format, (int)dst->Format);
                return;
            }
            VkImageBlit b{};
            b.srcSubresource = { aspect, (uint32_t)sLevel, (uint32_t)sLayer, 1 };
            b.srcOffsets[0] = { csx0, csy0, 0 };
            b.srcOffsets[1] = { csx1, csy1, 1 };
            b.dstSubresource = { aspect, (uint32_t)dLevel, (uint32_t)dLayer, 1 };
            b.dstOffsets[0] = { cdx0, cdy0, 0 };
            b.dstOffsets[1] = { cdx1, cdy1, 1 };
            vkCmdBlitImage(cmd, src->Image, VK_IMAGE_LAYOUT_GENERAL, dst->Image, VK_IMAGE_LAYOUT_GENERAL, 1, &b,
                           (linear && !depth && !src->Integer) ? VK_FILTER_LINEAR : VK_FILTER_NEAREST);
        };

        if (mask & GL_COLOR_BUFFER_BIT)
        {
            Texture* src = nullptr;
            int sLevel = 0, sLayer = 0;
            if (c.S.ReadFBO == 0)
                src = &c.BackColor;
            else if (rf->ReadBuffer >= GL_COLOR_ATTACHMENT0 && rf->ReadBuffer < GL_COLOR_ATTACHMENT0 + 8)
            {
                const Attachment& a = rf->Color[rf->ReadBuffer - GL_COLOR_ATTACHMENT0];
                src = AttachmentTexture(a);
                sLevel = a.Level;
                sLayer = a.Layer;
            }
            if (c.S.DrawFBO == 0)
                blitOne(src, sLevel, sLayer, &c.BackColor, 0, 0, VK_IMAGE_ASPECT_COLOR_BIT, filter == GL_LINEAR);
            else
                for (int i = 0; i < df->NumDrawBuffers && i < 8; ++i)
                {
                    const GLenum b = df->DrawBuffers[i];
                    if (b < GL_COLOR_ATTACHMENT0 || b >= GL_COLOR_ATTACHMENT0 + 8)
                        continue;
                    const Attachment& a = df->Color[b - GL_COLOR_ATTACHMENT0];
                    blitOne(src, sLevel, sLayer, AttachmentTexture(a), a.Level, a.Layer, VK_IMAGE_ASPECT_COLOR_BIT, filter == GL_LINEAR);
                }
        }
        if (mask & (GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT))
        {
            Texture* src = c.S.ReadFBO == 0 ? &c.BackDepth : AttachmentTexture(rf->Depth.Valid() ? rf->Depth : rf->Stencil);
            Texture* dst = c.S.DrawFBO == 0 ? &c.BackDepth : AttachmentTexture(df->Depth.Valid() ? df->Depth : df->Stencil);
            if (src && dst)
            {
                VkImageAspectFlags aspect = 0;
                if ((mask & GL_DEPTH_BUFFER_BIT) && (src->Aspect & VK_IMAGE_ASPECT_DEPTH_BIT) && (dst->Aspect & VK_IMAGE_ASPECT_DEPTH_BIT))
                    aspect |= VK_IMAGE_ASPECT_DEPTH_BIT;
                if ((mask & GL_STENCIL_BUFFER_BIT) && (src->Aspect & VK_IMAGE_ASPECT_STENCIL_BIT) && (dst->Aspect & VK_IMAGE_ASPECT_STENCIL_BIT))
                    aspect |= VK_IMAGE_ASPECT_STENCIL_BIT;
                // Birleşik derinlik-şablon görüntüsünde kopya iki düzlemi birlikte ister
                if (aspect && src->Format == dst->Format && (src->Aspect & VK_IMAGE_ASPECT_STENCIL_BIT))
                    aspect = src->Aspect;
                if (aspect)
                    blitOne(src, 0, 0, dst, 0, 0, aspect, false);
            }
        }
        core.FullBarrier(cmd);
    }

    void BeginFrameResources()
    {
        auto& c = Ctx();
        auto& core = Core::Get();
        const uint32_t w = core.FramebufferWidth(), h = core.FramebufferHeight();
        if (c.BackColor.Width != w || c.BackColor.Height != h || !c.BackColor.Image)
        {
            EndRendering();
            AllocateTexture(c.BackColor, GL_TEXTURE_2D, GL_RGBA8, w, h, 1, 1);
            c.BackColor.DefinedLevels = 1;
            AllocateTexture(c.BackDepth, GL_TEXTURE_2D, GL_DEPTH24_STENCIL8, w, h, 1, 1);
            c.BackDepth.DefinedLevels = 1;
            c.LastSet = VK_NULL_HANDLE;
        }
    }

    void SwapBuffers()
    {
        auto& c = Ctx();
        auto& core = Core::Get();
        core.BeginFrameIfNeeded();
        EndRendering();
        core.Present(c.BackColor.Image, c.BackColor.Width, c.BackColor.Height);
        c.LastSet = VK_NULL_HANDLE;
        c.BoundPipelineKey = 0;
        BeginFrameResources();
    }
}

#endif
