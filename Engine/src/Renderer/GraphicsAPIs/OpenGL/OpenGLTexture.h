#pragma once

#include "Renderer/RHI/Texture.h"

#include <glad/glad.h>

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

        /// stbi_load_from_memory ile yüklenen sıkıştırılmış görüntü (png/jpg vb.)
        OpenGLTexture2D(const unsigned char* imageBytes, uint32_t byteLength);
        /// Assimp sıkıştırılmamış gömülü; pcData BGRA (aiTexel), veri kopyalanır
        OpenGLTexture2D(uint32_t width, uint32_t height, const unsigned char* bgraPixels);

    private:
        void InitFromDecodedStb(int width, int height, int channels, unsigned char* data);

        std::string m_Path;
        uint32_t m_Width = 0;
        uint32_t m_Height = 0;
        uint32_t m_RendererID = 0;
        GLenum m_InternalFormat = 0;
        GLenum m_DataFormat = 0;
    };
}
