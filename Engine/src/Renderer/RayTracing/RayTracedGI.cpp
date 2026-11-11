#include "RayTracedGI.h"
#include "RayTracingScene.h"

#include "Core/Logging/Log.h"
#include "Core/Profiling/FrameProfiler.h"
#include "Core/Time/TimeManager.h"
#include "Core/Utils/FileSystem.h"
#include "Renderer/RenderSettings.h"
#include "Renderer/Renderer.h"
#include "Renderer/Utilities/Renderer3D/Renderer3D.h"
#include "Scene/Scene.h"

#include <glad/glad.h>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <random>
#include <sstream>
#include <unordered_map>

namespace Conqueror
{
    namespace
    {
        constexpr int kIrrTile = 8, kDistTile = 16;
        constexpr int kRayBudget = 98304;        // karede en çok ışın (yakın problar her karede, uzaklar sırayla)
        constexpr int kMaxRays = 256;

        // Çekirdeklerin ortak parçası (RayTracingScene::CommonGLSL + DynamicGI.glsl'den sonra)
        const char* kKernelCommon = R"(
uniform ivec3 u_Counts;
uniform ivec3 u_MinCell;
uniform float u_Spacing;
uniform int u_LayerColumns;
uniform int u_RayCount;
uniform int u_Groups;           // bu karede güncellenen prob sayısı (liste uzunluğu)
uniform int u_ProbeTotal;
uniform mat3 u_Rotation;
// Bu karenin prob listesi: x yuva sırası (x + Cx * (z + Cz * y)), y güncelleme periyodu (kare; zamanla harmanlama için)
layout(rg32i, binding = 2) uniform readonly iimage2D u_UpdateList;

ivec3 giSlotFromLinear(int linear)
{
    int cx = u_Counts.x, cz = u_Counts.z;
    return ivec3(linear % cx, linear / (cx * cz), (linear / cx) % cz);
}
ivec3 giCellFromSlot(ivec3 slot) { return u_MinCell + CQ_GISlot(slot - u_MinCell, u_Counts); }
// Grubun probu (-1: liste dışı)
int giLinear(int group, out float period)
{
    period = 1.0;
    if (group >= u_Groups) return -1;
    ivec2 item = imageLoad(u_UpdateList, ivec2(group, 0)).xy;
    period = float(max(item.y, 1));
    return item.x < u_ProbeTotal ? item.x : -1;
}
vec3 giFibonacci(int i, int n)
{
    float fi = float(i), fn = float(n);
    float phi = 2.0 * PI * fract(fi * 0.6180339887);
    float cosTheta = 1.0 - (2.0 * fi + 1.0) / fn;
    float sinTheta = sqrt(clamp(1.0 - cosTheta * cosTheta, 0.0, 1.0));
    return vec3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
}
vec3 giRayDir(int i) { return normalize(u_Rotation * giFibonacci(i, u_RayCount)); }
// Oktahedral kenar texel'i -> kopyalandığı iç texel (RTXGI kenar düzeni)
ivec2 giBorderSource(ivec2 t, int T)
{
    int n = T - 2;
    bool bx = t.x == 0 || t.x == T - 1, by = t.y == 0 || t.y == T - 1;
    if (bx && by) return ivec2(t.x == 0 ? n : 1, t.y == 0 ? n : 1);
    if (by) return ivec2(T - 1 - t.x, t.y == 0 ? 1 : n);
    if (bx) return ivec2(t.x == 0 ? 1 : n, T - 1 - t.y);
    return t;
}
vec3 giTexelDir(ivec2 src, int T)
{
    float n = float(T - 2);
    vec2 e = ((vec2(src) - 1.0 + 0.5) / n) * 2.0 - 1.0;
    return CQ_GIOctDecode(e);
}
)";

        // Hacim kayınca yeni hücrelerin yuvaları boşaltılır (durum 0: güncellenene dek örneklenmez)
        const char* kResetMain = R"(
uniform ivec3 u_OldMinCell;
uniform int u_ResetAll;
layout(rgba16f, binding = 0) uniform image2D u_ProbeImg;
void main()
{
    int linear = int(gl_GlobalInvocationID.x);
    if (linear >= u_ProbeTotal) return;
    ivec3 slot = giSlotFromLinear(linear);
    ivec3 cNew = u_MinCell + CQ_GISlot(slot - u_MinCell, u_Counts);
    ivec3 cOld = u_OldMinCell + CQ_GISlot(slot - u_OldMinCell, u_Counts);
    if (u_ResetAll == 1 || any(notEqual(cNew, cOld)))
        imageStore(u_ProbeImg, CQ_GITile(slot, u_Counts, u_LayerColumns), vec4(0.0));
}
)";

        // Işın izleme: grup başına bir prob, iş parçacıkları ışınları paylaşır
        const char* kTraceMain = R"(
