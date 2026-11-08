#include "OpenGLRendererAPI.h"
#include "Core/Base/Base.h"
#include "Core/Logging/Log.h"
#include "Renderer/RHI/VertexArray.h"

#include <glad/glad.h>

#include <cstdlib>
#include <cstring>

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
        // Mesaj biçim dizesi olarak verilmez ({} içeren GL mesajları fmt istisnası atardı).
        // GL hataları çökme dökümü üretmez: kritik log + dosya diyaloğu motoru dondururdu.
        // Gölgelendirici derleyicisinin uyarıları (Mesa bunları yüksek önemle bildirir, ör. FSR 2'nin çalışma anında derlenen
        // geçişlerindeki "used uninitialized") hata değildir: konsolda kırmızı hata olarak görünmesin
        if (message && std::strstr(message, ": warning:"))
        {
            CQ_CORE_WARN("[GL] {0}", message);
            return;
        }
        switch (severity)
        {
            case GL_DEBUG_SEVERITY_HIGH:         CQ_CORE_ERROR("[GL] {0}", message); return;
            case GL_DEBUG_SEVERITY_MEDIUM:       CQ_CORE_WARN("[GL] {0}", message); return;
            case GL_DEBUG_SEVERITY_LOW:          CQ_CORE_TRACE("[GL] {0}", message); return;
            case GL_DEBUG_SEVERITY_NOTIFICATION: CQ_CORE_TRACE("[GL] {0}", message); return;
            default:                             CQ_CORE_TRACE("[GL] {0}", message); return;
        }
    }

    void OpenGLRendererAPI::Init()
    {
        bool debugOutput = false;
        #ifdef CQ_DEBUG
            debugOutput = true;
        #endif
        // Sürüm derlemesinde de: CQ_GL_DEBUG=1 (performans / API uyarıları Log paneline)
        if (const char* dbg = std::getenv("CQ_GL_DEBUG"); dbg && dbg[0] == '1')
            debugOutput = true;
        if (debugOutput && glDebugMessageCallback)
        {
            glEnable(GL_DEBUG_OUTPUT);
            glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
            glDebugMessageCallback(OpenGLMessageCallback, nullptr);

            glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DEBUG_SEVERITY_NOTIFICATION, 0, NULL, GL_FALSE);
            CQ_CORE_INFO("[GL] Debug output enabled");
        }

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
        // Core profile'da taşınabilir tek çizgi genişliği 1.0'dır; daha büyük
        // değerler deprecated'tir ve sürücü her çağrıda uyarı basar. Sebebi
        // ortadan kaldır: kelepçele (ilk seferde tek satır bilgi ver).
        if (width != 1.0f)
        {
            static bool s_Warned = false;
            if (!s_Warned)
            {
                s_Warned = true;
                CQ_CORE_WARN("OpenGL core profile only supports line width 1.0; clamping {0}", width);
            }
            width = 1.0f;
        }
        glLineWidth(width);
    }
}
