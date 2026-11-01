#include "GraphicsContext.h"
#include "Core/Base/Base.h"
#include "Renderer/GraphicsAPIs/OpenGL/OpenGLContext.h"
#include "Renderer/Renderer.h"
#ifdef CQ_VULKAN_ENABLED
    #include "Renderer/GraphicsAPIs/Vulkan/VulkanContext.h"
#endif
#ifdef CQ_METAL_ENABLED
    #include "Renderer/GraphicsAPIs/Metal/MetalContext.h"
#endif
#ifdef CQ_D3D12_ENABLED
    #include "Renderer/GraphicsAPIs/DirectX/D3D12/D3D12Context.h"
#endif
#ifdef CQ_D3D11_ENABLED
    #include "Renderer/GraphicsAPIs/DirectX/D3D11/D3D11Context.h"
#endif

namespace Conqueror
{
    GraphicsContext* GraphicsContext::Create(void* window)
    {
        switch (Renderer::GetAPI())
        {
            case RendererAPI::API::None:    CQ_CORE_ASSERT(false, "RendererAPI::None is currently not supported!"); return nullptr;
            case RendererAPI::API::OpenGL:
            case RendererAPI::API::Vulkan:
#ifdef CQ_VULKAN_ENABLED
                if (RendererAPI::GetBackend() == RendererAPI::Backend::Vulkan)
                    return new VulkanContext(static_cast<GLFWwindow*>(window));
#endif
#ifdef CQ_METAL_ENABLED
                if (RendererAPI::GetBackend() == RendererAPI::Backend::Metal)
                    return new MetalContext(static_cast<GLFWwindow*>(window));
#endif
#ifdef CQ_D3D12_ENABLED
                if (RendererAPI::GetBackend() == RendererAPI::Backend::Direct3D12)
                    return new D3D12Context(static_cast<GLFWwindow*>(window));
#endif
#ifdef CQ_D3D11_ENABLED
                if (RendererAPI::GetBackend() == RendererAPI::Backend::Direct3D11)
                    return new D3D11Context(static_cast<GLFWwindow*>(window));
#endif
                return new OpenGLContext(static_cast<GLFWwindow*>(window));
        }

        CQ_CORE_ASSERT(false, "Unknown RendererAPI!");
        return nullptr;
    }
}
