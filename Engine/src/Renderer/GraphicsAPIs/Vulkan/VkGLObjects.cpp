#ifdef CQ_VULKAN_ENABLED

#include "VkGL.h"
#include "Core/Logging/Log.h"

#include <glm/gtc/packing.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Conqueror::VkGL
{
    using VK::Core;

    Context& Ctx()
    {
        static Context c;
        return c;
    }

    Texture* GetTexture(uint32_t name)
    {
        auto& c = Ctx();
        auto it = c.Textures.find(name);
        return it == c.Textures.end() ? nullptr : it->second.get();
    }

    Texture* GetRenderbuffer(uint32_t name)
    {
        auto& c = Ctx();
        auto it = c.Renderbuffers.find(name);
        return it == c.Renderbuffers.end() ? nullptr : it->second.get();
    }

    Buffer* GetBuffer(uint32_t name)
    {
        auto& c = Ctx();
        auto it = c.Buffers.find(name);
        return it == c.Buffers.end() ? nullptr : it->second.get();
    }

    Framebuffer* GetFramebuffer(uint32_t name)
    {
        auto& c = Ctx();
        if (name == 0)
            return &c.DefaultFB;
        auto it = c.Framebuffers.find(name);
        return it == c.Framebuffers.end() ? nullptr : it->second.get();
    }

    Program* GetProgram(uint32_t name)
    {
        auto& c = Ctx();
        auto it = c.Programs.find(name);
        return it == c.Programs.end() ? nullptr : it->second.get();
    }

    Texture* AttachmentTexture(const Attachment& a)
    {
        auto& c = Ctx();
        // Varsayılan çerçeve arabelleğinin iç ekleri (ad 0xFFFFFFF0..)
        if (a.Texture == 0xFFFFFFF0u) return &c.BackColor;
        if (a.Texture == 0xFFFFFFF1u) return &c.BackDepth;
        if (a.Texture) return GetTexture(a.Texture);
        if (a.Renderbuffer) return GetRenderbuffer(a.Renderbuffer);
        return nullptr;
    }

    // ── Biçimler ──
    VkFormat ToVkFormat(GLenum f, VkImageAspectFlags& aspect, bool& integer)
    {
        aspect = VK_IMAGE_ASPECT_COLOR_BIT;
        integer = false;
        switch (f)
        {
            case GL_RGBA: case GL_RGBA8: case GL_RGB: case GL_RGB8: case GL_BGRA: return VK_FORMAT_R8G8B8A8_UNORM;
            case GL_SRGB8_ALPHA8: case GL_SRGB_ALPHA: case GL_SRGB8: case GL_SRGB: return VK_FORMAT_R8G8B8A8_SRGB;
            case GL_RED: case GL_R8: return VK_FORMAT_R8_UNORM;
            case GL_RG: case GL_RG8: return VK_FORMAT_R8G8_UNORM;
            case GL_RGBA16F: case GL_RGB16F: return VK_FORMAT_R16G16B16A16_SFLOAT;
            case GL_RGBA32F: case GL_RGB32F: return VK_FORMAT_R32G32B32A32_SFLOAT;
            case GL_R16F: return VK_FORMAT_R16_SFLOAT;
            case GL_RG16F: return VK_FORMAT_R16G16_SFLOAT;
            case GL_R32F: return VK_FORMAT_R32_SFLOAT;
            case GL_RG32F: return VK_FORMAT_R32G32_SFLOAT;
            case GL_R11F_G11F_B10F: return VK_FORMAT_B10G11R11_UFLOAT_PACK32;
            case GL_RGBA16: case GL_RGB16: return VK_FORMAT_R16G16B16A16_UNORM;
            case GL_R16: return VK_FORMAT_R16_UNORM;
            case GL_RG16: return VK_FORMAT_R16G16_UNORM;
            case GL_R32I: integer = true; return VK_FORMAT_R32_SINT;
            case GL_R32UI: integer = true; return VK_FORMAT_R32_UINT;
            case GL_RG32I: integer = true; return VK_FORMAT_R32G32_SINT;
            case GL_RGBA32I: integer = true; return VK_FORMAT_R32G32B32A32_SINT;
            case GL_R8I: integer = true; return VK_FORMAT_R8_SINT;
            case GL_R8UI: integer = true; return VK_FORMAT_R8_UINT;
            case GL_DEPTH_COMPONENT16: aspect = VK_IMAGE_ASPECT_DEPTH_BIT; return VK_FORMAT_D16_UNORM;
            case GL_DEPTH_COMPONENT: case GL_DEPTH_COMPONENT24: case GL_DEPTH_COMPONENT32: case GL_DEPTH_COMPONENT32F:
                aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
                return VK_FORMAT_D32_SFLOAT;
            case GL_DEPTH_STENCIL: case GL_DEPTH24_STENCIL8:
                aspect = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
                return Core::Get().DepthStencilFormat();
            case GL_DEPTH32F_STENCIL8:
                aspect = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
                return VK_FORMAT_D32_SFLOAT_S8_UINT;
            default:
                CQ_CORE_WARN("[VkGL] Unsupported internal format 0x{0:x}, using RGBA8", (unsigned)f);
                return VK_FORMAT_R8G8B8A8_UNORM;
        }
    }

    namespace
    {
        // Hedef biçimin piksel düzeni
        enum class Comp : uint8_t { UNorm8, SNorm8, UNorm16, Half, Float, SInt32, UInt32, SInt8, UInt8, B10G11R11, D32F, D16, D24S8, D32S8 };
        struct VkLayout
        {
            int Channels = 4;
            Comp Type = Comp::UNorm8;
            int Bytes = 4;          // texel başına
        };

        VkLayout LayoutOf(VkFormat f)
        {
            switch (f)
            {
                case VK_FORMAT_R8G8B8A8_UNORM: case VK_FORMAT_R8G8B8A8_SRGB: case VK_FORMAT_B8G8R8A8_UNORM: return { 4, Comp::UNorm8, 4 };
                case VK_FORMAT_R8_UNORM: return { 1, Comp::UNorm8, 1 };
                case VK_FORMAT_R8G8_UNORM: return { 2, Comp::UNorm8, 2 };
                case VK_FORMAT_R16G16B16A16_SFLOAT: return { 4, Comp::Half, 8 };
                case VK_FORMAT_R32G32B32A32_SFLOAT: return { 4, Comp::Float, 16 };
                case VK_FORMAT_R16_SFLOAT: return { 1, Comp::Half, 2 };
                case VK_FORMAT_R16G16_SFLOAT: return { 2, Comp::Half, 4 };
                case VK_FORMAT_R32_SFLOAT: return { 1, Comp::Float, 4 };
                case VK_FORMAT_R32G32_SFLOAT: return { 2, Comp::Float, 8 };
                case VK_FORMAT_B10G11R11_UFLOAT_PACK32: return { 3, Comp::B10G11R11, 4 };
                case VK_FORMAT_R16G16B16A16_UNORM: return { 4, Comp::UNorm16, 8 };
                case VK_FORMAT_R16_UNORM: return { 1, Comp::UNorm16, 2 };
                case VK_FORMAT_R16G16_UNORM: return { 2, Comp::UNorm16, 4 };
                case VK_FORMAT_R32_SINT: return { 1, Comp::SInt32, 4 };
                case VK_FORMAT_R32_UINT: return { 1, Comp::UInt32, 4 };
                case VK_FORMAT_R32G32_SINT: return { 2, Comp::SInt32, 8 };
                case VK_FORMAT_R32G32B32A32_SINT: return { 4, Comp::SInt32, 16 };
                case VK_FORMAT_R8_SINT: return { 1, Comp::SInt8, 1 };
                case VK_FORMAT_R8_UINT: return { 1, Comp::UInt8, 1 };
                case VK_FORMAT_D32_SFLOAT: return { 1, Comp::D32F, 4 };
                case VK_FORMAT_D16_UNORM: return { 1, Comp::D16, 2 };
                case VK_FORMAT_D24_UNORM_S8_UINT: return { 1, Comp::D24S8, 4 };
                case VK_FORMAT_D32_SFLOAT_S8_UINT: return { 1, Comp::D32S8, 4 };
                default: return { 4, Comp::UNorm8, 4 };
            }
        }

        int GLChannels(GLenum format)
        {
            switch (format)
            {
                case GL_RED: case GL_RED_INTEGER: case GL_DEPTH_COMPONENT: case GL_ALPHA: return 1;
                case GL_RG: case GL_RG_INTEGER: case GL_DEPTH_STENCIL: return 2;
                case GL_RGB: case GL_BGR: case GL_RGB_INTEGER: return 3;
                default: return 4;
            }
        }

        int GLTypeSize(GLenum type)
        {
            switch (type)
            {
                case GL_UNSIGNED_BYTE: case GL_BYTE: return 1;
                case GL_UNSIGNED_SHORT: case GL_SHORT: case GL_HALF_FLOAT: return 2;
                default: return 4;
            }
        }

        // GL kaynak bileşeni -> float (normalize türler 0..1)
        double ReadGL(const uint8_t* p, GLenum type)
        {
            switch (type)
            {
                case GL_UNSIGNED_BYTE: return *p / 255.0;
                case GL_BYTE: return std::max(-1.0, (int8_t)*p / 127.0);
                case GL_UNSIGNED_SHORT: { uint16_t v; std::memcpy(&v, p, 2); return v / 65535.0; }
                case GL_SHORT: { int16_t v; std::memcpy(&v, p, 2); return std::max(-1.0, v / 32767.0); }
                case GL_HALF_FLOAT: { uint16_t v; std::memcpy(&v, p, 2); return glm::unpackHalf1x16(v); }
                case GL_INT: { int32_t v; std::memcpy(&v, p, 4); return (double)v; }
                case GL_UNSIGNED_INT: { uint32_t v; std::memcpy(&v, p, 4); return (double)v; }
                default: { float v; std::memcpy(&v, p, 4); return v; }
            }
        }

        void WriteGL(uint8_t* p, GLenum type, double v)
        {
            switch (type)
            {
                case GL_UNSIGNED_BYTE: *p = (uint8_t)std::clamp(std::lround(v * 255.0), 0L, 255L); break;
                case GL_BYTE: *p = (uint8_t)(int8_t)std::clamp(std::lround(v * 127.0), -127L, 127L); break;
                case GL_UNSIGNED_SHORT: { uint16_t x = (uint16_t)std::clamp(std::lround(v * 65535.0), 0L, 65535L); std::memcpy(p, &x, 2); break; }
                case GL_HALF_FLOAT: { uint16_t x = glm::packHalf1x16((float)v); std::memcpy(p, &x, 2); break; }
                case GL_INT: { int32_t x = (int32_t)std::llround(v); std::memcpy(p, &x, 4); break; }
                case GL_UNSIGNED_INT: { uint32_t x = (uint32_t)std::max(0LL, std::llround(v)); std::memcpy(p, &x, 4); break; }
                default: { float x = (float)v; std::memcpy(p, &x, 4); break; }
            }
        }

        // Vulkan texel'i dört bileşenli double'a
        void ReadVk(const uint8_t* p, const VkLayout& l, double out[4])
        {
            out[0] = out[1] = out[2] = 0.0;
            out[3] = 1.0;
            switch (l.Type)
            {
                case Comp::UNorm8: for (int i = 0; i < l.Channels; ++i) out[i] = p[i] / 255.0; break;
                case Comp::UNorm16: for (int i = 0; i < l.Channels; ++i) { uint16_t v; std::memcpy(&v, p + i * 2, 2); out[i] = v / 65535.0; } break;
                case Comp::Half: for (int i = 0; i < l.Channels; ++i) { uint16_t v; std::memcpy(&v, p + i * 2, 2); out[i] = glm::unpackHalf1x16(v); } break;
                case Comp::Float: case Comp::D32F: for (int i = 0; i < l.Channels; ++i) { float v; std::memcpy(&v, p + i * 4, 4); out[i] = v; } break;
                case Comp::SInt32: for (int i = 0; i < l.Channels; ++i) { int32_t v; std::memcpy(&v, p + i * 4, 4); out[i] = v; } break;
                case Comp::UInt32: for (int i = 0; i < l.Channels; ++i) { uint32_t v; std::memcpy(&v, p + i * 4, 4); out[i] = v; } break;
                case Comp::SInt8: out[0] = (int8_t)p[0]; break;
                case Comp::UInt8: out[0] = p[0]; break;
                case Comp::B10G11R11: { uint32_t v; std::memcpy(&v, p, 4); glm::vec3 c = glm::unpackF2x11_1x10(v); out[0] = c.r; out[1] = c.g; out[2] = c.b; break; }
                case Comp::D16: { uint16_t v; std::memcpy(&v, p, 2); out[0] = v / 65535.0; break; }
                case Comp::D24S8: { uint32_t v; std::memcpy(&v, p, 4); out[0] = (v & 0xFFFFFF) / 16777215.0; break; }
                default: break;
            }
        }

        void WriteVk(uint8_t* p, const VkLayout& l, const double in[4])
        {
            switch (l.Type)
            {
                case Comp::UNorm8: for (int i = 0; i < l.Channels; ++i) p[i] = (uint8_t)std::clamp(std::lround(in[i] * 255.0), 0L, 255L); break;
                case Comp::UNorm16: for (int i = 0; i < l.Channels; ++i) { uint16_t v = (uint16_t)std::clamp(std::lround(in[i] * 65535.0), 0L, 65535L); std::memcpy(p + i * 2, &v, 2); } break;
                case Comp::Half: for (int i = 0; i < l.Channels; ++i) { uint16_t v = glm::packHalf1x16((float)in[i]); std::memcpy(p + i * 2, &v, 2); } break;
                case Comp::Float: case Comp::D32F: for (int i = 0; i < l.Channels; ++i) { float v = (float)in[i]; std::memcpy(p + i * 4, &v, 4); } break;
                case Comp::SInt32: for (int i = 0; i < l.Channels; ++i) { int32_t v = (int32_t)std::llround(in[i]); std::memcpy(p + i * 4, &v, 4); } break;
                case Comp::UInt32: for (int i = 0; i < l.Channels; ++i) { uint32_t v = (uint32_t)std::max(0LL, std::llround(in[i])); std::memcpy(p + i * 4, &v, 4); } break;
                case Comp::SInt8: p[0] = (uint8_t)(int8_t)std::llround(in[0]); break;
                case Comp::UInt8: p[0] = (uint8_t)std::llround(in[0]); break;
                case Comp::B10G11R11: { uint32_t v = glm::packF2x11_1x10(glm::vec3((float)in[0], (float)in[1], (float)in[2])); std::memcpy(p, &v, 4); break; }
                case Comp::D16: { uint16_t v = (uint16_t)std::clamp(std::lround(in[0] * 65535.0), 0L, 65535L); std::memcpy(p, &v, 2); break; }
                default: break;
            }
        }

        bool FormatSupports(VkFormat f, VkFormatFeatureFlags feat)
        {
            VkFormatProperties fp;
            vkGetPhysicalDeviceFormatProperties(Core::Get().Physical(), f, &fp);
            return (fp.optimalTilingFeatures & feat) == feat;
        }

        uint32_t MaxLevels(uint32_t w, uint32_t h)
        {
            uint32_t levels = 1;
            uint32_t m = std::max(w, h);
            while (m > 1) { m >>= 1; ++levels; }
            return levels;
        }
    }

    void DestroyTextureImage(Texture& t)
    {
        auto& core = Core::Get();
        if (!core.IsInitialized())
            return;
        VkDevice dev = core.Device();
        std::vector<VkImageView> views;
        if (t.SampledView) views.push_back(t.SampledView);
        for (auto& [k, v] : t.AttachmentViews) views.push_back(v);
        VkImage image = t.Image;
        VkDeviceMemory mem = t.Memory;
        if (image || !views.empty())
        {
            core.DeferDelete([dev, views, image, mem]() {
                for (auto v : views) vkDestroyImageView(dev, v, nullptr);
                if (image) vkDestroyImage(dev, image, nullptr);
                if (mem) vkFreeMemory(dev, mem, nullptr);
            });
        }
        t.SampledView = VK_NULL_HANDLE;
        t.AttachmentViews.clear();
        t.Image = VK_NULL_HANDLE;
        t.Memory = VK_NULL_HANDLE;
        t.DefinedLevels = 0;
    }

    bool AllocateTexture(Texture& t, GLenum target, GLenum internalFormat, uint32_t w, uint32_t h, uint32_t levels, uint32_t samples)
    {
        auto& core = Core::Get();
        DestroyTextureImage(t);
        t.Target = target;
        t.InternalFormat = internalFormat;
        t.Format = ToVkFormat(internalFormat, t.Aspect, t.Integer);
        t.Width = std::max(1u, w);
        t.Height = std::max(1u, h);
        t.Levels = std::max(1u, std::min(levels, MaxLevels(t.Width, t.Height)));
        t.Layers = target == GL_TEXTURE_CUBE_MAP ? 6u : 1u;
        t.Samples = std::max(1u, samples);
        if (t.Samples > 1)
            t.Levels = 1;

        VkImageCreateInfo ii{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
        ii.imageType = VK_IMAGE_TYPE_2D;
        ii.format = t.Format;
        ii.extent = { t.Width, t.Height, 1 };
        ii.mipLevels = t.Levels;
        ii.arrayLayers = t.Layers;
        ii.samples = (VkSampleCountFlagBits)t.Samples;
        ii.tiling = VK_IMAGE_TILING_OPTIMAL;
        ii.flags = target == GL_TEXTURE_CUBE_MAP ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0;
        ii.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        const bool depth = (t.Aspect & VK_IMAGE_ASPECT_DEPTH_BIT) != 0;
        if (FormatSupports(t.Format, VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT))
            ii.usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
        if (depth && FormatSupports(t.Format, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT))
            ii.usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        if (!depth && FormatSupports(t.Format, VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT))
            ii.usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (!core.CreateImage(ii, t.Image, t.Memory))
        {
            CQ_CORE_ERROR("[VkGL] Cannot create {0}x{1} image (format {2})", t.Width, t.Height, (int)t.Format);
            return false;
        }
        VkCommandBuffer cmd = TransferCmd();
        core.TransitionToGeneral(cmd, t.Image, t.Aspect, t.Levels, t.Layers, VK_IMAGE_LAYOUT_UNDEFINED);
        // GL'de tanımsız; sıfırlamak tutarlı sonuç verir
        VkImageSubresourceRange range{ t.Aspect, 0, t.Levels, 0, t.Layers };
        if (depth)
        {
            VkClearDepthStencilValue v{ 1.0f, 0 };
            vkCmdClearDepthStencilImage(cmd, t.Image, VK_IMAGE_LAYOUT_GENERAL, &v, 1, &range);
        }
        else
        {
            VkClearColorValue v{};
            vkCmdClearColorImage(cmd, t.Image, VK_IMAGE_LAYOUT_GENERAL, &v, 1, &range);
        }
        core.FullBarrier(cmd);
        return true;
    }

    VkImageView GetSampledView(Texture& t)
    {
        if (!t.Image)
            return VK_NULL_HANDLE;
        // Örneklenebilir düzeyler: değişmez dokuda hepsi; değişkende 0'dan itibaren ardışık tanımlılar
        int levels = (int)t.Levels;
        if (!t.Immutable && t.DefinedLevels)
        {
            levels = 0;
            while (levels < (int)t.Levels && (t.DefinedLevels & (1u << levels)))
                ++levels;
            levels = std::max(levels, 1);
        }
        levels = std::clamp(std::min(levels, t.MaxLevel + 1), 1, (int)t.Levels);
        if (t.SampledView && t.SampledViewLevels == levels)
            return t.SampledView;
        auto& core = Core::Get();
        if (t.SampledView)
        {
            VkImageView old = t.SampledView;
            VkDevice dev = core.Device();
            core.DeferDelete([dev, old]() { vkDestroyImageView(dev, old, nullptr); });
        }
        VkImageViewCreateInfo vi{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        vi.image = t.Image;
        vi.viewType = t.Target == GL_TEXTURE_CUBE_MAP ? VK_IMAGE_VIEW_TYPE_CUBE : VK_IMAGE_VIEW_TYPE_2D;
        vi.format = t.Format;
        const bool depth = (t.Aspect & VK_IMAGE_ASPECT_DEPTH_BIT) != 0;
        if (depth)
            vi.components = { VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_ZERO, VK_COMPONENT_SWIZZLE_ZERO, VK_COMPONENT_SWIZZLE_ONE };
        vi.subresourceRange = { depth ? (VkImageAspectFlags)VK_IMAGE_ASPECT_DEPTH_BIT : t.Aspect, 0, (uint32_t)levels, 0, t.Layers };
        vkCreateImageView(core.Device(), &vi, nullptr, &t.SampledView);
        t.SampledViewLevels = levels;
        return t.SampledView;
    }

    VkImageView GetAttachmentView(Texture& t, int level, int layer)
    {
        if (!t.Image)
            return VK_NULL_HANDLE;
        const uint32_t key = (uint32_t)level | ((uint32_t)layer << 8);
        auto it = t.AttachmentViews.find(key);
        if (it != t.AttachmentViews.end())
            return it->second;
        VkImageViewCreateInfo vi{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        vi.image = t.Image;
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = t.Format;
        vi.subresourceRange = { t.Aspect, (uint32_t)level, 1, (uint32_t)layer, 1 };
        VkImageView view = VK_NULL_HANDLE;
        vkCreateImageView(Core::Get().Device(), &vi, nullptr, &view);
        t.AttachmentViews[key] = view;
        return view;
    }

    VkSampler GetSampler(const Texture& p, bool integer)
    {
        auto& c = Ctx();
        auto& core = Core::Get();
        auto filterOf = [](GLenum f) { return (f == GL_NEAREST || f == GL_NEAREST_MIPMAP_NEAREST || f == GL_NEAREST_MIPMAP_LINEAR) ? VK_FILTER_NEAREST : VK_FILTER_LINEAR; };
        auto addressOf = [](GLenum w) {
            switch (w)
            {
                case GL_CLAMP_TO_EDGE: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
                case GL_MIRRORED_REPEAT: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
                case GL_CLAMP_TO_BORDER: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
                default: return VK_SAMPLER_ADDRESS_MODE_REPEAT;
            }
        };
        const bool mip = p.MinFilter != GL_NEAREST && p.MinFilter != GL_LINEAR;
        VkSamplerCreateInfo si{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };
        si.magFilter = integer ? VK_FILTER_NEAREST : filterOf(p.MagFilter);
        si.minFilter = integer ? VK_FILTER_NEAREST : filterOf(p.MinFilter);
        si.mipmapMode = (p.MinFilter == GL_NEAREST_MIPMAP_LINEAR || p.MinFilter == GL_LINEAR_MIPMAP_LINEAR) && !integer ? VK_SAMPLER_MIPMAP_MODE_LINEAR
                                                                                                                          : VK_SAMPLER_MIPMAP_MODE_NEAREST;
        si.addressModeU = addressOf(p.WrapS);
        si.addressModeV = addressOf(p.WrapT);
        si.addressModeW = addressOf(p.WrapR);
        si.mipLodBias = p.LodBias;
        const float maxAniso = core.Properties().limits.maxSamplerAnisotropy;
        si.anisotropyEnable = (!integer && p.MaxAnisotropy > 1.0f && core.Features().samplerAnisotropy) ? VK_TRUE : VK_FALSE;
        si.maxAnisotropy = std::clamp(p.MaxAnisotropy, 1.0f, maxAniso);
        si.compareEnable = p.CompareMode == GL_COMPARE_REF_TO_TEXTURE ? VK_TRUE : VK_FALSE;
        switch (p.CompareFunc)
        {
            case GL_NEVER: si.compareOp = VK_COMPARE_OP_NEVER; break;
            case GL_LESS: si.compareOp = VK_COMPARE_OP_LESS; break;
            case GL_EQUAL: si.compareOp = VK_COMPARE_OP_EQUAL; break;
            case GL_GREATER: si.compareOp = VK_COMPARE_OP_GREATER; break;
            case GL_NOTEQUAL: si.compareOp = VK_COMPARE_OP_NOT_EQUAL; break;
            case GL_GEQUAL: si.compareOp = VK_COMPARE_OP_GREATER_OR_EQUAL; break;
            case GL_ALWAYS: si.compareOp = VK_COMPARE_OP_ALWAYS; break;
            default: si.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL; break;
        }
        // Mip'siz küçültme: yalnızca taban düzey (GL davranışı)
        si.minLod = mip ? std::max(0.0f, p.MinLod) : 0.0f;
        si.maxLod = mip ? std::max(si.minLod, std::min(p.MaxLod, 1000.0f)) : 0.25f;
        const bool white = p.BorderColor[0] > 0.5f;
        const bool opaque = p.BorderColor[3] > 0.5f;
        si.borderColor = white ? VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE : opaque ? VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK : VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
        if (integer)
            si.borderColor = white ? VK_BORDER_COLOR_INT_OPAQUE_WHITE : opaque ? VK_BORDER_COLOR_INT_OPAQUE_BLACK : VK_BORDER_COLOR_INT_TRANSPARENT_BLACK;

        uint64_t key = 1469598103934665603ull;
        auto mix = [&](uint64_t v) { key = (key ^ v) * 1099511628211ull; };
        mix(si.magFilter); mix(si.minFilter); mix(si.mipmapMode); mix(si.addressModeU); mix(si.addressModeV); mix(si.addressModeW);
        mix(si.anisotropyEnable); mix((uint64_t)(si.maxAnisotropy * 16.0f)); mix(si.compareEnable); mix(si.compareOp);
        mix((uint64_t)(int64_t)(si.minLod * 64.0f)); mix((uint64_t)(int64_t)(si.maxLod * 64.0f)); mix((uint64_t)(int64_t)(si.mipLodBias * 64.0f)); mix(si.borderColor);
        auto it = c.SamplerCache.find(key);
        if (it != c.SamplerCache.end())
            return it->second;
        VkSampler sampler = VK_NULL_HANDLE;
        vkCreateSampler(core.Device(), &si, nullptr, &sampler);
        c.SamplerCache[key] = sampler;
        return sampler;
    }

    void UploadTexture(Texture& t, int level, int layer, int x, int y, int w, int h, GLenum format, GLenum type, const void* data)
    {
        if (!t.Image || !data || w <= 0 || h <= 0)
            return;
        auto& c = Ctx();
        auto& core = Core::Get();
        const VkLayout dst = LayoutOf(t.Format);
        const int srcChannels = GLChannels(format);
        const int compSize = GLTypeSize(type);
        const bool packed = type == GL_UNSIGNED_INT_8_8_8_8_REV || type == GL_UNSIGNED_INT_8_8_8_8;
        const int srcTexel = packed ? 4 : srcChannels * compSize;
        const int rowLength = c.S.UnpackRowLength > 0 ? c.S.UnpackRowLength : w;
        const int align = std::max(1, c.S.UnpackAlignment);
        const size_t srcPitch = ((size_t)rowLength * srcTexel + align - 1) / align * align;
        const size_t dstPitch = (size_t)w * dst.Bytes;
        VK::RingAlloc staging = core.AllocRing(dstPitch * h, 16);
        const uint8_t* src = static_cast<const uint8_t*>(data);
        const bool bgra = format == GL_BGRA || format == GL_BGR;

        // Hızlı yol: aynı düzen (RGBA8 <- RGBA/UNSIGNED_BYTE, float -> float, int -> int)
        const bool direct = !bgra && !packed && srcChannels == dst.Channels &&
                            ((dst.Type == Comp::UNorm8 && type == GL_UNSIGNED_BYTE) || (dst.Type == Comp::Float && type == GL_FLOAT) ||
                             (dst.Type == Comp::Half && type == GL_HALF_FLOAT) || (dst.Type == Comp::SInt32 && type == GL_INT) ||
                             (dst.Type == Comp::UInt32 && type == GL_UNSIGNED_INT) || (dst.Type == Comp::UNorm16 && type == GL_UNSIGNED_SHORT) ||
                             (dst.Type == Comp::D32F && type == GL_FLOAT));
        for (int row = 0; row < h; ++row)
        {
            const uint8_t* s = src + srcPitch * row;
            uint8_t* d = staging.Ptr + dstPitch * row;
            if (direct)
            {
                std::memcpy(d, s, dstPitch);
                continue;
            }
            for (int col = 0; col < w; ++col)
            {
                double v[4] = { 0.0, 0.0, 0.0, 1.0 };
                const uint8_t* sp = s + (size_t)col * srcTexel;
                if (packed)
                {
                    for (int i = 0; i < 4; ++i)
                        v[i] = sp[i] / 255.0;
                }
                else
                {
                    for (int i = 0; i < srcChannels && i < 4; ++i)
                        v[i] = ReadGL(sp + i * compSize, type);
                    if (format == GL_ALPHA)
                    {
                        v[3] = v[0];
                        v[0] = v[1] = v[2] = 0.0;
                    }
                }
                if (bgra)
                    std::swap(v[0], v[2]);
                WriteVk(d + (size_t)col * dst.Bytes, dst, v);
            }
        }

        VkCommandBuffer cmd = TransferCmd();
        VkBufferImageCopy region{};
        region.bufferOffset = staging.Offset;
        region.imageSubresource = { (t.Aspect & VK_IMAGE_ASPECT_DEPTH_BIT) ? (VkImageAspectFlags)VK_IMAGE_ASPECT_DEPTH_BIT : t.Aspect, (uint32_t)level, (uint32_t)layer, 1 };
        region.imageOffset = { x, y, 0 };
        region.imageExtent = { (uint32_t)w, (uint32_t)h, 1 };
        vkCmdCopyBufferToImage(cmd, staging.Buffer, t.Image, VK_IMAGE_LAYOUT_GENERAL, 1, &region);
        core.FullBarrier(cmd);
    }

    void ReadImage(Texture& t, int level, int layer, int x, int y, int w, int h, GLenum format, GLenum type, void* out, int packAlignment)
    {
        if (!t.Image || !out || w <= 0 || h <= 0)
            return;
        auto& core = Core::Get();
        const VkLayout srcL = LayoutOf(t.Format);
        // Derinlik-şablon biçimlerinde yalnızca derinlik düzlemi kopyalanır
        VkLayout copyL = srcL;
        VkImageAspectFlags aspect = t.Aspect;
        if (t.Aspect == (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT))
        {
            aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
            copyL = t.Format == VK_FORMAT_D24_UNORM_S8_UINT ? VkLayout{ 1, Comp::D24S8, 4 } : VkLayout{ 1, Comp::D32F, 4 };
        }
        const size_t pitch = (size_t)w * copyL.Bytes;
        VK::RingAlloc staging = core.AllocRing(pitch * h, 16);
        VkCommandBuffer cmd = TransferCmd();
        VkBufferImageCopy region{};
        region.bufferOffset = staging.Offset;
        region.imageSubresource = { aspect, (uint32_t)level, (uint32_t)layer, 1 };
        region.imageOffset = { x, y, 0 };
        region.imageExtent = { (uint32_t)w, (uint32_t)h, 1 };
        vkCmdCopyImageToBuffer(cmd, t.Image, VK_IMAGE_LAYOUT_GENERAL, staging.Buffer, 1, &region);
        core.FullBarrier(cmd);
        core.FlushAndWait();

        const int outChannels = GLChannels(format);
        const int compSize = GLTypeSize(type);
        const int outTexel = outChannels * compSize;
        const size_t outPitch = ((size_t)w * outTexel + std::max(1, packAlignment) - 1) / std::max(1, packAlignment) * std::max(1, packAlignment);
        uint8_t* dst = static_cast<uint8_t*>(out);
        const bool bgra = format == GL_BGRA;
        const bool direct = !bgra && outChannels == copyL.Channels &&
                            ((copyL.Type == Comp::UNorm8 && type == GL_UNSIGNED_BYTE) || (copyL.Type == Comp::Float && type == GL_FLOAT) ||
                             (copyL.Type == Comp::SInt32 && type == GL_INT) || (copyL.Type == Comp::D32F && type == GL_FLOAT));
        for (int row = 0; row < h; ++row)
        {
            const uint8_t* s = staging.Ptr + pitch * row;
            uint8_t* d = dst + outPitch * row;
            if (direct)
            {
                std::memcpy(d, s, pitch);
                continue;
            }
            for (int col = 0; col < w; ++col)
            {
                double v[4];
                ReadVk(s + (size_t)col * copyL.Bytes, copyL, v);
                if (bgra)
                    std::swap(v[0], v[2]);
                for (int i = 0; i < outChannels; ++i)
                    WriteGL(d + (size_t)col * outTexel + i * compSize, type, v[i]);
            }
        }
    }

    bool GrowLevels(Texture& t, uint32_t levels)
    {
        auto& core = Core::Get();
        levels = std::min(levels, MaxLevels(t.Width, t.Height));
        if (!t.Image || levels <= t.Levels || t.Samples > 1)
            return t.Image != VK_NULL_HANDLE;
        // Eski görüntü bu karenin komutları bitene dek yaşar (ertelenmiş silme): içerik ondan kopyalanır
        core.BeginFrameIfNeeded();
        const VkImage oldImage = t.Image;
        const uint32_t oldLevels = t.Levels;
        const uint32_t defined = t.DefinedLevels;
        if (!AllocateTexture(t, t.Target, t.InternalFormat, t.Width, t.Height, levels, 1))
            return false;
        std::vector<VkImageCopy> regions;
        for (uint32_t l = 0; l < oldLevels; ++l)
        {
            if (!(defined & (1u << l)))
                continue;
            VkImageCopy r{};
            r.srcSubresource = { t.Aspect, l, 0, t.Layers };
            r.dstSubresource = { t.Aspect, l, 0, t.Layers };
            r.extent = { std::max(1u, t.Width >> l), std::max(1u, t.Height >> l), 1 };
            regions.push_back(r);
        }
        if (!regions.empty())
        {
            VkCommandBuffer cmd = TransferCmd();
            vkCmdCopyImage(cmd, oldImage, VK_IMAGE_LAYOUT_GENERAL, t.Image, VK_IMAGE_LAYOUT_GENERAL, (uint32_t)regions.size(), regions.data());
            core.FullBarrier(cmd);
        }
        t.DefinedLevels = defined;
        return true;
    }

    void ClearTexture(Texture& t, int level, GLenum format, GLenum type, const void* data)
    {
        if (!t.Image || level < 0 || level >= (int)t.Levels)
            return;
        auto& core = Core::Get();
        VkCommandBuffer cmd = TransferCmd();
        VkImageSubresourceRange range{ t.Aspect, (uint32_t)level, 1, 0, t.Layers };
        const auto* bytes = static_cast<const uint8_t*>(data);
        if (t.Aspect & VK_IMAGE_ASPECT_DEPTH_BIT)
        {
            VkClearDepthStencilValue v{ 1.0f, 0 };
            if (bytes)
            {
                if (type == GL_UNSIGNED_INT_24_8)
                {
                    uint32_t packed;
                    std::memcpy(&packed, bytes, 4);
                    v.depth = (packed >> 8) / 16777215.0f;
                    v.stencil = packed & 0xFF;
                }
                else
                    v.depth = (float)ReadGL(bytes, type);
            }
            vkCmdClearDepthStencilImage(cmd, t.Image, VK_IMAGE_LAYOUT_GENERAL, &v, 1, &range);
        }
        else
        {
            VkClearColorValue v{};
            if (bytes)
            {
                const int n = GLChannels(format);
                const int cs = GLTypeSize(type);
                for (int i = 0; i < n && i < 4; ++i)
                {
                    const uint8_t* p = bytes + i * cs;
                    if (t.Integer)
                    {
                        int64_t iv = 0;
                        switch (type)
                        {
                            case GL_BYTE: iv = (int8_t)*p; break;
                            case GL_UNSIGNED_BYTE: iv = *p; break;
                            case GL_SHORT: { int16_t x; std::memcpy(&x, p, 2); iv = x; break; }
                            case GL_UNSIGNED_SHORT: { uint16_t x; std::memcpy(&x, p, 2); iv = x; break; }
                            case GL_UNSIGNED_INT: { uint32_t x; std::memcpy(&x, p, 4); iv = x; break; }
                            default: { int32_t x; std::memcpy(&x, p, 4); iv = x; break; }
                        }
                        v.int32[i] = (int32_t)iv;
                    }
                    else
                        v.float32[i] = (float)ReadGL(p, type);
                }
                if (!t.Integer && n < 4)
                    v.float32[3] = 1.0f;
            }
            vkCmdClearColorImage(cmd, t.Image, VK_IMAGE_LAYOUT_GENERAL, &v, 1, &range);
        }
        core.FullBarrier(cmd);
        t.DefinedLevels |= 1u << level;
    }

    void BufferRef(Buffer& b, VkBuffer& outBuffer, VkDeviceSize& outOffset)
    {
        auto& core = Core::Get();
        core.BeginFrameIfNeeded();
        const VkDeviceSize size = std::max<VkDeviceSize>(b.Shadow.size(), 16);
        // Hiç yazılmamış kuyruk kopyalanmaz (büyük toplu arabellekler kısmen dolu kullanılır)
        const size_t copyBytes = std::min(b.Shadow.size(), b.HighWater);
        const bool ringStale = b.CurrentIsRing && b.RingFrame != core.FrameNumber();
        if (b.Dirty || ringStale || !b.Current)
        {
            // Adanmış sürüm GPU'da kullanılmıyorsa yerinde güncelle; değilse bu karenin halkasına yeniden adlandır
            const bool dedicatedFree = b.Dedicated && b.DedicatedSize >= size && b.DedicatedLastUse <= core.CompletedSerial();
            if (!b.Dedicated || b.DedicatedSize < size)
            {
                if (b.Dedicated)
                {
                    VkDevice dev = core.Device();
                    VkBuffer old = b.Dedicated;
                    VkDeviceMemory mem = b.Memory;
                    core.DeferDelete([dev, old, mem]() { vkDestroyBuffer(dev, old, nullptr); vkFreeMemory(dev, mem, nullptr); });
                    b.Dedicated = VK_NULL_HANDLE;
                }
                void* mapped = nullptr;
                core.CreateBuffer(size,
                                  VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT |
                                      VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, b.Dedicated, b.Memory, &mapped);
                b.Mapped = (uint8_t*)mapped;
                b.DedicatedSize = size;
                b.DedicatedLastUse = 0;
                if (b.Mapped)
                {
                    std::memset(b.Mapped, 0, (size_t)size);
                    if (copyBytes)
                        std::memcpy(b.Mapped, b.Shadow.data(), copyBytes);
                }
                b.Current = b.Dedicated;
                b.CurrentOffset = 0;
                b.CurrentIsRing = false;
            }
            else if (dedicatedFree)
            {
                if (b.Mapped && copyBytes)
                    std::memcpy(b.Mapped, b.Shadow.data(), copyBytes);
                b.Current = b.Dedicated;
                b.CurrentOffset = 0;
                b.CurrentIsRing = false;
            }
            else
            {
                VK::RingAlloc a = core.AllocRing(size, 256);
                if (copyBytes)
                    std::memcpy(a.Ptr, b.Shadow.data(), copyBytes);
                b.Current = a.Buffer;
                b.CurrentOffset = a.Offset;
                b.CurrentIsRing = true;
                b.RingFrame = core.FrameNumber();
            }
            b.Dirty = false;
        }
        if (!b.CurrentIsRing)
            b.DedicatedLastUse = core.FrameSerial();
        outBuffer = b.Current;
        outOffset = b.CurrentOffset;
    }
}

#endif
