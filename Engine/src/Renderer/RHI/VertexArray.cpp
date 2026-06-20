#include "VertexArray.h"
#include "Core/Base/Base.h"
#include "Renderer/Renderer.h"
#include "Renderer/GraphicsAPIs/OpenGL/OpenGLVertexArray.h"

namespace Conqueror
{
    std::shared_ptr<VertexArray> VertexArray::Create()
    {
        switch (Renderer::GetAPI())
        {
            case RendererAPI::API::None:    CQ_CORE_ASSERT(false, "RendererAPI::None is currently not supported!"); return nullptr;
            case RendererAPI::API::OpenGL:  return std::make_shared<OpenGLVertexArray>();
        }

        CQ_CORE_ASSERT(false, "Unknown RendererAPI!");
        return nullptr;
    }
}
