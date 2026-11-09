#include "AudioOcclusion.h"
#include "Scene/Scene.h"
#include "Scene/Entity.h"
#include "Scene/Components.h"
#include "Scene/ComponentRegistry.h"
#include "Core/Audio/AudioSource.h"
#include "Core/Audio/AudioOneShots.h"
#include "Core/Audio/SoundCue.h"
#include "Core/Assets/SimpleAsset.h"
#include "Core/Serialization/SceneSerializer.h"
#include "Core/PhysicsSystem/Physics3D/PhysicsWorld3D.h"

#include <IconsFontAwesome6.h>

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <vector>

namespace Conqueror
{
    template<> void Scene::OnComponentAdded<SoundSettingsComponent>(Entity, SoundSettingsComponent&) {}

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

    namespace
    {
        struct ActiveMix
        {
            std::string Path;
            float Weight = 0.0f;
            float FadeIn = 0.0f, FadeOut = 0.0f, Duration = 0.0f, Time = 0.0f;
            bool Releasing = false;
        };

        std::vector<ActiveMix>& Mixes()
        {
            static std::vector<ActiveMix> m;
            return m;
        }

        void UpdateMixes(float dt)
        {
            auto& mixes = Mixes();
            for (auto& m : mixes)
            {
                m.Time += dt;
                if (m.Duration > 0.0f && m.Time >= m.Duration)
                    m.Releasing = true;
                if (m.Releasing)
                    m.Weight = m.FadeOut > 0.0f ? std::max(0.0f, m.Weight - dt / m.FadeOut) : 0.0f;
                else
                    m.Weight = m.FadeIn > 0.0f ? std::min(1.0f, m.Weight + dt / m.FadeIn) : 1.0f;
            }
            mixes.erase(std::remove_if(mixes.begin(), mixes.end(), [](const ActiveMix& m) { return m.Releasing && m.Weight <= 0.0f; }), mixes.end());
        }

        // Sound Class zinciri: kendi + üst sınıfların ses / perdesi, etkin Sound Mix ayarları
        void EvaluateClass(const std::string& path, float& gain, float& pitch)
        {
            std::string current = path;
            std::vector<std::string> visited;
            for (int depth = 0; depth < 16 && !current.empty(); ++depth)
            {
                if (std::find(visited.begin(), visited.end(), current) != visited.end())
                    break;
                visited.push_back(current);
                SimpleAssets::Data data;
                if (!SimpleAssets::Load(current, data))
                    break;
                gain *= data.Float("Volume", 1.0f);
                pitch *= data.Float("Pitch", 1.0f);
                const std::string key = SceneSerializer::ResolveAssetPath(current);
                for (const auto& mix : Mixes())
                {
                    SimpleAssets::Data mixData;
                    if (!SimpleAssets::Load(mix.Path, mixData))
                        continue;
                    for (const auto& row : mixData.Table("Adjusters"))
                        if (SceneSerializer::ResolveAssetPath(SimpleAssets::Cell(row, 0)) == key)
                        {
                            gain *= 1.0f + (SimpleAssets::CellFloat(row, 1, 1.0f) - 1.0f) * mix.Weight;
                            pitch *= 1.0f + (SimpleAssets::CellFloat(row, 2, 1.0f) - 1.0f) * mix.Weight;
                        }
                }
                current = data.Str("Parent");
            }
        }

        void ApplySoundSettings(Scene& scene)
        {
            auto& reg = scene.m_Registry;
            auto view = reg.view<SoundSettingsComponent, AudioSourceComponent>();
            for (auto e : view)
            {
                auto& settings = view.get<SoundSettingsComponent>(e);
                auto& src = view.get<AudioSourceComponent>(e);
                settings.Gain = settings.Pitch = 1.0f;
                if (!settings.SoundClass.empty())
                    EvaluateClass(settings.SoundClass, settings.Gain, settings.Pitch);
                SimpleAssets::Data data;
                if (!settings.Attenuation.empty() && SimpleAssets::Load(settings.Attenuation, data))
                {
                    const std::string model = data.Str("Model");
                    src.Attenuation = model == "None" ? AudioSourceComponent::AttenuationModel::None
                                      : model == "Linear" ? AudioSourceComponent::AttenuationModel::Linear
                                      : model == "Exponential" ? AudioSourceComponent::AttenuationModel::Exponential
                                                               : AudioSourceComponent::AttenuationModel::Inverse;
                    src.MinDistance = data.Float("Min Distance", src.MinDistance);
                    src.MaxDistance = data.Float("Max Distance", src.MaxDistance);
                    src.RolloffFactor = data.Float("Rolloff", src.RolloffFactor);
                    src.Is3D = data.Bool("Spatialize", src.Is3D);
                }
                if (!settings.Concurrency.empty() && SimpleAssets::Load(settings.Concurrency, data))
                {
                    auto& con = reg.get_or_emplace<SoundConcurrencyComponent>(e);
                    con.Group = settings.Concurrency;
                    con.MaxCount = data.Int("Max Count", 4);
                    const std::string rule = data.Str("Resolution Rule");
                    con.Rule = rule == "Stop Farthest" ? ConcurrencyRule::StopFarthest : rule == "Stop Quietest" ? ConcurrencyRule::StopQuietest
                               : rule == "Prevent New" ? ConcurrencyRule::PreventNew : ConcurrencyRule::StopOldest;
                }
                if (settings.Reverb != settings.AppliedReverb && SimpleAssets::Load(settings.Reverb, data))
                {
                    settings.AppliedReverb = settings.Reverb;
                    auto& r = reg.get_or_emplace<AudioReverbFilterComponent>(e);
                    r.DecayTime = data.Float("Decay Time", r.DecayTime);
                    r.DecayHFRatio = data.Float("Decay HF Ratio", r.DecayHFRatio);
                    r.Density = data.Float("Density", r.Density);
                    r.Diffusion = data.Float("Diffusion", r.Diffusion);
                    r.DryLevel = data.Float("Dry Level", r.DryLevel);
                    r.Room = data.Float("Room", r.Room);
                    r.ReverbLevel = data.Float("Reverb Level", r.ReverbLevel);
                    src.NeedsReconnection = true;
                }
            }
        }

