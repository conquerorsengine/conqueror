#include "OpenGLTexture.h"
#include "Core/Tiering/QualitySettings.h"
#include "Renderer/Renderer.h"
#include "Core/Logging/Log.h"
#include "Core/Utils/FileSystem.h"
#include "Renderer/TextureStreaming.h"

#include <stb_image.h>
#include <stb_image_resize2.h>
// Crunch çözücüsü (uygulama crnlib'in crn_decomp.cpp'sinde)
#define CRND_HEADER_FILE_ONLY
#include <crn_decomp.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <vector>

namespace Conqueror
{
    namespace
    {
        uint32_t MipCount(uint32_t w, uint32_t h)
        {
            uint32_t levels = 1;
            uint32_t m = std::max(w, h);
            while (m > 1)
            {
                m >>= 1;
                ++levels;
            }
            return levels;
        }

        void TrackMemory(uint64_t& tracked, uint64_t bytes)
        {
            auto& stat = Renderer::GetStats().TextureMemory;
            stat = std::max(0.0f, stat - (float)tracked / (1024.0f * 1024.0f));
            tracked = bytes;
            stat += (float)bytes / (1024.0f * 1024.0f);
        }

        bool ReadFile(const std::filesystem::path& path, std::vector<uint8_t>& out)
        {
            std::ifstream stream(path, std::ios::binary | std::ios::ate);
            if (!stream)
                return false;
            std::streamsize size = stream.tellg();
            if (size <= 0)
                return false;
            stream.seekg(0, std::ios::beg);
            out.resize((size_t)size);
            return (bool)stream.read(reinterpret_cast<char*>(out.data()), size);
        }

        // Gri tonlamalı (ya da RGB ortalamalı) yükseklikten teğet uzayı normal haritası (Sobel)
        void HeightToNormals(std::vector<uint8_t>& rgba, uint32_t w, uint32_t h, float bumpiness)
        {
            std::vector<float> height((size_t)w * h);
            for (size_t i = 0; i < height.size(); ++i)
                height[i] = (rgba[i * 4 + 0] + rgba[i * 4 + 1] + rgba[i * 4 + 2]) / (3.0f * 255.0f);
            auto H = [&](int x, int y) {
                x = (x % (int)w + (int)w) % (int)w;
                y = (y % (int)h + (int)h) % (int)h;
                return height[(size_t)y * w + x];
            };
            for (uint32_t y = 0; y < h; ++y)
                for (uint32_t x = 0; x < w; ++x)
                {
                    float dx = (H(x + 1, y - 1) + 2.0f * H(x + 1, y) + H(x + 1, y + 1)) - (H(x - 1, y - 1) + 2.0f * H(x - 1, y) + H(x - 1, y + 1));
                    float dy = (H(x - 1, y + 1) + 2.0f * H(x, y + 1) + H(x + 1, y + 1)) - (H(x - 1, y - 1) + 2.0f * H(x, y - 1) + H(x + 1, y - 1));
                    float nx = -dx * bumpiness, ny = -dy * bumpiness, nz = 1.0f;
                    float len = std::sqrt(nx * nx + ny * ny + nz * nz);
                    uint8_t* p = &rgba[((size_t)y * w + x) * 4];
                    p[0] = (uint8_t)std::lround((nx / len * 0.5f + 0.5f) * 255.0f);
                    p[1] = (uint8_t)std::lround((ny / len * 0.5f + 0.5f) * 255.0f);
                    p[2] = (uint8_t)std::lround((nz / len * 0.5f + 0.5f) * 255.0f);
                    p[3] = 255;
                }
        }
    }

