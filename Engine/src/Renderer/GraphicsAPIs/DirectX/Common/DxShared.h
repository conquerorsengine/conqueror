#pragma once

#if defined(CQ_D3D11_ENABLED) || defined(CQ_D3D12_ENABLED)

// Direct3D 11 (DxGL) ve Direct3D 12 (D12GL) çeviri katmanlarının ortak parçaları:
//  * GLSL -> SPIR-V -> HLSL -> DXBC çevirisi ve yansıtma (uniform yuvaları, örnekleyiciler, bloklar, girişler)
//  * GL iç biçimi -> DXGI biçimi, texel dönüşümleri (yükleme / geri okuma / temizleme değeri)
//  * GL örnekleyici parametreleri -> D3D örnekleyici tanımı (D3D11 ve D3D12 tanımları alan alana aynıdır)

#ifndef NOMINMAX
    #define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <dxgiformat.h>

#include <glad/glad.h>

#include "../../GLEmu/GLReflect.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace Conqueror::DXCommon
{
    using GLEmu::UniformSlot;
    using GLEmu::SamplerBinding;
    using GLEmu::BlockBinding;
    using GLEmu::VertexInput;
    using GLEmu::ShaderSource;
    using ShaderLimits = GLEmu::ResourceLimits;

    // Yansıtma (GLEmu) + DXBC (vs_5_0 / ps_5_0)
    struct TranslatedProgram : GLEmu::ReflectedProgram
    {
        std::vector<uint8_t> VS, PS;
    };

    // GLSL köşe + parça gölgelendiricisini HLSL'e çevirip derler; id yalnızca döküm dosyası adı içindir
    bool TranslateProgram(const std::vector<ShaderSource>& shaders, uint64_t id, const ShaderLimits& limits, TranslatedProgram& out, std::string& log);
    // HLSL -> DXBC (d3dcompiler_47)
    bool CompileHLSL(const std::string& source, const char* name, const char* entry, const char* target, std::vector<uint8_t>& out, std::string& errors);

    // ── Biçimler ──
    struct FormatInfo
    {
        DXGI_FORMAT Resource = DXGI_FORMAT_R8G8B8A8_UNORM;
        DXGI_FORMAT SRV = DXGI_FORMAT_R8G8B8A8_UNORM;
        DXGI_FORMAT RTV = DXGI_FORMAT_R8G8B8A8_UNORM;
        DXGI_FORMAT DSV = DXGI_FORMAT_UNKNOWN;
        bool Depth = false, Stencil = false, Integer = false;
    };
    FormatInfo FormatOf(GLenum internalFormat);
    uint32_t TexelBytes(DXGI_FORMAT resourceFormat);
    uint32_t MaxLevels(uint32_t w, uint32_t h);

    // GL kaynak verisini DXGI satırlarına çevirir. Dönüşüm gerekmiyorsa data'yı döndürür, gerekiyorsa staging'i doldurur.
    const uint8_t* ConvertUpload(DXGI_FORMAT dst, int w, int h, GLenum format, GLenum type, const void* data, int unpackRowLength, int unpackAlignment,
                                 std::vector<uint8_t>& staging, size_t& pitch);
    // DXGI satırlarını (src, srcPitch; ilk texel = (0,0)) GL biçimine yazar
    void ConvertReadback(DXGI_FORMAT src, const uint8_t* data, size_t srcPitch, int w, int h, GLenum format, GLenum type, void* out, int packAlignment);
    // glClearTexImage verisi -> renk / derinlik + şablon
    void ParseClearValue(bool depth, bool integer, GLenum format, GLenum type, const void* data, float color[4], float& depthValue, uint8_t& stencil);

    // ── Örnekleyici ──
    struct SamplerDesc
    {
        UINT Filter = 0;
        UINT AddressU = 1, AddressV = 1, AddressW = 1;   // 1 = WRAP
        float MipLODBias = 0.0f;
        UINT MaxAnisotropy = 1;
        UINT ComparisonFunc = 4;                         // LESS_EQUAL
        float BorderColor[4] = { 0, 0, 0, 0 };
        float MinLOD = 0.0f, MaxLOD = 0.0f;
        uint64_t Key = 0;
    };
    SamplerDesc MakeSamplerDesc(GLenum minFilter, GLenum magFilter, GLenum wrapS, GLenum wrapT, GLenum wrapR, GLenum compareFunc, float maxAnisotropy, float minLod,
                                float maxLod, float lodBias, const float borderColor[4], bool integer, bool comparison);

    template <typename T>
    SamplerDesc MakeSamplerDesc(const T& p, bool integer, bool comparison)
    {
        return MakeSamplerDesc(p.MinFilter, p.MagFilter, p.WrapS, p.WrapT, p.WrapR, p.CompareFunc, p.MaxAnisotropy, p.MinLod, p.MaxLod, p.LodBias, p.BorderColor,
                               integer, comparison);
    }

    // GL karşılaştırma işlevi -> D3D_COMPARISON (D3D11 ve D3D12 değerleri aynı)
    UINT CompareFunc(GLenum f);
}

#endif
