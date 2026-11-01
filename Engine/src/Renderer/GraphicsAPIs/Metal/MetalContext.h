#pragma once

#ifdef CQ_METAL_ENABLED

#include "Renderer/RHI/GraphicsContext.h"

struct GLFWwindow;

namespace Conqueror
{
    // Metal arka ucu: cihaz + takas zinciri (DX::Core) ve GL çeviri katmanı (DxGL)
    class MetalContext : public GraphicsContext
    {
    public:
        explicit MetalContext(GLFWwindow* windowHandle);
        ~MetalContext() override;

        bool Init() override;
        void SwapBuffers() override;
        void SetVSync(bool enabled) override;
        const char* GetName() const override { return "Metal"; }

    private:
        GLFWwindow* m_WindowHandle = nullptr;
        bool m_Initialized = false;
    };
}

#endif
