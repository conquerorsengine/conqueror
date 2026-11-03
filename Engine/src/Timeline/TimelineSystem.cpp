#include "Scene/Signals.h"
#include "TimelineSystem.h"
#include "Scene/Scene.h"
#include "Scene/Entity.h"
#include "Scene/Components.h"
#include "Scene/ComponentRegistry.h"
#include "Core/Audio/AudioSource.h"
#include "Core/Audio/AudioEngine.h"
#include "Core/Project/Project.h"
#include "Core/Profiling/FrameProfiler.h"
#include "Core/Logging/Log.h"
#include "Scripting/ConquerorScript/CQSValue.h"
#include "Core/Animation/AnimationClip.h"
#include "Renderer/Utilities/Renderer3D/ModelLoader.h"

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <map>
#include <tuple>
#include <unordered_map>

namespace Conqueror
{
    namespace
    {
        struct CachedAsset
        {
            std::shared_ptr<TimelineAsset> Asset;
            std::filesystem::file_time_type Time{};
        };

        std::unordered_map<std::string, CachedAsset>& Cache()
        {
            static std::unordered_map<std::string, CachedAsset> s;
            return s;
        }

        // Ses klipleri: (sahne, yönetmen, parça, klip) -> kaynak
        using AudioKey = std::tuple<Scene*, uint32_t, uint32_t, uint32_t>;
        std::map<AudioKey, std::unique_ptr<AudioSource>>& AudioSources()
        {
            static std::map<AudioKey, std::unique_ptr<AudioSource>> s;
            return s;
        }

        void StopDirectorAudio(Scene* scene, uint32_t director)
        {
            auto& sources = AudioSources();
            for (auto it = sources.begin(); it != sources.end();)
            {
                if (std::get<0>(it->first) == scene && std::get<1>(it->first) == director)
                {
                    if (it->second)
                        it->second->Stop();
                    it = sources.erase(it);
                }
                else
                    ++it;
            }
        }

        void WriteField(const ComponentField& field, void* component, const glm::vec4& v)
        {
            if (!field.Ptr || !component)
                return;
            void* p = field.Ptr(component);
            switch (field.Kind)
            {
                case FieldKind::Float:
                case FieldKind::Angle: *static_cast<float*>(p) = v.x; break;
                case FieldKind::Int: *static_cast<int*>(p) = (int)std::lround(v.x); break;
                case FieldKind::Bool: *static_cast<bool*>(p) = v.x >= 0.5f; break;
                case FieldKind::Vec2: *static_cast<glm::vec2*>(p) = glm::vec2(v); break;
                case FieldKind::Vec3:
                case FieldKind::Color3: *static_cast<glm::vec3*>(p) = glm::vec3(v); break;
                case FieldKind::Vec4:
                case FieldKind::Color4: *static_cast<glm::vec4*>(p) = v; break;
                default: break;
            }
        }

        glm::vec4 ReadField(const ComponentField& field, void* component)
        {
            if (!field.Ptr || !component)
                return glm::vec4(0.0f);
            void* p = field.Ptr(component);
            switch (field.Kind)
            {
                case FieldKind::Float:
                case FieldKind::Angle: return glm::vec4(*static_cast<float*>(p), 0.0f, 0.0f, 0.0f);
                case FieldKind::Int: return glm::vec4((float)*static_cast<int*>(p), 0.0f, 0.0f, 0.0f);
                case FieldKind::Bool: return glm::vec4(*static_cast<bool*>(p) ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f);
                case FieldKind::Vec2: return glm::vec4(*static_cast<glm::vec2*>(p), 0.0f, 0.0f);
                case FieldKind::Vec3:
                case FieldKind::Color3: return glm::vec4(*static_cast<glm::vec3*>(p), 0.0f);
                case FieldKind::Vec4:
                case FieldKind::Color4: return *static_cast<glm::vec4*>(p);
                default: return glm::vec4(0.0f);
            }
        }

        void SetActive(Entity e, bool active)
        {
            if (!e.HasComponent<ActiveComponent>())
                e.AddComponent<ActiveComponent>(active);
            else
                e.GetComponent<ActiveComponent>().Active = active;
        }

