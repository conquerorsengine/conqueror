#pragma once

#ifdef CQ_D3D12_ENABLED

#ifndef NOMINMAX
    #define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_5.h>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

struct GLFWwindow;

namespace Conqueror::DX12
{
    // Kökler (tek kök imzası, her çizimde aynı): b0..b11 kök CBV, t0..t63 tablo, s0..s15 tablo
    constexpr uint32_t kRootCBVs = 12;
    constexpr uint32_t kMaxSRVs = 64;
    constexpr uint32_t kMaxSamplers = 16;
    constexpr uint32_t kRootSRVTable = kRootCBVs;
    constexpr uint32_t kRootSamplerTable = kRootCBVs + 1;

    constexpr uint32_t kFrames = 3;

    struct UploadAlloc
    {
        uint8_t* Cpu = nullptr;
        D3D12_GPU_VIRTUAL_ADDRESS Gpu = 0;
        ID3D12Resource* Resource = nullptr;
        uint64_t Offset = 0;
    };

    struct CpuDescriptor
    {
        D3D12_CPU_DESCRIPTOR_HANDLE Handle{};
        uint32_t Index = UINT32_MAX;
        bool Valid() const { return Index != UINT32_MAX; }
    };

    // Gölgelendiriciye görünmeyen tanımlayıcı yığını (sabit boy, boş liste)
    class DescriptorPool
    {
    public:
        bool Init(ID3D12Device* device, D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t count);
        void Shutdown();
        CpuDescriptor Allocate();
        void Free(CpuDescriptor& d);

    private:
        ID3D12DescriptorHeap* m_Heap = nullptr;
        D3D12_CPU_DESCRIPTOR_HANDLE m_Start{};
        uint32_t m_Increment = 0, m_Count = 0, m_Next = 0;
        std::vector<uint32_t> m_FreeList;
        D3D12_DESCRIPTOR_HEAP_TYPE m_Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    };

    // Direct3D 12 cihazı, komut kuyruğu, kare halkası, yükleme halkası, tanımlayıcı yığınları ve takas zinciri.
    // GL çeviri katmanı (D12GL) tek bir komut listesine kaydeder; kare sonunda gönderilir.
    class Core
    {
    public:
        static Core& Get();

        bool Init(GLFWwindow* window, std::string& error);
        void Shutdown();
        bool IsInitialized() const { return m_Device != nullptr; }

        ID3D12Device* Device() const { return m_Device; }
        ID3D12GraphicsCommandList* List() const { return m_List; }
        ID3D12RootSignature* RootSignature() const { return m_RootSignature; }
        const std::string& DeviceName() const { return m_DeviceName; }
        uint32_t VendorId() const { return m_VendorId; }
        uint64_t VideoMemory() const { return m_VideoMemory; }
        uint32_t FramebufferWidth() const;
        uint32_t FramebufferHeight() const;
        void SetVSync(bool enabled) { m_VSync = enabled; }

        // Gönderim sırası: kaydedilen komutlar bu değer işaretlenince biter. Kare sayacı her sunumda artar.
        uint64_t SubmitSerial() const { return m_NextSerial; }
        uint64_t CompletedSerial() const;
        uint64_t FrameSerial() const { return m_FrameSerial; }
        // Her komut listesi yeniden açıldığında artar (D12GL bağlı durum önbelleğini sıfırlar)
        uint64_t ListEpoch() const { return m_ListEpoch; }

        // Kayıtlı komutları gönder, GPU'yu bekle, listeyi yeniden aç (geri okuma / halka taşması)
        void Flush();
        // Kaynak GPU işi bittiğinde bırakılır
        void DeferRelease(IUnknown* object);

        UploadAlloc AllocUpload(uint64_t size, uint64_t alignment);

        DescriptorPool& Pool(D3D12_DESCRIPTOR_HEAP_TYPE type) { return m_Pools[type]; }
        // Gölgelendiriciye görünür CBV/SRV tablosu (bu karenin halkasından): kaynak tanımlayıcılar kopyalanır
        D3D12_GPU_DESCRIPTOR_HANDLE CopyResourceTable(const D3D12_CPU_DESCRIPTOR_HANDLE* source, uint32_t count);
        // Örnekleyici tablosu (anahtar başına kalıcı önbellek; 2048 sınırı dolunca boşaltılır)
        D3D12_GPU_DESCRIPTOR_HANDLE SamplerTable(const D3D12_CPU_DESCRIPTOR_HANDLE* source, uint32_t count, uint64_t key);
        // Başlangıç durumu: kök imzası + görünür yığınlar (yeni listede ve yardımcı çizimden sonra)
        void BindRoot();

