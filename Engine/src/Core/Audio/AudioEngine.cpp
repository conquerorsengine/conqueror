#include "AudioEngine.h"
#include "Core/Logging/Log.h"
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <thread>

// OGG Vorbis desteği (miniaudio 0.11: stb_vorbis başlığı önce, uygulaması miniaudio'dan sonra)
#if defined(__GNUC__)
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wunused-value"
    #pragma GCC diagnostic ignored "-Wunused-parameter"
    #pragma GCC diagnostic ignored "-Wsign-compare"
    #pragma GCC diagnostic ignored "-Wpedantic"
#endif
#define STB_VORBIS_HEADER_ONLY
#include <stb_vorbis.c>
#if defined(__GNUC__)
    #pragma GCC diagnostic pop
#endif

#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

#if defined(__GNUC__)
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wunused-value"
    #pragma GCC diagnostic ignored "-Wunused-parameter"
    #pragma GCC diagnostic ignored "-Wsign-compare"
    #pragma GCC diagnostic ignored "-Wpedantic"
    #pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
    #pragma GCC diagnostic ignored "-Wunused-variable"
    #pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
    #pragma GCC diagnostic ignored "-Wmisleading-indentation"
#endif
#undef STB_VORBIS_HEADER_ONLY
#include <stb_vorbis.c>
// stb_vorbis kısa makro adları tanımlar (L, C, R): motor koduna sızmasın
#undef L
#undef C
#undef R
#if defined(__GNUC__)
    #pragma GCC diagnostic pop
#endif

namespace Conqueror
{
    AudioEngine* AudioEngine::s_Instance = nullptr;

    namespace
    {
        // Kayıt: ses iş parçacığı (onProcess) yalnızca capturing açıkken ve halka tampon hazırken yazar; EndCapture busy sıfırlanana
        // kadar bekleyip tamponu kapatır
        std::atomic<bool> s_Capturing{ false };
        std::atomic<int> s_CaptureBusy{ 0 };
        std::atomic<ma_pcm_rb*> s_CaptureRb{ nullptr };

        void OnEngineProcess(void*, float* frames, ma_uint64 frameCount)
        {
            s_CaptureBusy.fetch_add(1);
            ma_pcm_rb* rb = s_CaptureRb.load();
            if (s_Capturing.load() && rb && frames)
            {
                const ma_uint32 channels = ma_pcm_rb_get_channels(rb);
                ma_uint64 left = frameCount;
                const float* src = frames;
                while (left > 0)
                {
                    ma_uint32 n = (ma_uint32)std::min<ma_uint64>(left, 4096);
                    void* dst = nullptr;
                    if (ma_pcm_rb_acquire_write(rb, &n, &dst) != MA_SUCCESS || n == 0)
                        break;   // dolu: kayıt geride kaldı, fazlası atılır
                    std::memcpy(dst, src, (size_t)n * channels * sizeof(float));
                    ma_pcm_rb_commit_write(rb, n);
                    src += (size_t)n * channels;
                    left -= n;
                }
            }
            s_CaptureBusy.fetch_sub(1);
        }
    }

    bool AudioEngine::BeginCapture(uint32_t& channels, uint32_t& sampleRate)
    {
        if (!m_Engine || s_CaptureRb.load())
            return false;
        channels = ma_engine_get_channels(m_Engine);
        sampleRate = ma_engine_get_sample_rate(m_Engine);
        if (channels == 0 || sampleRate == 0)
            return false;
        auto* rb = new ma_pcm_rb();
        if (ma_pcm_rb_init(ma_format_f32, channels, sampleRate * 4, nullptr, nullptr, rb) != MA_SUCCESS)   // 4 s
        {
            delete rb;
            return false;
        }
        m_CaptureRing = rb;
        s_CaptureRb.store(rb);
        s_Capturing.store(true);
        return true;
    }

    size_t AudioEngine::ReadCapture(float* out, size_t maxFrames)
    {
        ma_pcm_rb* rb = s_CaptureRb.load();
        if (!rb || !out)
            return 0;
        const ma_uint32 channels = ma_pcm_rb_get_channels(rb);
        size_t total = 0;
        while (total < maxFrames)
        {
            ma_uint32 n = (ma_uint32)std::min<size_t>(maxFrames - total, 4096);
            void* src = nullptr;
            if (ma_pcm_rb_acquire_read(rb, &n, &src) != MA_SUCCESS || n == 0)
                break;
            std::memcpy(out + total * channels, src, (size_t)n * channels * sizeof(float));
            ma_pcm_rb_commit_read(rb, n);
            total += n;
        }
        return total;
    }

    void AudioEngine::EndCapture()
    {
        s_Capturing.store(false);
        ma_pcm_rb* rb = s_CaptureRb.exchange(nullptr);
        while (s_CaptureBusy.load() != 0)
            std::this_thread::yield();
        if (rb)
        {
            ma_pcm_rb_uninit(rb);
            delete rb;
        }
        m_CaptureRing = nullptr;
    }

    AudioEngine::AudioEngine()
    {
        m_Engine = new ma_engine();

        // CQ_AUDIO_NULL=1 (otomatik testler): miniaudio'nun sessiz "null" aygıtı; saat gerçek zamanda ilerler, karıştırma çalışır,
        // ama hoparlörden ses çıkmaz
        ma_engine_config config = ma_engine_config_init();
        if (const char* nullAudio = std::getenv("CQ_AUDIO_NULL"); nullAudio && nullAudio[0] && nullAudio[0] != '0')
        {
            m_Context = new ma_context();
            const ma_backend backends[] = { ma_backend_null };
            if (ma_context_init(backends, 1, nullptr, m_Context) == MA_SUCCESS)
            {
                config.pContext = m_Context;
                CQ_CORE_INFO("Audio Engine: null device (CQ_AUDIO_NULL)");
            }
            else
            {
                delete m_Context;
                m_Context = nullptr;
            }
        }
        config.onProcess = OnEngineProcess;   // kayıt için karıştırılmış çıkışın kopyası (yalnız kayıt sırasında yazar)
        ma_result result = ma_engine_init(&config, m_Engine);
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
        if (m_CaptureRing)
            EndCapture();
        if (m_Engine)
        {
            ma_engine_uninit(m_Engine);
            delete m_Engine;
            m_Engine = nullptr;
        }
        if (m_Context)
        {
            ma_context_uninit(m_Context);
            delete m_Context;
            m_Context = nullptr;
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

    void AudioEngine::SetPaused(bool paused)
    {
        if (!m_Engine || paused == m_Paused)
            return;
        m_Paused = paused;
        CQ_CORE_INFO("Audio output {0}", paused ? "paused" : "resumed");
        if (paused)
            ma_engine_stop(m_Engine);
        else
            ma_engine_start(m_Engine);
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