    namespace
    {
        // 8 bit görüntü: çöz (stbi), normal üretimi, MaxSize küçültmesi, Format / Compression. Yalnız CPU (GL çağrısı yok):
        // doku akışının arka plan iş parçacığı da aynı yolu kullanır. tw / th: hedef boyut (MaxSize sınırı)
        bool DecodeLDR(const unsigned char* bytes, size_t size, const TextureImportSettings& settings, int channels, uint32_t tw, uint32_t th,
                       std::vector<uint8_t>& pixels, int& comps, GLenum& internalFormat, GLenum& dataFormat)
        {
            int w = 0, h = 0, got = 0;
            // 8 bit: 1/2 kanallı (gri, gri+alfa) görüntüler RGBA'ya genişletilir
            const int wanted = channels == 3 ? 3 : 4;
            stbi_uc* data = stbi_load_from_memory(bytes, (int)size, &w, &h, &got, wanted);
            if (!data)
                return false;
            pixels.assign(data, data + (size_t)w * h * wanted);
            stbi_image_free(data);
            comps = wanted;
            if (settings.Type == TextureType::NormalMap && settings.NormalFromGrayscale)
            {
                if (comps == 3)
                {
                    std::vector<uint8_t> rgba((size_t)w * h * 4);
                    for (size_t i = 0; i < (size_t)w * h; ++i)
                    {
                        rgba[i * 4 + 0] = pixels[i * 3 + 0];
                        rgba[i * 4 + 1] = pixels[i * 3 + 1];
                        rgba[i * 4 + 2] = pixels[i * 3 + 2];
                        rgba[i * 4 + 3] = 255;
                    }
                    pixels.swap(rgba);
                    comps = 4;
                }
                HeightToNormals(pixels, (uint32_t)w, (uint32_t)h, settings.Bumpiness);
            }
            if (tw != (uint32_t)w || th != (uint32_t)h)
            {
                std::vector<uint8_t> scaled((size_t)tw * th * comps);
                // Renk dokuları sRGB kodlu: ölçekleme doğrusal uzayda (normal haritaları doğrusal). Resize Algorithm: Mitchell ya da Bilinear
                stbir_resize(pixels.data(), w, h, 0, scaled.data(), (int)tw, (int)th, 0, comps == 4 ? STBIR_RGBA : STBIR_RGB,
                             settings.Type == TextureType::NormalMap ? STBIR_TYPE_UINT8 : STBIR_TYPE_UINT8_SRGB, STBIR_EDGE_CLAMP,
                             settings.Resize == TextureResizeAlgorithm::Bilinear ? STBIR_FILTER_TRIANGLE : STBIR_FILTER_MITCHELL);
                pixels.swap(scaled);
            }
            // Format ve Compression (Import Settings): GPU biçimi
            const size_t pixelCount = (size_t)tw * th;
            if (settings.Format == TextureFormatChoice::R8)
            {
                std::vector<uint8_t> red(pixelCount);
                for (size_t i = 0; i < pixelCount; ++i)
                    red[i] = pixels[i * (size_t)comps];
                pixels.swap(red);
                comps = 1;
                internalFormat = GL_R8;
                dataFormat = GL_RED;
                return true;
            }
            if (settings.Format == TextureFormatChoice::RGB24 && comps == 4)
            {
                std::vector<uint8_t> rgb(pixelCount * 3);
                for (size_t i = 0; i < pixelCount; ++i)
                    for (int k = 0; k < 3; ++k)
                        rgb[i * 3 + (size_t)k] = pixels[i * 4 + (size_t)k];
                pixels.swap(rgb);
                comps = 3;
            }
            else if (settings.Format == TextureFormatChoice::RGBA32 && comps == 3)
            {
                std::vector<uint8_t> rgba(pixelCount * 4, 255);
                for (size_t i = 0; i < pixelCount; ++i)
                    for (int k = 0; k < 3; ++k)
                        rgba[i * 4 + (size_t)k] = pixels[i * 3 + (size_t)k];
                pixels.swap(rgba);
                comps = 4;
            }
            internalFormat = comps == 3 ? GL_RGB8 : GL_RGBA8;
            dataFormat = comps == 3 ? GL_RGB : GL_RGBA;
            if (settings.Format == TextureFormatChoice::Automatic && settings.Compression != TextureCompression::None)
            {
                bool hasAlpha = false;
                if (comps == 4)
                    for (size_t i = 0; i < pixelCount && !hasAlpha; ++i)
                        hasAlpha = pixels[i * 4 + 3] != 255;
                // Sürücü yüklerken sıkıştırır; uzantı yoksa sıkıştırılmamış kalır
                if (settings.Compression == TextureCompression::HighQuality && (GLAD_GL_VERSION_4_2 || GLAD_GL_ARB_texture_compression_bptc))
                    internalFormat = GL_COMPRESSED_RGBA_BPTC_UNORM;
                else if (GLAD_GL_EXT_texture_compression_s3tc)
                    internalFormat = hasAlpha ? GL_COMPRESSED_RGBA_S3TC_DXT5_EXT : GL_COMPRESSED_RGB_S3TC_DXT1_EXT;
            }
            return true;
        }

