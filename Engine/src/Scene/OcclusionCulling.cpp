#include "OcclusionCulling.h"
#include "Scene/Scene.h"
#include "Scene/Entity.h"
#include "Scene/Components.h"
#include "Scene/ComponentRegistry.h"
#include "Renderer/Utilities/Renderer3D/Renderer3D.h"
#include "Renderer/Utilities/Renderer3D/Mesh.h"
#include "Renderer/Utilities/Renderer3D/ModelLoader.h"
#include "Core/Debug/DebugDraw.h"
#include "Core/Logging/Log.h"

#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <IconsFontAwesome6.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

namespace Conqueror
{
    namespace
    {
        constexpr uint32_t kBackface = 0xFFFFFFFFu;

        const char* kDrawVS = R"(#version 450 core
layout(location = 0) in vec3 a_Pos;
layout(location = 1) in float a_ID;
uniform mat4 u_VP;
flat out uint v_ID;
void main() { gl_Position = u_VP * vec4(a_Pos, 1.0); v_ID = uint(a_ID + 0.5); }
)";
        const char* kDrawFS = R"(#version 450 core
flat in uint v_ID;
layout(location = 0) out uint o_ID;
void main() { o_ID = gl_FrontFacing ? v_ID : 0xFFFFFFFFu; }
)";
        // Kimlik görüntüsündeki her pikselin nesnesini görünür işaretle; arka yüzleri say (örnek nokta bir nesnenin içindeyse atılır)
        const char* kGatherCS = R"(#version 450 core
