#include "CameraRig.h"
#include "Scene/Scene.h"
#include "Scene/Entity.h"
#include "Scene/Components.h"
#include "Scene/ComponentRegistry.h"
#include "Core/Input/GameInput.h"
#include "Core/Input/Input.h"
#include "Core/Math/Math.h"
#include "Core/PhysicsSystem/Physics3D/PhysicsWorld3D.h"
#include "Scene/PhysicsComponents.h"

#include <IconsFontAwesome6.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <vector>

namespace Conqueror
{
    namespace
    {
        struct ActiveImpulse
        {
            glm::vec3 Position{ 0.0f };
            float Amplitude = 1.0f, Frequency = 12.0f, Duration = 0.4f, Radius = 30.0f;
            float Elapsed = 0.0f;
            float Phase = 0.0f;
        };

        std::unordered_map<const Scene*, std::vector<ActiveImpulse>>& Impulses()
        {
            static std::unordered_map<const Scene*, std::vector<ActiveImpulse>> s_Impulses;
            return s_Impulses;
        }

        // Ucuz, yumuşak 1B gürültü (değer gürültüsü + kosinüs ara değerleme)
        float Noise1(float x)
        {
            auto hash = [](int n) {
                n = (n << 13) ^ n;
                return 1.0f - (float)((n * (n * n * 15731 + 789221) + 1376312589) & 0x7fffffff) / 1073741824.0f;
            };
            int i = (int)std::floor(x);
            float f = x - (float)i;
            float u = (1.0f - std::cos(f * 3.14159265f)) * 0.5f;
            return hash(i) * (1.0f - u) + hash(i + 1) * u;
        }

        bool WorldPose(Scene& scene, uint64_t uuid, glm::vec3& position, glm::quat& rotation)
        {
            if (!uuid) return false;
            Entity e = scene.FindEntityByUUID(uuid);
            if (!e) return false;
            glm::vec3 s, r;
            Math::DecomposeTransform(scene.ComputeWorldMatrix((entt::entity)e), position, r, s);
            rotation = glm::quat(r);
            return true;
        }

        // Varlığın dünya kutusu: Box Collider'ı varsa onun kutusu (ofset + boyut x ölçek), yoksa konum + ölçek
        bool WorldBox(Scene& scene, uint64_t uuid, glm::vec3& mn, glm::vec3& mx)
        {
            Entity e = scene.FindEntityByUUID(uuid);
            if (!e || !e.HasComponent<TransformComponent>())
                return false;
            const glm::mat4 w = scene.ComputeWorldMatrix((entt::entity)e);
            glm::vec3 c(0.0f), size(1.0f);
            if (e.HasComponent<BoxColliderComponent>())
            {
                c = e.GetComponent<BoxColliderComponent>().Offset;
                size = e.GetComponent<BoxColliderComponent>().Size;
            }
            // Döndürülmüş kutunun eksen hizalı sınırları: 8 köşe
            mn = glm::vec3(1e30f);
            mx = glm::vec3(-1e30f);
            for (int i = 0; i < 8; ++i)
            {
                const glm::vec3 corner = c + 0.5f * size * glm::vec3((i & 1) ? 1.0f : -1.0f, (i & 2) ? 1.0f : -1.0f, (i & 4) ? 1.0f : -1.0f);
                const glm::vec3 p = glm::vec3(w * glm::vec4(corner, 1.0f));
                mn = glm::min(mn, p);
                mx = glm::max(mx, p);
            }
            return true;
        }

        // 2B şekil (dünya XY): Polygon Collider 2D noktaları, Box Collider 2D ya da konum + ölçek kutusu
        bool WorldShape2D(Scene& scene, uint64_t uuid, std::vector<glm::vec2>& out)
        {
            Entity e = scene.FindEntityByUUID(uuid);
            if (!e || !e.HasComponent<TransformComponent>())
                return false;
            const glm::mat4 w = scene.ComputeWorldMatrix((entt::entity)e);
            std::vector<glm::vec2> local;
            if (e.HasComponent<PolygonCollider2DComponent>())
            {
                const auto& pc = e.GetComponent<PolygonCollider2DComponent>();
                for (const glm::vec2& p : pc.Points)
                    local.push_back(p + pc.Offset);
            }
            else
            {
                glm::vec2 c(0.0f), s(1.0f);
                if (e.HasComponent<BoxCollider2DComponent>())
                {
                    c = e.GetComponent<BoxCollider2DComponent>().Offset;
                    s = e.GetComponent<BoxCollider2DComponent>().Size;
                }
                local = { c + glm::vec2(-0.5f, -0.5f) * s, c + glm::vec2(0.5f, -0.5f) * s, c + glm::vec2(0.5f, 0.5f) * s, c + glm::vec2(-0.5f, 0.5f) * s };
            }
            if (local.size() < 3)
                return false;
            out.clear();
            for (const glm::vec2& p : local)
                out.push_back(glm::vec2(w * glm::vec4(p, 0.0f, 1.0f)));
            return true;
        }

        bool InsidePolygon(const std::vector<glm::vec2>& poly, const glm::vec2& p)
        {
            bool inside = false;
            for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++)
                if (((poly[i].y > p.y) != (poly[j].y > p.y)) && (p.x < (poly[j].x - poly[i].x) * (p.y - poly[i].y) / (poly[j].y - poly[i].y + 1e-12f) + poly[i].x))
                    inside = !inside;
            return inside;
        }

