#pragma once

#ifdef CQ_WEBGPU_ENABLED

// GL -> WebGPU çeviri katmanı. VkGL / MtlGL / DxGL ile aynı ön yüzü (GLEmu/GLFrontEnd.inl) kullanır.
//
// Temel kurallar (MtlGL ile aynı):
//  * Görüntüler GL yönünde tutulur (satır 0 = alt): köşe gölgelendiricisi y'yi çevirir, z'yi [-w,w] -> [0,w] taşır. Böylece
//    gl_FragCoord, glViewport, glScissor, glReadPixels ve dokuya çizim GL ile birebir aynıdır. Sunumda dikey çevrilir.
//  * GL_CCW ön yüz => WGPUFrontFace_CW (y çevrimi sarımı tersler).
// WebGPU'ya özgü:
//  * Gölgelendiriciler GLSL -> SPIR-V (GLEmu yansıtması) -> WGSL (Tint). Bağlamalar küme 0: sabit arabellekler / dokular SPIR-V
//    numarasında, birleşik örnekleyiciden ayrılan örnekleyiciler Tint'in verdiği numarada (WGSL'den okunur).
//  * Arabellek ve doku güncellemeleri kuyruk yazımıdır (wgpuQueueWriteBuffer): bu kare kaydedilmiş çizimlerin eski veriyi görmesi
//    için GPU sürümü kullanılmışsa halkada yeni yer alınır (MtlGL'deki yeniden adlandırma).
//  * Web'de GPU'dan eşzamanlı geri okuma yok (ana iş parçacığı bekleyemez): glReadPixels son tamamlanan okumayı verir.

#include "WebGPUApi.h"
#include "../GLEmu/GLEmuCommon.h"
#include "../GLEmu/GLReflect.h"
#include "../GLEmu/GLTexel.h"

#include <glad/glad.h>

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

struct GLFWwindow;

namespace Conqueror::WgpuGL
{
    struct Texture
    {
        GLenum Target = 0;
        GLenum InternalFormat = 0;
        WGPUTextureFormat Format = WGPUTextureFormat_Undefined;
        GLEmu::TexelKind Kind = GLEmu::TexelKind::RGBA8;
        bool Depth = false, Stencil = false;
        uint32_t Width = 0, Height = 0, Levels = 0, Layers = 1, Samples = 1;
        bool Immutable = false;
        bool Integer = false;
        bool Renderable = false;
        bool Filterable = true;                          // 32 bit float (float32-filterable yoksa) / tamsayı / derinlik: hayır
        uint32_t DefinedLevels = 0;
        WGPUTexture Image = nullptr;
        WGPUTextureView View = nullptr;                  // örnekleme görünümü (tanımlı düzeyler)
        int ViewLevels = 0;
        uint64_t LastUseSubmit = 0;                      // en son kullanıldığı gönderim (yazmadan önce boşaltma)
        // Örnekleyici parametreleri (ön yüz yazar)
        GLenum MinFilter = GL_NEAREST_MIPMAP_LINEAR, MagFilter = GL_LINEAR;
        GLenum WrapS = GL_REPEAT, WrapT = GL_REPEAT, WrapR = GL_REPEAT;
        GLenum CompareMode = GL_NONE, CompareFunc = GL_LEQUAL;
        float MaxAnisotropy = 1.0f, MinLod = -1000.0f, MaxLod = 1000.0f, LodBias = 0.0f;
        int BaseLevel = 0, MaxLevel = 1000;
        float BorderColor[4] = { 0, 0, 0, 0 };
    };

    struct Buffer
    {
        std::vector<uint8_t> Shadow;
        GLenum Usage = GL_STATIC_DRAW;
        bool Dirty = true;
        size_t HighWater = 0;
        uint64_t Version = 1;
        uint64_t GpuVersion = 0;
        WGPUBuffer GpuBuffer = nullptr;                  // geçerli sürüm (kalıcı ya da halka)
        uint64_t GpuOffset = 0, GpuSize = 0;
        bool GpuIsRing = false;
        uint64_t RingFrame = 0;
        WGPUBuffer Dedicated = nullptr;
        uint64_t DedicatedSize = 0;
        uint64_t DedicatedLastUse = 0;                   // son kullanan gönderim
        uint64_t WriteFrame = 0;
        uint32_t WritesThisFrame = 0;
    };

    struct Attachment
    {
        uint32_t Texture = 0;
        uint32_t Renderbuffer = 0;
        int Level = 0;
        int Layer = 0;
        bool Valid() const { return Texture || Renderbuffer; }
    };

