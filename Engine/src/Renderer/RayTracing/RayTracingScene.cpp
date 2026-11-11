#include "RayTracingScene.h"
#include "Scene/Scene.h"
#include "Scene/Entity.h"
#include "Scene/Components.h"
#include "Scene/MeshRendererMaterial.h"
#include "Scene/MultiMesh.h"
#include "Terrain/TerrainComponent.h"
#include "Renderer/IBL/EnvironmentMap.h"
#include "Renderer/RHI/Texture.h"
#include "Renderer/Utilities/Renderer3D/Renderer3D.h"
#include "Renderer/Utilities/Renderer3D/RendererUBO.h"
#include "Renderer/Utilities/Renderer3D/Mesh.h"
#include "Renderer/Utilities/Renderer3D/Material.h"
#include "Renderer/Utilities/Renderer3D/ModelLoader.h"
#include "Core/Utils/ThreadPool.h"
#include "Core/Logging/Log.h"

#include <glad/glad.h>
#include <glm/gtc/matrix_inverse.hpp>

#include <algorithm>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstring>
#include <future>
#include <numeric>
#include <unordered_map>
#include <vector>

namespace Conqueror
{
    namespace
    {
        // GPU düzeni (std430). Üçgen: BLAS'ın nesne uzayında köşeler + köşe normalleri
        struct Tri { glm::vec4 V0, V1, V2; glm::vec4 N0, N1, N2; };
        // BVH düğümü: Min.w = sol çocuk / ilk öğe, Max.w = öğe sayısı (0: iç düğüm); ikisi de tam sayı bitleri (floatBitsToInt)
        struct Node { glm::vec4 Min, Max; };
        // Albedo: rgb doğrusal renk, a temel kapsama (saydamlık); Emission: rgb ışıma, w ışıma dokusu ortalama yuvası (-1 yok);
        // Params: x metal, y pürüz, z bayraklar (1 iki yüzlü, 2 Cutout / saydam), w renk dokusu ortalama yuvası (-1 yok)
        struct Mat { glm::vec4 Albedo, Emission, Params; };
        // Örnek: dünya ve ters dünya matrisinin satırları (3x4); Info: x BLAS kök düğümü, y malzeme, z üçgen sayısı
        struct Inst { glm::vec4 W0, W1, W2; glm::vec4 I0, I1, I2; glm::ivec4 Info; };
        static_assert(sizeof(Inst) == 112, "Inst must match std430");