        // Takas zinciri: geçerli arka arabellek (RENDER_TARGET durumuna getirilmiş) ve sunum
        ID3D12Resource* BeginBackbuffer(D3D12_CPU_DESCRIPTOR_HANDLE& rtv);
        void Present();

        // Zaman damgaları (GL_TIMESTAMP)
        uint32_t WriteTimestamp();
        bool ReadTimestamp(uint32_t index, uint64_t serial, bool wait, uint64_t& ns);
        // Örtüşme sorguları (GL_SAMPLES_PASSED): başlat / bitir (aynı komut listesinde), sonuç geri okuma arabelleğinden
        uint32_t BeginOcclusion();
        void EndOcclusion(uint32_t index);
        bool ReadOcclusion(uint32_t index, uint64_t serial, bool wait, uint64_t& samples);

        // HLSL -> bayt kodu (d3dcompiler_47)
        static bool CompileHLSL(const std::string& source, const char* name, const char* entry, const char* target, std::vector<uint8_t>& out, std::string& errors);

        void Barrier(ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after, UINT subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES);

    private:
        struct RingChunk
        {
            ID3D12Resource* Resource = nullptr;
            uint8_t* Cpu = nullptr;
            D3D12_GPU_VIRTUAL_ADDRESS Gpu = 0;
            uint64_t Size = 0;
        };
        struct Frame
        {
            ID3D12CommandAllocator* Allocator = nullptr;
            uint64_t Serial = 0;                          // son gönderimin işareti
            std::vector<RingChunk> Chunks;                // ilk parça kalıcı, taşmalar kare bitince bırakılır
            size_t Chunk = 0;
            uint64_t Offset = 0;
            std::vector<IUnknown*> Garbage;
            uint32_t DescriptorCursor = 0;
        };

        bool CreateSwapchain(std::string& error);
        void AcquireBackbuffers();
        void ReleaseBackbuffers();
        void Submit();
        void WaitSerial(uint64_t serial);
        void BeginFrame(Frame& f);
        void OpenList(Frame& f);
        bool NewChunk(Frame& f, uint64_t size);

        GLFWwindow* m_Window = nullptr;
        HWND m_Hwnd = nullptr;
        HMODULE m_D3D12 = nullptr;
        ID3D12Device* m_Device = nullptr;
        ID3D12CommandQueue* m_Queue = nullptr;
        ID3D12GraphicsCommandList* m_List = nullptr;
        ID3D12Fence* m_Fence = nullptr;
        HANDLE m_FenceEvent = nullptr;
        uint64_t m_NextSerial = 1;
        uint64_t m_FrameSerial = 1;
        uint64_t m_ListEpoch = 1;
        bool m_ListOpen = false;
        Frame m_Frames[kFrames];
        uint32_t m_FrameIndex = 0;
        ID3D12RootSignature* m_RootSignature = nullptr;

        DescriptorPool m_Pools[D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES];
        ID3D12DescriptorHeap* m_ShaderHeap = nullptr;    // CBV/SRV/UAV, kare başına bölüm
        ID3D12DescriptorHeap* m_SamplerHeap = nullptr;
        uint32_t m_ShaderHeapSize = 0, m_ShaderIncrement = 0, m_SamplerIncrement = 0, m_SamplerCursor = 0;
        std::unordered_map<uint64_t, D3D12_GPU_DESCRIPTOR_HANDLE> m_SamplerTables;

        IDXGIFactory4* m_Factory = nullptr;
        IDXGISwapChain3* m_Swapchain = nullptr;
        static constexpr uint32_t kBackbuffers = 3;
        ID3D12Resource* m_Backbuffers[kBackbuffers] = {};
        CpuDescriptor m_BackbufferRTV[kBackbuffers];
        uint32_t m_SwapWidth = 0, m_SwapHeight = 0;
        uint32_t m_CurrentBackbuffer = 0;
        bool m_BackbufferAcquired = false;
        bool m_VSync = true;
        bool m_Tearing = false;

        ID3D12QueryHeap* m_QueryHeap = nullptr;
        ID3D12Resource* m_QueryReadback = nullptr;
        uint64_t* m_QueryData = nullptr;
        uint32_t m_QueryCursor = 0;
        uint64_t m_TimestampFrequency = 0;
        ID3D12QueryHeap* m_OcclusionHeap = nullptr;
        ID3D12Resource* m_OcclusionReadback = nullptr;
        uint64_t* m_OcclusionData = nullptr;
        uint32_t m_OcclusionCursor = 0;

        std::string m_DeviceName;
        uint32_t m_VendorId = 0;
        uint64_t m_VideoMemory = 0;
    };
}

#endif
