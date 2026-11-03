#include "RayTracingShaders.h"

#include "Core/Logging/Log.h"
#include "Core/Serialization/SceneSerializer.h"
#include "Core/Time/TimeManager.h"
#include "Renderer/RHI/Texture.h"
#include "Renderer/RenderTargetRegistry.h"
#include "Renderer/Utilities/Renderer3D/Material.h"
#include "Renderer/Utilities/Renderer3D/Mesh.h"
#include "Renderer/Utilities/Renderer3D/Renderer3D.h"
#include "Scene/Components.h"
#include "Scene/Entity.h"
#include "Scene/MeshRendererMaterial.h"
#include "Scene/Scene.h"

#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace Conqueror::RayTracingShaders
{
    namespace fs = std::filesystem;

    namespace
    {
        // GPU düzeni (std430): üçgen = 3 köşe + normal + albedo (vec4'lerle hizalı)
        struct GpuTriangle { glm::vec4 V0, V1, V2, Normal, Albedo; };
        // BVH düğümü: Min.w = sol çocuk / ilk üçgen, Max.w = üçgen sayısı (0: iç düğüm)
        struct GpuNode { glm::vec4 Min, Max; };

        struct Accel
        {
            GLuint TriBuffer = 0, NodeBuffer = 0;
            size_t Hash = 0;
            int Triangles = 0, Nodes = 0;
        } s_Accel;

        struct Program { GLuint Id = 0; fs::file_time_type Time{}; std::string Error; };
        std::unordered_map<std::string, Program>& Programs()
        {
            static auto* m = new std::unordered_map<std::string, Program>();
            return *m;
        }

        size_t SceneHash(Scene& scene)
        {
            size_t h = 1469598103934665603ull;
            auto mix = [&](size_t v) { h = (h ^ v) * 1099511628211ull; };
            auto view = scene.m_Registry.view<TransformComponent, MeshRendererComponent>();
            for (auto e : view)
            {
                if (!scene.IsEntityActive(e)) continue;
                const auto& t = view.get<TransformComponent>(e).WorldTransform;
                for (int c = 0; c < 4; ++c)
                    for (int r = 0; r < 4; ++r)
                        mix(std::hash<float>{}(t[c][r]));
                mix((size_t)view.get<MeshRendererComponent>(e).Type);
                const auto& m = MeshRendererMaterial::Shared(view.get<MeshRendererComponent>(e));
                mix(std::hash<float>{}(m.Albedo.r + m.Albedo.g * 3.0f + m.Albedo.b * 7.0f));
            }
            return h;
        }

        void BuildAccel(Scene& scene)
        {
            std::vector<GpuTriangle> tris;
            auto view = scene.m_Registry.view<TransformComponent, MeshRendererComponent>();
            for (auto e : view)
            {
                if (!scene.IsEntityActive(e)) continue;
                const auto& mr = view.get<MeshRendererComponent>(e);
                if (mr.IsModel()) continue;   // model ağları bu (deneysel) ışın izleme sahnesinde yok (önceden de yoktu)
                std::shared_ptr<Mesh> mesh = mr.CustomMesh;
                if (!mesh)
                    switch (mr.Type)
                    {
                        case MeshType::Sphere: mesh = Renderer3D::GetSphereMesh(); break;
                        case MeshType::Plane: mesh = Renderer3D::GetPlaneMesh(); break;
                        case MeshType::Cylinder: mesh = Renderer3D::GetCylinderMesh(); break;
                        default: mesh = Renderer3D::GetCubeMesh(); break;
                    }
                if (!mesh) continue;
                const glm::mat4& w = view.get<TransformComponent>(e).WorldTransform;
                const auto& m = MeshRendererMaterial::Shared(mr);
                const glm::vec4 albedo(m.Albedo, m.Opacity);
                const auto& vs = mesh->GetVertices();
                const auto& is = mesh->GetIndices();
                for (size_t i = 0; i + 2 < is.size(); i += 3)
                {
                    GpuTriangle t;
                    glm::vec3 p0 = glm::vec3(w * glm::vec4(vs[is[i]].Position, 1.0f));
                    glm::vec3 p1 = glm::vec3(w * glm::vec4(vs[is[i + 1]].Position, 1.0f));
                    glm::vec3 p2 = glm::vec3(w * glm::vec4(vs[is[i + 2]].Position, 1.0f));
                    glm::vec3 n = glm::cross(p1 - p0, p2 - p0);
                    if (glm::dot(n, n) < 1e-14f) continue;
                    t.V0 = glm::vec4(p0, 0.0f); t.V1 = glm::vec4(p1, 0.0f); t.V2 = glm::vec4(p2, 0.0f);
                    t.Normal = glm::vec4(glm::normalize(n), 0.0f);
                    t.Albedo = albedo;
                    tris.push_back(t);
                }
            }
            // BVH: ağırlık merkezine göre en uzun eksende ortadan bölme (yaprak <= 4 üçgen)
            std::vector<GpuNode> nodes;
            std::vector<int> order(tris.size());
            for (size_t i = 0; i < order.size(); ++i) order[i] = (int)i;
            auto centroid = [&](int i) { return (glm::vec3(tris[(size_t)i].V0) + glm::vec3(tris[(size_t)i].V1) + glm::vec3(tris[(size_t)i].V2)) / 3.0f; };
            std::function<int(int, int)> build = [&](int begin, int end) -> int {
                const int index = (int)nodes.size();
                nodes.push_back({});
                glm::vec3 mn(1e30f), mx(-1e30f), cmn(1e30f), cmx(-1e30f);
                for (int i = begin; i < end; ++i)
                {
                    const auto& t = tris[(size_t)order[(size_t)i]];
                    for (const glm::vec4* v : { &t.V0, &t.V1, &t.V2 }) { mn = glm::min(mn, glm::vec3(*v)); mx = glm::max(mx, glm::vec3(*v)); }
                    const glm::vec3 c = centroid(order[(size_t)i]);
                    cmn = glm::min(cmn, c); cmx = glm::max(cmx, c);
                }
                // Düz yüzeyler (düzlem) sıfır kalınlıklı kutu vermesin: hassasiyet payı
                mn -= glm::vec3(1e-3f);
                mx += glm::vec3(1e-3f);
                const int count = end - begin;
                if (count <= 4)
                {
                    nodes[(size_t)index] = { glm::vec4(mn, (float)begin), glm::vec4(mx, (float)count) };
                    return index;
                }
                const glm::vec3 ext = cmx - cmn;
                const int axis = ext.x > ext.y && ext.x > ext.z ? 0 : (ext.y > ext.z ? 1 : 2);
                const int mid = (begin + end) / 2;
                std::nth_element(order.begin() + begin, order.begin() + mid, order.begin() + end,
                                 [&](int a, int b) { return centroid(a)[axis] < centroid(b)[axis]; });
                const int left = build(begin, mid);
                build(mid, end);   // sağ çocuk = sol + 1 alt ağacın hemen ardı (left + boyut); indeks aşağıda saklanır
                nodes[(size_t)index] = { glm::vec4(mn, (float)left), glm::vec4(mx, 0.0f) };
                return index;
            };
            if (!tris.empty())
                build(0, (int)tris.size());
            // İç düğümde sağ çocuk: sol alt ağacın bitişi. GPU'da kolay olsun diye Max.w'ye -(sağ+1) yazılır
            std::function<int(int)> fixRight = [&](int index) -> int {
                GpuNode& n = nodes[(size_t)index];
                if (n.Max.w > 0.0f)
                    return index + 1;   // yaprak: sonraki düğüm
                const int left = (int)n.Min.w;
                const int right = fixRight(left);
                nodes[(size_t)index].Max.w = -(float)(right + 1);
                return fixRight(right);
            };
            if (!nodes.empty())
                fixRight(0);
            std::vector<GpuTriangle> sorted(tris.size());
            for (size_t i = 0; i < order.size(); ++i)
                sorted[i] = tris[(size_t)order[i]];

            if (!s_Accel.TriBuffer) glCreateBuffers(1, &s_Accel.TriBuffer);
            if (!s_Accel.NodeBuffer) glCreateBuffers(1, &s_Accel.NodeBuffer);
            glNamedBufferData(s_Accel.TriBuffer, (GLsizeiptr)std::max<size_t>(1, sorted.size()) * sizeof(GpuTriangle), sorted.empty() ? nullptr : sorted.data(), GL_DYNAMIC_DRAW);
            glNamedBufferData(s_Accel.NodeBuffer, (GLsizeiptr)std::max<size_t>(1, nodes.size()) * sizeof(GpuNode), nodes.empty() ? nullptr : nodes.data(), GL_DYNAMIC_DRAW);
            s_Accel.Triangles = (int)sorted.size();
            s_Accel.Nodes = (int)nodes.size();
        }

        const char* kPrelude = R"(#version 450 core
layout(local_size_x = 8, local_size_y = 8) in;
layout(rgba8) uniform image2D u_Result;
struct CQ_Triangle { vec4 V0, V1, V2, Normal, Albedo; };
struct CQ_Node { vec4 Min, Max; };
layout(std430, binding = 0) readonly buffer CQ_Triangles { CQ_Triangle cq_Tris[]; };
layout(std430, binding = 1) readonly buffer CQ_Nodes { CQ_Node cq_Nodes[]; };
uniform int u_NodeCount;
uniform mat4 u_InvViewProj;
uniform vec3 u_CameraPos;
uniform vec3 u_SunDirection;   // ışığın gittiği yön
uniform vec3 u_SunColor;
uniform float u_Time;

struct CQ_RayHit { float Distance; vec3 Position; vec3 Normal; vec4 Albedo; int Triangle; };

bool cq_Box(vec3 o, vec3 inv, vec3 mn, vec3 mx, float tMax)
{
    vec3 t0 = (mn - o) * inv, t1 = (mx - o) * inv;
    // Eksene paralel ışın: 0 * sonsuz = NaN olmasın
    t0 = mix(t0, vec3(-1e30), isnan(t0)); t1 = mix(t1, vec3(1e30), isnan(t1));
    vec3 a = min(t0, t1), b = max(t0, t1);
    float tn = max(max(a.x, a.y), a.z), tf = min(min(b.x, b.y), b.z);
    return tf >= max(tn, 0.0) && tn < tMax;
}

// Işın - sahne (BVH). En yakın çarpma; yoksa false
bool CQ_TraceRay(vec3 origin, vec3 direction, float maxDistance, out CQ_RayHit hit)
{
    hit.Distance = maxDistance; hit.Triangle = -1; hit.Position = vec3(0); hit.Normal = vec3(0); hit.Albedo = vec4(0);
    if (u_NodeCount == 0) return false;
    vec3 safeDir = mix(direction, vec3(1e-8), lessThan(abs(direction), vec3(1e-8)));
    vec3 inv = 1.0 / safeDir;
    int stack[64]; int sp = 0; stack[sp++] = 0;
    while (sp > 0)
    {
        CQ_Node n = cq_Nodes[stack[--sp]];
        if (!cq_Box(origin, inv, n.Min.xyz, n.Max.xyz, hit.Distance)) continue;
        if (n.Max.w > 0.0)
        {
            int first = int(n.Min.w), count = int(n.Max.w);
            for (int i = first; i < first + count; ++i)
            {
                CQ_Triangle t = cq_Tris[i];
                vec3 e1 = t.V1.xyz - t.V0.xyz, e2 = t.V2.xyz - t.V0.xyz;
                vec3 p = cross(direction, e2);
                float det = dot(e1, p);
                if (abs(det) < 1e-8) continue;
                float invDet = 1.0 / det;
                vec3 s = origin - t.V0.xyz;
                float u = dot(s, p) * invDet;
                if (u < 0.0 || u > 1.0) continue;
                vec3 q = cross(s, e1);
                float v = dot(direction, q) * invDet;
                if (v < 0.0 || u + v > 1.0) continue;
                float d = dot(e2, q) * invDet;
                if (d > 1e-4 && d < hit.Distance)
                {
                    hit.Distance = d; hit.Triangle = i; hit.Normal = t.Normal.xyz; hit.Albedo = t.Albedo;
                }
            }
        }
        else if (sp < 62)
        {
            stack[sp++] = int(-n.Max.w) - 1;
            stack[sp++] = int(n.Min.w);
        }
    }
    if (hit.Triangle < 0) return false;
    hit.Position = origin + direction * hit.Distance;
    if (dot(hit.Normal, direction) > 0.0) hit.Normal = -hit.Normal;
    return true;
}

// Kameradan piksele ışın (uv 0..1)
void CQ_CameraRay(vec2 uv, out vec3 origin, out vec3 direction)
{
    vec4 farP = u_InvViewProj * vec4(uv * 2.0 - 1.0, 1.0, 1.0);
    origin = u_CameraPos;
    direction = normalize(farP.xyz / farP.w - origin);
}
#line 1
)";
        const char* kMain = R"(
void main()
{
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size = imageSize(u_Result);
    if (pixel.x >= size.x || pixel.y >= size.y) return;
    vec2 uv = (vec2(pixel) + 0.5) / vec2(size);
    imageStore(u_Result, pixel, RayGeneration(pixel, uv));
}
)";

        Program& GetProgram(const std::string& shader)
        {
            const std::string file = SceneSerializer::ResolveAssetPath(shader);
            Program& p = Programs()[file];
            std::error_code ec;
            const auto t = fs::last_write_time(file, ec);
            if (ec) { p.Error = "Ray tracing shader not found: " + shader; return p; }
            if (t == p.Time && (p.Id || !p.Error.empty())) return p;
            p.Time = t;
            if (p.Id) glDeleteProgram(p.Id);
            p.Id = 0;
            p.Error.clear();
            if (!GLAD_GL_VERSION_4_3) { p.Error = "Ray tracing shaders need OpenGL 4.3 compute"; return p; }
            std::ifstream f(file);
            std::stringstream ss;
            ss << f.rdbuf();
            const std::string src = std::string(kPrelude) + ss.str() + kMain;
            GLuint s = glCreateShader(GL_COMPUTE_SHADER);
            const char* c = src.c_str();
            glShaderSource(s, 1, &c, nullptr);
            glCompileShader(s);
            GLint ok = 0;
            glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
            if (!ok)
            {
                char log[4096];
                glGetShaderInfoLog(s, sizeof(log), nullptr, log);
                p.Error = log;
                glDeleteShader(s);
                CQ_CORE_ERROR("[RayTracing] {0}: {1}", fs::path(file).filename().string(), p.Error);
                return p;
            }
            p.Id = glCreateProgram();
            glAttachShader(p.Id, s);
            glLinkProgram(p.Id);
            glDeleteShader(s);
            glGetProgramiv(p.Id, GL_LINK_STATUS, &ok);
            if (!ok) { char log[4096]; glGetProgramInfoLog(p.Id, sizeof(log), nullptr, log); p.Error = log; glDeleteProgram(p.Id); p.Id = 0; }
            return p;
        }
    }

    bool Dispatch(Scene& scene, const std::string& shader, const std::string& targetTexture)
    {
        Program& p = GetProgram(shader);
        if (!p.Id)
            return false;
        auto target = Texture2D::Create(SceneSerializer::ResolveAssetPath(targetTexture));
        if (!target || !target->GetRendererID())
        {
            p.Error = "Target texture not found: " + targetTexture;
            return false;
        }
        // İvme yapısı: sahne değiştiyse yeniden kur
        scene.UpdateWorldTransforms();
        const size_t h = SceneHash(scene);
        if (h != s_Accel.Hash || !s_Accel.TriBuffer)
        {
            BuildAccel(scene);
            s_Accel.Hash = h;
        }
        // Kamera (birincil)
        glm::mat4 view(1.0f), proj = glm::perspective(glm::radians(60.0f), 1.0f, 0.1f, 1000.0f);
        glm::vec3 camPos(0.0f, 1.0f, 10.0f);
        const float aspect = (float)target->GetWidth() / (float)std::max(1u, target->GetHeight());
        if (Entity cam = scene.GetPrimaryCameraEntity(); cam && cam.HasComponent<CameraComponent>())
        {
            const glm::mat4 w = cam.GetComponent<TransformComponent>().WorldTransform;
            view = glm::inverse(w);
            camPos = glm::vec3(w[3]);
            const auto& c = cam.GetComponent<CameraComponent>().Camera;
            proj = glm::perspective(c.GetPerspectiveVerticalFOV(), aspect, c.GetPerspectiveNearClip(), c.GetPerspectiveFarClip());
        }
        else
            view = glm::lookAt(camPos, glm::vec3(0.0f), glm::vec3(0, 1, 0));
        glm::vec3 sunDir = glm::normalize(glm::vec3(-0.4f, -1.0f, -0.35f)), sunColor(1.0f);
        if (Entity sun = scene.GetSunSourceEntity(); sun && sun.HasComponent<DirectionalLightComponent>())
        {
            const auto& d = sun.GetComponent<DirectionalLightComponent>();
            sunDir = glm::normalize(d.Direction);
            sunColor = d.Color * d.Intensity;
        }
        GLint prev = 0;
        glGetIntegerv(GL_CURRENT_PROGRAM, &prev);
        glUseProgram(p.Id);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, s_Accel.TriBuffer);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, s_Accel.NodeBuffer);
        glUniform1i(glGetUniformLocation(p.Id, "u_NodeCount"), s_Accel.Nodes);
        const glm::mat4 inv = glm::inverse(proj * view);
        glUniformMatrix4fv(glGetUniformLocation(p.Id, "u_InvViewProj"), 1, GL_FALSE, &inv[0][0]);
        glUniform3fv(glGetUniformLocation(p.Id, "u_CameraPos"), 1, &camPos[0]);
        glUniform3fv(glGetUniformLocation(p.Id, "u_SunDirection"), 1, &sunDir[0]);
        glUniform3fv(glGetUniformLocation(p.Id, "u_SunColor"), 1, &sunColor[0]);
        glUniform1f(glGetUniformLocation(p.Id, "u_Time"), TimeManager::GetTime());
        glBindImageTexture(0, target->GetRendererID(), 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);
        glUniform1i(glGetUniformLocation(p.Id, "u_Result"), 0);
        glDispatchCompute((target->GetWidth() + 7) / 8, (target->GetHeight() + 7) / 8, 1);
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
        glUseProgram((GLuint)prev);
        if (auto* rt = dynamic_cast<RenderTargetTexture*>(target.get())) rt->RenderCount++;
        return true;
    }

    std::string GetError(const std::string& shader) { return GetProgram(shader).Error; }
    int GetTriangleCount() { return s_Accel.Triangles; }
    int GetNodeCount() { return s_Accel.Nodes; }

    void Shutdown()
    {
        if (s_Accel.TriBuffer) glDeleteBuffers(1, &s_Accel.TriBuffer);
        if (s_Accel.NodeBuffer) glDeleteBuffers(1, &s_Accel.NodeBuffer);
        s_Accel = {};
        for (auto& [k, p] : Programs()) if (p.Id) glDeleteProgram(p.Id);
        Programs().clear();
    }
}
