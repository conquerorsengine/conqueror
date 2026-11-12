#include "AudioOneShots.h"
#include "Core/Localization/Localization.h"

#include "Core/Audio/AudioEngine.h"
#include "Core/Audio/AudioMixer.h"
#include "Core/Logging/Log.h"

#include <miniaudio.h>

#include <algorithm>
#include <memory>
#include <unordered_set>
#include <vector>

namespace Conqueror
{
    namespace
    {
        struct Voice
        {
            uint32_t Handle = 0;
            std::unique_ptr<ma_sound> Sound;
        };

        struct State
        {
            std::vector<Voice> Voices;
            uint32_t NextHandle = 1;
            std::unordered_set<std::string> FailedPaths;   // eksik dosya her çağrıda günlüğü doldurmasın
        };

        State& S()
        {
            static State* s = new State();
            return *s;
        }

        void Release(Voice& v)
        {
            if (v.Sound)
            {
                ma_sound_stop(v.Sound.get());
                ma_sound_uninit(v.Sound.get());
                v.Sound.reset();
            }
        }

        uint32_t Start(const std::string& requestedPath, float volume, float pitch, float pan, const std::string& group,
                       bool spatial, const glm::vec3& position, float minDistance, float maxDistance)
        {
            if (requestedPath.empty() || !AudioEngine::IsInitialized())
                return 0;
            const std::string path = Localization::Remap(requestedPath);   // yerelleştirme Remaps
            ma_engine* engine = AudioEngine::Get().GetNativeEngine();
            if (!engine)
                return 0;
            State& s = S();
            if (s.FailedPaths.count(path))
                return 0;

            // Ses sınırı: en eski sesi kes
            while ((int)s.Voices.size() >= AudioOneShots::MaxVoices)
            {
                Release(s.Voices.front());
                s.Voices.erase(s.Voices.begin());
            }

            auto sound = std::make_unique<ma_sound>();
            ma_uint32 flags = MA_SOUND_FLAG_NO_DEFAULT_ATTACHMENT | MA_SOUND_FLAG_DECODE;
            if (!spatial)
                flags |= MA_SOUND_FLAG_NO_SPATIALIZATION;
            if (ma_sound_init_from_file(engine, path.c_str(), flags, nullptr, nullptr, sound.get()) != MA_SUCCESS)
            {
                s.FailedPaths.insert(path);
                CQ_CORE_ERROR("[Audio] Cannot play one-shot '{}'", path);
                return 0;
            }
            ma_sound_set_volume(sound.get(), std::max(0.0f, volume));
            ma_sound_set_pitch(sound.get(), std::clamp(pitch, 0.05f, 8.0f));
            ma_sound_set_pan(sound.get(), std::clamp(pan, -1.0f, 1.0f));
            if (spatial)
            {
                ma_sound_set_position(sound.get(), position.x, position.y, position.z);
                ma_sound_set_min_distance(sound.get(), std::max(0.01f, minDistance));
                ma_sound_set_max_distance(sound.get(), std::max(minDistance, maxDistance));
                ma_sound_set_attenuation_model(sound.get(), ma_attenuation_model_inverse);
            }
            ma_node_base* input = AudioMixer::Get().IsInitialized() ? AudioMixer::Get().GetGroupInput(group) : nullptr;
            ma_node_attach_output_bus(sound.get(), 0, input ? (ma_node*)input : ma_engine_get_endpoint(engine), 0);
            if (ma_sound_start(sound.get()) != MA_SUCCESS)
            {
                ma_sound_uninit(sound.get());
                return 0;
            }
            const uint32_t handle = s.NextHandle++;
            if (s.NextHandle == 0)
                s.NextHandle = 1;
            s.Voices.push_back({ handle, std::move(sound) });
            return handle;
        }
    }

    uint32_t AudioOneShots::Play(const std::string& path, float volume, float pitch, float pan, const std::string& group)
    {
        return Start(path, volume, pitch, pan, group, false, glm::vec3(0.0f), 1.0f, 50.0f);
    }

    uint32_t AudioOneShots::PlayAt(const std::string& path, const glm::vec3& position, float volume, float pitch,
                                   const std::string& group, float minDistance, float maxDistance)
    {
        return Start(path, volume, pitch, 0.0f, group, true, position, minDistance, maxDistance);
    }

    bool AudioOneShots::IsPlaying(uint32_t handle)
    {
        for (auto& v : S().Voices)
            if (v.Handle == handle)
                return v.Sound && ma_sound_is_playing(v.Sound.get());
        return false;
    }

    void AudioOneShots::Stop(uint32_t handle)
    {
        auto& voices = S().Voices;
        for (auto it = voices.begin(); it != voices.end(); ++it)
            if (it->Handle == handle)
            {
                Release(*it);
                voices.erase(it);
                return;
            }
    }

    void AudioOneShots::StopAll()
    {
        auto& voices = S().Voices;
        for (auto& v : voices)
            Release(v);
        voices.clear();
    }

    int AudioOneShots::GetActiveCount() { return (int)S().Voices.size(); }

    void AudioOneShots::Update()
    {
        auto& voices = S().Voices;
        for (size_t i = 0; i < voices.size();)
        {
            ma_sound* sound = voices[i].Sound.get();
            if (!sound || ma_sound_at_end(sound) || !ma_sound_is_playing(sound))
            {
                Release(voices[i]);
                voices.erase(voices.begin() + (std::ptrdiff_t)i);
            }
            else
                ++i;
        }
    }

    void AudioOneShots::Shutdown()
    {
        StopAll();
        S().FailedPaths.clear();
    }
}