layout(rgba16f, binding = 0) uniform writeonly image2D u_RayData;
uniform int u_Bounce;           // 1: hacimde veri var (çoklu sekme)
uniform float u_DiffuseBoost;
uniform uint u_FrameSeed;
void main()
{
    int group = int(gl_WorkGroupID.x);
    float period;
    int linear = giLinear(group, period);
    if (linear < 0) return;
    ivec3 slot = giSlotFromLinear(linear);
    ivec3 cell = giCellFromSlot(slot);
    vec4 pd = texelFetch(u_GIProbeData, CQ_GITile(slot, u_Counts, u_LayerColumns), 0);
    vec3 origin = vec3(cell) * u_Spacing + pd.xyz;
    for (int r = int(gl_LocalInvocationID.x); r < u_RayCount; r += 64)
    {
        s_Seed = (uint(linear) * 9781u + uint(r) * 6271u + u_FrameSeed * 26699u) | 1u;
        rnd(); rnd();
        vec3 dir = giRayDir(r);
        float t;
        vec2 bc;
        int ti = trace(origin, dir, 1e30, t, bc, false);
        vec4 outv;
        if (ti < 0)
            outv = vec4(min(ambient(dir), vec3(60000.0)), 1e4);
        else
        {
            Hit h = hitInfo(ti, origin, dir, t, bc);
            if (h.Back && !h.TwoSided)
                outv = vec4(0.0, 0.0, 0.0, -t * 0.2);   // geometrinin içi (arka yüz)
            else
            {
                Mat m = h.M;
                // Unreal Diffuse Color Boost: sekmede renk güçlenir; enerji korunumu için üst sınır
                m.Albedo.rgb = min(pow(clamp(m.Albedo.rgb, 0.0, 1.0), vec3(1.0 / max(u_DiffuseBoost, 0.1))), vec3(0.96));
                vec3 po = h.P + h.Ng * max(2e-3, t * 1e-4);
                vec3 rad = m.Emission.rgb + directLightStochastic(h.P, po, h.N, m);
                if (u_Bounce == 1)
                {
                    float cov;
                    vec3 irr = CQ_GIIrradiance(h.P, h.N, -dir, cov);
                    irr = mix(ambient(h.N), irr, cov);
                    rad += m.Albedo.rgb * (1.0 - m.Params.x) * irr;
                }
                outv = vec4(min(rad, vec3(60000.0)), t);
            }
        }
        imageStore(u_RayData, ivec2(r, group), outv);
    }
}
)";

        // Işınım harmanlama: grup başına bir prob, iş parçacığı başına bir atlas texel'i (8x8, kenarlar sarmalı kopya)
        const char* kBlendIrradianceMain = R"(
layout(rgba16f, binding = 0) uniform image2D u_IrrImg;
layout(rgba16f, binding = 1) uniform readonly image2D u_RayData;
uniform float u_DeltaTime;      // kare süresi (s)
uniform float u_Tau;            // uyum süresi (s; Update Speed)
shared vec4 s_Ray[64];
shared vec3 s_Dir[64];
void main()
{
    int group = int(gl_WorkGroupID.x);
    float period;
    int linear = giLinear(group, period);
    if (linear < 0) return;
    ivec3 slot = giSlotFromLinear(linear);
    ivec2 tile = CQ_GITile(slot, u_Counts, u_LayerColumns);
    vec4 pd = texelFetch(u_GIProbeData, tile, 0);
    ivec2 t = ivec2(gl_LocalInvocationID.xy);
    vec3 texelDir = giTexelDir(giBorderSource(t, 8), 8);
    vec3 sum = vec3(0.0);
    float wsum = 0.0, skySum = 0.0;
    uint li = gl_LocalInvocationIndex;
    for (int base = 0; base < u_RayCount; base += 64)
    {
        int idx = base + int(li);
        if (idx < u_RayCount)
        {
            s_Ray[li] = imageLoad(u_RayData, ivec2(idx, group));
            s_Dir[li] = giRayDir(idx);
        }
        barrier();
        int n = min(64, u_RayCount - base);
        for (int k = 0; k < n; ++k)
        {
            float w = max(dot(texelDir, s_Dir[k]), 0.0);
            sum += s_Ray[k].rgb * w;
            skySum += (s_Ray[k].w >= 1000.0 ? 1.0 : 0.0) * w;   // ıskalayan ışın: gökyüzüne ulaştı
            wsum += w;
        }
        barrier();
    }
    // Kosinüs ağırlıklı ortalama ışıma = E / π (yüzey albedo ile çarpar); alfa: gökyüzü görünürlüğü
    vec3 result = wsum > 1e-6 ? sum / wsum : vec3(0.0);
    float skyVis = wsum > 1e-6 ? skySum / wsum : 1.0;
    ivec2 coord = tile * 8 + t;
    vec3 enc = sqrt(max(result, vec3(0.0)));
    vec4 prev4 = imageLoad(u_IrrImg, coord);
    vec3 prev = prev4.rgb;
    // Zamanla harmanlama: prob 'period' karede bir güncellenir, uyum süresi u_Tau (kare hızından bağımsız)
    float h = clamp(exp(-period * u_DeltaTime / max(u_Tau, 1e-3)), 0.0, 0.985);
    float hVis = pd.w < 0.5 ? 0.0 : h;
    if (pd.w >= 0.5)
    {
        // Büyük ışık değişiminde (ışık açıldı / kapandı, kapı açıldı: parlaklık 3 kattan fazla) hızlı uyum. Az ışının çarptığı küçük
        // parlak yüzeyin güncellemeler arası oynaması bunun altındadır: zamanla harmanlanır, titremez.
        // (RTXGI'nin parlaklık sınırlaması orta büyüklükte değişimleri çok yavaşlatıyordu: yeni ışık saniyelerce birikiyordu)
        float a = max(lumOf(prev * prev), 0.002), b = max(lumOf(enc * enc), 0.002);
        if (max(a, b) / min(a, b) > 3.0)
            h = max(h - 0.5, 0.0);
    }
    else
        h = 0.0;   // yeni prob: geçmiş yok
    imageStore(u_IrrImg, coord, vec4(mix(enc, prev, h), mix(skyVis, prev4.a, hVis)));
}
)";

        // Uzaklık harmanlama: 16x16 texel, keskin kosinüs ağırlığı; ortalama ve kare ortalaması (Chebyshev görünürlüğü)
        const char* kBlendDistanceMain = R"(