        void FireSignals(Scene& scene, Entity director, const TimelineAsset& asset, double from, double to)
        {
            for (size_t i = 0; i < asset.Tracks.size(); ++i)
            {
                const auto& track = asset.Tracks[i];
                if (track.Type != TimelineTrackType::Signal || track.Muted)
                    continue;
                Entity target = TimelineSystem::ResolveBinding(scene, director, i);
                if (!target)
                    continue;
                for (const auto& m : track.Markers)
                {
                    const bool crossed = (from < 0.0 ? m.Time >= 0.0f : m.Time > from) && m.Time <= to;
                    if (!crossed)
                        continue;
                    // Signal Emitter: Signal varlığı bağlı nesnenin Signal Receiver'ına gider
                    if (!m.Signal.empty())
                        SignalAssets::Send(scene, (uint32_t)(entt::entity)target, m.Signal);
                    if (m.Method.empty())
                        continue;
                    if (m.Argument.empty())
                        scene.InvokeScriptMethod((uint32_t)(entt::entity)target, m.Method.c_str(), nullptr, 0);
                    else
                    {
                        CQS::Value arg = CQS::Value::MakeString(m.Argument);
                        scene.InvokeScriptMethod((uint32_t)(entt::entity)target, m.Method.c_str(), &arg, 1);
                    }
                }
            }
        }

        void UpdateAudio(Scene& scene, Entity director, const TimelineAsset& asset, double time)
        {
            if (!AudioEngine::IsInitialized())
                return;
            auto& sources = AudioSources();
            const uint32_t dirId = (uint32_t)(entt::entity)director;
            for (size_t ti = 0; ti < asset.Tracks.size(); ++ti)
            {
                const auto& track = asset.Tracks[ti];
                if (track.Type != TimelineTrackType::Audio)
                    continue;
                for (size_t ci = 0; ci < track.Clips.size(); ++ci)
                {
                    const auto& clip = track.Clips[ci];
                    const AudioKey key{ &scene, dirId, (uint32_t)ti, (uint32_t)ci };
                    const bool inside = !track.Muted && time >= clip.Start && time < clip.End() && !clip.Asset.empty();
                    auto it = sources.find(key);
                    if (!inside)
                    {
                        if (it != sources.end())
                        {
                            it->second->Stop();
                            sources.erase(it);
                        }
                        continue;
                    }
                    if (it != sources.end())
                        continue;
                    auto source = std::make_unique<AudioSource>();
                    const auto path = TimelineSystem::ResolvePath(clip.Asset);
                    if (!source->LoadFromFile(path.string()))
                    {
                        CQ_CORE_WARN("[Timeline] Audio clip not found: {0}", clip.Asset);
                        sources[key] = nullptr;
                        continue;
                    }
                    source->SetSpatialization(false);
                    source->SetVolume(clip.Volume);
                    source->SetPitch(std::max(0.05f, clip.Speed));
                    source->SetCurrentTimeInSeconds(clip.ClipIn + (float)(time - clip.Start) * clip.Speed);
                    source->Play();
                    sources[key] = std::move(source);
                }
            }
        }
    }

    std::filesystem::path TimelineSystem::ResolvePath(const std::string& path)
    {
        std::filesystem::path p(path);
        if (p.empty() || p.is_absolute())
            return p;
        const auto dir = Project::GetActiveProjectDirectory();
        return dir.empty() ? p : dir / p;
    }

    std::shared_ptr<TimelineAsset> TimelineSystem::GetAsset(const std::string& path)
    {
        if (path.empty())
            return nullptr;
        const auto abs = ResolvePath(path);
        std::error_code ec;
        const auto mtime = std::filesystem::last_write_time(abs, ec);
        if (ec)
            return nullptr;
        auto& cache = Cache();
        auto it = cache.find(abs.string());
        if (it != cache.end() && it->second.Time == mtime)
            return it->second.Asset;
        std::string error;
        auto asset = TimelineAsset::Load(abs, &error);
        if (!asset)
        {
            CQ_CORE_ERROR("[Timeline] Cannot load {0}: {1}", path, error);
            return nullptr;
        }
        cache[abs.string()] = { asset, mtime };
        return asset;
    }

    void TimelineSystem::InvalidateAsset(const std::string& path)
    {
        Cache().erase(ResolvePath(path).string());
    }

    double TimelineSystem::GetDuration(const PlayableDirectorComponent& director)
    {
        auto asset = GetAsset(director.Timeline);
        return asset ? asset->Duration : 0.0;
    }

    Entity TimelineSystem::ResolveBinding(Scene& scene, Entity director, size_t trackIndex)
    {
        if (!director || !director.HasComponent<PlayableDirectorComponent>())
            return {};
        const auto& d = director.GetComponent<PlayableDirectorComponent>();
        const uint64_t uuid = trackIndex < d.Bindings.size() ? d.Bindings[trackIndex] : 0;
        if (uuid == 0)
            return director;
        return scene.FindEntityByUUID(uuid);
    }

    void TimelineSystem::Evaluate(Scene& scene, Entity director, double time)
    {
        if (!director || !director.HasComponent<PlayableDirectorComponent>())
            return;
        auto asset = GetAsset(director.GetComponent<PlayableDirectorComponent>().Timeline);
        if (asset)
            Evaluate(scene, director, *asset, time);
    }