    struct Framebuffer
    {
        Attachment Color[8];
        Attachment Depth;
        Attachment Stencil;
        GLenum DrawBuffers[8] = { GL_COLOR_ATTACHMENT0, GL_NONE, GL_NONE, GL_NONE, GL_NONE, GL_NONE, GL_NONE, GL_NONE };
        int NumDrawBuffers = 1;
        GLenum ReadBuffer = GL_COLOR_ATTACHMENT0;
    };

    struct Attrib
    {
        bool Enabled = false;
        GLint Size = 4;
        GLenum Type = GL_FLOAT;
        bool Normalized = false;
        bool Integer = false;
        GLsizei Stride = 0;
        uint64_t Offset = 0;
        uint32_t Buffer = 0;
        GLuint Divisor = 0;
    };

    struct VertexArray
    {
        Attrib Attribs[16];
        uint32_t ElementBuffer = 0;
    };

    struct ShaderObject
    {
        GLenum Type = 0;
        std::string Source;
        bool Compiled = false;
        std::string Log;
        int AttachCount = 0;
        bool DeletePending = false;
    };

    using GLEmu::UniformSlot;
    using GLEmu::SamplerBinding;
    using GLEmu::BlockBinding;
    using GLEmu::VertexInput;

    // WGSL'deki bir doku bağlaması ve (varsa) ayrılmış örnekleyicisi
    struct TextureSlot
    {
        uint32_t Binding = 0;                            // doku (GLEmu yazmacı)
        int SamplerBinding = -1;                         // Tint'in örnekleyiciye verdiği numara
        int Unit = 0;                                    // GL doku birimi (ön yüzün SamplerBinding::Units'inden)
        GLenum Target = GL_TEXTURE_2D;
        bool Shadow = false;                             // texture_depth_* + sampler_comparison
        bool Integer = false;
        bool Multisampled = false;
        WGPUShaderStage Visibility = WGPUShaderStage_None;
        int ArrayElement = 0;
        int SamplerIndex = -1;                           // Program::Samplers dizini
    };

    struct Program
    {
        uint64_t Id = 0;
        std::vector<uint32_t> Shaders;
        bool Linked = false;
        std::string Log;
        WGPUShaderModule VS = nullptr, FS = nullptr;
        std::string VSEntry = "main", FSEntry = "main";
        int GlobalsRegister = -1;
        uint32_t GlobalsSize = 0;
        std::vector<uint8_t> Globals;
        bool GlobalsDirty = true;
        WGPUBuffer GlobalsBuffer = nullptr;              // halkada (GlobalsFrame gönderiminde geçerli)
        uint64_t GlobalsOffset = 0, GlobalsFrame = 0;
        std::vector<UniformSlot> Uniforms;
        std::unordered_map<std::string, int> UniformByName;
        std::vector<SamplerBinding> Samplers;
        std::vector<BlockBinding> Blocks;
        std::vector<VertexInput> Inputs;
        std::vector<TextureSlot> Textures;
        uint32_t BlockVisibility[64] = {};               // sabit arabellek bağlaması başına aşama bitleri
        uint32_t ColorOutputs = 1;
        uint32_t ColorOutputMask = 1;
        uint8_t OutputComponents[8] = { 4, 4, 4, 4, 4, 4, 4, 4 };
        std::string Label;                               // glObjectLabel (motorun gölgelendirici adı)   // parça çıktısı bileşen sayısı (yazma maskesi bunu aşamaz)
        bool DeletePending = false;
    };

    struct QueryObject
    {
        uint64_t Ns = 0;                                 // CPU zamanı (WebGPU'da timestamp-query her yerde yok)
        bool Written = false;
        uint64_t Frame = 0;
        bool Occlusion = false, Any = false;
    };