layout(rg16f, binding = 0) uniform image2D u_DistImg;
layout(rgba16f, binding = 1) uniform readonly image2D u_RayData;
uniform float u_DeltaTime;
uniform float u_Tau;
uniform float u_MaxDistance;
shared float s_Dist[256];
shared vec3 s_Dir[256];
void main()
{
    int group = int(gl_WorkGroupID.x);
    float period;
    int linear = giLinear(group, period);
    if (linear < 0) return;
    ivec3 slot = giSlotFromLinear(linear);
    ivec2 tile = CQ_GITile(slot, u_Counts, u_LayerColumns);
    vec4 pd = texelFetch(u_GIProbeData, tile, 0);
    ivec2 t = ivec2(gl_LocalInvocationID.xy);
    vec3 texelDir = giTexelDir(giBorderSource(t, 16), 16);
    vec2 sum = vec2(0.0);
    float wsum = 0.0;
    uint li = gl_LocalInvocationIndex;
    for (int base = 0; base < u_RayCount; base += 256)
    {
        int idx = base + int(li);
        if (idx < u_RayCount)
        {
            s_Dist[li] = min(abs(imageLoad(u_RayData, ivec2(idx, group)).w), u_MaxDistance);
            s_Dir[li] = giRayDir(idx);
        }
        barrier();
        int n = min(256, u_RayCount - base);
        for (int k = 0; k < n; ++k)
        {
            float w = pow(max(dot(texelDir, s_Dir[k]), 0.0), 50.0);
            float d = s_Dist[k];
            sum += vec2(d, d * d) * w;
            wsum += w;
        }
        barrier();
    }
    vec2 result = wsum > 1e-6 ? sum / wsum : vec2(u_MaxDistance, u_MaxDistance * u_MaxDistance);
    ivec2 coord = tile * 16 + t;
    vec2 prev = imageLoad(u_DistImg, coord).rg;
    float h = pd.w < 0.5 ? 0.0 : clamp(exp(-period * u_DeltaTime / max(u_Tau, 1e-3)), 0.0, 0.99);
    imageStore(u_DistImg, coord, vec4(mix(result, prev, h), 0.0, 0.0));
}
)";

        // Prob yeri ve durumu: geometrinin içinde kalan prob dışarı, yüzeye çok yakın olan uzağa kaydırılır (hücre içinde);
        // ışınlarının çoğu arka yüze çarpan prob kapatılır (örneklenmez)
        const char* kRelocateMain = R"(
