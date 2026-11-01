#pragma once

#ifdef CQ_D3D11_ENABLED

// GL -> Direct3D 11 çeviri katmanı (Windows varsayılan arka ucu). VkGL ile aynı ön yüzü (GLEmu/GLFrontEnd.inl)
// kullanır; burada yalnızca D3D11'e özgü kaynaklar ve kancalar tanımlanır.
//
// Temel kurallar:
//  * Görüntüler GL yönünde tutulur (bellek satırı 0 = GL alt satırı). Köşe gölgelendiricisi gl_Position.y'yi
//    çevirir (SPIRV-Cross flip_vert_y) ve z'yi [0,w]'ya taşır (fixup_clipspace); böylece viewport/scissor/
//    gl_FragCoord GL ile birebir aynı satırlara düşer. Sunumda takas zincirine dikey çevrilerek kopyalanır.
//  * Y çevirmesi sarımı ters çevirir: GL_CCW ön yüz => D3D FrontCounterClockwise = FALSE.
//  * Arabellekler D3D11 DYNAMIC + Map(WRITE_DISCARD): sürücü yeniden adlandırır, GL anlamı korunur.

#include "D3D11Core.h"
#include "../../GLEmu/GLEmuCommon.h"
#include "../Common/DxShared.h"

#include <glad/glad.h>

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Conqueror::DxGL
{
    struct Texture
    {
        GLenum Target = 0;
        GLenum InternalFormat = 0;
        DXGI_FORMAT Format = DXGI_FORMAT_UNKNOWN;        // kaynak (derinlikte typeless)
        DXGI_FORMAT SRVFormat = DXGI_FORMAT_UNKNOWN;
        DXGI_FORMAT RTVFormat = DXGI_FORMAT_UNKNOWN;     // renk ise
        DXGI_FORMAT DSVFormat = DXGI_FORMAT_UNKNOWN;     // derinlik ise
        bool Depth = false, Stencil = false;
        uint32_t Width = 0, Height = 0, Levels = 0, Layers = 1, Samples = 1;
        bool Immutable = false;
        bool Integer = false;
        bool AutoMips = false;                           // D3D11_RESOURCE_MISC_GENERATE_MIPS
        uint32_t DefinedLevels = 0;
        ID3D11Texture2D* Image = nullptr;
        ID3D11ShaderResourceView* SRV = nullptr;
        int SRVLevels = 0;
        std::unordered_map<uint32_t, ID3D11RenderTargetView*> RTVs;   // düzey | katman << 8
        std::unordered_map<uint32_t, ID3D11DepthStencilView*> DSVs;
        // Örnekleyici parametreleri (VkGL ile aynı adlar: ön yüz bunları yazar)
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
        uint64_t Version = 1;                            // her yazımda artar
        ID3D11Buffer* Geometry = nullptr;                // köşe + indeks
        size_t GeometrySize = 0;
        uint64_t GeometryVersion = 0;
        ID3D11Buffer* Constant = nullptr;                // sabit arabellek (bağlama bayrağı ayrı olmalı)
        size_t ConstantSize = 0;
        uint64_t ConstantVersion = 0;
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

    using DXCommon::UniformSlot;
    using DXCommon::SamplerBinding;
    using DXCommon::BlockBinding;
    using DXCommon::VertexInput;

    struct Program
    {
        uint64_t Id = 0;
        std::vector<uint32_t> Shaders;
        bool Linked = false;
        std::string Log;
        ID3D11VertexShader* VS = nullptr;
        ID3D11PixelShader* PS = nullptr;
        std::vector<uint8_t> VSBytecode;                 // giriş düzeni imzası için
        int GlobalsRegister = -1;
        uint32_t GlobalsSize = 0;
        std::vector<uint8_t> Globals;
        bool GlobalsDirty = true;
        ID3D11Buffer* GlobalsBuffer = nullptr;
        std::vector<UniformSlot> Uniforms;
        std::unordered_map<std::string, int> UniformByName;
        std::vector<SamplerBinding> Samplers;
        std::vector<BlockBinding> Blocks;
        std::vector<VertexInput> Inputs;
        uint32_t ColorOutputs = 1;
        uint32_t ColorOutputMask = 1;
        bool DeletePending = false;
        std::unordered_map<uint64_t, ID3D11InputLayout*> InputLayouts;
    };

    struct QueryObject
    {
        std::shared_ptr<ID3D11Query> Query;              // ön yüz silerken haritadan çıkarır: paylaşımlı bırakma
        uint32_t Frame = 0;                              // zamanlama ayrıklık sorgusu (kare halkası) dizini
        uint64_t FrameSerial = 0;
        bool Written = false;
        std::shared_ptr<ID3D11Query> Occl;              // örtüşme sorgusu (GL_SAMPLES_PASSED)
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

        // Çıkış birleştirici bağlaması (gereksiz yeniden bağlamayı önler)
        bool Rendering = false;
        uint32_t RenderingFBO = UINT32_MAX;
        uint64_t RenderingKey = 0;
        uint32_t RenderWidth = 0, RenderHeight = 0;
        std::vector<DXGI_FORMAT> RenderingColorFormats;
        std::vector<bool> RenderingColorInteger;
        bool RenderingHasDepth = false, RenderingHasStencil = false;
        uint32_t RenderingSamples = 1;
        ID3D11RenderTargetView* BoundRTVs[8] = {};
        ID3D11DepthStencilView* BoundDSV = nullptr;
        uint32_t BoundRTVCount = 0;

        // Durum nesnesi önbellekleri
        std::unordered_map<uint64_t, ID3D11BlendState*> BlendStates;
        std::unordered_map<uint64_t, ID3D11DepthStencilState*> DepthStates;
        std::unordered_map<uint64_t, ID3D11RasterizerState*> RasterStates;
        std::unordered_map<uint64_t, ID3D11SamplerState*> SamplerCache;

        ID3D11Buffer* NullVertexBuffer = nullptr;        // kapalı öznitelik: (0,0,0,1)
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
    ID3D11ShaderResourceView* GetSRV(Texture& t);
    ID3D11RenderTargetView* GetRTV(Texture& t, int level, int layer);
    ID3D11DepthStencilView* GetDSV(Texture& t, int level, int layer);
    ID3D11SamplerState* GetSampler(const Texture& params, bool integer, bool comparison);
    void UploadTexture(Texture& t, int level, int layer, int x, int y, int w, int h, GLenum format, GLenum type, const void* data);
    void ReadImage(Texture& t, int level, int layer, int x, int y, int w, int h, GLenum format, GLenum type, void* out, int packAlignment);
    ID3D11Buffer* GeometryBuffer(Buffer& b);
    ID3D11Buffer* ConstantBuffer(Buffer& b);

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

    template <typename T>
    void SafeRelease(T*& p)
    {
        if (p)
        {
            p->Release();
            p = nullptr;
        }
    }
}

#endif
