#pragma once

#include <cstdint>

// GL ön yüzünün (GLFrontEnd.inl) arka uçtan istediği cihaz sınırları
namespace Conqueror::GLEmu
{
    struct Limits
    {
        int32_t MaxTextureSize = 16384;
        int32_t MaxCubeMapSize = 16384;
        int32_t MaxTextureUnits = 32;
        int32_t MaxVertexAttribs = 16;
        int32_t MaxUniformBufferBindings = 64;
        int32_t MaxUniformBlockSize = 65536;
        int32_t UniformBufferAlignment = 256;
        int32_t MaxColorAttachments = 8;
        int32_t MaxViewport[2] = { 16384, 16384 };
        float MaxAnisotropy = 1.0f;
        float LineWidthRange[2] = { 1.0f, 1.0f };
        int32_t VideoMemoryKB = 0;
    };
}
