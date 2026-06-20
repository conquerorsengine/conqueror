#include "OpenGLTexture.h"
#include "Renderer/Renderer.h"
#include "Core/Logging/Log.h"

#include <stb_image.h>

#include <filesystem>
#include <fstream>
#include <vector>

namespace Conqueror
{
    void OpenGLTexture2D::InitFromDecodedStb(int width, int height, int channels, unsigned char* data)
    {
        if (!data)
            return;

        if (channels != 3 && channels != 4)
        {
            stbi_image_free(data);
            CQ_CORE_ERROR("Texture format not supported (channels={0})", channels);
            return;
        }

        m_Width = static_cast<uint32_t>(width);
        m_Height = static_cast<uint32_t>(height);

        GLenum internalFormat = channels == 4 ? GL_RGBA8 : GL_RGB8;
        GLenum dataFormat = channels == 4 ? GL_RGBA : GL_RGB;
        m_InternalFormat = internalFormat;
        m_DataFormat = dataFormat;

        glCreateTextures(GL_TEXTURE_2D, 1, &m_RendererID);
        glTextureStorage2D(m_RendererID, 1, internalFormat, m_Width, m_Height);

        glTextureParameteri(m_RendererID, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTextureParameteri(m_RendererID, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        glTextureParameteri(m_RendererID, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTextureParameteri(m_RendererID, GL_TEXTURE_WRAP_T, GL_REPEAT);

        glTextureSubImage2D(m_RendererID, 0, 0, 0, m_Width, m_Height, dataFormat, GL_UNSIGNED_BYTE, data);

        stbi_image_free(data);

        float memoryMB = static_cast<float>(m_Width * m_Height * channels) / (1024.0f * 1024.0f);
        Renderer::GetStats().TextureMemory += memoryMB;
    }

    OpenGLTexture2D::OpenGLTexture2D(uint32_t width, uint32_t height)
        : m_Width(width), m_Height(height)
    {
        m_InternalFormat = GL_RGBA8;
        m_DataFormat = GL_RGBA;

        glCreateTextures(GL_TEXTURE_2D, 1, &m_RendererID);
        glTextureStorage2D(m_RendererID, 1, m_InternalFormat, m_Width, m_Height);

        glTextureParameteri(m_RendererID, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTextureParameteri(m_RendererID, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        glTextureParameteri(m_RendererID, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTextureParameteri(m_RendererID, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        uint32_t channels = 4;
        float memoryMB = static_cast<float>(m_Width * m_Height * channels) / (1024.0f * 1024.0f);
        Renderer::GetStats().TextureMemory += memoryMB;
    }

    OpenGLTexture2D::OpenGLTexture2D(const std::string& path)
    {
        namespace fs = std::filesystem;

        fs::path filePath = fs::path(path).lexically_normal();
        m_Path = filePath.string();

        std::error_code ec;
        if (!fs::is_regular_file(filePath, ec))
        {
            CQ_CORE_ERROR("Texture file not found: {0}", m_Path);
            return;
        }

        fs::path canonical = fs::weakly_canonical(filePath, ec);
        if (!ec)
        {
            filePath = canonical;
            m_Path = filePath.string();
        }

        std::ifstream stream(filePath, std::ios::binary | std::ios::ate);
        if (!stream)
        {
            CQ_CORE_ERROR("Texture file could not be opened: {0}", m_Path);
            return;
        }

        const std::streamsize rawSize = stream.tellg();
        if (rawSize <= 0)
        {
            CQ_CORE_ERROR("Texture file is empty: {0}", m_Path);
            return;
        }

        stream.seekg(0, std::ios::beg);
        std::vector<uint8_t> bytes(static_cast<size_t>(rawSize));
        if (!stream.read(reinterpret_cast<char*>(bytes.data()), rawSize))
        {
            CQ_CORE_ERROR("Texture file read failed: {0}", m_Path);
            return;
        }

        int width = 0, height = 0, channels = 0;
        stbi_set_flip_vertically_on_load(1);
        stbi_uc* data = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &width, &height, &channels, 0);

        if (!data)
        {
            const char* why = stbi_failure_reason();
            CQ_CORE_ERROR("Texture decode failed: {0} ({1})", m_Path, why ? why : "unknown");
            return;
        }

        InitFromDecodedStb(width, height, channels, data);
    }

    OpenGLTexture2D::OpenGLTexture2D(const unsigned char* imageBytes, uint32_t byteLength)
    {
        int width = 0, height = 0, channels = 0;
        stbi_set_flip_vertically_on_load(1);
        stbi_uc* data = stbi_load_from_memory(imageBytes, static_cast<int>(byteLength), &width, &height, &channels, 0);
        if (!data)
        {
            CQ_CORE_ERROR("Failed to decode texture from embedded memory ({0} bytes)", byteLength);
            return;
        }

        InitFromDecodedStb(width, height, channels, data);
    }

    OpenGLTexture2D::OpenGLTexture2D(uint32_t width, uint32_t height, const unsigned char* bgraPixels)
        : m_Width(width), m_Height(height)
    {
        m_InternalFormat = GL_RGBA8;
        m_DataFormat = GL_BGRA;

        glCreateTextures(GL_TEXTURE_2D, 1, &m_RendererID);
        glTextureStorage2D(m_RendererID, 1, m_InternalFormat, m_Width, m_Height);

        glTextureParameteri(m_RendererID, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTextureParameteri(m_RendererID, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        glTextureParameteri(m_RendererID, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTextureParameteri(m_RendererID, GL_TEXTURE_WRAP_T, GL_REPEAT);

        glTextureSubImage2D(m_RendererID, 0, 0, 0, m_Width, m_Height, GL_BGRA, GL_UNSIGNED_BYTE, bgraPixels);

        float memoryMB = static_cast<float>(m_Width * m_Height * 4) / (1024.0f * 1024.0f);
        Renderer::GetStats().TextureMemory += memoryMB;
    }

    OpenGLTexture2D::~OpenGLTexture2D()
    {
        if (m_RendererID)
            glDeleteTextures(1, &m_RendererID);
    }

    void OpenGLTexture2D::SetData(void* data, [[maybe_unused]] uint32_t size)
    {
        if (!m_RendererID)
            return;

        [[maybe_unused]] uint32_t bpp = m_DataFormat == GL_RGBA ? 4 : 3;
        glTextureSubImage2D(m_RendererID, 0, 0, 0, m_Width, m_Height, m_DataFormat, GL_UNSIGNED_BYTE, data);
    }

    void OpenGLTexture2D::Bind(uint32_t slot) const
    {
        if (!m_RendererID)
            return;

        glBindTextureUnit(slot, m_RendererID);
    }
}
