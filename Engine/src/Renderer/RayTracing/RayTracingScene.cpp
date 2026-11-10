#include "RayTracingScene.h"
#include "Scene/Scene.h"
#include "Scene/Entity.h"
#include "Scene/Components.h"
#include "Scene/MeshRendererMaterial.h"
#include "Renderer/IBL/EnvironmentMap.h"
#include "Renderer/Utilities/Renderer3D/Renderer3D.h"
#include "Renderer/Utilities/Renderer3D/RendererUBO.h"
#include "Renderer/Utilities/Renderer3D/Mesh.h"
#include "Renderer/Utilities/Renderer3D/Material.h"
#include "Renderer/Utilities/Renderer3D/ModelLoader.h"
#include "Core/Logging/Log.h"

#include <glad/glad.h>
#include <glm/gtc/matrix_inverse.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <vector>

namespace Conqueror
{
    namespace
    {
        struct Tri { glm::vec4 V0, V1, V2; glm::vec4 N0, N1, N2; };   // V0.w = malzeme dizini
        struct Node { glm::vec4 Min, Max; };                          // Min.w = sol çocuk / ilk üçgen, Max.w = üçgen sayısı (0: iç düğüm)
        struct Mat { glm::vec4 Albedo, Emission, Params; };          // Params: x metal, y pürüz


