#pragma once

#include "Renderer/RHI/GraphicsContext.h"

struct GLFWwindow;

namespace Conqueror
{
    class OpenGLContext : public GraphicsContext
    {
    public:
        OpenGLContext(GLFWwindow* windowHandle);

        virtual bool Init() override;
        virtual void SwapBuffers() override;
        virtual const char* GetName() const override { return "OpenGL"; }

    private:
        GLFWwindow* m_WindowHandle;
    };
}
