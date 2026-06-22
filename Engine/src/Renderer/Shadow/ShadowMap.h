#pragma once

#include "Core/Base/Base.h"
#include "Renderer/RHI/Framebuffer.h"
#include "Renderer/RHI/Texture.h"

#include <glm/glm.hpp>
#include <memory>

namespace Conqueror
{
    class CQ_API ShadowMap
    {
    public:
        ShadowMap(uint32_t width = 2048, uint32_t height = 2048);
        ~ShadowMap() = default;

        void Bind() const;
        void Unbind() const;

        uint32_t GetWidth() const { return m_Width; }
        uint32_t GetHeight() const { return m_Height; }
        uint32_t GetDepthTextureID() const;

        std::shared_ptr<Framebuffer> GetFramebuffer() const { return m_Framebuffer; }

        void Resize(uint32_t width, uint32_t height);

        static std::shared_ptr<ShadowMap> Create(uint32_t width = 2048, uint32_t height = 2048);

    private:
        uint32_t m_Width;
        uint32_t m_Height;
        std::shared_ptr<Framebuffer> m_Framebuffer;
    };
}
