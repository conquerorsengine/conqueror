#pragma once

// GL çeviri katmanlarının ortak gölgelendirici yansıtması (DxGL, D12GL, MtlGL). Bkz. GLReflect.cpp.

#include <glad/glad.h>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace Conqueror::GLEmu
{
    struct UniformSlot
    {
        std::string Name;
        uint32_t Offset = 0;
        GLenum Type = GL_FLOAT;
        uint32_t MatrixStride = 16;
        int Sampler = -1;
        int SamplerElement = 0;
        uint32_t ArrayStride = 0;
        int ArrayRemaining = 1;
    };

    struct SamplerBinding
    {
        std::string Name;
        uint32_t Register = 0;                           // doku yazmacı (ve örnekleyici — paylaşımlı değilse aynı)
        uint32_t SamplerRegister = 0;
        uint32_t Count = 1;
        bool SharedSampler = false;                      // büyük dizi: dokular ayrı, tek örnekleyici
        GLenum Target = GL_TEXTURE_2D;
        bool Shadow = false;
        bool Integer = false;
        std::vector<int> Units;
    };

    struct BlockBinding
    {
        std::string Name;
        uint32_t Register = 0;                           // sabit arabellek yazmacı
        uint32_t GLBinding = 0;
        uint32_t Size = 0;
    };

    struct VertexInput
    {
        int Location = 0;
        bool Integer = false;
        uint32_t Components = 4;
        std::string Name;
    };

    struct ResourceLimits
    {
        uint32_t ConstantBuffers = 14;
        uint32_t Resources = 128;
        uint32_t Samplers = 16;
        const char* ApiName = "GPU";
    };

    struct ShaderSource
    {
        GLenum Type = 0;
        const std::string* Source = nullptr;
    };

    // Yazmaç düzeni: sabit arabellekler 0.. (bloklar, sonra CQ_Globals); birleşik örnekleyiciler doku = örnekleyici yazmacı;
    // ayrık dokular / örnekleyiciler (büyük diziler) ardından. Hepsi küme 0'da, SPIR-V Binding = yazmaç.
    struct ReflectedProgram
    {
        std::vector<UniformSlot> Uniforms;
        std::unordered_map<std::string, int> UniformByName;
        std::vector<SamplerBinding> Samplers;
        std::vector<BlockBinding> Blocks;
        std::vector<VertexInput> Inputs;
        uint32_t ColorOutputs = 1;
        uint32_t ColorOutputMask = 1;
        int GlobalsRegister = -1;
        uint32_t GlobalsSize = 0;
        std::vector<uint8_t> Globals;                    // ilk değerler (uniform x = ...;) yazılmış
        std::vector<uint32_t> VertexSpirv, FragmentSpirv;
    };

    bool ReflectProgram(const std::vector<ShaderSource>& shaders, const ResourceLimits& limits, ReflectedProgram& out, std::string& log);
}
