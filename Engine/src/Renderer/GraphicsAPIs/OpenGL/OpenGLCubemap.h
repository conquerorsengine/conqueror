#pragma once

#include "Renderer/RHI/Cubemap.h"

namespace Conqueror
{
    class OpenGLCubemap : public Cubemap
    {
    public:
        OpenGLCubemap(const std::string& equirectangularPath, uint32_t resolution);
        OpenGLCubemap(const std::string faces[6]);
        virtual ~OpenGLCubemap();

        void Bind(uint32_t slot = 0) const override;
        void Unbind() const override;

        uint32_t GetRendererID() const override { return m_RendererID; }
        uint32_t GetWidth() const override { return m_Width; }
        uint32_t GetHeight() const override { return m_Height; }
        const std::string& GetPath() const override { return m_Path; }

    private:
        void ConvertEquirectangularToCubemap(const std::string& path, uint32_t resolution);
        void LoadFromFaces(const std::string faces[6]);

    private:
        uint32_t m_RendererID = 0;
        uint32_t m_Width = 0;
        uint32_t m_Height = 0;
        std::string m_Path;
    };
}
