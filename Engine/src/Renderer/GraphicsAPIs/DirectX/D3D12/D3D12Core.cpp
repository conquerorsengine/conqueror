#ifdef CQ_D3D12_ENABLED

#include "D3D12Core.h"
#include "../Common/DxShared.h"
#include "Core/Logging/Log.h"

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace Conqueror::DX12
{
    namespace
    {
        constexpr uint64_t kChunkSize = 32ull * 1024 * 1024;
        constexpr uint32_t kShaderDescriptorsPerFrame = 120000;
        constexpr uint32_t kSamplerHeapSize = 2048;
        constexpr uint32_t kTimestamps = 4096;

        // d3d12.dll çalışma anında açılır: yürütülebilir dosya D3D12'siz sistemlerde de başlar (D3D11 / OpenGL'e düşer)
        PFN_D3D12_CREATE_DEVICE s_CreateDevice = nullptr;
        PFN_D3D12_SERIALIZE_ROOT_SIGNATURE s_SerializeRootSignature = nullptr;
        PFN_D3D12_GET_DEBUG_INTERFACE s_GetDebugInterface = nullptr;

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

        D3D12_HEAP_PROPERTIES HeapProps(D3D12_HEAP_TYPE type)
        {
            D3D12_HEAP_PROPERTIES hp{};
            hp.Type = type;
            hp.CreationNodeMask = hp.VisibleNodeMask = 1;
            return hp;
        }

        D3D12_RESOURCE_DESC BufferDesc(uint64_t size)
        {
            D3D12_RESOURCE_DESC rd{};
            rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            rd.Width = size;
            rd.Height = 1;
            rd.DepthOrArraySize = 1;
            rd.MipLevels = 1;
            rd.SampleDesc.Count = 1;
            rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            return rd;
        }
    }

    // ── Tanımlayıcı havuzu ──
    bool DescriptorPool::Init(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t count)
    {
        D3D12_DESCRIPTOR_HEAP_DESC hd{};
        hd.Type = type;
        hd.NumDescriptors = count;
        if (FAILED(device->CreateDescriptorHeap(&hd, __uuidof(ID3D12DescriptorHeap), (void**)&m_Heap)) || !m_Heap)
            return false;
        m_Type = type;
        m_Start = m_Heap->GetCPUDescriptorHandleForHeapStart();
        m_Increment = device->GetDescriptorHandleIncrementSize(type);
        m_Count = count;
        m_Next = 0;
        m_FreeList.clear();
        return true;
    }

    void DescriptorPool::Shutdown()
    {
        Release(m_Heap);
        m_FreeList.clear();
        m_Count = m_Next = 0;
    }

    CpuDescriptor DescriptorPool::Allocate()
    {
        CpuDescriptor d;
        if (!m_FreeList.empty())
        {
            d.Index = m_FreeList.back();
            m_FreeList.pop_back();
        }
        else if (m_Next < m_Count)
            d.Index = m_Next++;
        else
        {
            CQ_CORE_ERROR("[D3D12] Descriptor heap {0} exhausted ({1})", (int)m_Type, m_Count);
            return d;
        }
        d.Handle.ptr = m_Start.ptr + (SIZE_T)d.Index * m_Increment;
        return d;
    }

    void DescriptorPool::Free(CpuDescriptor& d)
    {
        if (d.Valid())
            m_FreeList.push_back(d.Index);
        d = CpuDescriptor{};
    }

    // ── Çekirdek ──
    Core& Core::Get()
    {
        static Core s;
        return s;
    }

    bool Core::CompileHLSL(const std::string& source, const char* name, const char* entry, const char* target, std::vector<uint8_t>& out, std::string& errors)
    {
        return DXCommon::CompileHLSL(source, name, entry, target, out, errors);
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

    bool Core::Init(GLFWwindow* window, std::string& error)
    {
        m_Window = window;
        m_Hwnd = glfwGetWin32Window(window);
        if (!m_Hwnd)
        {
            error = "No Win32 window handle";
            return false;
        }
        m_D3D12 = LoadLibraryW(L"d3d12.dll");
        if (!m_D3D12)
        {
            error = "d3d12.dll not found (Windows 10 or newer required)";
            return false;
        }
        s_CreateDevice = (PFN_D3D12_CREATE_DEVICE)(void*)GetProcAddress(m_D3D12, "D3D12CreateDevice");
        s_SerializeRootSignature = (PFN_D3D12_SERIALIZE_ROOT_SIGNATURE)(void*)GetProcAddress(m_D3D12, "D3D12SerializeRootSignature");
        s_GetDebugInterface = (PFN_D3D12_GET_DEBUG_INTERFACE)(void*)GetProcAddress(m_D3D12, "D3D12GetDebugInterface");
        if (!s_CreateDevice || !s_SerializeRootSignature)
        {
            error = "d3d12.dll is missing entry points";
            return false;
        }
        if (const char* dbg = std::getenv("CQ_D3D_DEBUG"); dbg && dbg[0] == '1' && s_GetDebugInterface)
        {
            ID3D12Debug* debug = nullptr;
            if (SUCCEEDED(s_GetDebugInterface(__uuidof(ID3D12Debug), (void**)&debug)) && debug)
            {
                debug->EnableDebugLayer();
                debug->Release();
            }
        }

        IDXGIFactory1* factory1 = nullptr;
        if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), (void**)&factory1)) || !factory1)
        {
            error = "CreateDXGIFactory1 failed";
            return false;
        }
        factory1->QueryInterface(__uuidof(IDXGIFactory4), (void**)&m_Factory);
        factory1->Release();
        if (!m_Factory)
        {
            error = "DXGI 1.4 factory unavailable";
            return false;
        }

        // Donanım bağdaştırıcısı (yazılımsal olanları atla); bulunamazsa varsayılan
        IDXGIAdapter1* adapter = nullptr;
        for (UINT i = 0; m_Factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i)
        {
            DXGI_ADAPTER_DESC1 desc{};
            adapter->GetDesc1(&desc);
            if (!(desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) &&
                SUCCEEDED(s_CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), (void**)&m_Device)))
            {
                m_DeviceName = Narrow(desc.Description);
                m_VendorId = desc.VendorId;
                m_VideoMemory = desc.DedicatedVideoMemory ? desc.DedicatedVideoMemory : desc.SharedSystemMemory;
                adapter->Release();
                break;
            }
            adapter->Release();
            adapter = nullptr;
        }
        if (!m_Device && FAILED(s_CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), (void**)&m_Device)))
            m_Device = nullptr;
        if (!m_Device)
        {
            error = "D3D12CreateDevice failed (feature level 11_0 required)";
            return false;
        }

        D3D12_COMMAND_QUEUE_DESC qd{};
        qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        if (FAILED(m_Device->CreateCommandQueue(&qd, __uuidof(ID3D12CommandQueue), (void**)&m_Queue)) ||
            FAILED(m_Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), (void**)&m_Fence)))
        {
            error = "Cannot create the command queue";
            return false;
        }
        m_FenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        m_Queue->GetTimestampFrequency(&m_TimestampFrequency);

        // Kök imzası: b0..b11 kök CBV, SRV tablosu t0..t63, örnekleyici tablosu s0..s15
        D3D12_ROOT_PARAMETER params[kRootCBVs + 2] = {};
        for (uint32_t i = 0; i < kRootCBVs; ++i)
        {
            params[i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
            params[i].Descriptor.ShaderRegister = i;
            params[i].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        }
        D3D12_DESCRIPTOR_RANGE srvRange{ D3D12_DESCRIPTOR_RANGE_TYPE_SRV, kMaxSRVs, 0, 0, 0 };
        D3D12_DESCRIPTOR_RANGE smpRange{ D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER, kMaxSamplers, 0, 0, 0 };
        params[kRootSRVTable].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[kRootSRVTable].DescriptorTable = { 1, &srvRange };
        params[kRootSRVTable].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        params[kRootSamplerTable].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[kRootSamplerTable].DescriptorTable = { 1, &smpRange };
        params[kRootSamplerTable].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
        D3D12_ROOT_SIGNATURE_DESC rsd{};
        rsd.NumParameters = kRootCBVs + 2;
        rsd.pParameters = params;
        rsd.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
        ID3DBlob* blob = nullptr;
        ID3DBlob* blobErr = nullptr;
        if (FAILED(s_SerializeRootSignature(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &blobErr)) || !blob ||
            FAILED(m_Device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), __uuidof(ID3D12RootSignature), (void**)&m_RootSignature)))
        {
            error = "Root signature creation failed";
            if (blobErr)
                error += std::string(": ") + (const char*)blobErr->GetBufferPointer();
            Release(blob);
            Release(blobErr);
            return false;
        }
        Release(blob);
        Release(blobErr);

        // Tanımlayıcı yığınları
        if (!m_Pools[D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV].Init(m_Device, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 65536) ||
            !m_Pools[D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER].Init(m_Device, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, 2048) ||
            !m_Pools[D3D12_DESCRIPTOR_HEAP_TYPE_RTV].Init(m_Device, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 16384) ||
            !m_Pools[D3D12_DESCRIPTOR_HEAP_TYPE_DSV].Init(m_Device, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 8192))
        {
            error = "Descriptor heap creation failed";
            return false;
        }
        D3D12_DESCRIPTOR_HEAP_DESC hd{};
        hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        hd.NumDescriptors = kShaderDescriptorsPerFrame * kFrames;
        hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        if (FAILED(m_Device->CreateDescriptorHeap(&hd, __uuidof(ID3D12DescriptorHeap), (void**)&m_ShaderHeap)))
        {
            error = "Shader-visible descriptor heap creation failed";
            return false;
        }
        m_ShaderHeapSize = hd.NumDescriptors;
        hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
        hd.NumDescriptors = kSamplerHeapSize;
        if (FAILED(m_Device->CreateDescriptorHeap(&hd, __uuidof(ID3D12DescriptorHeap), (void**)&m_SamplerHeap)))
        {
            error = "Sampler descriptor heap creation failed";
            return false;
        }
        m_ShaderIncrement = m_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        m_SamplerIncrement = m_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);

        // Zaman damgaları
        D3D12_QUERY_HEAP_DESC qhd{};
        qhd.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
        qhd.Count = kTimestamps;
        m_Device->CreateQueryHeap(&qhd, __uuidof(ID3D12QueryHeap), (void**)&m_QueryHeap);
        const D3D12_HEAP_PROPERTIES readback = HeapProps(D3D12_HEAP_TYPE_READBACK);
        const D3D12_RESOURCE_DESC qbd = BufferDesc(kTimestamps * 8ull);
        if (m_QueryHeap && SUCCEEDED(m_Device->CreateCommittedResource(&readback, D3D12_HEAP_FLAG_NONE, &qbd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                                        __uuidof(ID3D12Resource), (void**)&m_QueryReadback)))
            m_QueryReadback->Map(0, nullptr, (void**)&m_QueryData);
        qhd.Type = D3D12_QUERY_HEAP_TYPE_OCCLUSION;
        m_Device->CreateQueryHeap(&qhd, __uuidof(ID3D12QueryHeap), (void**)&m_OcclusionHeap);
        if (m_OcclusionHeap && SUCCEEDED(m_Device->CreateCommittedResource(&readback, D3D12_HEAP_FLAG_NONE, &qbd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr,
                                                                            __uuidof(ID3D12Resource), (void**)&m_OcclusionReadback)))
            m_OcclusionReadback->Map(0, nullptr, (void**)&m_OcclusionData);

        // Kareler + ilk komut listesi
        for (Frame& f : m_Frames)
            if (FAILED(m_Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, __uuidof(ID3D12CommandAllocator), (void**)&f.Allocator)) ||
                !NewChunk(f, kChunkSize))
            {
                error = "Cannot create frame resources";
                return false;
            }
        for (uint32_t i = 0; i < kFrames; ++i)
            m_Frames[i].DescriptorCursor = i * kShaderDescriptorsPerFrame;
        if (FAILED(m_Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_Frames[0].Allocator, nullptr, __uuidof(ID3D12GraphicsCommandList),
                                               (void**)&m_List)))
        {
            error = "Cannot create the command list";
            return false;
        }
        m_ListOpen = true;
        BindRoot();

        if (!CreateSwapchain(error))
            return false;

        CQ_CORE_INFO("[D3D12] Device: {0} ({1} MB){2}", m_DeviceName, m_VideoMemory / (1024 * 1024), m_Tearing ? " [tearing supported]" : "");
        return true;
    }

    bool Core::CreateSwapchain(std::string& error)
    {
        IDXGIFactory5* f5 = nullptr;
        if (SUCCEEDED(m_Factory->QueryInterface(__uuidof(IDXGIFactory5), (void**)&f5)) && f5)
        {
            BOOL allow = FALSE;
            if (SUCCEEDED(f5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allow, sizeof(allow))))
                m_Tearing = allow != FALSE;
            f5->Release();
        }
        DXGI_SWAP_CHAIN_DESC1 sd{};
        sd.Width = FramebufferWidth();
        sd.Height = FramebufferHeight();
        sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;   // motor çıktısı zaten sRGB kodlu
        sd.SampleDesc.Count = 1;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.BufferCount = kBackbuffers;
        sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        sd.Scaling = DXGI_SCALING_STRETCH;
        sd.Flags = m_Tearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
        IDXGISwapChain1* sc1 = nullptr;
        if (FAILED(m_Factory->CreateSwapChainForHwnd(m_Queue, m_Hwnd, &sd, nullptr, nullptr, &sc1)) || !sc1)
        {
            error = "CreateSwapChainForHwnd failed";
            return false;
        }
        sc1->QueryInterface(__uuidof(IDXGISwapChain3), (void**)&m_Swapchain);
        sc1->Release();
        if (!m_Swapchain)
        {
            error = "IDXGISwapChain3 unavailable";
            return false;
        }
        m_Factory->MakeWindowAssociation(m_Hwnd, DXGI_MWA_NO_ALT_ENTER);   // tam ekranı GLFW yönetir
        m_SwapWidth = sd.Width;
        m_SwapHeight = sd.Height;
        AcquireBackbuffers();
        return true;
    }

    void Core::AcquireBackbuffers()
    {
        auto& pool = m_Pools[D3D12_DESCRIPTOR_HEAP_TYPE_RTV];
        for (uint32_t i = 0; i < kBackbuffers; ++i)
        {
            if (FAILED(m_Swapchain->GetBuffer(i, __uuidof(ID3D12Resource), (void**)&m_Backbuffers[i])))
                continue;
            if (!m_BackbufferRTV[i].Valid())
                m_BackbufferRTV[i] = pool.Allocate();
            m_Device->CreateRenderTargetView(m_Backbuffers[i], nullptr, m_BackbufferRTV[i].Handle);
        }
    }

    void Core::ReleaseBackbuffers()
    {
        for (auto& b : m_Backbuffers)
            Release(b);
    }

    uint64_t Core::CompletedSerial() const { return m_Fence ? m_Fence->GetCompletedValue() : 0; }

    void Core::WaitSerial(uint64_t serial)
    {
        if (!m_Fence || serial == 0 || m_Fence->GetCompletedValue() >= serial)
            return;
        m_Fence->SetEventOnCompletion(serial, m_FenceEvent);
        WaitForSingleObject(m_FenceEvent, INFINITE);
    }

    void Core::BindRoot()
    {
        if (!m_List)
            return;
        ID3D12DescriptorHeap* heaps[] = { m_ShaderHeap, m_SamplerHeap };
        m_List->SetDescriptorHeaps(2, heaps);
        m_List->SetGraphicsRootSignature(m_RootSignature);
    }

    void Core::Submit()
    {
        if (!m_ListOpen)
            return;
        m_List->Close();
        ID3D12CommandList* lists[] = { m_List };
        m_Queue->ExecuteCommandLists(1, lists);
        m_Queue->Signal(m_Fence, m_NextSerial);
        m_Frames[m_FrameIndex].Serial = m_NextSerial;
        ++m_NextSerial;
        m_ListOpen = false;
    }

    void Core::OpenList(Frame& f)
    {
        f.Allocator->Reset();
        m_List->Reset(f.Allocator, nullptr);
        m_ListOpen = true;
        ++m_ListEpoch;
        BindRoot();
    }

    void Core::Flush()
    {
        if (!m_Device)
            return;
        Submit();
        WaitSerial(m_NextSerial - 1);
        // GPU boşta: tüm karelerin çöpü bırakılabilir, bu karenin tanımlayıcı bölümü baştan kullanılabilir
        for (Frame& f : m_Frames)
        {
            for (IUnknown* o : f.Garbage)
                o->Release();
            f.Garbage.clear();
        }
        Frame& cur = m_Frames[m_FrameIndex];
        cur.DescriptorCursor = m_FrameIndex * kShaderDescriptorsPerFrame;
        OpenList(cur);
    }

    void Core::DeferRelease(IUnknown* object)
    {
        if (object)
            m_Frames[m_FrameIndex].Garbage.push_back(object);
    }

    bool Core::NewChunk(Frame& f, uint64_t size)
    {
        RingChunk chunk;
        chunk.Size = std::max(size, kChunkSize);
        const D3D12_HEAP_PROPERTIES hp = HeapProps(D3D12_HEAP_TYPE_UPLOAD);
        const D3D12_RESOURCE_DESC rd = BufferDesc(chunk.Size);
        if (FAILED(m_Device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, __uuidof(ID3D12Resource),
                                                     (void**)&chunk.Resource)))
            return false;
        chunk.Resource->Map(0, nullptr, (void**)&chunk.Cpu);
        chunk.Gpu = chunk.Resource->GetGPUVirtualAddress();
        f.Chunks.push_back(chunk);
        return true;
    }

    UploadAlloc Core::AllocUpload(uint64_t size, uint64_t alignment)
    {
        Frame& f = m_Frames[m_FrameIndex];
        alignment = std::max<uint64_t>(alignment, 16);
        size = std::max<uint64_t>(size, 1);
        for (;;)
        {
            RingChunk& c = f.Chunks[f.Chunk];
            const uint64_t at = (f.Offset + alignment - 1) / alignment * alignment;
            if (at + size <= c.Size)
            {
                f.Offset = at + size;
                return { c.Cpu + at, c.Gpu + at, c.Resource, at };
            }
            if (f.Chunk + 1 < f.Chunks.size())
            {
                ++f.Chunk;
                f.Offset = 0;
                continue;
            }
            if (!NewChunk(f, size + alignment))
            {
                CQ_CORE_ERROR("[D3D12] Upload ring allocation of {0} bytes failed", size);
                return {};
            }
            f.Chunk = f.Chunks.size() - 1;
            f.Offset = 0;
        }
    }

    D3D12_GPU_DESCRIPTOR_HANDLE Core::CopyResourceTable(const D3D12_CPU_DESCRIPTOR_HANDLE* source, uint32_t count)
    {
        Frame& f = m_Frames[m_FrameIndex];
        const uint32_t end = (m_FrameIndex + 1) * kShaderDescriptorsPerFrame;
        if (f.DescriptorCursor + count > end)
            Flush();   // bölüm doldu: GPU'yu bekle, bölüm baştan
        const uint32_t at = f.DescriptorCursor;
        f.DescriptorCursor += std::max(count, 1u);
        D3D12_CPU_DESCRIPTOR_HANDLE dst = m_ShaderHeap->GetCPUDescriptorHandleForHeapStart();
        dst.ptr += (SIZE_T)at * m_ShaderIncrement;
        for (uint32_t i = 0; i < count; ++i)
        {
            D3D12_CPU_DESCRIPTOR_HANDLE d = dst;
            d.ptr += (SIZE_T)i * m_ShaderIncrement;
            m_Device->CopyDescriptorsSimple(1, d, source[i], D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        }
        D3D12_GPU_DESCRIPTOR_HANDLE gpu = m_ShaderHeap->GetGPUDescriptorHandleForHeapStart();
        gpu.ptr += (UINT64)at * m_ShaderIncrement;
        return gpu;
    }

    D3D12_GPU_DESCRIPTOR_HANDLE Core::SamplerTable(const D3D12_CPU_DESCRIPTOR_HANDLE* source, uint32_t count, uint64_t key)
    {
        if (auto it = m_SamplerTables.find(key); it != m_SamplerTables.end())
            return it->second;
        count = std::max(count, 1u);
        if (m_SamplerCursor + count > kSamplerHeapSize)
        {
            // Önbellek doldu: GPU bitirince yığın baştan doldurulur
            Flush();
            m_SamplerTables.clear();
            m_SamplerCursor = 0;
        }
        D3D12_CPU_DESCRIPTOR_HANDLE dst = m_SamplerHeap->GetCPUDescriptorHandleForHeapStart();
        dst.ptr += (SIZE_T)m_SamplerCursor * m_SamplerIncrement;
        for (uint32_t i = 0; i < count; ++i)
        {
            D3D12_CPU_DESCRIPTOR_HANDLE d = dst;
            d.ptr += (SIZE_T)i * m_SamplerIncrement;
            m_Device->CopyDescriptorsSimple(1, d, source[i], D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
        }
        D3D12_GPU_DESCRIPTOR_HANDLE gpu = m_SamplerHeap->GetGPUDescriptorHandleForHeapStart();
        gpu.ptr += (UINT64)m_SamplerCursor * m_SamplerIncrement;
        m_SamplerCursor += count;
        m_SamplerTables[key] = gpu;
        return gpu;
    }

    void Core::Barrier(ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after, UINT subresource)
    {
        if (!resource || before == after)
            return;
        D3D12_RESOURCE_BARRIER b{};
        b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b.Transition.pResource = resource;
        b.Transition.Subresource = subresource;
        b.Transition.StateBefore = before;
        b.Transition.StateAfter = after;
        m_List->ResourceBarrier(1, &b);
    }

    ID3D12Resource* Core::BeginBackbuffer(D3D12_CPU_DESCRIPTOR_HANDLE& rtv)
    {
        if (!m_Swapchain)
            return nullptr;
        const uint32_t fw = FramebufferWidth(), fh = FramebufferHeight();
        if (fw != m_SwapWidth || fh != m_SwapHeight)
        {
            Flush();
            ReleaseBackbuffers();
            if (SUCCEEDED(m_Swapchain->ResizeBuffers(0, fw, fh, DXGI_FORMAT_UNKNOWN, m_Tearing ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0)))
            {
                m_SwapWidth = fw;
                m_SwapHeight = fh;
            }
            AcquireBackbuffers();
        }
        m_CurrentBackbuffer = m_Swapchain->GetCurrentBackBufferIndex();
        ID3D12Resource* bb = m_Backbuffers[m_CurrentBackbuffer];
        if (!bb)
            return nullptr;
        Barrier(bb, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
        m_BackbufferAcquired = true;
        rtv = m_BackbufferRTV[m_CurrentBackbuffer].Handle;
        return bb;
    }

    void Core::BeginFrame(Frame& f)
    {
        WaitSerial(f.Serial);
        for (IUnknown* o : f.Garbage)
            o->Release();
        f.Garbage.clear();
        // Taşma parçaları bırakılır, ilk parça kalıcı
        for (size_t i = 1; i < f.Chunks.size(); ++i)
            f.Chunks[i].Resource->Release();
        if (f.Chunks.size() > 1)
            f.Chunks.resize(1);
        f.Chunk = 0;
        f.Offset = 0;
        f.DescriptorCursor = m_FrameIndex * kShaderDescriptorsPerFrame;
        OpenList(f);
    }

    void Core::Present()
    {
        if (!m_Swapchain)
            return;
        if (m_BackbufferAcquired)
        {
            Barrier(m_Backbuffers[m_CurrentBackbuffer], D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
            m_BackbufferAcquired = false;
        }
        Submit();
        const UINT flags = (!m_VSync && m_Tearing) ? DXGI_PRESENT_ALLOW_TEARING : 0;
        const HRESULT hr = m_Swapchain->Present(m_VSync ? 1 : 0, flags);
        if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
            CQ_CORE_ERROR("[D3D12] Device lost (0x{0:x})", (unsigned)m_Device->GetDeviceRemovedReason());
        ++m_FrameSerial;
        m_FrameIndex = (m_FrameIndex + 1) % kFrames;
        BeginFrame(m_Frames[m_FrameIndex]);
    }

    uint32_t Core::WriteTimestamp()
    {
        if (!m_QueryHeap || !m_QueryReadback)
            return UINT32_MAX;
        const uint32_t index = m_QueryCursor;
        m_QueryCursor = (m_QueryCursor + 1) % kTimestamps;
        m_List->EndQuery(m_QueryHeap, D3D12_QUERY_TYPE_TIMESTAMP, index);
        m_List->ResolveQueryData(m_QueryHeap, D3D12_QUERY_TYPE_TIMESTAMP, index, 1, m_QueryReadback, (UINT64)index * 8);
        return index;
    }

    bool Core::ReadTimestamp(uint32_t index, uint64_t serial, bool wait, uint64_t& ns)
    {
        ns = 0;
        if (index == UINT32_MAX || !m_QueryData || !m_TimestampFrequency)
            return true;
        if (CompletedSerial() < serial)
        {
            if (!wait)
                return false;
            if (serial >= m_NextSerial)
                Flush();   // sorgu henüz gönderilmemiş komut listesinde
            else
                WaitSerial(serial);
        }
        const uint64_t ticks = m_QueryData[index];
        ns = (uint64_t)((double)ticks * 1.0e9 / (double)m_TimestampFrequency);
        return true;
    }

    uint32_t Core::BeginOcclusion()
    {
        if (!m_OcclusionHeap || !m_OcclusionData)
            return UINT32_MAX;
        const uint32_t index = m_OcclusionCursor;
        m_OcclusionCursor = (m_OcclusionCursor + 1) % kTimestamps;
        m_List->BeginQuery(m_OcclusionHeap, D3D12_QUERY_TYPE_OCCLUSION, index);
        return index;
    }

    void Core::EndOcclusion(uint32_t index)
    {
        if (index == UINT32_MAX || !m_OcclusionHeap)
            return;
        m_List->EndQuery(m_OcclusionHeap, D3D12_QUERY_TYPE_OCCLUSION, index);
        m_List->ResolveQueryData(m_OcclusionHeap, D3D12_QUERY_TYPE_OCCLUSION, index, 1, m_OcclusionReadback, (UINT64)index * 8);
    }

    bool Core::ReadOcclusion(uint32_t index, uint64_t serial, bool wait, uint64_t& samples)
    {
        samples = 0;
        if (index == UINT32_MAX || !m_OcclusionData)
            return true;
        if (CompletedSerial() < serial)
        {
            if (!wait)
                return false;
            if (serial >= m_NextSerial)
                Flush();   // sorgu henüz gönderilmemiş komut listesinde
            else
                WaitSerial(serial);
        }
        samples = m_OcclusionData[index];
        return true;
    }

    void Core::Shutdown()
    {
        if (!m_Device)
        {
            if (m_D3D12)
            {
                FreeLibrary(m_D3D12);
                m_D3D12 = nullptr;
            }
            return;
        }
        if (m_ListOpen)
            Submit();
        WaitSerial(m_NextSerial - 1);
        for (Frame& f : m_Frames)
        {
            for (IUnknown* o : f.Garbage)
                o->Release();
            f.Garbage.clear();
            for (auto& c : f.Chunks)
                if (c.Resource)
                    c.Resource->Release();
            f.Chunks.clear();
            Release(f.Allocator);
        }
        ReleaseBackbuffers();
        Release(m_Swapchain);
        Release(m_Factory);
        if (m_QueryReadback)
            m_QueryReadback->Unmap(0, nullptr);
        m_QueryData = nullptr;
        Release(m_QueryReadback);
        if (m_OcclusionReadback)
            m_OcclusionReadback->Unmap(0, nullptr);
        m_OcclusionData = nullptr;
        Release(m_OcclusionReadback);
        Release(m_OcclusionHeap);
        Release(m_QueryHeap);
        Release(m_ShaderHeap);
        Release(m_SamplerHeap);
        m_SamplerTables.clear();
        for (auto& p : m_Pools)
            p.Shutdown();
        for (auto& r : m_BackbufferRTV)
            r = CpuDescriptor{};
        Release(m_RootSignature);
        Release(m_List);
        Release(m_Fence);
        Release(m_Queue);
        Release(m_Device);
        if (m_FenceEvent)
        {
            CloseHandle(m_FenceEvent);
            m_FenceEvent = nullptr;
        }
        if (m_D3D12)
        {
            FreeLibrary(m_D3D12);
            m_D3D12 = nullptr;
        }
        m_ListOpen = false;
    }
}

#endif
