#pragma once

#include "Core/Base/Base.h"
#include <glm/glm.hpp>
#include <memory>
#include <string>

namespace Conqueror
{
    class VertexArray;

    class RendererAPI
    {
    public:
        enum class API
        {
            None = 0,
            OpenGL = 1,
            Vulkan = 2
        };

    public:
        virtual ~RendererAPI() = default;

        virtual void Init() = 0;
        virtual void SetViewport(uint32_t x, uint32_t y, uint32_t width, uint32_t height) = 0;
        virtual void SetClearColor(const glm::vec4& color) = 0;
        virtual void Clear() = 0;

        virtual void DrawIndexed(const std::shared_ptr<VertexArray>& vertexArray, uint32_t indexCount = 0) = 0;
        virtual void DrawLines(const std::shared_ptr<VertexArray>& vertexArray, uint32_t vertexCount) = 0;

        virtual void SetLineWidth(float width) = 0;

        static API GetAPI() { return s_API; }
        static std::unique_ptr<RendererAPI> Create();

        // Grafik arka ucu. Kaynak sınıfları (OpenGL*) her iki arka uçta da kullanılır: Vulkan arka ucu GL
        // çağrılarını Vulkan'a çeviren katmandır (VkGL). Pencere oluşturulmadan önce seçilmelidir.
        enum class Backend
        {
            OpenGL = 0,
            Vulkan = 1,
            Direct3D11 = 2,
            Direct3D12 = 3,
            Metal = 4
        };
        static Backend GetBackend() { return s_Backend; }
        static void SetBackend(Backend backend) { s_Backend = backend; }
        static const char* GetBackendName(Backend backend);
        static const char* GetBackendName() { return GetBackendName(s_Backend); }
        static bool IsBackendCompiled(Backend backend);
        // Öncelik: komut satırı (--vulkan / --d3d11 / --d3d12 / --metal / --opengl / --renderer <ad>) > CQ_RENDERER ortam değişkeni >
        // istenen ("Auto"/"OpenGL"/"Vulkan"/"Direct3D11"/"Direct3D12"/"Metal"). Auto: Windows'ta Direct3D 11, macOS'ta Metal, diğerlerinde OpenGL.
        static Backend ResolveBackend(int argc, char** argv, const std::string& requested);
        static bool ParseBackend(const std::string& name, Backend& out);
        // Etkin GPU açıklaması (GL_RENDERER; Vulkan'da cihaz adı + API sürümü)
        static std::string GetDeviceDescription();

    private:
        static API s_API;
        static Backend s_Backend;
    };
}
