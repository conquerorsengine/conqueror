#include "OpenGLContext.h"
#include "GLProfile.h"
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

    bool OpenGLContext::Init()
    {
        glfwMakeContextCurrent(m_WindowHandle);
        [[maybe_unused]] int status = gladLoadGLLoader((GLADloadproc)glfwGetProcAddress);
        CQ_CORE_ASSERT(status, "Failed to initialize Glad!");

        CQ_CORE_INFO("OpenGL Info:");
        CQ_CORE_INFO("  Vendor: {0}", (const char*)glGetString(GL_VENDOR));
        CQ_CORE_INFO("  Renderer: {0}", (const char*)glGetString(GL_RENDERER));
        CQ_CORE_INFO("  Version: {0}", (const char*)glGetString(GL_VERSION));

        // Profil algılama + 4.5 (DSA) öncesi / ES bağlamlarında eksik işlevlerin taklidi
        GLProfile::OnContextCreated([](const char* name) { return (void*)glfwGetProcAddress(name); });
        const auto& profile = GLProfile::Current();
        CQ_CORE_INFO("  Loaded {0} {1}.{2}", profile.ES ? "OpenGL ES" : "OpenGL", profile.Major, profile.Minor);

        // En az: masaüstü OpenGL 3.3 ya da OpenGL ES 3.0
        if ((!profile.ES && (profile.Major < 3 || (profile.Major == 3 && profile.Minor < 3))) || (profile.ES && profile.Major < 3))
        {
            CQ_CORE_ERROR("Conqueror requires OpenGL 3.3+ or OpenGL ES 3.0+ (this system has {0})", profile.Description);
            return false;
        }
        return status != 0;
    }

    void OpenGLContext::SwapBuffers()
    {
        glfwSwapBuffers(m_WindowHandle);
    }
}
