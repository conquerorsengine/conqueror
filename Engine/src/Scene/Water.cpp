#include "Water.h"
#include "Scene/Scene.h"
#include "Scene/Entity.h"
#include "Scene/Components.h"
#include "Scene/ComponentRegistry.h"
#include "Core/Debug/DebugDraw.h"
#include "Core/Logging/Log.h"
#include "Core/Time/TimeManager.h"
#include "Core/PhysicsSystem/Physics3D/PhysicsWorld3D.h"
#include "Renderer/PostProcess.h"
#include "Renderer/RHI/Shader.h"
#include "Renderer/Utilities/Renderer3D/Renderer3D.h"

#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <IconsFontAwesome6.h>

#include <algorithm>
#include <cmath>
#include <map>

namespace Conqueror
{
    namespace
    {
        struct Wave { glm::vec2 Dir; float Length, Amplitude, Speed, Steepness; };

        // Taban dalgadan türetilen dalga takımı (Unreal Gerstner Wave Generator gibi): yön yelpazesi, kısalan boy ve genlik
        int MakeWaves(const WaterBodyComponent& w, Wave out[4])
        {
            const int n = std::clamp(w.WaveCount, 0, 4);
            static const float dirOffset[4] = { 0.0f, 27.0f, -33.0f, 61.0f };
            static const float lenScale[4] = { 1.0f, 0.61f, 0.37f, 0.23f };
            static const float ampScale[4] = { 1.0f, 0.55f, 0.3f, 0.17f };
            for (int i = 0; i < n; ++i)
            {
                const float a = glm::radians(w.WaveDirection + dirOffset[i]);
                out[i] = { glm::vec2(std::cos(a), std::sin(a)), std::max(w.WaveLength * lenScale[i], 0.05f), w.WaveAmplitude * ampScale[i],
                           w.WaveSpeed * std::sqrt(lenScale[i]), w.Steepness };
            }
            return n;
        }

        // Gerstner yer değiştirmesi (shader ile aynı)
        glm::vec3 Displace(const Wave* waves, int n, const glm::vec2& base, float t)
        {
            glm::vec3 d(0.0f);
            for (int i = 0; i < n; ++i)
            {
                const float k = 6.2831853f / waves[i].Length;
                const float a = waves[i].Amplitude;
                const float q = std::clamp(waves[i].Steepness, 0.0f, 1.0f) / std::max(k * a * (float)n, 1e-4f);
                const float f = k * (glm::dot(waves[i].Dir, base) - waves[i].Speed * t);
                d.x += q * a * waves[i].Dir.x * std::cos(f);
                d.z += q * a * waves[i].Dir.y * std::cos(f);
                d.y += a * std::sin(f);
            }
            return d;
        }

        struct Grid { GLuint Vao = 0, Vbo = 0, Ibo = 0; GLsizei Count = 0; };
        Grid& GridOf(int res)
        {
            static auto& grids = *new std::map<int, Grid>();
            Grid& g = grids[res];
            if (g.Vao)
                return g;
            std::vector<float> v;
            std::vector<uint32_t> idx;
            for (int y = 0; y <= res; ++y)
                for (int x = 0; x <= res; ++x)
                {
                    v.push_back((float)x / (float)res - 0.5f);
                    v.push_back((float)y / (float)res - 0.5f);
                }
            for (int y = 0; y < res; ++y)
                for (int x = 0; x < res; ++x)
                {
                    const uint32_t i0 = (uint32_t)(y * (res + 1) + x), i1 = i0 + 1, i2 = i0 + (uint32_t)res + 1, i3 = i2 + 1;
                    idx.insert(idx.end(), { i0, i2, i1, i1, i2, i3 });
                }
            glCreateVertexArrays(1, &g.Vao);
            glCreateBuffers(1, &g.Vbo);
            glNamedBufferStorage(g.Vbo, (GLsizeiptr)(v.size() * sizeof(float)), v.data(), 0);
            glCreateBuffers(1, &g.Ibo);
            glNamedBufferStorage(g.Ibo, (GLsizeiptr)(idx.size() * sizeof(uint32_t)), idx.data(), 0);
            glVertexArrayVertexBuffer(g.Vao, 0, g.Vbo, 0, 2 * sizeof(float));
            glEnableVertexArrayAttrib(g.Vao, 0);
            glVertexArrayAttribFormat(g.Vao, 0, 2, GL_FLOAT, GL_FALSE, 0);
            glVertexArrayAttribBinding(g.Vao, 0, 0);
            glVertexArrayElementBuffer(g.Vao, g.Ibo);
            g.Count = (GLsizei)idx.size();
            return g;
        }