layout(local_size_x = 8, local_size_y = 8) in;
layout(r32ui, binding = 0) uniform readonly uimage2D u_IDs;
layout(std430, binding = 1) buffer Vis { uint data[]; };
uniform ivec2 u_Size;
void main()
{
    ivec2 p = ivec2(gl_GlobalInvocationID.xy);
    if (p.x >= u_Size.x || p.y >= u_Size.y) return;
    uint id = imageLoad(u_IDs, p).r;
    if (id == 0u) return;
    if (id == 0xFFFFFFFFu) { atomicAdd(data[0], 1u); return; }
    atomicOr(data[1u + (id >> 5u)], 1u << (id & 31u));
}
)";

        GLuint CompileShader(GLenum type, const char* src, std::string& err)
        {
            GLuint s = glCreateShader(type);
            glShaderSource(s, 1, &src, nullptr);
            glCompileShader(s);
            GLint ok = 0;
            glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
            if (!ok)
            {
                char log[1024] = {};
                glGetShaderInfoLog(s, sizeof(log), nullptr, log);
                err = log;
                glDeleteShader(s);
                return 0;
            }
            return s;
        }

        GLuint LinkProgram(std::initializer_list<GLuint> shaders, std::string& err)
        {
            GLuint p = glCreateProgram();
            for (GLuint s : shaders) glAttachShader(p, s);
            glLinkProgram(p);
            for (GLuint s : shaders) glDeleteShader(s);
            GLint ok = 0;
            glGetProgramiv(p, GL_LINK_STATUS, &ok);
            if (!ok)
            {
                char log[1024] = {};
                glGetProgramInfoLog(p, sizeof(log), nullptr, log);
                err = log;
                glDeleteProgram(p);
                return 0;
            }
            return p;
        }

        // Nesnenin dünya üçgenleri (konum + kimlik)
        void AppendMesh(const std::shared_ptr<Mesh>& mesh, const glm::mat4& world, float id, std::vector<float>& out)
        {
            if (!mesh)
                return;
            const auto& vs = mesh->GetVertices();
            const auto& is = mesh->GetIndices();
            for (size_t i = 0; i + 2 < is.size(); i += 3)
                for (int k = 0; k < 3; ++k)
                {
                    const uint32_t vi = is[i + (size_t)k];
                    if (vi >= vs.size())
                        continue;
                    const glm::vec3 p = glm::vec3(world * glm::vec4(vs[vi].Position, 1.0f));
                    out.insert(out.end(), { p.x, p.y, p.z, id });
                }
        }

        void AppendBox(const glm::mat4& world, const glm::vec3& center, const glm::vec3& size, float id, std::vector<float>& out)
        {
            glm::vec3 c[8];
            for (int i = 0; i < 8; ++i)
            {
                const glm::vec3 s((i & 1) ? 0.5f : -0.5f, (i & 2) ? 0.5f : -0.5f, (i & 4) ? 0.5f : -0.5f);
                c[i] = glm::vec3(world * glm::vec4(center + s * size, 1.0f));
            }
            static const int f[6][4] = { { 0, 2, 3, 1 }, { 4, 5, 7, 6 }, { 0, 1, 5, 4 }, { 2, 6, 7, 3 }, { 0, 4, 6, 2 }, { 1, 3, 7, 5 } };
            for (const auto& q : f)
                for (int t : { 0, 1, 2, 0, 2, 3 })
                    out.insert(out.end(), { c[q[t]].x, c[q[t]].y, c[q[t]].z, id });
        }

        void WorldAABB(const glm::mat4& world, const glm::vec3& center, const glm::vec3& size, glm::vec3& mn, glm::vec3& mx)
        {
            mn = glm::vec3(1e30f);
            mx = glm::vec3(-1e30f);
            for (int i = 0; i < 8; ++i)
            {
                const glm::vec3 s((i & 1) ? 0.5f : -0.5f, (i & 2) ? 0.5f : -0.5f, (i & 4) ? 0.5f : -0.5f);
                const glm::vec3 p = glm::vec3(world * glm::vec4(center + s * size, 1.0f));
                mn = glm::min(mn, p);
                mx = glm::max(mx, p);
            }
        }

        std::shared_ptr<Mesh> PrimitiveMesh(MeshType t)
        {
            switch (t)
            {
                case MeshType::Sphere: return Renderer3D::GetSphereMesh();
                case MeshType::Plane: return Renderer3D::GetPlaneMesh();
                case MeshType::Cylinder: return Renderer3D::GetCylinderMesh();
                default: return Renderer3D::GetCubeMesh();
            }
        }
    }

    // ------------------------------------------------------------------ Seri hale getirme

    std::string OcclusionData::Serialize() const
    {
        std::string out;
        auto put = [&](const void* p, size_t n) { out.append((const char*)p, n); };
        auto putU = [&](uint32_t v) { put(&v, 4); };
        out.append("CQOC", 4);
        putU(1);
        put(&Origin, sizeof(glm::vec3));
        put(&CellSize, sizeof(glm::vec3));
        put(&Dims, sizeof(glm::ivec3));
        put(&BakeSeconds, 4);
        putU((uint32_t)Renderers.size());
        put(Renderers.data(), Renderers.size() * 8);
        putU((uint32_t)Portals.size());
        put(Portals.data(), Portals.size() * 8);
        putU((uint32_t)CellIndex.size());
        put(CellIndex.data(), CellIndex.size() * 4);
        putU((uint32_t)Cells.size());
        for (const auto& c : Cells)
        {
            putU((uint32_t)c.Visible.size());
            put(c.Visible.data(), c.Visible.size() * 4);
            put(c.PortalMask.data(), c.PortalMask.size() * 4);
        }
        return out;
    }

    std::shared_ptr<OcclusionData> OcclusionData::Deserialize(const std::string& data)
    {
        size_t at = 0;
        auto get = [&](void* p, size_t n) { if (at + n > data.size()) return false; std::memcpy(p, data.data() + at, n); at += n; return true; };
        auto getU = [&](uint32_t& v) { return get(&v, 4); };
        if (data.size() < 8 || data.compare(0, 4, "CQOC") != 0)
            return nullptr;
        at = 4;
        uint32_t version = 0, n = 0;
        auto d = std::make_shared<OcclusionData>();
        if (!getU(version) || version != 1 || !get(&d->Origin, sizeof(glm::vec3)) || !get(&d->CellSize, sizeof(glm::vec3)) ||
            !get(&d->Dims, sizeof(glm::ivec3)) || !get(&d->BakeSeconds, 4))
            return nullptr;
        if (!getU(n) || n > (1u << 24)) return nullptr;
        d->Renderers.resize(n);
        if (!get(d->Renderers.data(), (size_t)n * 8)) return nullptr;
        if (!getU(n) || n > 32) return nullptr;
        d->Portals.resize(n);
        if (!get(d->Portals.data(), (size_t)n * 8)) return nullptr;
        if (!getU(n) || n > (1u << 24)) return nullptr;
        d->CellIndex.resize(n);
        if (!get(d->CellIndex.data(), (size_t)n * 4)) return nullptr;
        if (!getU(n) || n > (1u << 24)) return nullptr;
        d->Cells.resize(n);
        for (auto& c : d->Cells)
        {
            uint32_t m = 0;
            if (!getU(m) || m > (1u << 24)) return nullptr;
            c.Visible.resize(m);
            c.PortalMask.resize(m);
            if (!get(c.Visible.data(), (size_t)m * 4) || !get(c.PortalMask.data(), (size_t)m * 4)) return nullptr;
        }
        return d;
    }

    // ------------------------------------------------------------------ Pişirme

    struct OcclusionBaker::Impl
    {
        Scene* SceneRef = nullptr;
        OcclusionBakeSettings Settings;
        std::shared_ptr<OcclusionData> Data;
        bool Running = false;
        std::chrono::steady_clock::time_point Start;

        GLuint DrawProgram = 0, GatherProgram = 0;
        GLuint Fbo = 0, IdTex = 0, Depth = 0, Ssbo = 0;
        GLuint Vao = 0, Vbo = 0;
        GLsizei SceneVerts = 0;
        std::vector<GLint> PortalFirst;     // portal kutularının VBO'daki başlangıcı (36 köşe)
        int Res = 64;
        uint32_t Words = 1;

        std::vector<int> CellOrder;          // pişirilecek hücreler (grid dizini)
        size_t Next = 0;
        // Ham görünürlük (hücre -> nesne bitleri; geçiş 0 açık, k kapalı portal k-1)
        std::vector<std::vector<std::vector<uint32_t>>> Raw;

        void Release()
        {
            if (DrawProgram) glDeleteProgram(DrawProgram);
            if (GatherProgram) glDeleteProgram(GatherProgram);
            if (Fbo) glDeleteFramebuffers(1, &Fbo);
            if (IdTex) glDeleteTextures(1, &IdTex);
            if (Depth) glDeleteRenderbuffers(1, &Depth);
            if (Ssbo) glDeleteBuffers(1, &Ssbo);
            if (Vbo) glDeleteBuffers(1, &Vbo);
            if (Vao) glDeleteVertexArrays(1, &Vao);
            DrawProgram = GatherProgram = Fbo = IdTex = Depth = Ssbo = Vbo = Vao = 0;
        }

        // Bir hücrenin bir geçişi: tüm örnek noktalarından 6 yön; arka yüzü çok gören (bir nesnenin içinde kalan) noktalar atılır
        std::vector<uint32_t> RenderCell(const glm::vec3& center, int pass)
        {
            const glm::vec3 cs = Data->CellSize;
            std::vector<glm::vec3> pts{ center };
            const int samples = Settings.SamplesPerCell;
            if (samples >= 5)
                for (int i = 0; i < 4; ++i)
                    pts.push_back(center + glm::vec3((i & 1) ? 0.4f : -0.4f, 0.0f, (i & 2) ? 0.4f : -0.4f) * cs);
            if (samples >= 9)
                for (int i = 0; i < 4; ++i)
                    pts.push_back(center + glm::vec3((i & 1) ? 0.4f : -0.4f, 0.4f, (i & 2) ? 0.4f : -0.4f) * cs);
            std::vector<uint32_t> result(Words, 0u), buf(Words + 1, 0u);
            static const glm::vec3 dirs[6] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
            static const glm::vec3 ups[6] = { { 0, 1, 0 }, { 0, 1, 0 }, { 0, 0, -1 }, { 0, 0, 1 }, { 0, 1, 0 }, { 0, 1, 0 } };
            const glm::mat4 proj = glm::perspective(glm::radians(90.0f), 1.0f, 0.05f, 5000.0f);
            const GLint vpLoc = glGetUniformLocation(DrawProgram, "u_VP");
            for (const glm::vec3& p : pts)
            {
                const GLuint zero = 0;
                glClearNamedBufferData(Ssbo, GL_R32UI, GL_RED_INTEGER, GL_UNSIGNED_INT, &zero);
                glBindFramebuffer(GL_FRAMEBUFFER, Fbo);
                const GLuint clearId[4] = { 0, 0, 0, 0 };
                glClearBufferuiv(GL_COLOR, 0, clearId);
                glClear(GL_DEPTH_BUFFER_BIT);
                glUseProgram(DrawProgram);
                glBindVertexArray(Vao);
                for (int f = 0; f < 6; ++f)
                {
                    glViewport(f * Res, 0, Res, Res);
                    const glm::mat4 vp = proj * glm::lookAt(p, p + dirs[f], ups[f]);
                    glUniformMatrix4fv(vpLoc, 1, GL_FALSE, &vp[0][0]);
                    glDrawArrays(GL_TRIANGLES, 0, SceneVerts);
                    if (pass > 0 && pass - 1 < (int)PortalFirst.size())
                        glDrawArrays(GL_TRIANGLES, PortalFirst[(size_t)pass - 1], 36);
                }
                glMemoryBarrier(GL_FRAMEBUFFER_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
                glUseProgram(GatherProgram);
                glBindImageTexture(0, IdTex, 0, GL_FALSE, 0, GL_READ_ONLY, GL_R32UI);
                glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, Ssbo);
                glUniform2i(glGetUniformLocation(GatherProgram, "u_Size"), 6 * Res, Res);
                glDispatchCompute((GLuint)(6 * Res + 7) / 8, (GLuint)(Res + 7) / 8, 1);
                glMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT | GL_SHADER_STORAGE_BARRIER_BIT);
                glGetNamedBufferSubData(Ssbo, 0, (GLsizeiptr)(buf.size() * 4), buf.data());
                // Pikselin yarısından çoğu arka yüzse nokta bir nesnenin içindedir: gördükleri sayılmaz
                if (buf[0] > (uint32_t)(6 * Res * Res / 2))
                    continue;
                for (uint32_t w = 0; w < Words; ++w)
                    result[w] |= buf[1 + w];
            }
            return result;
        }
    };

    OcclusionBaker::OcclusionBaker() : m(std::make_unique<Impl>()) {}
    OcclusionBaker::~OcclusionBaker() { m->Release(); }
    bool OcclusionBaker::IsRunning() const { return m->Running; }
    std::shared_ptr<OcclusionData> OcclusionBaker::Result() const { return m->Running ? nullptr : m->Data; }
    float OcclusionBaker::Progress() const
    {
        return m->CellOrder.empty() ? (m->Running ? 0.0f : 1.0f) : (float)m->Next / (float)m->CellOrder.size();
    }
    void OcclusionBaker::Cancel()
    {
        m->Running = false;
        m->Data.reset();
        m->Release();
    }

    bool OcclusionBaker::Begin(Scene& scene, const OcclusionBakeSettings& settings, std::string& error)
    {
        Cancel();
        m->SceneRef = &scene;
        m->Settings = settings;
        m->Settings.CellSize = std::max(settings.CellSize, 0.25f);
        m->Res = std::clamp(settings.Resolution, 16, 512);
        scene.UpdateWorldTransforms();
        auto& reg = scene.m_Registry;
        auto data = std::make_shared<OcclusionData>();

        // Statik nesneler (Unity Occluder / Occludee Static): hem engeller hem gizlenebilir
        std::vector<float> verts;
        glm::vec3 sceneMin(1e30f), sceneMax(-1e30f);
        for (auto e : reg.view<TransformComponent, MeshRendererComponent>())
        {
            if (!scene.IsEntityActive(e))
                continue;
            const auto* tag = reg.try_get<TagComponent>(e);
            if (!tag || !tag->IsStatic || !reg.all_of<IDComponent>(e))
                continue;
            const auto& mr = reg.get<MeshRendererComponent>(e);
            const glm::mat4& w = reg.get<TransformComponent>(e).WorldTransform;
            const float id = (float)(data->Renderers.size() + 1);
            const size_t before = verts.size();
            if (mr.IsModel())
            {
                if (mr.ModelData)
                    for (const auto& mesh : mr.ModelData->Meshes)
                        AppendMesh(mesh, w, id, verts);
            }
            else
                AppendMesh(mr.CustomMesh ? mr.CustomMesh : PrimitiveMesh(mr.Type), w, id, verts);
            if (verts.size() == before)
                continue;
            for (size_t i = before; i < verts.size(); i += 4)
            {
                const glm::vec3 p(verts[i], verts[i + 1], verts[i + 2]);
                sceneMin = glm::min(sceneMin, p);
                sceneMax = glm::max(sceneMax, p);
            }
            data->Renderers.push_back((uint64_t)reg.get<IDComponent>(e).ID);
        }
        if (data->Renderers.empty())
        {
            error = "No static objects: mark the objects that block and can be hidden as Static.";
            return false;
        }
        const GLsizei sceneVerts = (GLsizei)(verts.size() / 4);
        // Portallar (kapalı geçişte engel kutusu, kimlik 0)
        std::vector<GLint> portalFirst;
        for (auto e : reg.view<TransformComponent, OcclusionPortalComponent>())
        {
            if (data->Portals.size() >= 32 || !reg.all_of<IDComponent>(e))
                continue;
            const auto& pc = reg.get<OcclusionPortalComponent>(e);
            portalFirst.push_back((GLint)(verts.size() / 4));
            AppendBox(scene.ComputeWorldMatrix(e), pc.Center, pc.Size, 0.0f, verts);
            data->Portals.push_back((uint64_t)reg.get<IDComponent>(e).ID);
        }

        // Görünüm hacimleri: Occlusion Area'lar (yoksa statik nesnelerin kapsadığı alan)
        std::vector<std::pair<glm::vec3, glm::vec3>> volumes;
        for (auto e : reg.view<TransformComponent, OcclusionAreaComponent>())
        {
            const auto& a = reg.get<OcclusionAreaComponent>(e);
            if (!a.IsViewVolume || !scene.IsEntityActive(e))
                continue;
            glm::vec3 mn, mx;
            WorldAABB(scene.ComputeWorldMatrix(e), a.Center, a.Size, mn, mx);
            volumes.push_back({ mn, mx });
        }
        if (volumes.empty())
            volumes.push_back({ sceneMin - glm::vec3(1.0f), sceneMax + glm::vec3(1.0f) });
        glm::vec3 gmin(1e30f), gmax(-1e30f);
        for (auto& v : volumes) { gmin = glm::min(gmin, v.first); gmax = glm::max(gmax, v.second); }
        const float cell = m->Settings.CellSize;
        data->Origin = gmin;
        data->CellSize = glm::vec3(cell);
        data->Dims = glm::max(glm::ivec3(glm::ceil((gmax - gmin) / cell)), glm::ivec3(1));
        const size_t total = (size_t)data->Dims.x * data->Dims.y * data->Dims.z;
        if (total > 200000)
        {
            error = "Too many cells (" + std::to_string(total) + "): use a larger Cell Size or smaller Occlusion Areas.";
            return false;
        }
        data->CellIndex.assign(total, -1);
        m->CellOrder.clear();
        for (int z = 0; z < data->Dims.z; ++z)
            for (int y = 0; y < data->Dims.y; ++y)
                for (int x = 0; x < data->Dims.x; ++x)
                {
                    const glm::vec3 c = data->Origin + (glm::vec3(x, y, z) + 0.5f) * cell;
                    bool inside = false;
                    for (auto& v : volumes)
                        if (glm::all(glm::greaterThanEqual(c, v.first)) && glm::all(glm::lessThanEqual(c, v.second))) { inside = true; break; }
                    if (inside)
                        m->CellOrder.push_back((x + data->Dims.x * (y + data->Dims.y * z)));
                }

        // GL kaynakları
        std::string err;
        GLuint vs = CompileShader(GL_VERTEX_SHADER, kDrawVS, err);
        GLuint fs = vs ? CompileShader(GL_FRAGMENT_SHADER, kDrawFS, err) : 0;
        m->DrawProgram = fs ? LinkProgram({ vs, fs }, err) : 0;
        GLuint cs = m->DrawProgram ? CompileShader(GL_COMPUTE_SHADER, kGatherCS, err) : 0;
        m->GatherProgram = cs ? LinkProgram({ cs }, err) : 0;
        if (!m->DrawProgram || !m->GatherProgram)
        {
            error = "Occlusion bake shaders failed: " + err;
            m->Release();
            return false;
        }
        glCreateVertexArrays(1, &m->Vao);
        glCreateBuffers(1, &m->Vbo);
        glNamedBufferStorage(m->Vbo, (GLsizeiptr)(verts.size() * sizeof(float)), verts.data(), 0);
        glVertexArrayVertexBuffer(m->Vao, 0, m->Vbo, 0, 4 * sizeof(float));
        glEnableVertexArrayAttrib(m->Vao, 0);
        glVertexArrayAttribFormat(m->Vao, 0, 3, GL_FLOAT, GL_FALSE, 0);
        glVertexArrayAttribBinding(m->Vao, 0, 0);
        glEnableVertexArrayAttrib(m->Vao, 1);
        glVertexArrayAttribFormat(m->Vao, 1, 1, GL_FLOAT, GL_FALSE, 3 * sizeof(float));
        glVertexArrayAttribBinding(m->Vao, 1, 0);
        glCreateTextures(GL_TEXTURE_2D, 1, &m->IdTex);
        glTextureStorage2D(m->IdTex, 1, GL_R32UI, 6 * m->Res, m->Res);
        glCreateRenderbuffers(1, &m->Depth);
        glNamedRenderbufferStorage(m->Depth, GL_DEPTH_COMPONENT24, 6 * m->Res, m->Res);
        glCreateFramebuffers(1, &m->Fbo);
        glNamedFramebufferTexture(m->Fbo, GL_COLOR_ATTACHMENT0, m->IdTex, 0);
        glNamedFramebufferRenderbuffer(m->Fbo, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m->Depth);
        if (glCheckNamedFramebufferStatus(m->Fbo, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        {
            error = "Occlusion bake framebuffer is incomplete.";
            m->Release();
            return false;
        }
        m->Words = (uint32_t)((data->Renderers.size() + 1 + 31) / 32);
        glCreateBuffers(1, &m->Ssbo);
        glNamedBufferStorage(m->Ssbo, (GLsizeiptr)((m->Words + 1) * 4), nullptr, GL_DYNAMIC_STORAGE_BIT);
        m->SceneVerts = sceneVerts;
        m->PortalFirst = portalFirst;
        m->Raw.assign(total, {});
        m->Next = 0;
        m->Data = data;
        m->Running = true;
        m->Start = std::chrono::steady_clock::now();
        CQ_CORE_INFO("Occlusion bake: {0} static objects, {1} portals, {2} view cells ({3}x{4}x{5}, {6:.2f} m)", data->Renderers.size(),
                     data->Portals.size(), m->CellOrder.size(), data->Dims.x, data->Dims.y, data->Dims.z, cell);
        return true;
    }

    bool OcclusionBaker::Step(float budgetMs)
    {
        if (!m->Running)
            return true;
        // Düzenleyicinin GL durumunu sakla / geri yükle
        GLint prevFbo = 0, prevProgram = 0, prevVao = 0, viewport[4] = {};
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
        glGetIntegerv(GL_CURRENT_PROGRAM, &prevProgram);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVao);
        glGetIntegerv(GL_VIEWPORT, viewport);
        const GLboolean depthTest = glIsEnabled(GL_DEPTH_TEST), cull = glIsEnabled(GL_CULL_FACE), blend = glIsEnabled(GL_BLEND);
        GLboolean depthMask = GL_TRUE;
        glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glDepthMask(GL_TRUE);
        glDisable(GL_CULL_FACE);   // ince duvarlar iki yandan da engellesin
        glDisable(GL_BLEND);

        const auto t0 = std::chrono::steady_clock::now();
        const int passes = 1 + (int)m->PortalFirst.size();
        auto& d = *m->Data;
        while (m->Next < m->CellOrder.size())
        {
            const int gi = m->CellOrder[m->Next];
            const int x = gi % d.Dims.x, y = (gi / d.Dims.x) % d.Dims.y, z = gi / (d.Dims.x * d.Dims.y);
            const glm::vec3 center = d.Origin + (glm::vec3(x, y, z) + 0.5f) * d.CellSize;
            auto& raw = m->Raw[(size_t)gi];
            raw.resize((size_t)passes);
            for (int p = 0; p < passes; ++p)
                raw[(size_t)p] = m->RenderCell(center, p);
            ++m->Next;
            if (std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - t0).count() > budgetMs)
                break;
        }

        glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)prevFbo);
        glUseProgram((GLuint)prevProgram);
        glBindVertexArray((GLuint)prevVao);
        glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
        if (depthTest) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
        if (cull) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
        if (blend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
        glDepthMask(depthMask);

        if (m->Next < m->CellOrder.size())
            return false;

        // Son: komşu hücrelerle birleştir (hücre içinde dolaşırken nesne atlamasın), portal bağımlılıkları
        const uint32_t n = (uint32_t)d.Renderers.size();
        auto test = [](const std::vector<uint32_t>& bits, uint32_t id) { return (bits[id >> 5] >> (id & 31)) & 1u; };
        for (int gi : m->CellOrder)
        {
            const int x = gi % d.Dims.x, y = (gi / d.Dims.x) % d.Dims.y, z = gi / (d.Dims.x * d.Dims.y);
            std::vector<uint32_t> open(m->Words, 0u);
            std::vector<uint32_t> mask((size_t)n + 1, 0xFFFFFFFFu);   // kapanınca gizleyen portallar (komşularla AND)
            std::vector<uint8_t> seen((size_t)n + 1, 0);
            const int nb[7][3] = { { 0, 0, 0 }, { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
            for (const auto& o : nb)
            {
                const int nx = x + o[0], ny = y + o[1], nz = z + o[2];
                if (nx < 0 || ny < 0 || nz < 0 || nx >= d.Dims.x || ny >= d.Dims.y || nz >= d.Dims.z)
                    continue;
                const auto& raw = m->Raw[(size_t)(nx + d.Dims.x * (ny + d.Dims.y * nz))];
                if (raw.empty())
                    continue;
                for (uint32_t id = 1; id <= n; ++id)
                {
                    if (!test(raw[0], id))
                        continue;
                    uint32_t pm = 0;
                    for (size_t p = 1; p < raw.size(); ++p)
                        if (!test(raw[p], id))
                            pm |= 1u << (p - 1);
                    mask[id] = seen[id] ? (mask[id] & pm) : pm;
                    seen[id] = 1;
                }
            }
            OcclusionData::Cell cellOut;
            for (uint32_t id = 1; id <= n; ++id)
                if (seen[id])
                {
                    cellOut.Visible.push_back(id - 1);
                    cellOut.PortalMask.push_back(mask[id]);
                }
            d.CellIndex[(size_t)gi] = (int32_t)d.Cells.size();
            d.Cells.push_back(std::move(cellOut));
        }
        m->Raw.clear();
        d.BakeSeconds = std::chrono::duration<float>(std::chrono::steady_clock::now() - m->Start).count();
        m->Running = false;
        m->Release();
        CQ_CORE_INFO("Occlusion bake done in {0:.1f} s: {1} cells", d.BakeSeconds, d.Cells.size());
        return true;
    }

    // ------------------------------------------------------------------ Çalışma zamanı

    namespace OcclusionCulling
    {
        void ComputeHidden(Scene& scene, const glm::vec3& cam, std::unordered_set<uint32_t>& hidden)
        {
            auto data = scene.GetOcclusionData();
            if (!data || data->Cells.empty())
                return;
            const glm::ivec3 c = glm::ivec3(glm::floor((cam - data->Origin) / data->CellSize));
            if (glm::any(glm::lessThan(c, glm::ivec3(0))) || glm::any(glm::greaterThanEqual(c, data->Dims)))
                return;
            const int32_t ci = data->CellIndex[(size_t)(c.x + data->Dims.x * (c.y + data->Dims.y * c.z))];
            if (ci < 0)
                return;
            uint32_t closed = 0;
            for (size_t p = 0; p < data->Portals.size(); ++p)
            {
                Entity pe = scene.FindEntityByUUID(data->Portals[p]);
                if (pe && pe.HasComponent<OcclusionPortalComponent>() && !pe.GetComponent<OcclusionPortalComponent>().Open)
                    closed |= 1u << p;
            }
            std::vector<uint8_t> visible(data->Renderers.size(), 0);
            const auto& cell = data->Cells[(size_t)ci];
            for (size_t i = 0; i < cell.Visible.size(); ++i)
                if ((cell.PortalMask[i] & closed) == 0 && cell.Visible[i] < visible.size())
                    visible[cell.Visible[i]] = 1;
            for (size_t r = 0; r < data->Renderers.size(); ++r)
                if (!visible[r])
                    if (Entity e = scene.FindEntityByUUID(data->Renderers[r]))
                        hidden.insert((uint32_t)(entt::entity)e);
        }

        void DrawGizmo(Scene& scene, uint32_t entity)
        {
            const entt::entity e = (entt::entity)entity;
            auto& reg = scene.m_Registry;
            if (!reg.valid(e))
                return;
            auto box = [&](const glm::vec3& center, const glm::vec3& size, const glm::vec4& col) {
                const glm::mat4 w = scene.ComputeWorldMatrix(e);
                glm::vec3 c[8];
                for (int i = 0; i < 8; ++i)
                {
                    const glm::vec3 s((i & 1) ? 0.5f : -0.5f, (i & 2) ? 0.5f : -0.5f, (i & 4) ? 0.5f : -0.5f);
                    c[i] = glm::vec3(w * glm::vec4(center + s * size, 1.0f));
                }
                static const int edges[12][2] = { { 0, 1 }, { 2, 3 }, { 4, 5 }, { 6, 7 }, { 0, 2 }, { 1, 3 }, { 4, 6 }, { 5, 7 }, { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 } };
                for (const auto& ed : edges)
                    DebugDraw::Line(c[ed[0]], c[ed[1]], col, 0.0f, false);
            };
            if (const auto* a = reg.try_get<OcclusionAreaComponent>(e))
                box(a->Center, a->Size, glm::vec4(0.35f, 0.85f, 1.0f, 1.0f));
            if (const auto* p = reg.try_get<OcclusionPortalComponent>(e))
                box(p->Center, p->Size, p->Open ? glm::vec4(0.4f, 1.0f, 0.4f, 1.0f) : glm::vec4(1.0f, 0.35f, 0.3f, 1.0f));
        }

        void DrawCells(Scene& scene, const glm::vec3& cam)
        {
            auto data = scene.GetOcclusionData();
            if (!data || data->Cells.empty())
                return;
            const glm::ivec3 c = glm::ivec3(glm::floor((cam - data->Origin) / data->CellSize));
            const glm::vec3 lo = data->Origin, hi = data->Origin + glm::vec3(data->Dims) * data->CellSize;
            DebugDraw::Box((lo + hi) * 0.5f, hi - lo, glm::vec4(0.35f, 0.85f, 1.0f, 0.6f), 0.0f, false);
            if (glm::all(glm::greaterThanEqual(c, glm::ivec3(0))) && glm::all(glm::lessThan(c, data->Dims)))
            {
                const glm::vec3 mn = data->Origin + glm::vec3(c) * data->CellSize;
                DebugDraw::Box(mn + data->CellSize * 0.5f, data->CellSize, glm::vec4(1.0f, 0.85f, 0.3f, 1.0f), 0.0f, false);
            }
        }
    }

    template<> void Scene::OnComponentAdded<OcclusionAreaComponent>(Entity, OcclusionAreaComponent&) {}
    template<> void Scene::OnComponentAdded<OcclusionPortalComponent>(Entity, OcclusionPortalComponent&) {}

    void RegisterOcclusionComponents()
    {
        const glm::vec4 kColor{ 0.35f, 0.85f, 1.0f, 1.0f };
        {
            using C = OcclusionAreaComponent;
            ComponentDef<C>("Occlusion Area", "OcclusionAreaComponent", "Rendering", ICON_FA_BORDER_ALL).Color(kColor)
                .Describe("Where the camera can go: Occlusion Culling bakes view cells inside this box (without any area, around all static objects). "
                          "Bake in Window > Occlusion Culling.")
                .Field("Center", "Center", &C::Center).Speed(0.05f)
                .Field("Size", "Size", &C::Size).Speed(0.05f)
                .Field("IsViewVolume", "Is View Volume", &C::IsViewVolume);
        }
        {
            using C = OcclusionPortalComponent;
            ComponentDef<C>("Occlusion Portal", "OcclusionPortalComponent", "Rendering", ICON_FA_DOOR_OPEN).Color(kColor)
                .Describe("A door for Occlusion Culling: while it is closed, static objects seen only through it are hidden. "
                          "Set Open from scripts when the door opens.")
                .Field("Open", "Open", &C::Open)
                .Field("Center", "Center", &C::Center).Speed(0.05f)
                .Field("Size", "Size", &C::Size).Speed(0.05f);
        }
    }
}
