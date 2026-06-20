#include "AudioSource.h"
#include "AudioEngine.h"
#include "Core/Logging/Log.h"

#include <miniaudio.h>

namespace Conqueror
{
    AudioSource::AudioSource()
    {
        m_Sound = new ma_sound();
    }

    AudioSource::~AudioSource()
    {
        Unload();
        
        if (m_Sound)
        {
            delete m_Sound;
            m_Sound = nullptr;
        }
    }

    bool AudioSource::LoadFromFile(const std::string& filepath)
    {
        if (m_IsLoaded)
        {
            Unload();
        }

        ma_engine* engine = AudioEngine::Get().GetNativeEngine();
        if (!engine)
        {
            CQ_CORE_ERROR("Audio Engine not initialized!");
            return false;
        }

        ma_uint32 flags = MA_SOUND_FLAG_NO_DEFAULT_ATTACHMENT;
        if (!m_IsSpatialized)
            flags |= MA_SOUND_FLAG_NO_SPATIALIZATION;

        ma_result result = ma_sound_init_from_file(engine, filepath.c_str(), flags, nullptr, nullptr, m_Sound);
        
        if (result != MA_SUCCESS)
        {
            CQ_CORE_ERROR("Failed to load audio file: {0}", filepath);
            return false;
        }

        m_FilePath = filepath;
        m_IsLoaded = true;

        // Varsayılan 3D ayarları uygula
        if (m_IsSpatialized)
        {
            SetPosition(m_Position);
            SetMinDistance(m_MinDistance);
            SetMaxDistance(m_MaxDistance);
        }

        CQ_CORE_INFO("Audio file loaded: {0}", filepath);
        return true;
    }

    void AudioSource::Unload()
    {
        if (m_IsLoaded && m_Sound)
        {
            ma_sound_uninit(m_Sound);
            m_IsLoaded = false;
            m_FilePath.clear();
        }
    }

    void AudioSource::Play()
    {
        if (m_IsLoaded && m_Sound)
        {
            ma_sound_start(m_Sound);
        }
    }

    void AudioSource::Pause()
    {
        if (m_IsLoaded && m_Sound)
        {
            ma_sound_stop(m_Sound);
        }
    }

    void AudioSource::Stop()
    {
        if (m_IsLoaded && m_Sound)
        {
            ma_sound_stop(m_Sound);
            ma_sound_seek_to_pcm_frame(m_Sound, 0);
        }
    }

    void AudioSource::Rewind()
    {
        if (m_IsLoaded && m_Sound)
        {
            ma_sound_seek_to_pcm_frame(m_Sound, 0);
        }
    }

    bool AudioSource::IsPlaying() const
    {
        if (m_IsLoaded && m_Sound)
        {
            return ma_sound_is_playing(m_Sound);
        }
        return false;
    }

    bool AudioSource::IsLooping() const
    {
        if (m_IsLoaded && m_Sound)
        {
            return ma_sound_is_looping(m_Sound);
        }
        return false;
    }

    bool AudioSource::IsAtEnd() const
    {
        if (m_IsLoaded && m_Sound)
        {
            return ma_sound_at_end(m_Sound);
        }
        return true;
    }

    void AudioSource::SetVolume(float volume)
    {
        if (m_IsLoaded && m_Sound)
        {
            ma_sound_set_volume(m_Sound, volume);
        }
    }

    void AudioSource::SetPitch(float pitch)
    {
        if (m_IsLoaded && m_Sound)
        {
            ma_sound_set_pitch(m_Sound, pitch);
        }
    }

    void AudioSource::SetPan(float pan)
    {
        if (m_IsLoaded && m_Sound)
        {
            ma_sound_set_pan(m_Sound, pan);
        }
    }

    void AudioSource::SetLooping(bool loop)
    {
        if (m_IsLoaded && m_Sound)
        {
            ma_sound_set_looping(m_Sound, loop ? MA_TRUE : MA_FALSE);
        }
    }

    void AudioSource::SetMute(bool mute)
    {
        if (m_IsLoaded && m_Sound)
        {
            // miniaudio'da mute için volume 0 yapıyoruz
            if (mute)
                ma_sound_set_volume(m_Sound, 0.0f);
            else
                ma_sound_set_volume(m_Sound, GetVolume());
        }
    }

    float AudioSource::GetVolume() const
    {
        if (m_IsLoaded && m_Sound)
        {
            return ma_sound_get_volume(m_Sound);
        }
        return 0.0f;
    }

    float AudioSource::GetPitch() const
    {
        if (m_IsLoaded && m_Sound)
        {
            return ma_sound_get_pitch(m_Sound);
        }
        return 1.0f;
    }

