#include "AudioEngine.h"
#include "Core/Logging/Log.h"

#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

namespace Conqueror
{
    AudioEngine* AudioEngine::s_Instance = nullptr;

    AudioEngine::AudioEngine()
    {
        m_Engine = new ma_engine();
        
        ma_result result = ma_engine_init(nullptr, m_Engine);
        if (result != MA_SUCCESS)
        {
            CQ_CORE_ERROR("Failed to initialize Audio Engine!");
            delete m_Engine;
            m_Engine = nullptr;
            return;
        }
        
        CQ_CORE_INFO("Audio Engine initialized");
    }

    AudioEngine::~AudioEngine()
    {
        if (m_Engine)
        {
            ma_engine_uninit(m_Engine);
            delete m_Engine;
            m_Engine = nullptr;
        }
        
        CQ_CORE_INFO("Audio Engine shut down");
    }

    void AudioEngine::Init()
    {
        if (!s_Instance)
        {
            s_Instance = new AudioEngine();
        }
    }

    void AudioEngine::Shutdown()
    {
        if (s_Instance)
        {
            delete s_Instance;
            s_Instance = nullptr;
        }
    }

    void AudioEngine::SetMasterVolume(float volume)
    {
        if (m_Engine)
        {
            ma_engine_set_volume(m_Engine, volume);
        }
    }

    float AudioEngine::GetMasterVolume() const
    {
        if (m_Engine)
        {
            return ma_engine_get_volume(m_Engine);
        }
        return 0.0f;
    }

    void AudioEngine::SetListenerPosition(const glm::vec3& position)
    {
        m_ListenerPosition = position;
        
        if (m_Engine)
        {
            ma_engine_listener_set_position(m_Engine, 0, position.x, position.y, position.z);
        }
    }

    void AudioEngine::SetListenerDirection(const glm::vec3& forward, const glm::vec3& up)
    {
        m_ListenerForward = forward;
        m_ListenerUp = up;
        
        if (m_Engine)
        {
            ma_engine_listener_set_direction(m_Engine, 0, forward.x, forward.y, forward.z);
            ma_engine_listener_set_world_up(m_Engine, 0, up.x, up.y, up.z);
        }
    }

    void AudioEngine::SetListenerVelocity(const glm::vec3& velocity)
    {
        m_ListenerVelocity = velocity;
        
        if (m_Engine)
        {
            ma_engine_listener_set_velocity(m_Engine, 0, velocity.x, velocity.y, velocity.z);
        }
    }

    void AudioEngine::GetSpectrumData(float* buffer, uint32_t count, bool isPlaying, float volume)
    {
        static float phase = 0.0f;
        static std::vector<float> prevBuffer;
        
        if (prevBuffer.size() != count)
            prevBuffer.resize(count, 0.0f);

        if (isPlaying)
            phase += 0.05f;

        for (uint32_t i = 0; i < count; i++)
        {
            if (isPlaying && volume > 0.0f)
            {
                float freqFactor = 1.0f - ((float)i / count);
                float val = (std::sin(phase + i * 0.2f) * 0.5f + 0.5f) * freqFactor;
                float target = (val * val * 0.8f + ((float)rand() / RAND_MAX * 0.05f)) * volume;
                prevBuffer[i] = prevBuffer[i] * 0.7f + target * 0.3f;
            }
            else
            {
                prevBuffer[i] *= 0.85f; // Yumuşak düşüş
            }
            buffer[i] = prevBuffer[i];
        }
    }
}