        const char* kCommonGLSL = R"(
struct Tri { vec4 V0, V1, V2, N0, N1, N2; };
struct Node { vec4 Min, Max; };
struct Mat { vec4 Albedo, Emission, Params; };
struct Inst { vec4 W0, W1, W2, I0, I1, I2; ivec4 Info; };
struct PointL { vec4 PositionRange, ColorIntensity, Attenuation, Profile0, Profile1; };
struct SpotL { vec4 PositionRange, ColorIntensity, DirectionCosOuter, AttenuationCosInner, Profile0, Profile1; };
struct AreaL { vec4 PositionRange, ColorIntensity, Right, Up; };
layout(std430, binding = 0) readonly buffer Tris { Tri tris[]; };
layout(std430, binding = 1) readonly buffer Nodes { Node nodes[]; };
layout(std430, binding = 2) readonly buffer Mats { Mat mats[]; };
layout(std430, binding = 3) readonly buffer Points { PointL points[]; };
layout(std430, binding = 4) readonly buffer Spots { SpotL spots[]; };
layout(std430, binding = 5) readonly buffer Areas { AreaL areas[]; };
layout(std430, binding = 6) readonly buffer TNodes { Node tnodes[]; };
layout(std430, binding = 7) readonly buffer Insts { Inst insts[]; };
uniform int u_NodeCount;        // TLAS düğümü (0: boş sahne)
uniform vec4 u_SunDir;          // xyz ışığın gittiği yön, w: güneş var
uniform vec3 u_SunColor;
uniform ivec3 u_LightCounts;    // nokta, spot, alan
// Gökyüzü: arka plan küpü (kamera ışını) ve aydınlatma küpü (sekmeler), motorun ortam ışığı ayarları
uniform samplerCube u_BgCube;
uniform samplerCube u_LightCube;
uniform sampler2D u_TexAvg;     // malzeme dokularının ortalaması (x: yuva), doğrusal renk + alfa
uniform ivec4 u_SkyFlags;       // x arka plan küpü var, y aydınlatma küpü var, z ortam kaynağı (0 gökyüzü, 1 renk, 2 gradyan), w gökyüzü çizilir
uniform vec4 u_BgTintExposure;
uniform vec2 u_SkyRot;          // cos, sin (skybox dönüşü)
uniform vec4 u_AmbientSky;      // rgb, a ölçek
uniform vec3 u_AmbientEquator;
uniform vec3 u_AmbientGround;
uniform vec3 u_BgColor;         // küp yoksa düz arka plan
uniform vec4 u_SunExclude;      // xyz güneşe yön, w cos(yarıçap): prosedürel gökyüzü küpündeki güneş diski sekmelerde sayılmaz (güneş doğrudan ışıkta)

const float PI = 3.14159265;
uint s_Seed;
float rnd() { s_Seed ^= s_Seed << 13; s_Seed ^= s_Seed >> 17; s_Seed ^= s_Seed << 5; return float(s_Seed & 0x00FFFFFFu) / 16777216.0; }
int g_HitInst = -1;             // son trace isabetinin örneği

vec4 texAvg(float slot) { return slot >= 0.0 ? texelFetch(u_TexAvg, ivec2(int(slot), 0), 0) : vec4(1.0); }

// Kutuya giriş uzaklığı (ıskalarsa 1e30)
float boxEntry(vec3 o, vec3 inv, vec3 mn, vec3 mx, float tMax)
{
    vec3 t0 = (mn - o) * inv, t1 = (mx - o) * inv;
    vec3 a = min(t0, t1), b = max(t0, t1);
    float lo = max(max(a.x, a.y), max(a.z, 0.0)), hi = min(min(b.x, b.y), min(b.z, tMax));
    return lo <= hi ? lo : 1e30;
}

uint hashU(uint x) { x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16; return x; }

// Kapsama (Cutout yaprak, saydam cam): ışın üçgenin bu noktasında kapsama olasılığıyla durur (konuma bağlı, kare kare aynı)
bool coverageHit(int tri, vec3 pObj, float coverage)
{
    ivec3 q = ivec3(floor(pObj * 24.0));
    uint h = hashU(uint(tri) * 0x9E3779B9u ^ hashU(uint(q.x) * 73856093u ^ uint(q.y) * 19349663u ^ uint(q.z) * 83492791u));
    return float(h & 0xFFFFu) / 65535.0 < coverage;
}

vec3 safeInv(vec3 d) { return vec3(1.0) / vec3(abs(d.x) > 1e-12 ? d.x : 1e-12, abs(d.y) > 1e-12 ? d.y : 1e-12, abs(d.z) > 1e-12 ? d.z : 1e-12); }

// Dolaşma yığını LDS'de (paylaşılan bellek): iş parçacığı başına RT_STACK girdi; özel dizi yavaş belleğe (scratch) taşar.
// TLAS ve BLAS aynı dilimi paylaşır (BLAS, TLAS'ın o anki derinliğinin üstünden başlar). Yerel boyut en çok 64 iş parçacığı
#define RT_STACK 40
shared int s_RTStack[64 * RT_STACK];

// Bir örneğin BLAS'ı (nesne uzayı; d birim değil, t dünya ile aynı): en yakın isabet tHit'i kısaltır. base: yığında bu dolaşmanın
// başladığı yer (dilim içi mutlak)
int traceBlas(int root, vec3 o, vec3 d, float coverage, inout float tHit, inout vec2 bary, bool any, int base, int limit)
{
    vec3 inv = safeInv(d);
    int best = -1;
    if (boxEntry(o, inv, nodes[root].Min.xyz, nodes[root].Max.xyz, tHit) >= 1e30) return -1;
    int sp = base;
    s_RTStack[sp++] = root;
    while (sp > base)
    {
        Node n = nodes[s_RTStack[--sp]];
        int count = floatBitsToInt(n.Max.w);
        int first = floatBitsToInt(n.Min.w);
        if (count > 0)
        {
            for (int i = first; i < first + count; ++i)
            {
                vec3 p0 = tris[i].V0.xyz;
                vec3 e1 = tris[i].V1.xyz - p0, e2 = tris[i].V2.xyz - p0;
                vec3 p = cross(d, e2);
                float det = dot(e1, p);
                if (abs(det) < 1e-14) continue;
                float invDet = 1.0 / det;
                vec3 s = o - p0;
                float u = dot(s, p) * invDet;
                if (u < 0.0 || u > 1.0) continue;
                vec3 q = cross(s, e1);
                float v = dot(d, q) * invDet;
                if (v < 0.0 || u + v > 1.0) continue;
                float t = dot(e2, q) * invDet;
                if (t > 1e-4 && t < tHit)
                {
                    if (coverage < 0.999 && !coverageHit(i, o + d * t, coverage)) continue;
                    tHit = t; bary = vec2(u, v); best = i;
                    if (any) return best;
                }
            }
        }
        else
        {
            // Çocuk kutuları burada denenir; yakın olan önce (yığının üstünde)
            Node l = nodes[first], r = nodes[first + 1];
            float tl = boxEntry(o, inv, l.Min.xyz, l.Max.xyz, tHit);
            float tr = boxEntry(o, inv, r.Min.xyz, r.Max.xyz, tHit);
            bool hl = tl < 1e30, hr = tr < 1e30;
            if (hl && hr)
            {
                if (sp + 2 > limit) continue;
                s_RTStack[sp++] = tl <= tr ? first + 1 : first;
                s_RTStack[sp++] = tl <= tr ? first : first + 1;
            }
            else if ((hl || hr) && sp < limit)
                s_RTStack[sp++] = hl ? first : first + 1;
        }
    }
    return best;
}

// En yakın (any: herhangi bir) kesişim; üçgen dizini (-1: yok), t ve barisentrik; örnek g_HitInst'te
int trace(vec3 o, vec3 d, float tMax, out float tHit, out vec2 bary, bool any)
{
    tHit = tMax;
    bary = vec2(0.0);
    g_HitInst = -1;
    if (u_NodeCount <= 0) return -1;
    vec3 inv = safeInv(d);
    int slice = int(gl_LocalInvocationIndex) * RT_STACK;
    int limit = slice + RT_STACK;
    int best = -1;
    if (boxEntry(o, inv, tnodes[0].Min.xyz, tnodes[0].Max.xyz, tHit) >= 1e30) return -1;
    int sp = slice;
    s_RTStack[sp++] = 0;
    while (sp > slice)
    {
        Node n = tnodes[s_RTStack[--sp]];
        int count = floatBitsToInt(n.Max.w);
        int first = floatBitsToInt(n.Min.w);
        if (count > 0)
        {
            for (int k = first; k < first + count; ++k)
            {
                vec4 I0 = insts[k].I0, I1 = insts[k].I1, I2 = insts[k].I2;
                ivec4 info = insts[k].Info;
                vec3 lo = vec3(dot(I0.xyz, o) + I0.w, dot(I1.xyz, o) + I1.w, dot(I2.xyz, o) + I2.w);
                vec3 ld = vec3(dot(I0.xyz, d), dot(I1.xyz, d), dot(I2.xyz, d));
                float coverage = 1.0;
                vec4 mp = mats[info.y].Params;
                if ((int(mp.z + 0.5) & 2) != 0)
                    coverage = clamp(mats[info.y].Albedo.a * texAvg(mp.w).a, 0.0, 1.0);
                int b = traceBlas(info.x, lo, ld, coverage, tHit, bary, any, sp, limit);
                if (b >= 0)
                {
                    best = b;
                    g_HitInst = k;
                    if (any) return best;
                }
            }
        }
        else
        {
            Node l = tnodes[first], r = tnodes[first + 1];
            float tl = boxEntry(o, inv, l.Min.xyz, l.Max.xyz, tHit);
            float tr = boxEntry(o, inv, r.Min.xyz, r.Max.xyz, tHit);
            bool hl = tl < 1e30, hr = tr < 1e30;
            if (hl && hr)
            {
                if (sp + 2 > limit) continue;
                s_RTStack[sp++] = tl <= tr ? first + 1 : first;
                s_RTStack[sp++] = tl <= tr ? first : first + 1;
            }
            else if ((hl || hr) && sp < limit)
                s_RTStack[sp++] = hl ? first : first + 1;
        }
    }
    return best;
}

bool occluded(vec3 o, vec3 d, float dist) { float t; vec2 b; return trace(o, d, dist, t, b, true) >= 0; }

// İsabet: dünya konumu, gölgelendirme ve geometri normali (ışına bakar), arka yüze mi çarptı, malzeme (renk / ışıma doku ortalamalı)
struct Hit { vec3 P; vec3 N; vec3 Ng; bool Back; bool TwoSided; Mat M; };
Hit hitInfo(int ti, vec3 o, vec3 d, float t, vec2 bc)
{
    Hit h;
    Tri tr = tris[ti];
    int k = max(g_HitInst, 0);
    vec4 I0 = insts[k].I0, I1 = insts[k].I1, I2 = insts[k].I2;
    vec3 n = tr.N0.xyz * (1.0 - bc.x - bc.y) + tr.N1.xyz * bc.x + tr.N2.xyz * bc.y;
    vec3 ng = cross(tr.V1.xyz - tr.V0.xyz, tr.V2.xyz - tr.V0.xyz);
    // Normal: ters dünya matrisinin devriği (aynalanmış nesnede de dış yüz korunur)
    h.N = n.x * I0.xyz + n.y * I1.xyz + n.z * I2.xyz;
    h.N = dot(h.N, h.N) > 1e-20 ? normalize(h.N) : vec3(0.0, 1.0, 0.0);
    h.Ng = ng.x * I0.xyz + ng.y * I1.xyz + ng.z * I2.xyz;
    h.Ng = dot(h.Ng, h.Ng) > 1e-30 ? normalize(h.Ng) : h.N;
    Mat m = mats[insts[k].Info.y];
    h.TwoSided = (int(m.Params.z + 0.5) & 1) != 0;
    h.Back = dot(h.Ng, d) > 0.0;
    if (h.Back) h.Ng = -h.Ng;
    if (dot(h.N, h.Ng) < 0.0) h.N = -h.N;
    m.Albedo.rgb *= texAvg(m.Params.w).rgb;
    m.Emission.rgb *= texAvg(m.Emission.w).rgb;
    h.M = m;
    h.P = o + d * t;
    return h;
}

vec3 rotSky(vec3 d) { return vec3(u_SkyRot.x * d.x - u_SkyRot.y * d.z, d.y, u_SkyRot.y * d.x + u_SkyRot.x * d.z); }

vec3 ambient(vec3 d)
{
    if (u_SkyFlags.z == 0 && u_SkyFlags.y == 1)
    {
        if (u_SunExclude.w > 0.0 && dot(d, u_SunExclude.xyz) > u_SunExclude.w)
        {
            // Disk içindeki yön, diskin hemen dışındaki gökyüzüne kaydırılır
            vec3 s = u_SunExclude.xyz;
            vec3 perp = d - s * dot(d, s);
            perp = dot(perp, perp) > 1e-10 ? normalize(perp) : normalize(cross(s, abs(s.y) < 0.9 ? vec3(0, 1, 0) : vec3(1, 0, 0)));
            float c = u_SunExclude.w * u_SunExclude.w * 2.0 - 1.0;   // iki katı açı
            d = s * c + perp * sqrt(max(1.0 - c * c, 0.0));
        }
        return textureLod(u_LightCube, rotSky(d), 0.0).rgb * u_AmbientSky.a;
    }
    if (u_SkyFlags.z == 0)
        return u_AmbientSky.rgb * u_AmbientSky.a;
    if (u_SkyFlags.z == 1)
        return u_AmbientSky.rgb * u_AmbientSky.a;
    float up = d.y;
    vec3 c = up >= 0.0 ? mix(u_AmbientEquator, u_AmbientSky.rgb, smoothstep(0.0, 1.0, up)) : mix(u_AmbientEquator, u_AmbientGround, smoothstep(0.0, 1.0, -up));
    return c * u_AmbientSky.a;
}

vec3 background(vec3 d)
{
    if (u_SkyFlags.w == 1 && u_SkyFlags.x == 1)
        return textureLod(u_BgCube, rotSky(d), 0.0).rgb * u_BgTintExposure.rgb * u_BgTintExposure.a;
    return u_BgColor;
}

float D_GGX(float NdotH, float a) { float a2 = a * a; float d = NdotH * NdotH * (a2 - 1.0) + 1.0; return a2 / (PI * d * d + 1e-7); }
float G1(float NdotX, float a) { float k = a * 0.5; return NdotX / (NdotX * (1.0 - k) + k); }
vec3 F_Schlick(float c, vec3 f0) { return f0 + (1.0 - f0) * pow(1.0 - c, 5.0); }

vec3 brdf(vec3 n, vec3 v, vec3 l, Mat m)
{
    float NdotL = dot(n, l), NdotV = max(dot(n, v), 1e-4);
    if (NdotL <= 0.0) return vec3(0.0);
    vec3 h = normalize(v + l);
    float a = max(m.Params.y * m.Params.y, 0.002);
    vec3 f0 = mix(vec3(0.04), m.Albedo.rgb, m.Params.x);
    vec3 F = F_Schlick(max(dot(h, v), 0.0), f0);
    vec3 spec = D_GGX(max(dot(n, h), 0.0), a) * G1(NdotV, a) * G1(NdotL, a) * F / (4.0 * NdotV * NdotL + 1e-6);
    vec3 diff = (1.0 - F) * (1.0 - m.Params.x) * m.Albedo.rgb / PI;
    return diff + spec;
}

float rangeWindow(float dist, float range) { float r = dist / max(range, 1e-3); float r4 = r * r * r * r; float w = clamp(1.0 - r4, 0.0, 1.0); return w * w; }
float attenuation(float dist, float range, vec3 clq) { return rangeWindow(dist, range) / max(clq.x + clq.y * dist + clq.z * dist * dist, 1e-4); }

// Doğrudan ışık: güneş, nokta, spot, alan ışıkları (gölge ışınlı). diffuseOnly: yalnız yayınık terim (Ray Traced GI problarının
// gördüğü ışık; parlama noktaları probu titretmesin)
vec3 directLightEx(vec3 p, vec3 po, vec3 n, vec3 v, Mat m, bool diffuseOnly)
{
    vec3 sum = vec3(0.0);
    vec3 lambert = (1.0 - m.Params.x) * m.Albedo.rgb / PI;
    if (u_SunDir.w > 0.5)
    {
        vec3 l = -normalize(u_SunDir.xyz);
        float c = dot(n, l);
        if (c > 0.0 && !occluded(po, l, 1e30))
            sum += (diffuseOnly ? lambert : brdf(n, v, l, m)) * u_SunColor * c;
    }
    for (int i = 0; i < u_LightCounts.x; ++i)
    {
        vec3 tl = points[i].PositionRange.xyz - p;
        float dist = length(tl);
        if (dist >= points[i].PositionRange.w || dist < 1e-4) continue;
        vec3 l = tl / dist;
        float c = dot(n, l);
        if (c <= 0.0 || occluded(po, l, dist - 2e-3)) continue;
        sum += (diffuseOnly ? lambert : brdf(n, v, l, m)) * points[i].ColorIntensity.rgb * attenuation(dist, points[i].PositionRange.w, points[i].Attenuation.xyz) * c;
    }
    for (int i = 0; i < u_LightCounts.y; ++i)
    {
        vec3 tl = spots[i].PositionRange.xyz - p;
        float dist = length(tl);
        if (dist >= spots[i].PositionRange.w || dist < 1e-4) continue;
        vec3 l = tl / dist;
        float cone = smoothstep(spots[i].DirectionCosOuter.w, spots[i].AttenuationCosInner.w, dot(-l, spots[i].DirectionCosOuter.xyz));
        float c = dot(n, l);
        if (cone <= 0.0 || c <= 0.0 || occluded(po, l, dist - 2e-3)) continue;
        sum += (diffuseOnly ? lambert : brdf(n, v, l, m)) * spots[i].ColorIntensity.rgb * attenuation(dist, spots[i].PositionRange.w, spots[i].AttenuationCosInner.xyz) * cone * c;
    }
    for (int i = 0; i < u_LightCounts.z; ++i)
    {
        // Alan ışığı yüzeyinden bir nokta örneklenir (dikdörtgen ya da disk), ışık +Z = sağ x yukarı yönüne yayar
        vec3 R = areas[i].Right.xyz, U = areas[i].Up.xyz;
        bool disc = areas[i].Right.w > 0.5;
        float u1 = rnd(), u2 = rnd();
        vec3 q;
        float areaSize;
        if (disc) { float r = sqrt(u1), ph = 2.0 * PI * u2; q = areas[i].PositionRange.xyz + R * (r * cos(ph)) + U * (r * sin(ph)); areaSize = PI * length(R) * length(U); }
        else { q = areas[i].PositionRange.xyz + R * (u1 * 2.0 - 1.0) + U * (u2 * 2.0 - 1.0); areaSize = 4.0 * length(R) * length(U); }
        vec3 tl = q - p;
        float dist = length(tl);
        if (dist < 1e-4 || dist >= areas[i].PositionRange.w) continue;
        vec3 l = tl / dist;
        vec3 ln = normalize(cross(R, U));
        float cl = dot(-l, ln);
        if (areas[i].ColorIntensity.w > 0.5) cl = abs(cl);
        float c = dot(n, l);
        if (cl <= 0.0 || c <= 0.0 || occluded(po, l, dist - 2e-3)) continue;
        sum += (diffuseOnly ? lambert : brdf(n, v, l, m)) * areas[i].ColorIntensity.rgb * rangeWindow(dist, areas[i].PositionRange.w) * c * cl * areaSize / (dist * dist);
    }
    return sum;
}

vec3 directLight(vec3 p, vec3 po, vec3 n, vec3 v, Mat m) { return directLightEx(p, po, n, v, m, false); }

float lumOf(vec3 c) { return dot(c, vec3(0.2126, 0.7152, 0.0722)); }

// Yayınık doğrudan ışık, ışık başına gölge ışını yerine: güneş + yerel ışıklardan (nokta, spot, alan) gölgesiz katkı tahminiyle
// orantılı seçilen BİR ışık (olasılığına bölünür). Çok ışıklı sahnede maliyet sabit; gürültüsü zamanla harmanlanır (Ray Traced GI)
vec3 directLightStochastic(vec3 p, vec3 po, vec3 n, Mat m)
{
    vec3 lambert = (1.0 - m.Params.x) * m.Albedo.rgb / PI;
    vec3 sum = vec3(0.0);
    if (u_SunDir.w > 0.5)
    {
        vec3 l = -normalize(u_SunDir.xyz);
        float c = dot(n, l);
        if (c > 0.0 && !occluded(po, l, 1e30))
            sum += lambert * u_SunColor * c;
    }
    int total = u_LightCounts.x + u_LightCounts.y + u_LightCounts.z;
    if (total == 0)
        return sum;
    // 1) Gölgesiz katkı tahmini (ışık başına), toplam
    float weightSum = 0.0;
    for (int i = 0; i < total; ++i)
    {
        float w = 0.0;
        if (i < u_LightCounts.x)
        {
            vec3 tl = points[i].PositionRange.xyz - p;
            float dist = length(tl);
            if (dist < points[i].PositionRange.w && dist > 1e-4)
                w = lumOf(points[i].ColorIntensity.rgb) * attenuation(dist, points[i].PositionRange.w, points[i].Attenuation.xyz) * max(dot(n, tl / dist), 0.0);
        }
        else if (i < u_LightCounts.x + u_LightCounts.y)
        {
            int k = i - u_LightCounts.x;
            vec3 tl = spots[k].PositionRange.xyz - p;
            float dist = length(tl);
            if (dist < spots[k].PositionRange.w && dist > 1e-4)
            {
                vec3 l = tl / dist;
                float cone = smoothstep(spots[k].DirectionCosOuter.w, spots[k].AttenuationCosInner.w, dot(-l, spots[k].DirectionCosOuter.xyz));
                w = lumOf(spots[k].ColorIntensity.rgb) * attenuation(dist, spots[k].PositionRange.w, spots[k].AttenuationCosInner.xyz) * cone * max(dot(n, l), 0.0);
            }
        }
        else
        {
            int k = i - u_LightCounts.x - u_LightCounts.y;
            vec3 tl = areas[k].PositionRange.xyz - p;
            float dist = max(length(tl), 1e-3);
            if (dist < areas[k].PositionRange.w)
            {
                float areaSize = 4.0 * length(areas[k].Right.xyz) * length(areas[k].Up.xyz);
                w = lumOf(areas[k].ColorIntensity.rgb) * rangeWindow(dist, areas[k].PositionRange.w) * areaSize / (dist * dist + areaSize) * max(dot(n, tl / dist), 0.05);
            }
        }
        weightSum += w;
    }
    if (weightSum <= 1e-8)
        return sum;
    // 2) Bir ışık seç (katkıyla orantılı) ve gölge ışınıyla tam hesapla
    float pick = rnd() * weightSum;
    for (int i = 0; i < total; ++i)
    {
        float w = 0.0;
        vec3 contrib = vec3(0.0);
        bool visibleTest = false;
        vec3 l = vec3(0.0);
        float dist = 0.0;
        if (i < u_LightCounts.x)
        {
            vec3 tl = points[i].PositionRange.xyz - p;
            dist = length(tl);
            if (dist < points[i].PositionRange.w && dist > 1e-4)
            {
                l = tl / dist;
                float c = max(dot(n, l), 0.0);
                float a = attenuation(dist, points[i].PositionRange.w, points[i].Attenuation.xyz);
                w = lumOf(points[i].ColorIntensity.rgb) * a * c;
                contrib = lambert * points[i].ColorIntensity.rgb * a * c;
            }
        }
        else if (i < u_LightCounts.x + u_LightCounts.y)
        {
            int k = i - u_LightCounts.x;
            vec3 tl = spots[k].PositionRange.xyz - p;
            dist = length(tl);
            if (dist < spots[k].PositionRange.w && dist > 1e-4)
            {
                l = tl / dist;
                float cone = smoothstep(spots[k].DirectionCosOuter.w, spots[k].AttenuationCosInner.w, dot(-l, spots[k].DirectionCosOuter.xyz));
                float c = max(dot(n, l), 0.0);
                float a = attenuation(dist, spots[k].PositionRange.w, spots[k].AttenuationCosInner.xyz) * cone;
                w = lumOf(spots[k].ColorIntensity.rgb) * a * c;
                contrib = lambert * spots[k].ColorIntensity.rgb * a * c;
            }
        }
        else
        {
            int k = i - u_LightCounts.x - u_LightCounts.y;
            vec3 tc = areas[k].PositionRange.xyz - p;
            float dc = max(length(tc), 1e-3);
            if (dc < areas[k].PositionRange.w)
            {
                float areaEst = 4.0 * length(areas[k].Right.xyz) * length(areas[k].Up.xyz);
                w = lumOf(areas[k].ColorIntensity.rgb) * rangeWindow(dc, areas[k].PositionRange.w) * areaEst / (dc * dc + areaEst) * max(dot(n, tc / dc), 0.05);
                // Yüzeyden bir nokta (dikdörtgen ya da disk)
                vec3 R = areas[k].Right.xyz, U = areas[k].Up.xyz;
                bool disc = areas[k].Right.w > 0.5;
                float u1 = rnd(), u2 = rnd();
                vec3 q;
                float areaSize;
                if (disc) { float r = sqrt(u1), ph = 2.0 * PI * u2; q = areas[k].PositionRange.xyz + R * (r * cos(ph)) + U * (r * sin(ph)); areaSize = PI * length(R) * length(U); }
                else { q = areas[k].PositionRange.xyz + R * (u1 * 2.0 - 1.0) + U * (u2 * 2.0 - 1.0); areaSize = 4.0 * length(R) * length(U); }
                vec3 tl = q - p;
                dist = length(tl);
                if (dist > 1e-4 && dist < areas[k].PositionRange.w)
                {
                    l = tl / dist;
                    vec3 ln = normalize(cross(R, U));
                    float cl = dot(-l, ln);
                    if (areas[k].ColorIntensity.w > 0.5) cl = abs(cl);
                    float c = dot(n, l);
                    if (cl > 0.0 && c > 0.0)
                        contrib = lambert * areas[k].ColorIntensity.rgb * rangeWindow(dist, areas[k].PositionRange.w) * c * cl * areaSize / (dist * dist);
                }
            }
        }
        pick -= w;
        if (pick > 0.0 && i < total - 1)
            continue;
        // Seçilen ışık: görünürse katkı / olasılık
        if (w > 0.0 && dot(contrib, contrib) > 0.0 && !occluded(po, l, dist - 2e-3))
            sum += contrib * (weightSum / w);
        break;
    }
    return sum;
}

)";

