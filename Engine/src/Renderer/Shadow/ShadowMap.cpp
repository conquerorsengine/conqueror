#include "ShadowMap.h"

namespace Conqueror
{
    ShadowMap::ShadowMap(uint32_t width, uint32_t height)
        : m_Width(width), m_Height(height)
    {
        FramebufferSpecification spec;
        spec.Width = width;
        spec.Height = height;
        spec.Attachments = { FramebufferTextureFormat::Depth };
        spec.SwapChainTarget = false;

        m_Framebuffer = Framebuffer::Create(spec);
    }

    void ShadowMap::Bind() const
    {
        m_Framebuffer->Bind();
    }

    void ShadowMap::Unbind() const
    {
        m_Framebuffer->Unbind();
    }

    uint32_t ShadowMap::GetDepthTextureID() const
    {
        return m_Framebuffer->GetDepthAttachmentRendererID();
    }

    void ShadowMap::Resize(uint32_t width, uint32_t height)
    {
        if (width == m_Width && height == m_Height)
            return;

        m_Width = width;
        m_Height = height;
        m_Framebuffer->Resize(width, height);
    }

    std::shared_ptr<ShadowMap> ShadowMap::Create(uint32_t width, uint32_t height)
    {
        return std::make_shared<ShadowMap>(width, height);
    }
}
