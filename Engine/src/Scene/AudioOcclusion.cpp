#include "AudioOcclusion.h"
#include "Scene/Scene.h"
#include "Scene/Entity.h"
#include "Scene/Components.h"
#include "Scene/ComponentRegistry.h"
#include "Core/Audio/AudioSource.h"
#include "Core/PhysicsSystem/Physics3D/PhysicsWorld3D.h"

#include <IconsFontAwesome6.h>

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <vector>

namespace Conqueror
{
    namespace
    {
        entt::entity RootOf(Scene& scene, entt::entity e)
        {
            Entity cur{ e, &scene };
            for (int guard = 0; guard < 256; ++guard)
            {
                Entity parent = cur.GetParent();
                if (!parent)
                    break;
                cur = parent;
            }
            return (entt::entity)cur;
        }

        // Dinleyici: ilk etkin Audio Listener, yoksa birincil kamera
        bool ListenerPosition(Scene& scene, glm::vec3& out, entt::entity& listenerEntity)
        {
            auto view = scene.m_Registry.view<TransformComponent, AudioListenerComponent>();
            for (auto e : view)
                if (view.get<AudioListenerComponent>(e).IsActive && scene.IsEntityActive(e))
                {
                    out = glm::vec3(view.get<TransformComponent>(e).WorldTransform[3]);
                    listenerEntity = e;
                    return true;
                }
            if (Entity cam = scene.GetPrimaryCameraEntity())
            {
                out = glm::vec3(cam.GetComponent<TransformComponent>().WorldTransform[3]);
                listenerEntity = (entt::entity)cam;
                return true;
            }
            return false;
        }
    }

    namespace AudioOcclusion
    {
        void Update(Scene& scene, float dt, double time)
        {
            auto& reg = scene.m_Registry;
            glm::vec3 listener(0.0f);
            entt::entity listenerEntity = entt::null;
            const bool hasListener = ListenerPosition(scene, listener, listenerEntity);
            const entt::entity listenerRoot = hasListener && reg.valid(listenerEntity) ? RootOf(scene, listenerEntity) : entt::null;

            // ---- Occlusion
            PhysicsWorld3D* physics = scene.GetPhysicsWorld3D();
            std::vector<Physics3D::RaycastHit> hits;
            auto occView = reg.view<TransformComponent, AudioSourceComponent, SoundOcclusionComponent>();
            for (auto e : occView)
            {
                auto& occ = occView.get<SoundOcclusionComponent>(e);
                const auto& src = occView.get<AudioSourceComponent>(e);
                if (!occ.Enabled || !src.Is3D || !hasListener || !physics)
                {
                    occ.Occluded = false;
                    occ.Occlusion = 0.0f;
                    occ.Gain = 1.0f;
                    continue;
                }
                occ.Timer -= dt;
                if (occ.Timer <= 0.0f)
                {
                    occ.Timer = std::max(occ.CheckInterval, 0.0f);
                    const glm::vec3 pos = glm::vec3(occView.get<TransformComponent>(e).WorldTransform[3]);
                    const glm::vec3 delta = pos - listener;
                    const float dist = glm::length(delta);
                    bool blocked = false;
                    if (dist > 0.05f)
                    {
                        hits.clear();
                        physics->RaycastAll(listener, delta / dist, dist - 0.05f, hits);
                        const entt::entity sourceRoot = RootOf(scene, e);
                        for (const auto& h : hits)
                        {
                            if (h.IsTrigger || h.Entity == Physics3D::NoEntity || !reg.valid((entt::entity)h.Entity))
                                continue;
                            const entt::entity root = RootOf(scene, (entt::entity)h.Entity);
                            if (root == sourceRoot || root == listenerRoot)
                                continue;
                            blocked = true;
                            break;
                        }
                    }
                    occ.Occluded = blocked;
                }
                const float target = occ.Occluded ? 1.0f : 0.0f;
                const float k = occ.InterpolationTime > 1e-4f ? std::min(1.0f, dt / occ.InterpolationTime) : 1.0f;
                occ.Occlusion += (target - occ.Occlusion) * k;
                occ.Gain = 1.0f + (std::clamp(occ.OccludedVolume, 0.0f, 1.0f) - 1.0f) * occ.Occlusion;
            }

            // ---- Concurrency
            struct Voice { entt::entity Entity; double Start; float Distance; float Loudness; };
            std::unordered_map<std::string, std::vector<Voice>> groups;
            std::unordered_map<std::string, int> limits;
            auto conView = reg.view<TransformComponent, AudioSourceComponent, SoundConcurrencyComponent>();
            for (auto e : conView)
            {
                auto& con = conView.get<SoundConcurrencyComponent>(e);
                auto& src = conView.get<AudioSourceComponent>(e);
                auto* runtime = static_cast<AudioSource*>(src.RuntimeSound);
                const bool playing = runtime && runtime->IsPlaying();
                if (!playing)
                {
                    con.StartTime = -1.0;
                    continue;
                }
                if (con.StartTime < 0.0)
                    con.StartTime = time;
                const glm::vec3 pos = glm::vec3(conView.get<TransformComponent>(e).WorldTransform[3]);
                const float distance = hasListener ? glm::distance(pos, listener) : 0.0f;
                float loudness = src.Volume;
                if (src.Is3D)
                    loudness *= src.MinDistance / std::max(distance, std::max(src.MinDistance, 1e-3f));
                if (auto* occ = reg.try_get<SoundOcclusionComponent>(e))
                    loudness *= occ->Gain;
                groups[con.Group].push_back({ e, con.StartTime, distance, loudness });
                auto it = limits.find(con.Group);
                const int limit = std::max(con.MaxCount, 0);
                if (it == limits.end()) limits[con.Group] = limit; else it->second = std::min(it->second, limit);
            }
            for (auto& [group, voices] : groups)
            {
                const int limit = limits[group];
                if ((int)voices.size() <= limit)
                    continue;
                const ConcurrencyRule rule = reg.get<SoundConcurrencyComponent>(voices.front().Entity).Rule;
                // Sıralama: korunacaklar başa
                std::sort(voices.begin(), voices.end(), [rule](const Voice& a, const Voice& b) {
                    switch (rule)
                    {
                        case ConcurrencyRule::StopOldest: return a.Start > b.Start;       // yeniler kalır
                        case ConcurrencyRule::StopFarthest: return a.Distance < b.Distance;
                        case ConcurrencyRule::StopQuietest: return a.Loudness > b.Loudness;
                        case ConcurrencyRule::PreventNew: return a.Start < b.Start;       // eskiler kalır
                    }
                    return a.Start > b.Start;
                });
                for (size_t i = (size_t)limit; i < voices.size(); ++i)
                {
                    auto& src = reg.get<AudioSourceComponent>(voices[i].Entity);
                    if (auto* runtime = static_cast<AudioSource*>(src.RuntimeSound))
                        runtime->Stop();
                    src.IsPlaying = false;
                    auto& con = reg.get<SoundConcurrencyComponent>(voices[i].Entity);
                    con.StartTime = -1.0;
                    ++con.Stopped;
                }
            }
        }