        // Doku akışı: tam boyutlu görüntüden "level" mip seviyesinin boyutuna (w >> level) küçült (renk sRGB doğrusal uzayda)
        void DownscaleLevel(std::vector<uint8_t>& pixels, uint32_t& w, uint32_t& h, int comps, int level, bool linearData)
        {
            if (level <= 0)
                return;
            const uint32_t nw = std::max(1u, w >> level), nh = std::max(1u, h >> level);
            std::vector<uint8_t> scaled((size_t)nw * nh * (size_t)comps);
            const stbir_pixel_layout layout = comps == 4 ? STBIR_RGBA : comps == 3 ? STBIR_RGB : STBIR_1CHANNEL;
            stbir_resize(pixels.data(), (int)w, (int)h, 0, scaled.data(), (int)nw, (int)nh, 0, layout,
                         linearData ? STBIR_TYPE_UINT8 : STBIR_TYPE_UINT8_SRGB, STBIR_EDGE_CLAMP, STBIR_FILTER_BOX);
            pixels.swap(scaled);
            w = nw;
            h = nh;
        }

        // Hedef boyut: MaxSize sınırı (en-boy oranı korunur)
        void TargetSize(int w, int h, const TextureImportSettings& settings, uint32_t& tw, uint32_t& th)
        {
            tw = (uint32_t)w;
            th = (uint32_t)h;
            const uint32_t maxSize = (uint32_t)std::max(32, settings.MaxSize);
            if (std::max(tw, th) > maxSize)
            {
                float k = (float)maxSize / (float)std::max(tw, th);
                tw = std::max(1u, (uint32_t)std::lround(tw * k));
                th = std::max(1u, (uint32_t)std::lround(th * k));
            }
        }
    }

    bool OpenGLTexture2D::DecodeStreamingLevel(const std::string& path, const TextureImportSettings& settings, int level, StreamingPixels& out)
    {
        std::vector<uint8_t> bytes;
        if (!ReadFile(path, bytes))
            return false;
        // Arka plan iş parçacığı: çevirme bayrağı bu iş parçacığına özel (genel bayrağı başka yükleyiciler değiştirir)
        stbi_set_flip_vertically_on_load_thread(1);
        int w = 0, h = 0, channels = 0;
        if (!stbi_info_from_memory(bytes.data(), (int)bytes.size(), &w, &h, &channels))
            return false;
        if (stbi_is_hdr_from_memory(bytes.data(), (int)bytes.size()) || stbi_is_16_bit_from_memory(bytes.data(), (int)bytes.size()))
            return false;
        uint32_t tw = 0, th = 0;
        TargetSize(w, h, settings, tw, th);
        GLenum internalFormat = GL_RGBA8, dataFormat = GL_RGBA;
        if (!DecodeLDR(bytes.data(), bytes.size(), settings, channels, tw, th, out.Pixels, out.Comps, internalFormat, dataFormat))
            return false;
        out.Width = tw;
        out.Height = th;
        DownscaleLevel(out.Pixels, out.Width, out.Height, out.Comps, level, settings.Type == TextureType::NormalMap || out.Comps == 1);
        out.InternalFormat = (uint32_t)internalFormat;
        out.DataFormat = (uint32_t)dataFormat;
        out.Level = level;
        return true;
    }

    // ------------------------------------------------------------------ Yükleme

