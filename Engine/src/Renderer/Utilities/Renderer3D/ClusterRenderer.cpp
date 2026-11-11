#include "ClusterRenderer.h"
#include "ClusterMesh.h"
#include "Renderer3D.h"
#include "Material.h"
#include "ModelLoader.h"

#include "Core/Logging/Log.h"
#include "Core/Profiling/FrameProfiler.h"
#include "Renderer/Renderer.h"
#include "Scene/Scene.h"
#include "Scene/Components.h"
#include "Scene/VertexPaint.h"
#include "Scene/VirtualGeometry.h"

#include <glad/glad.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Conqueror
{
    namespace
    {
        // Küme (std430): görünürlük küresi, kendi LOD küresi, üst grubun LOD küresi; hata / üst hata; ilk indeks / indeks sayısı
        struct GCluster { glm::vec4 CenterRadius, LodCenterRadius, ParentLodCenterRadius; glm::vec2 Errors; glm::uvec2 Range; };
        static_assert(sizeof(GCluster) == 64, "cluster layout");
        // Örnek (öznitelik düzeniyle aynı, 80 bayt): dünya matrisinin sütunları + renk
        struct GInst { glm::vec4 C0, C1, C2, C3, Color; };
        // Seçim verisi: nesne uzayında kamera + en büyük ölçek; eşikler (piksel): x ana görünüm, y gölge
        struct GCull { glm::vec4 CameraLocalScale, Params; };
        static_assert(sizeof(GInst) == 80, "instance layout");

        constexpr int kCountersPerBatch = 8;     // 0: 1. geçiş, 1: 2. geçiş, 2..5: gölge kademeleri, 6: örtülen, 7: üçgen
        constexpr uint32_t kMaxCapacity = 1u << 22;

        const char* kCullSrc = R"(#version 450 core
layout(local_size_x = 64) in;
struct GCluster { vec4 CenterRadius, LodCenterRadius, ParentLodCenterRadius; vec2 Errors; uvec2 Range; };
struct GInst { vec4 C0, C1, C2, C3, Color; };
struct GCull { vec4 CameraLocalScale, Params; };
struct Cmd { uint Count, InstanceCount, FirstIndex; int BaseVertex; uint BaseInstance; };
layout(std430, binding = 0) readonly buffer Clusters { GCluster clusters[]; };
layout(std430, binding = 1) readonly buffer Insts { GInst insts[]; };
layout(std430, binding = 2) readonly buffer Culls { GCull culls[]; };
layout(std430, binding = 3) writeonly buffer Cmds { Cmd cmds[]; };
layout(std430, binding = 4) buffer Counters { uint counters[]; };
layout(std430, binding = 5) buffer Visibility { uint visBits[]; };
uniform uint u_ClusterCount;
uniform uint u_InstanceCount;
uniform uint u_FirstInstance;     // partinin ilk örneği (genel tamponlarda)
uniform uint u_MaxDraws;
uniform uint u_CounterSlot;       // çizim sayısı sayacı
uniform uint u_BatchCounters;     // partinin sayaç başı
uniform uint u_InstanceStats;     // örnek başına istatistik sayaçlarının başı (0: yok)
uniform vec4 u_Frustum[6];
uniform float u_PixelScale;
uniform int u_Ortho;
uniform int u_Mode;               // 0 gölge, 1 1. geçiş (önceki karede görünen), 2 2. geçiş (HZB), 3 hepsi (örtme yok)
uniform sampler2D u_HZB;
uniform mat4 u_ViewProj;
uniform vec2 u_HZBSize;
uniform int u_HZBMips;

float projectedError(float error, vec3 center, float radius, vec3 cam, float scale)
{
    if (error >= 1e29) return 1e30;
    if (u_Ortho == 1) return error * scale * u_PixelScale;
    float d = max(distance(cam, center) - radius, 1e-4);
    return error / d * u_PixelScale;
}

// Kürenin kutusu HZB'de (o bölgedeki en uzak derinlik) tamamen arkada mı
bool hzbVisible(vec3 wc, float wr)
{
    vec2 mn = vec2(1e9), mx = vec2(-1e9);
    float minZ = 1e9;
    for (int i = 0; i < 8; ++i)
    {
        vec3 corner = wc + wr * vec3((i & 1) != 0 ? 1.0 : -1.0, (i & 2) != 0 ? 1.0 : -1.0, (i & 4) != 0 ? 1.0 : -1.0);
        vec4 clip = u_ViewProj * vec4(corner, 1.0);
        if (clip.w <= 1e-4) return true;   // yakın düzlemi kesiyor
        vec3 ndc = clip.xyz / clip.w;
        mn = min(mn, ndc.xy);
        mx = max(mx, ndc.xy);
        minZ = min(minZ, ndc.z);
    }
    vec2 uvMin = clamp(mn * 0.5 + 0.5, 0.0, 1.0), uvMax = clamp(mx * 0.5 + 0.5, 0.0, 1.0);
    // Titreşim (TAA) ve yuvarlama payı: kutu bir piksel büyütülür
    uvMin -= 1.0 / u_HZBSize;
    uvMax += 1.0 / u_HZBSize;
    uvMin = clamp(uvMin, 0.0, 1.0);
    uvMax = clamp(uvMax, 0.0, 1.0);
    vec2 sizePx = (uvMax - uvMin) * u_HZBSize;
    // Kutu bu seviyede en çok 2x2 texel kaplar
    float level = ceil(log2(max(max(sizePx.x, sizePx.y), 1.0)));
    level = clamp(level, 0.0, float(u_HZBMips - 1));
    int l = int(level);
    vec2 lvl = max(floor(u_HZBSize / exp2(level)), vec2(1.0));
    ivec2 p0 = ivec2(min(uvMin * lvl, lvl - 1.0));
    ivec2 p1 = ivec2(min(uvMax * lvl, lvl - 1.0));
    float d = max(max(texelFetch(u_HZB, p0, l).r, texelFetch(u_HZB, ivec2(p1.x, p0.y), l).r),
                  max(texelFetch(u_HZB, ivec2(p0.x, p1.y), l).r, texelFetch(u_HZB, p1, l).r));
    float nearDepth = minZ * 0.5 + 0.5;
    return nearDepth <= d + 1e-6;
}

void emit(GCluster c, uint inst, uint slot)
{
    uint idx = atomicAdd(counters[slot], 1u);
    if (idx < u_MaxDraws)
    {
        Cmd cmd;
        cmd.Count = c.Range.y;
        cmd.InstanceCount = 1u;
        cmd.FirstIndex = c.Range.x;
        cmd.BaseVertex = 0;
        cmd.BaseInstance = u_FirstInstance + inst;
        cmds[idx] = cmd;
    }
    if (u_Mode == 1 || u_Mode == 2 || u_Mode == 3)
    {
        uint tri = c.Range.y / 3u;
        atomicAdd(counters[u_BatchCounters + 7u], tri);
        if (u_InstanceStats > 0u)
        {
            uint g = u_InstanceStats + (u_FirstInstance + inst) * 2u;
            atomicAdd(counters[g], 1u);
            atomicAdd(counters[g + 1u], tri);
        }
    }
}

void main()
{
    uint gid = gl_GlobalInvocationID.x;
    if (gid >= u_ClusterCount * u_InstanceCount) return;
    uint inst = gid / u_ClusterCount, ci = gid % u_ClusterCount;
    GCluster c = clusters[ci];
    GCull cull = culls[u_FirstInstance + inst];
    vec3 cam = cull.CameraLocalScale.xyz;
    float scale = cull.CameraLocalScale.w;
    float threshold = max(u_Mode == 0 ? cull.Params.y : cull.Params.x, 0.05);
    // LOD: bu küme yeterince ayrıntılı ve üst grubu yetersiz (çatlaksız DAG kesiti)
    if (projectedError(c.Errors.x, c.LodCenterRadius.xyz, c.LodCenterRadius.w, cam, scale) > threshold) return;
    if (projectedError(c.Errors.y, c.ParentLodCenterRadius.xyz, c.ParentLodCenterRadius.w, cam, scale) <= threshold) return;
    // Görüş konisi (ya da gölge kademesinin ışık hacmi)
    GInst I = insts[u_FirstInstance + inst];
    mat4 W = mat4(I.C0, I.C1, I.C2, I.C3);
    vec3 wc = (W * vec4(c.CenterRadius.xyz, 1.0)).xyz;
    float wr = c.CenterRadius.w * scale;
    for (int p = 0; p < 6; ++p)
        if (dot(u_Frustum[p].xyz, wc) + u_Frustum[p].w < -wr) return;
    if (u_Mode == 0 || u_Mode == 3)
    {
        emit(c, inst, u_CounterSlot);
        return;
    }
    uint bit = inst * u_ClusterCount + ci;
    bool wasVisible = ((visBits[bit >> 5u] >> (bit & 31u)) & 1u) != 0u;
    if (u_Mode == 1)
    {
        if (wasVisible) emit(c, inst, u_CounterSlot);
        return;
    }
    // 2. geçiş: bu karenin derinliğine göre görünürlük; önceki karede görünmeyip artık görünen çizilir
    bool visible = hzbVisible(wc, wr);
    if (visible && !wasVisible) emit(c, inst, u_CounterSlot);
    if (!visible) atomicAdd(counters[u_BatchCounters + 6u], 1u);
    if (visible != wasVisible)
    {
        if (visible) atomicOr(visBits[bit >> 5u], 1u << (bit & 31u));
        else atomicAnd(visBits[bit >> 5u], ~(1u << (bit & 31u)));
    }
}
)";

        // HZB: 0. seviye derinlik kopyasından, sonrakiler 2x2 (tek boyutta 3) en uzak derinlik
        const char* kHZBSrc = R"(#version 450 core
layout(local_size_x = 8, local_size_y = 8) in;
uniform sampler2D u_Source;       // 0. seviye: derinlik dokusu, sonrakiler: HZB'nin önceki seviyesi
uniform int u_SourceLevel;
uniform ivec2 u_SourceSize;
uniform ivec2 u_DestSize;
uniform int u_First;
layout(r32f, binding = 0) uniform writeonly image2D u_Dest;
void main()
{
    ivec2 p = ivec2(gl_GlobalInvocationID.xy);
    if (p.x >= u_DestSize.x || p.y >= u_DestSize.y) return;
    if (u_First == 1)
    {
        imageStore(u_Dest, p, vec4(texelFetch(u_Source, p, 0).r));
        return;
    }
    ivec2 s = p * 2;
    float d = 0.0;
    int ex = (p.x == u_DestSize.x - 1 && (u_SourceSize.x & 1) == 1) ? 2 : 1;
    int ey = (p.y == u_DestSize.y - 1 && (u_SourceSize.y & 1) == 1) ? 2 : 1;
    for (int y = 0; y <= ey; ++y)
        for (int x = 0; x <= ex; ++x)
            d = max(d, texelFetch(u_Source, min(s + ivec2(x, y), u_SourceSize - 1), u_SourceLevel).r);
    imageStore(u_Dest, p, vec4(d));
}
)";

        struct MeshGPU
        {
            std::weak_ptr<ClusterMesh> Source;
            GLuint Buffer = 0;
            uint32_t Count = 0;
            uint64_t LastUsed = 0;
        };

        // Kareler arası kalan parti kaynakları (komut tamponu, görünürlük bitleri)
        struct Persist
        {
            GLuint Commands = 0;
            uint32_t Capacity = 0;
            GLuint Visibility = 0;
            uint32_t VisWords = 0;
            uint32_t VisInstances = 0, VisClusters = 0;
            uint64_t LastUsed = 0;
            bool Overflow = false;
        };

        struct Batch
        {
            std::shared_ptr<ClusterMesh> Mesh;
            std::shared_ptr<Material> Mat;
            bool Lightmapped = false, CastShadows = true;
            uint64_t Key = 0;
            uint32_t FirstInstance = 0, InstanceCount = 0;
            uint32_t ClusterCount = 0;
            GLuint ClusterBuffer = 0;
            Persist* P = nullptr;
        };

        struct Readback
        {
            GLuint Staging = 0;
            size_t Bytes = 0;
            GLsync Fence = nullptr;
            int Batches = 0;
            std::vector<uint32_t> InstanceEntities;   // genel örnek -> varlık
            std::vector<std::pair<uint64_t, uint32_t>> Keys;   // parti -> (anahtar, kapasite): taşma algılaması
        };

        struct State
        {
            bool Tried = false, Supported = false, Enabled = true, Occlusion = true;
            GLuint Cull = 0, HZBProg = 0;
            std::unordered_map<const ClusterMesh*, MeshGPU> Meshes;
            std::unordered_map<uint64_t, Persist> Persistent;
            std::vector<Batch> Batches;
            std::unordered_set<uint32_t> Handled;
            std::vector<GInst> Instances;
            std::vector<GCull> Culls;
            std::vector<uint32_t> InstanceEntity;
            GLuint InstanceBuf = 0, CullBuf = 0, CounterBuf = 0;
            size_t InstanceCap = 0, CullCap = 0, CounterCap = 0;
            uint32_t View = 0;
            int ShadowRound = 0;
            uint64_t Frame = 0;
            // HZB
            GLuint DepthCopy = 0, DepthFBO = 0, HZB = 0;
            int DepthW = 0, DepthH = 0, DepthFormat = 0, HZBMips = 0;
            bool HZBReady = false;
            // İstatistik (birkaç kare gecikmeli, beklemeden)
            Readback Ring[3];
            int RingNext = 0;
            ClusterRenderer::Stats Info;
            std::unordered_map<uint32_t, glm::uvec2> EntityStats;   // varlık -> (küme, üçgen), son okunan
        };
        State& S() { static auto& s = *new State(); return s; }

        void Mix(uint64_t& h, uint64_t v) { h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2); }

        GLuint Compile(const char* name, const char* src)
        {
            GLuint sh = glCreateShader(GL_COMPUTE_SHADER);
            glShaderSource(sh, 1, &src, nullptr);
            glCompileShader(sh);
            GLint ok = 0;
            glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
            if (!ok)
            {
                char log[4096] = {};
                glGetShaderInfoLog(sh, sizeof(log), nullptr, log);
                CQ_CORE_ERROR("[Virtual Geometry] {0} compile failed: {1}", name, log);
                glDeleteShader(sh);
                return 0;
            }
            GLuint prog = glCreateProgram();
            glAttachShader(prog, sh);
            glLinkProgram(prog);
            glDeleteShader(sh);
            glGetProgramiv(prog, GL_LINK_STATUS, &ok);
            if (!ok)
            {
                glDeleteProgram(prog);
                return 0;
            }
            return prog;
        }

        bool EnsureSupported(State& s)
        {
            if (s.Tried)
                return s.Supported;
            s.Tried = true;
            if (!glMultiDrawElementsIndirectCount || !glDispatchCompute)
            {
                CQ_CORE_INFO("[Virtual Geometry] GPU-driven clusters unavailable (no glMultiDrawElementsIndirectCount); using CPU selection");
                return false;
            }
            s.Cull = Compile("cluster cull", kCullSrc);
            s.HZBProg = Compile("HZB", kHZBSrc);
            s.Supported = s.Cull && s.HZBProg;
            return s.Supported;
        }

        void Grow(GLuint& buf, size_t& cap, size_t bytes)
        {
            if (buf && cap >= bytes)
                return;
            if (buf) glDeleteBuffers(1, &buf);
            cap = std::max<size_t>(bytes + bytes / 2, 4096);
            glCreateBuffers(1, &buf);
            glNamedBufferData(buf, (GLsizeiptr)cap, nullptr, GL_DYNAMIC_DRAW);
        }

        MeshGPU& MeshBuffer(State& s, const std::shared_ptr<ClusterMesh>& cm)
        {
            MeshGPU& m = s.Meshes[cm.get()];
            m.LastUsed = s.Frame;
            if (m.Buffer && m.Source.lock() == cm)
                return m;
            const auto& clusters = cm->GetClusters();
            std::vector<GCluster> data(clusters.size());
            for (size_t i = 0; i < clusters.size(); ++i)
            {
                const ClusterInfo& c = clusters[i];
                GCluster& g = data[i];
                g.CenterRadius = glm::vec4(c.Center, c.Radius);
                g.LodCenterRadius = glm::vec4(c.LodCenter, c.LodRadius);
                g.ParentLodCenterRadius = glm::vec4(c.ParentLodCenter, c.ParentLodRadius);
                g.Errors = glm::vec2(c.Error, std::min(c.ParentError, 1e30f));
                g.Range = glm::uvec2(c.IndexOffset, c.IndexCount);
            }
            if (m.Buffer) glDeleteBuffers(1, &m.Buffer);
            glCreateBuffers(1, &m.Buffer);
            glNamedBufferData(m.Buffer, (GLsizeiptr)std::max<size_t>(data.size() * sizeof(GCluster), 16), data.empty() ? nullptr : data.data(), GL_STATIC_DRAW);
            m.Count = (uint32_t)data.size();
            m.Source = cm;
            return m;
        }

        // Kümeleri seç (görüş + LOD [+ örtme]) ve komutları yaz
        void Cull(State& s, const Batch& b, int mode, uint32_t slot, const glm::vec4* planes, const RenderCamera& cam)
        {
            const uint32_t batchIndex = (uint32_t)(&b - s.Batches.data());
            const uint32_t counterBase = batchIndex * kCountersPerBatch;
            const uint32_t zero = 0;
            glClearNamedBufferSubData(s.CounterBuf, GL_R32UI, (GLintptr)((counterBase + slot) * 4), 4, GL_RED_INTEGER, GL_UNSIGNED_INT, &zero);
            glUseProgram(s.Cull);
            auto U = [&](const char* n) { return glGetUniformLocation(s.Cull, n); };
            glUniform1ui(U("u_ClusterCount"), b.ClusterCount);
            glUniform1ui(U("u_InstanceCount"), b.InstanceCount);
            glUniform1ui(U("u_FirstInstance"), b.FirstInstance);
            glUniform1ui(U("u_MaxDraws"), b.P->Capacity);
            glUniform1ui(U("u_CounterSlot"), counterBase + slot);
            glUniform1ui(U("u_BatchCounters"), counterBase);
            glUniform1ui(U("u_InstanceStats"), (uint32_t)s.Batches.size() * kCountersPerBatch);
            glUniform4fv(U("u_Frustum"), 6, &planes[0].x);
            glUniform1f(U("u_PixelScale"), std::abs(cam.Projection[1][1]) * (float)std::max(cam.ViewportHeight, 1u) * 0.5f);
            glUniform1i(U("u_Ortho"), cam.Orthographic ? 1 : 0);
            glUniform1i(U("u_Mode"), mode);
            const glm::mat4 vp = cam.Projection * cam.View;
            glUniformMatrix4fv(U("u_ViewProj"), 1, GL_FALSE, &vp[0][0]);
            glUniform2f(U("u_HZBSize"), (float)s.DepthW, (float)s.DepthH);
            glUniform1i(U("u_HZBMips"), s.HZBMips);
            glUniform1i(U("u_HZB"), 0);
            if (mode == 2)
                glBindTextureUnit(0, s.HZB);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, b.ClusterBuffer);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, s.InstanceBuf);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, s.CullBuf);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, b.P->Commands);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, s.CounterBuf);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, b.P->Visibility);
            const uint64_t threads = (uint64_t)b.ClusterCount * b.InstanceCount;
            glDispatchCompute((GLuint)((threads + 63) / 64), 1, 1);
            glMemoryBarrier(GL_COMMAND_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT);
        }

        void DrawBatch(State& s, const Batch& b, uint32_t slot)
        {
            const uint32_t batchIndex = (uint32_t)(&b - s.Batches.data());
            Renderer3D::DrawClusterBatch(*b.Mesh, b.Mat, s.InstanceBuf, b.P->Commands, s.CounterBuf,
                                         (batchIndex * kCountersPerBatch + slot) * 4, b.P->Capacity, b.Lightmapped, true);
        }

        // Derinliği örneklenebilir kopyaya al (MSAA çözülür), HZB piramidini kur
        bool BuildHZB(State& s)
        {
            GLint drawFbo = 0;
            glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFbo);
            if (drawFbo == 0)
                return false;
            GLint vp[4];
            glGetIntegerv(GL_VIEWPORT, vp);
            if (vp[2] <= 0 || vp[3] <= 0)
                return false;
            GLint objType = GL_NONE, objName = 0, format = 0;
            GLenum attachment = GL_DEPTH_ATTACHMENT;
            glGetNamedFramebufferAttachmentParameteriv(drawFbo, attachment, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &objType);
            if (objType == GL_NONE)
            {
                attachment = GL_DEPTH_STENCIL_ATTACHMENT;
                glGetNamedFramebufferAttachmentParameteriv(drawFbo, attachment, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &objType);
            }
            if (objType == GL_NONE)
                return false;
            glGetNamedFramebufferAttachmentParameteriv(drawFbo, attachment, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME, &objName);
            if (objType == GL_TEXTURE)
                glGetTextureLevelParameteriv((GLuint)objName, 0, GL_TEXTURE_INTERNAL_FORMAT, &format);
            else if (objType == GL_RENDERBUFFER)
                glGetNamedRenderbufferParameteriv((GLuint)objName, GL_RENDERBUFFER_INTERNAL_FORMAT, &format);
            if (format == 0)
                return false;
            if (!s.DepthCopy || s.DepthW != vp[2] || s.DepthH != vp[3] || s.DepthFormat != format)
            {
                if (s.DepthCopy) glDeleteTextures(1, &s.DepthCopy);
                if (s.DepthFBO) glDeleteFramebuffers(1, &s.DepthFBO);
                if (s.HZB) glDeleteTextures(1, &s.HZB);
                glCreateTextures(GL_TEXTURE_2D, 1, &s.DepthCopy);
                glTextureStorage2D(s.DepthCopy, 1, (GLenum)format, vp[2], vp[3]);
                glTextureParameteri(s.DepthCopy, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                glTextureParameteri(s.DepthCopy, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                glTextureParameteri(s.DepthCopy, GL_TEXTURE_COMPARE_MODE, GL_NONE);
                glCreateFramebuffers(1, &s.DepthFBO);
                const bool stencil = format == GL_DEPTH24_STENCIL8 || format == GL_DEPTH32F_STENCIL8;
                glNamedFramebufferTexture(s.DepthFBO, stencil ? GL_DEPTH_STENCIL_ATTACHMENT : GL_DEPTH_ATTACHMENT, s.DepthCopy, 0);
                s.DepthW = vp[2];
                s.DepthH = vp[3];
                s.DepthFormat = format;
                s.HZBMips = 1 + (int)std::floor(std::log2((float)std::max(vp[2], vp[3])));
                glCreateTextures(GL_TEXTURE_2D, 1, &s.HZB);
                glTextureStorage2D(s.HZB, s.HZBMips, GL_R32F, vp[2], vp[3]);
                glTextureParameteri(s.HZB, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
                glTextureParameteri(s.HZB, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            }
            glBlitNamedFramebuffer((GLuint)drawFbo, s.DepthFBO, vp[0], vp[1], vp[0] + vp[2], vp[1] + vp[3], 0, 0, vp[2], vp[3],
                                   GL_DEPTH_BUFFER_BIT, GL_NEAREST);
            glUseProgram(s.HZBProg);
            auto U = [&](const char* n) { return glGetUniformLocation(s.HZBProg, n); };
            glUniform1i(U("u_Source"), 0);
            int w = s.DepthW, h = s.DepthH;
            for (int level = 0; level < s.HZBMips; ++level)
            {
                const int dw = std::max(1, s.DepthW >> level), dh = std::max(1, s.DepthH >> level);
                glUniform1i(U("u_First"), level == 0 ? 1 : 0);
                glUniform1i(U("u_SourceLevel"), level == 0 ? 0 : level - 1);
                glUniform2i(U("u_SourceSize"), w, h);
                glUniform2i(U("u_DestSize"), dw, dh);
                glBindTextureUnit(0, level == 0 ? s.DepthCopy : s.HZB);
                glBindImageTexture(0, s.HZB, level, GL_FALSE, 0, GL_WRITE_ONLY, GL_R32F);
                glDispatchCompute((GLuint)((dw + 7) / 8), (GLuint)((dh + 7) / 8), 1);
                glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
                w = dw;
                h = dh;
            }
            glBindImageTexture(0, 0, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_R32F);
            glBindTextureUnit(0, 0);
            glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)drawFbo);
            glViewport(vp[0], vp[1], vp[2], vp[3]);
            return true;
        }

        // Birkaç kare önceki sayaçlar hazırsa istatistik (GPU'yu beklemeden)
        void ConsumeReadback(State& s)
        {
            for (auto& r : s.Ring)
            {
                if (!r.Fence)
                    continue;
                const GLenum st = glClientWaitSync(r.Fence, 0, 0);
                if (st != GL_ALREADY_SIGNALED && st != GL_CONDITION_SATISFIED)
                    continue;
                glDeleteSync(r.Fence);
                r.Fence = nullptr;
                std::vector<uint32_t> data(r.Bytes / 4);
                if (!data.empty())
                    glGetNamedBufferSubData(r.Staging, 0, (GLsizeiptr)r.Bytes, data.data());
                ClusterRenderer::Stats st2 = s.Info;
                st2.ClustersDrawn = st2.TrianglesDrawn = st2.ClustersOccluded = 0;
                for (int b = 0; b < r.Batches && (size_t)(b * kCountersPerBatch + 7) < data.size(); ++b)
                {
                    st2.ClustersDrawn += data[(size_t)b * kCountersPerBatch + 0] + data[(size_t)b * kCountersPerBatch + 1];
                    st2.ClustersOccluded += data[(size_t)b * kCountersPerBatch + 6];
                    st2.TrianglesDrawn += data[(size_t)b * kCountersPerBatch + 7];
                    // Bir turun çizim sayısı kapasiteye ulaştıysa (komutlar sığmadı) parti bir sonraki karede büyür
                    if ((size_t)b < r.Keys.size())
                        for (int k = 0; k < 6; ++k)
                            if (data[(size_t)b * kCountersPerBatch + k] >= r.Keys[(size_t)b].second)
                            {
                                auto it = s.Persistent.find(r.Keys[(size_t)b].first);
                                if (it != s.Persistent.end())
                                    it->second.Overflow = true;
                                break;
                            }
                }
                s.EntityStats.clear();
                const size_t base = (size_t)r.Batches * kCountersPerBatch;
                for (size_t i = 0; i < r.InstanceEntities.size() && base + i * 2 + 1 < data.size(); ++i)
                {
                    auto& e = s.EntityStats[r.InstanceEntities[i]];
                    e.x += data[base + i * 2];
                    e.y += data[base + i * 2 + 1];
                }
                s.Info.ClustersDrawn = st2.ClustersDrawn;
                s.Info.ClustersOccluded = st2.ClustersOccluded;
                s.Info.TrianglesDrawn = st2.TrianglesDrawn;
            }
        }
    }

    namespace ClusterRenderer
    {
        bool Supported() { return EnsureSupported(S()); }
        const Stats& GetStats() { return S().Info; }
        void SetOcclusionCulling(bool on) { S().Occlusion = on; }
        bool GetOcclusionCulling() { return S().Occlusion; }
        void SetEnabled(bool on) { S().Enabled = on; }
        bool GetEnabled() { return S().Enabled; }
        bool Handles(uint32_t entity) { return S().Handled.count(entity) != 0; }

        void Gather(Scene& scene, const RenderCamera& cam, uint32_t view, const std::function<bool(uint32_t)>& drawable)
        {
            State& s = S();
            ++s.Frame;
            s.View = view;
            s.Handled.clear();
            s.Batches.clear();
            s.Instances.clear();
            s.Culls.clear();
            s.InstanceEntity.clear();
            s.ShadowRound = 0;
            s.HZBReady = false;
            if (!s.Enabled || !EnsureSupported(s))
                return;
            ConsumeReadback(s);
            auto& reg = scene.m_Registry;
            // Parti: aynı küme meshi + malzeme + lightmap durumu + gölge düşürme
            std::unordered_map<uint64_t, size_t> index;
            std::vector<std::vector<std::pair<GInst, GCull>>> items;
            std::vector<std::vector<uint32_t>> owners;
            for (auto e : reg.view<TransformComponent, MeshRendererComponent, VirtualGeometryComponent>())
            {
                if (!drawable((uint32_t)e))
                    continue;
                auto& vg = reg.get<VirtualGeometryComponent>(e);
                const auto& mr = reg.get<MeshRendererComponent>(e);
                if (!vg.Enabled || vg.VisualizeClusters || !mr.IsModel() || !mr.ModelData || mr.ModelData->IsSkinned)
                    continue;
                if (reg.all_of<VertexPaintComponent>(e))
                    continue;
                const Model& model = *mr.ModelData;
                std::vector<std::shared_ptr<ClusterMesh>> cms;
                bool ready = !model.Meshes.empty();
                for (const auto& m : model.Meshes)
                {
                    auto cm = m ? VirtualGeometry::Acquire(m, vg.ClusterSize) : nullptr;
                    if (!cm || !cm->IsUploaded()) { ready = false; break; }
                    cms.push_back(cm);
                }
                if (!ready)
                    continue;   // eski yol (tam ayrıntı) hazır olana dek
                vg.Ready = true;
                s.Handled.insert((uint32_t)e);
                const glm::mat4& w = reg.get<TransformComponent>(e).WorldTransform;
                const glm::vec3 camLocal = glm::vec3(glm::inverse(w) * glm::vec4(cam.Position, 1.0f));
                const float scale = std::max({ glm::length(glm::vec3(w[0])), glm::length(glm::vec3(w[1])), glm::length(glm::vec3(w[2])) });
                const auto* tag = reg.try_get<TagComponent>(e);
                const bool lightmapped = tag && tag->IsStatic;
                int clusters = 0, levels = 0, total = 0;
                for (size_t i = 0; i < cms.size(); ++i)
                {
                    const auto material = i < model.Materials.size() ? model.Materials[i] : nullptr;
                    uint64_t key = 0x5A17Eull;
                    Mix(key, (uint64_t)(uintptr_t)cms[i].get());
                    Mix(key, (uint64_t)(uintptr_t)material.get());
                    Mix(key, lightmapped ? 1 : 0);
                    Mix(key, mr.CastShadows ? 1 : 0);
                    Mix(key, s.View);
                    auto it = index.find(key);
                    if (it == index.end())
                    {
                        it = index.emplace(key, s.Batches.size()).first;
                        Batch b;
                        b.Mesh = cms[i];
                        b.Mat = material;
                        b.Lightmapped = lightmapped;
                        b.CastShadows = mr.CastShadows;
                        b.Key = key;
                        s.Batches.push_back(b);
                        items.emplace_back();
                        owners.emplace_back();
                    }
                    GInst gi;
                    gi.C0 = w[0]; gi.C1 = w[1]; gi.C2 = w[2]; gi.C3 = w[3];
                    gi.Color = glm::vec4(mr.Tint, 1.0f);
                    GCull gc;
                    gc.CameraLocalScale = glm::vec4(camLocal, scale);
                    gc.Params = glm::vec4(vg.ErrorThreshold, vg.ShadowErrorThreshold, 0.0f, 0.0f);
                    items[it->second].push_back({ gi, gc });
                    owners[it->second].push_back((uint32_t)e);
                    clusters += (int)cms[i]->GetClusters().size();
                    levels = std::max(levels, cms[i]->GetLevelCount());
                    total += (int)cms[i]->GetLeafTriangleCount();
                }
                vg.Clusters = clusters;
                vg.Levels = levels;
                vg.TrianglesTotal = total;
                auto st = s.EntityStats.find((uint32_t)e);
                vg.ClustersDrawn = st != s.EntityStats.end() ? (int)st->second.x : 0;
                vg.TrianglesDrawn = st != s.EntityStats.end() ? (int)st->second.y : 0;
            }
            if (s.Batches.empty())
            {
                s.Info.Instances = 0;
                s.Info.Batches = 0;
                return;
            }
            // Genel tamponlar: partiler sırayla
            for (size_t b = 0; b < s.Batches.size(); ++b)
            {
                Batch& batch = s.Batches[b];
                batch.FirstInstance = (uint32_t)s.Instances.size();
                batch.InstanceCount = (uint32_t)items[b].size();
                for (auto& [gi, gc] : items[b])
                {
                    s.Instances.push_back(gi);
                    s.Culls.push_back(gc);
                }
                for (uint32_t o : owners[b])
                    s.InstanceEntity.push_back(o);
                MeshGPU& mg = MeshBuffer(s, batch.Mesh);
                batch.ClusterBuffer = mg.Buffer;
                batch.ClusterCount = mg.Count;
                Persist& p = s.Persistent[batch.Key];
                p.LastUsed = s.Frame;
                // Komut kapasitesi: önceki karelerde taştıysa büyür
                const uint64_t worst = (uint64_t)batch.ClusterCount * batch.InstanceCount;
                uint32_t want = (uint32_t)std::min<uint64_t>(worst, std::max<uint64_t>(p.Capacity, std::min<uint64_t>(worst, 65536)));
                if (p.Overflow)
                    want = (uint32_t)std::min<uint64_t>(worst, std::min<uint64_t>((uint64_t)p.Capacity * 2, kMaxCapacity));
                want = std::max<uint32_t>(want, 1);
                if (!p.Commands || p.Capacity < want)
                {
                    if (p.Commands) glDeleteBuffers(1, &p.Commands);
                    glCreateBuffers(1, &p.Commands);
                    glNamedBufferData(p.Commands, (GLsizeiptr)want * 20, nullptr, GL_DYNAMIC_DRAW);
                    p.Capacity = want;
                    p.Overflow = false;
                }
                // Görünürlük bitleri (örnek x küme); örnek / küme sayısı değişince hepsi görünür kabul edilir
                if (!p.Visibility || p.VisInstances != batch.InstanceCount || p.VisClusters != batch.ClusterCount)
                {
                    const uint32_t words = (uint32_t)((worst + 31) / 32);
                    if (p.Visibility) glDeleteBuffers(1, &p.Visibility);
                    glCreateBuffers(1, &p.Visibility);
                    glNamedBufferData(p.Visibility, (GLsizeiptr)std::max<uint32_t>(words, 1) * 4, nullptr, GL_DYNAMIC_DRAW);
                    const uint32_t ones = 0xFFFFFFFFu;
                    glClearNamedBufferData(p.Visibility, GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_INT, &ones);
                    p.VisWords = words;
                    p.VisInstances = batch.InstanceCount;
                    p.VisClusters = batch.ClusterCount;
                }
                batch.P = &p;
            }
            Grow(s.InstanceBuf, s.InstanceCap, s.Instances.size() * sizeof(GInst));
            Grow(s.CullBuf, s.CullCap, s.Culls.size() * sizeof(GCull));
            glNamedBufferSubData(s.InstanceBuf, 0, (GLsizeiptr)(s.Instances.size() * sizeof(GInst)), s.Instances.data());
            glNamedBufferSubData(s.CullBuf, 0, (GLsizeiptr)(s.Culls.size() * sizeof(GCull)), s.Culls.data());
            const size_t counters = s.Batches.size() * kCountersPerBatch + s.Instances.size() * 2;
            Grow(s.CounterBuf, s.CounterCap, counters * 4);
            const uint32_t zero = 0;
            glClearNamedBufferSubData(s.CounterBuf, GL_R32UI, 0, (GLsizeiptr)(counters * 4), GL_RED_INTEGER, GL_UNSIGNED_INT, &zero);
            s.Info.Instances = (int)s.Instances.size();
            s.Info.Batches = (int)s.Batches.size();
            // Eski kaynaklar
            if (s.Frame % 240 == 0)
            {
                for (auto it = s.Persistent.begin(); it != s.Persistent.end();)
                {
                    if (s.Frame - it->second.LastUsed > 600)
                    {
                        if (it->second.Commands) glDeleteBuffers(1, &it->second.Commands);
                        if (it->second.Visibility) glDeleteBuffers(1, &it->second.Visibility);
                        it = s.Persistent.erase(it);
                    }
                    else ++it;
                }
                for (auto it = s.Meshes.begin(); it != s.Meshes.end();)
                {
                    if (it->second.Source.expired() || s.Frame - it->second.LastUsed > 600)
                    {
                        if (it->second.Buffer) glDeleteBuffers(1, &it->second.Buffer);
                        it = s.Meshes.erase(it);
                    }
                    else ++it;
                }
            }
        }

        void DrawShadows(const glm::vec4* planes, const std::function<void()>& bindDepthShader)
        {
            State& s = S();
            if (s.Batches.empty() || !planes)
                return;
            const uint32_t slot = 2u + (uint32_t)(s.ShadowRound++ % 4);
            const RenderCamera& cam = Renderer3D::GetCamera();
            for (const Batch& b : s.Batches)
            {
                if (!b.CastShadows)
                    continue;
                Cull(s, b, 0, slot, planes, cam);
            }
            bindDepthShader();
            for (const Batch& b : s.Batches)
            {
                if (!b.CastShadows)
                    continue;
                const uint32_t batchIndex = (uint32_t)(&b - s.Batches.data());
                b.Mesh->GetVertexArray()->Bind();
                GLint vao = 0;
                glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &vao);
                Renderer3D::BindInstanceAttributesVAO((uint32_t)vao, s.InstanceBuf);
                glBindBuffer(GL_DRAW_INDIRECT_BUFFER, b.P->Commands);
                glBindBuffer(GL_PARAMETER_BUFFER, s.CounterBuf);
                glMultiDrawElementsIndirectCount(GL_TRIANGLES, GL_UNSIGNED_INT, nullptr,
                                                 (GLintptr)((batchIndex * kCountersPerBatch + slot) * 4), (GLsizei)b.P->Capacity, 0);
                glBindBuffer(GL_PARAMETER_BUFFER, 0);
                glBindBuffer(GL_DRAW_INDIRECT_BUFFER, 0);
                Renderer3D::UnbindInstanceAttributesVAO((uint32_t)vao);
                Renderer::GetStats().ShadowDrawCalls++;
            }
        }

        void DrawOpaque(const RenderCamera& cam)
        {
            State& s = S();
            if (s.Batches.empty())
                return;
            CQ_PROFILE_GPU("Virtual Geometry");
            glm::vec4 planes[6];
            Renderer3D::ExtractFrustum(cam.Projection * cam.View, planes);
            const int mode = s.Occlusion ? 1 : 3;
            for (const Batch& b : s.Batches)
                Cull(s, b, mode, 0, planes, cam);
            for (const Batch& b : s.Batches)
                DrawBatch(s, b, 0);
            // Stats katmanı: GPU'da seçilen üçgenler (birkaç kare önceki okuma; beklemeden)
            Renderer::GetStats().Triangles += s.Info.TrianglesDrawn;
        }

        void DrawDisoccluded(const RenderCamera& cam)
        {
            State& s = S();
            if (s.Batches.empty() || !s.Occlusion)
                return;
            CQ_PROFILE_GPU("Virtual Geometry Occlusion");
            if (!BuildHZB(s))
            {
                // HZB kurulamadı (varsayılan çerçeve tamponu): hepsi görünür kabul edilir, bir sonraki kare 1. geçiş tümünü çizer
                for (const Batch& b : s.Batches)
                {
                    const uint32_t ones = 0xFFFFFFFFu;
                    glClearNamedBufferData(b.P->Visibility, GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_INT, &ones);
                }
                return;
            }
            glm::vec4 planes[6];
            Renderer3D::ExtractFrustum(cam.Projection * cam.View, planes);
            for (const Batch& b : s.Batches)
                Cull(s, b, 2, 1, planes, cam);
            glBindTextureUnit(0, 0);
            for (const Batch& b : s.Batches)
                DrawBatch(s, b, 1);
        }

        void EndView()
        {
            State& s = S();
            if (s.Batches.empty())
            {
                s.Handled.clear();
                return;
            }
            // Sayaçları beklemeden oku: kopya + çit; birkaç kare sonra hazırsa istatistik
            Readback& r = s.Ring[s.RingNext];
            s.RingNext = (s.RingNext + 1) % 3;
            if (r.Fence) { glDeleteSync(r.Fence); r.Fence = nullptr; }
            const size_t bytes = (s.Batches.size() * kCountersPerBatch + s.Instances.size() * 2) * 4;
            if (!r.Staging || r.Bytes < bytes)
            {
                if (r.Staging) glDeleteBuffers(1, &r.Staging);
                glCreateBuffers(1, &r.Staging);
                glNamedBufferData(r.Staging, (GLsizeiptr)bytes, nullptr, GL_STREAM_READ);
            }
            r.Bytes = bytes;
            glCopyNamedBufferSubData(s.CounterBuf, r.Staging, 0, 0, (GLsizeiptr)bytes);
            r.Fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
            r.Batches = (int)s.Batches.size();
            r.InstanceEntities = s.InstanceEntity;
            r.Keys.clear();
            for (const Batch& b : s.Batches)
                r.Keys.push_back({ b.Key, b.P ? b.P->Capacity : 0u });
            // Taşan partiler bir sonraki karede büyür (çizim sayısı kapasiteye ulaştıysa)
            s.Handled.clear();
            s.Batches.clear();
        }
    }
}