layout(rgba16f, binding = 0) uniform image2D u_ProbeImg;
layout(rgba16f, binding = 1) uniform readonly image2D u_RayData;
void main()
{
    int group = int(gl_GlobalInvocationID.x);
    float period;
    int linear = giLinear(group, period);
    if (linear < 0) return;
    ivec3 slot = giSlotFromLinear(linear);
    ivec2 tile = CQ_GITile(slot, u_Counts, u_LayerColumns);
    vec4 pd = imageLoad(u_ProbeImg, tile);
    int backfaces = 0;
    float closestBack = 1e27, closestFront = 1e27, farthestFront = 0.0;
    vec3 closestBackDir = vec3(0.0), closestFrontDir = vec3(0.0), farthestFrontDir = vec3(0.0);
    for (int r = 0; r < u_RayCount; ++r)
    {
        float d = imageLoad(u_RayData, ivec2(r, group)).w;
        vec3 dir = giRayDir(r);
        if (d < 0.0)
        {
            ++backfaces;
            float dd = -d * 5.0;
            if (dd < closestBack) { closestBack = dd; closestBackDir = dir; }
        }
        else
        {
            if (d < closestFront) { closestFront = d; closestFrontDir = dir; }
            if (d > farthestFront) { farthestFront = d; farthestFrontDir = dir; }
        }
    }
    float ratio = float(backfaces) / float(max(u_RayCount, 1));
    float minFront = 0.2 * u_Spacing;
    vec3 offset = pd.xyz;
    vec3 full = offset;
    if (ratio > 0.25)
        full += closestBackDir * (closestBack + minFront * 0.5);
    else if (closestFront < minFront && dot(closestFrontDir, farthestFrontDir) <= 0.5)
        full += farthestFrontDir * min(minFront, farthestFront * 0.5);
    if (all(lessThan(abs(full), vec3(0.45 * u_Spacing))))
        offset = full;
    imageStore(u_ProbeImg, tile, vec4(offset, ratio > 0.25 ? 2.0 : 1.0));
}
)";

        struct Programs
        {
            bool Tried = false;
            GLuint Reset = 0, Trace = 0, BlendIrr = 0, BlendDist = 0, Relocate = 0;
        };

        struct Volume
        {
            glm::ivec3 Counts{ 0 };
            int LayerColumns = 1;
            float Spacing = 0.0f;
            glm::ivec3 MinCell{ 0 };
            bool HasMinCell = false;
            GLuint Irradiance = 0, Distance = 0, ProbeData = 0, RayData = 0, UpdateList = 0;
            int RayDataW = 0, RayDataH = 0, UpdateListW = 0;
            uint64_t Frame = 0;
            uint64_t LastUsed = 0;
            uint32_t View = 0;
            Renderer3D::DynamicGIBinding Bound;   // son bağlanan (ikincil kameralar)
            uint64_t FramesSinceReset = 0;   // çoklu sekme, yakın problar bir kez dolduktan sonra
            bool ResetPending = true;
            std::vector<uint8_t> Fresh;      // yuva başına: sıfırlandı, henüz güncellenmedi (önce bunlar)
            std::vector<glm::ivec2> List;    // bu karenin (yuva, periyot) listesi
        };

        struct State
        {
            Programs P;
            std::unordered_map<uint64_t, Volume> Volumes;
            RayTracedGI::Stats Info;
            uint64_t Frame = 0;
            bool ResetRequested = false;
            std::mt19937 Rng{ 1234u };
        };
        State& S() { static auto& s = *new State(); return s; }

        std::string ReadShaderInclude(const char* relative)
        {
            const std::string path = FileSystem::ResolveResourcePath(std::string("Engine/src/Shaders/") + relative);
            std::ifstream in(path, std::ios::binary);
            if (!in)
                return {};
            std::stringstream ss;
            ss << in.rdbuf();
            return ss.str();
        }

        bool CompilePrograms(Programs& p)
        {
            if (p.Tried)
                return p.Trace != 0;
            p.Tried = true;
            const std::string gi = ReadShaderInclude("Include/DynamicGI.glsl");
            if (gi.empty())
            {
                CQ_CORE_ERROR("[Ray Traced GI] Include/DynamicGI.glsl not found");
                return false;
            }
            const std::string head = std::string("#define CQ_GI_COMPUTE\n") + gi + kKernelCommon;
            p.Reset = RayTracingScene::CompileCompute("Ray Traced GI Reset", "layout(local_size_x = 64) in;", head + kResetMain);
            p.Trace = RayTracingScene::CompileCompute("Ray Traced GI Trace", "layout(local_size_x = 64) in;", head + kTraceMain);
            p.BlendIrr = RayTracingScene::CompileCompute("Ray Traced GI Irradiance", "layout(local_size_x = 8, local_size_y = 8) in;", head + kBlendIrradianceMain);
            p.BlendDist = RayTracingScene::CompileCompute("Ray Traced GI Distance", "layout(local_size_x = 16, local_size_y = 16) in;", head + kBlendDistanceMain);
            p.Relocate = RayTracingScene::CompileCompute("Ray Traced GI Relocate", "layout(local_size_x = 32) in;", head + kRelocateMain);
            if (!p.Reset || !p.Trace || !p.BlendIrr || !p.BlendDist || !p.Relocate)
            {
                for (GLuint* prog : { &p.Reset, &p.Trace, &p.BlendIrr, &p.BlendDist, &p.Relocate })
                    if (*prog) { glDeleteProgram(*prog); *prog = 0; }
                return false;
            }
            return true;
        }

        void DeleteTextures(Volume& v)
        {
            for (GLuint* t : { &v.Irradiance, &v.Distance, &v.ProbeData, &v.RayData, &v.UpdateList })
                if (*t) { glDeleteTextures(1, t); *t = 0; }
            v.RayDataW = v.RayDataH = v.UpdateListW = 0;
        }

        GLuint MakeTexture(GLenum format, int w, int h, bool linear)
        {
            GLuint t = 0;
            glCreateTextures(GL_TEXTURE_2D, 1, &t);
            glTextureStorage2D(t, 1, format, std::max(w, 1), std::max(h, 1));
            glTextureParameteri(t, GL_TEXTURE_MIN_FILTER, linear ? GL_LINEAR : GL_NEAREST);
            glTextureParameteri(t, GL_TEXTURE_MAG_FILTER, linear ? GL_LINEAR : GL_NEAREST);
            glTextureParameteri(t, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTextureParameteri(t, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            const float zero[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
            glClearTexImage(t, 0, format == GL_RG16F ? GL_RG : GL_RGBA, GL_FLOAT, zero);
            return t;
        }

        // Rastgele dönüş (her karede): ışın yönleri sabit desende kalmasın
        glm::mat3 RandomRotation(std::mt19937& rng)
        {
            std::normal_distribution<float> n(0.0f, 1.0f);
            glm::quat q(n(rng), n(rng), n(rng), n(rng));
            const float len = std::sqrt(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
            if (len < 1e-6f)
                return glm::mat3(1.0f);
            q = glm::quat(q.w / len, q.x / len, q.y / len, q.z / len);
            return glm::mat3_cast(q);
        }

        void SetCommon(GLuint prog, const Volume& v, int rays, int groups, int total, const glm::mat3& rot)
        {
            glUseProgram(prog);
            auto U = [&](const char* n) { return glGetUniformLocation(prog, n); };
            glUniform3i(U("u_Counts"), v.Counts.x, v.Counts.y, v.Counts.z);
            glUniform3i(U("u_MinCell"), v.MinCell.x, v.MinCell.y, v.MinCell.z);
            glUniform1f(U("u_Spacing"), v.Spacing);
            glUniform1i(U("u_LayerColumns"), v.LayerColumns);
            glUniform1i(U("u_RayCount"), rays);
            glUniform1i(U("u_Groups"), groups);
            glUniform1i(U("u_ProbeTotal"), total);
            glUniformMatrix3fv(U("u_Rotation"), 1, GL_FALSE, &rot[0][0]);
            glBindImageTexture(2, v.UpdateList, 0, GL_FALSE, 0, GL_READ_ONLY, GL_RG32I);
        }

        int PosMod(int a, int n) { const int m = a % n; return m < 0 ? m + n : m; }

        // Bu karede güncellenecek problar. Kameraya yakın problar sık (görüntüde en etkili, ışık değişimine hemen uyar), uzaktakiler
        // seyrek güncellenir; periyotlar ışın bütçesine sığacak kadar büyütülür. Sıfırlanmış (yeni) problar öncelikli
        void BuildUpdateList(Volume& v, const glm::ivec3& cameraCell, int maxProbes, uint64_t frame)
        {
            const glm::ivec3 c = v.Counts;
            const int total = c.x * c.y * c.z;
            // Chebyshev uzaklığı (hücre) eşikleri ve periyotlar
            const int limits[4] = { 2, 4, 8, 1 << 20 };
            int periods[4] = { 1, 2, 8, 32 };
            static thread_local std::vector<glm::ivec2> s_Tier[4];   // (yuva, faz)
            for (auto& t : s_Tier) t.clear();
            for (int L = 0; L < total; ++L)
            {
                const glm::ivec3 slot(L % c.x, L / (c.x * c.z), (L / c.x) % c.z);
                const glm::ivec3 cell(v.MinCell.x + PosMod(slot.x - v.MinCell.x, c.x), v.MinCell.y + PosMod(slot.y - v.MinCell.y, c.y),
                                      v.MinCell.z + PosMod(slot.z - v.MinCell.z, c.z));
                const glm::ivec3 d = glm::abs(cell - cameraCell);
                const int cheb = std::max(d.x, std::max(d.y, d.z));
                int t = 0;
                while (cheb > limits[t]) ++t;
                // Faz hücreye bağlı (hacim kayınca prob aynı karelerde güncellenmeye devam eder)
                const uint32_t phase = (((uint32_t)cell.x * 73856093u) ^ ((uint32_t)cell.y * 19349663u) ^ ((uint32_t)cell.z * 83492791u)) & 0x7FFFFFFFu;
                s_Tier[t].push_back({ L, (int)phase });
            }
            auto load = [&]() { float n = 0.0f; for (int t = 0; t < 4; ++t) n += (float)s_Tier[t].size() / (float)periods[t]; return n; };
            for (int guard = 0; guard < 40 && load() > (float)maxProbes; ++guard)
            {
                // Dıştan içe: en dış katmanın periyodu iki katına (256'ya kadar), sonra bir içtekinin
                int t = 3;
                while (t > 0 && periods[t] >= 256) --t;
                periods[t] *= 2;
            }
            v.List.clear();
            static thread_local std::vector<uint8_t> s_Listed;
            s_Listed.assign((size_t)total, 0);
            // Önce yeni problar (yakından uzağa), sonra periyodu gelenler
            for (int t = 0; t < 4; ++t)
                for (const glm::ivec2& e : s_Tier[t])
                {
                    if ((int)v.List.size() >= maxProbes) break;
                    if (v.Fresh[(size_t)e.x])
                    {
                        v.Fresh[(size_t)e.x] = 0;
                        s_Listed[(size_t)e.x] = 1;
                        v.List.push_back({ e.x, 1 });
                    }
                }
            for (int t = 0; t < 4; ++t)
                for (const glm::ivec2& e : s_Tier[t])
                {
                    if ((int)v.List.size() >= maxProbes) break;
                    if ((frame + (uint64_t)e.y) % (uint64_t)periods[t] != 0 || s_Listed[(size_t)e.x]) continue;
                    v.List.push_back({ e.x, periods[t] });
                }
        }

        // Atlas / prob dokularını örnekleyici olarak bağlar (çoklu sekme ve kararlar için) + hacim parametreleri
        void BindVolumeSamplers(GLuint prog, const Volume& v, const glm::vec4& v0, const glm::vec4& v1, const glm::vec4& v2, int firstUnit)
        {
            auto U = [&](const char* n) { return glGetUniformLocation(prog, n); };
            glUniform4fv(U("u_GIVolume0"), 1, &v0.x);
            glUniform4fv(U("u_GIVolume1"), 1, &v1.x);
            glUniform4fv(U("u_GIVolume2"), 1, &v2.x);
            glUniform1i(U("u_GIIrradiance"), firstUnit);
            glUniform1i(U("u_GIDistance"), firstUnit + 1);
            glUniform1i(U("u_GIProbeData"), firstUnit + 2);
            glBindTextureUnit((GLuint)firstUnit, v.Irradiance);
            glBindTextureUnit((GLuint)firstUnit + 1, v.Distance);
            glBindTextureUnit((GLuint)firstUnit + 2, v.ProbeData);
        }
    }

    namespace RayTracedGI
    {
        const Stats& GetStats() { return S().Info; }

        void Reset() { S().ResetRequested = true; }

        void EndView()
        {
            Renderer3D::SetDynamicGI(Renderer3D::DynamicGIBinding{});
        }

        void Update(Scene& scene, const RenderCamera& camera, const PostProcessSettings& pp, uint32_t view, bool trace)
        {
            State& s = S();
            const bool enabled = pp.Enabled && pp.RayTracedGI && !camera.Orthographic;
            if (!enabled)
            {
                if (trace)
                    s.Info = Stats{};
                Renderer3D::SetDynamicGI(Renderer3D::DynamicGIBinding{});
                return;
            }
            if (!trace)
            {
                // İkincil kamera: görünümün son hacmi (güncellenmeden)
                Renderer3D::DynamicGIBinding binding;
                for (auto& [key, v] : s.Volumes)
                    if (v.View == view && v.Irradiance && v.HasMinCell && s.Frame - v.LastUsed <= 2 && v.Bound.Volume0.w > 0.0f)
                    {
                        binding = v.Bound;
                        break;
                    }
                Renderer3D::SetDynamicGI(binding);
                return;
            }
            ++s.Frame;
            if (!CompilePrograms(s.P))
            {
                Renderer3D::SetDynamicGI(Renderer3D::DynamicGIBinding{});
                return;
            }
            CQ_PROFILE_SAMPLE("Render.RayTracedGI", Rendering);
            CQ_PROFILE_GPU("Ray Traced GI");

            uint64_t key = (uint64_t)(uintptr_t)&scene;
            key ^= (uint64_t)view * 0x9E3779B97F4A7C15ull;
            Volume& v = s.Volumes[key];
            v.LastUsed = s.Frame;
            v.View = view;
            // Kullanılmayan hacimler (kapanan görünüm / sahne) bırakılır
            if (s.Frame % 300 == 0)
                for (auto it = s.Volumes.begin(); it != s.Volumes.end();)
                {
                    if (s.Frame - it->second.LastUsed > 600) { DeleteTextures(it->second); it = s.Volumes.erase(it); }
                    else ++it;
                }

            // Hacim boyutu: yatay 2 x görüş uzaklığı, dikeyde yarısı; aralık ve sayılar sınırlı (bellek, ışın bütçesi)
            const float spacing = std::clamp(pp.RTGIProbeSpacing, 0.25f, 16.0f);
            const int cxz = std::clamp((int)std::lround(2.0f * std::max(pp.RTGIViewDistance, 1.0f) / spacing) + 1, 4, 48);
            const int cy = std::clamp((cxz + 1) / 2, 4, 24);
            const glm::ivec3 counts(cxz, cy, cxz);
            const int layerColumns = std::max(1, (int)std::ceil(std::sqrt((float)counts.y)));
            const int layerRows = (counts.y + layerColumns - 1) / layerColumns;
            const int total = counts.x * counts.y * counts.z;
            const int rays = std::clamp((pp.RTGIRaysPerProbe + 31) / 32 * 32, 32, kMaxRays);

            if (counts != v.Counts || spacing != v.Spacing || !v.Irradiance || s.ResetRequested)
            {
                if (counts != v.Counts || spacing != v.Spacing || !v.Irradiance)
                {
                    DeleteTextures(v);
                    v.Counts = counts;
                    v.Spacing = spacing;
                    v.LayerColumns = layerColumns;
                    const int atlasW = counts.x * layerColumns, atlasH = counts.z * layerRows;
                    v.Irradiance = MakeTexture(GL_RGBA16F, atlasW * kIrrTile, atlasH * kIrrTile, true);
                    v.Distance = MakeTexture(GL_RG16F, atlasW * kDistTile, atlasH * kDistTile, true);
                    v.ProbeData = MakeTexture(GL_RGBA16F, atlasW, atlasH, false);
                }
                v.ResetPending = true;
                v.HasMinCell = false;
                v.FramesSinceReset = 0;
                v.Fresh.assign((size_t)total, 1);
                s.ResetRequested = false;
            }

            // Sahne (BVH): hazır olmayan büyük meshler beklenmez
            const RayTracingScene::Stats& rt = RayTracingScene::Update(scene, false);

            // Hacim kamerayla hücre hücre kayar (dikeyde biraz yukarı: yer altındaki problar az). Ölü bölge: kamera hacim merkezinden
            // 0.75 hücreden fazla uzaklaşınca kayar (hücre sınırında gidip gelen kamera, örn. zıplarken, kenar dilimini sıfırlayıp durmasın)
            const glm::vec3 camCell = camera.Position / spacing;
            const glm::ivec3 centerOffset(counts.x / 2, counts.y / 2 - counts.y / 6, counts.z / 2);
            glm::ivec3 minCell = glm::ivec3(glm::floor(camCell)) - centerOffset;
            if (v.HasMinCell)
            {
                minCell = v.MinCell;
                const glm::vec3 rel = camCell - (glm::vec3(v.MinCell + centerOffset) + 0.5f);
                for (int a = 0; a < 3; ++a)
                    if (std::abs(rel[a]) > 0.75f)
                        minCell[a] = (int)std::floor(camCell[a]) - centerOffset[a];
            }
            const glm::ivec3 oldMin = v.HasMinCell ? v.MinCell : minCell;
            const bool moved = v.HasMinCell && minCell != v.MinCell;
            // Kamera bir hacimden uzağa ışınlandı: hepsi yeniden
            const glm::ivec3 jump = glm::abs(minCell - oldMin);
            const bool teleport = moved && (jump.x >= counts.x || jump.y >= counts.y || jump.z >= counts.z);
            v.MinCell = minCell;
            v.HasMinCell = true;

            // Kayınca yeni hücrelerin yuvaları (ya da ışınlanmada tamamı) yeni: önce onlar güncellenir
            if ((int)v.Fresh.size() != total)
                v.Fresh.assign((size_t)total, 1);
            if (teleport)
                std::fill(v.Fresh.begin(), v.Fresh.end(), 1);
            else if (moved)
                for (int L = 0; L < total; ++L)
                {
                    const glm::ivec3 slot(L % counts.x, L / (counts.x * counts.z), (L / counts.x) % counts.z);
                    for (int a = 0; a < 3; ++a)
                        if (oldMin[a] + PosMod(slot[a] - oldMin[a], counts[a]) != minCell[a] + PosMod(slot[a] - minCell[a], counts[a]))
                        {
                            v.Fresh[(size_t)L] = 1;
                            break;
                        }
                }

            // Bu karede güncellenecek problar (yakınlar sık, uzaklar seyrek; ışın bütçesi)
            const int maxProbes = std::max(64, kRayBudget / rays);
            BuildUpdateList(v, glm::ivec3(glm::floor(camCell)), maxProbes, v.Frame);
            const int groups = (int)v.List.size();
            if (v.UpdateListW < maxProbes || !v.UpdateList)
            {
                if (v.UpdateList) glDeleteTextures(1, &v.UpdateList);
                glCreateTextures(GL_TEXTURE_2D, 1, &v.UpdateList);
                glTextureStorage2D(v.UpdateList, 1, GL_RG32I, maxProbes, 1);
                v.UpdateListW = maxProbes;
            }
            if (groups > 0)
                glTextureSubImage2D(v.UpdateList, 0, 0, 0, groups, 1, GL_RG_INTEGER, GL_INT, v.List.data());

            // Zamanla harmanlama: değişime uyum süresi (Scene Lighting Update Speed); çekirdek prob periyoduyla ölçekler
            const float dt = std::clamp(TimeManager::GetUnscaledDeltaTime(), 1.0f / 240.0f, 0.1f);
            // Sıfırlamadan (açılış, ışınlanma) sonraki ilk anlarda daha hızlı: sekmeler birkaç karede birikir, ışık yavaş yavaş dolmaz
            const float warmup = std::clamp(0.25f + (float)v.FramesSinceReset / 90.0f, 0.25f, 1.0f);
            const float tau = 0.3f * warmup / std::clamp(pp.RTGIUpdateSpeed, 0.1f, 8.0f);

            const glm::vec4 v0((float)minCell.x, (float)minCell.y, (float)minCell.z, spacing);
            const glm::vec4 v1((float)counts.x, (float)counts.y, (float)counts.z, (float)layerColumns);
            const glm::vec4 v2(0.12f, 0.18f, std::max(pp.RTGIIntensity, 0.0f), std::clamp(pp.RTGISkyLeaking, 0.0f, 1.0f));

            // Işın verisi: (ışın, güncellenen prob)
            if (v.RayDataW != rays || v.RayDataH < maxProbes)
            {
                if (v.RayData) glDeleteTextures(1, &v.RayData);
                v.RayData = MakeTexture(GL_RGBA16F, rays, maxProbes, false);
                v.RayDataW = rays;
                v.RayDataH = maxProbes;
            }

            // Doku birimleri: compute örnekleyicileri en üst birimlerde (forward çizimin birimlerine dokunmasın)
            GLint maxUnits = 32;
            glGetIntegerv(GL_MAX_COMPUTE_TEXTURE_IMAGE_UNITS, &maxUnits);
            const int unitBase = std::max(8, std::min(maxUnits, 32) - 6);
            const glm::mat3 rot = RandomRotation(s.Rng);

            // 1) Yeni hücrelerin yuvaları (ya da tamamı)
            if (v.ResetPending || moved)
            {
                SetCommon(s.P.Reset, v, rays, groups, total, rot);
                glUniform3i(glGetUniformLocation(s.P.Reset, "u_OldMinCell"), oldMin.x, oldMin.y, oldMin.z);
                glUniform1i(glGetUniformLocation(s.P.Reset, "u_ResetAll"), (v.ResetPending || teleport) ? 1 : 0);
                glBindImageTexture(0, v.ProbeData, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA16F);
                glDispatchCompute((GLuint)((total + 63) / 64), 1, 1);
                glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
                if (v.ResetPending || teleport)
                {
                    v.FramesSinceReset = 0;
                    v.ResetPending = false;
                }
            }
            if (groups == 0)
            {
                ++v.Frame;
                ++v.FramesSinceReset;
                Renderer3D::SetDynamicGI(v.Bound);
                return;
            }

            // 2) Işın izleme
            {
            CQ_PROFILE_GPU("GI Trace");
            RayTracingScene::Bind(s.P.Trace, unitBase + 3, unitBase + 4, unitBase + 5);
            SetCommon(s.P.Trace, v, rays, groups, total, rot);
            BindVolumeSamplers(s.P.Trace, v, v0, v1, v2, unitBase);
            glUniform1i(glGetUniformLocation(s.P.Trace, "u_Bounce"), v.FramesSinceReset > 4 ? 1 : 0);
            glUniform1f(glGetUniformLocation(s.P.Trace, "u_DiffuseBoost"), std::clamp(pp.RTGIDiffuseBoost, 0.1f, 4.0f));
            glUniform1ui(glGetUniformLocation(s.P.Trace, "u_FrameSeed"), (GLuint)(s.Frame & 0xFFFFFFFFu));
            glBindImageTexture(0, v.RayData, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
            glDispatchCompute((GLuint)groups, 1, 1);
            glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
            }

            // 3) Işınım ve uzaklık harmanlama
            CQ_PROFILE_GPU("GI Blend");
            SetCommon(s.P.BlendIrr, v, rays, groups, total, rot);
            BindVolumeSamplers(s.P.BlendIrr, v, v0, v1, v2, unitBase);
            glUniform1f(glGetUniformLocation(s.P.BlendIrr, "u_DeltaTime"), dt);
            glUniform1f(glGetUniformLocation(s.P.BlendIrr, "u_Tau"), tau);
            glBindImageTexture(0, v.Irradiance, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA16F);
            glBindImageTexture(1, v.RayData, 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA16F);
            glDispatchCompute((GLuint)groups, 1, 1);

            SetCommon(s.P.BlendDist, v, rays, groups, total, rot);
            BindVolumeSamplers(s.P.BlendDist, v, v0, v1, v2, unitBase);
            glUniform1f(glGetUniformLocation(s.P.BlendDist, "u_DeltaTime"), dt);
            glUniform1f(glGetUniformLocation(s.P.BlendDist, "u_Tau"), tau * 1.5f);
            glUniform1f(glGetUniformLocation(s.P.BlendDist, "u_MaxDistance"), spacing * 1.5f * 1.7320508f);
            glBindImageTexture(0, v.Distance, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RG16F);
            glBindImageTexture(1, v.RayData, 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA16F);
            glDispatchCompute((GLuint)groups, 1, 1);
            glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);

            // 4) Prob yeri / durumu (harmanlamalar bu karenin "yeni prob" durumunu gördükten sonra)
            SetCommon(s.P.Relocate, v, rays, groups, total, rot);
            glBindImageTexture(0, v.ProbeData, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA16F);
            glBindImageTexture(1, v.RayData, 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA16F);
            glDispatchCompute((GLuint)((groups + 31) / 32), 1, 1);
            glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

            glBindImageTexture(0, 0, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA16F);
            glBindImageTexture(1, 0, 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA16F);
            glBindImageTexture(2, 0, 0, GL_FALSE, 0, GL_READ_ONLY, GL_RG32I);
            glUseProgram(0);
            Renderer3D::InvalidateFrameBindings();
            ++v.Frame;
            ++v.FramesSinceReset;

            Renderer3D::DynamicGIBinding binding;
            binding.Volume0 = v0;
            binding.Volume1 = v1;
            binding.Volume2 = v2;
            binding.Irradiance = v.Irradiance;
            binding.Distance = v.Distance;
            binding.ProbeData = v.ProbeData;
            v.Bound = binding;
            Renderer3D::SetDynamicGI(binding);

            s.Info.Active = true;
            s.Info.Counts = counts;
            s.Info.Probes = total;
            s.Info.ProbesUpdated = groups;
            s.Info.RaysTraced = groups * rays;
            s.Info.Spacing = spacing;
            s.Info.Triangles = rt.Triangles;
            s.Info.Instances = rt.Instances;
            s.Info.PendingBuilds = rt.PendingBuilds;
        }

        bool ReadProbe(uint32_t view, const glm::ivec3& cell, glm::vec3& irradianceUp, int& state)
        {
            State& s = S();
            for (auto& [key, v] : s.Volumes)
            {
                if (v.View != view || s.Frame - v.LastUsed > 2)
                    continue;
                if (!v.Irradiance || !v.HasMinCell)
                    continue;
                const glm::ivec3 rel = cell - v.MinCell;
                if (glm::any(glm::lessThan(rel, glm::ivec3(0))) || glm::any(glm::greaterThanEqual(rel, v.Counts)))
                    return false;
                glm::ivec3 slot = cell - v.Counts * glm::ivec3(glm::floor(glm::vec3(cell) / glm::vec3(v.Counts)));
                const glm::ivec2 tile(slot.x + v.Counts.x * (slot.y % v.LayerColumns), slot.z + v.Counts.z * (slot.y / v.LayerColumns));
                float pd[4] = {};
                glGetTextureSubImage(v.ProbeData, 0, tile.x, tile.y, 0, 1, 1, 1, GL_RGBA, GL_FLOAT, sizeof(pd), pd);
                state = (int)std::lround(pd[3]);
                // Yukarı yön: oktahedral merkez (e = 0,0 -> +z); dünya yukarısı +y'nin texel'i
                const glm::vec3 up(0.0f, 1.0f, 0.0f);
                const glm::vec3 n = up / (std::abs(up.x) + std::abs(up.y) + std::abs(up.z));
                const glm::vec2 e(n.x, n.y);
                const glm::vec2 uv = (e * 0.5f + 0.5f) * 6.0f + 1.0f;
                const int tx = tile.x * kIrrTile + std::clamp((int)uv.x, 1, 6), ty = tile.y * kIrrTile + std::clamp((int)uv.y, 1, 6);
                float px[4] = {};
                glGetTextureSubImage(v.Irradiance, 0, tx, ty, 0, 1, 1, 1, GL_RGBA, GL_FLOAT, sizeof(px), px);
                const glm::vec3 enc = glm::max(glm::vec3(px[0], px[1], px[2]), glm::vec3(0.0f));
                irradianceUp = enc * enc;   // karekök kodlu
                return true;
            }
            return false;
        }
    }
}