    float AudioSource::GetPan() const
    {
        if (m_IsLoaded && m_Sound)
        {
            return ma_sound_get_pan(m_Sound);
        }
        return 0.0f;
    }

    void AudioSource::SetPosition(const glm::vec3& position)
    {
        m_Position = position;
        
        if (m_IsLoaded && m_Sound && m_IsSpatialized)
        {
            ma_sound_set_position(m_Sound, position.x, position.y, position.z);
        }
    }

    void AudioSource::SetVelocity(const glm::vec3& velocity)
    {
        m_Velocity = velocity;
        
        if (m_IsLoaded && m_Sound && m_IsSpatialized)
        {
            ma_sound_set_velocity(m_Sound, velocity.x, velocity.y, velocity.z);
        }
    }

    void AudioSource::SetDirection(const glm::vec3& direction)
    {
        m_Direction = direction;
        
        if (m_IsLoaded && m_Sound && m_IsSpatialized)
        {
            ma_sound_set_direction(m_Sound, direction.x, direction.y, direction.z);
        }
    }

    void AudioSource::SetMinDistance(float distance)
    {
        m_MinDistance = distance;
        
        if (m_IsLoaded && m_Sound && m_IsSpatialized)
        {
            ma_sound_set_min_distance(m_Sound, distance);
        }
    }

    void AudioSource::SetMaxDistance(float distance)
    {
        m_MaxDistance = distance;
        
        if (m_IsLoaded && m_Sound && m_IsSpatialized)
        {
            ma_sound_set_max_distance(m_Sound, distance);
        }
    }

    void AudioSource::SetDopplerFactor(float factor)
    {
        m_DopplerFactor = factor;
        
        if (m_IsLoaded && m_Sound && m_IsSpatialized)
        {
            ma_sound_set_doppler_factor(m_Sound, factor);
        }
    }

    void AudioSource::SetAttenuationModel(AudioAttenuationModel model)
    {
        if (m_IsLoaded && m_Sound && m_IsSpatialized)
        {
            ma_attenuation_model maModel;
            
            switch (model)
            {
                case AudioAttenuationModel::None:
                    maModel = ma_attenuation_model_none;
                    break;
                case AudioAttenuationModel::Inverse:
                    maModel = ma_attenuation_model_inverse;
                    break;
                case AudioAttenuationModel::Linear:
                    maModel = ma_attenuation_model_linear;
                    break;
                case AudioAttenuationModel::Exponential:
                    maModel = ma_attenuation_model_exponential;
                    break;
                default:
                    maModel = ma_attenuation_model_inverse;
                    break;
            }
            
            ma_sound_set_attenuation_model(m_Sound, maModel);
        }
    }

    void AudioSource::SetRolloff(float rolloff)
    {
        m_Rolloff = rolloff;
        
        if (m_IsLoaded && m_Sound && m_IsSpatialized)
        {
            ma_sound_set_rolloff(m_Sound, rolloff);
        }
    }

    void AudioSource::SetSpatialization(bool enabled)
    {
        if (m_IsSpatialized == enabled) return;
        
        m_IsSpatialized = enabled;
        
        // Spatialization değişikliği için ses yeniden yüklenmeli
        if (m_IsLoaded)
        {
            std::string filepath = m_FilePath;
            bool wasPlaying = IsPlaying();
            float currentTime = GetCurrentTimeInSeconds();
            
            Unload();
            LoadFromFile(filepath);
            
            if (wasPlaying)
            {
                Play();
            }
            SetCurrentTimeInSeconds(currentTime);
        }
    }

    float AudioSource::GetLengthInSeconds() const
    {
        if (m_IsLoaded && m_Sound)
        {
            float length;
            ma_sound_get_length_in_seconds(m_Sound, &length);
            return length;
        }
        return 0.0f;
    }

    float AudioSource::GetCurrentTimeInSeconds() const
    {
        if (m_IsLoaded && m_Sound)
        {
            float cursor;
            ma_sound_get_cursor_in_seconds(m_Sound, &cursor);
            return cursor;
        }
        return 0.0f;
    }

    void AudioSource::SetCurrentTimeInSeconds(float time)
    {
        if (m_IsLoaded && m_Sound)
        {
            ma_sound_seek_to_pcm_frame(m_Sound, (ma_uint64)(time * ma_engine_get_sample_rate(ma_sound_get_engine(m_Sound))));
        }
    }

    ma_node_base* AudioSource::GetNativeNode() const
    {
        if (!m_Sound) return nullptr;
        // Correct way to get the node from a ma_sound in miniaudio
        return (ma_node_base*)m_Sound; 
    }
}