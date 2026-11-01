#if defined(CQ_D3D11_ENABLED) || defined(CQ_D3D12_ENABLED)

#include "DxShared.h"
#include "Core/Logging/Log.h"

#include <glm/gtc/packing.hpp>

#include <cmath>
#include <cstring>

namespace Conqueror::DXCommon
{
    namespace
    {
        FormatInfo Color(DXGI_FORMAT f, bool integer = false)
        {
            FormatInfo i;
            i.Resource = i.SRV = i.RTV = f;
            i.Integer = integer;
            return i;
        }

        FormatInfo DepthFmt(DXGI_FORMAT res, DXGI_FORMAT srv, DXGI_FORMAT dsv, bool stencil)
        {
            FormatInfo i;
            i.Resource = res;
            i.SRV = srv;
            i.RTV = DXGI_FORMAT_UNKNOWN;
            i.DSV = dsv;
            i.Depth = true;
            i.Stencil = stencil;
            return i;
        }

        // ── Texel düzeni (yükleme / geri okuma dönüşümleri, VkGL ile aynı mantık) ──
        enum class Comp : uint8_t { UNorm8, UNorm16, Half, Float, SInt32, UInt32, SInt8, UInt8, R11G11B10, D16, D24S8, D32F, D32S8 };
        struct TexelLayout
        {
            int Channels = 4;
            Comp Type = Comp::UNorm8;
            int Bytes = 4;
        };

        TexelLayout LayoutOf(DXGI_FORMAT f)
        {
            switch (f)
            {
                case DXGI_FORMAT_R8G8B8A8_UNORM: case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: case DXGI_FORMAT_R8G8B8A8_TYPELESS: return { 4, Comp::UNorm8, 4 };
                case DXGI_FORMAT_R8_UNORM: return { 1, Comp::UNorm8, 1 };
                case DXGI_FORMAT_R8G8_UNORM: return { 2, Comp::UNorm8, 2 };
                case DXGI_FORMAT_R16G16B16A16_FLOAT: return { 4, Comp::Half, 8 };
                case DXGI_FORMAT_R32G32B32A32_FLOAT: return { 4, Comp::Float, 16 };
                case DXGI_FORMAT_R16_FLOAT: return { 1, Comp::Half, 2 };
                case DXGI_FORMAT_R16G16_FLOAT: return { 2, Comp::Half, 4 };
                case DXGI_FORMAT_R32_FLOAT: return { 1, Comp::Float, 4 };
                case DXGI_FORMAT_R32G32_FLOAT: return { 2, Comp::Float, 8 };
                case DXGI_FORMAT_R11G11B10_FLOAT: return { 3, Comp::R11G11B10, 4 };
                case DXGI_FORMAT_R16G16B16A16_UNORM: return { 4, Comp::UNorm16, 8 };
                case DXGI_FORMAT_R16_UNORM: return { 1, Comp::UNorm16, 2 };
                case DXGI_FORMAT_R16G16_UNORM: return { 2, Comp::UNorm16, 4 };
                case DXGI_FORMAT_R32_SINT: return { 1, Comp::SInt32, 4 };
                case DXGI_FORMAT_R32_UINT: return { 1, Comp::UInt32, 4 };
                case DXGI_FORMAT_R32G32_SINT: return { 2, Comp::SInt32, 8 };
                case DXGI_FORMAT_R32G32B32A32_SINT: return { 4, Comp::SInt32, 16 };
                case DXGI_FORMAT_R8_SINT: return { 1, Comp::SInt8, 1 };
                case DXGI_FORMAT_R8_UINT: return { 1, Comp::UInt8, 1 };
                case DXGI_FORMAT_R16_TYPELESS: return { 1, Comp::D16, 2 };
                case DXGI_FORMAT_R32_TYPELESS: return { 1, Comp::D32F, 4 };
                case DXGI_FORMAT_R24G8_TYPELESS: return { 1, Comp::D24S8, 4 };
                case DXGI_FORMAT_R32G8X24_TYPELESS: return { 1, Comp::D32S8, 8 };
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

        void ReadTexel(const uint8_t* p, const TexelLayout& l, double out[4])
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
                case Comp::R11G11B10: { uint32_t v; std::memcpy(&v, p, 4); glm::vec3 c = glm::unpackF2x11_1x10(v); out[0] = c.r; out[1] = c.g; out[2] = c.b; break; }
                case Comp::D16: { uint16_t v; std::memcpy(&v, p, 2); out[0] = v / 65535.0; break; }
                case Comp::D24S8: { uint32_t v; std::memcpy(&v, p, 4); out[0] = (v & 0xFFFFFF) / 16777215.0; break; }
                case Comp::D32S8: { float v; std::memcpy(&v, p, 4); out[0] = v; break; }
            }
        }

