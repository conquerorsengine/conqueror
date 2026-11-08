#include "ParticleSystem.h"
#include "Core/Tiering/QualitySettings.h"
#include "Core/Logging/Log.h"
#include "Renderer/Renderer.h"
#include "Renderer/PostProcess.h"
#include "Renderer/RHI/Shader.h"
#include "Renderer/RHI/Texture.h"
#include "Renderer/Utilities/Renderer3D/Renderer3D.h"
#include "Scene/GameplaySystems.h"

#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace Conqueror
{
    namespace
    {
        constexpr float kPi = 3.14159265358979f;

        struct InstanceData
        {
            glm::vec4 PosSize;
            glm::vec4 Color;
            glm::vec4 Params;
            glm::vec4 Velocity;
        };

        struct RenderState
        {
            std::shared_ptr<Shader> ParticleShader;
            GLuint VAO = 0, QuadVBO = 0, InstanceVBO = 0;
            size_t InstanceCapacity = 0;
            std::vector<InstanceData> Instances;
        };
        RenderState* s_Render = nullptr;

        RenderState& GetRender()
        {
            if (!s_Render)
            {
                s_Render = new RenderState();
                s_Render->ParticleShader = Shader::Create("Engine/src/Shaders/3D/Forward/Particle.cqsh");
                glCreateVertexArrays(1, &s_Render->VAO);
                glBindVertexArray(s_Render->VAO);
                const float corners[] = { -0.5f, -0.5f, 0.5f, -0.5f, 0.5f, 0.5f, -0.5f, -0.5f, 0.5f, 0.5f, -0.5f, 0.5f };
                glGenBuffers(1, &s_Render->QuadVBO);
                glBindBuffer(GL_ARRAY_BUFFER, s_Render->QuadVBO);
                glBufferData(GL_ARRAY_BUFFER, sizeof(corners), corners, GL_STATIC_DRAW);
                glEnableVertexAttribArray(0);
                glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);

                glGenBuffers(1, &s_Render->InstanceVBO);
                glBindBuffer(GL_ARRAY_BUFFER, s_Render->InstanceVBO);
                for (int i = 0; i < 4; ++i)
                {
                    glEnableVertexAttribArray(1 + i);
                    glVertexAttribPointer(1 + i, 4, GL_FLOAT, GL_FALSE, sizeof(InstanceData), (const void*)(sizeof(glm::vec4) * i));
                    glVertexAttribDivisor(1 + i, 1);
                }
                glBindVertexArray(0);
            }
            return *s_Render;
        }

        float Rand01(std::mt19937& rng) { return std::uniform_real_distribution<float>(0.0f, 1.0f)(rng); }
        float RandRange(std::mt19937& rng, const glm::vec2& r) { return r.x + (r.y - r.x) * Rand01(rng); }

        // Basit 3D değer gürültüsü (eğri ürünü yeterli; akış benzeri kıpırtı için)
        float Hash(glm::vec3 p)
        {
            p = glm::fract(p * 0.3183099f + glm::vec3(0.1f, 0.2f, 0.3f));
            p *= 17.0f;
            return glm::fract(p.x * p.y * p.z * (p.x + p.y + p.z)) * 2.0f - 1.0f;
        }
        float ValueNoise(const glm::vec3& x)
        {
            glm::vec3 i = glm::floor(x), f = glm::fract(x);
            glm::vec3 u = f * f * (3.0f - 2.0f * f);
            auto h = [&](float a, float b, float c) { return Hash(i + glm::vec3(a, b, c)); };
            return glm::mix(glm::mix(glm::mix(h(0, 0, 0), h(1, 0, 0), u.x), glm::mix(h(0, 1, 0), h(1, 1, 0), u.x), u.y),
                            glm::mix(glm::mix(h(0, 0, 1), h(1, 0, 1), u.x), glm::mix(h(0, 1, 1), h(1, 1, 1), u.x), u.y), u.z);
        }
    }

    // ------------------------------------------------------------------ Eğri / gradyan

    float FloatCurve::Evaluate(float t) const
    {
        if (Keys.empty()) return 1.0f;
        if (t <= Keys.front().Time) return Keys.front().Value;
        if (t >= Keys.back().Time) return Keys.back().Value;
        for (size_t i = 1; i < Keys.size(); ++i)
            if (t <= Keys[i].Time)
            {
                float span = std::max(Keys[i].Time - Keys[i - 1].Time, 1e-6f);
                float k = (t - Keys[i - 1].Time) / span;
                k = k * k * (3.0f - 2.0f * k); // yumuşak teğetler
                return Keys[i - 1].Value + (Keys[i].Value - Keys[i - 1].Value) * k;
            }
        return Keys.back().Value;
    }

    void FloatCurve::Sort()
    {
        std::sort(Keys.begin(), Keys.end(), [](const Key& a, const Key& b) { return a.Time < b.Time; });
    }

    glm::vec4 ColorGradient::Evaluate(float t) const
    {
        if (Keys.empty()) return glm::vec4(1.0f);
        if (t <= Keys.front().Time) return Keys.front().Color;
        if (t >= Keys.back().Time) return Keys.back().Color;
        for (size_t i = 1; i < Keys.size(); ++i)
            if (t <= Keys[i].Time)
            {
                float span = std::max(Keys[i].Time - Keys[i - 1].Time, 1e-6f);
                return glm::mix(Keys[i - 1].Color, Keys[i].Color, (t - Keys[i - 1].Time) / span);
            }
        return Keys.back().Color;
    }

    void ColorGradient::Sort()
    {
        std::sort(Keys.begin(), Keys.end(), [](const Key& a, const Key& b) { return a.Time < b.Time; });
    }

    // ------------------------------------------------------------------ Kontrol

    void ParticleSystem::Play(ParticleSystemComponent& ps)
    {
        auto& rt = *ps.RT;
        if (rt.Paused) { rt.Paused = false; return; }
        if (!rt.Playing)
        {
            rt.Playing = true;
            rt.Started = false;
            rt.Time = 0.0f;
            rt.EmitAccumulator = 0.0f;
            rt.BurstCyclesDone.assign(ps.Bursts.size(), 0);
        }
    }

    void ParticleSystem::Pause(ParticleSystemComponent& ps) { ps.RT->Paused = true; }

    void ParticleSystem::Stop(ParticleSystemComponent& ps, bool clear)
    {
        auto& rt = *ps.RT;
        rt.Playing = false;
        rt.Paused = false;
        rt.Time = 0.0f;
        if (clear)
            rt.Particles.clear();
    }

    void ParticleSystem::Restart(ParticleSystemComponent& ps)
    {
        Stop(ps, true);
        ps.RT->TotalTime = 0.0f;
        ps.RT->HasLastPosition = false;
        Play(ps);
    }

    void ParticleSystem::Emit(ParticleSystemComponent& ps, const glm::mat4& world, int count)
    {
        auto& rt = *ps.RT;
        // Kalite düzeyinin parçacık bütçesi üst sınırdır
        const int maxP = std::clamp(std::min(ps.MaxParticles, std::max(QualitySettings::GetPreset().MaxParticles, 1)), 1, 100000);
        auto& rng = rt.Rng;
        const glm::mat3 rot = glm::mat3(glm::normalize(glm::vec3(world[0])), glm::normalize(glm::vec3(world[1])), glm::normalize(glm::vec3(world[2])));
        const glm::vec3 origin = glm::vec3(world[3]);

        for (int n = 0; n < count && (int)rt.Particles.size() < maxP; ++n)
        {
            Particle p;
            glm::vec3 pos(0.0f), dir(0.0f, 1.0f, 0.0f);
            const float r = ps.EmitFromShell ? ps.Radius : ps.Radius * std::cbrt(Rand01(rng));
            if (ps.ShapeEnabled)
            {
                switch (ps.Shape)
                {
                    case ParticleShape::Sphere:
                    case ParticleShape::Hemisphere:
                    {
                        float z = Rand01(rng) * 2.0f - 1.0f, a = Rand01(rng) * 2.0f * kPi;
                        float s = std::sqrt(std::max(0.0f, 1.0f - z * z));
                        dir = glm::vec3(s * std::cos(a), z, s * std::sin(a));
                        if (ps.Shape == ParticleShape::Hemisphere) dir.y = std::fabs(dir.y);
                        pos = dir * r;
                        break;
                    }
                    case ParticleShape::Cone:
                    {
                        float a = Rand01(rng) * 2.0f * kPi;
                        float rr = (ps.EmitFromShell ? 1.0f : std::sqrt(Rand01(rng))) * ps.Radius;
                        pos = glm::vec3(std::cos(a) * rr, 0.0f, std::sin(a) * rr);
                        float spread = std::tan(glm::radians(std::clamp(ps.Angle, 0.0f, 89.0f)));
                        dir = glm::normalize(glm::vec3(0.0f, 1.0f, 0.0f) + glm::vec3(pos.x, 0.0f, pos.z) / std::max(ps.Radius, 1e-4f) * spread);
                        break;
                    }
                    case ParticleShape::Box:
                        pos = (glm::vec3(Rand01(rng), Rand01(rng), Rand01(rng)) - 0.5f) * ps.BoxSize;
                        dir = glm::vec3(0.0f, 1.0f, 0.0f);
                        break;
                    case ParticleShape::Circle:
                    {
                        float a = Rand01(rng) * 2.0f * kPi;
                        float rr = (ps.EmitFromShell ? 1.0f : std::sqrt(Rand01(rng))) * ps.Radius;
                        pos = glm::vec3(std::cos(a) * rr, 0.0f, std::sin(a) * rr);
                        dir = glm::length(pos) > 1e-5f ? glm::normalize(pos) : glm::vec3(1, 0, 0);
                        break;
                    }
                    case ParticleShape::Edge:
                        pos = glm::vec3((Rand01(rng) - 0.5f) * 2.0f * ps.Radius, 0.0f, 0.0f);
                        dir = glm::vec3(0.0f, 1.0f, 0.0f);
                        break;
                    default:
                        break;
                }
            }
            if (ps.RandomizeDirection > 0.0f)
            {
                glm::vec3 rnd = glm::normalize(glm::vec3(Rand01(rng), Rand01(rng), Rand01(rng)) * 2.0f - 1.0f + glm::vec3(1e-4f));
                dir = glm::normalize(glm::mix(dir, rnd, std::clamp(ps.RandomizeDirection, 0.0f, 1.0f)));
            }

            float speed = RandRange(rng, ps.StartSpeed);
            if (ps.SimulationSpace == ParticleSpace::World)
            {
                p.Position = origin + rot * pos;
                p.Velocity = rot * dir * speed;
            }
            else
            {
                p.Position = pos;
                p.Velocity = dir * speed;
            }
            p.Lifetime = std::max(0.01f, RandRange(rng, ps.StartLifetime));
            p.StartSize = RandRange(rng, ps.StartSize);
            p.Rotation = glm::radians(RandRange(rng, ps.StartRotation));
            p.AngularVelocity = glm::radians(ps.AngularVelocity) * (Rand01(rng) < 0.5f ? -1.0f : 1.0f);
            p.StartColor = glm::mix(ps.StartColorA, ps.StartColorB, Rand01(rng));
            p.Random = Rand01(rng);
            rt.Particles.push_back(p);
        }
    }

    bool ParticleSystem::IsFinished(const ParticleSystemComponent& ps)
    {
        const auto& rt = *ps.RT;
        return rt.EverActive && !rt.Playing && rt.Particles.empty();
    }

    void ParticleSystem::Update(ParticleSystemComponent& ps, const glm::mat4& world, float dtIn)
    {
        auto& rt = *ps.RT;
        if (ps.PlayOnAwake && !rt.Started && !rt.Playing)
            Play(ps);
        if (rt.Playing || !rt.Particles.empty())
            rt.EverActive = true;
        if (!rt.Playing && rt.Particles.empty())
            return;
        if (rt.Paused)
            return;

        float dt = std::clamp(dtIn, 0.0f, 0.1f) * std::max(ps.SimulationSpeed, 0.0f);
        if (dt <= 0.0f)
            return;

        // İlk kare: prewarm (döngüsel sistemi bir süre simüle et)
        if (rt.Playing && !rt.Started)
        {
            rt.Started = true;
            rt.BurstCyclesDone.assign(ps.Bursts.size(), 0);
            if (ps.Prewarm && ps.Looping)
            {
                const float step = 1.0f / 30.0f;
                ps.Prewarm = false; // özyinelemeyi engelle
                for (float t = 0.0f; t < ps.Duration; t += step)
                    Update(ps, world, step / std::max(ps.SimulationSpeed, 1e-4f));
                ps.Prewarm = true;
            }
        }

        const glm::vec3 origin = glm::vec3(world[3]);
        rt.TotalTime += dt;

        // ---- Yayım
        if (rt.Playing && ps.EmissionEnabled)
        {
            float t = rt.Time - ps.StartDelay;
            if (t >= 0.0f)
            {
                rt.EmitAccumulator += ps.RateOverTime * dt;
                int count = (int)rt.EmitAccumulator;
                rt.EmitAccumulator -= count;

                if (ps.RateOverDistance > 0.0f)
                {
                    if (rt.HasLastPosition)
                        rt.DistanceAccumulator += glm::length(origin - rt.LastPosition) * ps.RateOverDistance;
                    int dc = (int)rt.DistanceAccumulator;
                    rt.DistanceAccumulator -= dc;
                    count += dc;
                }
                if (count > 0)
                    Emit(ps, world, count);

                if (rt.BurstCyclesDone.size() != ps.Bursts.size())
                    rt.BurstCyclesDone.assign(ps.Bursts.size(), 0);
                for (size_t b = 0; b < ps.Bursts.size(); ++b)
                {
                    const auto& burst = ps.Bursts[b];
                    int& done = rt.BurstCyclesDone[b];
                    int cycles = std::max(1, burst.Cycles);
                    while (done < cycles && t >= burst.Time + done * std::max(burst.Interval, 0.001f))
                    {
                        Emit(ps, world, burst.Count);
                        ++done;
                    }
                }
            }

            rt.Time += dt;
            if (rt.Time >= ps.Duration + ps.StartDelay)
            {
                if (ps.Looping)
                {
                    rt.Time = std::fmod(rt.Time - ps.StartDelay, std::max(ps.Duration, 0.01f)) + ps.StartDelay;
                    rt.BurstCyclesDone.assign(ps.Bursts.size(), 0);
                }
                else
                {
                    rt.Playing = false; // mevcut parçacıklar ömrünü tamamlar
                }
            }
        }
        rt.LastPosition = origin;
        rt.HasLastPosition = true;

        // ---- Simülasyon
        const glm::vec3 gravity = glm::vec3(0.0f, -9.81f, 0.0f) * ps.GravityModifier;
        const float noiseT = rt.TotalTime * ps.NoiseScrollSpeed;
        auto& parts = rt.Particles;
        for (size_t i = 0; i < parts.size();)
        {
            Particle& p = parts[i];
            p.Age += dt;
            if (p.Age >= p.Lifetime)
            {
                p = parts.back();
                parts.pop_back();
                continue;
            }
            p.Velocity += gravity * dt;
            // Rüzgâr bölgeleri (Unity: External Forces)
            if (GameplaySystems::HasWind())
            {
                const bool localSpace = ps.SimulationSpace == ParticleSpace::Local;
                glm::vec3 wp = localSpace ? glm::vec3(world * glm::vec4(p.Position, 1.0f)) : p.Position;
                glm::vec3 wind = GameplaySystems::SampleWind(wp);
                if (localSpace)
                    wind = glm::inverse(glm::mat3(world)) * wind;
                p.Velocity += wind * dt;
            }
            // Particle System Force Field'lar
            if (GameplaySystems::HasForceFields())
            {
                const bool localSpace = ps.SimulationSpace == ParticleSpace::Local;
                glm::vec3 wp = localSpace ? glm::vec3(world * glm::vec4(p.Position, 1.0f)) : p.Position;
                glm::vec3 wv = localSpace ? glm::mat3(world) * p.Velocity : p.Velocity;
                GameplaySystems::ApplyForceFields(wp, wv, dt);
                p.Velocity = localSpace ? glm::inverse(glm::mat3(world)) * wv : wv;
            }
            if (ps.VelocityEnabled)
                p.Position += ps.VelocityOverLifetime * dt;
            if (ps.VelocityEnabled && ps.OrbitalSpeed != 0.0f)
            {
                glm::vec3 c = ps.SimulationSpace == ParticleSpace::World ? origin : glm::vec3(0.0f);
                glm::vec3 rel = p.Position - c;
                float a = ps.OrbitalSpeed * dt;
                float ca = std::cos(a), sa = std::sin(a);
                p.Position = c + glm::vec3(rel.x * ca - rel.z * sa, rel.y, rel.x * sa + rel.z * ca);
            }
            if (ps.NoiseEnabled && ps.NoiseStrength > 0.0f)
            {
                glm::vec3 q = p.Position * ps.NoiseFrequency + glm::vec3(noiseT, p.Random * 13.0f, -noiseT);
                glm::vec3 n(ValueNoise(q), ValueNoise(q + glm::vec3(31.7f, 0.0f, 0.0f)), ValueNoise(q + glm::vec3(0.0f, 47.3f, 0.0f)));
                p.Velocity += n * ps.NoiseStrength * dt * 4.0f;
            }
            if (ps.LimitVelocityEnabled)
            {
                p.Velocity *= std::max(0.0f, 1.0f - ps.Drag * dt);
                float sp = glm::length(p.Velocity);
                if (sp > ps.SpeedLimit && sp > 0.0f)
                    p.Velocity *= ps.SpeedLimit / sp;
            }
            p.Position += p.Velocity * dt;
            if (ps.RotationOverLifetimeEnabled)
                p.Rotation += p.AngularVelocity * dt;

            if (ps.CollisionEnabled)
            {
                float worldY = ps.SimulationSpace == ParticleSpace::World ? p.Position.y : (world * glm::vec4(p.Position, 1.0f)).y;
                if (worldY < ps.CollisionPlaneY && p.Velocity.y < 0.0f)
                {
                    p.Position.y += (ps.CollisionPlaneY - worldY);
                    p.Velocity.y = -p.Velocity.y * std::clamp(ps.Bounce, 0.0f, 1.0f);
                    p.Velocity.x *= 0.9f;
                    p.Velocity.z *= 0.9f;
                    p.Age += p.Lifetime * std::clamp(ps.LifetimeLoss, 0.0f, 1.0f);
                }
            }
            ++i;
        }
    }

    // ------------------------------------------------------------------ Çizim

    void ParticleSystem::Render(ParticleSystemComponent& ps, const glm::mat4& world)
    {
        auto& rt = *ps.RT;
        if (rt.Particles.empty())
            return;
        auto& r = GetRender();
        if (!r.ParticleShader || !r.ParticleShader->IsValid())
            return;

        // Doku
        if (rt.LoadedTexturePath != ps.TexturePath)
        {
            rt.LoadedTexturePath = ps.TexturePath;
            rt.Texture = ps.TexturePath.empty() ? nullptr : Texture2D::Create(ps.TexturePath);
        }

        const glm::vec3 camPos = Renderer3D::GetCamera().Position;
        const bool local = ps.SimulationSpace == ParticleSpace::Local;
        const int frames = ps.TextureSheetEnabled ? std::max(1, ps.Tiles.x) * std::max(1, ps.Tiles.y) : 1;

        // Sıralama (alfa için arkadan öne)
        std::vector<uint32_t> order(rt.Particles.size());
        for (uint32_t i = 0; i < order.size(); ++i) order[i] = i;
        std::vector<float> depth(rt.Particles.size());
        for (size_t i = 0; i < rt.Particles.size(); ++i)
        {
            glm::vec3 wp = local ? glm::vec3(world * glm::vec4(rt.Particles[i].Position, 1.0f)) : rt.Particles[i].Position;
            depth[i] = glm::dot(wp - camPos, wp - camPos);
        }
        if (ps.SortByDistance && ps.Blend != ParticleBlend::Additive)
            std::sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) { return depth[a] > depth[b]; });

        r.Instances.resize(order.size());
        for (size_t k = 0; k < order.size(); ++k)
        {
            const Particle& p = rt.Particles[order[k]];
            float life = std::clamp(p.Age / p.Lifetime, 0.0f, 1.0f);
            glm::vec3 wp = local ? glm::vec3(world * glm::vec4(p.Position, 1.0f)) : p.Position;
            glm::vec3 wv = local ? glm::vec3(world * glm::vec4(p.Velocity, 0.0f)) : p.Velocity;
            float size = p.StartSize * (ps.SizeOverLifetimeEnabled ? ps.SizeOverLifetime.Evaluate(life) : 1.0f);
            glm::vec4 color = p.StartColor * (ps.ColorOverLifetimeEnabled ? ps.ColorOverLifetime.Evaluate(life) : glm::vec4(1.0f));
            float frame = 0.0f;
            if (ps.TextureSheetEnabled)
            {
                float start = ps.RandomStartFrame ? p.Random * frames : 0.0f;
                frame = std::floor(std::fmod(start + life * frames * std::max(ps.AnimationCycles, 0.01f), (float)frames));
            }
            auto& inst = r.Instances[k];
            inst.PosSize = glm::vec4(wp, size);
            inst.Color = color;
            inst.Params = glm::vec4(p.Rotation, frame, glm::length(wv), 0.0f);
            inst.Velocity = glm::vec4(wv, 0.0f);
        }

        glBindBuffer(GL_ARRAY_BUFFER, r.InstanceVBO);
        size_t bytes = r.Instances.size() * sizeof(InstanceData);
        if (bytes > r.InstanceCapacity)
        {
            r.InstanceCapacity = std::max(bytes, r.InstanceCapacity * 2);
            glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)r.InstanceCapacity, nullptr, GL_DYNAMIC_DRAW);
        }
        glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)bytes, r.Instances.data());

        auto& shader = r.ParticleShader;
        shader->Bind();
        shader->SetInt("u_RenderMode", (int)ps.RenderMode);
        shader->SetFloat("u_LengthScale", ps.LengthScale);
        shader->SetFloat("u_SpeedScale", ps.SpeedScale);
        shader->SetFloat2("u_Tiles", ps.TextureSheetEnabled ? glm::vec2((float)std::max(1, ps.Tiles.x), (float)std::max(1, ps.Tiles.y)) : glm::vec2(1.0f));
        shader->SetInt("u_Blend", (int)ps.Blend);
        shader->SetFloat("u_Emissive", ps.EmissiveIntensity);
        shader->SetInt("u_HasTexture", rt.Texture && rt.Texture->GetRendererID() ? 1 : 0);
        if (rt.Texture)
            rt.Texture->Bind(0);
        shader->SetInt("u_Texture", 0);
        // Yumuşak parçacık: sahne derinliği okunur (derinlik yazılmaz; bariyer sonrası aynı dokuyu örneklemek tanımlı).
        // Yoksa düz dörtgen yüzeyi (küp, silah, zemin) kestiği yerde dümdüz bir çizgiyle kesiliyordu
        const uint32_t sceneDepth = PostProcessStack::GetCurrentSceneDepth();
        shader->SetInt("u_SoftDepth", sceneDepth ? 1 : 0);
        shader->SetInt("u_SceneDepth", 1);
        if (sceneDepth)
        {
            glTextureBarrier();
            glBindTextureUnit(1, sceneDepth);
        }

        GLboolean blend = glIsEnabled(GL_BLEND), cull = glIsEnabled(GL_CULL_FACE);
        GLboolean depthMask = GL_TRUE;
        glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
        glEnable(GL_BLEND);
        if (ps.Blend == ParticleBlend::Additive) glBlendFunc(GL_ONE, GL_ONE);
        else if (ps.Blend == ParticleBlend::Premultiplied) glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        else glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDisable(GL_CULL_FACE);
        glDepthMask(GL_FALSE);

        glBindVertexArray(r.VAO);
        shader->SetInt("u_ReactivePass", 0);
        glDrawArraysInstanced(GL_TRIANGLES, 0, 6, (GLsizei)r.Instances.size());
        // TAA tepki maskesi: parçacıklar her karede hareket eder, derinlik ve hareket bayrağı yazmaz. Kapladıkları pikseller
        // ayrı maskeye (en büyük örtü) yazılır, TAA yalnız o piksellerde geçmişe güvenmez. Maske yokken duman duran yüzey
        // gibi işleniyor, piksel piksel farklı yola düşüp düz sınırlarla kesiliyordu
        if (PostProcessStack::BeginReactiveDraw())
        {
            shader->SetInt("u_ReactivePass", 1);
            glBlendEquation(GL_MAX);
            glBlendFunc(GL_ONE, GL_ONE);
            glDrawArraysInstanced(GL_TRIANGLES, 0, 6, (GLsizei)r.Instances.size());
            glBlendEquation(GL_FUNC_ADD);
            shader->SetInt("u_ReactivePass", 0);
            PostProcessStack::EndReactiveDraw();
        }
        else if (PostProcessStack::IsStabilityActive() && !r.Instances.empty())
        {
            // Maske yazılamıyorsa (MSAA) tek kare yöntemlerinin zamansal kararlılık katmanı parçacıkların ekran dikdörtgeninde
            // kendini kapatır: dünya kutusu (uzatılmış dörtgende hızla uzayan boy dahil) bildirilir
            glm::vec3 lo(1e30f), hi(-1e30f);
            const bool stretched = ps.RenderMode == ParticleRenderMode::StretchedBillboard;
            for (const auto& inst : r.Instances)
            {
                const float along = stretched ? 0.5f * (ps.LengthScale + inst.Params.z * ps.SpeedScale) : 0.0f;
                const float radius = inst.PosSize.w * std::max(0.75f, along);
                lo = glm::min(lo, glm::vec3(inst.PosSize) - radius);
                hi = glm::max(hi, glm::vec3(inst.PosSize) + radius);
            }
            Renderer3D::MarkMovingBounds((lo + hi) * 0.5f, (hi - lo) * 0.5f);
        }
        glBindVertexArray(0);

        glDepthMask(depthMask);
        if (!blend) glDisable(GL_BLEND);
        if (cull) glEnable(GL_CULL_FACE);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        auto& stats = Renderer::GetStats();
        stats.DrawCalls++;
        stats.Triangles += (uint32_t)r.Instances.size() * 2;
        stats.Vertices += (uint32_t)r.Instances.size() * 4;
    }

    void ParticleSystem::Shutdown()
    {
        if (!s_Render)
            return;
        if (s_Render->VAO) glDeleteVertexArrays(1, &s_Render->VAO);
        if (s_Render->QuadVBO) glDeleteBuffers(1, &s_Render->QuadVBO);
        if (s_Render->InstanceVBO) glDeleteBuffers(1, &s_Render->InstanceVBO);
        delete s_Render;
        s_Render = nullptr;
    }

    // ------------------------------------------------------------------ Hazır ayarlar

    const std::vector<std::string>& ParticleSystem::GetPresetNames()
    {
        static const std::vector<std::string> names = { "Fire", "Smoke", "Sparks", "Explosion", "Rain", "Snow", "Magic", "Dust" };
        return names;
    }

    bool ParticleSystem::ApplyPreset(ParticleSystemComponent& ps, const std::string& preset)
    {
        ParticleSettings s; // varsayılanlardan başla
        auto grad = [](std::initializer_list<ColorGradient::Key> keys) { ColorGradient g; g.Keys = keys; return g; };

        if (preset == "Fire")
        {
            s.StartLifetime = { 0.6f, 1.2f }; s.StartSpeed = { 1.0f, 2.2f }; s.StartSize = { 0.4f, 0.9f };
            s.RateOverTime = 60.0f; s.Shape = ParticleShape::Cone; s.Radius = 0.3f; s.Angle = 12.0f;
            s.Blend = ParticleBlend::Additive; s.EmissiveIntensity = 3.0f;
            s.StartColorA = { 1.0f, 0.85f, 0.4f, 1.0f }; s.StartColorB = { 1.0f, 0.5f, 0.15f, 1.0f };
            s.ColorOverLifetime = grad({ { 0.0f, { 1, 1, 1, 0 } }, { 0.15f, { 1, 1, 1, 1 } }, { 0.6f, { 1, 0.45f, 0.1f, 0.8f } }, { 1.0f, { 0.3f, 0.05f, 0.0f, 0 } } });
            s.SizeOverLifetimeEnabled = true; s.SizeOverLifetime = FloatCurve::Linear(1.0f, 0.2f);
            s.NoiseEnabled = true; s.NoiseStrength = 0.6f; s.NoiseFrequency = 1.2f;
            s.StartRotation = { 0.0f, 360.0f }; s.RotationOverLifetimeEnabled = true; s.AngularVelocity = 60.0f;
        }
        else if (preset == "Smoke")
        {
            s.StartLifetime = { 3.0f, 5.0f }; s.StartSpeed = { 0.6f, 1.2f }; s.StartSize = { 0.8f, 1.4f };
            s.RateOverTime = 12.0f; s.Shape = ParticleShape::Cone; s.Radius = 0.4f; s.Angle = 15.0f;
            s.StartColorA = { 0.55f, 0.55f, 0.58f, 0.6f }; s.StartColorB = { 0.35f, 0.35f, 0.38f, 0.5f };
            s.ColorOverLifetime = grad({ { 0.0f, { 1, 1, 1, 0 } }, { 0.2f, { 1, 1, 1, 0.8f } }, { 1.0f, { 1, 1, 1, 0 } } });
            s.SizeOverLifetimeEnabled = true; s.SizeOverLifetime = FloatCurve::Linear(0.6f, 2.5f);
            s.NoiseEnabled = true; s.NoiseStrength = 0.4f; s.NoiseFrequency = 0.4f;
            s.StartRotation = { 0.0f, 360.0f }; s.RotationOverLifetimeEnabled = true; s.AngularVelocity = 20.0f;
        }
        else if (preset == "Sparks")
        {
            s.StartLifetime = { 0.4f, 0.9f }; s.StartSpeed = { 4.0f, 8.0f }; s.StartSize = { 0.04f, 0.08f };
            s.RateOverTime = 80.0f; s.Shape = ParticleShape::Hemisphere; s.Radius = 0.1f; s.GravityModifier = 1.0f;
            s.Blend = ParticleBlend::Additive; s.EmissiveIntensity = 4.0f; s.RenderMode = ParticleRenderMode::StretchedBillboard;
            s.LengthScale = 1.0f; s.SpeedScale = 0.6f;
            s.StartColorA = { 1.0f, 0.9f, 0.5f, 1.0f }; s.StartColorB = { 1.0f, 0.6f, 0.2f, 1.0f };
            s.ColorOverLifetime = grad({ { 0.0f, { 1, 1, 1, 1 } }, { 1.0f, { 1, 0.3f, 0.0f, 0 } } });
            s.CollisionEnabled = true; s.Bounce = 0.4f; s.LifetimeLoss = 0.2f;
        }
        else if (preset == "Explosion")
        {
            s.Looping = false; s.Duration = 1.0f; s.RateOverTime = 0.0f;
            s.Bursts = { { 0.0f, 120, 1, 0.1f } };
            s.StartLifetime = { 0.6f, 1.4f }; s.StartSpeed = { 3.0f, 9.0f }; s.StartSize = { 0.3f, 1.0f };
            s.Shape = ParticleShape::Sphere; s.Radius = 0.3f; s.Blend = ParticleBlend::Additive; s.EmissiveIntensity = 4.0f;
            s.LimitVelocityEnabled = true; s.Drag = 3.0f;
            s.StartColorA = { 1.0f, 0.8f, 0.3f, 1.0f }; s.StartColorB = { 1.0f, 0.35f, 0.1f, 1.0f };
            s.ColorOverLifetime = grad({ { 0.0f, { 1, 1, 1, 1 } }, { 0.4f, { 1, 0.5f, 0.2f, 0.8f } }, { 1.0f, { 0.2f, 0.2f, 0.2f, 0 } } });
            s.SizeOverLifetimeEnabled = true; s.SizeOverLifetime = FloatCurve::Linear(0.6f, 1.8f);
        }
        else if (preset == "Rain")
        {
            s.StartLifetime = { 1.2f, 1.6f }; s.StartSpeed = { 0.0f, 0.0f }; s.StartSize = { 0.03f, 0.05f };
            s.RateOverTime = 600.0f; s.MaxParticles = 5000; s.Shape = ParticleShape::Box; s.BoxSize = { 30.0f, 0.1f, 30.0f };
            s.GravityModifier = 1.6f; s.RenderMode = ParticleRenderMode::StretchedBillboard; s.LengthScale = 1.0f; s.SpeedScale = 0.08f;
            s.StartColorA = { 0.7f, 0.8f, 1.0f, 0.5f }; s.StartColorB = s.StartColorA;
            s.ColorOverLifetimeEnabled = false;
        }
        else if (preset == "Snow")
        {
            s.StartLifetime = { 6.0f, 9.0f }; s.StartSpeed = { 0.0f, 0.2f }; s.StartSize = { 0.05f, 0.14f };
            s.RateOverTime = 150.0f; s.MaxParticles = 3000; s.Shape = ParticleShape::Box; s.BoxSize = { 30.0f, 0.1f, 30.0f };
            s.GravityModifier = 0.08f; s.NoiseEnabled = true; s.NoiseStrength = 0.5f; s.NoiseFrequency = 0.3f;
            s.StartColorA = { 1, 1, 1, 0.9f }; s.StartColorB = s.StartColorA;
            s.ColorOverLifetime = grad({ { 0.0f, { 1, 1, 1, 0 } }, { 0.1f, { 1, 1, 1, 1 } }, { 0.9f, { 1, 1, 1, 1 } }, { 1.0f, { 1, 1, 1, 0 } } });
            s.Prewarm = true;
        }
        else if (preset == "Magic")
        {
            s.StartLifetime = { 1.0f, 2.0f }; s.StartSpeed = { 0.2f, 0.8f }; s.StartSize = { 0.08f, 0.2f };
            s.RateOverTime = 50.0f; s.Shape = ParticleShape::Sphere; s.Radius = 0.6f; s.EmitFromShell = true;
            s.Blend = ParticleBlend::Additive; s.EmissiveIntensity = 3.0f;
            s.VelocityEnabled = true; s.OrbitalSpeed = 2.0f; s.VelocityOverLifetime = { 0.0f, 0.5f, 0.0f };
            s.StartColorA = { 0.5f, 0.7f, 1.0f, 1.0f }; s.StartColorB = { 0.9f, 0.4f, 1.0f, 1.0f };
            s.ColorOverLifetime = grad({ { 0.0f, { 1, 1, 1, 0 } }, { 0.2f, { 1, 1, 1, 1 } }, { 1.0f, { 1, 1, 1, 0 } } });
        }
        else if (preset == "Dust")
        {
            s.StartLifetime = { 4.0f, 7.0f }; s.StartSpeed = { 0.02f, 0.1f }; s.StartSize = { 0.02f, 0.05f };
            s.RateOverTime = 30.0f; s.Shape = ParticleShape::Box; s.BoxSize = { 8.0f, 3.0f, 8.0f };
            s.NoiseEnabled = true; s.NoiseStrength = 0.15f; s.NoiseFrequency = 0.6f; s.Prewarm = true;
            s.StartColorA = { 1.0f, 0.95f, 0.85f, 0.5f }; s.StartColorB = s.StartColorA;
            s.ColorOverLifetime = grad({ { 0.0f, { 1, 1, 1, 0 } }, { 0.5f, { 1, 1, 1, 1 } }, { 1.0f, { 1, 1, 1, 0 } } });
        }
        else
        {
            return false;
        }
        static_cast<ParticleSettings&>(ps) = s;
        Restart(ps);
        return true;
    }
}