    void TimelineSystem::Evaluate(Scene& scene, Entity director, const TimelineAsset& asset, double time)
    {
        if (!director || !director.HasComponent<PlayableDirectorComponent>())
            return;
        const float t = (float)time;
        for (size_t i = 0; i < asset.Tracks.size(); ++i)
        {
            const auto& track = asset.Tracks[i];
            if (track.Muted)
                continue;
            Entity target = ResolveBinding(scene, director, i);
            if (!target)
                continue;
            switch (track.Type)
            {
                case TimelineTrackType::Activation:
                {
                    bool active = false;
                    for (const auto& c : track.Clips)
                        if (t >= c.Start && t < c.End())
                            active = true;
                    SetActive(target, active);
                    break;
                }
                case TimelineTrackType::Transform:
                {
                    auto& tc = target.GetComponent<TransformComponent>();
                    if (const auto* ch = track.FindChannel("Position"); ch && !ch->Keys.empty())
                        tc.Position = glm::vec3(EvaluateChannel(*ch, t, glm::vec4(tc.Position, 0.0f)));
                    if (const auto* ch = track.FindChannel("Rotation"); ch && !ch->Keys.empty())
                        tc.Rotation = glm::radians(glm::vec3(EvaluateChannel(*ch, t, glm::vec4(glm::degrees(tc.Rotation), 0.0f))));
                    if (const auto* ch = track.FindChannel("Scale"); ch && !ch->Keys.empty())
                        tc.Scale = glm::vec3(EvaluateChannel(*ch, t, glm::vec4(tc.Scale, 0.0f)));
                    break;
                }
                case TimelineTrackType::Property:
                {
                    ComponentInfo* info = ComponentRegistry::Find(track.Component);
                    if (!info || !info->Has || !info->Has(target))
                        break;
                    const ComponentField* field = ComponentRegistry::FindField(*info, track.Field);
                    if (!field || track.Channels.empty() || track.Channels.front().Keys.empty())
                        break;
                    void* comp = info->Get(target);
                    WriteField(*field, comp, EvaluateChannel(track.Channels.front(), t, ReadField(*field, comp)));
                    break;
                }
                case TimelineTrackType::Animation:
                {
                    if (!target.HasComponent<AnimatorComponent>())
                        break;
                    // Bu andaki klipler: örtüşen iki klip Unity Timeline gibi örtüşme boyunca yumuşak geçer
                    const TimelineClip* outgoing = nullptr;
                    const TimelineClip* incoming = nullptr;
                    for (const auto& c : track.Clips)
                    {
                        if (t < c.Start || t >= c.End())
                            continue;
                        if (!outgoing || c.Start < outgoing->Start)
                        {
                            incoming = outgoing;
                            outgoing = &c;
                        }
                        else if (!incoming || c.Start > incoming->Start)
                            incoming = &c;
                    }
                    if (!outgoing)
                        break;
                    // Klip zamanı; döngüsüz klip animasyon bitince son karede kalır
                    const MeshRendererComponent* model = target.HasComponent<MeshRendererComponent>() ? &target.GetComponent<MeshRendererComponent>() : nullptr;
                    auto clipTime = [&](const TimelineClip& c) {
                        float time = c.ClipIn + (float)(t - c.Start) * c.Speed;
                        if (!c.Loop && model && model->ModelData && c.AnimationIndex >= 0 &&
                            c.AnimationIndex < (int)model->ModelData->Animations.size() && model->ModelData->Animations[(size_t)c.AnimationIndex])
                            time = std::min(time, std::max(0.0f, model->ModelData->Animations[(size_t)c.AnimationIndex]->DurationSeconds() - 1e-3f));
                        return time;
                    };
                    auto& anim = target.GetComponent<AnimatorComponent>();
                    anim.Playing = false;   // zaman çizelgesi sürer
                    anim.ActiveClipIndex = outgoing->AnimationIndex;
                    anim.CurrentTime = clipTime(*outgoing);
                    anim.BlendClipIndex = -1;
                    anim.BlendWeight = 0.0f;
                    if (incoming)
                    {
                        const double from = incoming->Start, to = std::min(outgoing->End(), incoming->End());
                        double w = to > from ? (t - from) / (to - from) : 1.0;
                        w = std::clamp(w, 0.0, 1.0);
                        anim.BlendClipIndex = incoming->AnimationIndex;
                        anim.BlendClipTime = clipTime(*incoming);
                        anim.BlendWeight = (float)(w * w * (3.0 - 2.0 * w));   // yumuşak başlayıp biten geçiş
                    }
                    break;
                }
                default:
                    break;   // ses ve sinyaller yalnızca oyunda
            }
        }
    }