        void WriteTexel(uint8_t* p, const TexelLayout& l, const double in[4])
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
                case Comp::R11G11B10: { uint32_t v = glm::packF2x11_1x10(glm::vec3((float)in[0], (float)in[1], (float)in[2])); std::memcpy(p, &v, 4); break; }
                case Comp::D16: { uint16_t v = (uint16_t)std::clamp(std::lround(in[0] * 65535.0), 0L, 65535L); std::memcpy(p, &v, 2); break; }
                default: break;
            }
        }
    }

    FormatInfo FormatOf(GLenum f)
    {
        switch (f)
        {
            case GL_RGBA: case GL_RGBA8: case GL_RGB: case GL_RGB8: case GL_BGRA: return Color(DXGI_FORMAT_R8G8B8A8_UNORM);
            case GL_SRGB8_ALPHA8: case GL_SRGB_ALPHA: case GL_SRGB8: case GL_SRGB: return Color(DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);
            case GL_RED: case GL_R8: return Color(DXGI_FORMAT_R8_UNORM);
            case GL_RG: case GL_RG8: return Color(DXGI_FORMAT_R8G8_UNORM);
            case GL_RGBA16F: case GL_RGB16F: return Color(DXGI_FORMAT_R16G16B16A16_FLOAT);
            case GL_RGBA32F: case GL_RGB32F: return Color(DXGI_FORMAT_R32G32B32A32_FLOAT);
            case GL_R16F: return Color(DXGI_FORMAT_R16_FLOAT);
            case GL_RG16F: return Color(DXGI_FORMAT_R16G16_FLOAT);
            case GL_R32F: return Color(DXGI_FORMAT_R32_FLOAT);
            case GL_RG32F: return Color(DXGI_FORMAT_R32G32_FLOAT);
            case GL_R11F_G11F_B10F: return Color(DXGI_FORMAT_R11G11B10_FLOAT);
            case GL_RGBA16: case GL_RGB16: return Color(DXGI_FORMAT_R16G16B16A16_UNORM);
            case GL_R16: return Color(DXGI_FORMAT_R16_UNORM);
            case GL_RG16: return Color(DXGI_FORMAT_R16G16_UNORM);
            case GL_R32I: return Color(DXGI_FORMAT_R32_SINT, true);
            case GL_R32UI: return Color(DXGI_FORMAT_R32_UINT, true);
            case GL_RG32I: return Color(DXGI_FORMAT_R32G32_SINT, true);
            case GL_RGBA32I: return Color(DXGI_FORMAT_R32G32B32A32_SINT, true);
            case GL_R8I: return Color(DXGI_FORMAT_R8_SINT, true);
            case GL_R8UI: return Color(DXGI_FORMAT_R8_UINT, true);
            case GL_DEPTH_COMPONENT16: return DepthFmt(DXGI_FORMAT_R16_TYPELESS, DXGI_FORMAT_R16_UNORM, DXGI_FORMAT_D16_UNORM, false);
            case GL_DEPTH_COMPONENT: case GL_DEPTH_COMPONENT24: case GL_DEPTH_COMPONENT32: case GL_DEPTH_COMPONENT32F:
                return DepthFmt(DXGI_FORMAT_R32_TYPELESS, DXGI_FORMAT_R32_FLOAT, DXGI_FORMAT_D32_FLOAT, false);
            case GL_DEPTH_STENCIL: case GL_DEPTH24_STENCIL8:
                return DepthFmt(DXGI_FORMAT_R24G8_TYPELESS, DXGI_FORMAT_R24_UNORM_X8_TYPELESS, DXGI_FORMAT_D24_UNORM_S8_UINT, true);
            case GL_DEPTH32F_STENCIL8:
                return DepthFmt(DXGI_FORMAT_R32G8X24_TYPELESS, DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS, DXGI_FORMAT_D32_FLOAT_S8X24_UINT, true);
            default:
                CQ_CORE_WARN("[DX] Unsupported internal format 0x{0:x}, using RGBA8", (unsigned)f);
                return Color(DXGI_FORMAT_R8G8B8A8_UNORM);
        }
    }

    uint32_t TexelBytes(DXGI_FORMAT f) { return (uint32_t)LayoutOf(f).Bytes; }

    uint32_t MaxLevels(uint32_t w, uint32_t h)
    {
        uint32_t levels = 1;
        for (uint32_t m = std::max(w, h); m > 1; m >>= 1)
            ++levels;
        return levels;
    }

    const uint8_t* ConvertUpload(DXGI_FORMAT dstFormat, int w, int h, GLenum format, GLenum type, const void* data, int unpackRowLength, int unpackAlignment,
                                 std::vector<uint8_t>& staging, size_t& pitch)
    {
        const TexelLayout dst = LayoutOf(dstFormat);
        const int srcChannels = GLChannels(format);
        const int compSize = GLTypeSize(type);
        const bool packed = type == GL_UNSIGNED_INT_8_8_8_8_REV || type == GL_UNSIGNED_INT_8_8_8_8;
        const int srcTexel = packed ? 4 : srcChannels * compSize;
        const int rowLength = unpackRowLength > 0 ? unpackRowLength : w;
        const int align = std::max(1, unpackAlignment);
        const size_t srcPitch = ((size_t)rowLength * srcTexel + align - 1) / align * align;
        const size_t dstPitch = (size_t)w * dst.Bytes;
        const uint8_t* src = static_cast<const uint8_t*>(data);
        const bool bgra = format == GL_BGRA || format == GL_BGR;
        const bool direct = !bgra && !packed && srcChannels == dst.Channels &&
                            ((dst.Type == Comp::UNorm8 && type == GL_UNSIGNED_BYTE) || (dst.Type == Comp::Float && type == GL_FLOAT) ||
                             (dst.Type == Comp::Half && type == GL_HALF_FLOAT) || (dst.Type == Comp::SInt32 && type == GL_INT) ||
                             (dst.Type == Comp::UInt32 && type == GL_UNSIGNED_INT) || (dst.Type == Comp::UNorm16 && type == GL_UNSIGNED_SHORT));
        if (direct && srcPitch == dstPitch)
        {
            pitch = srcPitch;
            return src;
        }
        staging.resize(dstPitch * (size_t)h);
        for (int row = 0; row < h; ++row)
        {
            const uint8_t* s = src + srcPitch * row;
            uint8_t* d = staging.data() + dstPitch * row;
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
                    for (int i = 0; i < 4; ++i)
                        v[i] = sp[i] / 255.0;
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
                WriteTexel(d + (size_t)col * dst.Bytes, dst, v);
            }
        }
        pitch = dstPitch;
        return staging.data();
    }

    void ConvertReadback(DXGI_FORMAT srcFormat, const uint8_t* data, size_t srcPitch, int w, int h, GLenum format, GLenum type, void* out, int packAlignment)
    {
        const TexelLayout layout = LayoutOf(srcFormat);
        const int outChannels = GLChannels(format);
        const int compSize = GLTypeSize(type);
        const int outTexel = outChannels * compSize;
        const int align = std::max(1, packAlignment);
        const size_t outPitch = ((size_t)w * outTexel + align - 1) / align * align;
        uint8_t* dst = static_cast<uint8_t*>(out);
        const bool bgra = format == GL_BGRA;
        for (int row = 0; row < h; ++row)
        {
            const uint8_t* s = data + srcPitch * (size_t)row;
            uint8_t* d = dst + outPitch * row;
            for (int col = 0; col < w; ++col)
            {
                double v[4];
                ReadTexel(s + (size_t)col * layout.Bytes, layout, v);
                if (bgra)
                    std::swap(v[0], v[2]);
                for (int i = 0; i < outChannels; ++i)
                    WriteGL(d + (size_t)col * outTexel + i * compSize, type, v[i]);
            }
        }
    }

    void ParseClearValue(bool depth, bool integer, GLenum format, GLenum type, const void* data, float color[4], float& depthValue, uint8_t& stencil)
    {
        const auto* bytes = static_cast<const uint8_t*>(data);
        color[0] = color[1] = color[2] = color[3] = 0.0f;
        depthValue = 1.0f;
        stencil = 0;
        if (!bytes)
            return;
        if (depth)
        {
            if (type == GL_UNSIGNED_INT_24_8)
            {
                uint32_t packedValue;
                std::memcpy(&packedValue, bytes, 4);
                depthValue = (packedValue >> 8) / 16777215.0f;
                stencil = (uint8_t)(packedValue & 0xFF);
            }
            else
                depthValue = (float)ReadGL(bytes, type);
            return;
        }
        const int n = GLChannels(format);
        const int cs = GLTypeSize(type);
        for (int i = 0; i < n && i < 4; ++i)
        {
            const uint8_t* p = bytes + i * cs;
            if (integer)
            {
                int32_t iv = 0;
                if (cs == 4) std::memcpy(&iv, p, 4);
                else if (cs == 2) { int16_t s; std::memcpy(&s, p, 2); iv = s; }
                else iv = (int8_t)*p;
                color[i] = (float)iv;   // tamsayı hedeflerde D3D değeri türe çevirir
            }
            else
                color[i] = (float)ReadGL(p, type);
        }
        if (!integer && n < 4)
            color[3] = 1.0f;
    }

    UINT CompareFunc(GLenum f)
    {
        // D3D_COMPARISON_*: NEVER 1, LESS 2, EQUAL 3, LESS_EQUAL 4, GREATER 5, NOT_EQUAL 6, GREATER_EQUAL 7, ALWAYS 8
        switch (f)
        {
            case GL_NEVER: return 1;
            case GL_LESS: return 2;
            case GL_EQUAL: return 3;
            case GL_LEQUAL: return 4;
            case GL_GREATER: return 5;
            case GL_NOTEQUAL: return 6;
            case GL_GEQUAL: return 7;
            default: return 8;
        }
    }

    SamplerDesc MakeSamplerDesc(GLenum minFilter, GLenum magFilter, GLenum wrapS, GLenum wrapT, GLenum wrapR, GLenum compareFunc, float maxAnisotropy, float minLod,
                                float maxLod, float lodBias, const float borderColor[4], bool integer, bool comparison)
    {
        auto linear = [](GLenum f) { return !(f == GL_NEAREST || f == GL_NEAREST_MIPMAP_NEAREST || f == GL_NEAREST_MIPMAP_LINEAR); };
        // D3D_TEXTURE_ADDRESS_*: WRAP 1, MIRROR 2, CLAMP 3, BORDER 4
        auto address = [](GLenum w) -> UINT {
            switch (w)
            {
                case GL_CLAMP_TO_EDGE: return 3;
                case GL_MIRRORED_REPEAT: return 2;
                case GL_CLAMP_TO_BORDER: return 4;
                default: return 1;
            }
        };
        const bool mip = minFilter != GL_NEAREST && minFilter != GL_LINEAR;
        const bool mipLinear = minFilter == GL_NEAREST_MIPMAP_LINEAR || minFilter == GL_LINEAR_MIPMAP_LINEAR;
        const bool minL = !integer && linear(minFilter), magL = !integer && linear(magFilter), mipL = !integer && mipLinear;
        SamplerDesc sd;
        // D3D_FILTER kodlaması elle (MinGW başlıklarında indirgeme türlü makrolar yok):
        // min << 4 | mag << 2 | mip, karşılaştırma (indirgeme 1) << 7, anizotropik bit 0x40
        const UINT reduction = comparison ? 1u : 0u;
        if (!integer && mip && maxAnisotropy > 1.0f)
            sd.Filter = 0x40u | 0x15u | (reduction << 7);
        else
            sd.Filter = ((minL ? 1u : 0u) << 4) | ((magL ? 1u : 0u) << 2) | (mipL ? 1u : 0u) | (reduction << 7);
        sd.AddressU = address(wrapS);
        sd.AddressV = address(wrapT);
        sd.AddressW = address(wrapR);
        sd.MipLODBias = lodBias;
        sd.MaxAnisotropy = (UINT)std::clamp(maxAnisotropy, 1.0f, 16.0f);
        sd.ComparisonFunc = CompareFunc(compareFunc);
        if (sd.ComparisonFunc == 8 && compareFunc != GL_ALWAYS)
            sd.ComparisonFunc = 4;
        for (int i = 0; i < 4; ++i)
            sd.BorderColor[i] = borderColor[i];
        // Mip'siz küçültme yalnızca taban düzeyi örnekler (GL davranışı)
        sd.MinLOD = mip ? std::max(0.0f, minLod) : 0.0f;
        sd.MaxLOD = mip ? std::max(sd.MinLOD, std::min(maxLod, 3.402823466e+38f)) : 0.0f;

        uint64_t key = 1469598103934665603ull;
        auto mix = [&](uint64_t v) { key = (key ^ v) * 1099511628211ull; };
        mix(sd.Filter); mix(sd.AddressU); mix(sd.AddressV); mix(sd.AddressW); mix((uint64_t)(int64_t)(sd.MipLODBias * 64.0f)); mix(sd.MaxAnisotropy);
        mix(sd.ComparisonFunc); mix((uint64_t)(int64_t)(sd.MinLOD * 64.0f)); mix((uint64_t)(int64_t)(std::min(sd.MaxLOD, 1.0e6f) * 64.0f));
        for (float b : sd.BorderColor) mix((uint64_t)(int64_t)(b * 255.0f));
        sd.Key = key;
        return sd;
    }
}

#endif
