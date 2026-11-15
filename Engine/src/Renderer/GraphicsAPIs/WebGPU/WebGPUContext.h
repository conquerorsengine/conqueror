#pragma once

#ifdef CQ_WEBGPU_ENABLED

#include "Renderer/RHI/GraphicsContext.h"

struct GLFWwindow;

namespace Conqueror
{
    // WebGPU arka ucu: cihaz + yüzey (WgpuGLCore) ve GL çeviri katmanı (WgpuGL). Masaüstünde Dawn / wgpu-native, Web'de tarayıcı
    class WebGPUContext : public GraphicsContext
    {
    public:
        explicit WebGPUContext(GLFWwindow* windowHandle);
        ~WebGPUContext() override;

        bool Init() override;
        void SwapBuffers() override;
        void SetVSync(bool enabled) override;
        const char* GetName() const override { return "WebGPU"; }

    private:
        GLFWwindow* m_WindowHandle = nullptr;
        bool m_Initialized = false;
    };
}

#endif
