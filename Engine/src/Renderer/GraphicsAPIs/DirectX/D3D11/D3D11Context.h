#pragma once

#ifdef CQ_D3D11_ENABLED

#include "Renderer/RHI/GraphicsContext.h"

struct GLFWwindow;

namespace Conqueror
{
    // Direct3D 11 arka ucu: cihaz + takas zinciri (DX::Core) ve GL çeviri katmanı (DxGL)
    class D3D11Context : public GraphicsContext
    {
    public:
        explicit D3D11Context(GLFWwindow* windowHandle);
        ~D3D11Context() override;

        bool Init() override;
        void SwapBuffers() override;
        void SetVSync(bool enabled) override;
        const char* GetName() const override { return "Direct3D 11"; }

    private:
        GLFWwindow* m_WindowHandle = nullptr;
        bool m_Initialized = false;
    };
}

#endif