    bool OpenGLTexture2D::LoadCrunched(const std::vector<uint8_t>& data)
    {
        crnd::crn_texture_info info;
        if (data.empty() || !crnd::crnd_get_texture_info(data.data(), (crnd::uint32)data.size(), &info))
            return false;
        const bool dxt5 = info.m_format == cCRNFmtDXT5, dxt1 = info.m_format == cCRNFmtDXT1;
        if ((!dxt1 && !dxt5) || info.m_faces != 1 || !GLAD_GL_EXT_texture_compression_s3tc)
            return false;
        crnd::crnd_unpack_context ctx = crnd::crnd_unpack_begin(data.data(), (crnd::uint32)data.size());
        if (!ctx)
            return false;
        Release();
        m_Width = m_SourceWidth = info.m_width;
        m_Height = m_SourceHeight = info.m_height;
        m_SourceChannels = dxt5 ? 4 : 3;
        // Diğer yüklemelerle aynı: doğrusal biçim, sRGB çözümü gölgelendiricide
        m_InternalFormat = dxt5 ? GL_COMPRESSED_RGBA_S3TC_DXT5_EXT : GL_COMPRESSED_RGB_S3TC_DXT1_EXT;
        m_DataFormat = dxt5 ? GL_RGBA : GL_RGB;
        m_DataType = GL_UNSIGNED_BYTE;
        m_MipLevels = m_Settings.Mipmaps ? std::max(1u, info.m_levels) : 1u;
        m_Streamed = false;
        m_StreamLevel = 0;
        TextureStreaming::Unregister(this);
        glCreateTextures(GL_TEXTURE_2D, 1, &m_RendererID);
        glTextureStorage2D(m_RendererID, (GLsizei)m_MipLevels, m_InternalFormat, (GLsizei)m_Width, (GLsizei)m_Height);
        const uint32_t blockBytes = dxt5 ? 16u : 8u;
        std::vector<uint8_t> level;
        bool ok = true;
        for (uint32_t l = 0; l < m_MipLevels && ok; ++l)
        {
            const uint32_t lw = std::max(1u, m_Width >> l), lh = std::max(1u, m_Height >> l);
            const uint32_t rowPitch = ((lw + 3) / 4) * blockBytes, size = rowPitch * ((lh + 3) / 4);
            level.resize(size);
            void* dst = level.data();
            ok = crnd::crnd_unpack_level(ctx, &dst, size, rowPitch, l);
            if (ok)
                glCompressedTextureSubImage2D(m_RendererID, (GLint)l, 0, 0, (GLsizei)lw, (GLsizei)lh, m_InternalFormat, (GLsizei)size, level.data());
        }
        crnd::crnd_unpack_end(ctx);
        if (!ok)
        {
            Release();
            return false;
        }
        ApplySampler();
        m_BytesPerPixel = 1;
        UpdateMemory(m_Width, m_Height);
        return true;
    }

    bool OpenGLTexture2D::LoadEncoded(const unsigned char* bytes, size_t size)
    {
        stbi_set_flip_vertically_on_load(1);
        int w = 0, h = 0, channels = 0;
        if (!stbi_info_from_memory(bytes, (int)size, &w, &h, &channels))
        {
            const char* why = stbi_failure_reason();
            CQ_CORE_ERROR("Texture decode failed: {0} ({1})", m_Path.empty() ? std::string("<memory>") : m_Path, why ? why : "unknown");
            return false;
        }
        m_SourceWidth = (uint32_t)w;
        m_SourceHeight = (uint32_t)h;
        m_SourceChannels = (uint32_t)channels;

        uint32_t tw = 0, th = 0;
        TargetSize(w, h, m_Settings, tw, th);
        const bool resize = tw != (uint32_t)w || th != (uint32_t)h;
        // HDR / 16 bit dokular akışa girmez (tam yüklenir)
        m_Streamed = false;
        m_StreamLevel = 0;

        if (stbi_is_hdr_from_memory(bytes, (int)size))
        {
            // HDR (.hdr): 16-bit kayan nokta
            float* data = stbi_loadf_from_memory(bytes, (int)size, &w, &h, &channels, 4);
            if (!data)
                return false;
            std::vector<float> pixels(data, data + (size_t)w * h * 4);
            stbi_image_free(data);
            if (resize)
            {
                std::vector<float> scaled((size_t)tw * th * 4);
                stbir_resize_float_linear(pixels.data(), w, h, 0, scaled.data(), (int)tw, (int)th, 0, STBIR_RGBA);
                pixels.swap(scaled);
            }
            Upload(pixels.data(), tw, th, GL_RGBA16F, GL_RGBA, GL_FLOAT, 8);
            return true;
        }
        if (stbi_is_16_bit_from_memory(bytes, (int)size))
        {
            // 16 bit (yükseklik haritaları): RGBA16 ile tam hassasiyet
            stbi_us* data = stbi_load_16_from_memory(bytes, (int)size, &w, &h, &channels, 4);
            if (!data)
                return false;
            std::vector<uint16_t> pixels(data, data + (size_t)w * h * 4);
            stbi_image_free(data);
            if (resize)
            {
                std::vector<uint16_t> scaled((size_t)tw * th * 4);
                stbir_resize(pixels.data(), w, h, 0, scaled.data(), (int)tw, (int)th, 0, STBIR_RGBA, STBIR_TYPE_UINT16, STBIR_EDGE_CLAMP,
                             STBIR_FILTER_DEFAULT);
                pixels.swap(scaled);
            }
            Upload(pixels.data(), tw, th, GL_RGBA16, GL_RGBA, GL_UNSIGNED_SHORT, 8);
            return true;
        }

        // 8 bit: çözme ve tüm dönüşümler DecodeLDR'de (doku akışının arka plan iş parçacığı da aynı yolu kullanır)
        std::vector<uint8_t> pixels;
        int comps = 4;
        GLenum internalFormat = GL_RGBA8, dataFormat = GL_RGBA;
        if (!DecodeLDR(bytes, size, m_Settings, channels, tw, th, pixels, comps, internalFormat, dataFormat))
            return false;
        // Texture Streaming (Streaming Mipmaps): yalnız izin verilen en kaba mip seviyesi yüklenir, kameraların gerektirdiğini
        // TextureStreaming sonra getirir. Genişlik / yükseklik tam boyut kalır (Unity texture.width gibi)
        const bool streamable = !m_Path.empty() && m_Settings.StreamingMipmaps && m_Settings.Mipmaps;
        const int level = streamable ? TextureStreaming::InitialLevel(tw, th) : 0;
        uint32_t lw = tw, lh = th;
        DownscaleLevel(pixels, lw, lh, comps, level, m_Settings.Type == TextureType::NormalMap || comps == 1);
        Upload(pixels.data(), lw, lh, internalFormat, dataFormat, GL_UNSIGNED_BYTE, (uint32_t)comps);
        m_Width = tw;
        m_Height = th;
        m_Streamed = streamable;
        m_StreamLevel = level;
        if (streamable)
            TextureStreaming::Register(this);
        else
            TextureStreaming::Unregister(this);
        return true;
    }

