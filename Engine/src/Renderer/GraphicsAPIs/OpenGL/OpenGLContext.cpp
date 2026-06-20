#include "OpenGLContext.h"
#include "Core/Logging/Log.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>

namespace Conqueror
{
    OpenGLContext::OpenGLContext(GLFWwindow* windowHandle)
        : m_WindowHandle(windowHandle)
    {
        CQ_CORE_ASSERT(windowHandle, "Window handle is null!")
    }

    void OpenGLContext::Init()
    {
        glfwMakeContextCurrent(m_WindowHandle);
        [[maybe_unused]] int status = gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);
        CQ_CORE_ASSERT(status, "Failed to initialize Glad!");

        CQ_CORE_INFO("OpenGL Info:");
        CQ_CORE_INFO("  Vendor: {0}", (const char*)glGetString(GL_VENDOR));
        CQ_CORE_INFO("  Renderer: {0}", (const char*)glGetString(GL_RENDERER));
        CQ_CORE_INFO("  Version: {0}", (const char*)glGetString(GL_VERSION));

        // Check OpenGL version - require at least 4.1 (more compatible than 4.5)
        int major = GLVersion.major;
        int minor = GLVersion.minor;
        CQ_CORE_INFO("  Loaded OpenGL {0}.{1}", major, minor);
        
        if (major < 4 || (major == 4 && minor < 1))
        {
            CQ_CORE_ERROR("Conqueror requires at least OpenGL version 4.1!");
            CQ_CORE_ERROR("Your system has OpenGL {0}.{1}", major, minor);
            CQ_CORE_ASSERT(false, "Insufficient OpenGL version!");
        }
    }

    void OpenGLContext::SwapBuffers()
    {
        glfwSwapBuffers(m_WindowHandle);
    }
}