        float GainOf(Scene& scene, uint32_t entity)
        {
            auto* occ = scene.m_Registry.try_get<SoundOcclusionComponent>((entt::entity)entity);
            return occ ? occ->Gain : 1.0f;
        }
    }

    void RegisterAudioOcclusionComponents()
    {
        {
            using C = SoundOcclusionComponent;
            ComponentDef<C>("Sound Occlusion", "SoundOcclusionComponent", "Audio", ICON_FA_VOLUME_LOW)
                .Describe("Muffles this Audio Source when a collider blocks the line between the listener and the source (Unreal attenuation occlusion).")
                .Field("Enabled", "Enabled", &C::Enabled)
                .Field("OccludedVolume", "Occluded Volume", &C::OccludedVolume).Range(0.0f, 1.0f, 0.01f).Tooltip("Volume multiplier when fully occluded")
                .Field("InterpolationTime", "Interpolation Time", &C::InterpolationTime).Range(0.0f, 5.0f, 0.01f)
                .Field("CheckInterval", "Trace Interval", &C::CheckInterval).Range(0.0f, 1.0f, 0.01f)
                .Field("Occlusion", "Occlusion", &C::Occlusion).ReadOnly()
                .Field("Occluded", "Occluded", &C::Occluded).ReadOnly()
                .Field("Gain", "Gain", &C::Gain).ReadOnly();
        }
        {
            using C = SoundConcurrencyComponent;
            ComponentDef<C>("Sound Concurrency", "SoundConcurrencyComponent", "Audio", ICON_FA_LAYER_GROUP)
                .Describe("Limits how many Audio Sources of the same group play at once (Unreal Sound Concurrency).")
                .Field("Group", "Group", &C::Group)
                .Field("MaxCount", "Max Count", &C::MaxCount).Range(0.0f, 64.0f, 1.0f)
                .Field("Rule", "Resolution Rule", &C::Rule).Options({ "Stop Oldest", "Stop Farthest", "Stop Quietest", "Prevent New" })
                .Field("Stopped", "Voices Stopped", &C::Stopped).ReadOnly();
        }
    }
}
