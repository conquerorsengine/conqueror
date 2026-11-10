#include "NavAgents.h"
#include "NavMesh.h"
#include "Scene/Scene.h"
#include "Scene/Entity.h"
#include "Scene/Components.h"
#include "Renderer/RHI/Shader.h"
#include "Core/Logging/Log.h"
#include "Core/Project/ProjectSettings.h"

#include <glad/glad.h>

#include <algorithm>
#include <cmath>
#include <memory>

namespace Conqueror
{
    namespace
    {
        // Aynı sahne geometrisi için pişirme sonucu (oyuna her girişte yeniden pişirmemek için)
        struct BakeCache
        {
            uint64_t Signature = 0;
            bool Valid = false;
            NavMesh Mesh;
        };
        BakeCache* s_Cache = nullptr;
        BakeCache& Cache()
        {
            if (!s_Cache) s_Cache = new BakeCache();
            return *s_Cache;
        }

        struct DebugState
        {
            std::shared_ptr<Conqueror::Shader> DebugShader;
            GLuint Vao = 0, Vbo = 0;
            GLsizei Count = 0;
            uint64_t Version = ~0ull, BlockedVersion = ~0ull;
            const NavMesh* Source = nullptr;
        };
        DebugState* s_Debug = nullptr;

        float WrapAngle(float a)
        {
            while (a > glm::pi<float>()) a -= glm::two_pi<float>();
            while (a < -glm::pi<float>()) a += glm::two_pi<float>();
            return a;
        }

        void RebuildPathCache(NavMeshAgentComponent& a)
        {
            a.Path.clear();
            for (auto& p : a.PathPoints)
                a.Path.push_back(p.Position);
        }

        float RemainingDistance(const NavMeshAgentComponent& a, const glm::vec3& pos)
        {
            if (!a.HasPath || a.CurrentPathIndex >= (int)a.PathPoints.size())
                return 0.0f;
            float d = glm::length(a.PathPoints[(size_t)a.CurrentPathIndex].Position - pos);
            for (size_t i = (size_t)a.CurrentPathIndex + 1; i < a.PathPoints.size(); ++i)
                d += glm::length(a.PathPoints[i].Position - a.PathPoints[i - 1].Position);
            return d;
        }
    }

    namespace NavAgents
    {
        // Project Settings > Navigation alan maliyetleri (Not Walkable her zaman geçilemez)
        static void ApplyAreaCosts(NavMesh& nav)
        {
            const auto& areas = ProjectSettings::Navigation().Areas;
            for (int i = 0; i < kNavMaxAreas && i < (int)areas.size(); ++i)
                nav.SetAreaCost(i, i == NavArea_NotWalkable ? 0.0f : std::max(areas[(size_t)i].Cost, 0.01f));
        }

        void Bake(Scene& scene)
        {
            NavMesh& nav = scene.GetNavMesh();
            ApplyAreaCosts(nav);
            NavMeshBuilder::Bake(&scene, nav);
            auto& cache = Cache();
            cache.Signature = NavMeshBuilder::ComputeSceneSignature(&scene);
            cache.Mesh = nav;
            cache.Valid = nav.IsBaked();
        }

        void BakeCached(Scene& scene)
        {
            const uint64_t sig = NavMeshBuilder::ComputeSceneSignature(&scene);
            auto& cache = Cache();
            if (cache.Valid && cache.Signature == sig)
            {
                scene.GetNavMesh() = cache.Mesh;
                ApplyAreaCosts(scene.GetNavMesh());
                return;
            }
            Bake(scene);
        }

        void Prebake(Scene& scene)
        {
            bool bakeOnPlay = false;
            for (auto e : scene.m_Registry.view<NavMeshSurfaceComponent>())
                bakeOnPlay |= scene.m_Registry.get<NavMeshSurfaceComponent>(e).BakeOnPlay && scene.IsEntityActive(e);
            if (!bakeOnPlay)
                return;
            scene.UpdateWorldTransforms();
            const uint64_t sig = NavMeshBuilder::ComputeSceneSignature(&scene);
            auto& cache = Cache();
            if (cache.Valid && cache.Signature == sig)
                return;
            NavMesh nav;
            ApplyAreaCosts(nav);
            NavMeshBuilder::Bake(&scene, nav);
            cache.Signature = sig;
            cache.Mesh = nav;
            cache.Valid = nav.IsBaked();
        }

