#pragma once

#ifdef CQ_VULKAN_ENABLED

// GL -> Vulkan çeviri katmanı (ANGLE/Zink yaklaşımı). Motorun ve ImGui'nin kullandığı GL 4.5 alt kümesi
// glad işlev göstericileri üzerinden buraya yönlenir; komutlar Vulkan'a kaydedilir.
//
// Temel kurallar:
//  * Tüm görüntüler VK_IMAGE_LAYOUT_GENERAL düzenindedir; geçişler kaba tam bariyerlerle sıralanır.
//  * Görüntüler GL yönünde tutulur (satır 0 = alt). Sunumda takas zincirine dikey çevrilerek kopyalanır,
//    böylece gl_FragCoord, glViewport, glReadPixels ve dokuya çizim GL ile birebir aynıdır.
//  * GL derinlik aralığı [-1,1] köşe gölgelendiricisinin sonuna eklenen dönüşümle Vulkan [0,1]'e eşlenir;
//    ön yüz sarımı ters çevrilir (Vulkan alan formülü işaret farkı).
//  * Arabellekler GL gibi davranır: güncelleme, daha önce kaydedilmiş çizimleri etkilemez (yeniden adlandırma).

#include "VulkanCore.h"
#include "../GLEmu/GLEmuCommon.h"

#include <glad/glad.h>

