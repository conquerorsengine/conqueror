#include "GraphicsContext.h"
#include "Core/Base/Base.h"
#include "Renderer/GraphicsAPIs/OpenGL/OpenGLContext.h"
#include "Renderer/Renderer.h"

namespace Conqueror
{
    GraphicsContext* GraphicsContext::Create(void* window)
    {
        switch (Renderer::GetAPI())
        {
            case RendererAPI::API::None:    CQ_CORE_ASSERT(false, "RendererAPI::None is currently not supported!"); return nullptr;
            case RendererAPI::API::OpenGL:  return new OpenGLContext(static_cast<GLFWwindow*>(window));
        }

        CQ_CORE_ASSERT(false, "Unknown RendererAPI!");
        return nullptr;
    }
}