        void OnRuntimeStart(Scene& scene)
        {
            bool bakeOnPlay = false;
            for (auto e : scene.m_Registry.view<NavMeshSurfaceComponent>())
                bakeOnPlay |= scene.m_Registry.get<NavMeshSurfaceComponent>(e).BakeOnPlay && scene.IsEntityActive(e);
            if (!bakeOnPlay)
                return;
            const uint64_t sig = NavMeshBuilder::ComputeSceneSignature(&scene);
            auto& cache = Cache();
            if (cache.Valid && cache.Signature == sig)
            {
                scene.GetNavMesh() = cache.Mesh;
                ApplyAreaCosts(scene.GetNavMesh());
                return;
            }
            Bake(scene);
        }

        bool SetDestination(Scene& scene, uint32_t agentEntity, const glm::vec3& destination)
        {
            entt::entity e = (entt::entity)agentEntity;
            auto& reg = scene.m_Registry;
            if (!reg.valid(e) || !reg.all_of<NavMeshAgentComponent, TransformComponent>(e))
                return false;
            auto& a = reg.get<NavMeshAgentComponent>(e);
            const glm::vec3 pos = glm::vec3(reg.get<TransformComponent>(e).WorldTransform[3]) - glm::vec3(0.0f, a.BaseOffset, 0.0f);
            a.Destination = destination;
            bool complete = false;
            if (!scene.GetNavMesh().FindPath(pos, destination, a.PathPoints, a.AreaMask, &complete) || a.PathPoints.size() < 1)
            {
                a.HasPath = false;
                a.PathPoints.clear();
                a.Path.clear();
                return false;
            }
            a.PathComplete = complete;
            a.HasPath = true;
            a.CurrentPathIndex = a.PathPoints.size() > 1 ? 1 : 0;
            a.OnLink = false;
            a.IsStopped = false;
            RebuildPathCache(a);
            a.RemainingDistance = RemainingDistance(a, pos);
            return true;
        }

        void Warp(Scene& scene, uint32_t agentEntity, const glm::vec3& position)
        {
            entt::entity e = (entt::entity)agentEntity;
            auto& reg = scene.m_Registry;
            if (!reg.valid(e) || !reg.all_of<NavMeshAgentComponent, TransformComponent>(e))
                return;
            auto& a = reg.get<NavMeshAgentComponent>(e);
            glm::vec3 p = position;
            glm::vec3 snapped;
            if (scene.GetNavMesh().SamplePosition(position, 3.0f, snapped))
                p = snapped;
            auto& t = reg.get<TransformComponent>(e);
            t.Position = p + glm::vec3(0.0f, a.BaseOffset, 0.0f);
            a.HasPath = false;
            a.PathPoints.clear();
            a.Path.clear();
            a.Velocity = glm::vec3(0.0f);
            a.OnLink = false;
        }

        void Stop(Scene& scene, uint32_t agentEntity, bool stopped)
        {
            entt::entity e = (entt::entity)agentEntity;
            if (scene.m_Registry.valid(e) && scene.m_Registry.all_of<NavMeshAgentComponent>(e))
                scene.m_Registry.get<NavMeshAgentComponent>(e).IsStopped = stopped;
        }

