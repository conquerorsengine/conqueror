#ifdef CQ_D3D12_ENABLED

#include "D12GL.h"
#include "Core/Logging/Log.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace Conqueror::D12GL
{
    using DX12::Core;
    using DXCommon::FormatInfo;
    using DXCommon::FormatOf;
    using DXCommon::MaxLevels;

    namespace
    {
        constexpr D3D12_RESOURCE_STATES kShaderRead = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        constexpr D3D12_RESOURCE_STATES kBufferRead = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER | D3D12_RESOURCE_STATE_INDEX_BUFFER;

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

        UINT FormatSupport(DXGI_FORMAT f)
        {
            if (f == DXGI_FORMAT_UNKNOWN)
                return 0;
            D3D12_FEATURE_DATA_FORMAT_SUPPORT fs{ f, D3D12_FORMAT_SUPPORT1_NONE, D3D12_FORMAT_SUPPORT2_NONE };
            if (FAILED(Core::Get().Device()->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT, &fs, sizeof(fs))))
                return 0;
            return (UINT)fs.Support1;
        }

        UINT Subresource(const Texture& t, int level, int layer) { return (UINT)level + (UINT)layer * t.Levels; }

        uint64_t AlignUp(uint64_t v, uint64_t a) { return (v + a - 1) / a * a; }
    }

    Context& Ctx()
    {
        static Context c;
        return c;
    }

    Texture* GetTexture(uint32_t name)
    {
        auto& c = Ctx();
        auto it = c.Textures.find(name);
        return it == c.Textures.end() ? nullptr : it->second.get();
    }

    Texture* GetRenderbuffer(uint32_t name)
    {
        auto& c = Ctx();
        auto it = c.Renderbuffers.find(name);
        return it == c.Renderbuffers.end() ? nullptr : it->second.get();
    }

    Buffer* GetBuffer(uint32_t name)
    {
        auto& c = Ctx();
        auto it = c.Buffers.find(name);
        return it == c.Buffers.end() ? nullptr : it->second.get();
    }

    Framebuffer* GetFramebuffer(uint32_t name)
    {
        auto& c = Ctx();
        if (name == 0)
            return &c.DefaultFB;
        auto it = c.Framebuffers.find(name);
        return it == c.Framebuffers.end() ? nullptr : it->second.get();
    }

    Program* GetProgram(uint32_t name)
    {
        auto& c = Ctx();
        auto it = c.Programs.find(name);
        return it == c.Programs.end() ? nullptr : it->second.get();
    }

    Texture* AttachmentTexture(const Attachment& a)
    {
        auto& c = Ctx();
        if (a.Texture == 0xFFFFFFF0u) return &c.BackColor;
        if (a.Texture == 0xFFFFFFF1u) return &c.BackDepth;
        if (a.Texture) return GetTexture(a.Texture);
        if (a.Renderbuffer) return GetRenderbuffer(a.Renderbuffer);
        return nullptr;
    }

    void Transition(Texture& t, D3D12_RESOURCE_STATES state)
    {
        if (!t.Image || t.State == state)
            return;
        Core::Get().Barrier(t.Image, t.State, state);
        t.State = state;
    }

    void DestroyTextureImage(Texture& t)
    {
        // Bağlı çıkış görünümleri serbest kalacak: birleştirici önbelleği geçersiz
        EndRendering();
        auto& core = Core::Get();
        core.Pool(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV).Free(t.SRV);
        for (auto& [k, v] : t.RTVs)
            core.Pool(D3D12_DESCRIPTOR_HEAP_TYPE_RTV).Free(v);
        for (auto& [k, v] : t.DSVs)
            core.Pool(D3D12_DESCRIPTOR_HEAP_TYPE_DSV).Free(v);
        t.RTVs.clear();
        t.DSVs.clear();
        DeferRelease(t.Image);
        t.State = D3D12_RESOURCE_STATE_COMMON;
        t.SRVLevels = 0;
        t.DefinedLevels = 0;
    }

    bool AllocateTexture(Texture& t, GLenum target, GLenum internalFormat, uint32_t w, uint32_t h, uint32_t levels, uint32_t samples)
    {
        auto& core = Core::Get();
        DestroyTextureImage(t);
        const FormatInfo fi = FormatOf(internalFormat);
        t.Target = target;
        t.InternalFormat = internalFormat;
        t.Format = fi.Resource;
        t.SRVFormat = fi.SRV;
        t.RTVFormat = fi.RTV;
        t.DSVFormat = fi.DSV;
        t.Depth = fi.Depth;
        t.Stencil = fi.Stencil;
        t.Integer = fi.Integer;
        t.Width = std::max(1u, w);
        t.Height = std::max(1u, h);
        t.Levels = std::max(1u, std::min(levels, MaxLevels(t.Width, t.Height)));
        t.Layers = target == GL_TEXTURE_CUBE_MAP ? 6u : 1u;
        t.Samples = std::max(1u, samples);
        if (t.Samples > 1)
            t.Levels = 1;

        const UINT srvSupport = FormatSupport(fi.SRV);
        t.Sampleable = (srvSupport & (D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE | D3D12_FORMAT_SUPPORT1_SHADER_LOAD)) != 0;
        t.Renderable = !fi.Depth && (FormatSupport(fi.RTV) & D3D12_FORMAT_SUPPORT1_RENDER_TARGET);
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = t.Width;
        desc.Height = t.Height;
        desc.DepthOrArraySize = (UINT16)t.Layers;
        desc.MipLevels = (UINT16)t.Levels;
        desc.Format = fi.Resource;
        desc.SampleDesc.Count = t.Samples;
        desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        if (fi.Depth)
            desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        else if (t.Renderable)
            desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        const D3D12_RESOURCE_STATES initial = fi.Depth ? D3D12_RESOURCE_STATE_DEPTH_WRITE : t.Renderable ? D3D12_RESOURCE_STATE_RENDER_TARGET : D3D12_RESOURCE_STATE_COPY_DEST;
        const D3D12_HEAP_PROPERTIES hp = HeapProps(D3D12_HEAP_TYPE_DEFAULT);
        if (FAILED(core.Device()->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &desc, initial, nullptr, __uuidof(ID3D12Resource), (void**)&t.Image)) || !t.Image)
        {
            CQ_CORE_ERROR("[D12GL] Cannot create {0}x{1} texture (format {2}, {3} samples)", t.Width, t.Height, (int)fi.Resource, t.Samples);
            t.Image = nullptr;
            return false;
        }
        t.State = initial;
        // GL'de tanımsız; temizlemek tutarlı sonuç verir (VkGL / DxGL ile aynı)
        auto* list = core.List();
        const float zero[4] = { 0, 0, 0, 0 };
        for (uint32_t layer = 0; layer < t.Layers; ++layer)
        {
            if (fi.Depth)
                list->ClearDepthStencilView(GetDSV(t, 0, (int)layer), D3D12_CLEAR_FLAG_DEPTH | (fi.Stencil ? D3D12_CLEAR_FLAG_STENCIL : (D3D12_CLEAR_FLAGS)0), 1.0f, 0, 0,
                                            nullptr);
            else if (t.Renderable)
                list->ClearRenderTargetView(GetRTV(t, 0, (int)layer), zero, 0, nullptr);
        }
        return true;
    }

    CpuDescriptor CreateSRV(Texture& t, int mostDetailedMip, int mipLevels, int layer)
    {
        CpuDescriptor d = Core::Get().Pool(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV).Allocate();
        if (!d.Valid() || !t.Image)
            return d;
        D3D12_SHADER_RESOURCE_VIEW_DESC vd{};
        vd.Format = t.SRVFormat;
        vd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        if (t.Samples > 1)
            vd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMS;
        else if (layer >= 0 && t.Layers > 1)
        {
            vd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
            vd.Texture2DArray.MostDetailedMip = (UINT)mostDetailedMip;
            vd.Texture2DArray.MipLevels = (UINT)mipLevels;
            vd.Texture2DArray.FirstArraySlice = (UINT)layer;
            vd.Texture2DArray.ArraySize = 1;
        }
        else if (t.Layers == 6)
        {
            vd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
            vd.TextureCube.MostDetailedMip = (UINT)mostDetailedMip;
            vd.TextureCube.MipLevels = (UINT)mipLevels;
        }
        else
        {
            vd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            vd.Texture2D.MostDetailedMip = (UINT)mostDetailedMip;
            vd.Texture2D.MipLevels = (UINT)mipLevels;
        }
        Core::Get().Device()->CreateShaderResourceView(t.Image, &vd, d.Handle);
        return d;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE GetSRV(Texture& t)
    {
        auto& c = Ctx();
        if (!t.Image || !t.Sampleable)
            return c.NullSRV.Handle;
        int levels = (int)t.Levels;
        if (!t.Immutable && t.DefinedLevels)
        {
            levels = 0;
            while (levels < (int)t.Levels && (t.DefinedLevels & (1u << levels)))
                ++levels;
            levels = std::max(levels, 1);
        }
        levels = std::clamp(std::min(levels, t.MaxLevel + 1), 1, (int)t.Levels);
        if (t.SRV.Valid() && t.SRVLevels == levels)
            return t.SRV.Handle;
        // Tablolara her çizimde kopyalanır: aynı yuvaya yeniden yazmak güvenli
        if (!t.SRV.Valid())
            t.SRV = Core::Get().Pool(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV).Allocate();
        if (!t.SRV.Valid())
            return c.NullSRV.Handle;
        D3D12_SHADER_RESOURCE_VIEW_DESC vd{};
        vd.Format = t.SRVFormat;
        vd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        if (t.Samples > 1)
            vd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMS;
        else if (t.Layers == 6)
        {
            vd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
            vd.TextureCube.MipLevels = (UINT)levels;
        }
        else
        {
            vd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            vd.Texture2D.MipLevels = (UINT)levels;
        }
        Core::Get().Device()->CreateShaderResourceView(t.Image, &vd, t.SRV.Handle);
        t.SRVLevels = levels;
        return t.SRV.Handle;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE GetRTV(Texture& t, int level, int layer)
    {
        auto& c = Ctx();
        if (!t.Image || t.Depth || !t.Renderable)
            return c.NullRTV.Handle;
        const uint32_t key = (uint32_t)level | ((uint32_t)layer << 8);
        if (auto it = t.RTVs.find(key); it != t.RTVs.end())
            return it->second.Handle;
        CpuDescriptor d = Core::Get().Pool(D3D12_DESCRIPTOR_HEAP_TYPE_RTV).Allocate();
        if (!d.Valid())
            return c.NullRTV.Handle;
        D3D12_RENDER_TARGET_VIEW_DESC vd{};
        vd.Format = t.RTVFormat;
        if (t.Samples > 1)
            vd.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMS;
        else if (t.Layers > 1)
        {
            vd.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
            vd.Texture2DArray.MipSlice = (UINT)level;
            vd.Texture2DArray.FirstArraySlice = (UINT)layer;
            vd.Texture2DArray.ArraySize = 1;
        }
        else
        {
            vd.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
            vd.Texture2D.MipSlice = (UINT)level;
        }
        Core::Get().Device()->CreateRenderTargetView(t.Image, &vd, d.Handle);
        t.RTVs[key] = d;
        return d.Handle;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE GetDSV(Texture& t, int level, int layer)
    {
        if (!t.Image || !t.Depth)
            return {};
        const uint32_t key = (uint32_t)level | ((uint32_t)layer << 8);
        if (auto it = t.DSVs.find(key); it != t.DSVs.end())
            return it->second.Handle;
        CpuDescriptor d = Core::Get().Pool(D3D12_DESCRIPTOR_HEAP_TYPE_DSV).Allocate();
        if (!d.Valid())
            return {};
        D3D12_DEPTH_STENCIL_VIEW_DESC vd{};
        vd.Format = t.DSVFormat;
        if (t.Samples > 1)
            vd.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DMS;
        else if (t.Layers > 1)
        {
            vd.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
            vd.Texture2DArray.MipSlice = (UINT)level;
            vd.Texture2DArray.FirstArraySlice = (UINT)layer;
            vd.Texture2DArray.ArraySize = 1;
        }
        else
        {
            vd.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
            vd.Texture2D.MipSlice = (UINT)level;
        }
        Core::Get().Device()->CreateDepthStencilView(t.Image, &vd, d.Handle);
        t.DSVs[key] = d;
        return d.Handle;
    }

    D3D12_CPU_DESCRIPTOR_HANDLE GetSampler(const Texture& p, bool integer, bool comparison)
    {
        auto& c = Ctx();
        const DXCommon::SamplerDesc d = DXCommon::MakeSamplerDesc(p, integer, comparison);
        if (auto it = c.SamplerCache.find(d.Key); it != c.SamplerCache.end())
            return it->second.Handle;
        CpuDescriptor slot = Core::Get().Pool(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER).Allocate();
        if (!slot.Valid())
            return c.NullSampler.Handle;
        D3D12_SAMPLER_DESC sd{};
        sd.Filter = (D3D12_FILTER)d.Filter;
        sd.AddressU = (D3D12_TEXTURE_ADDRESS_MODE)d.AddressU;
        sd.AddressV = (D3D12_TEXTURE_ADDRESS_MODE)d.AddressV;
        sd.AddressW = (D3D12_TEXTURE_ADDRESS_MODE)d.AddressW;
        sd.MipLODBias = d.MipLODBias;
        sd.MaxAnisotropy = d.MaxAnisotropy;
        sd.ComparisonFunc = (D3D12_COMPARISON_FUNC)d.ComparisonFunc;
        for (int i = 0; i < 4; ++i)
            sd.BorderColor[i] = d.BorderColor[i];
        sd.MinLOD = d.MinLOD;
        sd.MaxLOD = d.MaxLOD;
        Core::Get().Device()->CreateSampler(&sd, slot.Handle);
        c.SamplerCache[d.Key] = slot;
        return slot.Handle;
    }

    void UploadTexture(Texture& t, int level, int layer, int x, int y, int w, int h, GLenum format, GLenum type, const void* data)
    {
        if (!t.Image || !data || w <= 0 || h <= 0)
            return;
        if (t.Depth || t.Samples > 1)
        {
            CQ_CORE_WARN("[D12GL] Uploads to depth / multisample textures are not supported");
            return;
        }
        auto& c = Ctx();
        auto& core = Core::Get();
        EndRendering();
        static thread_local std::vector<uint8_t> staging;
        size_t srcPitch = 0;
        const uint8_t* src = DXCommon::ConvertUpload(t.Format, w, h, format, type, data, c.S.UnpackRowLength, c.S.UnpackAlignment, staging, srcPitch);
        const uint64_t rowBytes = (uint64_t)w * DXCommon::TexelBytes(t.Format);
        const uint64_t pitch = AlignUp(rowBytes, D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);
        DX12::UploadAlloc a = core.AllocUpload(pitch * (uint64_t)h, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);
        if (!a.Cpu)
            return;
        for (int row = 0; row < h; ++row)
            std::memcpy(a.Cpu + pitch * (uint64_t)row, src + srcPitch * (size_t)row, (size_t)rowBytes);
        Transition(t, D3D12_RESOURCE_STATE_COPY_DEST);
        D3D12_TEXTURE_COPY_LOCATION dst{};
        dst.pResource = t.Image;
        dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst.SubresourceIndex = Subresource(t, level, layer);
        D3D12_TEXTURE_COPY_LOCATION srcLoc{};
        srcLoc.pResource = a.Resource;
        srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        srcLoc.PlacedFootprint.Offset = a.Offset;
        srcLoc.PlacedFootprint.Footprint = { t.Format, (UINT)w, (UINT)h, 1, (UINT)pitch };
        core.List()->CopyTextureRegion(&dst, (UINT)x, (UINT)y, 0, &srcLoc, nullptr);
    }

    void ReadImage(Texture& t, int level, int layer, int x, int y, int w, int h, GLenum format, GLenum type, void* out, int packAlignment)
    {
        if (!t.Image || !out || w <= 0 || h <= 0)
            return;
        auto& core = Core::Get();
        auto* dev = core.Device();
        EndRendering();
        ID3D12Resource* source = t.Image;
        UINT sourceSub = Subresource(t, level, layer);
        ID3D12Resource* resolved = nullptr;
        const D3D12_HEAP_PROPERTIES defaultHeap = HeapProps(D3D12_HEAP_TYPE_DEFAULT);
        if (t.Samples > 1)
        {
            if (t.Depth)
            {
                CQ_CORE_WARN("[D12GL] Reading a multisample depth texture is not supported");
                return;
            }
            D3D12_RESOURCE_DESC rd = t.Image->GetDesc();
            rd.SampleDesc.Count = 1;
            rd.SampleDesc.Quality = 0;
            rd.Flags = D3D12_RESOURCE_FLAG_NONE;
            if (FAILED(dev->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_RESOLVE_DEST, nullptr, __uuidof(ID3D12Resource),
                                                   (void**)&resolved)))
                return;
            Transition(t, D3D12_RESOURCE_STATE_RESOLVE_SOURCE);
            core.List()->ResolveSubresource(resolved, 0, t.Image, 0, t.RTVFormat);
            core.Barrier(resolved, D3D12_RESOURCE_STATE_RESOLVE_DEST, D3D12_RESOURCE_STATE_COPY_SOURCE);
            source = resolved;
            sourceSub = 0;
        }
        else
            Transition(t, D3D12_RESOURCE_STATE_COPY_SOURCE);

        // Derinlik kaynakları yalnızca tüm alt kaynak olarak kopyalanabilir
        const bool whole = t.Depth;
        D3D12_RESOURCE_DESC srcDesc = source->GetDesc();
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};
        UINT64 total = 0;
        dev->GetCopyableFootprints(&srcDesc, sourceSub, 1, 0, &fp, nullptr, nullptr, &total);
        if (!whole)
        {
            fp.Footprint.Width = (UINT)w;
            fp.Footprint.Height = (UINT)h;
            fp.Footprint.RowPitch = (UINT)AlignUp((uint64_t)w * DXCommon::TexelBytes(fp.Footprint.Format), D3D12_TEXTURE_DATA_PITCH_ALIGNMENT);
            total = (UINT64)fp.Footprint.RowPitch * (UINT64)h;
        }
        fp.Offset = 0;
        const D3D12_HEAP_PROPERTIES readbackHeap = HeapProps(D3D12_HEAP_TYPE_READBACK);
        const D3D12_RESOURCE_DESC bd = BufferDesc(std::max<UINT64>(total, 256));
        ID3D12Resource* readback = nullptr;
        if (FAILED(dev->CreateCommittedResource(&readbackHeap, D3D12_HEAP_FLAG_NONE, &bd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, __uuidof(ID3D12Resource),
                                               (void**)&readback)))
        {
            DeferRelease(resolved);
            return;
        }
        D3D12_TEXTURE_COPY_LOCATION dst{};
        dst.pResource = readback;
        dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dst.PlacedFootprint = fp;
        D3D12_TEXTURE_COPY_LOCATION src{};
        src.pResource = source;
        src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        src.SubresourceIndex = sourceSub;
        if (whole)
            core.List()->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        else
        {
            D3D12_BOX box{ (UINT)x, (UINT)y, 0, (UINT)(x + w), (UINT)(y + h), 1 };
            core.List()->CopyTextureRegion(&dst, 0, 0, 0, &src, &box);
        }
        core.Flush();
        uint8_t* mapped = nullptr;
        D3D12_RANGE range{ 0, (SIZE_T)total };
        if (SUCCEEDED(readback->Map(0, &range, (void**)&mapped)) && mapped)
        {
            const int ox = whole ? x : 0, oy = whole ? y : 0;
            const uint8_t* origin = mapped + (size_t)fp.Footprint.RowPitch * (size_t)oy + (size_t)ox * DXCommon::TexelBytes(fp.Footprint.Format);
            DXCommon::ConvertReadback(fp.Footprint.Format, origin, fp.Footprint.RowPitch, w, h, format, type, out, packAlignment);
            D3D12_RANGE none{ 0, 0 };
            readback->Unmap(0, &none);
        }
        readback->Release();   // GPU boşta (Flush)
        SafeRelease(resolved);
    }

    bool GrowLevels(Texture& t, uint32_t levels)
    {
        levels = std::min(levels, MaxLevels(t.Width, t.Height));
        if (!t.Image || levels <= t.Levels || t.Samples > 1)
            return t.Image != nullptr;
        auto& core = Core::Get();
        ID3D12Resource* old = t.Image;
        old->AddRef();   // AllocateTexture eskiyi bırakır; kopya için tut
        const D3D12_RESOURCE_STATES oldState = t.State;
        const uint32_t oldLevels = t.Levels;
        const uint32_t defined = t.DefinedLevels;
        if (!AllocateTexture(t, t.Target, t.InternalFormat, t.Width, t.Height, levels, 1))
        {
            core.DeferRelease(old);
            return false;
        }
        core.Barrier(old, oldState, D3D12_RESOURCE_STATE_COPY_SOURCE);
        Transition(t, D3D12_RESOURCE_STATE_COPY_DEST);
        for (uint32_t l = 0; l < oldLevels; ++l)
        {
            if (!(defined & (1u << l)))
                continue;
            for (uint32_t layer = 0; layer < t.Layers; ++layer)
            {
                D3D12_TEXTURE_COPY_LOCATION dst{};
                dst.pResource = t.Image;
                dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                dst.SubresourceIndex = Subresource(t, (int)l, (int)layer);
                D3D12_TEXTURE_COPY_LOCATION src{};
                src.pResource = old;
                src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                src.SubresourceIndex = l + layer * oldLevels;
                core.List()->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
            }
        }
        core.DeferRelease(old);
        t.DefinedLevels = defined;
        return true;
    }

    void ClearTexture(Texture& t, int level, GLenum format, GLenum type, const void* data)
    {
        if (!t.Image || level < 0 || level >= (int)t.Levels)
            return;
        auto* list = Core::Get().List();
        EndRendering();
        float color[4], depth = 1.0f;
        uint8_t stencil = 0;
        DXCommon::ParseClearValue(t.Depth, t.Integer, format, type, data, color, depth, stencil);
        if (t.Depth)
        {
            Transition(t, D3D12_RESOURCE_STATE_DEPTH_WRITE);
            for (uint32_t layer = 0; layer < t.Layers; ++layer)
                list->ClearDepthStencilView(GetDSV(t, level, (int)layer), D3D12_CLEAR_FLAG_DEPTH | (t.Stencil ? D3D12_CLEAR_FLAG_STENCIL : (D3D12_CLEAR_FLAGS)0),
                                            depth, stencil, 0, nullptr);
        }
        else if (t.Renderable)
        {
            Transition(t, D3D12_RESOURCE_STATE_RENDER_TARGET);
            for (uint32_t layer = 0; layer < t.Layers; ++layer)
                list->ClearRenderTargetView(GetRTV(t, level, (int)layer), color, 0, nullptr);
        }
        t.DefinedLevels |= 1u << level;
    }

    // ── Arabellekler ──
    D3D12_GPU_VIRTUAL_ADDRESS BufferAddress(Buffer& b, uint64_t minSize, uint64_t* size)
    {
        auto& core = Core::Get();
        const uint64_t frame = core.FrameSerial();
        if (b.Dirty)
        {
            ++b.Version;
            b.Dirty = false;
            if (b.WriteFrame == frame)
                ++b.WritesThisFrame;
            else
            {
                b.WriteFrame = frame;
                b.WritesThisFrame = 1;
            }
        }
        const uint64_t need = AlignUp(std::max<uint64_t>({ (uint64_t)b.Shadow.size(), minSize, 16 }), 256);
        if (b.GpuVersion == b.Version && b.GpuSize >= need && (!b.GpuIsRing || b.RingFrame == frame))
        {
            if (size)
                *size = b.GpuSize;
            return b.GpuAddress;
        }
        const size_t bytes = std::min<size_t>(b.Shadow.size(), (size_t)need);
        // Bu kare yazılan devingen veri (ya da kare içinde birden çok yazılan) halkadan; kararlı veri kalıcı arabellekte
        const bool ring = b.WriteFrame == frame && (b.WritesThisFrame > 1 || b.Usage != GL_STATIC_DRAW);
        DX12::UploadAlloc a = core.AllocUpload(need, 256);
        if (!a.Cpu)
            return 0;
        if (bytes)
            std::memcpy(a.Cpu, b.Shadow.data(), bytes);
        if (need > bytes)
            std::memset(a.Cpu + bytes, 0, (size_t)(need - bytes));
        if (ring)
        {
            b.GpuAddress = a.Gpu;
            b.GpuIsRing = true;
            b.RingFrame = frame;
        }
        else
        {
            if (!b.Dedicated || b.DedicatedSize < need)
            {
                DeferRelease(b.Dedicated);
                const uint64_t capacity = AlignUp(need, 65536);
                const D3D12_HEAP_PROPERTIES hp = HeapProps(D3D12_HEAP_TYPE_DEFAULT);
                const D3D12_RESOURCE_DESC rd = BufferDesc(capacity);
                if (FAILED(core.Device()->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, __uuidof(ID3D12Resource),
                                                                 (void**)&b.Dedicated)))
                {
                    b.Dedicated = nullptr;
                    b.DedicatedSize = 0;
                    b.GpuAddress = a.Gpu;   // kalıcı arabellek yok: halkadan (bu kare)
                    b.GpuIsRing = true;
                    b.RingFrame = frame;
                    b.GpuVersion = b.Version;
                    b.GpuSize = need;
                    if (size)
                        *size = need;
                    return b.GpuAddress;
                }
                b.DedicatedSize = capacity;
                b.DedicatedState = D3D12_RESOURCE_STATE_COPY_DEST;
            }
            // Kopya komut listesinde sıralanır: önceki çizimler eski içeriği görür (GL anlamı)
            core.Barrier(b.Dedicated, b.DedicatedState, D3D12_RESOURCE_STATE_COPY_DEST);
            core.List()->CopyBufferRegion(b.Dedicated, 0, a.Resource, a.Offset, need);
            core.Barrier(b.Dedicated, D3D12_RESOURCE_STATE_COPY_DEST, kBufferRead);
            b.DedicatedState = kBufferRead;
            b.GpuAddress = b.Dedicated->GetGPUVirtualAddress();
            b.GpuIsRing = false;
        }
        b.GpuVersion = b.Version;
        b.GpuSize = ring ? need : std::max(need, b.DedicatedSize);
        if (size)
            *size = b.GpuSize;
        return b.GpuAddress;
    }

    void ReleaseBuffer(Buffer& b)
    {
        DeferRelease(b.Dedicated);
        b.DedicatedSize = 0;
        b.DedicatedState = D3D12_RESOURCE_STATE_COMMON;
        b.GpuVersion = 0;
        b.GpuAddress = 0;
        b.GpuSize = 0;
        b.GpuIsRing = false;
    }
}

#endif