// 1.3 çekirdek işlevleri sürüm bağımsız dağıtım tablosundan (1.2 + KHR/EXT sürücülerinde de çalışır)
#define vkCmdBeginRendering ::Conqueror::VK::g_Fn.CmdBeginRendering
#define vkCmdEndRendering ::Conqueror::VK::g_Fn.CmdEndRendering
#define vkCmdSetCullMode ::Conqueror::VK::g_Fn.CmdSetCullMode
#define vkCmdSetFrontFace ::Conqueror::VK::g_Fn.CmdSetFrontFace
#define vkCmdSetDepthTestEnable ::Conqueror::VK::g_Fn.CmdSetDepthTestEnable
#define vkCmdSetDepthWriteEnable ::Conqueror::VK::g_Fn.CmdSetDepthWriteEnable
#define vkCmdSetDepthCompareOp ::Conqueror::VK::g_Fn.CmdSetDepthCompareOp
#define vkCmdSetStencilTestEnable ::Conqueror::VK::g_Fn.CmdSetStencilTestEnable
#define vkCmdSetStencilOp ::Conqueror::VK::g_Fn.CmdSetStencilOp
#define vkResetQueryPool ::Conqueror::VK::g_Fn.ResetQueryPool

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace Conqueror::VkGL
{
    struct Texture
    {
        GLenum Target = 0;                  // GL_TEXTURE_2D, GL_TEXTURE_CUBE_MAP, GL_TEXTURE_2D_MULTISAMPLE, GL_RENDERBUFFER
        GLenum InternalFormat = 0;
        VkFormat Format = VK_FORMAT_UNDEFINED;
        VkImageAspectFlags Aspect = VK_IMAGE_ASPECT_COLOR_BIT;
        uint32_t Width = 0, Height = 0, Levels = 0, Layers = 1, Samples = 1;
        bool Immutable = false;
        bool Integer = false;
        uint32_t DefinedLevels = 0;         // bit maskesi (değişken dokular düzey düzey tanımlanır)
        VkImage Image = VK_NULL_HANDLE;
        VkDeviceMemory Memory = VK_NULL_HANDLE;
        VkImageView SampledView = VK_NULL_HANDLE;
        int SampledViewLevels = 0;
        std::unordered_map<uint32_t, VkImageView> AttachmentViews;   // düzey | katman << 8
        // Örnekleyici parametreleri
        GLenum MinFilter = GL_NEAREST_MIPMAP_LINEAR, MagFilter = GL_LINEAR;
        GLenum WrapS = GL_REPEAT, WrapT = GL_REPEAT, WrapR = GL_REPEAT;
        GLenum CompareMode = GL_NONE, CompareFunc = GL_LEQUAL;
        float MaxAnisotropy = 1.0f, MinLod = -1000.0f, MaxLod = 1000.0f, LodBias = 0.0f;
        int BaseLevel = 0, MaxLevel = 1000;
        float BorderColor[4] = { 0, 0, 0, 0 };
    };

    struct Buffer
    {
        std::vector<uint8_t> Shadow;        // GL'nin gördüğü içerik
        GLenum Usage = GL_STATIC_DRAW;
        bool Dirty = true;                  // gölge kopya GPU sürümünden yeni
        // Kalıcı (adanmış) sürüm
        VkBuffer Dedicated = VK_NULL_HANDLE;
        VkDeviceMemory Memory = VK_NULL_HANDLE;
        uint8_t* Mapped = nullptr;
        VkDeviceSize DedicatedSize = 0;
        uint64_t DedicatedLastUse = 0;      // son kullanan gönderim
        // Geçerli sürüm (adanmış ya da bu karenin halkası)
        VkBuffer Current = VK_NULL_HANDLE;
        VkDeviceSize CurrentOffset = 0;
        bool CurrentIsRing = false;
        uint64_t RingFrame = 0;
        size_t HighWater = 0;               // yazılmış en yüksek bayt (kopyalar bununla sınırlı)
    };

    struct Attachment
    {
        uint32_t Texture = 0;               // doku adı
        uint32_t Renderbuffer = 0;          // ya da renderbuffer adı
        int Level = 0;
        int Layer = 0;                      // küp yüzü
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

    // Uniform konumu: dizi elemanları ve yapı alanları ayrı konumlardır
    struct UniformSlot
    {
        std::string Name;
        uint32_t Offset = 0;                // CQ_Globals içinde
        GLenum Type = GL_FLOAT;             // GL_FLOAT, GL_FLOAT_VEC2.., GL_INT, GL_FLOAT_MAT4..
        uint32_t MatrixStride = 16;
        int Sampler = -1;                   // örnekleyici ise Samplers dizini
        int SamplerElement = 0;
        uint32_t ArrayStride = 0;           // dizi tabanı için (glUniform*v sayımı)
        int ArrayRemaining = 1;             // bu elemandan itibaren kalan eleman
    };

    struct SamplerBinding
    {
        std::string Name;
        uint32_t Binding = 0;               // Vulkan tanımlayıcı bağlama noktası
        uint32_t Count = 1;
        GLenum Target = GL_TEXTURE_2D;      // GL_TEXTURE_2D / GL_TEXTURE_CUBE_MAP / GL_TEXTURE_2D_MULTISAMPLE
        bool Shadow = false;
        bool Integer = false;
        bool SharedSampler = false;         // ayrık doku dizisi + tek örnekleyici (SAMPLED_IMAGE + SAMPLER)
        uint32_t SamplerSlot = 0;           // ortak örnekleyicinin bağlama noktası
        std::vector<int> Units;
    };

    struct BlockBinding
    {
        std::string Name;
        uint32_t Binding = 0;               // Vulkan
        uint32_t GLBinding = 0;             // glBindBufferBase noktası
        uint32_t Size = 0;
    };

    struct VertexInput
    {
        int Location = 0;
        VkFormat Format = VK_FORMAT_R32G32B32A32_SFLOAT;
        bool Integer = false;
        std::string Name;
    };

    struct Program
    {
        uint64_t Id = 0;                    // boru hattı önbellek anahtarı (yeniden bağlamada değişir)
        std::vector<uint32_t> Shaders;
        bool Linked = false;
        std::string Log;
        VkShaderModule Vert = VK_NULL_HANDLE, Frag = VK_NULL_HANDLE;
        VkDescriptorSetLayout SetLayout = VK_NULL_HANDLE;
        VkPipelineLayout Layout = VK_NULL_HANDLE;
        int GlobalsBinding = -1;
        uint32_t GlobalsSize = 0;
        std::vector<uint8_t> Globals;
        bool GlobalsDirty = true;
        VkBuffer GlobalsBuffer = VK_NULL_HANDLE;
        VkDeviceSize GlobalsOffset = 0;
        uint64_t GlobalsFrame = 0;
        std::vector<UniformSlot> Uniforms;
        std::unordered_map<std::string, int> UniformByName;
        std::vector<SamplerBinding> Samplers;
        std::vector<BlockBinding> Blocks;
        std::vector<VertexInput> Inputs;
        uint32_t ColorOutputs = 1;
        uint32_t ColorOutputMask = 1;       // parça gölgelendiricinin yazdığı çıktı konumları
        bool DeletePending = false;
    };

    // GL_TIMESTAMP sorgusu: her glQueryCounter havuzda yeni bir dizin alır
    struct QueryObject
    {
        uint32_t Index = UINT32_MAX;
        uint64_t Serial = 0;                // yazan gönderim
        bool Written = false;
        // Örtüşme sorgusu: Vulkan sorgusu çizim kapsamını aşamaz; her kapsam parçası ayrı dizin, sonuçlar toplanır
        std::vector<uint32_t> OcclIndices;
        uint64_t OcclSerial = 0;
        bool Occlusion = false, Any = false;
    };

    // GL durum makinesi
    struct State
    {
        // Etkinleştirmeler
        bool Blend = false, DepthTest = false, CullFace = false, ScissorTest = false, StencilTest = false;
        bool PolygonOffsetFill = false, PrimitiveRestart = false;
        // Karıştırma
        GLenum BlendSrcRGB = GL_ONE, BlendDstRGB = GL_ZERO, BlendSrcA = GL_ONE, BlendDstA = GL_ZERO;
        GLenum BlendEqRGB = GL_FUNC_ADD, BlendEqA = GL_FUNC_ADD;
        // Derinlik / yüz / poligon
        bool DepthMask = true;
        GLenum DepthFunc = GL_LESS;
        GLenum CullMode = GL_BACK;
        GLenum FrontFace = GL_CCW;
        GLenum PolygonMode = GL_FILL;
        float PolygonOffsetFactor = 0.0f, PolygonOffsetUnits = 0.0f;
        bool ColorMask[4] = { true, true, true, true };
        uint8_t ColorMaskBits[8] = { 15, 15, 15, 15, 15, 15, 15, 15 };   // ek başına (glColorMaski): R=1 G=2 B=4 A=8
        float LineWidth = 1.0f;
        // Şablon
        GLenum StencilFunc = GL_ALWAYS;
        GLint StencilRef = 0;
        GLuint StencilValueMask = 0xFFFFFFFF, StencilWriteMask = 0xFFFFFFFF;
        GLenum StencilFail = GL_KEEP, StencilZFail = GL_KEEP, StencilZPass = GL_KEEP;
        // Görünüm
        GLint Viewport[4] = { 0, 0, 0, 0 };
        GLint Scissor[4] = { 0, 0, 0, 0 };
        float ClearColor[4] = { 0, 0, 0, 0 };
        float ClearDepth = 1.0f;
        GLint ClearStencil = 0;
        // Bağlamalar
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
        // Piksel depolama
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
        std::unordered_map<uint32_t, std::unique_ptr<Texture>> SamplerObjects;   // yalnızca parametreler
        std::unordered_map<uint32_t, QueryObject> QueryObjects;
        uint32_t NextName = 1;
        uint32_t NextQueryIndex = 0;
        uint64_t NextProgramId = 1;
        VertexArray DefaultVAO;             // VAO 0

        // Varsayılan çerçeve arabelleği (FBO 0)
        Texture BackColor;
        Texture BackDepth;
        Framebuffer DefaultFB;

        // Kayıt durumu
        bool Rendering = false;
        uint32_t RenderingFBO = UINT32_MAX;
        uint64_t RenderingKey = 0;
        VkExtent2D RenderArea{ 0, 0 };
        std::vector<VkFormat> RenderingColorFormats;
        std::vector<bool> RenderingColorInteger;
        VkFormat RenderingDepthFormat = VK_FORMAT_UNDEFINED;
        VkFormat RenderingStencilFormat = VK_FORMAT_UNDEFINED;
        uint32_t RenderingSamples = 1;

        std::unordered_map<uint64_t, VkPipeline> Pipelines;
        std::vector<VkDescriptorPool> Pools[VK::kFramesInFlight];
        uint64_t PoolFrame[VK::kFramesInFlight] = {};
        size_t PoolIndex[VK::kFramesInFlight] = {};
        uint64_t LastSetKey = 0;
        VkDescriptorSet LastSet = VK_NULL_HANDLE;
        uint64_t LastSetFrame = 0;
        uint64_t BoundPipelineKey = 0;
        std::unordered_map<uint64_t, VkSampler> SamplerCache;
        VkQueryPool QueryPool = VK_NULL_HANDLE;
        VkQueryPool OcclusionPool = VK_NULL_HANDLE;
        uint32_t NextOcclusionIndex = 0;
        QueryObject* ActiveOcclusion = nullptr;   // GL sorgusu etkin
        bool OcclusionRunning = false;            // geçerli çizim kapsamında Vulkan sorgusu açık
        static constexpr uint32_t kQueryCount = 4096;
        float TimestampPeriod = 1.0f;       // ns / tik
        bool TimestampsSupported = false;
        VkBuffer NullVertexBuffer = VK_NULL_HANDLE;     // 0..15: float (0,0,0,1), 16..31: int (0,0,0,1)
        VkDeviceMemory NullVertexMemory = VK_NULL_HANDLE;
        std::vector<std::unique_ptr<Texture>> InternalTextures;
        Texture* NullTexture2D = nullptr;
        Texture* NullTextureCube = nullptr;
        Texture* NullTextureInt = nullptr;

        // Bilgi dizgeleri
        std::string Vendor, Renderer, Version;
        std::vector<std::string> Extensions;
        GLDEBUGPROC DebugCallback = nullptr;
        const void* DebugUserParam = nullptr;
        bool Live = false;
    };

    Context& Ctx();

    // ── Kurulum ──
    bool Install(std::string& error);       // glad göstericilerini bu katmana yönlendirir
    void Shutdown();
    void BeginFrameResources();             // varsayılan çerçeve boyutu vb.
    void SwapBuffers();

    // ── İç yardımcılar (VkGLObjects / VkGLDraw / VkGLShaders arasında) ──
    Texture* GetTexture(uint32_t name);
    Texture* GetRenderbuffer(uint32_t name);
    Buffer* GetBuffer(uint32_t name);
    Framebuffer* GetFramebuffer(uint32_t name);
    Program* GetProgram(uint32_t name);
    Texture* AttachmentTexture(const Attachment& a);

    VkFormat ToVkFormat(GLenum internalFormat, VkImageAspectFlags& aspect, bool& integer);
    void DestroyTextureImage(Texture& t);
    bool AllocateTexture(Texture& t, GLenum target, GLenum internalFormat, uint32_t w, uint32_t h, uint32_t levels, uint32_t samples);
    VkImageView GetSampledView(Texture& t);
    VkImageView GetAttachmentView(Texture& t, int level, int layer);
    VkSampler GetSampler(const Texture& params, bool integer);
    void UploadTexture(Texture& t, int level, int layer, int x, int y, int w, int h, GLenum format, GLenum type, const void* data);
    void ReadImage(Texture& t, int level, int layer, int x, int y, int w, int h, GLenum format, GLenum type, void* out, int packAlignment);
    void BufferRef(Buffer& b, VkBuffer& outBuffer, VkDeviceSize& outOffset);

    // ── GL ön yüzü kancaları (GLEmu/GLFrontEnd.inl) ──
    using GLEmu::Limits;
    const Limits& GetLimits();
    uint32_t ClampSamples(GLsizei samples, bool depth);
    void ReleaseBuffer(Buffer& b);
    void WriteTimestamp(QueryObject& q);
    bool ReadTimestamp(QueryObject& q, bool wait, uint64_t& ns);
    void BeginOcclusion(QueryObject& q);
    void EndOcclusion(QueryObject& q);
    bool ReadOcclusion(QueryObject& q, bool wait, uint64_t& samples);
    void StartOcclusionSegment();   // çizim kapsamı başında (VkGLDraw)
    void StopOcclusionSegment();    // çizim kapsamı sonunda

    VertexArray* CurrentVAO();
    bool Live();                            // Vulkan cihazı hazır (kapanıştan sonra çağrılar yok sayılır)
    void FlushAndWaitGL();                  // kaydı gönder + bekle (geri okuma)
    bool GrowLevels(Texture& t, uint32_t levels);   // değişken dokunun mip zincirini içeriği koruyarak büyüt
    void GenerateMipmaps(Texture& t);
    void ClearTexture(Texture& t, int level, GLenum format, GLenum type, const void* data);
    void BlitFramebuffer(GLint sx0, GLint sy0, GLint sx1, GLint sy1, GLint dx0, GLint dy0, GLint dx1, GLint dy1, GLbitfield mask, GLenum filter);

    void EndRendering();
    bool EnsureRendering();
    VkCommandBuffer TransferCmd();          // render geçişini bitirir ve bariyer koyar
    void Draw(GLenum mode, GLint first, GLsizei count, GLsizei instances, bool indexed, GLenum indexType, const void* indices, GLint baseVertex);
    void ClearCurrent(GLbitfield mask);

    bool LinkProgram(Program& p);
    void DestroyProgram(Program& p);
    UniformSlot* FindUniform(Program& p, GLint location);
}

#endif
