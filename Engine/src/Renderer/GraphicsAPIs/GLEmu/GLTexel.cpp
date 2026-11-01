// API'den bağımsız texel dönüşümleri (bkz. GLTexel.h). DirectX/Common/DxFormats.cpp ile aynı mantık, DXGI yerine TexelKind.

#include "GLTexel.h"
#include "Core/Logging/Log.h"

#include <glm/gtc/packing.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Conqueror::GLEmu
{
    namespace
    {
        enum class Comp : uint8_t { UNorm8, UNorm16, Half, Float, SInt32, UInt32, SInt8, UInt8, R11G11B10, D16, D24S8, D32F, D32S8 };

        Comp CompOf(TexelKind k)
        {
            switch (k)
            {
                case TexelKind::RGBA8: case TexelKind::RGBA8_SRGB: case TexelKind::R8: case TexelKind::RG8: return Comp::UNorm8;
                case TexelKind::RGBA16F: case TexelKind::R16F: case TexelKind::RG16F: return Comp::Half;
                case TexelKind::RGBA32F: case TexelKind::R32F: case TexelKind::RG32F: return Comp::Float;
                case TexelKind::RG11B10F: return Comp::R11G11B10;
                case TexelKind::RGBA16: case TexelKind::R16: case TexelKind::RG16: return Comp::UNorm16;
                case TexelKind::R32I: case TexelKind::RG32I: case TexelKind::RGBA32I: return Comp::SInt32;
                case TexelKind::R32UI: return Comp::UInt32;
                case TexelKind::R8I: return Comp::SInt8;
                case TexelKind::R8UI: return Comp::UInt8;
                case TexelKind::D16: return Comp::D16;
                case TexelKind::D24S8: return Comp::D24S8;
                case TexelKind::D32F: case TexelKind::D32FS8: return Comp::D32F;   // D32FS8: derinlik düzlemi (float)
            }
            return Comp::UNorm8;
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

        void ReadTexel(const uint8_t* p, const TexelInfo& l, double out[4])
        {
            out[0] = out[1] = out[2] = 0.0;
            out[3] = 1.0;
            switch (CompOf(l.Kind))
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

        void WriteTexel(uint8_t* p, const TexelInfo& l, const double in[4])
        {
            switch (CompOf(l.Kind))
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

    TexelInfo TexelInfoOf(TexelKind k)
    {
        TexelInfo i;
        i.Kind = k;
        switch (k)
        {
            case TexelKind::RGBA8: case TexelKind::RGBA8_SRGB: i.Channels = 4; i.Bytes = 4; break;
            case TexelKind::R8: i.Channels = 1; i.Bytes = 1; break;
            case TexelKind::RG8: i.Channels = 2; i.Bytes = 2; break;
            case TexelKind::RGBA16F: i.Channels = 4; i.Bytes = 8; break;
            case TexelKind::RGBA32F: i.Channels = 4; i.Bytes = 16; break;
            case TexelKind::R16F: i.Channels = 1; i.Bytes = 2; break;
            case TexelKind::RG16F: i.Channels = 2; i.Bytes = 4; break;
            case TexelKind::R32F: i.Channels = 1; i.Bytes = 4; break;
            case TexelKind::RG32F: i.Channels = 2; i.Bytes = 8; break;
            case TexelKind::RG11B10F: i.Channels = 3; i.Bytes = 4; break;
            case TexelKind::RGBA16: i.Channels = 4; i.Bytes = 8; break;
            case TexelKind::R16: i.Channels = 1; i.Bytes = 2; break;
            case TexelKind::RG16: i.Channels = 2; i.Bytes = 4; break;
            case TexelKind::R32I: case TexelKind::R32UI: i.Channels = 1; i.Bytes = 4; i.Integer = true; break;
            case TexelKind::RG32I: i.Channels = 2; i.Bytes = 8; i.Integer = true; break;
            case TexelKind::RGBA32I: i.Channels = 4; i.Bytes = 16; i.Integer = true; break;
            case TexelKind::R8I: case TexelKind::R8UI: i.Channels = 1; i.Bytes = 1; i.Integer = true; break;
            case TexelKind::D16: i.Channels = 1; i.Bytes = 2; i.Depth = true; break;
            case TexelKind::D32F: i.Channels = 1; i.Bytes = 4; i.Depth = true; break;
            case TexelKind::D24S8: i.Channels = 1; i.Bytes = 4; i.Depth = i.Stencil = true; break;
            case TexelKind::D32FS8: i.Channels = 1; i.Bytes = 4; i.Depth = i.Stencil = true; break;
        }
        return i;
    }

    TexelInfo TexelOf(GLenum f)
    {
        switch (f)
        {
            case GL_RGBA: case GL_RGBA8: case GL_RGB: case GL_RGB8: case GL_BGRA: return TexelInfoOf(TexelKind::RGBA8);
            case GL_SRGB8_ALPHA8: case GL_SRGB_ALPHA: case GL_SRGB8: case GL_SRGB: return TexelInfoOf(TexelKind::RGBA8_SRGB);
            case GL_RED: case GL_R8: return TexelInfoOf(TexelKind::R8);
            case GL_RG: case GL_RG8: return TexelInfoOf(TexelKind::RG8);
            case GL_RGBA16F: case GL_RGB16F: return TexelInfoOf(TexelKind::RGBA16F);
            case GL_RGBA32F: case GL_RGB32F: return TexelInfoOf(TexelKind::RGBA32F);
            case GL_R16F: return TexelInfoOf(TexelKind::R16F);
            case GL_RG16F: return TexelInfoOf(TexelKind::RG16F);
            case GL_R32F: return TexelInfoOf(TexelKind::R32F);
            case GL_RG32F: return TexelInfoOf(TexelKind::RG32F);
            case GL_R11F_G11F_B10F: return TexelInfoOf(TexelKind::RG11B10F);
            case GL_RGBA16: case GL_RGB16: return TexelInfoOf(TexelKind::RGBA16);
            case GL_R16: return TexelInfoOf(TexelKind::R16);
            case GL_RG16: return TexelInfoOf(TexelKind::RG16);
            case GL_R32I: return TexelInfoOf(TexelKind::R32I);
            case GL_R32UI: return TexelInfoOf(TexelKind::R32UI);
            case GL_RG32I: return TexelInfoOf(TexelKind::RG32I);
            case GL_RGBA32I: return TexelInfoOf(TexelKind::RGBA32I);
            case GL_R8I: return TexelInfoOf(TexelKind::R8I);
            case GL_R8UI: return TexelInfoOf(TexelKind::R8UI);
            case GL_DEPTH_COMPONENT16: return TexelInfoOf(TexelKind::D16);
            case GL_DEPTH_COMPONENT: case GL_DEPTH_COMPONENT24: case GL_DEPTH_COMPONENT32: case GL_DEPTH_COMPONENT32F: return TexelInfoOf(TexelKind::D32F);
            case GL_DEPTH_STENCIL: case GL_DEPTH24_STENCIL8: return TexelInfoOf(TexelKind::D24S8);
            case GL_DEPTH32F_STENCIL8: return TexelInfoOf(TexelKind::D32FS8);
            default:
                CQ_CORE_WARN("[GLEmu] Unsupported internal format 0x{0:x}, using RGBA8", (unsigned)f);
                return TexelInfoOf(TexelKind::RGBA8);
        }
    }

    uint32_t MaxLevels(uint32_t w, uint32_t h)
    {
        uint32_t levels = 1;
        for (uint32_t m = std::max(w, h); m > 1; m >>= 1)
            ++levels;
        return levels;
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

    const uint8_t* ConvertUpload(TexelKind dstKind, int w, int h, GLenum format, GLenum type, const void* data, int unpackRowLength, int unpackAlignment,
                                 std::vector<uint8_t>& staging, size_t& pitch)
    {
        const TexelInfo dst = TexelInfoOf(dstKind);
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
                            ((CompOf(dst.Kind) == Comp::UNorm8 && type == GL_UNSIGNED_BYTE) || (CompOf(dst.Kind) == Comp::Float && type == GL_FLOAT) ||
                             (CompOf(dst.Kind) == Comp::Half && type == GL_HALF_FLOAT) || (CompOf(dst.Kind) == Comp::SInt32 && type == GL_INT) ||
                             (CompOf(dst.Kind) == Comp::UInt32 && type == GL_UNSIGNED_INT) || (CompOf(dst.Kind) == Comp::UNorm16 && type == GL_UNSIGNED_SHORT));
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

    void ConvertReadback(TexelKind srcKind, const uint8_t* data, size_t srcPitch, int w, int h, GLenum format, GLenum type, void* out, int packAlignment)
    {
        const TexelInfo layout = TexelInfoOf(srcKind);
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

}