    void OpenGLTexture2D::Upload(const void* pixels, uint32_t width, uint32_t height, GLenum internalFormat, GLenum dataFormat, GLenum type,
                                 uint32_t bytesPerPixel)
    {
        Release();
        m_Width = width;
        m_Height = height;
        m_InternalFormat = internalFormat;
        m_DataFormat = dataFormat;
        m_DataType = type;
        m_MipLevels = m_Settings.Mipmaps ? MipCount(width, height) : 1;

        glCreateTextures(GL_TEXTURE_2D, 1, &m_RendererID);
        glTextureStorage2D(m_RendererID, (GLsizei)m_MipLevels, internalFormat, (GLsizei)width, (GLsizei)height);
        // Satır hizalaması (RGB8 / tek sayılı genişlikler)
        GLint previousAlign = 4;
        glGetIntegerv(GL_UNPACK_ALIGNMENT, &previousAlign);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTextureSubImage2D(m_RendererID, 0, 0, 0, (GLsizei)width, (GLsizei)height, dataFormat, type, pixels);
        glPixelStorei(GL_UNPACK_ALIGNMENT, previousAlign);
        if (m_MipLevels > 1)
            glGenerateTextureMipmap(m_RendererID);
        ApplySampler();
        m_BytesPerPixel = bytesPerPixel;
        UpdateMemory(width, height);
    }

    void OpenGLTexture2D::UpdateMemory(uint32_t width, uint32_t height)
    {
        uint64_t bytes = (uint64_t)width * height * m_BytesPerPixel;
        if (m_MipLevels > 1)
            bytes = bytes * 4 / 3;   // tam mip zinciri ~ %33 ek
        GLint compressed = GL_FALSE;
        glGetTextureLevelParameteriv(m_RendererID, 0, GL_TEXTURE_COMPRESSED, &compressed);
        if (compressed == GL_TRUE)
        {
            // Sıkıştırılmış: gerçek boyut sürücüden
            bytes = 0;
            for (uint32_t level = 0; level < m_MipLevels; ++level)
            {
                GLint size = 0;
                glGetTextureLevelParameteriv(m_RendererID, (GLint)level, GL_TEXTURE_COMPRESSED_IMAGE_SIZE, &size);
                bytes += (uint64_t)std::max(size, 0);
            }
        }
        TrackMemory(m_MemoryBytes, bytes);
    }