        const char* kTexAverageGLSL = R"(#version 450 core
layout(local_size_x = 64) in;
uniform sampler2D u_Tex;
uniform float u_Lod;
uniform int u_Slot;
layout(rgba16f, binding = 0) uniform writeonly image2D u_Out;
shared vec4 s_Sum[64];
void main()
{
    uint i = gl_LocalInvocationID.x;
    vec4 acc = vec4(0.0);
    for (uint k = 0u; k < 4u; ++k)
    {
        uint idx = i * 4u + k;
        vec2 uv = (vec2(float(idx % 16u), float(idx / 16u)) + 0.5) / 16.0;
        acc += textureLod(u_Tex, uv, u_Lod);
    }
    s_Sum[i] = acc;
    barrier();
    for (uint st = 32u; st > 0u; st >>= 1u)
    {
        if (i < st) s_Sum[i] += s_Sum[i + st];
        barrier();
    }
    if (i == 0u) imageStore(u_Out, ivec2(u_Slot, 0), s_Sum[0] / 256.0);
}
)";

        // Motorun malzeme renkleri sRGB (shader SRGBToLinear ile açar)
        float SRGB(float c) { c = std::max(c, 0.0f); return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); }
        glm::vec3 SRGB(const glm::vec3& c) { return { SRGB(c.x), SRGB(c.y), SRGB(c.z) }; }

        void Mix(uint64_t& h, uint64_t v) { h ^= v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2); }
        void MixF(uint64_t& h, float f) { uint32_t u; std::memcpy(&u, &f, 4); Mix(h, u); }
        void MixBytes(uint64_t& h, const void* data, size_t bytes)
        {
            const unsigned char* p = (const unsigned char*)data;
            uint64_t x = 1469598103934665603ull;
            for (size_t i = 0; i < bytes; ++i) { x ^= p[i]; x *= 1099511628211ull; }
            Mix(h, x);
        }

        float IntBits(int32_t v) { float f; std::memcpy(&f, &v, 4); return f; }

        // ------------------------------------------------------------------ BVH (binned SAH)

        struct Prim { glm::vec3 Min, Max; };

        float HalfArea(const glm::vec3& mn, const glm::vec3& mx)
        {
            const glm::vec3 e = glm::max(mx - mn, glm::vec3(0.0f));
            return e.x * e.y + e.y * e.z + e.z * e.x;
        }

        // order: öğe sırası (yapraklar order'daki aralıklara bakar); düğümler göreli (kök 0)
        void BuildBVH(const std::vector<Prim>& prims, std::vector<uint32_t>& order, std::vector<Node>& nodes, uint32_t maxLeaf)
        {
            const uint32_t n = (uint32_t)prims.size();
            order.resize(n);
            std::iota(order.begin(), order.end(), 0u);
            nodes.clear();
            if (n == 0)
                return;
            std::vector<glm::vec3> cent(n);
            for (uint32_t i = 0; i < n; ++i)
                cent[i] = (prims[i].Min + prims[i].Max) * 0.5f;
            nodes.reserve((size_t)n * 2 / std::max(1u, maxLeaf / 2) + 2);
            struct Job { uint32_t Node, Begin, End; };
            std::vector<Job> jobs{ { 0, 0, n } };
            nodes.push_back({});
            constexpr int kBins = 12;
            while (!jobs.empty())
            {
                const Job j = jobs.back();
                jobs.pop_back();
                glm::vec3 mn(FLT_MAX), mx(-FLT_MAX), cmn(FLT_MAX), cmx(-FLT_MAX);
                for (uint32_t i = j.Begin; i < j.End; ++i)
                {
                    const Prim& p = prims[order[i]];
                    mn = glm::min(mn, p.Min);
                    mx = glm::max(mx, p.Max);
                    cmn = glm::min(cmn, cent[order[i]]);
                    cmx = glm::max(cmx, cent[order[i]]);
                }
                const uint32_t count = j.End - j.Begin;
                nodes[j.Node].Min = glm::vec4(mn, IntBits((int32_t)j.Begin));
                nodes[j.Node].Max = glm::vec4(mx, IntBits((int32_t)count));
                if (count <= 1)
                    continue;
                const glm::vec3 ext = cmx - cmn;
                const int axis = ext.x > ext.y ? (ext.x > ext.z ? 0 : 2) : (ext.y > ext.z ? 1 : 2);
                uint32_t mid = j.Begin;
                if (ext[axis] < 1e-9f)
                {
                    // Merkezler çakışık: küçükse yaprak, değilse sırayla ikiye böl
                    if (count <= maxLeaf * 2)
                        continue;
                    mid = j.Begin + count / 2;
                }
                else
                {
                    uint32_t binCount[kBins] = {};
                    glm::vec3 binMin[kBins], binMax[kBins];
                    for (int b = 0; b < kBins; ++b) { binMin[b] = glm::vec3(FLT_MAX); binMax[b] = glm::vec3(-FLT_MAX); }
                    const float scale = (float)kBins / ext[axis] * 0.99999f;
                    auto binOf = [&](uint32_t k) { return std::min(kBins - 1, (int)((cent[k][axis] - cmn[axis]) * scale)); };
                    for (uint32_t i = j.Begin; i < j.End; ++i)
                    {
                        const int b = binOf(order[i]);
                        ++binCount[b];
                        binMin[b] = glm::min(binMin[b], prims[order[i]].Min);
                        binMax[b] = glm::max(binMax[b], prims[order[i]].Max);
                    }
                    float leftArea[kBins], rightArea[kBins];
                    uint32_t leftN[kBins], rightN[kBins];
                    {
                        glm::vec3 a(FLT_MAX), b(-FLT_MAX);
                        uint32_t c = 0;
                        for (int i = 0; i < kBins; ++i)
                        {
                            if (binCount[i]) { a = glm::min(a, binMin[i]); b = glm::max(b, binMax[i]); }
                            c += binCount[i];
                            leftN[i] = c;
                            leftArea[i] = c ? HalfArea(a, b) : 0.0f;
                        }
                    }
                    {
                        glm::vec3 a(FLT_MAX), b(-FLT_MAX);
                        uint32_t c = 0;
                        for (int i = kBins - 1; i >= 0; --i)
                        {
                            if (binCount[i]) { a = glm::min(a, binMin[i]); b = glm::max(b, binMax[i]); }
                            c += binCount[i];
                            rightN[i] = c;
                            rightArea[i] = c ? HalfArea(a, b) : 0.0f;
                        }
                    }
                    int bestSplit = -1;
                    float bestCost = FLT_MAX;
                    for (int s = 0; s < kBins - 1; ++s)
                    {
                        if (leftN[s] == 0 || rightN[s + 1] == 0)
                            continue;
                        const float cost = leftArea[s] * (float)leftN[s] + rightArea[s + 1] * (float)rightN[s + 1];
                        if (cost < bestCost) { bestCost = cost; bestSplit = s; }
                    }
                    // SAH: yaprak (count kesişim) mi, bölmek (1 geçiş + çocukların beklenen kesişimi) mi ucuz
                    const float parentArea = std::max(HalfArea(mn, mx), 1e-20f);
                    if (count <= maxLeaf && (bestSplit < 0 || 1.0f + bestCost / parentArea >= (float)count))
                        continue;
                    if (bestSplit >= 0)
                        mid = (uint32_t)(std::partition(order.begin() + j.Begin, order.begin() + j.End,
                                                        [&](uint32_t k) { return binOf(k) <= bestSplit; }) - order.begin());
                    if (mid == j.Begin || mid == j.End)
                    {
                        mid = j.Begin + count / 2;
                        std::nth_element(order.begin() + j.Begin, order.begin() + mid, order.begin() + j.End,
                                         [&](uint32_t a, uint32_t b) { return cent[a][axis] < cent[b][axis]; });
                    }
                }
                const uint32_t left = (uint32_t)nodes.size();
                nodes.push_back({});
                nodes.push_back({});
                nodes[j.Node].Min.w = IntBits((int32_t)left);
                nodes[j.Node].Max.w = IntBits(0);
                jobs.push_back({ left, j.Begin, mid });
                jobs.push_back({ left + 1, mid, j.End });
            }
        }

        // ------------------------------------------------------------------ BLAS

        struct BlasData
        {
            std::vector<Tri> Tris;
            std::vector<Node> Nodes;         // göreli (kök 0, yapraklar Tris dizinine)
            glm::vec3 Min{ 0.0f }, Max{ 0.0f };
        };

        struct BlasInput
        {
            std::vector<glm::vec3> Positions;
            std::vector<glm::vec3> Normals;
            std::vector<uint32_t> Indices;
        };

        std::shared_ptr<BlasData> BuildBlas(const BlasInput& in)
        {
            auto out = std::make_shared<BlasData>();
            const size_t vc = in.Positions.size();
            out->Tris.reserve(in.Indices.size() / 3);
            std::vector<Prim> prims;
            prims.reserve(in.Indices.size() / 3);
            for (size_t i = 0; i + 2 < in.Indices.size(); i += 3)
            {
                const uint32_t a = in.Indices[i], b = in.Indices[i + 1], c = in.Indices[i + 2];
                if (a >= vc || b >= vc || c >= vc)
                    continue;
                const glm::vec3 &p0 = in.Positions[a], &p1 = in.Positions[b], &p2 = in.Positions[c];
                const glm::vec3 face = glm::cross(p1 - p0, p2 - p0);
                if (glm::dot(face, face) < 1e-24f)
                    continue;   // sıfır alan: hiçbir ışın çarpmaz
                const glm::vec3 fn = glm::normalize(face);
                auto nrm = [&](uint32_t k) {
                    const glm::vec3 n = k < in.Normals.size() ? in.Normals[k] : fn;
                    return glm::dot(n, n) > 1e-12f ? glm::normalize(n) : fn;
                };
                Tri t;
                t.V0 = glm::vec4(p0, 0.0f); t.V1 = glm::vec4(p1, 0.0f); t.V2 = glm::vec4(p2, 0.0f);
                t.N0 = glm::vec4(nrm(a), 0.0f); t.N1 = glm::vec4(nrm(b), 0.0f); t.N2 = glm::vec4(nrm(c), 0.0f);
                out->Tris.push_back(t);
                prims.push_back({ glm::min(p0, glm::min(p1, p2)), glm::max(p0, glm::max(p1, p2)) });
            }
            std::vector<uint32_t> order;
            BuildBVH(prims, order, out->Nodes, 4);
            std::vector<Tri> sorted(out->Tris.size());
            for (size_t i = 0; i < order.size(); ++i)
                sorted[i] = out->Tris[order[i]];
            out->Tris.swap(sorted);
            if (!out->Nodes.empty())
            {
                out->Min = glm::vec3(out->Nodes[0].Min);
                out->Max = glm::vec3(out->Nodes[0].Max);
            }
            return out;
        }

        BlasInput InputFromMesh(const Mesh& mesh, const std::vector<glm::mat4>* transforms = nullptr, size_t maxTriangles = SIZE_MAX)
        {
            BlasInput in;
            const auto& vs = mesh.GetVertices();
            const auto& is = mesh.GetIndices();
            if (!transforms)
            {
                in.Positions.resize(vs.size());
                in.Normals.resize(vs.size());
                for (size_t i = 0; i < vs.size(); ++i) { in.Positions[i] = vs[i].Position; in.Normals[i] = vs[i].Normal; }
                in.Indices = is;
                return in;
            }
            // MultiMesh: tüm örnekler bileşenin yerel uzayında tek BLAS (sınırlı üçgen)
            const size_t tris = is.size() / 3;
            const size_t count = tris ? std::min(transforms->size(), maxTriangles / tris) : 0;
            in.Positions.reserve(vs.size() * count);
            in.Normals.reserve(vs.size() * count);
            in.Indices.reserve(is.size() * count);
            for (size_t k = 0; k < count; ++k)
            {
                const glm::mat4& m = (*transforms)[k];
                const glm::mat3 nm = glm::inverseTranspose(glm::mat3(m));
                const uint32_t base = (uint32_t)in.Positions.size();
                for (const Vertex& v : vs)
                {
                    in.Positions.push_back(glm::vec3(m * glm::vec4(v.Position, 1.0f)));
                    in.Normals.push_back(nm * v.Normal);
                }
                for (uint32_t idx : is)
                    in.Indices.push_back(base + idx);
            }
            return in;
        }

        struct BlasEntry
        {
            std::weak_ptr<Mesh> Source;      // mesh yok olunca kayıt geçersiz (aynı adreste yeni mesh olabilir)
            uint64_t Version = 0;            // kaynağın sürümü (mesh köşe güncellemesi / MultiMesh örnek verisi)
            std::shared_ptr<BlasData> Data;  // hazır (GPU'ya yüklendi)
            std::future<std::shared_ptr<BlasData>> Job;
            uint64_t JobVersion = 0;
            bool Uploaded = false;
            uint32_t NodeBase = 0, TriBase = 0;   // birleşik tamponlarda
            uint32_t NodeCount = 0, TriCount = 0;
            uint64_t LastUsed = 0;
        };

        // Birleşik GPU dizisi (BLAS'lar sona eklenir, büyüdükçe kopyalanarak genişler)
        struct GpuArray
        {
            GLuint Buf = 0;
            size_t Capacity = 0, Used = 0;
        };

        size_t Append(GpuArray& a, const void* data, size_t bytes)
        {
            if (a.Used + bytes > a.Capacity || !a.Buf)
            {
                const size_t cap = std::max({ a.Capacity * 2, a.Used + bytes, (size_t)(1u << 20) });
                GLuint nb = 0;
                glCreateBuffers(1, &nb);
                glNamedBufferData(nb, (GLsizeiptr)cap, nullptr, GL_DYNAMIC_DRAW);
                if (a.Buf && a.Used)
                    glCopyNamedBufferSubData(a.Buf, nb, 0, 0, (GLsizeiptr)a.Used);
                if (a.Buf)
                    glDeleteBuffers(1, &a.Buf);
                a.Buf = nb;
                a.Capacity = cap;
            }
            const size_t offset = a.Used;
            if (bytes)
                glNamedBufferSubData(a.Buf, (GLintptr)offset, (GLsizeiptr)bytes, data);
            a.Used += bytes;
            return offset;
        }

        void UploadSmall(GLuint& buf, const void* data, size_t bytes)
        {
            if (!buf)
                glCreateBuffers(1, &buf);
            glNamedBufferData(buf, (GLsizeiptr)std::max<size_t>(bytes, 16), nullptr, GL_DYNAMIC_DRAW);
            if (bytes)
                glNamedBufferSubData(buf, 0, (GLsizeiptr)bytes, data);
        }

        std::shared_ptr<Mesh> PrimitiveMesh(MeshType type)
        {
            switch (type)
            {
                case MeshType::Sphere: return Renderer3D::GetSphereMesh();
                case MeshType::Plane: return Renderer3D::GetPlaneMesh();
                case MeshType::Cylinder: return Renderer3D::GetCylinderMesh();
                default: return Renderer3D::GetCubeMesh();
            }
        }

        std::shared_ptr<Mesh> MultiMeshPrimitive(int primitive)
        {
            switch (primitive)
            {
                case 1: return Renderer3D::GetSphereMesh();
                case 2: return Renderer3D::GetPlaneMesh();
                case 3: return Renderer3D::GetCylinderMesh();
                default: return Renderer3D::GetCubeMesh();
            }
        }

        struct TexAvgEntry { std::weak_ptr<Texture2D> Source; uint32_t Id = 0; int Slot = -1; };

        struct State
        {
            std::unordered_map<uint64_t, BlasEntry> Blas;
            GpuArray Tris, Nodes;
            size_t WastedBytes = 0;
            GLuint TNodeBuf = 0, InstBuf = 0, MatBuf = 0, PointBuf = 0, SpotBuf = 0, AreaBuf = 0;
            uint64_t InstanceHash = 0, LightHash = 0;
            uint64_t Frame = 0;
            bool LayoutChanged = true;
            RayTracingScene::Stats Info;
            // Doku ortalamaları
            GLuint AvgTexture = 0, AvgProgram = 0, AvgSampler = 0, AvgSamplerNoMip = 0;
            bool AvgTried = false;
            std::unordered_map<const Texture2D*, TexAvgEntry> Averages;
            int NextSlot = 0;
        };
        State& S() { static auto& s = *new State(); return s; }

        ThreadPool& Pool() { static auto& p = *new ThreadPool(2); return p; }

        constexpr int kAvgSlots = 4096;
        constexpr uint32_t kSyncTriangles = 4096;        // bundan küçük BLAS ana iş parçacığında hemen dizilir
        constexpr size_t kMultiMeshMaxTriangles = 2000000;

        // Doku ortalamaları: yuva dokusu (beyaz ile dolu) ve ortalama alan compute programı
        void EnsureAverages(State& s)
        {
            if (!s.AvgTried)
            {
                s.AvgTried = true;
                GLuint sh = glCreateShader(GL_COMPUTE_SHADER);
                glShaderSource(sh, 1, &kTexAverageGLSL, nullptr);
                glCompileShader(sh);
                GLint ok = 0;
                glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
                if (ok)
                {
                    s.AvgProgram = glCreateProgram();
                    glAttachShader(s.AvgProgram, sh);
                    glLinkProgram(s.AvgProgram);
                    glGetProgramiv(s.AvgProgram, GL_LINK_STATUS, &ok);
                    if (!ok) { glDeleteProgram(s.AvgProgram); s.AvgProgram = 0; }
                }
                glDeleteShader(sh);
                if (!s.AvgProgram)
                    CQ_CORE_ERROR("[Ray Tracing] texture average shader failed");
                glCreateTextures(GL_TEXTURE_2D, 1, &s.AvgTexture);
                glTextureStorage2D(s.AvgTexture, 1, GL_RGBA16F, kAvgSlots, 1);
                const float white[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
                glClearTexImage(s.AvgTexture, 0, GL_RGBA, GL_FLOAT, white);
                glCreateSamplers(1, &s.AvgSampler);
                glSamplerParameteri(s.AvgSampler, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
                glSamplerParameteri(s.AvgSampler, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glSamplerParameteri(s.AvgSampler, GL_TEXTURE_WRAP_S, GL_REPEAT);
                glSamplerParameteri(s.AvgSampler, GL_TEXTURE_WRAP_T, GL_REPEAT);
                glCreateSamplers(1, &s.AvgSamplerNoMip);
                glSamplerParameteri(s.AvgSamplerNoMip, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glSamplerParameteri(s.AvgSamplerNoMip, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glSamplerParameteri(s.AvgSamplerNoMip, GL_TEXTURE_WRAP_S, GL_REPEAT);
                glSamplerParameteri(s.AvgSamplerNoMip, GL_TEXTURE_WRAP_T, GL_REPEAT);
            }
        }

        // Dokunun doğrusal ortalaması için yuva (-1: doku yok / yer kalmadı). Yeni dokuda GPU'da ortalanır (geri okuma yok)
        float AverageSlot(const std::shared_ptr<Texture2D>& tex)
        {
            if (!tex || !tex->GetRendererID())
                return -1.0f;
            State& s = S();
            auto it = s.Averages.find(tex.get());
            if (it != s.Averages.end() && !it->second.Source.expired() && it->second.Id == tex->GetRendererID())
                return (float)it->second.Slot;
            EnsureAverages(s);
            if (!s.AvgProgram)
                return -1.0f;
            int slot = -1;
            if (it != s.Averages.end())
                slot = it->second.Slot;       // aynı adres, yeni doku: yuva yeniden kullanılır
            else if (s.NextSlot < kAvgSlots)
                slot = s.NextSlot++;
            else
            {
                // Yer kalmadı: ölü dokuların yuvaları geri alınır
                for (auto e = s.Averages.begin(); e != s.Averages.end();)
                {
                    if (e->second.Source.expired()) { slot = e->second.Slot; e = s.Averages.erase(e); break; }
                    ++e;
                }
                if (slot < 0)
                    return -1.0f;
            }
            const GLuint id = tex->GetRendererID();
            // Mip zinciri varsa ~16 piksellik seviye, yoksa taban seviyeden 256 örnek
            GLint w = 0, h = 0, w1 = 0;
            glGetTextureLevelParameteriv(id, 0, GL_TEXTURE_WIDTH, &w);
            glGetTextureLevelParameteriv(id, 0, GL_TEXTURE_HEIGHT, &h);
            glGetTextureLevelParameteriv(id, 1, GL_TEXTURE_WIDTH, &w1);
            const bool mips = w1 > 0;
            const float lod = mips ? std::max(0.0f, std::log2((float)std::max(std::max(w, h), 1)) - 4.0f) : 0.0f;
            glUseProgram(s.AvgProgram);
            glUniform1i(glGetUniformLocation(s.AvgProgram, "u_Tex"), 0);
            glUniform1f(glGetUniformLocation(s.AvgProgram, "u_Lod"), lod);
            glUniform1i(glGetUniformLocation(s.AvgProgram, "u_Slot"), slot);
            GLint prevUnit = 0;
            glGetIntegerv(GL_ACTIVE_TEXTURE, &prevUnit);
            glBindTextureUnit(0, id);
            glBindSampler(0, mips ? s.AvgSampler : s.AvgSamplerNoMip);
            glBindImageTexture(0, s.AvgTexture, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
            glDispatchCompute(1, 1, 1);
            glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
            glBindSampler(0, 0);
            glBindImageTexture(0, 0, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
            glActiveTexture((GLenum)prevUnit);
            TexAvgEntry& e = s.Averages[tex.get()];
            e.Source = tex;
            e.Id = id;
            e.Slot = slot;
            return (float)slot;
        }

        Mat MakeMat(const Material& m)
        {
            Mat out;
            out.Albedo = glm::vec4(SRGB(m.Albedo), 1.0f);
            out.Emission = glm::vec4(glm::max(m.EmissionColor, glm::vec3(0.0f)) * std::max(m.EmissionStrength, 0.0f), AverageSlot(m.EmissionMap));
            int flags = m.Culling == Material::CullMode::Off ? 1 : 0;
            if (m.Rendering == Material::RenderMode::Cutout)
            {
                flags |= 2;
                out.Albedo.a = 1.0f;
            }
            else if (m.Rendering == Material::RenderMode::Transparent)
            {
                flags |= 2 | 1;
                out.Albedo.a = std::clamp(m.Opacity, 0.0f, 1.0f);
            }
            if (m.Shading == Material::ShadingModel::Unlit)
            {
                // Işıksız: rengi kendi ışıması (ekran, LED duvar); yansıttığı ışık yok
                out.Emission = glm::vec4(glm::vec3(out.Albedo) + glm::vec3(out.Emission), AverageSlot(m.AlbedoMap));
                out.Albedo = glm::vec4(0.0f, 0.0f, 0.0f, out.Albedo.a);
            }
            out.Params = glm::vec4(std::clamp(m.Metallic, 0.0f, 1.0f), std::clamp(m.Roughness, 0.02f, 1.0f), (float)flags, AverageSlot(m.AlbedoMap));
            return out;
        }

        void MixMat(uint64_t& h, const Mat& m)
        {
            MixBytes(h, &m, sizeof(Mat));
        }


        // Hazır BLAS'ı birleşik tamponlara ekler (düğümlerin çocuk / üçgen dizinleri mutlak olur)
        void UploadBlas(State& s, BlasEntry& e)
        {
            const BlasData& d = *e.Data;
            if (e.Uploaded)
                s.WastedBytes += (size_t)e.TriCount * sizeof(Tri) + (size_t)e.NodeCount * sizeof(Node);
            const uint32_t triBase = (uint32_t)(s.Tris.Used / sizeof(Tri));
            const uint32_t nodeBase = (uint32_t)(s.Nodes.Used / sizeof(Node));
            std::vector<Node> nodes = d.Nodes;
            for (Node& n : nodes)
            {
                int32_t first, count;
                std::memcpy(&first, &n.Min.w, 4);
                std::memcpy(&count, &n.Max.w, 4);
                n.Min.w = IntBits(count > 0 ? first + (int32_t)triBase : first + (int32_t)nodeBase);
            }
            Append(s.Tris, d.Tris.data(), d.Tris.size() * sizeof(Tri));
            Append(s.Nodes, nodes.data(), nodes.size() * sizeof(Node));
            e.TriBase = triBase;
            e.NodeBase = nodeBase;
            e.TriCount = (uint32_t)d.Tris.size();
            e.NodeCount = (uint32_t)nodes.size();
            e.Uploaded = true;
            s.LayoutChanged = true;
        }

        // Ölü / kullanılmayan BLAS'lar silinir; boşa giden yer çoksa birleşik tamponlar baştan kurulur
        void CollectGarbage(State& s)
        {
            for (auto it = s.Blas.begin(); it != s.Blas.end();)
            {
                BlasEntry& e = it->second;
                const bool busy = e.Job.valid() && e.Job.wait_for(std::chrono::seconds(0)) != std::future_status::ready;
                if (!busy && s.Frame - e.LastUsed > 600)
                {
                    if (e.Uploaded)
                        s.WastedBytes += (size_t)e.TriCount * sizeof(Tri) + (size_t)e.NodeCount * sizeof(Node);
                    it = s.Blas.erase(it);
                    s.LayoutChanged = true;
                    continue;
                }
                ++it;
            }
            const size_t used = s.Tris.Used + s.Nodes.Used;
            if (s.WastedBytes > (16u << 20) && s.WastedBytes * 2 > used)
            {
                s.Tris.Used = 0;
                s.Nodes.Used = 0;
                s.WastedBytes = 0;
                for (auto& [key, e] : s.Blas)
                {
                    e.Uploaded = false;
                    if (e.Data)
                        UploadBlas(s, e);
                }
                s.LayoutChanged = true;
            }
        }
    }

    namespace RayTracingScene
    {
        const char* CommonGLSL() { return kCommonGLSL; }
        const Stats& GetStats() { return S().Info; }

        uint32_t CompileCompute(const std::string& name, const std::string& localSize, const std::string& body)
        {
            const std::string source = "#version 450 core\n" + localSize + "\n" + kCommonGLSL + body;
            const char* text = source.c_str();
            GLuint sh = glCreateShader(GL_COMPUTE_SHADER);
            glShaderSource(sh, 1, &text, nullptr);
            glCompileShader(sh);
            GLint ok = 0;
            glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
            if (!ok)
            {
                char log[4096] = {};
                glGetShaderInfoLog(sh, sizeof(log), nullptr, log);
                CQ_CORE_ERROR("[{0}] shader compile failed: {1}", name, log);
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
                char log[4096] = {};
                glGetProgramInfoLog(prog, sizeof(log), nullptr, log);
                CQ_CORE_ERROR("[{0}] shader link failed: {1}", name, log);
                glDeleteProgram(prog);
                return 0;
            }
            return prog;
        }

        const Stats& Update(Scene& scene, bool wait)
        {
            State& s = S();
            ++s.Frame;
            auto& reg = scene.m_Registry;

            // Bu karenin örnekleri: (BLAS anahtarı, dünya matrisi, malzeme)
            struct Desc { uint64_t Key; glm::mat4 World; int Mat; };
            std::vector<Desc> descs;
            std::vector<Mat> mats;
            uint64_t h = 1469598103934665603ull;
            Mix(h, (uint64_t)(uintptr_t)&scene);

            // BLAS'ı ister: yoksa / kaynağı değiştiyse dizer (küçükse hemen, büyükse arka planda)
            auto request = [&](uint64_t key, const std::shared_ptr<Mesh>& mesh, uint64_t version, auto&& makeInput) -> bool {
                BlasEntry& e = s.Blas[key];
                e.LastUsed = s.Frame;
                const bool stale = !e.Data || e.Version != version || (mesh && e.Source.lock() != mesh);
                if (stale && !e.Job.valid())
                {
                    BlasInput input = makeInput();
                    const size_t triangles = input.Indices.size() / 3;
                    e.Source = mesh;
                    e.JobVersion = version;
                    if (triangles <= kSyncTriangles || wait)
                    {
                        std::promise<std::shared_ptr<BlasData>> p;
                        p.set_value(BuildBlas(input));
                        e.Job = p.get_future();
                    }
                    else
                        e.Job = Pool().Submit([in = std::move(input)]() { return BuildBlas(in); });
                }
                if (e.Job.valid() && (wait || e.Job.wait_for(std::chrono::seconds(0)) == std::future_status::ready))
                {
                    e.Data = e.Job.get();
                    e.Version = e.JobVersion;
                    e.Source = mesh;
                    UploadBlas(s, e);
                }
                return e.Data && e.Uploaded && !e.Data->Nodes.empty();
            };

            auto meshKey = [](const Mesh* m) { return (uint64_t)(uintptr_t)m; };
            auto addInstance = [&](uint64_t key, const glm::mat4& world, const Mat& m) {
                Mix(h, key);
                for (int i = 0; i < 4; ++i) for (int k = 0; k < 4; ++k) MixF(h, world[i][k]);
                MixMat(h, m);
                descs.push_back({ key, world, (int)mats.size() });
                mats.push_back(m);
            };
            auto addMesh = [&](const std::shared_ptr<Mesh>& mesh, const glm::mat4& world, const Mat& m) {
                if (!mesh || mesh->GetIndices().size() < 3)
                    return;
                const uint64_t key = meshKey(mesh.get());
                const uint64_t version = mesh->GetVersion();
                Mix(h, version);
                if (request(key, mesh, version, [&]() { return InputFromMesh(*mesh); }))
                    addInstance(key, world, m);
                else
                    Mix(h, 0xB1A5ull);   // bekleyen yapım: hazır olunca özet değişir
            };

            // Mesh Renderer: ilkel, özel mesh, model (iskeletli modeller hariç: bağlanma pozu hareketli karakterle uyuşmaz)
            for (auto e : reg.view<TransformComponent, MeshRendererComponent>())
            {
                if (!scene.IsEntityActive(e)) continue;
                const auto& mr = reg.get<MeshRendererComponent>(e);
                const glm::mat4& w = reg.get<TransformComponent>(e).WorldTransform;
                if (mr.IsModel())
                {
                    if (!mr.ModelData || mr.ModelData->IsSkinned) continue;
                    for (size_t mi = 0; mi < mr.ModelData->Meshes.size(); ++mi)
                    {
                        static const Material kDefault;
                        const Material& m = mi < mr.ModelData->Materials.size() && mr.ModelData->Materials[mi] ? *mr.ModelData->Materials[mi] : kDefault;
                        addMesh(mr.ModelData->Meshes[mi], w, MakeMat(m));
                    }
                }
                else
                    addMesh(mr.CustomMesh ? mr.CustomMesh : PrimitiveMesh(mr.Type), w, MakeMat(MeshRendererMaterial::Shared(mr)));
            }

            // Arazi: katmanların splat ağırlıklı ortalama rengi
            for (auto e : reg.view<TransformComponent, TerrainComponent>())
            {
                if (!scene.IsEntityActive(e)) continue;
                const auto& t = reg.get<TerrainComponent>(e);
                if (!t.RuntimeMesh) continue;
                glm::vec4 weights(0.0f);
                if (!t.Splat.empty())
                {
                    const size_t px = t.Splat.size() / 4;
                    const size_t step = std::max<size_t>(1, px / 4096);
                    for (size_t i = 0; i < px; i += step)
                        weights += glm::vec4(t.Splat[i * 4], t.Splat[i * 4 + 1], t.Splat[i * 4 + 2], t.Splat[i * 4 + 3]);
                }
                if (weights.x + weights.y + weights.z + weights.w <= 0.0f)
                    weights = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
                glm::vec3 albedo(0.0f);
                float rough = 0.0f, metal = 0.0f, total = 0.0f;
                int avgSlot = -1;
                for (int l = 0; l < std::min(t.LayerCount, TerrainComponent::MaxLayers); ++l)
                {
                    const float wl = weights[l];
                    if (wl <= 0.0f) continue;
                    // Katman dokusu: ortalaması GPU'da; arazide tek yuva kullanılır (en ağır katman), diğerleri renk ayarıyla
                    glm::vec3 c = SRGB(t.Layers[l].Tint);
                    albedo += c * wl;
                    rough += t.Layers[l].Roughness * wl;
                    metal += t.Layers[l].Metallic * wl;
                    total += wl;
                }
                {
                    int heaviest = 0;
                    for (int l = 1; l < std::min(t.LayerCount, TerrainComponent::MaxLayers); ++l)
                        if (weights[l] > weights[heaviest]) heaviest = l;
                    avgSlot = (int)AverageSlot(t.Layers[heaviest].Albedo);
                }
                Mat m;
                m.Albedo = glm::vec4(total > 0.0f ? albedo / total : glm::vec3(0.5f), 1.0f);
                m.Emission = glm::vec4(0.0f, 0.0f, 0.0f, -1.0f);
                m.Params = glm::vec4(total > 0.0f ? metal / total : 0.0f, total > 0.0f ? rough / total : 0.85f, 0.0f, (float)avgSlot);
                addMesh(t.RuntimeMesh, reg.get<TransformComponent>(e).WorldTransform, m);
            }

            // MultiMesh: her kaynak alt meshi için tüm örnekler tek BLAS (bileşenin yerel uzayında)
            for (auto e : reg.view<TransformComponent, MultiMeshComponent>())
            {
                if (!scene.IsEntityActive(e)) continue;
                const auto& c = reg.get<MultiMeshComponent>(e);
                if (c.Transforms.empty()) continue;
                const glm::mat4& w = reg.get<TransformComponent>(e).WorldTransform;
                std::vector<std::pair<std::shared_ptr<Mesh>, Mat>> parts;
                if (c.Source == MultiMeshSource::Model)
                {
                    if (!c.RuntimeModel) continue;
                    for (size_t i = 0; i < c.RuntimeModel->Meshes.size(); ++i)
                    {
                        static const Material kDefault;
                        const Material& m = i < c.RuntimeModel->Materials.size() && c.RuntimeModel->Materials[i] ? *c.RuntimeModel->Materials[i] : kDefault;
                        parts.push_back({ c.RuntimeModel->Meshes[i], MakeMat(m) });
                    }
                }
                else if (c.RuntimeMaterial)
                    parts.push_back({ MultiMeshPrimitive(c.Primitive), MakeMat(*c.RuntimeMaterial) });
                else
                {
                    // Henüz çizilmemiş (malzemesi MultiMesh::Render'da kurulur): aynı ayarlarla
                    Material m;
                    m.Albedo = glm::vec3(c.Color);
                    m.Metallic = c.Metallic;
                    m.Roughness = c.Roughness;
                    m.AlbedoMap = c.RuntimeTexture;
                    m.Opacity = c.Color.a;
                    m.Rendering = c.Color.a < 0.999f ? Material::RenderMode::Transparent : Material::RenderMode::Opaque;
                    parts.push_back({ MultiMeshPrimitive(c.Primitive), MakeMat(m) });
                }
                for (size_t i = 0; i < parts.size(); ++i)
                {
                    const auto& mesh = parts[i].first;
                    if (!mesh || mesh->GetIndices().size() < 3) continue;
                    uint64_t key = 0xC0FFEEull;
                    Mix(key, (uint64_t)(uint32_t)e);
                    Mix(key, (uint64_t)(uintptr_t)&scene);
                    Mix(key, i);
                    uint64_t version = c.Version;
                    Mix(version, (uint64_t)(uintptr_t)mesh.get());
                    Mix(version, mesh->GetVersion());
                    Mix(h, version);
                    if (request(key, nullptr, version, [&]() { return InputFromMesh(*mesh, &c.Transforms, kMultiMeshMaxTriangles); }))
                        addInstance(key, w, parts[i].second);
                    else
                        Mix(h, 0xB1A5ull);
                }
            }

            int pending = 0;
            for (auto& [key, e] : s.Blas)
                if (e.Job.valid())
                    ++pending;
            s.Info.PendingBuilds = pending;
            if (s.Frame % 120 == 0)
                CollectGarbage(s);

            if (h == s.InstanceHash && !s.LayoutChanged && s.TNodeBuf)
                return s.Info;

            // TLAS: örneklerin dünya kutuları
            std::vector<Inst> insts;
            std::vector<Prim> prims;
            insts.reserve(descs.size());
            prims.reserve(descs.size());
            int triangles = 0;
            for (const Desc& d : descs)
            {
                auto it = s.Blas.find(d.Key);
                if (it == s.Blas.end() || !it->second.Uploaded || !it->second.Data) continue;
                const BlasEntry& e = it->second;
                const glm::mat4 inv = glm::inverse(d.World);
                if (!std::isfinite(inv[0][0]) || !std::isfinite(inv[3][3])) continue;   // sıfır ölçek
                Inst in;
                const glm::mat4 wt = glm::transpose(d.World), it4 = glm::transpose(inv);
                in.W0 = wt[0]; in.W1 = wt[1]; in.W2 = wt[2];
                in.I0 = it4[0]; in.I1 = it4[1]; in.I2 = it4[2];
                in.Info = glm::ivec4((int)e.NodeBase, d.Mat, (int)e.TriCount, 0);
                glm::vec3 mn(FLT_MAX), mx(-FLT_MAX);
                for (int c = 0; c < 8; ++c)
                {
                    const glm::vec3 p((c & 1) ? e.Data->Max.x : e.Data->Min.x, (c & 2) ? e.Data->Max.y : e.Data->Min.y, (c & 4) ? e.Data->Max.z : e.Data->Min.z);
                    const glm::vec3 wp = glm::vec3(d.World * glm::vec4(p, 1.0f));
                    mn = glm::min(mn, wp);
                    mx = glm::max(mx, wp);
                }
                insts.push_back(in);
                prims.push_back({ mn, mx });
                triangles += (int)e.TriCount;
            }
            std::vector<uint32_t> order;
            std::vector<Node> tnodes;
            BuildBVH(prims, order, tnodes, 2);
            std::vector<Inst> sorted(insts.size());
            for (size_t i = 0; i < order.size(); ++i)
                sorted[i] = insts[order[i]];
            UploadSmall(s.TNodeBuf, tnodes.data(), tnodes.size() * sizeof(Node));
            UploadSmall(s.InstBuf, sorted.data(), sorted.size() * sizeof(Inst));
            UploadSmall(s.MatBuf, mats.data(), mats.size() * sizeof(Mat));
            int unique = 0;
            for (auto& [key, e] : s.Blas)
                if (e.Uploaded) ++unique;
            s.Info.Triangles = triangles;
            s.Info.Nodes = (int)tnodes.size();
            s.Info.Instances = (int)sorted.size();
            s.Info.UniqueMeshes = unique;
            s.Info.BoundsMin = tnodes.empty() ? glm::vec3(0.0f) : glm::vec3(tnodes[0].Min);
            s.Info.BoundsMax = tnodes.empty() ? glm::vec3(0.0f) : glm::vec3(tnodes[0].Max);
            ++s.Info.GeometryVersion;
            s.InstanceHash = h;
            s.LayoutChanged = false;
            return s.Info;
        }

        uint64_t Bind(uint32_t program, int bgUnit, int lightUnit, int avgUnit)
        {
            State& s = S();
            // Bu karenin ışıkları ve ortamı (raster ile aynı veri)
            DirectionalLightComponent sun;
            const bool hasSun = Renderer3D::GetSunLight(sun);
            int pointCount = 0, spotCount = 0, areaCount = 0;
            const LightsUBO& lights = Renderer3D::GetLightData(pointCount, spotCount, areaCount);
            const SceneEnvironment& env = Renderer3D::GetEnvironment();
            const auto& rs = env.Settings;
            auto envMap = Renderer3D::GetActiveEnvironmentMap();
            uint32_t lightCube = 0;
            if (env.Skybox && env.Skybox->GetRendererID())
                lightCube = env.Skybox->GetRendererID();
            else if (env.UseProceduralSky && envMap && envMap->GetSkyCubemapID())
                lightCube = envMap->GetSkyCubemapID();
            float bgExposure = env.Skybox ? env.SkyboxExposure : 1.0f;
            glm::vec3 bgTint = env.Skybox ? env.SkyboxTint : glm::vec3(1.0f);
            uint32_t bgCube = lightCube;
            if (env.BackgroundSkybox && env.BackgroundSkybox->GetRendererID())
            {
                bgCube = env.BackgroundSkybox->GetRendererID();
                bgExposure = env.BackgroundExposure;
                bgTint = env.BackgroundTint;
            }
            const bool hasIBL = envMap && rs.Environment.ImageBasedLighting && lightCube;
            const glm::vec3 skyRGB = env.AmbientSource == 2 ? env.SkyColor : env.AmbientColor;
            float ambientScale = env.AmbientIntensity * (env.AmbientSource == 0 ? rs.Environment.SkyboxIntensity : 1.0f);
            if (hasIBL && env.Skybox)
                ambientScale *= env.SkyboxExposure;
            const float rot = glm::radians(env.Skybox ? env.SkyboxRotation : 0.0f);
            const glm::vec3 bgColor = SRGB(rs.Environment.BackgroundColor);

            uint64_t lh = 0;
            Mix(lh, hasSun ? 1 : 0);
            if (hasSun)
            {
                MixF(lh, sun.Direction.x); MixF(lh, sun.Direction.y); MixF(lh, sun.Direction.z);
                MixF(lh, sun.Color.x); MixF(lh, sun.Color.y); MixF(lh, sun.Color.z); MixF(lh, sun.Intensity);
            }
            Mix(lh, (uint64_t)pointCount | ((uint64_t)spotCount << 16) | ((uint64_t)areaCount << 32));
            MixBytes(lh, lights.PointLights, sizeof(GPUPointLight) * (size_t)pointCount);
            MixBytes(lh, lights.SpotLights, sizeof(GPUSpotLight) * (size_t)spotCount);
            MixBytes(lh, lights.AreaLights, sizeof(GPUAreaLight) * (size_t)areaCount);
            Mix(lh, lightCube); Mix(lh, bgCube); Mix(lh, (uint64_t)env.AmbientSource); Mix(lh, env.DrawSky ? 1 : 0);
            MixF(lh, ambientScale); MixF(lh, rot); MixF(lh, bgExposure); MixF(lh, skyRGB.x); MixF(lh, skyRGB.y); MixF(lh, skyRGB.z);
            if (lh != s.LightHash || !s.PointBuf)
            {
                UploadSmall(s.PointBuf, lights.PointLights, sizeof(GPUPointLight) * (size_t)pointCount);
                UploadSmall(s.SpotBuf, lights.SpotLights, sizeof(GPUSpotLight) * (size_t)spotCount);
                UploadSmall(s.AreaBuf, lights.AreaLights, sizeof(GPUAreaLight) * (size_t)areaCount);
                s.LightHash = lh;
            }
            // Boş sahnede de geçerli tamponlar (sürücü bağlanmamış SSBO'ya kızmasın)
            if (!s.TNodeBuf) UploadSmall(s.TNodeBuf, nullptr, 0);
            if (!s.InstBuf) UploadSmall(s.InstBuf, nullptr, 0);
            if (!s.MatBuf) UploadSmall(s.MatBuf, nullptr, 0);
            if (!s.Tris.Buf) Append(s.Tris, nullptr, 0);
            if (!s.Nodes.Buf) Append(s.Nodes, nullptr, 0);
            EnsureAverages(s);

            glUseProgram(program);
            auto U = [&](const char* n) { return glGetUniformLocation(program, n); };
            glUniform1i(U("u_NodeCount"), s.Info.Nodes);
            const glm::vec4 sunDir = hasSun ? glm::vec4(glm::normalize(sun.Direction), 1.0f) : glm::vec4(0.0f, -1.0f, 0.0f, 0.0f);
            const glm::vec3 sunColor = hasSun ? sun.Color * sun.Intensity : glm::vec3(0.0f);
            glUniform4fv(U("u_SunDir"), 1, &sunDir.x);
            glUniform3fv(U("u_SunColor"), 1, &sunColor.x);
            glUniform3i(U("u_LightCounts"), pointCount, spotCount, areaCount);
            glUniform4i(U("u_SkyFlags"), bgCube ? 1 : 0, (hasIBL && env.AmbientSource == 0) ? 1 : 0, env.AmbientSource, env.DrawSky ? 1 : 0);
            glUniform4f(U("u_BgTintExposure"), bgTint.x, bgTint.y, bgTint.z, bgExposure);
            glUniform2f(U("u_SkyRot"), std::cos(rot), std::sin(rot));
            glUniform4f(U("u_AmbientSky"), skyRGB.x, skyRGB.y, skyRGB.z, ambientScale);
            glUniform3fv(U("u_AmbientEquator"), 1, &env.EquatorColor.x);
            glUniform3fv(U("u_AmbientGround"), 1, &env.GroundColor.x);
            glUniform3fv(U("u_BgColor"), 1, &bgColor.x);
            // Prosedürel gökyüzü küpü güneş diskini içerir; güneş zaten doğrudan ışık olarak örneklenir
            const bool proceduralSky = !env.Skybox && lightCube && hasSun;
            const glm::vec3 toSun = hasSun ? -glm::normalize(sun.Direction) : glm::vec3(0.0f, 1.0f, 0.0f);
            glUniform4f(U("u_SunExclude"), toSun.x, toSun.y, toSun.z, proceduralSky ? std::cos(0.05f) : 0.0f);
            glUniform1i(U("u_BgCube"), bgUnit);
            glUniform1i(U("u_LightCube"), lightUnit);
            glUniform1i(U("u_TexAvg"), avgUnit);
            glBindTextureUnit((GLuint)bgUnit, bgCube);
            glBindTextureUnit((GLuint)lightUnit, lightCube);
            glBindTextureUnit((GLuint)avgUnit, s.AvgTexture);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, s.Tris.Buf);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, s.Nodes.Buf);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, s.MatBuf);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, s.PointBuf);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, s.SpotBuf);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, s.AreaBuf);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 6, s.TNodeBuf);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 7, s.InstBuf);
            return lh;
        }
    }
}