        // Okyanus ızgarası kamerayı yatayda izler (hücre boyuna kenetli: dalgalar kaymaz)
        glm::vec2 OceanOffset(const WaterBodyComponent& w, const glm::mat4& world, const glm::vec3& cam)
        {
            if (w.Type != WaterBodyType::Ocean)
                return glm::vec2(0.0f);
            const glm::vec3 l = glm::vec3(glm::inverse(world) * glm::vec4(cam, 1.0f));
            const int res = std::clamp(w.Resolution, 8, 1024);
            const glm::vec2 cell = w.Size / (float)res;
            return glm::floor(glm::vec2(l.x, l.z) / cell) * cell;
        }
    }

    namespace WaterBodies
    {
        float WaterHeight(Scene& scene, const glm::vec3& p, float t, bool& found)
        {
            found = false;
            float best = -1e30f;
            for (auto e : scene.m_Registry.view<TransformComponent, WaterBodyComponent>())
            {
                if (!scene.IsEntityActive(e))
                    continue;
                const auto& w = scene.m_Registry.get<WaterBodyComponent>(e);
                const glm::mat4 world = scene.ComputeWorldMatrix(e);
                const glm::vec3 l = glm::vec3(glm::inverse(world) * glm::vec4(p, 1.0f));
                // Yerel boyun dışında (okyanus hariç) su yok
                const glm::vec3 scale(glm::length(glm::vec3(world[0])), 1.0f, glm::length(glm::vec3(world[2])));
                (void)scale;
                if (w.Type != WaterBodyType::Ocean && (std::abs(l.x) > w.Size.x * 0.5f || std::abs(l.z) > w.Size.y * 0.5f))
                    continue;
                Wave waves[4];
                const int n = MakeWaves(w, waves);
                const float surfaceY = world[3][1];
                // Gerstner yatay kaymasını iki adımda geri çöz: bu noktanın üstüne gelen taban noktası
                glm::vec2 base(p.x, p.z);
                for (int it = 0; it < 2; ++it)
                {
                    const glm::vec3 d = Displace(waves, n, base, t);
                    base = glm::vec2(p.x, p.z) - glm::vec2(d.x, d.z);
                }
                const float h = surfaceY + Displace(waves, n, base, t).y;
                if (h > best)
                {
                    best = h;
                    found = true;
                }
            }
            return best;
        }

        void FixedUpdate(Scene& scene, float dt)
        {
            PhysicsWorld3D* physics = scene.GetPhysicsWorld3D();
            if (!physics)
                return;
            auto& reg = scene.m_Registry;
            const float t = TimeManager::GetTime();
            for (auto e : reg.view<TransformComponent, BuoyancyComponent, RigidbodyComponent>())
            {
                if (!scene.IsEntityActive(e) || reg.get<RigidbodyComponent>(e).Type != RigidbodyComponent::BodyType::Dynamic)
                    continue;
                auto& b = reg.get<BuoyancyComponent>(e);
                Entity ent{ e, &scene };
                const glm::mat4 w = scene.ComputeWorldMatrix(e);
                const float mass = physics->GetMass(ent);
                const size_t n = std::max<size_t>(b.Pontoons.size(), 1);
                const float r = std::max(b.PontoonRadius, 0.01f);
                float total = 0.0f;
                for (const glm::vec3& lp : b.Pontoons)
                {
                    const glm::vec3 p = glm::vec3(w * glm::vec4(lp, 1.0f));
                    bool found = false;
                    const float h = WaterHeight(scene, p, t, found);
                    if (!found)
                        continue;
                    const float s = std::clamp((h - (p.y - r)) / (2.0f * r), 0.0f, 1.0f);
                    if (s <= 0.0f)
                        continue;
                    total += s;
                    // Kaldırma: yarıya batınca ağırlığı taşır (katsayı 1), noktada uygulanır (eğilir, sallanır)
                    const glm::vec3 lift(0.0f, 9.81f * mass * 2.0f * s * b.BuoyancyCoefficient / (float)n, 0.0f);
                    physics->AddForceAtPosition(ent, lift, p);
                }
                b.Submerged = total / (float)n;
                b.InWater = total > 0.0f;
                if (b.InWater)
                {
                    // Su sürtünmesi (batma oranıyla)
                    const float k = std::clamp(b.Submerged, 0.0f, 1.0f);
                    physics->SetLinearVelocity(ent, physics->GetLinearVelocity(ent) * std::max(0.0f, 1.0f - b.WaterDrag * k * dt));
                    physics->SetAngularVelocity(ent, physics->GetAngularVelocity(ent) * std::max(0.0f, 1.0f - b.WaterAngularDrag * k * dt));
                }
            }
        }

