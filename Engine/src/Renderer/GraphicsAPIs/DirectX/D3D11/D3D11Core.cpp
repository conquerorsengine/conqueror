#ifdef CQ_D3D11_ENABLED

#include "D3D11Core.h"
#include "Core/Logging/Log.h"
#include "../Common/DxShared.h"

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace Conqueror::DX
{
    namespace
    {
        const char* kBlitHLSL = R"(
Texture2D g_Src : register(t0);
SamplerState g_Smp : register(s0);
struct VSOut { float4 Pos : SV_Position; float2 UV : TEXCOORD0; };
VSOut VSMain(uint id : SV_VertexID)
{
    VSOut o;
    float2 p = float2((id << 1) & 2, id & 2);
    o.Pos = float4(p * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    o.UV = float2(p.x, 1.0 - p.y);   // GL yönü (satır 0 = alt) -> ekran (satır 0 = üst)
    return o;
}
float4 PSMain(VSOut i) : SV_Target { return float4(g_Src.SampleLevel(g_Smp, i.UV, 0).rgb, 1.0); }
)";

        std::string Narrow(const wchar_t* w)
        {
            std::string out;
            if (!w)
                return out;
            const int len = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
            if (len > 1)
            {
                out.resize((size_t)len - 1);
                WideCharToMultiByte(CP_UTF8, 0, w, -1, out.data(), len, nullptr, nullptr);
            }
            return out;
        }

        template <typename T>
        void Release(T*& p)
        {
            if (p)
            {
                p->Release();
                p = nullptr;
            }
        }
    }

    Core& Core::Get()
    {
        static Core s;
        return s;
    }

    bool Core::CompileHLSL(const std::string& source, const char* name, const char* entry, const char* target, std::vector<uint8_t>& out, std::string& errors)
    {
        return DXCommon::CompileHLSL(source, name, entry, target, out, errors);
    }

    bool Core::Init(GLFWwindow* window, std::string& error)
    {
        m_Window = window;
        m_Hwnd = glfwGetWin32Window(window);
        if (!m_Hwnd)
        {
            error = "No Win32 window handle";
            return false;
        }

        UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
        if (const char* dbg = std::getenv("CQ_D3D_DEBUG"); dbg && dbg[0] == '1')
            flags |= D3D11_CREATE_DEVICE_DEBUG;
        const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };
        HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels, 2, D3D11_SDK_VERSION, &m_Device, &m_FeatureLevel, &m_Context);
        if (hr == E_INVALIDARG)   // 11.1 tanımayan çalışma zamanı
            hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels + 1, 1, D3D11_SDK_VERSION, &m_Device, &m_FeatureLevel, &m_Context);
        if (FAILED(hr) && (flags & D3D11_CREATE_DEVICE_DEBUG))
        {
            CQ_CORE_WARN("[D3D11] Debug layer unavailable, creating a release device");
            flags &= ~D3D11_CREATE_DEVICE_DEBUG;
            hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, levels + 1, 1, D3D11_SDK_VERSION, &m_Device, &m_FeatureLevel, &m_Context);
        }
        if (FAILED(hr) || !m_Device)
        {
            error = "D3D11CreateDevice failed (feature level 11_0 required)";
            return false;
        }
        m_Context->QueryInterface(__uuidof(ID3D11DeviceContext1), (void**)&m_Context1);

        IDXGIDevice* dxgiDevice = nullptr;
        IDXGIAdapter* adapter = nullptr;
        if (SUCCEEDED(m_Device->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgiDevice)) && SUCCEEDED(dxgiDevice->GetAdapter(&adapter)))
        {
            DXGI_ADAPTER_DESC desc{};
            if (SUCCEEDED(adapter->GetDesc(&desc)))
            {
                m_DeviceName = Narrow(desc.Description);
                m_VendorId = desc.VendorId;
                m_VideoMemory = desc.DedicatedVideoMemory ? desc.DedicatedVideoMemory : desc.SharedSystemMemory;
            }
            adapter->GetParent(__uuidof(IDXGIFactory2), (void**)&m_Factory);
        }
        Release(adapter);
        Release(dxgiDevice);
        if (!m_Factory)
        {
            error = "DXGI 1.2 factory unavailable";
            return false;
        }
        if (!CreateSwapchain(error) || !CreatePresentResources(error))
            return false;

        D3D11_QUERY_DESC qd{ D3D11_QUERY_TIMESTAMP_DISJOINT, 0 };
        for (auto& q : m_Disjoint)
            m_Device->CreateQuery(&qd, &q);
        BeginTiming();

        CQ_CORE_INFO("[D3D11] Device: {0} (feature level {1}.{2}, {3} MB){4}", m_DeviceName, (m_FeatureLevel >> 12) & 0xF, (m_FeatureLevel >> 8) & 0xF,
                     m_VideoMemory / (1024 * 1024), m_FlipModel ? "" : " [bitblt swap chain]");
        return true;
    }

    bool Core::CreateSwapchain(std::string& error)
    {
        DXGI_SWAP_CHAIN_DESC1 sd{};
        sd.Width = FramebufferWidth();
        sd.Height = FramebufferHeight();
        sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;   // motor çıktısı zaten sRGB kodlu
        sd.SampleDesc.Count = 1;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.BufferCount = 2;
        sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        sd.Scaling = DXGI_SCALING_STRETCH;
        HRESULT hr = m_Factory->CreateSwapChainForHwnd(m_Device, m_Hwnd, &sd, nullptr, nullptr, &m_Swapchain);
        if (FAILED(hr))
        {
            // Windows 7/8 ya da çevirme modelini desteklemeyen ortam
            m_FlipModel = false;
            sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
            sd.BufferCount = 1;
            hr = m_Factory->CreateSwapChainForHwnd(m_Device, m_Hwnd, &sd, nullptr, nullptr, &m_Swapchain);
        }
        if (FAILED(hr) || !m_Swapchain)
        {
            error = "CreateSwapChainForHwnd failed";
            return false;
        }
        m_Factory->MakeWindowAssociation(m_Hwnd, DXGI_MWA_NO_ALT_ENTER);   // tam ekranı GLFW yönetir
        m_SwapWidth = sd.Width;
        m_SwapHeight = sd.Height;
        return CreateBackbufferView();
    }

    bool Core::CreateBackbufferView()
    {
        Release(m_BackbufferRTV);
        ID3D11Texture2D* back = nullptr;
        if (FAILED(m_Swapchain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&back)) || !back)
            return false;
        const HRESULT hr = m_Device->CreateRenderTargetView(back, nullptr, &m_BackbufferRTV);
        back->Release();
        return SUCCEEDED(hr);
    }

    bool Core::CreatePresentResources(std::string& error)
    {
        std::vector<uint8_t> vs, ps;
        std::string err;
        if (!CompileHLSL(kBlitHLSL, "present_vs", "VSMain", "vs_5_0", vs, err) || !CompileHLSL(kBlitHLSL, "present_ps", "PSMain", "ps_5_0", ps, err))
        {
            error = "Present shader compile failed: " + err;
            return false;
        }
        m_Device->CreateVertexShader(vs.data(), vs.size(), nullptr, &m_BlitVS);
        m_Device->CreatePixelShader(ps.data(), ps.size(), nullptr, &m_BlitPS);
        D3D11_SAMPLER_DESC smp{};
        smp.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        smp.AddressU = smp.AddressV = smp.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        smp.MaxLOD = D3D11_FLOAT32_MAX;
        m_Device->CreateSamplerState(&smp, &m_BlitSampler);
        D3D11_RASTERIZER_DESC rs{};
        rs.FillMode = D3D11_FILL_SOLID;
        rs.CullMode = D3D11_CULL_NONE;
        rs.DepthClipEnable = TRUE;
        m_Device->CreateRasterizerState(&rs, &m_BlitRaster);
        D3D11_DEPTH_STENCIL_DESC ds{};
        ds.DepthEnable = FALSE;
        ds.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
        ds.DepthFunc = D3D11_COMPARISON_ALWAYS;
        m_Device->CreateDepthStencilState(&ds, &m_BlitDepth);
        D3D11_BLEND_DESC bd{};
        bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
        m_Device->CreateBlendState(&bd, &m_BlitBlend);
        return m_BlitVS && m_BlitPS && m_BlitSampler && m_BlitRaster && m_BlitDepth && m_BlitBlend;
    }

    uint32_t Core::FramebufferWidth() const
    {
        int w = 0, h = 0;
        glfwGetFramebufferSize(m_Window, &w, &h);
        return (uint32_t)std::max(w, 1);
    }

    uint32_t Core::FramebufferHeight() const
    {
        int w = 0, h = 0;
        glfwGetFramebufferSize(m_Window, &w, &h);
        return (uint32_t)std::max(h, 1);
    }

    void Core::BeginTiming()
    {
        if (!m_Disjoint[m_TimingIndex] || m_DisjointActive)
            return;
        m_Context->Begin(m_Disjoint[m_TimingIndex]);
        m_DisjointActive = true;
        m_DisjointUsed[m_TimingIndex] = true;
    }

    void Core::EndTiming()
    {
        if (!m_DisjointActive)
            return;
        m_Context->End(m_Disjoint[m_TimingIndex]);
        m_DisjointActive = false;
    }

    bool Core::TimingFrequency(uint32_t frame, bool wait, uint64_t& frequency, bool& disjoint)
    {
        frequency = 0;
        disjoint = true;
        frame %= kTimingFrames;
        if (!m_Disjoint[frame] || !m_DisjointUsed[frame])
            return true;
        if (frame == m_TimingIndex && m_DisjointActive)
        {
            if (!wait)
                return false;
            EndTiming();   // sonuç için kareyi erken kapat (yeni bir aralık sonraki karede başlar)
        }
        D3D11_QUERY_DATA_TIMESTAMP_DISJOINT data{};
        for (;;)
        {
            const HRESULT hr = m_Context->GetData(m_Disjoint[frame], &data, sizeof(data), wait ? 0 : D3D11_ASYNC_GETDATA_DONOTFLUSH);
            if (hr == S_OK)
                break;
            if (!wait || FAILED(hr))
                return false;
            Sleep(0);
        }
        frequency = data.Frequency;
        disjoint = data.Disjoint != FALSE;
        return true;
    }

    void Core::Present(ID3D11ShaderResourceView* source, uint32_t width, uint32_t height)
    {
        (void)width;
        (void)height;
        if (!m_Swapchain)
            return;
        EndTiming();
        const uint32_t fw = FramebufferWidth(), fh = FramebufferHeight();
        if (fw != m_SwapWidth || fh != m_SwapHeight)
        {
            m_Context->OMSetRenderTargets(0, nullptr, nullptr);
            Release(m_BackbufferRTV);
            m_Context->Flush();
            if (SUCCEEDED(m_Swapchain->ResizeBuffers(0, fw, fh, DXGI_FORMAT_UNKNOWN, 0)))
            {
                m_SwapWidth = fw;
                m_SwapHeight = fh;
            }
            CreateBackbufferView();
        }
        if (m_BackbufferRTV && source)
        {
            m_Context->OMSetRenderTargets(1, &m_BackbufferRTV, nullptr);
            D3D11_VIEWPORT vp{ 0.0f, 0.0f, (float)m_SwapWidth, (float)m_SwapHeight, 0.0f, 1.0f };
            m_Context->RSSetViewports(1, &vp);
            m_Context->RSSetState(m_BlitRaster);
            m_Context->OMSetDepthStencilState(m_BlitDepth, 0);
            m_Context->OMSetBlendState(m_BlitBlend, nullptr, 0xFFFFFFFF);
            m_Context->IASetInputLayout(nullptr);
            m_Context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            m_Context->VSSetShader(m_BlitVS, nullptr, 0);
            m_Context->PSSetShader(m_BlitPS, nullptr, 0);
            m_Context->PSSetShaderResources(0, 1, &source);
            m_Context->PSSetSamplers(0, 1, &m_BlitSampler);
            m_Context->Draw(3, 0);
            ID3D11ShaderResourceView* none = nullptr;
            m_Context->PSSetShaderResources(0, 1, &none);
        }
        const HRESULT hr = m_Swapchain->Present(m_VSync ? 1 : 0, 0);
        if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
            CQ_CORE_ERROR("[D3D11] Device lost (0x{0:x})", (unsigned)m_Device->GetDeviceRemovedReason());
        ++m_FrameSerial;
        m_TimingIndex = (m_TimingIndex + 1) % kTimingFrames;
        BeginTiming();
    }

    void Core::Shutdown()
    {
        if (!m_Device)
            return;
        EndTiming();
        if (m_Context)
        {
            m_Context->ClearState();
            m_Context->Flush();
        }
        for (auto& q : m_Disjoint)
            Release(q);
        Release(m_BlitVS);
        Release(m_BlitPS);
        Release(m_BlitSampler);
        Release(m_BlitRaster);
        Release(m_BlitDepth);
        Release(m_BlitBlend);
        Release(m_BackbufferRTV);
        Release(m_Swapchain);
        Release(m_Factory);
        Release(m_Context1);
        Release(m_Context);
        Release(m_Device);
    }
}

#endif
