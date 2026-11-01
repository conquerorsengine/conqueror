#pragma once

#ifdef CQ_METAL_ENABLED

// GL -> Metal çeviri katmanı (yalnızca .mm, ARC). VkGL / DxGL / D12GL ile aynı ön yüzü (GLEmu/GLFrontEnd.inl) kullanır.
//
// Temel kurallar (D3D katmanlarıyla aynı):
//  * Görüntüler GL yönünde tutulur; köşe gölgelendiricisi y'yi çevirir, z'yi [0,w]'ya taşır. Sunumda dikey çevrilir.
//  * GL_CCW ön yüz => MTLWindingClockwise.
// Metal'e özgü:
//  * Gölgelendiriciler SPIR-V -> MSL (SPIRV-Cross), çalışma anında derlenir. Dizinler: köşe arabellekleri 0..15,
//    sabit arabellekler 16 + b, doku / örnekleyici = GLEmu yazmacı.
//  * Hedef değişimi, kopya ve geri okuma render kodlayıcısını kapatır; sonraki çizim yeni geçiş açar (yükle / sakla).
//  * Metal komut arabellekleri kaynakları tutar: bırakma ertelenmez. Arabellek güncellemesi GPU'da kullanılan sürümü
//    değiştirmez (yeniden adlandırma ya da halka).

#include "MetalCore.h"
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

namespace Conqueror::MtlGL
{
    struct Texture
    {
        GLenum Target = 0;
        GLenum InternalFormat = 0;
        MTLPixelFormat Format = MTLPixelFormatInvalid;
        GLEmu::TexelKind Kind = GLEmu::TexelKind::RGBA8;
        bool Depth = false, Stencil = false;
        uint32_t Width = 0, Height = 0, Levels = 0, Layers = 1, Samples = 1;
        bool Immutable = false;
        bool Integer = false;
        bool Renderable = false;
        uint32_t DefinedLevels = 0;
        id<MTLTexture> Image = nil;
        id<MTLTexture> View = nil;                       // örnekleme görünümü (tanımlı düzeyler)
        int ViewLevels = 0;
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
        id<MTLBuffer> GpuBuffer = nil;                   // geçerli sürüm (kalıcı ya da halka)
        uint64_t GpuOffset = 0, GpuSize = 0;
        bool GpuIsRing = false;
        uint64_t RingFrame = 0;
        id<MTLBuffer> Dedicated = nil;
        uint64_t DedicatedLastUse = 0;                   // son kullanan kare
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

    struct Program
    {
        uint64_t Id = 0;
        std::vector<uint32_t> Shaders;
        bool Linked = false;
        std::string Log;
        id<MTLFunction> VS = nil, PS = nil;
        int GlobalsRegister = -1;
        uint32_t GlobalsSize = 0;
        std::vector<uint8_t> Globals;
        bool GlobalsDirty = true;
        id<MTLBuffer> GlobalsBuffer = nil;               // halkada (GlobalsFrame karesinde geçerli)
        uint64_t GlobalsOffset = 0, GlobalsFrame = 0;
        std::vector<UniformSlot> Uniforms;
        std::unordered_map<std::string, int> UniformByName;
        std::vector<SamplerBinding> Samplers;
        std::vector<BlockBinding> Blocks;
        std::vector<VertexInput> Inputs;
        uint32_t ColorOutputs = 1;
        uint32_t ColorOutputMask = 1;
        bool DeletePending = false;
    };

    struct QueryObject
    {
        uint64_t Ns = 0;                                 // CPU zamanı (Metal'de komut başına GPU zaman damgası yok)
        bool Written = false;
        // Örtüşme sorgusu: görünürlük arabelleğinde geçiş (kodlayıcı) başına bir yuva, sonuçlar toplanır
        std::vector<uint32_t> Slots;
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
        uint8_t ColorMaskBits[8] = { 15, 15, 15, 15, 15, 15, 15, 15 };   // ek başına (glColorMaski): R=1 G=2 B=4 A=8
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

        Texture BackColor;
        Texture BackDepth;
        Framebuffer DefaultFB;

        // Açık geçiş (render kodlayıcısı) ve hedefleri
        bool Rendering = false;
        uint32_t RenderingFBO = UINT32_MAX;
        uint64_t RenderingKey = 0;
        uint64_t RenderingEpoch = 0;
        uint32_t RenderWidth = 0, RenderHeight = 0;
        Texture* RenderColor[8] = {};
        int RenderColorLevel[8] = {}, RenderColorLayer[8] = {};
        uint32_t RenderColorCount = 0;
        Texture* RenderDepth = nullptr;
        int RenderDepthLevel = 0, RenderDepthLayer = 0;
        MTLPixelFormat RenderColorFormats[8] = {};
        bool RenderColorInteger[8] = {};
        MTLPixelFormat RenderingDepthFormat = MTLPixelFormatInvalid;
        bool RenderingHasDepth = false, RenderingHasStencil = false;
        uint32_t RenderingSamples = 1;

        std::unordered_map<uint64_t, id<MTLRenderPipelineState>> Pipelines;
        std::unordered_map<uint64_t, id<MTLDepthStencilState>> DepthStates;
        std::unordered_map<uint64_t, id<MTLSamplerState>> SamplerCache;
        id<MTLBuffer> NullVertexBuffer = nil;            // kapalı öznitelik: (0,0,0,1)
        id<MTLBuffer> ZeroConstants = nil;               // bağlanmamış blok: 64 KB sıfır
        id<MTLBuffer> Visibility = nil;                  // örtüşme sayaçları (her geçişe bağlı)
        uint32_t NextVisibilitySlot = 0;
        QueryObject* ActiveOcclusion = nullptr;
        uint64_t OcclusionEpoch = 0;                     // açık yuvanın kodlayıcısı
        std::vector<std::unique_ptr<Texture>> InternalTextures;
        Texture* NullTexture2D = nullptr;
        Texture* NullTextureCube = nullptr;
        Texture* NullTextureInt = nullptr;

        std::string Vendor, Renderer, Version;
        std::vector<std::string> Extensions;
        GLDEBUGPROC DebugCallback = nullptr;
        const void* DebugUserParam = nullptr;
        bool Live = false;
    };

    Context& Ctx();

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
    id<MTLTexture> SampleView(Texture& t);
    id<MTLSamplerState> GetSampler(const Texture& params, bool integer, bool comparison);
    void UploadTexture(Texture& t, int level, int layer, int x, int y, int w, int h, GLenum format, GLenum type, const void* data);
    void ReadImage(Texture& t, int level, int layer, int x, int y, int w, int h, GLenum format, GLenum type, void* out, int packAlignment);
    // Arabelleğin GPU sürümü (gerekirse yükler); minSize: sabit arabellek bildirilen boyu
    id<MTLBuffer> BufferBinding(Buffer& b, uint64_t minSize, uint64_t& offset, uint64_t* size = nullptr);

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
}

#endif