        struct Subtitle
        {
            std::string Speaker, Text;
            float Remaining = 0.0f;
        } s_Subtitle;
    }

    namespace SoundMixes
    {
        bool Push(const std::string& path)
        {
            SimpleAssets::Data data;
            if (!SimpleAssets::Load(path, data))
                return false;
            for (auto& m : Mixes())
                if (m.Path == path)
                {
                    m.Releasing = false;
                    m.Time = 0.0f;
                    return true;
                }
            ActiveMix mix;
            mix.Path = path;
            mix.FadeIn = data.Float("Fade In");
            mix.FadeOut = data.Float("Fade Out");
            mix.Duration = data.Float("Duration");
            Mixes().push_back(std::move(mix));
            return true;
        }

        bool Pop(const std::string& path)
        {
            for (auto& m : Mixes())
                if (m.Path == path)
                {
                    m.Releasing = true;
                    return true;
                }
            return false;
        }

        void Clear()
        {
            Mixes().clear();
        }
    }

    namespace Dialogue
    {
        bool Play(const std::string& path, float volume)
        {
            SimpleAssets::Data data;
            if (!SimpleAssets::Load(path, data))
                return false;
            if (const std::string voice = data.Str("Voice"); !voice.empty())
                AudioOneShots::Play(SceneSerializer::ResolveAssetPath(voice), volume, 1.0f, 0.0f, "SFX");
            s_Subtitle.Speaker = data.Str("Speaker");
            s_Subtitle.Text = data.Str("Spoken Text");
            float duration = data.Float("Duration");
            if (duration <= 0.0f)
            {
                int words = 1;
                for (char c : s_Subtitle.Text)
                    words += c == ' ' ? 1 : 0;
                duration = std::max(1.5f, (float)words / 2.5f);
            }
            s_Subtitle.Remaining = duration;
            return true;
        }

        bool GetSubtitle(std::string& speaker, std::string& text)
        {
            if (s_Subtitle.Remaining <= 0.0f)
                return false;
            speaker = s_Subtitle.Speaker;
            text = s_Subtitle.Text;
            return true;
        }

        void Clear()
        {
            s_Subtitle = {};
        }
    }

    namespace AudioOcclusion
    {
        void Update(Scene& scene, float dt, double time)
        {
            auto& reg = scene.m_Registry;
            UpdateMixes(dt);
            s_Subtitle.Remaining = std::max(0.0f, s_Subtitle.Remaining - dt);
            SoundCues::UpdatePending(dt);
            ApplySoundSettings(scene);
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
            using C = SoundSettingsComponent;
            ComponentDef<C>("Sound Settings", "SoundSettingsComponent", "Audio", ICON_FA_SLIDERS)
                .Describe("Binds sound assets to this Audio Source: Sound Attenuation, Sound Class, Sound Concurrency and Reverb Effect.")
                .Field("Attenuation", "Attenuation", &C::Attenuation, FieldKind::Asset).Filter(".cqattenuation")
                .Field("SoundClass", "Sound Class", &C::SoundClass, FieldKind::Asset).Filter(".cqsoundclass")
                .Field("Concurrency", "Concurrency", &C::Concurrency, FieldKind::Asset).Filter(".cqconcurrency")
                .Field("Reverb", "Reverb Effect", &C::Reverb, FieldKind::Asset).Filter(".cqreverb")
                .Field("Gain", "Gain", &C::Gain).ReadOnly()
                .Field("Pitch", "Pitch", &C::Pitch).ReadOnly();
        }
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