    // ------------------------------------------------------------------ Texture Streaming

    bool OpenGLTexture2D::StreamDropTo(int level)
    {
        // Daha kaba seviye: yeni (küçük) depoya eski dokunun alt mip'leri GPU'da kopyalanır (diskten okuma yok, anında)
        if (!m_Streamed || !m_RendererID || level <= m_StreamLevel)
            return false;
        const int delta = level - m_StreamLevel;
        if ((uint32_t)delta >= m_MipLevels)
            return false;
        const uint32_t nw = std::max(1u, m_Width >> level), nh = std::max(1u, m_Height >> level);
        const uint32_t levels = std::min(MipCount(nw, nh), m_MipLevels - (uint32_t)delta);
        GLuint id = 0;
        glCreateTextures(GL_TEXTURE_2D, 1, &id);
        glTextureStorage2D(id, (GLsizei)levels, m_InternalFormat, (GLsizei)nw, (GLsizei)nh);
        for (uint32_t k = 0; k < levels; ++k)
        {
            const GLsizei lw = (GLsizei)std::max(1u, nw >> k), lh = (GLsizei)std::max(1u, nh >> k);
            glCopyImageSubData(m_RendererID, GL_TEXTURE_2D, (GLint)(k + (uint32_t)delta), 0, 0, 0, id, GL_TEXTURE_2D, (GLint)k, 0, 0, 0, lw, lh, 1);
        }
        glDeleteTextures(1, &m_RendererID);
        m_RendererID = id;
        m_MipLevels = levels;
        m_StreamLevel = level;
        ApplySampler();
        UpdateMemory(nw, nh);
        return true;
    }

    void OpenGLTexture2D::StreamUpload(const StreamingPixels& pixels)
    {
        // Daha ayrıntılı seviye: arka planda çözülmüş görüntü yüklenir; genişlik / yükseklik tam boyut kalır
        if (!m_Streamed || pixels.Pixels.empty())
            return;
        const uint32_t fullW = m_Width, fullH = m_Height;
        Upload(pixels.Pixels.data(), pixels.Width, pixels.Height, (GLenum)pixels.InternalFormat, (GLenum)pixels.DataFormat, GL_UNSIGNED_BYTE,
               (uint32_t)pixels.Comps);
        m_Width = fullW;
        m_Height = fullH;
        m_StreamLevel = pixels.Level;
    }

    int OpenGLTexture2D::GetStreamingMaxLevel() const
    {
        return m_Streamed ? (int)MipCount(m_Width, m_Height) - 1 : 0;
    }

