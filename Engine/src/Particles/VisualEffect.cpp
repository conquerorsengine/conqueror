#include "VisualEffect.h"
#include "Particles/ParticleSystem.h"
#include "Scene/Scene.h"
#include "Scene/Entity.h"
#include "Scene/Components.h"
#include "Scene/ComponentRegistry.h"
#include "Core/Assets/Graph/NodeAsset.h"
#include "Core/Serialization/SceneSerializer.h"
#include "Core/Logging/Log.h"
#include "Renderer/RHI/Texture.h"

#include <glad/glad.h>
#include <IconsFontAwesome6.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <sstream>

namespace Conqueror
{
    namespace
    {
        const char* kInitCS = R"(#version 450 core
layout(local_size_x = 64) in;
struct P { vec4 PosAge; vec4 VelLife; vec4 Color; vec4 Misc; };
layout(std430, binding = 0) buffer Parts { P parts[]; };
uniform uint u_Head, u_Count, u_Capacity, u_Seed;
uniform mat4 u_World;
uniform int u_Shape, u_VelMode;
uniform vec3 u_ShapeSize, u_Dir;
uniform float u_ConeAngle, u_ColorVar;
uniform vec2 u_Speed, u_Life, u_Size;
uniform vec4 u_Color;
uint hash(uint x) { x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16; return x; }
float rnd(inout uint s) { s = hash(s); return float(s & 0xFFFFFFu) / 16777216.0; }
void main()
{
    uint i = gl_GlobalInvocationID.x;
    if (i >= u_Count) return;
    uint idx = (u_Head + i) % u_Capacity;
    uint s = hash(u_Seed * 9781u + i * 6271u + 17u);
    vec3 p = vec3(0.0), n = vec3(0.0, 1.0, 0.0);
    if (u_Shape == 1)
    {
        vec3 d = normalize(vec3(rnd(s) * 2.0 - 1.0, rnd(s) * 2.0 - 1.0, rnd(s) * 2.0 - 1.0) + 1e-5);
        p = d * pow(rnd(s), 1.0 / 3.0) * u_ShapeSize.x;
        n = d;
    }
    else if (u_Shape == 2)
    {
        p = (vec3(rnd(s), rnd(s), rnd(s)) - 0.5) * u_ShapeSize;
        n = normalize(p + vec3(0.0, 1e-5, 0.0));
    }
    else if (u_Shape == 3)
    {
        float a = rnd(s) * 6.2831853;
        float z = mix(cos(radians(u_ConeAngle)), 1.0, rnd(s));
        float sr = sqrt(max(1.0 - z * z, 0.0));
        n = vec3(cos(a) * sr, z, sin(a) * sr);
        float br = sqrt(rnd(s)) * u_ShapeSize.x;
        p = vec3(cos(a) * br, 0.0, sin(a) * br);
    }
    else if (u_Shape == 4)
    {
        float a = rnd(s) * 6.2831853;
        float r = sqrt(rnd(s)) * u_ShapeSize.x;
        p = vec3(cos(a) * r, 0.0, sin(a) * r);
        n = normalize(vec3(p.x, 1e-5, p.z));
    }
    vec3 dir;
    if (u_VelMode == 0) dir = normalize(u_Dir + vec3(0.0, 1e-6, 0.0));
    else if (u_VelMode == 1) dir = n;
    else dir = normalize(vec3(rnd(s) * 2.0 - 1.0, rnd(s) * 2.0 - 1.0, rnd(s) * 2.0 - 1.0) + 1e-5);
    float speed = mix(u_Speed.x, u_Speed.y, rnd(s));
    vec3 wp = (u_World * vec4(p, 1.0)).xyz;
    vec3 wd = mat3(u_World) * dir;
    vec3 wv = (length(wd) > 1e-6 ? normalize(wd) : vec3(0.0)) * speed;
    float life = max(mix(u_Life.x, u_Life.y, rnd(s)), 0.01);
    float size = mix(u_Size.x, u_Size.y, rnd(s));
    vec4 col = u_Color;
    col.rgb = max(col.rgb * (1.0 + (rnd(s) * 2.0 - 1.0) * u_ColorVar), vec3(0.0));
    parts[idx].PosAge = vec4(wp, 0.0);
    parts[idx].VelLife = vec4(wv, life);
    parts[idx].Color = col;
    parts[idx].Misc = vec4(size, rnd(s) * 6.2831853, rnd(s), 1.0);
}
)";
        const char* kUpdateCS = R"(#version 450 core
layout(local_size_x = 64) in;
struct P { vec4 PosAge; vec4 VelLife; vec4 Color; vec4 Misc; };
layout(std430, binding = 0) buffer Parts { P parts[]; };
layout(std430, binding = 1) buffer Inst { vec4 inst[]; };
uniform uint u_Capacity;
uniform float u_Dt, u_Time, u_Drag, u_Turb, u_TurbFreq, u_GroundY, u_Bounce, u_SizeEnd;
uniform vec3 u_Gravity;
uniform int u_Ground;
uniform vec4 u_ColorEnd;
void main()
{
    uint i = gl_GlobalInvocationID.x;
    if (i >= u_Capacity) return;
    P q = parts[i];
    if (q.Misc.w < 0.5)
    {
        inst[i * 4u] = vec4(0.0);
        return;
    }
    q.PosAge.w += u_Dt;
    if (q.PosAge.w >= q.VelLife.w)
    {
        q.Misc.w = 0.0;
        parts[i] = q;
        inst[i * 4u] = vec4(0.0);
        return;
    }
    vec3 v = q.VelLife.xyz + u_Gravity * u_Dt;
    v *= max(1.0 - u_Drag * u_Dt, 0.0);
    if (u_Turb > 0.0)
    {
        vec3 x = q.PosAge.xyz * u_TurbFreq;
        float t = u_Time * 0.7 + q.Misc.z * 10.0;
        v += vec3(sin(x.y * 1.7 + t) + sin(x.z * 2.3 - t), sin(x.z * 1.9 + t * 1.3) + sin(x.x * 2.1 + t), sin(x.x * 1.6 - t * 0.9) + sin(x.y * 2.2 + t)) * 0.5 * u_Turb * u_Dt;
    }
    vec3 p = q.PosAge.xyz + v * u_Dt;
    if (u_Ground == 1 && p.y < u_GroundY)
    {
        p.y = u_GroundY;
        if (v.y < 0.0) v.y = -v.y * u_Bounce;
        v.xz *= 0.9;
    }
    q.PosAge.xyz = p;
    q.VelLife.xyz = v;
    parts[i] = q;
    float t = q.PosAge.w / q.VelLife.w;
    vec4 col = q.Color * mix(vec4(1.0), u_ColorEnd, t);
    float size = q.Misc.x * mix(1.0, u_SizeEnd, t);
    inst[i * 4u + 0u] = vec4(p, size);
    inst[i * 4u + 1u] = col;
    inst[i * 4u + 2u] = vec4(q.Misc.y, 0.0, length(v), 0.0);
    inst[i * 4u + 3u] = vec4(v, 0.0);
}
)";

        GLuint s_InitProgram = 0, s_UpdateProgram = 0;
        bool s_ProgramsTried = false;

        GLuint BuildCompute(const char* src)
        {
            GLuint sh = glCreateShader(GL_COMPUTE_SHADER);
            glShaderSource(sh, 1, &src, nullptr);
            glCompileShader(sh);
            GLint ok = 0;
            glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
            if (!ok)
            {
                char log[1024] = {};
                glGetShaderInfoLog(sh, sizeof(log), nullptr, log);
                CQ_CORE_ERROR("[VFX] compute shader: {0}", log);
                glDeleteShader(sh);
                return 0;
            }
            GLuint p = glCreateProgram();
            glAttachShader(p, sh);
            glLinkProgram(p);
            glDeleteShader(sh);
            glGetProgramiv(p, GL_LINK_STATUS, &ok);
            if (!ok)
            {
                glDeleteProgram(p);
                return 0;
            }
            return p;
        }

        bool Programs()
        {
            if (!s_ProgramsTried)
            {
                s_ProgramsTried = true;
                s_InitProgram = BuildCompute(kInitCS);
                s_UpdateProgram = BuildCompute(kUpdateCS);
            }
            return s_InitProgram && s_UpdateProgram;
        }

        glm::vec3 Vec3(const std::string& s, const glm::vec3& def)
        {
            std::istringstream in(s);
            glm::vec3 v = def;
            in >> v.x >> v.y >> v.z;
            return in.fail() && !in.eof() ? def : v;
        }
        glm::vec4 Vec4(const std::string& s, const glm::vec4& def)
        {
            std::istringstream in(s);
            glm::vec4 v = def;
            in >> v.x >> v.y >> v.z;
            if (!(in >> v.w)) v.w = def.w;
            return v;
        }
        int ChoiceIndex(const std::string& v, std::initializer_list<const char*> options)
        {
            int i = 0;
            for (const char* o : options) { if (v == o) return i; ++i; }
            return 0;
        }

        struct SystemDesc
        {
            // Spawn
            float Rate = 50.0f;
            int BurstCount = 0;
            float BurstDelay = 0.0f, BurstRepeat = 0.0f;
            // Initialize
            uint32_t Capacity = 2000;
            glm::vec2 Life{ 1.0f, 2.0f }, Speed{ 1.0f, 3.0f }, Size{ 0.1f, 0.2f };
            int Shape = 0, VelMode = 0;
            glm::vec3 ShapeSize{ 1.0f }, Dir{ 0.0f, 1.0f, 0.0f };
            float ConeAngle = 25.0f, ColorVar = 0.0f, Bounds = 10.0f;
            glm::vec4 Color{ 1.0f };
            // Update
            glm::vec3 Gravity{ 0.0f, -9.81f, 0.0f };
            float Drag = 0.0f, Turb = 0.0f, TurbFreq = 1.0f, GroundY = 0.0f, Bounce = 0.3f;
            bool Ground = false;
            // Output
            int Blend = 1, RenderMode = 0;
            std::string Texture;
            glm::vec4 ColorEnd{ 1.0f, 1.0f, 1.0f, 0.0f };
            float SizeEnd = 1.0f, Stretch = 0.1f, Emissive = 1.0f;
        };

        struct SystemRT
        {
            SystemDesc D;
            GLuint Parts = 0, Inst = 0;
            uint32_t Head = 0;
            double SpawnAccum = 0.0;
            float Time = 0.0f, BurstTimer = 0.0f;
            int BurstsDone = 0;
            uint32_t Frame = 0;
            uint64_t Spawned = 0;
            std::shared_ptr<Texture2D> Tex;
            void Free()
            {
                if (Parts) glDeleteBuffers(1, &Parts);
                if (Inst) glDeleteBuffers(1, &Inst);
                Parts = Inst = 0;
            }
        };

        struct EffectRT
        {
            std::string Asset, Text;
            std::vector<SystemRT> Systems;
            uint64_t LastFrame = 0;
            void Free() { for (auto& s : Systems) s.Free(); Systems.clear(); }
        };

        std::map<std::pair<const Scene*, uint32_t>, EffectRT>& Effects()
        {
            static auto& m = *new std::map<std::pair<const Scene*, uint32_t>, EffectRT>();
            return m;
        }
        uint64_t s_Frame = 0;

        std::vector<SystemDesc> Parse(const NodeAssets::File& f)
        {
            std::vector<SystemDesc> out;
            for (const auto& n : f.Nodes)
            {
                if (n.Type != "initialize")
                    continue;
                SystemDesc d;
                d.Capacity = (uint32_t)std::clamp(n.Int("Capacity", 2000), 1, 4000000);
                d.Life = { n.Float("Lifetime Min", 1.0f), n.Float("Lifetime Max", 2.0f) };
                d.Shape = ChoiceIndex(n.Str("Shape", "Point"), { "Point", "Sphere", "Box", "Cone", "Circle" });
                d.ShapeSize = Vec3(n.Str("Shape Size", "1 1 1"), glm::vec3(1.0f));
                d.ConeAngle = n.Float("Cone Angle", 25.0f);
                d.VelMode = ChoiceIndex(n.Str("Velocity", "Direction"), { "Direction", "From Shape", "Random" });
                d.Dir = Vec3(n.Str("Direction", "0 1 0"), glm::vec3(0, 1, 0));
                d.Speed = { n.Float("Speed Min", 1.0f), n.Float("Speed Max", 3.0f) };
                d.Size = { n.Float("Size Min", 0.1f), n.Float("Size Max", 0.2f) };
                d.Color = Vec4(n.Str("Color", "1 1 1 1"), glm::vec4(1.0f));
                d.ColorVar = n.Float("Color Variation", 0.0f);
                d.Bounds = n.Float("Bounds Size", 10.0f);
                for (const auto* l : f.In(n.Id))
                    if (const auto* sp = f.Find(l->From); sp && sp->Type == "spawn")
                    {
                        d.Rate = sp->Float("Rate", 50.0f);
                        d.BurstCount = sp->Int("Burst Count", 0);
                        d.BurstDelay = sp->Float("Burst Delay", 0.0f);
                        d.BurstRepeat = sp->Float("Burst Repeat", 0.0f);
                    }
                // Initialize -> (Update) -> Output
                const NodeAssets::Node* next = nullptr;
                for (const auto* l : f.Out(n.Id))
                    if ((next = f.Find(l->To)))
                        break;
                if (next && next->Type == "update")
                {
                    d.Gravity = Vec3(next->Str("Gravity", "0 -9.81 0"), glm::vec3(0, -9.81f, 0));
                    d.Drag = next->Float("Drag", 0.0f);
                    d.Turb = next->Float("Turbulence", 0.0f);
                    d.TurbFreq = next->Float("Turbulence Frequency", 1.0f);
                    d.Ground = next->Bool("Ground Collision", false);
                    d.GroundY = next->Float("Ground Height", 0.0f);
                    d.Bounce = next->Float("Bounce", 0.3f);
                    const NodeAssets::Node* o = nullptr;
                    for (const auto* l : f.Out(next->Id))
                        if ((o = f.Find(l->To)))
                            break;
                    next = o;
                }
                else
                    d.Gravity = glm::vec3(0.0f);
                if (next && next->Type == "output")
                {
                    d.Blend = ChoiceIndex(next->Str("Blend", "Additive"), { "Alpha", "Additive", "Premultiplied" });
                    d.RenderMode = ChoiceIndex(next->Str("Render", "Billboard"), { "Billboard", "Stretched Billboard" });
                    d.Texture = next->Str("Texture");
                    d.ColorEnd = Vec4(next->Str("Color At End", "1 1 1 0"), glm::vec4(1, 1, 1, 0));
                    d.SizeEnd = next->Float("Size At End", 1.0f);
                    d.Stretch = next->Float("Stretch", 0.1f);
                    d.Emissive = next->Float("Emissive", 1.0f);
                }
                out.push_back(d);
            }
            return out;
        }

        void Allocate(SystemRT& s)
        {
            s.Free();
            glCreateBuffers(1, &s.Parts);
            glNamedBufferStorage(s.Parts, (GLsizeiptr)s.D.Capacity * 64, nullptr, GL_DYNAMIC_STORAGE_BIT);
            glCreateBuffers(1, &s.Inst);
            glNamedBufferStorage(s.Inst, (GLsizeiptr)s.D.Capacity * 64, nullptr, GL_DYNAMIC_STORAGE_BIT);
            const float zero = 0.0f;
            glClearNamedBufferData(s.Parts, GL_R32F, GL_RED, GL_FLOAT, &zero);
            glClearNamedBufferData(s.Inst, GL_R32F, GL_RED, GL_FLOAT, &zero);
            s.Head = 0;
            s.SpawnAccum = 0.0;
            s.Time = s.BurstTimer = 0.0f;
            s.BurstsDone = 0;
            s.Spawned = 0;
            s.Tex = s.D.Texture.empty() ? nullptr : Texture2D::Create(SceneSerializer::ResolveAssetPath(s.D.Texture));
        }
    }

    namespace VisualEffects
    {
        void Reinit(Scene& scene, entt::entity entity)
        {
            auto it = Effects().find({ &scene, (uint32_t)entity });
            if (it != Effects().end())
                for (auto& s : it->second.Systems)
                    Allocate(s);
        }

        uint64_t SpawnedTotal(Scene& scene, entt::entity entity)
        {
            auto it = Effects().find({ &scene, (uint32_t)entity });
            uint64_t total = 0;
            if (it != Effects().end())
                for (auto& s : it->second.Systems)
                    total += s.Spawned;
            return total;
        }

        void Update(Scene& scene, float dt)
        {
            ++s_Frame;
            auto view = scene.m_Registry.view<TransformComponent, VisualEffectComponent>();
            if (view.begin() != view.end() && !Programs())
            {
                static bool s_Warned = false;
                if (!s_Warned) { s_Warned = true; CQ_CORE_ERROR("[VFX] GPU particle programs are unavailable (compute shaders)."); }
            }
            if (view.begin() != view.end() && Programs())
            {
                for (auto e : view)
                {
                    if (!scene.IsEntityActive(e))
                        continue;
                    auto& vc = view.get<VisualEffectComponent>(e);
                    auto& rt = Effects()[{ &scene, (uint32_t)e }];
                    rt.LastFrame = s_Frame;
                    NodeAssets::File file;
                    const bool loaded = !vc.Asset.empty() && NodeAssets::Load(vc.Asset, file) && file.Kind == Kind;
                    const std::string text = loaded ? file.ToText() : std::string();
                    if (vc.Asset != rt.Asset || text != rt.Text)
                    {
                        rt.Free();
                        rt.Asset = vc.Asset;
                        rt.Text = text;
                        if (loaded)
                            for (const auto& d : Parse(file))
                            {
                                SystemRT s;
                                s.D = d;
                                Allocate(s);
                                rt.Systems.push_back(std::move(s));
                            }
                        CQ_CORE_INFO("[VFX] '{0}': {1} ({2} systems)", vc.Asset, loaded ? "loaded" : "not loaded", rt.Systems.size());
                        vc.Systems = (int)rt.Systems.size();
                        vc.Capacity = 0;
                        for (auto& s : rt.Systems) vc.Capacity += (int)s.D.Capacity;
                    }
                    const float sdt = std::max(dt * std::max(vc.PlayRate, 0.0f), 0.0f);
                    const glm::mat4 world = scene.ComputeWorldMatrix(e);
                    for (auto& s : rt.Systems)
                    {
                        s.Time += sdt;
                        // Doğurma: sabit oran + patlamalar
                        uint32_t n = 0;
                        if (vc.Playing)
                        {
                            s.SpawnAccum += (double)s.D.Rate * sdt;
                            const double whole = std::floor(s.SpawnAccum);
                            s.SpawnAccum -= whole;
                            n = (uint32_t)std::min(whole, (double)s.D.Capacity);
                            if (s.D.BurstCount > 0)
                            {
                                s.BurstTimer += sdt;
                                const bool due = s.BurstsDone == 0 ? s.BurstTimer >= s.D.BurstDelay
                                                                   : (s.D.BurstRepeat > 0.0f && s.BurstTimer >= s.D.BurstDelay + s.D.BurstRepeat * (float)s.BurstsDone);
                                if (due)
                                {
                                    n = std::min<uint32_t>(n + (uint32_t)s.D.BurstCount, s.D.Capacity);
                                    ++s.BurstsDone;
                                }
                            }
                        }
                        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, s.Parts);
                        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, s.Inst);
                        if (n > 0)
                        {
                            glUseProgram(s_InitProgram);
                            auto U = [](const char* name) { return glGetUniformLocation(s_InitProgram, name); };
                            glUniform1ui(U("u_Head"), s.Head);
                            glUniform1ui(U("u_Count"), n);
                            glUniform1ui(U("u_Capacity"), s.D.Capacity);
                            glUniform1ui(U("u_Seed"), (GLuint)(vc.Seed * 7919 + (int)s.Frame * 104729 + 1));
                            glUniformMatrix4fv(U("u_World"), 1, GL_FALSE, &world[0][0]);
                            glUniform1i(U("u_Shape"), s.D.Shape);
                            glUniform1i(U("u_VelMode"), s.D.VelMode);
                            glUniform3fv(U("u_ShapeSize"), 1, &s.D.ShapeSize.x);
                            glUniform3fv(U("u_Dir"), 1, &s.D.Dir.x);
                            glUniform1f(U("u_ConeAngle"), s.D.ConeAngle);
                            glUniform1f(U("u_ColorVar"), s.D.ColorVar);
                            glUniform2fv(U("u_Speed"), 1, &s.D.Speed.x);
                            glUniform2fv(U("u_Life"), 1, &s.D.Life.x);
                            glUniform2fv(U("u_Size"), 1, &s.D.Size.x);
                            glUniform4fv(U("u_Color"), 1, &s.D.Color.x);
                            glDispatchCompute((n + 63) / 64, 1, 1);
                            glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
                            s.Head = (s.Head + n) % s.D.Capacity;
                            s.Spawned += n;
                        }
                        glUseProgram(s_UpdateProgram);
                        auto U = [](const char* name) { return glGetUniformLocation(s_UpdateProgram, name); };
                        glUniform1ui(U("u_Capacity"), s.D.Capacity);
                        glUniform1f(U("u_Dt"), sdt);
                        glUniform1f(U("u_Time"), s.Time);
                        glUniform1f(U("u_Drag"), s.D.Drag);
                        glUniform1f(U("u_Turb"), s.D.Turb);
                        glUniform1f(U("u_TurbFreq"), s.D.TurbFreq);
                        glUniform1f(U("u_GroundY"), s.D.GroundY);
                        glUniform1f(U("u_Bounce"), s.D.Bounce);
                        glUniform1f(U("u_SizeEnd"), s.D.SizeEnd);
                        glUniform3fv(U("u_Gravity"), 1, &s.D.Gravity.x);
                        glUniform1i(U("u_Ground"), s.D.Ground ? 1 : 0);
                        glUniform4fv(U("u_ColorEnd"), 1, &s.D.ColorEnd.x);
                        glDispatchCompute((s.D.Capacity + 63) / 64, 1, 1);
                        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT);
                        ++s.Frame;
                    }
                }
                glUseProgram(0);
            }
            // Kullanılmayan efektler (silinen nesne, kapanan sahne): GPU tamponları bırakılır
            for (auto it = Effects().begin(); it != Effects().end();)
            {
                if (s_Frame - it->second.LastFrame > 240)
                {
                    it->second.Free();
                    it = Effects().erase(it);
                }
                else
                    ++it;
            }
        }

        void Render(Scene& scene)
        {
            auto view = scene.m_Registry.view<TransformComponent, VisualEffectComponent>();
            for (auto e : view)
            {
                if (!scene.IsEntityActive(e))
                    continue;
                auto it = Effects().find({ &scene, (uint32_t)e });
                if (it == Effects().end())
                    continue;
                const glm::vec3 c = glm::vec3(view.get<TransformComponent>(e).WorldTransform[3]);
                for (auto& s : it->second.Systems)
                {
                    ParticleSystem::GPUDraw d;
                    d.Blend = s.D.Blend == 1 ? (int)ParticleBlend::Additive : s.D.Blend == 2 ? (int)ParticleBlend::Premultiplied : (int)ParticleBlend::Alpha;
                    d.RenderMode = s.D.RenderMode == 1 ? (int)ParticleRenderMode::StretchedBillboard : (int)ParticleRenderMode::Billboard;
                    d.LengthScale = 1.0f;
                    d.SpeedScale = s.D.Stretch;
                    d.Emissive = s.D.Emissive;
                    d.Texture = s.Tex;
                    d.BoundsMin = c - glm::vec3(s.D.Bounds * 0.5f);
                    d.BoundsMax = c + glm::vec3(s.D.Bounds * 0.5f);
                    ParticleSystem::DrawInstanceBuffer(s.Inst, s.D.Capacity, d);
                }
            }
        }
    }

    template<> void Scene::OnComponentAdded<VisualEffectComponent>(Entity, VisualEffectComponent&) {}

    void RegisterVisualEffectComponents()
    {
        using C = VisualEffectComponent;
        ComponentDef<C>("Visual Effect", "VisualEffectComponent", "Effects", ICON_FA_WAND_SPARKLES).Color({ 1.0f, 0.6f, 0.3f, 1.0f })
            .Describe("Plays a VFX Graph (.cqvfx) on the GPU: hundreds of thousands of particles. Edit the graph's Spawn, Initialize, Update "
                      "and Output contexts by double-clicking the asset.")
            .Field("Asset", "Asset Template", &C::Asset, FieldKind::Asset).Filter(".cqvfx")
            .Field("Playing", "Playing", &C::Playing).Tooltip("Off: no new particles; existing ones finish their life.")
            .Field("PlayRate", "Play Rate", &C::PlayRate).Range(0.0f, 10.0f, 0.01f)
            .Field("Seed", "Seed", &C::Seed)
            .Action("Restart", [](Entity e, C&) { if (e.GetScene()) VisualEffects::Reinit(*e.GetScene(), (entt::entity)e); })
            .Field("Systems", "Systems", &C::Systems).ReadOnly().Header("Info")
            .Field("Capacity", "Capacity", &C::Capacity).ReadOnly();
    }
}
