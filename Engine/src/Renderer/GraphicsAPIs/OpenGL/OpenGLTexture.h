#pragma once

#include "Renderer/RHI/Texture.h"

#include <glad/glad.h>

#include <cstdint>
#include <vector>

namespace Conqueror
{
    class OpenGLTexture2D : public Texture2D
    {
    public:
        OpenGLTexture2D(uint32_t width, uint32_t height);
        OpenGLTexture2D(const std::string& path);
        virtual ~OpenGLTexture2D();

        uint32_t GetWidth() const override { return m_Width; }
        uint32_t GetHeight() const override { return m_Height; }
        uint32_t GetRendererID() const override { return m_RendererID; }
        const std::string& GetPath() const override { return m_Path; }

        void SetData(void* data, uint32_t size) override;

        void Bind(uint32_t slot = 0) const override;

        bool operator==(const Texture& other) const override
        {
            return m_RendererID == ((OpenGLTexture2D&)other).m_RendererID;
        }

        uint32_t GetMipLevels() const override { return m_MipLevels; }
        uint64_t GetMemoryBytes() const override { return m_MemoryBytes; }
        std::string GetFormatName() const override;
        uint32_t GetSourceWidth() const override { return m_SourceWidth; }
        uint32_t GetSourceHeight() const override { return m_SourceHeight; }
        uint32_t GetSourceChannels() const override { return m_SourceChannels; }
        const TextureImportSettings& GetImportSettings() const override { return m_Settings; }
        bool Reload() override;
        void SetMissing() override;

        // Texture Streaming (Unity Mipmap Streaming). GPU'daki en ayrıntılı seviye (0 = tam boyut)
        bool IsStreamed() const override { return m_Streamed; }
        int GetStreamingLevel() const override { return m_StreamLevel; }
        int GetStreamingMaxLevel() const override;
        // Daha kaba seviyeye in: GPU'da kopya, anında
        bool StreamDropTo(int level) override;
        // Arka planda çözülmüş daha ayrıntılı seviye (TextureStreaming ana iş parçacığında çağırır)
        struct StreamingPixels
        {
            std::vector<uint8_t> Pixels;
            uint32_t Width = 0, Height = 0;
            int Comps = 4;
            uint32_t InternalFormat = 0, DataFormat = 0;
            int Level = 0;
        };
        void StreamUpload(const StreamingPixels& pixels);
        // Dosyadan "level" seviyesinin pikselleri (yalnız CPU: arka plan iş parçacığında çalışır)
        static bool DecodeStreamingLevel(const std::string& path, const TextureImportSettings& settings, int level, StreamingPixels& out);

        /// stbi_load_from_memory ile yüklenen sıkıştırılmış görüntü (png/jpg vb.)
        OpenGLTexture2D(const unsigned char* imageBytes, uint32_t byteLength);
        /// Assimp sıkıştırılmamış gömülü; pcData BGRA (aiTexel), veri kopyalanır
        OpenGLTexture2D(uint32_t width, uint32_t height, const unsigned char* bgraPixels);

    private:
        // Kodlanmış görüntüyü çöz + ayarları uygula (boyut sınırı, normal üretimi) + GPU'ya yükle
        bool LoadEncoded(const unsigned char* bytes, size_t size);
        void Upload(const void* pixels, uint32_t width, uint32_t height, GLenum internalFormat, GLenum dataFormat, GLenum type,
                    uint32_t bytesPerPixel);
        void ApplySampler();
        void Release();
        void UpdateMemory(uint32_t width, uint32_t height);

        std::string m_Path;
        uint32_t m_Width = 0;
        uint32_t m_Height = 0;
        uint32_t m_RendererID = 0;
        GLenum m_InternalFormat = 0;
        GLenum m_DataFormat = 0;
        GLenum m_DataType = GL_UNSIGNED_BYTE;
        uint32_t m_MipLevels = 1;
        uint64_t m_MemoryBytes = 0;
        uint32_t m_SourceWidth = 0, m_SourceHeight = 0, m_SourceChannels = 4;
        uint32_t m_BytesPerPixel = 4;
        bool m_Streamed = false;
        int m_StreamLevel = 0;
        TextureImportSettings m_Settings;
    };
}