    void OpenGLTexture2D::ApplySampler()
    {
        if (!m_RendererID)
            return;
        GLenum minFilter = GL_LINEAR, magFilter = GL_LINEAR;
        switch (m_Settings.Filter)
        {
            case TextureFilter::Point:
                magFilter = GL_NEAREST;
                minFilter = m_MipLevels > 1 ? GL_NEAREST_MIPMAP_NEAREST : GL_NEAREST;
                break;
            case TextureFilter::Bilinear:
                minFilter = m_MipLevels > 1 ? GL_LINEAR_MIPMAP_NEAREST : GL_LINEAR;
                break;
            default:
                minFilter = m_MipLevels > 1 ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR;
                break;
        }
        glTextureParameteri(m_RendererID, GL_TEXTURE_MIN_FILTER, (GLint)minFilter);
        glTextureParameteri(m_RendererID, GL_TEXTURE_MAG_FILTER, (GLint)magFilter);
        GLenum wrap = m_Settings.Wrap == TextureWrap::Clamp ? GL_CLAMP_TO_EDGE : m_Settings.Wrap == TextureWrap::Mirror ? GL_MIRRORED_REPEAT : GL_REPEAT;
        glTextureParameteri(m_RendererID, GL_TEXTURE_WRAP_S, (GLint)wrap);
        glTextureParameteri(m_RendererID, GL_TEXTURE_WRAP_T, (GLint)wrap);
        // Eşyönsüz süzme: eğik yüzeylerde netlik (yalnızca mip zincirli, nokta olmayan süzmede)
        if (m_MipLevels > 1 && m_Settings.Filter != TextureFilter::Point && m_Settings.Anisotropy > 1 &&
            (GLAD_GL_VERSION_4_6 || GLAD_GL_ARB_texture_filter_anisotropic || GLAD_GL_EXT_texture_filter_anisotropic))
        {
            GLfloat maxAniso = 1.0f;
            glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY, &maxAniso);
            // Kalite düzeyinin eşyönsüz süzme üst sınırı
            const GLfloat qualityAniso = (GLfloat)std::max(QualitySettings::GetPreset().AnisotropicFiltering, 1);
            glTextureParameterf(m_RendererID, GL_TEXTURE_MAX_ANISOTROPY, std::min(std::min((GLfloat)m_Settings.Anisotropy, maxAniso), qualityAniso));
        }
    }

    void OpenGLTexture2D::Release()
    {
        if (m_RendererID)
        {
            glDeleteTextures(1, &m_RendererID);
            m_RendererID = 0;
        }
    }

    std::string OpenGLTexture2D::GetFormatName() const
    {
        switch (m_InternalFormat)
        {
            case GL_RGBA8: return "RGBA8";
            case GL_RGB8: return "RGB8";
            case GL_RGBA16: return "RGBA16";
            case GL_RGBA16F: return "RGBA16F (HDR)";
            case GL_R8: return "R8";
            case GL_COMPRESSED_RGB_S3TC_DXT1_EXT: return "RGB Compressed DXT1|BC1";
            case GL_COMPRESSED_RGBA_S3TC_DXT5_EXT: return "RGBA Compressed DXT5|BC3";
            case GL_COMPRESSED_RGBA_BPTC_UNORM: return "RGBA Compressed BC7";
            default: return "Unknown";
        }
    }

    void OpenGLTexture2D::SetMissing()
    {
        m_Streamed = false;
        m_StreamLevel = 0;
        TextureStreaming::Unregister(this);
        const uint32_t clear = 0u;
        const bool mips = m_Settings.Mipmaps;
        m_Settings.Mipmaps = false;
        Upload(&clear, 1, 1, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, 4);
        m_Settings.Mipmaps = mips;
        CQ_CORE_WARN("[Texture] Source file missing: {0}", m_Path);
    }

    bool OpenGLTexture2D::Reload()
    {
        if (m_Path.empty())
            return false;
        TextureImportSettings::Load(m_Path, m_Settings);
        m_Settings = m_Settings.ForPlatform(TextureImportSettings::CurrentPlatform());
        std::vector<uint8_t> bytes;
        if (!ReadFile(m_Path, bytes))
        {
            CQ_CORE_ERROR("Texture reload failed: {0}", m_Path);
            return false;
        }
        bool ok = LoadEncoded(bytes.data(), bytes.size());
        if (ok)
            CQ_CORE_INFO("[Texture] Reimported {0} ({1}x{2}, {3} mips, {4})", std::filesystem::path(m_Path).filename().string(), m_Width, m_Height,
                         m_MipLevels, TextureImportSettings::FilterName(m_Settings.Filter));
        return ok;
    }

    bool OpenGLTexture2D::ReloadFrom(const std::string& file)
    {
        if (file.empty())
            return false;
        // Karşılık dosyasının kendi içe aktarma ayarları; doku akışı kapalı (akış, mip seviyelerini GetPath'teki asıl dosyadan okurdu)
        TextureImportSettings settings;
        TextureImportSettings::Load(file, settings);
        settings = settings.ForPlatform(TextureImportSettings::CurrentPlatform());
        settings.StreamingMipmaps = false;
        std::vector<uint8_t> bytes;
        if (!ReadFile(file, bytes))
        {
            CQ_CORE_ERROR("Texture remap failed: {0}", file);
            return false;
        }
        const TextureImportSettings previous = m_Settings;
        m_Settings = settings;
        if (!LoadEncoded(bytes.data(), bytes.size()))
        {
            m_Settings = previous;
            return false;
        }
        return true;
    }

    // ------------------------------------------------------------------ Kurucular

    OpenGLTexture2D::OpenGLTexture2D(uint32_t width, uint32_t height)
        : m_Width(width > 0 ? width : 1), m_Height(height > 0 ? height : 1)   // GL 0 boyutlu doku kabul etmez
    {
        // Çalışma zamanında doldurulan doku (beyaz piksel, render hedefi önizlemeleri): mip yok, kenara kıstır
        m_Settings = TextureImportSettings::DefaultsFor(TextureType::UI);
        m_Settings.Filter = TextureFilter::Bilinear;
        m_SourceWidth = m_Width;
        m_SourceHeight = m_Height;
        m_InternalFormat = GL_RGBA8;
        m_DataFormat = GL_RGBA;
        m_MipLevels = 1;
        glCreateTextures(GL_TEXTURE_2D, 1, &m_RendererID);
        glTextureStorage2D(m_RendererID, 1, m_InternalFormat, m_Width, m_Height);
        ApplySampler();
        TrackMemory(m_MemoryBytes, (uint64_t)m_Width * m_Height * 4);
    }

    OpenGLTexture2D::OpenGLTexture2D(const std::string& path)
    {
        namespace fs = std::filesystem;
        std::string resolved = FileSystem::ResolveResourcePath(path);
        fs::path filePath = fs::path(resolved).lexically_normal();
        m_Path = filePath.string();

        std::error_code ec;
        // Crunch'lanmış doku (oyun derlemesi): "<görsel>.crn" önce; özgün görsel Read/Write kapalıysa derlemede yoktur
        {
            fs::path crn = filePath;
            crn += ".crn";
            if (fs::is_regular_file(crn, ec))
            {
                if (fs::path canonical = fs::weakly_canonical(filePath, ec); !ec)
                    m_Path = canonical.string();
                TextureImportSettings::Load(m_Path, m_Settings);
                m_Settings = m_Settings.ForPlatform(TextureImportSettings::CurrentPlatform());
                std::vector<uint8_t> crnBytes;
                if (ReadFile(crn.string(), crnBytes) && LoadCrunched(crnBytes))
                    return;
                CQ_CORE_WARN("Crunched texture could not be loaded, using the image: {0}", crn.string());
            }
        }
        if (!fs::is_regular_file(filePath, ec))
        {
            CQ_CORE_ERROR("Texture file not found: {0}", m_Path);
            return;
        }
        fs::path canonical = fs::weakly_canonical(filePath, ec);
        if (!ec)
            m_Path = canonical.string();

        TextureImportSettings::Load(m_Path, m_Settings);
        m_Settings = m_Settings.ForPlatform(TextureImportSettings::CurrentPlatform());
        std::vector<uint8_t> bytes;
        if (!ReadFile(m_Path, bytes))
        {
            CQ_CORE_ERROR("Texture file could not be read: {0}", m_Path);
            return;
        }
        LoadEncoded(bytes.data(), bytes.size());
    }

    OpenGLTexture2D::OpenGLTexture2D(const unsigned char* imageBytes, uint32_t byteLength)
    {
        // Model içine gömülü doku: varsayılan ayarlar (mip zinciri, üç doğrusal)
        if (!LoadEncoded(imageBytes, byteLength))
            CQ_CORE_ERROR("Failed to decode texture from embedded memory ({0} bytes)", byteLength);
    }

    OpenGLTexture2D::OpenGLTexture2D(uint32_t width, uint32_t height, const unsigned char* bgraPixels)
    {
        m_SourceWidth = width;
        m_SourceHeight = height;
        Upload(bgraPixels, width, height, GL_RGBA8, GL_BGRA, GL_UNSIGNED_BYTE, 4);
    }

    OpenGLTexture2D::~OpenGLTexture2D()
    {
        TextureStreaming::Unregister(this);
        Release();
        TrackMemory(m_MemoryBytes, 0);
    }

    void OpenGLTexture2D::SetData(void* data, [[maybe_unused]] uint32_t size)
    {
        if (!m_RendererID)
            return;
        GLint previousAlign = 4;
        glGetIntegerv(GL_UNPACK_ALIGNMENT, &previousAlign);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTextureSubImage2D(m_RendererID, 0, 0, 0, m_Width, m_Height, m_DataFormat, m_DataType, data);
        glPixelStorei(GL_UNPACK_ALIGNMENT, previousAlign);
        if (m_MipLevels > 1)
            glGenerateTextureMipmap(m_RendererID);
    }

    void OpenGLTexture2D::Bind(uint32_t slot) const
    {
        if (!m_RendererID)
            return;
        glBindTextureUnit(slot, m_RendererID);
    }
}