        const char* kCommonGLSL = R"(
struct Tri { vec4 V0, V1, V2, N0, N1, N2; };
struct Node { vec4 Min, Max; };
struct Mat { vec4 Albedo, Emission, Params; };
struct PointL { vec4 PositionRange, ColorIntensity, Attenuation, Profile0, Profile1; };
struct SpotL { vec4 PositionRange, ColorIntensity, DirectionCosOuter, AttenuationCosInner, Profile0, Profile1; };
struct AreaL { vec4 PositionRange, ColorIntensity, Right, Up; };
layout(std430, binding = 0) readonly buffer Tris { Tri tris[]; };
layout(std430, binding = 1) readonly buffer Nodes { Node nodes[]; };
layout(std430, binding = 2) readonly buffer Mats { Mat mats[]; };
layout(std430, binding = 3) readonly buffer Points { PointL points[]; };
layout(std430, binding = 4) readonly buffer Spots { SpotL spots[]; };
layout(std430, binding = 5) readonly buffer Areas { AreaL areas[]; };
uniform int u_NodeCount;
uniform vec4 u_SunDir;          // xyz ışığın gittiği yön, w: güneş var
uniform vec3 u_SunColor;
uniform ivec3 u_LightCounts;    // nokta, spot, alan
// Gökyüzü: arka plan küpü (kamera ışını) ve aydınlatma küpü (sekmeler), motorun ortam ışığı ayarları
uniform samplerCube u_BgCube;
uniform samplerCube u_LightCube;
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

bool hitBox(vec3 o, vec3 inv, vec3 mn, vec3 mx, float tMax)
{
    vec3 t0 = (mn - o) * inv, t1 = (mx - o) * inv;
    vec3 a = min(t0, t1), b = max(t0, t1);
    float lo = max(max(a.x, a.y), max(a.z, 0.0)), hi = min(min(b.x, b.y), min(b.z, tMax));
    return lo <= hi;
}

// En yakın (any: herhangi bir) kesişim; üçgen dizini (-1: yok), t ve barisentrik
int trace(vec3 o, vec3 d, float tMax, out float tHit, out vec2 bary, bool any)
{
    vec3 inv = vec3(1.0) / vec3(abs(d.x) > 1e-12 ? d.x : 1e-12, abs(d.y) > 1e-12 ? d.y : 1e-12, abs(d.z) > 1e-12 ? d.z : 1e-12);
    int stack[64];
    int sp = 0;
    int best = -1;
    tHit = tMax;
    bary = vec2(0.0);
    if (u_NodeCount <= 0) return -1;
    stack[sp++] = 0;
    while (sp > 0)
    {
        Node n = nodes[stack[--sp]];
        if (!hitBox(o, inv, n.Min.xyz, n.Max.xyz, tHit)) continue;
        int count = int(n.Max.w + 0.5);
        int first = int(n.Min.w + 0.5);
        if (count > 0)
        {
            for (int i = first; i < first + count; ++i)
            {
                vec3 p0 = tris[i].V0.xyz;
                vec3 e1 = tris[i].V1.xyz - p0, e2 = tris[i].V2.xyz - p0;
                vec3 p = cross(d, e2);
                float det = dot(e1, p);
                if (abs(det) < 1e-12) continue;
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
                    tHit = t; bary = vec2(u, v); best = i;
                    if (any) return best;
                }
            }
        }
        else if (sp < 62)
        {
            // Yakın çocuk önce (ışın yönünde merkezi önde olan)
            Node l = nodes[first], r = nodes[first + 1];
            float dl = dot((l.Min.xyz + l.Max.xyz) * 0.5 - o, d), dr = dot((r.Min.xyz + r.Max.xyz) * 0.5 - o, d);
            if (dl < dr) { stack[sp++] = first + 1; stack[sp++] = first; }
            else { stack[sp++] = first; stack[sp++] = first + 1; }
        }
    }
    return best;
}

bool occluded(vec3 o, vec3 d, float dist) { float t; vec2 b; return trace(o, d, dist, t, b, true) >= 0; }

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

// Doğrudan ışık: güneş, nokta, spot, alan ışıkları (gölge ışınlı)
vec3 directLight(vec3 p, vec3 po, vec3 n, vec3 v, Mat m)
{
    vec3 sum = vec3(0.0);
    if (u_SunDir.w > 0.5)
    {
        vec3 l = -normalize(u_SunDir.xyz);
        float c = dot(n, l);
        if (c > 0.0 && !occluded(po, l, 1e30))
            sum += brdf(n, v, l, m) * u_SunColor * c;
    }
    for (int i = 0; i < u_LightCounts.x; ++i)
    {
        vec3 tl = points[i].PositionRange.xyz - p;
        float dist = length(tl);
        if (dist >= points[i].PositionRange.w || dist < 1e-4) continue;
        vec3 l = tl / dist;
        float c = dot(n, l);
        if (c <= 0.0 || occluded(po, l, dist - 2e-3)) continue;
        sum += brdf(n, v, l, m) * points[i].ColorIntensity.rgb * attenuation(dist, points[i].PositionRange.w, points[i].Attenuation.xyz) * c;
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
        sum += brdf(n, v, l, m) * spots[i].ColorIntensity.rgb * attenuation(dist, spots[i].PositionRange.w, spots[i].AttenuationCosInner.xyz) * cone * c;
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
        sum += brdf(n, v, l, m) * areas[i].ColorIntensity.rgb * rangeWindow(dist, areas[i].PositionRange.w) * c * cl * areaSize / (dist * dist);
    }
    return sum;
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
        void MixMaterial(uint64_t& h, const Material& m)
        {
            MixF(h, m.Albedo.x); MixF(h, m.Albedo.y); MixF(h, m.Albedo.z);
            MixF(h, m.Metallic); MixF(h, m.Roughness);
            MixF(h, m.EmissionColor.x); MixF(h, m.EmissionColor.y); MixF(h, m.EmissionColor.z); MixF(h, m.EmissionStrength);
        }

        // Medyan bölmeli BVH (yaprak <= 4 üçgen); üçgenler düğüm sırasına dizilir
        void Build(std::vector<Tri>& tris, std::vector<Node>& nodes)
        {
            nodes.clear();
            if (tris.empty())
                return;
            std::vector<glm::vec3> cent(tris.size());
            for (size_t i = 0; i < tris.size(); ++i)
                cent[i] = (glm::vec3(tris[i].V0) + glm::vec3(tris[i].V1) + glm::vec3(tris[i].V2)) / 3.0f;
            std::vector<uint32_t> order(tris.size());
            for (uint32_t i = 0; i < (uint32_t)order.size(); ++i) order[i] = i;
            nodes.reserve(tris.size() * 2);
            struct Job { int Node; uint32_t Begin, End; };
            std::vector<Job> jobs{ { 0, 0, (uint32_t)tris.size() } };
            nodes.push_back({});
            while (!jobs.empty())
            {
                const Job j = jobs.back();
                jobs.pop_back();
                glm::vec3 mn(1e30f), mx(-1e30f), cmn(1e30f), cmx(-1e30f);
                for (uint32_t i = j.Begin; i < j.End; ++i)
                {
                    const Tri& t = tris[order[i]];
                    mn = glm::min(mn, glm::min(glm::vec3(t.V0), glm::min(glm::vec3(t.V1), glm::vec3(t.V2))));
                    mx = glm::max(mx, glm::max(glm::vec3(t.V0), glm::max(glm::vec3(t.V1), glm::vec3(t.V2))));
                    cmn = glm::min(cmn, cent[order[i]]);
                    cmx = glm::max(cmx, cent[order[i]]);
                }
                const uint32_t count = j.End - j.Begin;
                const glm::vec3 ext = cmx - cmn;
                nodes[(size_t)j.Node].Min = glm::vec4(mn, 0.0f);
                nodes[(size_t)j.Node].Max = glm::vec4(mx, 0.0f);
                if (count <= 4 || std::max(ext.x, std::max(ext.y, ext.z)) < 1e-6f)
                {
                    nodes[(size_t)j.Node].Min.w = (float)j.Begin;
                    nodes[(size_t)j.Node].Max.w = (float)count;
                    continue;
                }
                const int axis = ext.x > ext.y ? (ext.x > ext.z ? 0 : 2) : (ext.y > ext.z ? 1 : 2);
                const uint32_t mid = j.Begin + count / 2;
                std::nth_element(order.begin() + j.Begin, order.begin() + mid, order.begin() + j.End,
                                 [&](uint32_t a, uint32_t b) { return cent[a][axis] < cent[b][axis]; });
                const int left = (int)nodes.size();
                nodes.push_back({});
                nodes.push_back({});
                nodes[(size_t)j.Node].Min.w = (float)left;
                jobs.push_back({ left, j.Begin, mid });
                jobs.push_back({ left + 1, mid, j.End });
            }
            std::vector<Tri> sorted(tris.size());
            for (size_t i = 0; i < order.size(); ++i) sorted[i] = tris[order[i]];
            tris.swap(sorted);
        }

        void Upload(GLuint& buf, const void* data, size_t bytes)
        {
            if (buf) glDeleteBuffers(1, &buf);
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

        // Sahnedeki ağlar (Mesh Renderer: ilkel, özel ağ, model) -> dünya uzayı üçgenleri + malzemeler; özet her karede hesaplanır
        uint64_t Gather(Scene& scene, std::vector<Tri>* tris, std::vector<Mat>* mats)
        {
            uint64_t h = 1469598103934665603ull;
            auto& reg = scene.m_Registry;
            auto addMat = [&](const Material& m) -> float {
                if (!mats) return 0.0f;
                Mat out;
                out.Albedo = glm::vec4(SRGB(m.Albedo), 1.0f);
                out.Emission = glm::vec4(glm::max(m.EmissionColor, glm::vec3(0.0f)) * std::max(m.EmissionStrength, 0.0f), 0.0f);
                out.Params = glm::vec4(std::clamp(m.Metallic, 0.0f, 1.0f), std::clamp(m.Roughness, 0.02f, 1.0f), 0.0f, 0.0f);
                mats->push_back(out);
                return (float)(mats->size() - 1);
            };
            auto addMesh = [&](const std::shared_ptr<Mesh>& mesh, const glm::mat4& w, float matIndex) {
                if (!mesh || !tris) return;
                const glm::mat3 nm = glm::inverseTranspose(glm::mat3(w));
                const auto& vs = mesh->GetVertices();
                const auto& is = mesh->GetIndices();
                for (size_t i = 0; i + 2 < is.size(); i += 3)
                {
                    if (is[i] >= vs.size() || is[i + 1] >= vs.size() || is[i + 2] >= vs.size()) continue;
                    const Vertex* v[3] = { &vs[is[i]], &vs[is[i + 1]], &vs[is[i + 2]] };
                    Tri t;
                    glm::vec4* P[3] = { &t.V0, &t.V1, &t.V2 };
                    glm::vec4* N[3] = { &t.N0, &t.N1, &t.N2 };
                    for (int k = 0; k < 3; ++k)
                    {
                        *P[k] = glm::vec4(glm::vec3(w * glm::vec4(v[k]->Position, 1.0f)), 0.0f);
                        const glm::vec3 n = nm * v[k]->Normal;
                        *N[k] = glm::vec4(glm::dot(n, n) > 1e-12f ? glm::normalize(n) : glm::vec3(0.0f, 1.0f, 0.0f), 0.0f);
                    }
                    t.V0.w = matIndex;
                    tris->push_back(t);
                }
            };
            for (auto e : reg.view<TransformComponent, MeshRendererComponent>())
            {
                if (!scene.IsEntityActive(e)) continue;
                const auto& mr = reg.get<MeshRendererComponent>(e);
                const glm::mat4& w = reg.get<TransformComponent>(e).WorldTransform;
                Mix(h, (uint64_t)(uint32_t)e);
                for (int i = 0; i < 4; ++i) for (int k = 0; k < 4; ++k) MixF(h, w[i][k]);
                Mix(h, (uint64_t)(uintptr_t)mr.ModelData.get());
                Mix(h, (uint64_t)(uintptr_t)mr.CustomMesh.get());
                Mix(h, (uint64_t)mr.Type);
                if (mr.IsModel())
                {
                    if (!mr.ModelData) continue;
                    for (size_t mi = 0; mi < mr.ModelData->Meshes.size(); ++mi)
                    {
                        static const Material kDefault;
                        const Material& m = mi < mr.ModelData->Materials.size() && mr.ModelData->Materials[mi] ? *mr.ModelData->Materials[mi] : kDefault;
                        MixMaterial(h, m);
                        if (tris) addMesh(mr.ModelData->Meshes[mi], w, addMat(m));
                    }
                }
                else
                {
                    const Material& m = MeshRendererMaterial::Shared(mr);
                    MixMaterial(h, m);
                    if (tris) addMesh(mr.CustomMesh ? mr.CustomMesh : PrimitiveMesh(mr.Type), w, addMat(m));
                }
            }
            return h;
        }


        struct State
        {
            GLuint TriBuf = 0, NodeBuf = 0, MatBuf = 0, PointBuf = 0, SpotBuf = 0, AreaBuf = 0;
            uint64_t SceneHash = 0, LightHash = 0;
            const Scene* LastScene = nullptr;
            double LastRebuild = -1e9;
            RayTracingScene::Stats Info;
        };
        State& S() { static auto& s = *new State(); return s; }

        double Seconds()
        {
            using namespace std::chrono;
            static const auto start = steady_clock::now();
            return duration<double>(steady_clock::now() - start).count();
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

        const Stats& Update(Scene& scene, float minInterval)
        {
            State& s = S();
            const uint64_t hash = Gather(scene, nullptr, nullptr) ^ (uint64_t)(uintptr_t)&scene;
            if (hash == s.SceneHash && s.TriBuf)
                return s.Info;
            const double now = Seconds();
            if (s.TriBuf && &scene == s.LastScene && minInterval > 0.0f && now - s.LastRebuild < minInterval)
                return s.Info;
            std::vector<Tri> tris;
            std::vector<Mat> mats;
            Gather(scene, &tris, &mats);
            std::vector<Node> nodes;
            Build(tris, nodes);
            Upload(s.TriBuf, tris.data(), tris.size() * sizeof(Tri));
            Upload(s.NodeBuf, nodes.data(), nodes.size() * sizeof(Node));
            Upload(s.MatBuf, mats.data(), mats.size() * sizeof(Mat));
            s.Info.Triangles = (int)tris.size();
            s.Info.Nodes = (int)nodes.size();
            s.Info.BoundsMin = nodes.empty() ? glm::vec3(0.0f) : glm::vec3(nodes[0].Min);
            s.Info.BoundsMax = nodes.empty() ? glm::vec3(0.0f) : glm::vec3(nodes[0].Max);
            ++s.Info.GeometryVersion;
            s.SceneHash = hash;
            s.LastScene = &scene;
            s.LastRebuild = now;
            return s.Info;
        }

        uint64_t Bind(uint32_t program, int bgUnit, int lightUnit)
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
                Upload(s.PointBuf, lights.PointLights, sizeof(GPUPointLight) * (size_t)pointCount);
                Upload(s.SpotBuf, lights.SpotLights, sizeof(GPUSpotLight) * (size_t)spotCount);
                Upload(s.AreaBuf, lights.AreaLights, sizeof(GPUAreaLight) * (size_t)areaCount);
                s.LightHash = lh;
            }

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
            glBindTextureUnit((GLuint)bgUnit, bgCube);
            glBindTextureUnit((GLuint)lightUnit, lightCube);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, s.TriBuf);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, s.NodeBuf);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, s.MatBuf);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, s.PointBuf);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, s.SpotBuf);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, s.AreaBuf);
            return lh;
        }
    }
}
