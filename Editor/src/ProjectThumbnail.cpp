#include "ProjectThumbnail.h"
#include "Core/Logging/Log.h"

#include <glad/glad.h>
#include "stb_image_resize2.h"
#include "stb_image_write.h"

#include <algorithm>
#include <vector>

namespace Conqueror::Editor
{
    bool WriteProjectThumbnail(uint32_t colorTexture, uint32_t width, uint32_t height, const std::filesystem::path& path,
                               uint32_t outWidth, uint32_t outHeight)
    {
        if (!colorTexture || width < 2 || height < 2 || outWidth == 0 || outHeight == 0)
            return false;

        std::vector<uint8_t> pixels((size_t)width * height * 4);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glGetTextureImage(colorTexture, 0, GL_RGBA, GL_UNSIGNED_BYTE, (GLsizei)pixels.size(), pixels.data());

        // Hedef orana göre ortalanmış kırpma (OpenGL dokusu alttan başlar: satırları ters çevirerek kopyala)
        const float target = (float)outWidth / (float)outHeight;
        uint32_t cropW = width, cropH = height;
        if ((float)width / (float)height > target)
            cropW = std::max(1u, (uint32_t)((float)height * target));
        else
            cropH = std::max(1u, (uint32_t)((float)width / target));
        const uint32_t x0 = (width - cropW) / 2;
        const uint32_t y0 = (height - cropH) / 2;

        std::vector<uint8_t> cropped((size_t)cropW * cropH * 4);
        for (uint32_t y = 0; y < cropH; ++y)
        {
            const uint8_t* src = pixels.data() + ((size_t)(y0 + cropH - 1 - y) * width + x0) * 4;
            std::copy(src, src + (size_t)cropW * 4, cropped.data() + (size_t)y * cropW * 4);
        }
        for (size_t i = 3; i < cropped.size(); i += 4)
            cropped[i] = 255;

        std::vector<uint8_t> out((size_t)outWidth * outHeight * 4);
        if (!stbir_resize_uint8_srgb(cropped.data(), (int)cropW, (int)cropH, (int)cropW * 4, out.data(), (int)outWidth, (int)outHeight,
                                     (int)outWidth * 4, STBIR_RGBA))
            return false;

        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
        const bool ok = stbi_write_png(path.string().c_str(), (int)outWidth, (int)outHeight, 4, out.data(), (int)outWidth * 4) != 0;
        if (!ok)
            CQ_CORE_WARN("Project thumbnail could not be written: {0}", path.string());
        return ok;
    }
}
