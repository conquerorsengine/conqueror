#include "Cubemap.h"
#include "Renderer/Renderer.h"
#include "Renderer/GraphicsAPIs/OpenGL/OpenGLCubemap.h"

namespace Conqueror
{
    std::shared_ptr<Cubemap> Cubemap::CreateFromEquirectangular(const std::string& path, uint32_t resolution)
    {
        switch (Renderer::GetAPI())
        {
            case RendererAPI::API::None:
                CQ_CORE_ASSERT(false, "RendererAPI::None is not supported!");
                return nullptr;
            case RendererAPI::API::OpenGL:
                return std::make_shared<OpenGLCubemap>(path, resolution);
        }

        CQ_CORE_ASSERT(false, "Unknown RendererAPI!");
        return nullptr;
    }

    std::shared_ptr<Cubemap> Cubemap::CreateFromFaces(const std::string faces[6])
    {
        switch (Renderer::GetAPI())
        {
            case RendererAPI::API::None:
                CQ_CORE_ASSERT(false, "RendererAPI::None is not supported!");
                return nullptr;
            case RendererAPI::API::OpenGL:
                return std::make_shared<OpenGLCubemap>(faces);
        }

        CQ_CORE_ASSERT(false, "Unknown RendererAPI!");
        return nullptr;
    }
}
