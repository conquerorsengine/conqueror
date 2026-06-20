#include "RendererAPI.h"
#include "Core/Base/Base.h"
#include "Renderer/GraphicsAPIs/OpenGL/OpenGLRendererAPI.h"

namespace Conqueror
{
    RendererAPI::API RendererAPI::s_API = RendererAPI::API::OpenGL;

    std::unique_ptr<RendererAPI> RendererAPI::Create()
    {
        switch (s_API)
        {
            case RendererAPI::API::None:    CQ_CORE_ASSERT(false, "RendererAPI::None is currently not supported!"); return nullptr;
            case RendererAPI::API::OpenGL:  return std::make_unique<OpenGLRendererAPI>();
        }

        CQ_CORE_ASSERT(false, "Unknown RendererAPI!");
        return nullptr;
    }
}
