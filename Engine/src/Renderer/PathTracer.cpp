#include "PathTracer.h"
#include "Renderer/RayTracing/RayTracingScene.h"
#include "Renderer/Utilities/Renderer3D/Renderer3D.h"

#include <glad/glad.h>

#include <algorithm>
#include <cmath>

// Ortak sahne / BVH / ışık / gökyüzü: Renderer/RayTracing/RayTracingScene
namespace Conqueror
{
    namespace
    {
        const char* kPathTraceMain = R"(
layout(rgba32f, binding = 0) uniform image2D u_Accum;
layout(rgba16f, binding = 1) uniform writeonly image2D u_Target;
uniform mat4 u_InvViewProj;
uniform ivec2 u_Size;
uniform uint u_Frame;
uniform int u_ResolveOnly;      // 1: örnek eklenmez, yalnızca birikmiş ortalama hedefe yazılır
uniform int u_MaxBounces;
uniform float u_MaxPathIntensity;

void main()
{
    ivec2 px = ivec2(gl_GlobalInvocationID.xy);
    if (px.x >= u_Size.x || px.y >= u_Size.y) return;
    if (u_ResolveOnly == 1)
    {
        vec4 acc = imageLoad(u_Accum, px);
        imageStore(u_Target, px, vec4(acc.rgb / max(acc.w, 1.0), 1.0));
        return;
    }
    s_Seed = (uint(px.x) * 1973u + uint(px.y) * 9277u + u_Frame * 26699u) | 1u;
    for (int k = 0; k < 4; ++k) rnd();
    vec2 uv = (vec2(px) + vec2(rnd(), rnd())) / vec2(u_Size) * 2.0 - 1.0;
    vec4 a = u_InvViewProj * vec4(uv, -1.0, 1.0), b = u_InvViewProj * vec4(uv, 1.0, 1.0);
    vec3 o = a.xyz / a.w;
    vec3 d = normalize(b.xyz / b.w - o);
    // primary: kameradan görülen (gökyüzü, ışıma, ilk yüzeyin doğrudan ışığı); radiance: dolaylı (Max Path Intensity ile sınırlanır)
    vec3 primary = vec3(0.0), radiance = vec3(0.0), throughput = vec3(1.0);
    for (int bounce = 0; bounce <= u_MaxBounces; ++bounce)
    {
        float t; vec2 bc;
        int ti = trace(o, d, 1e30, t, bc, false);
        if (ti < 0)
        {
            if (bounce == 0) primary += background(d);
            else radiance += throughput * ambient(d);
            break;
        }
        // İsabet (iki seviyeli BVH: örneğin dünya matrisiyle normal, malzeme doku ortalamalı); normaller ışına bakar
        Hit hit = hitInfo(ti, o, d, t, bc);
        Mat m = hit.M;
        vec3 p = hit.P;
        vec3 n = hit.N;
        vec3 ng = hit.Ng;
        vec3 v = -d;
        vec3 po = p + ng * 1e-3;
        vec3 here = throughput * (m.Emission.rgb + directLight(p, po, n, v, m));
        if (bounce == 0) primary += here;
        else radiance += here;
        if (bounce == u_MaxBounces) break;
        // Sonraki yön: yansıma (GGX örnekleme) ya da dağınık (kosinüs)
        vec3 f0 = mix(vec3(0.04), m.Albedo.rgb, m.Params.x);
        float specP = clamp(mix(0.15, 1.0, m.Params.x) * (1.0 - m.Params.y * 0.5), 0.05, 0.95);
        vec3 up = abs(n.y) < 0.999 ? vec3(0, 1, 0) : vec3(1, 0, 0);
        vec3 tx = normalize(cross(up, n)), ty = cross(n, tx);
        vec3 nd;
        if (rnd() < specP)
        {
            float a2 = max(m.Params.y * m.Params.y, 0.002);
            float r1 = rnd(), r2 = rnd();
            float phi = 2.0 * PI * r1;
            float ct = sqrt((1.0 - r2) / (1.0 + (a2 * a2 - 1.0) * r2));
            float st = sqrt(max(1.0 - ct * ct, 0.0));
            vec3 h = normalize(tx * (cos(phi) * st) + ty * (sin(phi) * st) + n * ct);
            nd = reflect(-v, h);
            float NdotL = dot(n, nd), NdotV = max(dot(n, v), 1e-4), NdotH = max(dot(n, h), 1e-4), VdotH = max(dot(v, h), 1e-4);
            if (NdotL <= 0.0 || dot(nd, ng) <= 0.0) break;
            // GGX (D) örneklemesinde ağırlık: F * G * VdotH / (NdotV * NdotH)
            throughput *= F_Schlick(VdotH, f0) * G1(NdotV, a2) * G1(NdotL, a2) * VdotH / (NdotV * NdotH) / specP;
        }
        else
        {
            float r1 = rnd(), r2 = rnd();
            float phi = 2.0 * PI * r1, sr = sqrt(r2);
            nd = normalize(tx * (cos(phi) * sr) + ty * (sin(phi) * sr) + n * sqrt(max(1.0 - r2, 0.0)));
            if (dot(nd, ng) <= 0.0) break;
            vec3 F = F_Schlick(max(dot(n, v), 0.0), f0);
            throughput *= (1.0 - F) * (1.0 - m.Params.x) * m.Albedo.rgb / (1.0 - specP);
        }
        // Rus ruleti
        if (bounce >= 2)
        {
            float q = clamp(max(throughput.r, max(throughput.g, throughput.b)), 0.05, 0.95);
            if (rnd() > q) break;
            throughput /= q;
        }
        o = po;
        d = nd;
    }
    // Dolaylı ışığın tek örnek parlamaları (ateşböceği) renk tonu korunarak sınırlanır (Unreal Max Path Intensity)
    float peak = max(radiance.r, max(radiance.g, radiance.b));
    if (peak > u_MaxPathIntensity) radiance *= u_MaxPathIntensity / peak;
    vec4 acc = (u_Frame == 0u) ? vec4(0.0) : imageLoad(u_Accum, px);
    acc += vec4(primary + radiance, 1.0);
    imageStore(u_Accum, px, acc);
    imageStore(u_Target, px, vec4(acc.rgb / acc.w, 1.0));
}
)";

