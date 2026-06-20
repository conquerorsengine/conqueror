#include "Texture.h"
#include "Renderer/Renderer.h"
#include "Renderer/GraphicsAPIs/OpenGL/OpenGLTexture.h"

namespace Conqueror
{
    std::shared_ptr<Texture2D> Texture2D::Create(uint32_t width, uint32_t height)
    {
        switch (Renderer::GetAPI())
        {
            case RendererAPI::API::None:    CQ_CORE_ASSERT(false, "RendererAPI::None is currently not supported!"); return nullptr;
            case RendererAPI::API::OpenGL:  return std::make_shared<OpenGLTexture2D>(width, height);
        }

        CQ_CORE_ASSERT(false, "Unknown RendererAPI!");
        return nullptr;
    }

    std::shared_ptr<Texture2D> Texture2D::Create(const std::string& path)
    {
        switch (Renderer::GetAPI())
        {
            case RendererAPI::API::None:    CQ_CORE_ASSERT(false, "RendererAPI::None is currently not supported!"); return nullptr;
            case RendererAPI::API::OpenGL:
            {
                auto tex = std::make_shared<OpenGLTexture2D>(path);
                if (!tex->GetRendererID())
                    return nullptr;
                return tex;
            }
        }

        CQ_CORE_ASSERT(false, "Unknown RendererAPI!");
        return nullptr;
    }

    std::shared_ptr<Texture2D> Texture2D::CreateFromCompressedImageMemory(const unsigned char* data, uint32_t byteSize)
    {
        switch (Renderer::GetAPI())
        {
            case RendererAPI::API::None:    CQ_CORE_ASSERT(false, "RendererAPI::None is currently not supported!"); return nullptr;
            case RendererAPI::API::OpenGL:
            {
                auto tex = std::make_shared<OpenGLTexture2D>(data, byteSize);
                if (!tex->GetRendererID())
                    return nullptr;
                return tex;
            }
        }

        CQ_CORE_ASSERT(false, "Unknown RendererAPI!");
        return nullptr;
    }

    std::shared_ptr<Texture2D> Texture2D::CreateFromRawBGRA(uint32_t width, uint32_t height, const unsigned char* bgraPixels)
    {
        switch (Renderer::GetAPI())
        {
            case RendererAPI::API::None:    CQ_CORE_ASSERT(false, "RendererAPI::None is currently not supported!"); return nullptr;
            case RendererAPI::API::OpenGL:
            {
                if (!width || !height || !bgraPixels)
                    return nullptr;

                auto tex = std::make_shared<OpenGLTexture2D>(width, height, bgraPixels);
                if (!tex->GetRendererID())
                    return nullptr;
                return tex;
            }
        }

        CQ_CORE_ASSERT(false, "Unknown RendererAPI!");
        return nullptr;
    }
}
