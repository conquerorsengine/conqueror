#include "OpenGLRendererAPI.h"
#include "Core/Base/Base.h"
#include "Core/Logging/Log.h"
#include "Renderer/RHI/VertexArray.h"

#include <glad/glad.h>

namespace Conqueror
{
    void OpenGLMessageCallback(
        [[maybe_unused]] unsigned source,
        [[maybe_unused]] unsigned type,
        [[maybe_unused]] unsigned id,
        unsigned severity,
        [[maybe_unused]] int length,
        const char* message,
        [[maybe_unused]] const void* userParam)
    {
        switch (severity)
        {
            case GL_DEBUG_SEVERITY_HIGH:         CQ_CORE_CRITICAL(message); return;
            case GL_DEBUG_SEVERITY_MEDIUM:       CQ_CORE_ERROR(message); return;
            case GL_DEBUG_SEVERITY_LOW:          CQ_CORE_WARN(message); return;
            case GL_DEBUG_SEVERITY_NOTIFICATION: CQ_CORE_TRACE(message); return;
        }

        CQ_CORE_ASSERT(false, "Unknown severity level!");
    }

    void OpenGLRendererAPI::Init()
    {
        #ifdef CQ_DEBUG
            glEnable(GL_DEBUG_OUTPUT);
            glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
            glDebugMessageCallback(OpenGLMessageCallback, nullptr);

            glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DEBUG_SEVERITY_NOTIFICATION, 0, NULL, GL_FALSE);
        #endif

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        glEnable(GL_DEPTH_TEST);
        glEnable(GL_LINE_SMOOTH);
        
        // MSAA (Multi-Sample Anti-Aliasing) aktif et
        glEnable(GL_MULTISAMPLE);
    }

    void OpenGLRendererAPI::SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height)
    {
        glViewport(x, y, width, height);
    }

    void OpenGLRendererAPI::SetClearColor(const glm::vec4& color)
    {
        glClearColor(color.r, color.g, color.b, color.a);
    }

    void OpenGLRendererAPI::Clear()
    {
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }

    void OpenGLRendererAPI::DrawIndexed(const std::shared_ptr<VertexArray>& vertexArray, uint32_t indexCount)
    {
        vertexArray->Bind();
        uint32_t count = indexCount ? indexCount : vertexArray->GetIndexBuffer()->GetCount();
        glDrawElements(GL_TRIANGLES, count, GL_UNSIGNED_INT, nullptr);
    }

    void OpenGLRendererAPI::DrawLines(const std::shared_ptr<VertexArray>& vertexArray, uint32_t vertexCount)
    {
        vertexArray->Bind();
        glDrawArrays(GL_LINES, 0, vertexCount);
    }

    void OpenGLRendererAPI::SetLineWidth(float width)
    {
        glLineWidth(width);
    }
}