        void Update(Scene& scene, float dt)
        {
            auto& reg = scene.m_Registry;
            NavMesh& nav = scene.GetNavMesh();
            if (dt <= 0.0f)
                return;

            // Carve engeller: ağda delik (her kare yeniden; sürüm değişince ajanlar yolu yeniler)
            const uint64_t blockedBefore = nav.GetBlockedVersion();
            if (nav.IsBaked())
            {
                nav.ClearBlocked();
                for (auto o : reg.view<NavMeshObstacleComponent, TransformComponent>())
                {
                    const auto& ob = reg.get<NavMeshObstacleComponent>(o);
                    if (!ob.Carve || !scene.IsEntityActive(o))
                        continue;
                    nav.BlockBox(reg.get<TransformComponent>(o).WorldTransform, ob.Center, ob.Size, nav.GetSettings().AgentRadius);
                }
            }
            const bool blockersChanged = nav.GetBlockedVersion() != blockedBefore;

            auto agents = reg.view<NavMeshAgentComponent, TransformComponent>();
            std::vector<entt::entity> list(agents.begin(), agents.end());
            for (auto e : list)
            {
                if (!scene.IsEntityActive(e))
                    continue;
                auto& a = agents.get<NavMeshAgentComponent>(e);
                auto& t = agents.get<TransformComponent>(e);
                glm::vec3 pos = t.Position - glm::vec3(0.0f, a.BaseOffset, 0.0f);

                // Yol üstünde engel belirdiyse yeniden hesapla
                a.RepathTimer -= dt;
                if (a.HasPath && !a.OnLink && blockersChanged && a.RepathTimer <= 0.0f)
                {
                    a.RepathTimer = 0.25f;
                    SetDestination(scene, (uint32_t)e, a.Destination);
                }

                if (a.IsStopped || !a.HasPath || a.CurrentPathIndex >= (int)a.PathPoints.size())
                {
                    // Dur: hız sönümlenir
                    float speed = glm::length(a.Velocity);
                    if (speed > 0.0f)
                    {
                        speed = std::max(0.0f, speed - std::max(a.Acceleration, 0.1f) * dt);
                        a.Velocity = speed > 0.0f ? glm::normalize(a.Velocity) * speed : glm::vec3(0.0f);
                        pos += a.Velocity * dt;
                    }
                    a.RemainingDistance = 0.0f;
                    float y;
                    if (nav.IsBaked() && nav.GetHeight(pos, y)) pos.y = y;
                    t.Position = pos + glm::vec3(0.0f, a.BaseOffset, 0.0f);
                    continue;
                }

                // Bağlantı geçişi (atlama yayı / düz)
                if (a.OnLink)
                {
                    const float len = std::max(glm::length(a.LinkEnd - a.LinkStart), 0.01f);
                    a.LinkT = std::min(1.0f, a.LinkT + dt * std::max(a.Speed, 0.1f) / len);
                    glm::vec3 p = glm::mix(a.LinkStart, a.LinkEnd, a.LinkT);
                    const int linkIndex = a.PathPoints[(size_t)a.CurrentPathIndex].LinkIndex;
                    const bool jump = linkIndex >= 0 && linkIndex < (int)nav.GetLinks().size() && nav.GetLinks()[(size_t)linkIndex].Area == NavArea_Jump;
                    if (jump)
                        p.y += 4.0f * std::clamp(len * 0.25f, 0.3f, 2.0f) * a.LinkT * (1.0f - a.LinkT);
                    a.Velocity = (a.LinkEnd - a.LinkStart) / len * a.Speed;
                    t.Position = p + glm::vec3(0.0f, a.BaseOffset, 0.0f);
                    if (a.LinkT >= 1.0f)
                    {
                        a.OnLink = false;
                        ++a.CurrentPathIndex;
                    }
                    continue;
                }

                const NavPathPoint& target = a.PathPoints[(size_t)a.CurrentPathIndex];
                if (target.LinkIndex >= 0)
                {
                    if (!a.AutoTraverseLinks)
                    {
                        a.Velocity = glm::vec3(0.0f);
                        continue;
                    }
                    a.OnLink = true;
                    a.LinkT = 0.0f;
                    a.LinkStart = pos;
                    a.LinkEnd = target.Position;
                    continue;
                }

                const bool isLast = a.CurrentPathIndex == (int)a.PathPoints.size() - 1;
                glm::vec3 to = target.Position - pos;
                to.y = 0.0f;
                const float dist = glm::length(to);
                a.RemainingDistance = RemainingDistance(a, pos);
                const float arrive = std::max(a.StoppingDistance, 0.05f);
                if (isLast && a.RemainingDistance <= arrive)
                {
                    a.HasPath = false;
                    a.Velocity = glm::vec3(0.0f);
                    a.RemainingDistance = 0.0f;
                    continue;
                }
                // Köşeye varıldı ya da geçildi (köşeden sonraki bölümün yönünde ilerideyiz)
                if (!isLast)
                {
                    const glm::vec3 nextPoint = a.PathPoints[(size_t)a.CurrentPathIndex + 1].Position;
                    glm::vec3 seg = nextPoint - target.Position;
                    seg.y = 0.0f;
                    const bool passed = glm::length(seg) > 1e-4f && glm::dot(pos - target.Position, seg) > 0.0f && dist < 1.0f;
                    if (dist < std::max(0.15f, nav.GetSettings().CellSize) || passed)
                    {
                        ++a.CurrentPathIndex;
                        continue;
                    }
                }

                // İstenen hız: varışta yumuşak frenleme
                float desiredSpeed = std::max(a.Speed, 0.0f);
                if (a.AutoBraking && isLast)
                    desiredSpeed = std::min(desiredSpeed, std::sqrt(2.0f * std::max(a.Acceleration, 0.1f) * std::max(a.RemainingDistance - a.StoppingDistance, 0.0f)) + 0.05f);
                glm::vec3 desired = dist > 1e-5f ? to / dist * desiredSpeed : glm::vec3(0.0f);

                // Ajanlar arası kaçınma (ayrılma kuvveti; düşük öncelikli ajan kenara çekilir)
                if (a.AvoidOtherAgents)
                    for (auto o : list)
                    {
                        if (o == e || !scene.IsEntityActive(o))
                            continue;
                        const auto& ob = agents.get<NavMeshAgentComponent>(o);
                        if (ob.AvoidancePriority > a.AvoidancePriority)
                            continue;
                        glm::vec3 d = pos - (agents.get<TransformComponent>(o).Position - glm::vec3(0.0f, ob.BaseOffset, 0.0f));
                        d.y = 0.0f;
                        const float minDist = a.Radius + ob.Radius;
                        const float l = glm::length(d);
                        if (l < minDist * 1.25f && l > 1e-4f)
                            desired += d / l * (minDist * 1.25f - l) / (minDist * 1.25f) * desiredSpeed * 1.5f;
                    }

                // İvme sınırı
                glm::vec3 dv = desired - a.Velocity;
                dv.y = 0.0f;
                const float maxDv = std::max(a.Acceleration, 0.1f) * dt;
                const float dvl = glm::length(dv);
                if (dvl > maxDv)
                    dv *= maxDv / dvl;
                a.Velocity += dv;
                glm::vec3 next = pos + a.Velocity * dt;

                // Ağın üstünde kal: yeni konum ağ dışındaysa duvar boyunca kay (önce x sonra z ekseni)
                if (nav.IsBaked())
                {
                    const float cs = nav.GetSettings().CellSize;
                    auto onMesh = [&](const glm::vec3& p, glm::vec3& snapped) {
                        return nav.SamplePosition(p, cs * 3.0f, snapped) && std::fabs(snapped.x - p.x) < 1e-3f && std::fabs(snapped.z - p.z) < 1e-3f;
                    };
                    glm::vec3 snapped;
                    if (onMesh(next, snapped))
                        next = snapped;
                    else
                    {
                        glm::vec3 slideX(next.x, pos.y, pos.z), slideZ(pos.x, pos.y, next.z), s2;
                        if (onMesh(slideX, s2)) { next = s2; a.Velocity.z = 0.0f; }
                        else if (onMesh(slideZ, s2)) { next = s2; a.Velocity.x = 0.0f; }
                        else if (nav.SamplePosition(next, cs * 3.0f, s2)) next = s2;   // ağa geri çek
                    }
                }
                pos = next;

                // Hareket yönüne dön (açısal hız sınırı; y ekseni)
                const glm::vec3 v = a.Velocity;
                if (v.x * v.x + v.z * v.z > 1e-4f)
                {
                    const float targetYaw = std::atan2(v.x, v.z);
                    const float delta = WrapAngle(targetYaw - t.Rotation.y);
                    const float maxStep = glm::radians(std::max(a.AngularSpeed, 1.0f)) * dt;
                    t.Rotation.y = WrapAngle(t.Rotation.y + std::clamp(delta, -maxStep, maxStep));
                }
                t.Position = pos + glm::vec3(0.0f, a.BaseOffset, 0.0f);
            }
        }