    struct State
    {
        bool Blend = false, DepthTest = false, CullFace = false, ScissorTest = false, StencilTest = false;
        bool PolygonOffsetFill = false, PrimitiveRestart = false;
        GLenum BlendSrcRGB = GL_ONE, BlendDstRGB = GL_ZERO, BlendSrcA = GL_ONE, BlendDstA = GL_ZERO;
        GLenum BlendEqRGB = GL_FUNC_ADD, BlendEqA = GL_FUNC_ADD;
        bool DepthMask = true;
        GLenum DepthFunc = GL_LESS;
        GLenum CullMode = GL_BACK;
        GLenum FrontFace = GL_CCW;
        GLenum PolygonMode = GL_FILL;
        float PolygonOffsetFactor = 0.0f, PolygonOffsetUnits = 0.0f;
        bool ColorMask[4] = { true, true, true, true };
        uint8_t ColorMaskBits[8] = { 15, 15, 15, 15, 15, 15, 15, 15 };
        float LineWidth = 1.0f;
        GLenum StencilFunc = GL_ALWAYS;
        GLint StencilRef = 0;
        GLuint StencilValueMask = 0xFFFFFFFF, StencilWriteMask = 0xFFFFFFFF;
        GLenum StencilFail = GL_KEEP, StencilZFail = GL_KEEP, StencilZPass = GL_KEEP;
        GLint Viewport[4] = { 0, 0, 0, 0 };
        GLint Scissor[4] = { 0, 0, 0, 0 };
        float ClearColor[4] = { 0, 0, 0, 0 };
        float ClearDepth = 1.0f;
        GLint ClearStencil = 0;
        float BlendColor[4] = { 0, 0, 0, 0 };
        uint32_t Program = 0;
        uint32_t VAO = 0;
        uint32_t ArrayBuffer = 0;
        uint32_t ElementBufferNoVAO = 0;
        uint32_t UniformBuffer = 0;
        uint32_t PixelUnpackBuffer = 0;
        uint32_t DrawFBO = 0, ReadFBO = 0;
        uint32_t Renderbuffer = 0;
        uint32_t ActiveTexture = 0;
        std::array<uint32_t, 64> Tex2D{}, TexCube{}, Tex2DMS{}, Samplers{};
        std::array<uint32_t, 64> UBOBindings{};
        std::array<GLintptr, 64> UBOOffsets{};
        std::array<GLsizeiptr, 64> UBOSizes{};
        GLint UnpackAlignment = 4, PackAlignment = 4, UnpackRowLength = 0;
        GLenum ClipOrigin = GL_LOWER_LEFT;
    };

    struct Context
    {
        State S;
        std::unordered_map<uint32_t, std::unique_ptr<Texture>> Textures;
        std::unordered_map<uint32_t, std::unique_ptr<Texture>> Renderbuffers;
        std::unordered_map<uint32_t, std::unique_ptr<Buffer>> Buffers;
        std::unordered_map<uint32_t, std::unique_ptr<Framebuffer>> Framebuffers;
        std::unordered_map<uint32_t, std::unique_ptr<VertexArray>> VertexArrays;
        std::unordered_map<uint32_t, std::unique_ptr<ShaderObject>> ShaderObjects;
        std::unordered_map<uint32_t, std::unique_ptr<Program>> Programs;
        std::unordered_map<uint32_t, std::unique_ptr<Texture>> SamplerObjects;
        std::unordered_map<uint32_t, QueryObject> QueryObjects;
        uint32_t NextName = 1;
        uint64_t NextProgramId = 1;
        VertexArray DefaultVAO;

        Texture BackColor;                               // GL yönünde arka arabellek (sunumda yüzeye çevrilerek kopyalanır)
        Texture BackDepth;
        Framebuffer DefaultFB;

        // Açık geçiş ve hedefleri
        bool Rendering = false;
        uint32_t RenderingFBO = UINT32_MAX;
        uint64_t RenderingKey = 0;
        uint32_t RenderWidth = 0, RenderHeight = 0;
        Texture* RenderColor[8] = {};
        int RenderColorLevel[8] = {}, RenderColorLayer[8] = {};
        uint32_t RenderColorCount = 0;
        Texture* RenderDepth = nullptr;
        int RenderDepthLevel = 0, RenderDepthLayer = 0;
        uint32_t RenderingSamples = 1;

        std::vector<std::unique_ptr<Texture>> InternalTextures;
        Texture* NullTexture2D = nullptr;
        Texture* NullTextureCube = nullptr;
        Texture* NullTextureInt = nullptr;
        Texture* NullTextureDepth = nullptr;

        std::string Vendor, Renderer, Version;
        std::vector<std::string> Extensions;
        GLDEBUGPROC DebugCallback = nullptr;
        const void* DebugUserParam = nullptr;
        bool Live = false;
    };

    Context& Ctx();

