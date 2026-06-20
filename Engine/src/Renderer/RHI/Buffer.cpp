#include "Buffer.h"
#include "Core/Base/Base.h"
#include "Renderer/Renderer.h"
#include "Renderer/GraphicsAPIs/OpenGL/OpenGLBuffer.h"

namespace Conqueror
{
    std::shared_ptr<VertexBuffer> VertexBuffer::Create(uint32_t size)
    {
        switch (Renderer::GetAPI())
        {
            case RendererAPI::API::None:    CQ_CORE_ASSERT(false, "RendererAPI::None is currently not supported!"); return nullptr;
            case RendererAPI::API::OpenGL:  return std::make_shared<OpenGLVertexBuffer>(size);
        }

        CQ_CORE_ASSERT(false, "Unknown RendererAPI!");
        return nullptr;
    }

    std::shared_ptr<VertexBuffer> VertexBuffer::Create(float* vertices, uint32_t size)
    {
        switch (Renderer::GetAPI())
        {
            case RendererAPI::API::None:    CQ_CORE_ASSERT(false, "RendererAPI::None is currently not supported!"); return nullptr;
            case RendererAPI::API::OpenGL:  return std::make_shared<OpenGLVertexBuffer>(vertices, size);
        }

        CQ_CORE_ASSERT(false, "Unknown RendererAPI!");
        return nullptr;
    }

    std::shared_ptr<IndexBuffer> IndexBuffer::Create(uint32_t* indices, uint32_t count)
    {
        switch (Renderer::GetAPI())
        {
            case RendererAPI::API::None:    CQ_CORE_ASSERT(false, "RendererAPI::None is currently not supported!"); return nullptr;
            case RendererAPI::API::OpenGL:  return std::make_shared<OpenGLIndexBuffer>(indices, count);
        }

        CQ_CORE_ASSERT(false, "Unknown RendererAPI!");
        return nullptr;
    }
}