        void DrawDebug(const NavMesh& nav, const glm::mat4& viewProjection)
        {
            if (!nav.IsBaked())
                return;
            if (!s_Debug)
                s_Debug = new DebugState();
            DebugState& d = *s_Debug;
            if (!d.DebugShader)
                d.DebugShader = Shader::Create("Engine/src/Shaders/Editor/NavDebug.cqsh");
            if (!d.DebugShader || !d.DebugShader->GetRendererID())
                return;
            if (!d.Vao)
            {
                glCreateVertexArrays(1, &d.Vao);
                glCreateBuffers(1, &d.Vbo);
                glVertexArrayVertexBuffer(d.Vao, 0, d.Vbo, 0, sizeof(float) * 7);
                glEnableVertexArrayAttrib(d.Vao, 0);
                glVertexArrayAttribFormat(d.Vao, 0, 3, GL_FLOAT, GL_FALSE, 0);
                glVertexArrayAttribBinding(d.Vao, 0, 0);
                glEnableVertexArrayAttrib(d.Vao, 1);
                glVertexArrayAttribFormat(d.Vao, 1, 4, GL_FLOAT, GL_FALSE, sizeof(float) * 3);
                glVertexArrayAttribBinding(d.Vao, 1, 0);
            }
            if (d.Source != &nav || d.Version != nav.GetVersion() || d.BlockedVersion != nav.GetBlockedVersion())
            {
                d.Source = &nav;
                d.Version = nav.GetVersion();
                d.BlockedVersion = nav.GetBlockedVersion();
                std::vector<float> verts;
                verts.reserve(nav.GetCellCount() * 6 * 7);
                const float h = nav.GetSettings().CellSize * 0.5f * 0.96f;
                auto push = [&](const glm::vec3& p, const glm::vec4& c) {
                    verts.insert(verts.end(), { p.x, p.y, p.z, c.r, c.g, c.b, c.a });
                };
                for (size_t i = 0; i < nav.GetCellCount(); ++i)
                {
                    const auto& cell = nav.GetCell((int)i);
                    if (cell.Area == NavArea_NotWalkable)
                        continue;
                    glm::vec4 col = nav.IsBlocked((int)i) ? glm::vec4(0.9f, 0.25f, 0.2f, 0.45f)
                                  : cell.Area == NavArea_Walkable ? glm::vec4(0.2f, 0.55f, 1.0f, 0.35f)
                                  : cell.Area == NavArea_Jump ? glm::vec4(0.95f, 0.75f, 0.2f, 0.45f)
                                  : glm::vec4(0.3f + 0.1f * (float)(cell.Area % 5), 0.9f, 0.4f, 0.4f);
                    const glm::vec3 p = nav.CellPosition((int)i) + glm::vec3(0.0f, 0.03f, 0.0f);
                    const glm::vec3 a(p.x - h, p.y, p.z - h), b(p.x + h, p.y, p.z - h), c(p.x + h, p.y, p.z + h), e(p.x - h, p.y, p.z + h);
                    push(a, col); push(b, col); push(c, col);
                    push(a, col); push(c, col); push(e, col);
                }
                // Bağlantılar: ince yükselen yay şeridi yerine iki uçta işaret (dikdörtgen sütun)
                for (const auto& l : nav.GetLinks())
                    for (const glm::vec3& q : { l.Start, l.End })
                    {
                        const glm::vec4 col(1.0f, 0.85f, 0.2f, 0.8f);
                        const float s = h * 1.5f;
                        const glm::vec3 a(q.x - s, q.y + 0.05f, q.z - s), b(q.x + s, q.y + 0.05f, q.z - s), c(q.x + s, q.y + 0.05f, q.z + s), e(q.x - s, q.y + 0.05f, q.z + s);
                        push(a, col); push(b, col); push(c, col);
                        push(a, col); push(c, col); push(e, col);
                    }
                d.Count = (GLsizei)(verts.size() / 7);
                glNamedBufferData(d.Vbo, (GLsizeiptr)(verts.size() * sizeof(float)), verts.data(), GL_DYNAMIC_DRAW);
            }
            if (d.Count == 0)
                return;
            GLboolean blend = glIsEnabled(GL_BLEND), depthTest = glIsEnabled(GL_DEPTH_TEST), cull = glIsEnabled(GL_CULL_FACE);
            GLboolean depthMask;
            glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glEnable(GL_DEPTH_TEST);
            glDepthFunc(GL_LEQUAL);
            glDepthMask(GL_FALSE);
            glDisable(GL_CULL_FACE);
            for (GLuint i = 1; i < 4; ++i)
                glColorMaski(i, GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
            d.DebugShader->Bind();
            d.DebugShader->SetMat4("u_ViewProjection", viewProjection);
            glBindVertexArray(d.Vao);
            glDrawArrays(GL_TRIANGLES, 0, d.Count);
            glBindVertexArray(0);
            for (GLuint i = 1; i < 4; ++i)
                glColorMaski(i, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
            glDepthMask(depthMask);
            if (!blend) glDisable(GL_BLEND);
            if (!depthTest) glDisable(GL_DEPTH_TEST);
            if (cull) glEnable(GL_CULL_FACE);
        }

        void Shutdown()
        {
            if (s_Debug)
            {
                if (s_Debug->Vao) glDeleteVertexArrays(1, &s_Debug->Vao);
                if (s_Debug->Vbo) glDeleteBuffers(1, &s_Debug->Vbo);
                delete s_Debug;
                s_Debug = nullptr;
            }
            delete s_Cache;
            s_Cache = nullptr;
        }
    }
}