        void Render(Scene& scene, const glm::vec3& cam)
        {
            auto view = scene.m_Registry.view<TransformComponent, WaterBodyComponent>();
            if (view.begin() == view.end())
                return;
            // Çıkışta GL bağlamı kapandıktan sonra yok edilmesin (bilerek bırakılır)
            static auto& s_Shader = *new std::shared_ptr<Shader>();
            static bool s_Tried = false;
            if (!s_Tried)
            {
                s_Tried = true;
                s_Shader = Shader::Create("Engine/src/Shaders/3D/Forward/Water.cqsh");
            }
            if (!s_Shader || !s_Shader->IsValid())
                return;
            GLboolean blend = glIsEnabled(GL_BLEND), cull = glIsEnabled(GL_CULL_FACE);
            GLboolean depthMask = GL_TRUE;
            glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
            for (auto e : view)
            {
                if (!scene.IsEntityActive(e))
                    continue;
                const auto& w = view.get<WaterBodyComponent>(e);
                const glm::mat4 world = scene.ComputeWorldMatrix(e);
                Renderer3D::PrepareCustomShader(s_Shader);
                auto& sh = s_Shader;
                const int res = std::clamp(w.Resolution, 8, 1024);
                const glm::vec2 size = w.Type == WaterBodyType::Ocean ? glm::max(w.Size, glm::vec2(200.0f)) : w.Size;
                WaterBodyComponent sized = w;
                sized.Size = size;
                sh->SetMat4("u_Transform", world);
                sh->SetFloat2("u_Size", size);
                sh->SetFloat2("u_Offset", OceanOffset(sized, world, cam));
                Wave waves[4];
                const int n = MakeWaves(w, waves);
                sh->SetInt("u_WaveCount", n);
                for (int i = 0; i < n; ++i)
                {
                    sh->SetFloat4("u_Waves[" + std::to_string(i) + "]", glm::vec4(waves[i].Dir, waves[i].Length, waves[i].Amplitude));
                    sh->SetFloat4("u_WaveParams[" + std::to_string(i) + "]", glm::vec4(waves[i].Speed, waves[i].Steepness, 0.0f, 0.0f));
                }
                sh->SetFloat3("u_ShallowColor", w.ShallowColor);
                sh->SetFloat3("u_DeepColor", w.DeepColor);
                sh->SetFloat("u_Opacity", w.Opacity);
                sh->SetFloat("u_DepthFade", w.DepthFade);
                sh->SetFloat("u_ShoreFade", w.ShoreFade);
                sh->SetFloat("u_Roughness", w.Roughness);
                sh->SetFloat("u_Foam", w.Foam);
                const uint32_t sceneDepth = PostProcessStack::GetCurrentSceneDepth();
                sh->SetInt("u_HasSceneDepth", sceneDepth ? 1 : 0);
                // Sahne derinliği: çerçeve kaynaklarının kullanmadığı birim (0-5 malzeme, 8+ grafik / gölge / IBL / prob)
                sh->SetInt("u_WaterSceneDepth", 7);
                if (sceneDepth)
                {
                    glTextureBarrier();
                    glBindTextureUnit(7, sceneDepth);
                }
                sh->SetInt("u_ReactivePass", 0);
                glEnable(GL_BLEND);
                glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                glDisable(GL_CULL_FACE);
                glDepthMask(GL_FALSE);
                const Grid& g = GridOf(res);
                glBindVertexArray(g.Vao);
                glDrawElements(GL_TRIANGLES, g.Count, GL_UNSIGNED_INT, nullptr);
                // TAA: dalgalar her karede değişir; tepki maskesi (geçmişe az güven)
                if (PostProcessStack::BeginReactiveDraw())
                {
                    sh->SetInt("u_ReactivePass", 1);
                    glBlendEquation(GL_MAX);
                    glBlendFunc(GL_ONE, GL_ONE);
                    glDrawElements(GL_TRIANGLES, g.Count, GL_UNSIGNED_INT, nullptr);
                    glBlendEquation(GL_FUNC_ADD);
                    sh->SetInt("u_ReactivePass", 0);
                    PostProcessStack::EndReactiveDraw();
                }
                glBindVertexArray(0);
            }
            glDepthMask(depthMask);
            if (!blend) glDisable(GL_BLEND);
            if (cull) glEnable(GL_CULL_FACE);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        }