        // 299: kenar korumalı à-trous dalgacık süzgeci (Dammertz 2010 / SVGF uzamsal geçişi): 5x5 B3 çekirdeği, adım 1, 2, 4, 8 ...
        // Kenar durdurma: komşunun derinliği pikselin derinlik düzleminden (gradyanla öngörülen) uzaksa (başka yüzey) ve parlaklık
        // farkı yerel gürültü düzeyini (aynı yüzeydeki 5x5 parlaklık varyansı, geçişlerle süzülür) aşıyorsa ağırlık düşer: düz parlak
        // yüzey (ışıyan şerit) gürültülü karanlık zemine yayılmaz
        const char* kDenoiseMain = R"(
layout(rgba16f, binding = 0) uniform readonly image2D u_DnIn;
layout(rgba16f, binding = 1) uniform writeonly image2D u_DnOut;
uniform sampler2D u_DnDepth;
uniform ivec2 u_DnSize;
uniform int u_DnStep;
uniform int u_DnMode;      // 0: varyans ön geçişi (rgb + parlaklık varyansı a'ya), 1: süzgeç geçişi
uniform int u_DnLast;      // son geçiş: a = 1
uniform float u_DnNear;
uniform float u_DnFar;
float DnLinear(ivec2 p)
{
    float d = texelFetch(u_DnDepth, clamp(p, ivec2(0), u_DnSize - 1), 0).r;
    float z = d * 2.0 - 1.0;
    return 2.0 * u_DnNear * u_DnFar / max(u_DnFar + u_DnNear - z * (u_DnFar - u_DnNear), 1e-6);
}
float DnLum(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }
void main()
{
    ivec2 p = ivec2(gl_GlobalInvocationID.xy);
    if (p.x >= u_DnSize.x || p.y >= u_DnSize.y) return;
    vec4 cp = imageLoad(u_DnIn, p);
    float zp = DnLinear(p);
    float gx = 0.5 * (DnLinear(p + ivec2(1, 0)) - DnLinear(p - ivec2(1, 0)));
    float gy = 0.5 * (DnLinear(p + ivec2(0, 1)) - DnLinear(p - ivec2(0, 1)));
    float lp = DnLum(cp.rgb);
    const float k[3] = float[](0.375, 0.25, 0.0625);
    if (u_DnMode == 0)
    {
        // Aynı yüzeydeki 5x5 komşulukta parlaklık varyansı (gürültü düzeyi)
        float m1 = 0.0, m2 = 0.0, ws = 0.0;
        for (int y = -2; y <= 2; ++y)
            for (int x = -2; x <= 2; ++x)
            {
                ivec2 q = clamp(p + ivec2(x, y), ivec2(0), u_DnSize - 1);
                float expected = zp + gx * float(x) + gy * float(y);
                float w = abs(DnLinear(q) - expected) < 0.02 * zp + 1e-3 ? 1.0 : 0.0;
                float l = DnLum(imageLoad(u_DnIn, q).rgb);
                m1 += l * w; m2 += l * l * w; ws += w;
            }
        m1 /= max(ws, 1.0); m2 /= max(ws, 1.0);
        imageStore(u_DnOut, p, vec4(cp.rgb, max(m2 - m1 * m1, 0.0)));
        return;
    }
    float sigma = 4.0 * sqrt(max(cp.a, 0.0)) + 1e-3;
    vec3 sum = vec3(0.0);
    float wsum = 0.0, vsum = 0.0;
    for (int y = -2; y <= 2; ++y)
        for (int x = -2; x <= 2; ++x)
        {
            ivec2 o = ivec2(x, y) * u_DnStep;
            ivec2 q = clamp(p + o, ivec2(0), u_DnSize - 1);
            vec4 cq = imageLoad(u_DnIn, q);
            float expected = zp + gx * float(o.x) + gy * float(o.y);
            float wz = exp(-abs(DnLinear(q) - expected) / (0.01 * zp + 0.002 * float(u_DnStep) * zp + 1e-4));
            float wl = exp(-abs(lp - DnLum(cq.rgb)) / sigma);
            float w = k[abs(x)] * k[abs(y)] * wz * wl;
            sum += cq.rgb * w;
            vsum += w * w * cq.a;
            wsum += w;
        }
    wsum = max(wsum, 1e-6);
    imageStore(u_DnOut, p, vec4(sum / wsum, u_DnLast == 1 ? 1.0 : vsum / (wsum * wsum)));
}
)";

        struct State
        {
            GLuint Program = 0;
            bool ProgramTried = false;
            GLuint Accum = 0;
            GLuint DenoiseProgram = 0;
            bool DenoiseTried = false;
            GLuint DenoiseTemp[2] = { 0, 0 };
            int DenoiseW = 0, DenoiseH = 0;
            int AccumW = 0, AccumH = 0;
            uint64_t GeometryVersion = 0, LightHash = 0;
            glm::mat4 LastViewProj{ 0.0f };
            uint32_t Frame = 0;
            int LastBounces = -1;
            float LastIntensity = -1.0f;
        };
        State& S() { static auto& s = *new State(); return s; }
        PathTracer::Settings s_Settings;
    }

    namespace PathTracer
    {
        Settings& GetSettings() { return s_Settings; }
        void Reset() { S().Frame = 0; }
        int GetSampleCount() { return (int)S().Frame; }
        int GetTriangleCount() { return RayTracingScene::GetStats().Triangles; }

        // 299: az örnekli sonucun gürültüsü giderilir (target yerinde). Geçiş sayısı örnek sayısıyla azalır: 1-2 örnek 4 geçiş (adım
        // 1..8, ~30 piksellik etki), 3-8: 3, 9-32: 2, 33-128: 1, sonrası yok
        void Denoise(uint32_t target, uint32_t depth, int tw, int th, const RenderCamera& cam)
        {
            State& s = S();
            const int spp = (int)s.Frame;
            const int passes = spp <= 2 ? 4 : spp <= 8 ? 3 : spp <= 32 ? 2 : spp <= 128 ? 1 : 0;
            if (!passes || !depth || cam.Orthographic)
                return;
            if (!s.DenoiseTried)
            {
                s.DenoiseTried = true;
                s.DenoiseProgram = RayTracingScene::CompileCompute("Path Tracer Denoise", "layout(local_size_x = 8, local_size_y = 8) in;", kDenoiseMain);
            }
            if (!s.DenoiseProgram)
                return;
            if (!s.DenoiseTemp[0] || s.DenoiseW != tw || s.DenoiseH != th)
            {
                if (s.DenoiseTemp[0])
                    glDeleteTextures(2, s.DenoiseTemp);
                glCreateTextures(GL_TEXTURE_2D, 2, s.DenoiseTemp);
                glTextureStorage2D(s.DenoiseTemp[0], 1, GL_RGBA16F, tw, th);
                glTextureStorage2D(s.DenoiseTemp[1], 1, GL_RGBA16F, tw, th);
                s.DenoiseW = tw;
                s.DenoiseH = th;
            }
            glUseProgram(s.DenoiseProgram);
            auto U = [&](const char* n) { return glGetUniformLocation(s.DenoiseProgram, n); };
            glUniform2i(U("u_DnSize"), tw, th);
            glUniform1f(U("u_DnNear"), cam.NearClip);
            glUniform1f(U("u_DnFar"), cam.FarClip);
            glUniform1i(U("u_DnDepth"), 0);
            glBindTextureUnit(0, depth);
            const GLuint groupsX = ((uint32_t)tw + 7) / 8, groupsY = ((uint32_t)th + 7) / 8;
            // Varyans ön geçişi (hedef -> geçici 0), sonra süzgeç geçişleri geçici tamponlar arasında (varyans da süzülür, SVGF gibi)
            uint32_t src = target;
            for (int i = -1; i < passes; ++i)
            {
                const uint32_t dst = s.DenoiseTemp[(i + 1) & 1];
                glUniform1i(U("u_DnMode"), i < 0 ? 0 : 1);
                glUniform1i(U("u_DnLast"), i == passes - 1 ? 1 : 0);
                glUniform1i(U("u_DnStep"), i < 0 ? 1 : 1 << i);
                glBindImageTexture(0, src, 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA16F);
                glBindImageTexture(1, dst, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
                glDispatchCompute(groupsX, groupsY, 1);
                glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_UPDATE_BARRIER_BIT);
                src = dst;
            }
            glCopyImageSubData(src, GL_TEXTURE_2D, 0, 0, 0, 0, target, GL_TEXTURE_2D, 0, 0, 0, 0, tw, th, 1);
            glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT | GL_FRAMEBUFFER_BARRIER_BIT);
            glBindImageTexture(0, 0, 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA16F);
            glBindImageTexture(1, 0, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
            glBindTextureUnit(0, 0);
        }

        bool Render(Scene& scene, const RenderCamera& cam, uint32_t target, uint32_t depth)
        {
            State& s = S();
            if (!s.ProgramTried)
            {
                s.ProgramTried = true;
                s.Program = RayTracingScene::CompileCompute("Path Tracer", "layout(local_size_x = 8, local_size_y = 8) in;", kPathTraceMain);
            }
            if (!s.Program || !target)
                return false;
            // Hedefin gerçek boyutu (iç çözünürlük / Render Scale)
            GLint tw = 0, th = 0;
            glGetTextureLevelParameteriv(target, 0, GL_TEXTURE_WIDTH, &tw);
            glGetTextureLevelParameteriv(target, 0, GL_TEXTURE_HEIGHT, &th);
            if (tw <= 0 || th <= 0)
                return false;

            // Sahne değişince BVH yeniden kurulur ve birikim baştan başlar
            const RayTracingScene::Stats& stats = RayTracingScene::Update(scene, true);
            if (stats.GeometryVersion != s.GeometryVersion)
            {
                s.GeometryVersion = stats.GeometryVersion;
                s.Frame = 0;
            }
            if (!s.Accum || s.AccumW != tw || s.AccumH != th)
            {
                if (s.Accum) glDeleteTextures(1, &s.Accum);
                glCreateTextures(GL_TEXTURE_2D, 1, &s.Accum);
                glTextureStorage2D(s.Accum, 1, GL_RGBA32F, tw, th);
                s.AccumW = tw;
                s.AccumH = th;
                s.Frame = 0;
            }
            const glm::mat4 vp = cam.Projection * cam.View;
            if (vp != s.LastViewProj)
            {
                s.LastViewProj = vp;
                s.Frame = 0;
            }
            if (s_Settings.MaxBounces != s.LastBounces || s_Settings.MaxPathIntensity != s.LastIntensity)
            {
                s.LastBounces = s_Settings.MaxBounces;
                s.LastIntensity = s_Settings.MaxPathIntensity;
                s.Frame = 0;
            }

            const uint64_t lightHash = RayTracingScene::Bind(s.Program, 0, 1);
            if (lightHash != s.LightHash)
            {
                s.LightHash = lightHash;
                s.Frame = 0;
            }
            auto U = [&](const char* n) { return glGetUniformLocation(s.Program, n); };
            const glm::mat4 inv = glm::inverse(vp);
            glUniformMatrix4fv(U("u_InvViewProj"), 1, GL_FALSE, &inv[0][0]);
            glUniform2i(U("u_Size"), tw, th);
            glUniform1i(U("u_MaxBounces"), std::clamp(s_Settings.MaxBounces, 0, 32));
            glUniform1f(U("u_MaxPathIntensity"), std::max(s_Settings.MaxPathIntensity, 0.01f));
            glBindImageTexture(0, s.Accum, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA32F);
            glBindImageTexture(1, target, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
            const GLuint groupsX = ((uint32_t)tw + 7) / 8, groupsY = ((uint32_t)th + 7) / 8;
            const int maxSamples = std::max(1, s_Settings.MaxSamples);
            if ((int)s.Frame >= maxSamples)
            {
                // Yakınsadı: yeni örnek yok, hedef (bu karede raster ile çizildi) birikmiş ortalamayla yeniden doldurulur
                glUniform1i(U("u_ResolveOnly"), 1);
                glDispatchCompute(groupsX, groupsY, 1);
            }
            else
            {
                glUniform1i(U("u_ResolveOnly"), 0);
                const int spp = std::clamp(s_Settings.SamplesPerFrame, 1, 64);
                for (int k = 0; k < spp && (int)s.Frame < maxSamples; ++k)
                {
                    glUniform1ui(U("u_Frame"), s.Frame);
                    glDispatchCompute(groupsX, groupsY, 1);
                    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
                    ++s.Frame;
                }
            }
            glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT | GL_FRAMEBUFFER_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
            glBindImageTexture(0, 0, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA32F);
            glBindImageTexture(1, 0, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
            Denoise(target, depth, tw, th, cam);
            glUseProgram(0);
            return true;
        }
    }
}
