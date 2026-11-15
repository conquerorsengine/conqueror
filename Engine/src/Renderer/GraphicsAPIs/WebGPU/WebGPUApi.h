#pragma once

#ifdef CQ_WEBGPU_ENABLED

// WebGPU C API (webgpu.h) işlev tablosu. Masaüstünde uygulama çalışma anında açılır (Vulkan yükleyicisi gibi: Dawn'ın
// libwebgpu_dawn'ı ya da wgpu-native'in libwgpu_native'i; oyun onsuz da açılır ve OpenGL'e düşer). Web'de tarayıcının WebGPU'su
// Emscripten'in emdawnwebgpu portuyla doğrudan bağlanır. Motor her çağrıyı W().Xxx(...) ile yapar.

#if defined(__EMSCRIPTEN__)
    #include <webgpu/webgpu.h>
#else
    #define WGPU_SKIP_DECLARATIONS
    #include <webgpu/webgpu.h>
#endif

#include <string>

#define CQ_WGPU_PROCS(X) \
    X(CreateInstance) X(InstanceRequestAdapter) X(InstanceProcessEvents) X(InstanceWaitAny) X(InstanceCreateSurface) X(InstanceRelease) \
    X(AdapterRequestDevice) X(AdapterGetInfo) X(AdapterInfoFreeMembers) X(AdapterGetLimits) X(AdapterHasFeature) X(AdapterRelease) \
    X(DeviceGetQueue) X(DeviceGetLimits) X(DeviceHasFeature) X(DeviceRelease) X(DeviceCreateBuffer) X(DeviceCreateTexture) \
    X(DeviceCreateSampler) X(DeviceCreateShaderModule) X(DeviceCreateBindGroupLayout) X(DeviceCreatePipelineLayout) \
    X(DeviceCreateRenderPipeline) X(DeviceCreateBindGroup) X(DeviceCreateCommandEncoder) X(DevicePushErrorScope) X(DevicePopErrorScope) \
    X(QueueSubmit) X(QueueWriteBuffer) X(QueueWriteTexture) X(QueueOnSubmittedWorkDone) X(QueueRelease) \
    X(BufferMapAsync) X(BufferGetConstMappedRange) X(BufferUnmap) X(BufferDestroy) X(BufferRelease) \
    X(TextureCreateView) X(TextureDestroy) X(TextureRelease) X(TextureViewRelease) X(SamplerRelease) X(ShaderModuleRelease) \
    X(BindGroupLayoutRelease) X(PipelineLayoutRelease) X(RenderPipelineRelease) X(BindGroupRelease) \
    X(CommandEncoderBeginRenderPass) X(CommandEncoderCopyBufferToBuffer) X(CommandEncoderCopyTextureToTexture) \
    X(CommandEncoderCopyTextureToBuffer) X(CommandEncoderFinish) X(CommandEncoderRelease) X(CommandBufferRelease) \
    X(RenderPassEncoderSetPipeline) X(RenderPassEncoderSetBindGroup) X(RenderPassEncoderSetVertexBuffer) X(RenderPassEncoderSetIndexBuffer) \
    X(RenderPassEncoderDraw) X(RenderPassEncoderDrawIndexed) X(RenderPassEncoderSetViewport) X(RenderPassEncoderSetScissorRect) \
    X(RenderPassEncoderSetBlendConstant) X(RenderPassEncoderSetStencilReference) X(RenderPassEncoderEnd) X(RenderPassEncoderRelease) \
    X(SurfaceConfigure) X(SurfaceGetCurrentTexture) X(SurfacePresent) X(SurfaceGetCapabilities) X(SurfaceCapabilitiesFreeMembers) \
    X(SurfaceUnconfigure) X(SurfaceRelease)

namespace Conqueror::WGPU
{
    struct Procs
    {
#define CQ_WGPU_MEMBER(name) WGPUProc##name name = nullptr;
        CQ_WGPU_PROCS(CQ_WGPU_MEMBER)
#undef CQ_WGPU_MEMBER
    };

    // Çağrı tablosu (Load sonrası dolu)
    Procs& W();
    // Masaüstü: $CQ_WEBGPU_LIBRARY, oyunun yanındaki / sistemdeki libwebgpu_dawn ve libwgpu_native denenir. Web: her zaman true.
    bool Load(std::string& error);
    const std::string& LibraryName();

    inline WGPUStringView Str(const char* s) { return WGPUStringView{ s, WGPU_STRLEN }; }
    inline std::string ToString(WGPUStringView v)
    {
        if (!v.data)
            return {};
        return v.length == WGPU_STRLEN ? std::string(v.data) : std::string(v.data, v.length);
    }
}

#endif
