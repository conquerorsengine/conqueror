#pragma once

#ifdef CQ_D3D11_ENABLED

#ifndef NOMINMAX
    #define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <d3d11_1.h>
#include <dxgi1_2.h>

#include <cstdint>
#include <string>
#include <vector>

struct GLFWwindow;

namespace Conqueror::DX
{
    // Direct3D 11 cihazı + DXGI takas zinciri. GL çeviri katmanı (DxGL) bunun üstünde çalışır.
    class Core
    {
    public:
        static Core& Get();

        bool Init(GLFWwindow* window, std::string& error);
        void Shutdown();
        bool IsInitialized() const { return m_Device != nullptr; }

        ID3D11Device* Device() const { return m_Device; }
        ID3D11DeviceContext* Context() const { return m_Context; }
        ID3D11DeviceContext1* Context1() const { return m_Context1; }   // ClearView için (11.1, olmayabilir)
        D3D_FEATURE_LEVEL FeatureLevel() const { return m_FeatureLevel; }
        const std::string& DeviceName() const { return m_DeviceName; }
        uint32_t VendorId() const { return m_VendorId; }
        uint64_t VideoMemory() const { return m_VideoMemory; }

        uint32_t FramebufferWidth() const;
        uint32_t FramebufferHeight() const;

        // GL yönündeki (satır 0 = alt) kaynağı takas zincirine çevirerek kopyala ve sun
        void Present(ID3D11ShaderResourceView* source, uint32_t width, uint32_t height);
        void SetVSync(bool enabled) { m_VSync = enabled; }
        uint64_t FrameSerial() const { return m_FrameSerial; }

        // HLSL -> bayt kodu (d3dcompiler_47)
        static bool CompileHLSL(const std::string& source, const char* name, const char* entry, const char* target, std::vector<uint8_t>& out, std::string& errors);

        // Zamanlama: kare başına ayrıklık sorgusu halkası (GL_TIMESTAMP sorgularının frekansı)
        static constexpr uint32_t kTimingFrames = 8;
        uint32_t TimingFrame() const { return m_TimingIndex; }
        bool TimingFrequency(uint32_t frame, bool wait, uint64_t& frequency, bool& disjoint);
        void BeginTiming();
        void EndTiming();

    private:
        bool CreateSwapchain(std::string& error);
        bool CreateBackbufferView();
        bool CreatePresentResources(std::string& error);

        GLFWwindow* m_Window = nullptr;
        HWND m_Hwnd = nullptr;
        ID3D11Device* m_Device = nullptr;
        ID3D11DeviceContext* m_Context = nullptr;
        ID3D11DeviceContext1* m_Context1 = nullptr;
        IDXGIFactory2* m_Factory = nullptr;
        IDXGISwapChain1* m_Swapchain = nullptr;
        ID3D11RenderTargetView* m_BackbufferRTV = nullptr;
        uint32_t m_SwapWidth = 0, m_SwapHeight = 0;
        bool m_FlipModel = true;
        bool m_VSync = true;
        D3D_FEATURE_LEVEL m_FeatureLevel = D3D_FEATURE_LEVEL_11_0;
        std::string m_DeviceName;
        uint32_t m_VendorId = 0;
        uint64_t m_VideoMemory = 0;
        uint64_t m_FrameSerial = 1;

        // Sunum (dikey çevirerek kopyalama)
        ID3D11VertexShader* m_BlitVS = nullptr;
        ID3D11PixelShader* m_BlitPS = nullptr;
        ID3D11SamplerState* m_BlitSampler = nullptr;
        ID3D11RasterizerState* m_BlitRaster = nullptr;
        ID3D11DepthStencilState* m_BlitDepth = nullptr;
        ID3D11BlendState* m_BlitBlend = nullptr;

        ID3D11Query* m_Disjoint[kTimingFrames] = {};
        bool m_DisjointActive = false;
        bool m_DisjointUsed[kTimingFrames] = {};
        uint32_t m_TimingIndex = 0;
    };
}

#endif
