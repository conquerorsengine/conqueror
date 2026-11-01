#pragma once

#include <cstdint>
#include <filesystem>

namespace Conqueror::Editor
{
    // Hub proje kartı küçük resmi: verilen RGBA8 renk dokusunu 16:9 ortalanmış kırpar, küçültür ve PNG yazar.
    // Hub "<proje>/Library/thumbnail.png" dosyasını gösterir.
    bool WriteProjectThumbnail(uint32_t colorTexture, uint32_t width, uint32_t height, const std::filesystem::path& path,
                               uint32_t outWidth = 384, uint32_t outHeight = 216);
}
