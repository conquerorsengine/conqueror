#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "../../../../External/FidelityFX-FSR2/src/ffx-fsr2-api/ffx_fsr2.h"

// AMD FidelityFX Super Resolution 2 için OpenGL 4.5 arka ucu (SDK'nın FfxFsr2Interface'i). Kaynaklar GL dokuları; geçişler
// çalışma anında FSR 2'nin Vulkan GLSL kaynaklarından glslang (-> SPIR-V) ve SPIRV-Cross (-> GLSL 450, birleşik örnekleyiciler)
// ile derlenen hesaplama programları. Bağlama noktaları program yansımasından okunur.
namespace Conqueror::FSR2GL
{
    size_t GetScratchMemorySize();

    // shaderDirectory: FSR 2 GLSL kaynaklarının klasörü (Engine/src/Shaders/Include/ThirdParty/FSR2)
    FfxErrorCode GetInterface(FfxFsr2Interface* outInterface, void* scratchBuffer, size_t scratchBufferSize, const std::string& shaderDirectory);

    // GL dokusunu FSR 2 giriş / çıktı kaynağı olarak tanıtır (glInternalFormat: dokunun iç biçimi)
    FfxResource GetTextureResource(uint32_t texture, uint32_t width, uint32_t height, uint32_t glInternalFormat, const wchar_t* name,
                                   bool isDepth = false);
}