        void DrawGizmo(Scene& scene, uint32_t entity)
        {
            const entt::entity e = (entt::entity)entity;
            auto& reg = scene.m_Registry;
            if (!reg.valid(e)) return;
            const glm::mat4 w = scene.ComputeWorldMatrix(e);
            if (const auto* wb = reg.try_get<WaterBodyComponent>(e); wb && wb->Type != WaterBodyType::Ocean)
                DebugDraw::Box(w * glm::scale(glm::mat4(1.0f), glm::vec3(wb->Size.x, 0.01f, wb->Size.y)), glm::vec4(0.3f, 0.7f, 1.0f, 1.0f), 0.0f, false);
            if (const auto* b = reg.try_get<BuoyancyComponent>(e))
                for (const glm::vec3& p : b->Pontoons)
                    DebugDraw::Sphere(glm::vec3(w * glm::vec4(p, 1.0f)), b->PontoonRadius, glm::vec4(0.3f, 0.8f, 1.0f, 1.0f), 0.0f, false);
        }
    }

    template<> void Scene::OnComponentAdded<WaterBodyComponent>(Entity, WaterBodyComponent&) {}
    template<> void Scene::OnComponentAdded<BuoyancyComponent>(Entity, BuoyancyComponent&) {}

    void RegisterWaterComponents()
    {
        const glm::vec4 kColor{ 0.3f, 0.7f, 1.0f, 1.0f };
        {
            using C = WaterBodyComponent;
            ComponentDef<C>("Water Body", "WaterBodyComponent", "Rendering", ICON_FA_WATER).Color(kColor)
                .Describe("A water surface (Unreal Water Body): Ocean follows the camera to the horizon, Lake / Custom cover Size. "
                          "Gerstner waves, sky reflections, shallow-to-deep color, soft shores and foam. Buoyancy components float on it.")
                .Field("Type", "Water Body Type", &C::Type).Options({ "Ocean", "Lake", "Custom" })
                .Field("Size", "Size", &C::Size).Speed(0.5f).VisibleIf([](const C& c) { return c.Type != WaterBodyType::Ocean; })
                .Field("Resolution", "Resolution", &C::Resolution).Range(8.0f, 1024.0f, 1.0f)
                .Field("WaveCount", "Wave Count", &C::WaveCount).Range(0.0f, 4.0f, 1.0f).Header("Waves")
                .Field("WaveAmplitude", "Amplitude", &C::WaveAmplitude).Range(0.0f, 20.0f, 0.01f)
                .Field("WaveLength", "Wavelength", &C::WaveLength).Range(0.1f, 500.0f, 0.1f)
                .Field("WaveSpeed", "Speed", &C::WaveSpeed).Range(0.0f, 50.0f, 0.05f)
                .Field("WaveDirection", "Direction", &C::WaveDirection).Range(-360.0f, 360.0f, 1.0f)
                .Field("Steepness", "Steepness", &C::Steepness).Range(0.0f, 1.0f, 0.01f)
                .Field("ShallowColor", "Shallow Color", &C::ShallowColor, FieldKind::Color3).Header("Look")
                .Field("DeepColor", "Deep Color", &C::DeepColor, FieldKind::Color3)
                .Field("Opacity", "Opacity", &C::Opacity).Range(0.0f, 1.0f, 0.01f)
                .Field("DepthFade", "Depth Fade", &C::DepthFade).Range(0.01f, 100.0f, 0.05f)
                .Field("ShoreFade", "Shore Fade", &C::ShoreFade).Range(0.0f, 10.0f, 0.01f)
                .Field("Roughness", "Roughness", &C::Roughness).Range(0.0f, 1.0f, 0.01f)
                .Field("Foam", "Foam", &C::Foam).Range(0.0f, 2.0f, 0.01f);
        }
        {
            using C = BuoyancyComponent;
            ComponentDef<C>("Buoyancy", "BuoyancyComponent", "Physics", ICON_FA_LIFE_RING).Color({ 0.45f, 0.85f, 0.55f, 1.0f })
                .Requires("RigidbodyComponent")
                .Describe("Floats a Rigidbody on Water Bodies: each pontoon pushes up as it sinks (half under water carries its share of the weight), "
                          "so boats tilt and bob on the waves.")
                .Field("Pontoons", "Pontoons", &C::Pontoons).Speed(0.05f).Tooltip("Local points that feel the water.")
                .Field("PontoonRadius", "Pontoon Radius", &C::PontoonRadius).Range(0.01f, 50.0f, 0.01f)
                .Field("BuoyancyCoefficient", "Buoyancy Coefficient", &C::BuoyancyCoefficient).Range(0.0f, 10.0f, 0.01f)
                .Field("WaterDrag", "Water Drag", &C::WaterDrag).Range(0.0f, 50.0f, 0.05f)
                .Field("WaterAngularDrag", "Water Angular Drag", &C::WaterAngularDrag).Range(0.0f, 50.0f, 0.05f)
                .Field("InWater", "In Water", &C::InWater).ReadOnly().Header("Runtime")
                .Field("Submerged", "Submerged", &C::Submerged).ReadOnly();
        }
    }
}
