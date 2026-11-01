#pragma once

#ifdef CQ_D3D12_ENABLED

#include "Renderer/RHI/GraphicsContext.h"

struct GLFWwindow;

namespace Conqueror
{
    // Direct3D 12 arka ucu: cihaz + takas zinciri (DX::Core) ve GL çeviri katmanı (DxGL)
    class D3D12Context : public GraphicsContext
    {
    public:
        explicit D3D12Context(GLFWwindow* windowHandle);
        ~D3D12Context() override;

        bool Init() override;
        void SwapBuffers() override;
        void SetVSync(bool enabled) override;
        const char* GetName() const override { return "Direct3D 12"; }

    private:
        GLFWwindow* m_WindowHandle = nullptr;
        bool m_Initialized = false;
    };
}

#endif