        glm::vec2 ClosestOnPolygon(const std::vector<glm::vec2>& poly, const glm::vec2& p)
        {
            glm::vec2 best = poly[0];
            float bestD = 1e30f;
            for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++)
            {
                const glm::vec2 a = poly[j], b = poly[i], ab = b - a;
                const float t = glm::clamp(glm::dot(p - a, ab) / std::max(glm::dot(ab, ab), 1e-12f), 0.0f, 1.0f);
                const glm::vec2 q = a + ab * t;
                const float d = glm::dot(p - q, p - q);
                if (d < bestD) { bestD = d; best = q; }
            }
            return best;
        }

        float Damp(float damping, float dt)
        {
            return damping <= 1e-4f ? 1.0f : 1.0f - std::exp(-dt / damping);
        }

        glm::vec3 ImpulseDisplacement(const Scene& scene, const glm::vec3& at, float gain, bool use2DDistance = false, float frequencyGain = 1.0f)
        {
            glm::vec3 total(0.0f);
            auto it = Impulses().find(&scene);
            if (it == Impulses().end() || gain <= 0.0f)
                return total;
            for (const auto& im : it->second)
            {
                float d = use2DDistance ? glm::distance(glm::vec2(at), glm::vec2(im.Position)) : glm::distance(at, im.Position);
                if (d >= im.Radius) continue;
                float falloff = 1.0f - d / std::max(im.Radius, 1e-3f);
                float env = 1.0f - std::clamp(im.Elapsed / std::max(im.Duration, 1e-3f), 0.0f, 1.0f);
                env *= env;
                float w = 6.2831853f * im.Frequency * std::max(frequencyGain, 0.0f) * im.Elapsed + im.Phase;
                total += glm::vec3(std::sin(w), std::sin(w * 1.31f + 1.7f), std::sin(w * 0.87f + 4.1f)) * im.Amplitude * env * falloff * gain;
            }
            return total;
        }
    }

    namespace CameraRig
    {
        void GenerateImpulseAt(Scene& scene, const glm::vec3& position, float amplitude, float frequency, float duration, float radius)
        {
            ActiveImpulse im;
            im.Position = position;
            im.Amplitude = amplitude;
            im.Frequency = frequency;
            im.Duration = std::max(duration, 0.01f);
            im.Radius = std::max(radius, 0.01f);
            im.Phase = (float)(Impulses()[&scene].size() * 1.618);
            Impulses()[&scene].push_back(im);
        }

        void GenerateImpulse(Scene& scene, uint32_t sourceEntity, float strength)
        {
            Entity e{ (entt::entity)sourceEntity, &scene };
            if (!scene.m_Registry.valid((entt::entity)sourceEntity) || !e.HasComponent<ImpulseSourceComponent>())
                return;
            const auto& src = e.GetComponent<ImpulseSourceComponent>();
            glm::vec3 p = glm::vec3(scene.ComputeWorldMatrix((entt::entity)sourceEntity)[3]);
            GenerateImpulseAt(scene, p, src.Amplitude * strength, src.Frequency, src.Duration, src.Radius);
        }

        void OnCollision(Scene& scene, uint32_t entity, float impulse)
        {
            Entity e{ (entt::entity)entity, &scene };
            if (!scene.m_Registry.valid((entt::entity)entity) || !e.HasComponent<ImpulseSourceComponent>())
                return;
            if (!e.GetComponent<ImpulseSourceComponent>().GenerateOnCollision)
                return;
            // Çarpışma darbesi (N·s) kütleye göre kabaca 0..1 güce çevrilir
            GenerateImpulse(scene, entity, std::clamp(impulse / 50.0f, 0.1f, 2.0f));
        }

        void Reset(Scene* scene)
        {
            if (scene) Impulses().erase(scene);
            else Impulses().clear();
        }

        void LateUpdate(Scene& scene, float dt)
        {
            auto& reg = scene.m_Registry;

            // Darbeler: süresi biten silinir; "başlangıçta üret" kaynakları ilk karede
            auto& impulses = Impulses()[&scene];
            for (auto& im : impulses) im.Elapsed += dt;
            impulses.erase(std::remove_if(impulses.begin(), impulses.end(), [](const ActiveImpulse& im) { return im.Elapsed >= im.Duration; }), impulses.end());
            for (auto h : reg.view<ImpulseSourceComponent>())
            {
                auto& src = reg.get<ImpulseSourceComponent>(h);
                if (src.GenerateOnStart && scene.IsEntityActive(h))
                {
                    src.GenerateOnStart = false;   // yalnızca bir kez (çalışma zamanı kopyasında)
                    GenerateImpulse(scene, (uint32_t)h, 1.0f);
                }
            }

            // ── Target Group: hedeflerin ağırlıklı merkezi ve onları kapsayan küre (kameralardan önce) ──
            for (auto h : reg.view<TransformComponent, CinemachineTargetGroupComponent>())
            {
                auto& g = reg.get<CinemachineTargetGroupComponent>(h);
                const uint64_t ids[4] = { g.Target1, g.Target2, g.Target3, g.Target4 };
                const float ws[4] = { g.Weight1, g.Weight2, g.Weight3, g.Weight4 };
                const float rs[4] = { g.Radius1, g.Radius2, g.Radius3, g.Radius4 };
                glm::vec3 ps[4];
                bool ok[4];
                glm::vec3 sum(0.0f);
                float total = 0.0f;
                for (int k = 0; k < 4; ++k)
                {
                    glm::quat r;
                    ok[k] = ws[k] > 0.0f && WorldPose(scene, ids[k], ps[k], r);
                    if (ok[k]) { sum += ps[k] * ws[k]; total += ws[k]; }
                }
                if (total <= 0.0f)
                    continue;
                const glm::vec3 center = sum / total;
                float radius = 0.0f;
                for (int k = 0; k < 4; ++k)
                    if (ok[k])
                        radius = std::max(radius, glm::length(ps[k] - center) + std::max(rs[k], 0.0f));
                g.BoundingRadius = radius;
                scene.SetEntityWorldPose(Entity{ h, &scene }, center, nullptr);
            }

            // ── Sanal kameralar ──
            uint32_t live = 0xFFFFFFFFu;
            int livePriority = -2147483647;
            const glm::vec2 mouseDelta = GameInput::GetMouseDelta();
            for (auto h : reg.view<TransformComponent, VirtualCameraComponent>())
            {
                auto& vc = reg.get<VirtualCameraComponent>(h);
                vc.Live = false;
                if (!scene.IsEntityActive(h))
                    continue;
                glm::vec3 selfPos, selfScale, selfEuler;
                Math::DecomposeTransform(scene.ComputeWorldMatrix(h), selfPos, selfEuler, selfScale);
                glm::quat selfRot(selfEuler);
                if (!vc.Initialized)
                {
                    vc.StatePosition = selfPos;
                    vc.StateRotation = selfRot;
                    vc.Initialized = true;
                }

                glm::vec3 tPos(0.0f);
                glm::quat tRot(1, 0, 0, 0);
                const bool hasFollow = WorldPose(scene, vc.Follow, tPos, tRot);

                // Gövde: istenen konum
                glm::vec3 desired = vc.StatePosition;
                glm::vec3 yawFwd = tRot * glm::vec3(0, 0, -1);
                yawFwd.y = 0.0f;
                glm::quat yawRot = glm::length(yawFwd) > 1e-5f ? glm::quatLookAt(glm::normalize(yawFwd), glm::vec3(0, 1, 0)) : glm::quat(1, 0, 0, 0);
                if (hasFollow)
                {
                    switch (vc.Body)
                    {
                        case VCamBody::Follow:
                        {
                            glm::vec3 off = vc.FollowOffset;
                            if (vc.Binding == VCamBinding::LockToTargetWithWorldUp) off = yawRot * off;
                            else if (vc.Binding == VCamBinding::LockToTarget) off = tRot * off;
                            desired = tPos + off;
                            break;
                        }
                        case VCamBody::ThirdPerson:
                        {
                            glm::vec3 pivot = tPos + tRot * vc.ShoulderOffset;
                            glm::vec3 back = tRot * glm::vec3(0, 0, 1);
                            desired = pivot + back * vc.CameraDistance;
                            if (vc.AvoidObstacles)
                                if (PhysicsWorld3D* physics = scene.GetPhysicsWorld3D(); physics && physics->IsRunning())
                                {
                                    Physics3D::RaycastHit hit;
                                    Entity target = scene.FindEntityByUUID(vc.Follow);
                                    uint32_t ignore = target ? (uint32_t)(entt::entity)target : Physics3D::NoEntity;
                                    if (physics->SphereCast(pivot, std::max(vc.CameraRadius, 0.01f), back, vc.CameraDistance, hit, 0xFFFFFFFFu, ignore))
                                        desired = pivot + back * std::max(hit.Distance - 0.05f, 0.1f);
                                }
                            break;
                        }
                        case VCamBody::Orbital:
                        {
                            if (vc.OrbitInput && (Input::IsMouseButtonPressed(Mouse::ButtonRight) || Input::GetCursorMode() != 0))
                            {
                                vc.OrbitYaw -= mouseDelta.x * vc.OrbitSpeed;
                                vc.OrbitPitch = std::clamp(vc.OrbitPitch + mouseDelta.y * vc.OrbitSpeed, -80.0f, 80.0f);
                            }
                            float yaw = glm::radians(vc.OrbitYaw), pitch = glm::radians(vc.OrbitPitch);
                            glm::vec3 dir(std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch));
                            desired = tPos + glm::vec3(0, vc.OrbitHeight, 0) + dir * vc.OrbitRadius;
                            break;
                        }
                        case VCamBody::DoNothing:
                            desired = selfPos;
                            break;
                    }
                }
                else if (vc.Body == VCamBody::DoNothing || !vc.Follow)
                    desired = selfPos;

                vc.StatePosition.x += (desired.x - vc.StatePosition.x) * Damp(vc.PositionDamping.x, dt);
                vc.StatePosition.y += (desired.y - vc.StatePosition.y) * Damp(vc.PositionDamping.y, dt);
                vc.StatePosition.z += (desired.z - vc.StatePosition.z) * Damp(vc.PositionDamping.z, dt);

                // Nişan: istenen dönüş
                glm::quat aimRot = vc.StateRotation;
                glm::vec3 lPos;
                glm::quat lRot;
                const bool hasLook = WorldPose(scene, vc.LookAt ? vc.LookAt : vc.Follow, lPos, lRot);
                switch (vc.Aim)
                {
                    case VCamAim::Composer:
                    case VCamAim::HardLookAt:
                        if (hasLook)
                        {
                            glm::vec3 to = lPos + vc.LookAtOffset - vc.StatePosition;
                            if (glm::length(to) > 1e-4f)
                            {
                                glm::quat look = glm::quatLookAt(glm::normalize(to), glm::vec3(0, 1, 0));
                                aimRot = vc.Aim == VCamAim::HardLookAt ? look : glm::slerp(vc.StateRotation, look, Damp(vc.AimDamping, dt));
                            }
                        }
                        break;
                    case VCamAim::SameAsFollowTarget:
                        if (hasFollow) aimRot = tRot;
                        break;
                    case VCamAim::DoNothing:
                        aimRot = selfRot;
                        break;
                }
                vc.StateRotation = glm::normalize(aimRot);

                // ── Ekler: son pozu düzeltir (gövdenin sönümlü durumu korunur) ──
                vc.FinalPosition = vc.StatePosition;
                vc.FinalRotation = vc.StateRotation;
                vc.EffectiveFov = -1.0f;
                bool moved = false;
                const glm::vec3 lookPoint = hasLook ? lPos + vc.LookAtOffset : (hasFollow ? tPos : vc.StatePosition + vc.StateRotation * glm::vec3(0, 0, -1));
                if (auto* gf = reg.try_get<CinemachineGroupFramingComponent>(h))
                {
                    // Takip ya da bakış hedefi bir Target Group ise: grubun küresi ekranın FramingSize kadarını kaplayacak uzaklık (Dolly)
                    for (uint64_t id : { vc.LookAt, vc.Follow })
                    {
                        Entity ge = scene.FindEntityByUUID(id);
                        if (!ge || !ge.HasComponent<CinemachineTargetGroupComponent>())
                            continue;
                        const float r = ge.GetComponent<CinemachineTargetGroupComponent>().BoundingRadius;
                        if (r <= 0.0f)
                            break;
                        const glm::vec3 gc = glm::vec3(scene.ComputeWorldMatrix((entt::entity)ge)[3]);
                        const float half = glm::radians(std::clamp(vc.FieldOfView, 1.0f, 179.0f)) * 0.5f;
                        const float want = std::clamp(r / (std::max(gf->FramingSize, 0.05f) * std::tan(half)), gf->MinDistance, std::max(gf->MaxDistance, gf->MinDistance));
                        gf->Distance = gf->Distance < 0.0f ? want : gf->Distance + (want - gf->Distance) * Damp(gf->Damping, dt);
                        glm::vec3 dir = vc.FinalPosition - gc;
                        if (glm::length(dir) < 1e-4f)
                            dir = vc.FinalRotation * glm::vec3(0, 0, 1);
                        vc.FinalPosition = gc + glm::normalize(dir) * gf->Distance;
                        moved = true;
                        break;
                    }
                }
                if (auto* cf = reg.try_get<CinemachineConfiner3DComponent>(h))
                {
                    glm::vec3 mn, mx;
                    if (WorldBox(scene, cf->BoundingVolume, mn, mx))
                    {
                        glm::vec3 p = glm::clamp(vc.FinalPosition, mn, mx);
                        if (cf->SlowingDistance > 0.0f)
                        {
                            // Kenara yaklaşırken yumuşak: kutunun içinde kenara SlowingDistance kadar yaklaşınca sınıra doğru sıkışır
                            const glm::vec3 lo = mn + glm::vec3(cf->SlowingDistance), hi = mx - glm::vec3(cf->SlowingDistance);
                            for (int a = 0; a < 3; ++a)
                            {
                                if (lo[a] >= hi[a]) continue;
                                if (vc.FinalPosition[a] < lo[a]) p[a] = lo[a] - cf->SlowingDistance * std::tanh((lo[a] - vc.FinalPosition[a]) / cf->SlowingDistance);
                                if (vc.FinalPosition[a] > hi[a]) p[a] = hi[a] + cf->SlowingDistance * std::tanh((vc.FinalPosition[a] - hi[a]) / cf->SlowingDistance);
                            }
                        }
                        moved = moved || glm::length(p - vc.FinalPosition) > 1e-6f;
                        vc.FinalPosition = p;
                    }
                }
                if (auto* cf2 = reg.try_get<CinemachineConfiner2DComponent>(h))
                {
                    std::vector<glm::vec2> poly;
                    if (WorldShape2D(scene, cf2->BoundingShape, poly))
                    {
                        const glm::vec2 xy(vc.FinalPosition);
                        if (!InsidePolygon(poly, xy))
                        {
                            const glm::vec2 q = ClosestOnPolygon(poly, xy);
                            vc.FinalPosition.x = q.x;
                            vc.FinalPosition.y = q.y;
                            moved = true;
                        }
                    }
                }
                if (auto* de = reg.try_get<CinemachineDeoccluderComponent>(h); de && (hasLook || hasFollow))
                {
                    if (PhysicsWorld3D* physics = scene.GetPhysicsWorld3D(); physics && physics->IsRunning())
                    {
                        const glm::vec3 target = hasLook ? lPos + vc.LookAtOffset : tPos;
                        const glm::vec3 toCam = vc.FinalPosition - target;
                        const float dist = glm::length(toCam);
                        if (dist > 1e-3f)
                        {
                            const glm::vec3 dir = toCam / dist;
                            Entity tgt = scene.FindEntityByUUID(vc.LookAt ? vc.LookAt : vc.Follow);
                            const uint32_t ignore = tgt ? (uint32_t)(entt::entity)tgt : Physics3D::NoEntity;
                            Physics3D::RaycastHit hit;
                            float want = 0.0f;
                            if (physics->SphereCast(target, std::max(de->CameraRadius, 0.01f), dir, dist, hit, de->CollideAgainst, ignore))
                                want = dist - std::max(hit.Distance - 0.05f, de->MinimumDistanceFromTarget);
                            // Engel gelince hemen öne, kalkınca sönümle geri
                            de->Pull = want > de->Pull ? want : de->Pull + (want - de->Pull) * Damp(de->Damping, dt);
                            if (de->Pull > 1e-4f)
                            {
                                vc.FinalPosition = target + dir * std::max(dist - de->Pull, de->MinimumDistanceFromTarget);
                                moved = true;
                            }
                        }
                    }
                }
                if (moved && hasLook && (vc.Aim == VCamAim::Composer || vc.Aim == VCamAim::HardLookAt))
                {
                    const glm::vec3 to = lookPoint - vc.FinalPosition;
                    if (glm::length(to) > 1e-4f)
                        vc.FinalRotation = glm::quatLookAt(glm::normalize(to), glm::vec3(0, 1, 0));
                }
                if (auto* fz = reg.try_get<CinemachineFollowZoomComponent>(h))
                {
                    const float d = std::max(glm::length(lookPoint - vc.FinalPosition), 1e-3f);
                    const float want = std::clamp(glm::degrees(2.0f * std::atan(std::max(fz->Width, 1e-3f) * 0.5f / d)), fz->MinFov, std::max(fz->MaxFov, fz->MinFov));
                    fz->Fov = fz->Fov < 0.0f ? want : fz->Fov + (want - fz->Fov) * Damp(fz->Damping, dt);
                    vc.EffectiveFov = fz->Fov;
                }

                // Kendi dönüşümüne ham (eklentisiz) pozu yaz: Unity gibi; düzeltilmiş poz yazılsaydı Do Nothing gövdesinde Deoccluder / Confiner
                // düzeltmesi bir sonraki karenin başlangıcı olur, engel kalkınca kamera geri dönmezdi. Gerçek kamera Final pozu kullanır
                Entity self{ h, &scene };
                scene.SetEntityWorldPose(self, vc.StatePosition, &vc.StateRotation);

                if (vc.Priority >= livePriority)
                {
                    livePriority = vc.Priority;
                    live = (uint32_t)h;
                }
            }
            if (live != 0xFFFFFFFFu)
                reg.get<VirtualCameraComponent>((entt::entity)live).Live = true;

            // ── Beyin: canlı sanal kamerayı gerçek kameraya uygula, geçişlerde harmanla ──
            for (auto h : reg.view<TransformComponent, CameraBrainComponent, CameraComponent>())
            {
                if (!scene.IsEntityActive(h) || live == 0xFFFFFFFFu)
                    continue;
                auto& brain = reg.get<CameraBrainComponent>(h);
                auto& cam = reg.get<CameraComponent>(h);
                auto& vc = reg.get<VirtualCameraComponent>((entt::entity)live);
                Entity liveEntity{ (entt::entity)live, &scene };
                uint64_t liveId = liveEntity.HasComponent<IDComponent>() ? (uint64_t)liveEntity.GetComponent<IDComponent>().ID : 0;

                glm::vec3 curPos, curScale, curEuler;
                Math::DecomposeTransform(scene.ComputeWorldMatrix(h), curPos, curEuler, curScale);
                const float curFov = glm::degrees(cam.Camera.GetPerspectiveVerticalFOV());
                if (liveId != brain.ActiveCamera)
                {
                    brain.ActiveCamera = liveId;
                    if (brain.HasPose && brain.BlendCurve != CameraBlendCurve::Cut && brain.DefaultBlend > 1e-3f)
                    {
                        brain.BlendFromPosition = curPos;
                        brain.BlendFromRotation = glm::quat(curEuler);
                        brain.BlendFromFov = curFov;
                        brain.BlendProgress = 0.0f;
                    }
                    else
                        brain.BlendProgress = 1.0f;
                }
                brain.BlendProgress = std::min(1.0f, brain.BlendProgress + dt / std::max(brain.DefaultBlend, 1e-3f));
                float t = brain.BlendProgress;
                if (brain.BlendCurve == CameraBlendCurve::EaseInOut)
                    t = t * t * (3.0f - 2.0f * t);

                // Gürültü ve sarsıntı canlı kameranın üstüne eklenir (harmanlanmış pozda)
                vc.NoiseTime += dt * std::max(vc.NoiseFrequency, 0.0f);
                // Impulse Listener varsa onun ayarları (Unity), yoksa sanal kameranın Impulse Gain'i
                glm::vec3 shake;
                if (const auto* li = reg.try_get<CinemachineImpulseListenerComponent>((entt::entity)live))
                {
                    shake = ImpulseDisplacement(scene, vc.FinalPosition, li->Gain, li->Use2DDistance, li->FrequencyGain);
                    if (li->UseCameraSpace)
                        shake = vc.FinalRotation * shake;
                }
                else
                    shake = ImpulseDisplacement(scene, vc.FinalPosition, vc.ImpulseGain);
                glm::vec3 noiseEuler(0.0f);
                if (vc.NoiseAmplitude > 0.0f)
                    noiseEuler = glm::radians(glm::vec3(Noise1(vc.NoiseTime * 2.1f), Noise1(vc.NoiseTime * 1.7f + 37.0f), Noise1(vc.NoiseTime * 1.3f + 71.0f) * 0.5f) * vc.NoiseAmplitude);
                glm::quat targetRot = vc.FinalRotation * glm::quat(noiseEuler) * glm::angleAxis(glm::radians(vc.Dutch), glm::vec3(0, 0, 1));
                // Sarsıntının küçük bir kısmı açısal (daha inandırıcı)
                targetRot = targetRot * glm::quat(glm::vec3(shake.y, shake.x, 0.0f) * 0.02f);
                glm::vec3 targetPos = vc.FinalPosition + shake;

                glm::vec3 pos = brain.BlendProgress >= 1.0f ? targetPos : glm::mix(brain.BlendFromPosition, targetPos, t);
                glm::quat rot = brain.BlendProgress >= 1.0f ? targetRot : glm::slerp(brain.BlendFromRotation, targetRot, t);
                scene.SetEntityWorldPose(Entity{ h, &scene }, pos, &rot);
                if (brain.ApplyFieldOfView && cam.Camera.GetProjectionType() == SceneCamera::ProjectionType::Perspective)
                {
                    const float liveFov = vc.EffectiveFov > 0.0f ? vc.EffectiveFov : vc.FieldOfView;
                    float fov = brain.BlendProgress >= 1.0f ? liveFov : glm::mix(brain.BlendFromFov, liveFov, t);
                    cam.Camera.SetPerspectiveVerticalFOV(glm::radians(std::clamp(fov, 1.0f, 179.0f)));
                }
                brain.HasPose = true;
            }

            // Camera Brain yoksa: darbeler birincil kameranın konumuna eklenir (Camera Shake varlıkları düz kamerada da çalışır)
            static std::unordered_map<const Scene*, glm::vec3> s_PlainShake;
            if (reg.view<CameraBrainComponent>().empty())
            {
                if (Entity cam = scene.GetPrimaryCameraEntity())
                {
                    auto& tc = cam.GetComponent<TransformComponent>();
                    glm::vec3& last = s_PlainShake[&scene];
                    tc.Position -= last;
                    const glm::vec3 shake = ImpulseDisplacement(scene, glm::vec3(tc.WorldTransform[3]), 1.0f);
                    tc.Position += shake;
                    last = shake;
                }
            }
        }
    }

    template<> void Scene::OnComponentAdded<CinemachineConfiner3DComponent>(Entity, CinemachineConfiner3DComponent&) {}
    template<> void Scene::OnComponentAdded<CinemachineConfiner2DComponent>(Entity, CinemachineConfiner2DComponent&) {}
    template<> void Scene::OnComponentAdded<CinemachineDeoccluderComponent>(Entity, CinemachineDeoccluderComponent&) {}
    template<> void Scene::OnComponentAdded<CinemachineGroupFramingComponent>(Entity, CinemachineGroupFramingComponent&) {}
    template<> void Scene::OnComponentAdded<CinemachineFollowZoomComponent>(Entity, CinemachineFollowZoomComponent&) {}
    template<> void Scene::OnComponentAdded<CinemachineTargetGroupComponent>(Entity, CinemachineTargetGroupComponent&) {}
    template<> void Scene::OnComponentAdded<CinemachineImpulseListenerComponent>(Entity, CinemachineImpulseListenerComponent&) {}

    void RegisterCameraRigComponents()
    {
        const glm::vec4 kCamColor{ 0.95f, 0.45f, 0.35f, 1.0f };

        ComponentDef<VirtualCameraComponent>("Virtual Camera", "VirtualCameraComponent", "Cameras", ICON_FA_VIDEO).Color(kCamColor)
            .Describe("Cinemachine-style camera shot: follows and frames targets with damping. The highest-priority active one drives the Camera Brain.")
            .Field("Priority", "Priority", &VirtualCameraComponent::Priority).Range(-1000.0f, 1000.0f, 1.0f)
            .Field("Follow", "Follow", &VirtualCameraComponent::Follow)
            .Field("LookAt", "Look At", &VirtualCameraComponent::LookAt).Tooltip("Empty = the Follow target.")
            .Field("Body", "Body", &VirtualCameraComponent::Body).Options({ "Do Nothing", "Follow", "Third Person", "Orbital" }).Header("Body")
            .Field("Binding", "Binding Mode", &VirtualCameraComponent::Binding).Options({ "World", "Lock To Target (World Up)", "Lock To Target" })
                .VisibleIf([](const VirtualCameraComponent& c) { return c.Body == VCamBody::Follow; })
            .Field("FollowOffset", "Follow Offset", &VirtualCameraComponent::FollowOffset).Speed(0.05f)
                .VisibleIf([](const VirtualCameraComponent& c) { return c.Body == VCamBody::Follow; })
            .Field("ShoulderOffset", "Shoulder Offset", &VirtualCameraComponent::ShoulderOffset).Speed(0.02f)
                .VisibleIf([](const VirtualCameraComponent& c) { return c.Body == VCamBody::ThirdPerson; })
            .Field("CameraDistance", "Camera Distance", &VirtualCameraComponent::CameraDistance).Range(0.0f, 1000.0f, 0.05f)
                .VisibleIf([](const VirtualCameraComponent& c) { return c.Body == VCamBody::ThirdPerson; })
            .Field("AvoidObstacles", "Avoid Obstacles", &VirtualCameraComponent::AvoidObstacles)
                .VisibleIf([](const VirtualCameraComponent& c) { return c.Body == VCamBody::ThirdPerson; })
            .Field("CameraRadius", "Camera Radius", &VirtualCameraComponent::CameraRadius).Range(0.0f, 5.0f, 0.01f)
                .VisibleIf([](const VirtualCameraComponent& c) { return c.Body == VCamBody::ThirdPerson && c.AvoidObstacles; })
            .Field("OrbitRadius", "Orbit Radius", &VirtualCameraComponent::OrbitRadius).Range(0.0f, 10000.0f, 0.05f)
                .VisibleIf([](const VirtualCameraComponent& c) { return c.Body == VCamBody::Orbital; })
            .Field("OrbitHeight", "Orbit Height", &VirtualCameraComponent::OrbitHeight).Speed(0.05f)
                .VisibleIf([](const VirtualCameraComponent& c) { return c.Body == VCamBody::Orbital; })
            .Field("OrbitInput", "Mouse Orbit", &VirtualCameraComponent::OrbitInput)
                .Tooltip("Rotate with the mouse while the right button is held or the cursor is locked.")
                .VisibleIf([](const VirtualCameraComponent& c) { return c.Body == VCamBody::Orbital; })
            .Field("OrbitSpeed", "Orbit Speed", &VirtualCameraComponent::OrbitSpeed).Range(0.0f, 10.0f, 0.01f)
                .VisibleIf([](const VirtualCameraComponent& c) { return c.Body == VCamBody::Orbital; })
            .Field("OrbitYaw", "Orbit Yaw", &VirtualCameraComponent::OrbitYaw, FieldKind::Angle).Range(-360.0f, 360.0f, 0.5f).Format("%.1f°")
                .VisibleIf([](const VirtualCameraComponent& c) { return c.Body == VCamBody::Orbital; })
            .Field("OrbitPitch", "Orbit Pitch", &VirtualCameraComponent::OrbitPitch, FieldKind::Angle).Range(-80.0f, 80.0f, 0.5f).Format("%.1f°")
                .VisibleIf([](const VirtualCameraComponent& c) { return c.Body == VCamBody::Orbital; })
            .Field("PositionDamping", "Damping", &VirtualCameraComponent::PositionDamping).Speed(0.01f)
                .Tooltip("Seconds to catch up on each axis (0 = rigid).")
            .Field("Aim", "Aim", &VirtualCameraComponent::Aim).Options({ "Do Nothing", "Composer", "Hard Look At", "Same As Follow Target" }).Header("Aim")
            .Field("AimDamping", "Aim Damping", &VirtualCameraComponent::AimDamping).Range(0.0f, 10.0f, 0.01f)
                .VisibleIf([](const VirtualCameraComponent& c) { return c.Aim == VCamAim::Composer; })
            .Field("LookAtOffset", "Look At Offset", &VirtualCameraComponent::LookAtOffset).Speed(0.02f)
            .Field("FieldOfView", "Field Of View", &VirtualCameraComponent::FieldOfView, FieldKind::Angle).Range(1.0f, 179.0f, 0.5f).Format("%.1f°").Header("Lens")
            .Field("Dutch", "Dutch", &VirtualCameraComponent::Dutch, FieldKind::Angle).Range(-180.0f, 180.0f, 0.5f).Format("%.1f°")
            .Field("NoiseAmplitude", "Noise Amplitude", &VirtualCameraComponent::NoiseAmplitude).Range(0.0f, 45.0f, 0.05f).Format("%.2f°").Header("Noise")
                .Tooltip("Hand-held camera wobble.")
            .Field("NoiseFrequency", "Noise Frequency", &VirtualCameraComponent::NoiseFrequency).Range(0.0f, 50.0f, 0.05f)
            .Field("ImpulseGain", "Impulse Gain", &VirtualCameraComponent::ImpulseGain).Range(0.0f, 10.0f, 0.05f)
                .Tooltip("How strongly Impulse Sources shake this camera (0 = ignore). A Cinemachine Impulse Listener component replaces it.")
            .Field("Live", "Live", &VirtualCameraComponent::Live).ReadOnly().Header("Runtime");

        ComponentDef<CameraBrainComponent>("Camera Brain", "CameraBrainComponent", "Cameras", ICON_FA_BRAIN).Color(kCamColor)
            .Requires("CameraComponent")
            .Describe("Put on the real Camera: it shows the live Virtual Camera and blends smoothly when the shot changes.")
            .Field("DefaultBlend", "Default Blend", &CameraBrainComponent::DefaultBlend).Range(0.0f, 60.0f, 0.05f).Format("%.2f s")
            .Field("BlendCurve", "Blend Curve", &CameraBrainComponent::BlendCurve).Options({ "Ease In Out", "Linear", "Cut" })
            .Field("ApplyFieldOfView", "Apply Field Of View", &CameraBrainComponent::ApplyFieldOfView)
            .Field("BlendProgress", "Blend Progress", &CameraBrainComponent::BlendProgress).ReadOnly().Header("Runtime");

        ComponentDef<ImpulseSourceComponent>("Impulse Source", "ImpulseSourceComponent", "Cameras", ICON_FA_BURST).Color(kCamColor)
            .Describe("Camera shake emitter (explosions, landings). Trigger it with ImpulseGenerate(entity, strength), on start or on collision.")
            .Field("Amplitude", "Amplitude", &ImpulseSourceComponent::Amplitude).Range(0.0f, 100.0f, 0.01f)
            .Field("Frequency", "Frequency", &ImpulseSourceComponent::Frequency).Range(0.1f, 100.0f, 0.1f).Format("%.1f Hz")
            .Field("Duration", "Duration", &ImpulseSourceComponent::Duration).Range(0.01f, 30.0f, 0.01f).Format("%.2f s")
            .Field("Radius", "Radius", &ImpulseSourceComponent::Radius).Range(0.0f, 100000.0f, 0.1f)
            .Field("GenerateOnStart", "Generate On Start", &ImpulseSourceComponent::GenerateOnStart)
            .Field("GenerateOnCollision", "Generate On Collision", &ImpulseSourceComponent::GenerateOnCollision);
        using IL = CinemachineImpulseListenerComponent;
        ComponentDef<IL>("Cinemachine Impulse Listener", "CinemachineImpulseListenerComponent", "Cameras", ICON_FA_EAR_LISTEN).Color(kCamColor)
            .Describe("Put it on a Virtual Camera: sets how the camera reacts to Impulse Sources. It replaces the camera's Impulse Gain.")
            .Field("Gain", "Gain", &IL::Gain).Range(0.0f, 10.0f, 0.05f).Tooltip("0 = the camera ignores impulses.")
            .Field("Use2DDistance", "Use 2D Distance", &IL::Use2DDistance).Tooltip("Distance to the source ignores Z (2D games).")
            .Field("UseCameraSpace", "Use Camera Space", &IL::UseCameraSpace).Tooltip("Shake along the camera's own right / up / forward axes.")
            .Field("FrequencyGain", "Frequency Gain", &IL::FrequencyGain).Range(0.0f, 10.0f, 0.05f);

        using C3 = CinemachineConfiner3DComponent;
        ComponentDef<C3>("Cinemachine Confiner 3D", "CinemachineConfiner3DComponent", "Cameras", ICON_FA_CUBE).Color(kCamColor)
            .Describe("Keeps this Virtual Camera inside the Bounding Volume: its Box Collider box, or its position and scale.")
            .Field("BoundingVolume", "Bounding Volume", &C3::BoundingVolume)
            .Field("SlowingDistance", "Slowing Distance", &C3::SlowingDistance).Range(0.0f, 100.0f, 0.05f);
        using C2 = CinemachineConfiner2DComponent;
        ComponentDef<C2>("Cinemachine Confiner 2D", "CinemachineConfiner2DComponent", "Cameras", ICON_FA_SQUARE).Color(kCamColor)
            .Describe("Keeps this Virtual Camera inside a 2D shape (XY): the Bounding Shape's Polygon Collider 2D or Box Collider 2D.")
            .Field("BoundingShape", "Bounding Shape 2D", &C2::BoundingShape);
        using DO = CinemachineDeoccluderComponent;
        ComponentDef<DO>("Cinemachine Deoccluder", "CinemachineDeoccluderComponent", "Cameras", ICON_FA_EYE).Color(kCamColor)
            .Describe("When a collider blocks the view of the Look At (or Follow) target, the camera moves in front of it; it eases back when the view is clear.")
            .Field("CollideAgainst", "Collide Against", &DO::CollideAgainst)
            .Field("CameraRadius", "Camera Radius", &DO::CameraRadius).Range(0.0f, 5.0f, 0.01f)
            .Field("MinimumDistanceFromTarget", "Minimum Distance From Target", &DO::MinimumDistanceFromTarget).Range(0.0f, 50.0f, 0.01f)
            .Field("Damping", "Damping", &DO::Damping).Range(0.0f, 10.0f, 0.01f).Format("%.2f s");
        using GF = CinemachineGroupFramingComponent;
        ComponentDef<GF>("Cinemachine Group Framing", "CinemachineGroupFramingComponent", "Cameras", ICON_FA_USERS).Color(kCamColor)
            .Describe("When this Virtual Camera follows or looks at a Target Group, it moves to the distance where the whole group fills Framing Size of the screen.")
            .Field("FramingSize", "Framing Size", &GF::FramingSize).Range(0.05f, 1.0f, 0.01f)
            .Field("Damping", "Damping", &GF::Damping).Range(0.0f, 10.0f, 0.01f).Format("%.2f s")
            .Field("MinDistance", "Min Distance", &GF::MinDistance).Range(0.0f, 10000.0f, 0.1f)
            .Field("MaxDistance", "Max Distance", &GF::MaxDistance).Range(0.0f, 10000.0f, 0.1f);
        using FZ = CinemachineFollowZoomComponent;
        ComponentDef<FZ>("Cinemachine Follow Zoom", "CinemachineFollowZoomComponent", "Cameras", ICON_FA_MAGNIFYING_GLASS_PLUS).Color(kCamColor)
            .Describe("Changes the field of view so that Width metres around the target always look the same size on screen.")
            .Field("Width", "Width", &FZ::Width).Range(0.01f, 1000.0f, 0.05f)
            .Field("MinFov", "Min FOV", &FZ::MinFov).Range(1.0f, 179.0f, 0.5f)
            .Field("MaxFov", "Max FOV", &FZ::MaxFov).Range(1.0f, 179.0f, 0.5f)
            .Field("Damping", "Damping", &FZ::Damping).Range(0.0f, 10.0f, 0.01f).Format("%.2f s");
        using TG = CinemachineTargetGroupComponent;
        ComponentDef<TG>("Cinemachine Target Group", "CinemachineTargetGroupComponent", "Cameras", ICON_FA_PEOPLE_GROUP).Color(kCamColor)
            .Describe("A group that Virtual Cameras can follow or look at: this object moves to the weighted centre of its targets every frame; "
                      "Group Framing uses the radius that holds all of them.")
            .Field("Target1", "Target 1", &TG::Target1).Field("Weight1", "Weight 1", &TG::Weight1).Range(0.0f, 10.0f, 0.01f).Field("Radius1", "Radius 1", &TG::Radius1).Range(0.0f, 100.0f, 0.05f)
            .Field("Target2", "Target 2", &TG::Target2).Field("Weight2", "Weight 2", &TG::Weight2).Range(0.0f, 10.0f, 0.01f).Field("Radius2", "Radius 2", &TG::Radius2).Range(0.0f, 100.0f, 0.05f)
            .Field("Target3", "Target 3", &TG::Target3).Field("Weight3", "Weight 3", &TG::Weight3).Range(0.0f, 10.0f, 0.01f).Field("Radius3", "Radius 3", &TG::Radius3).Range(0.0f, 100.0f, 0.05f)
            .Field("Target4", "Target 4", &TG::Target4).Field("Weight4", "Weight 4", &TG::Weight4).Range(0.0f, 10.0f, 0.01f).Field("Radius4", "Radius 4", &TG::Radius4).Range(0.0f, 100.0f, 0.05f)
            .Field("BoundingRadius", "Bounding Radius", &TG::BoundingRadius).ReadOnly().Header("Runtime");
    }

}