    // Cihaz / yüzey (WgpuGLCore.cpp)
    bool InitDevice(GLFWwindow* window, std::string& error);
    void ShutdownDevice();
    void SetVSync(bool enabled);
    const std::string& DeviceName();
    WGPUDevice Device();
    WGPUQueue Queue();
    WGPUCommandEncoder Encoder();                        // bu gönderimin kodlayıcısı (gerekirse açılır)
    WGPURenderPassEncoder RenderPass();                  // açık geçiş (yoksa nullptr)
    WGPURenderPassEncoder BeginPass(const WGPURenderPassDescriptor& desc);
    void EndPass();
    void Submit();                                       // açık geçişi kapat, kodlayıcıyı gönder
    uint64_t SubmitSerial();                             // şu an kaydedilen gönderimin sıra numarası
    uint64_t FrameSerial();
    bool HasFeature(WGPUFeatureName feature);
    const WGPULimits& DeviceLimits();
    // Halka: kare başına yükleme alanı (köşe / indeks / sabit verisi). Dönen arabellek + konum; veri kuyruk yazımıyla girer.
    WGPUBuffer RingWrite(const void* data, uint64_t size, uint64_t alignment, uint64_t& offset);
    // Masaüstü: GPU işi bitene kadar bekler (geri okuma). Web: false (bekleyemez).
    bool WaitIdle();
    // Gönderimden hemen sonra çalışacak iş (eşzamansız geri okumanın eşleme isteği: kopya gönderilmeden eşlenemez)
    void AfterSubmit(void (*fn)(void*), void* user);
    bool MapRead(WGPUBuffer buffer, uint64_t size, std::vector<uint8_t>& out);
    WGPUTextureFormat SurfaceFormat();
    bool AcquireSurface(WGPUTexture& texture, uint32_t& width, uint32_t& height);
    bool SurfaceSize(uint32_t& width, uint32_t& height);   // pencerenin çerçeve arabelleği boyu (doku almadan)
    void PresentSurface();

    bool Install(std::string& error);
    void Shutdown();
    void BeginFrameResources();
    void SwapBuffers();

    Texture* GetTexture(uint32_t name);
    Texture* GetRenderbuffer(uint32_t name);
    Buffer* GetBuffer(uint32_t name);
    Framebuffer* GetFramebuffer(uint32_t name);
    Program* GetProgram(uint32_t name);
    Texture* AttachmentTexture(const Attachment& a);

    bool AllocateTexture(Texture& t, GLenum target, GLenum internalFormat, uint32_t w, uint32_t h, uint32_t levels, uint32_t samples);
    void DestroyTextureImage(Texture& t);
    WGPUTextureView SampleView(Texture& t);
    WGPUTextureFormat DepthAspectFormat(const Texture& t);
    WGPUTextureView TargetView(Texture& t, int level, int layer);
    WGPUSampler GetSampler(const Texture& params, bool filtering, bool comparison);
    void UploadTexture(Texture& t, int level, int layer, int x, int y, int w, int h, GLenum format, GLenum type, const void* data);
    void ReadImage(Texture& t, int level, int layer, int x, int y, int w, int h, GLenum format, GLenum type, void* out, int packAlignment);
    // Arabelleğin GPU sürümü (gerekirse yükler)
    WGPUBuffer BufferBinding(Buffer& b, uint64_t minSize, uint64_t& offset, uint64_t* size = nullptr);

    using GLEmu::Limits;
    const Limits& GetLimits();
    uint32_t ClampSamples(GLsizei samples, bool depth);
    void ReleaseBuffer(Buffer& b);
    void WriteTimestamp(QueryObject& q);
    bool ReadTimestamp(QueryObject& q, bool wait, uint64_t& ns);
    void BeginOcclusion(QueryObject& q);
    void EndOcclusion(QueryObject& q);
    bool ReadOcclusion(QueryObject& q, bool wait, uint64_t& samples);

    VertexArray* CurrentVAO();
    bool Live();
    void FlushAndWaitGL();
    bool GrowLevels(Texture& t, uint32_t levels);
    void GenerateMipmaps(Texture& t);
    void ClearTexture(Texture& t, int level, GLenum format, GLenum type, const void* data);
    void BlitFramebuffer(GLint sx0, GLint sy0, GLint sx1, GLint sy1, GLint dx0, GLint dy0, GLint dx1, GLint dy1, GLbitfield mask, GLenum filter);

    void EndRendering();
    bool EnsureRendering();
    void Draw(GLenum mode, GLint first, GLsizei count, GLsizei instances, bool indexed, GLenum indexType, const void* indices, GLint baseVertex);
    void ClearCurrent(GLbitfield mask);

    bool LinkProgram(Program& p);
    void DestroyProgram(Program& p);
    UniformSlot* FindUniform(Program& p, GLint location);
    void ReleaseDrawHelpers();
    // CQ_WGPUGL_TRACE=<kare sayısı>: ilk karelerde geçişleri ve çizimleri günlüğe yazar
    bool Tracing();
}

#endif