    void TimelineSystem::Update(Scene& scene, float dt)
    {
        auto view = scene.m_Registry.view<PlayableDirectorComponent>();
        if (view.empty())
            return;
        CQ_PROFILE_SAMPLE("Timeline.Update", Animation);
        std::vector<entt::entity> directors(view.begin(), view.end());
        for (auto e : directors)
        {
            if (!scene.m_Registry.valid(e) || !scene.IsEntityActive(e))
                continue;
            Entity director{ e, &scene };
            auto& d = director.GetComponent<PlayableDirectorComponent>();
            if (!d.Started)
            {
                d.Started = true;
                if (d.PlayOnAwake)
                {
                    d.Playing = true;
                    d.Time = 0.0;
                    d.LastSignalTime = -1.0;
                }
            }
            if (!d.Playing)
                continue;
            auto asset = GetAsset(d.Timeline);
            if (!asset)
                continue;
            const double duration = std::max(0.001, (double)asset->Duration);
            double prev = d.LastSignalTime;
            d.Time += (double)dt * (double)d.Speed;
            bool finished = false;
            if (d.Time >= duration)
            {
                if (d.WrapMode == 1)
                {
                    // Döngü: sondaki sinyaller, sonra baştan
                    FireSignals(scene, director, *asset, prev, duration);
                    d.Time = std::fmod(d.Time, duration);
                    prev = -1.0;
                    StopDirectorAudio(&scene, (uint32_t)e);
                }
                else
                {
                    d.Time = duration;
                    finished = true;
                }
            }
            Evaluate(scene, director, d.Time);
            if (!scene.m_Registry.valid(e))
                continue;
            FireSignals(scene, director, *asset, prev, d.Time);
            if (!scene.m_Registry.valid(e))
                continue;   // sinyal yönetmeni silmiş olabilir
            auto& dd = scene.m_Registry.get<PlayableDirectorComponent>(e);
            dd.LastSignalTime = dd.Time;
            UpdateAudio(scene, director, *asset, finished ? duration + 1.0 : dd.Time);
            if (finished)
            {
                dd.Playing = false;
                StopDirectorAudio(&scene, (uint32_t)e);
                // Etkinleştirme parçaları: oynatma sonrası durum
                for (size_t i = 0; i < asset->Tracks.size(); ++i)
                {
                    const auto& track = asset->Tracks[i];
                    if (track.Type != TimelineTrackType::Activation || track.Muted || track.PostPlayback == 0)
                        continue;
                    if (Entity target = ResolveBinding(scene, director, i))
                        SetActive(target, track.PostPlayback == 1);
                }
                if (dd.WrapMode == 2)
                {
                    dd.Time = 0.0;
                    dd.LastSignalTime = -1.0;
                }
            }
        }
    }

    void TimelineSystem::Play(Scene& scene, Entity director)
    {
        if (!director || !director.HasComponent<PlayableDirectorComponent>())
            return;
        auto& d = director.GetComponent<PlayableDirectorComponent>();
        d.Started = true;
        // Bitmiş (Hold) bir çizelge yeniden başlar
        if (d.Time >= GetDuration(d) - 1e-6)
        {
            d.Time = 0.0;
            d.LastSignalTime = -1.0;
        }
        d.Playing = true;
        (void)scene;
    }

    void TimelineSystem::Pause(Scene& scene, Entity director)
    {
        if (!director || !director.HasComponent<PlayableDirectorComponent>())
            return;
        director.GetComponent<PlayableDirectorComponent>().Playing = false;
        StopDirectorAudio(&scene, (uint32_t)(entt::entity)director);
    }

    void TimelineSystem::Stop(Scene& scene, Entity director)
    {
        if (!director || !director.HasComponent<PlayableDirectorComponent>())
            return;
        auto& d = director.GetComponent<PlayableDirectorComponent>();
        d.Playing = false;
        d.Started = true;
        d.Time = 0.0;
        d.LastSignalTime = -1.0;
        StopDirectorAudio(&scene, (uint32_t)(entt::entity)director);
        Evaluate(scene, director, 0.0);
    }

    void TimelineSystem::SetTime(Scene& scene, Entity director, double time)
    {
        if (!director || !director.HasComponent<PlayableDirectorComponent>())
            return;
        auto& d = director.GetComponent<PlayableDirectorComponent>();
        d.Time = std::clamp(time, 0.0, GetDuration(d));
        d.LastSignalTime = d.Time;   // atlanan sinyaller çalışmaz (Unity gibi)
        StopDirectorAudio(&scene, (uint32_t)(entt::entity)director);
        Evaluate(scene, director, d.Time);
    }

    void TimelineSystem::ForgetScene(Scene* scene)
    {
        auto& sources = AudioSources();
        for (auto it = sources.begin(); it != sources.end();)
        {
            if (std::get<0>(it->first) == scene)
            {
                if (it->second)
                    it->second->Stop();
                it = sources.erase(it);
            }
            else
                ++it;
        }
    }

    void TimelineSystem::Shutdown()
    {
        AudioSources().clear();
        Cache().clear();
    }
}
