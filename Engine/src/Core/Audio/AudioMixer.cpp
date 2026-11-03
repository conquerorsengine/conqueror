#include "AudioMixer.h"
#include "AudioEngine.h"
#include "AudioFilter.h"
#include "Core/Logging/Log.h"

#include <miniaudio.h>
#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <fstream>

namespace Conqueror
{
    namespace
    {
        double NowSeconds()
        {
            using namespace std::chrono;
            return duration<double>(steady_clock::now().time_since_epoch()).count();
        }

        ma_engine* Engine()
        {
            return AudioEngine::IsInitialized() ? AudioEngine::Get().GetNativeEngine() : nullptr;
        }

        // ------------------------------------------------------------ Özel düğüm temeli
        // ma_node_base ilk üye olmalı (miniaudio düğüm işaretçisi olarak kullanılır)
        template<typename Derived>
        struct CustomNode
        {
            ma_node_base Base;

            static void Process(ma_node* pNode, const float** ppFramesIn, ma_uint32* pFrameCountIn, float** ppFramesOut, ma_uint32* pFrameCountOut)
            {
                auto* self = reinterpret_cast<Derived*>(pNode);
                if (!ppFramesIn || !ppFramesIn[0] || !ppFramesOut || !ppFramesOut[0])
                {
                    *pFrameCountIn = 0;
                    *pFrameCountOut = 0;
                    return;
                }
                const ma_uint32 frames = std::min(*pFrameCountIn, *pFrameCountOut);
                const ma_uint32 channels = ma_node_get_output_channels(pNode, 0);
                self->Run(ppFramesIn[0], ppFramesOut[0], frames, channels);
                *pFrameCountIn = frames;
                *pFrameCountOut = frames;
            }

            static ma_node_vtable* VTable()
            {
                static ma_node_vtable vt = { &CustomNode::Process, nullptr, 1, 1, 0 };
                return &vt;
            }
        };

        // Ölçer: geçiş + tepe/RMS
        struct MeterNode : CustomNode<MeterNode>
        {
            std::shared_ptr<MixerMeterData> Data;

            void Run(const float* in, float* out, ma_uint32 frames, ma_uint32 channels)
            {
                float peak[2] = { 0, 0 }, sum[2] = { 0, 0 };
                for (ma_uint32 f = 0; f < frames; ++f)
                {
                    for (ma_uint32 c = 0; c < channels; ++c)
                    {
                        float s = in[f * channels + c];
                        out[f * channels + c] = s;
                        int k = c == 0 ? 0 : 1;
                        float a = std::fabs(s);
                        if (a > peak[k]) peak[k] = a;
                        sum[k] += s * s;
                    }
                }
                if (channels == 1) { peak[1] = peak[0]; sum[1] = sum[0]; }
                if (!Data || frames == 0)
                    return;
                if (peak[0] > Data->PeakL.load(std::memory_order_relaxed)) Data->PeakL.store(peak[0], std::memory_order_relaxed);
                if (peak[1] > Data->PeakR.load(std::memory_order_relaxed)) Data->PeakR.store(peak[1], std::memory_order_relaxed);
                Data->RmsL.store(std::sqrt(sum[0] / frames), std::memory_order_relaxed);
                Data->RmsR.store(std::sqrt(sum[1] / frames), std::memory_order_relaxed);
            }
        };

        // Kompresör (tepe izleyici, yumuşak diz yok; stereo bağlı)
        struct CompressorNode : CustomNode<CompressorNode>
        {
            std::atomic<float> ThresholdDb{ -18.0f }, Ratio{ 4.0f }, AttackMs{ 10.0f }, ReleaseMs{ 120.0f }, MakeupDb{ 0.0f };
            float Envelope = 0.0f;
            float SampleRate = 48000.0f;

            void Run(const float* in, float* out, ma_uint32 frames, ma_uint32 channels)
            {
                const float thr = ThresholdDb.load(), ratio = std::max(1.0f, Ratio.load());
                const float att = std::exp(-1.0f / (std::max(0.05f, AttackMs.load()) * 0.001f * SampleRate));
                const float rel = std::exp(-1.0f / (std::max(1.0f, ReleaseMs.load()) * 0.001f * SampleRate));
                const float makeup = AudioMixer::DbToLinear(MakeupDb.load());
                for (ma_uint32 f = 0; f < frames; ++f)
                {
                    float level = 0.0f;
                    for (ma_uint32 c = 0; c < channels; ++c)
                        level = std::max(level, std::fabs(in[f * channels + c]));
                    Envelope = level > Envelope ? att * Envelope + (1 - att) * level : rel * Envelope + (1 - rel) * level;
                    float envDb = AudioMixer::LinearToDb(Envelope);
                    float gainDb = envDb > thr ? (thr + (envDb - thr) / ratio) - envDb : 0.0f;
                    float g = AudioMixer::DbToLinear(gainDb) * makeup;
                    for (ma_uint32 c = 0; c < channels; ++c)
                        out[f * channels + c] = in[f * channels + c] * g;
                }
            }
        };

        // Tepe sınırlayıcı (anlık atak, ayarlanabilir bırakma)
        struct LimiterNode : CustomNode<LimiterNode>
        {
            std::atomic<float> CeilingDb{ -0.3f }, ReleaseMs{ 80.0f };
            float Gain = 1.0f;
            float SampleRate = 48000.0f;

            void Run(const float* in, float* out, ma_uint32 frames, ma_uint32 channels)
            {
                const float ceiling = AudioMixer::DbToLinear(CeilingDb.load());
                const float rel = std::exp(-1.0f / (std::max(1.0f, ReleaseMs.load()) * 0.001f * SampleRate));
                for (ma_uint32 f = 0; f < frames; ++f)
                {
                    float level = 0.0f;
                    for (ma_uint32 c = 0; c < channels; ++c)
                        level = std::max(level, std::fabs(in[f * channels + c]));
                    float target = level * Gain > ceiling && level > 0.0f ? ceiling / level : 1.0f;
                    Gain = target < Gain ? target : rel * Gain + (1 - rel) * target;
                    Gain = std::min(Gain, 1.0f);
                    for (ma_uint32 c = 0; c < channels; ++c)
                        out[f * channels + c] = std::clamp(in[f * channels + c] * Gain, -ceiling, ceiling);
                }
            }
        };

        // Kazanç (dB)
        struct GainDbNode : CustomNode<GainDbNode>
        {
            std::atomic<float> GainDb{ 0.0f };
            void Run(const float* in, float* out, ma_uint32 frames, ma_uint32 channels)
            {
                const float g = AudioMixer::DbToLinear(GainDb.load());
                for (ma_uint32 i = 0; i < frames * channels; ++i)
                    out[i] = in[i] * g;
            }
        };

        // Freeverb (Jezar) — 8 tarak + 4 tüm geçiren filtre, stereo
        struct ReverbNode : CustomNode<ReverbNode>
        {
            struct Comb
            {
                std::vector<float> Buf; size_t Idx = 0; float Store = 0.0f;
                float Process(float x, float feedback, float damp)
                {
                    float y = Buf[Idx];
                    Store = y * (1.0f - damp) + Store * damp;
                    Buf[Idx] = x + Store * feedback;
                    if (++Idx >= Buf.size()) Idx = 0;
                    return y;
                }
            };
            struct Allpass
            {
                std::vector<float> Buf; size_t Idx = 0;
                float Process(float x)
                {
                    float b = Buf[Idx];
                    float y = -x + b;
                    Buf[Idx] = x + b * 0.5f;
                    if (++Idx >= Buf.size()) Idx = 0;
                    return y;
                }
            };

            std::atomic<float> RoomSize{ 0.6f }, Damping{ 0.5f }, Wet{ 0.3f }, Dry{ 1.0f }, Width{ 1.0f };
            std::array<Comb, 8> CombL, CombR;
            std::array<Allpass, 4> ApL, ApR;

            void Setup(float sampleRate)
            {
                static const int combTuning[8] = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
                static const int apTuning[4] = { 556, 441, 341, 225 };
                const float scale = sampleRate / 44100.0f;
                for (int i = 0; i < 8; ++i)
                {
                    CombL[i].Buf.assign((size_t)std::max(1.0f, combTuning[i] * scale), 0.0f);
                    CombR[i].Buf.assign((size_t)std::max(1.0f, (combTuning[i] + 23) * scale), 0.0f);
                }
                for (int i = 0; i < 4; ++i)
                {
                    ApL[i].Buf.assign((size_t)std::max(1.0f, apTuning[i] * scale), 0.0f);
                    ApR[i].Buf.assign((size_t)std::max(1.0f, (apTuning[i] + 23) * scale), 0.0f);
                }
            }

            void Run(const float* in, float* out, ma_uint32 frames, ma_uint32 channels)
            {
                const float feedback = 0.7f + 0.28f * std::clamp(RoomSize.load(), 0.0f, 1.0f);
                const float damp = 0.4f * std::clamp(Damping.load(), 0.0f, 1.0f);
                const float wet = Wet.load() * 3.0f, dry = Dry.load(), width = std::clamp(Width.load(), 0.0f, 1.0f);
                const float wet1 = wet * (width * 0.5f + 0.5f), wet2 = wet * ((1.0f - width) * 0.5f);
                for (ma_uint32 f = 0; f < frames; ++f)
                {
                    float l = in[f * channels];
                    float r = channels > 1 ? in[f * channels + 1] : l;
                    float input = (l + r) * 0.015f;
                    float outL = 0.0f, outR = 0.0f;
                    for (int i = 0; i < 8; ++i)
                    {
                        outL += CombL[i].Process(input, feedback, damp);
                        outR += CombR[i].Process(input, feedback, damp);
                    }
                    for (int i = 0; i < 4; ++i)
                    {
                        outL = ApL[i].Process(outL);
                        outR = ApR[i].Process(outR);
                    }
                    float yl = outL * wet1 + outR * wet2 + l * dry;
                    float yr = outR * wet1 + outL * wet2 + r * dry;
                    out[f * channels] = yl;
                    if (channels > 1) out[f * channels + 1] = yr;
                    for (ma_uint32 c = 2; c < channels; ++c) out[f * channels + c] = in[f * channels + c] * dry;
                }
            }
        };

        // Özel düğümü AudioNode olarak saran sınıf
        template<typename NodeT>
        class CustomAudioNode : public AudioNode
        {
        public:
            CustomAudioNode()
            {
                ma_engine* engine = Engine();
                if (!engine)
                    return;
                ma_uint32 channels = ma_engine_get_channels(engine);
                ma_uint32 inCh[1] = { channels }, outCh[1] = { channels };
                ma_node_config config = ma_node_config_init();
                config.vtable = NodeT::VTable();
                config.pInputChannels = inCh;
                config.pOutputChannels = outCh;
                m_Node = std::make_unique<NodeT>();
                if constexpr (requires(NodeT n) { n.SampleRate; })
                    m_Node->SampleRate = (float)ma_engine_get_sample_rate(engine);
                if constexpr (requires(NodeT n) { n.Setup(1.0f); })
                    m_Node->Setup((float)ma_engine_get_sample_rate(engine));
                if (ma_node_init(ma_engine_get_node_graph(engine), &config, nullptr, &m_Node->Base) != MA_SUCCESS)
                {
                    CQ_CORE_ERROR("AudioMixer: custom node init failed");
                    m_Node.reset();
                }
            }
            ~CustomAudioNode() override
            {
                if (m_Node)
                    ma_node_uninit(&m_Node->Base, nullptr);
            }
            ma_node_base* GetNativeNode() const override { return m_Node ? &m_Node->Base : nullptr; }
            NodeT* Get() { return m_Node.get(); }

        private:
            std::unique_ptr<NodeT> m_Node;
        };

        std::shared_ptr<AudioNode> CreateEffectNode(const MixerEffect& e)
        {
            const float* p = e.Params;
            switch (e.Type)
            {
                case MixerEffectType::LowPass: return std::make_shared<AudioLowPassFilter>(p[0], std::clamp((int)p[1], 1, 8));
                case MixerEffectType::HighPass: return std::make_shared<AudioHighPassFilter>(p[0], std::clamp((int)p[1], 1, 8));
                case MixerEffectType::Echo: return std::make_shared<AudioEchoFilter>(p[0], p[1], p[2], p[3]);
                case MixerEffectType::Chorus: return std::make_shared<AudioChorusFilter>(p[0], p[1], p[2], p[3], p[4]);
                case MixerEffectType::Distortion: return std::make_shared<AudioDistortionFilter>(p[0]);
                case MixerEffectType::Reverb: return std::make_shared<CustomAudioNode<ReverbNode>>();
                case MixerEffectType::Compressor: return std::make_shared<CustomAudioNode<CompressorNode>>();
                case MixerEffectType::Limiter: return std::make_shared<CustomAudioNode<LimiterNode>>();
                case MixerEffectType::Gain: return std::make_shared<CustomAudioNode<GainDbNode>>();
                default: return nullptr;
            }
        }

        void ApplyEffectParams(MixerEffect& e)
        {
            if (!e.Runtime)
                return;
            const float* p = e.Params;
            switch (e.Type)
            {
                case MixerEffectType::LowPass: static_cast<AudioLowPassFilter*>(e.Runtime.get())->SetParams(p[0], std::clamp((int)p[1], 1, 8)); break;
                case MixerEffectType::HighPass: static_cast<AudioHighPassFilter*>(e.Runtime.get())->SetParams(p[0], std::clamp((int)p[1], 1, 8)); break;
                case MixerEffectType::Echo: static_cast<AudioEchoFilter*>(e.Runtime.get())->SetParams(p[0], p[1], p[2], p[3]); break;
                case MixerEffectType::Chorus: static_cast<AudioChorusFilter*>(e.Runtime.get())->SetParams(p[0], p[1], p[2], p[3], p[4]); break;
                case MixerEffectType::Distortion: static_cast<AudioDistortionFilter*>(e.Runtime.get())->SetParams(p[0]); break;
                case MixerEffectType::Reverb:
                    if (auto* n = static_cast<CustomAudioNode<ReverbNode>*>(e.Runtime.get())->Get())
                    { n->RoomSize = p[0]; n->Damping = p[1]; n->Wet = p[2]; n->Dry = p[3]; n->Width = p[4]; }
                    break;
                case MixerEffectType::Compressor:
                    if (auto* n = static_cast<CustomAudioNode<CompressorNode>*>(e.Runtime.get())->Get())
                    { n->ThresholdDb = p[0]; n->Ratio = p[1]; n->AttackMs = p[2]; n->ReleaseMs = p[3]; n->MakeupDb = p[4]; }
                    break;
                case MixerEffectType::Limiter:
                    if (auto* n = static_cast<CustomAudioNode<LimiterNode>*>(e.Runtime.get())->Get())
                    { n->CeilingDb = p[0]; n->ReleaseMs = p[1]; }
                    break;
                case MixerEffectType::Gain:
                    if (auto* n = static_cast<CustomAudioNode<GainDbNode>*>(e.Runtime.get())->Get())
                        n->GainDb = p[0];
                    break;
                default: break;
            }
        }
    }

    // ------------------------------------------------------------------ Efekt meta verisi

    const char* MixerEffect::GetTypeName(MixerEffectType type)
    {
        switch (type)
        {
            case MixerEffectType::LowPass: return "Lowpass";
            case MixerEffectType::HighPass: return "Highpass";
            case MixerEffectType::Echo: return "Echo";
            case MixerEffectType::Reverb: return "SFX Reverb";
            case MixerEffectType::Chorus: return "Chorus";
            case MixerEffectType::Distortion: return "Distortion";
            case MixerEffectType::Compressor: return "Compressor";
            case MixerEffectType::Limiter: return "Limiter";
            case MixerEffectType::Gain: return "Gain";
            default: return "?";
        }
    }

    int MixerEffect::GetParamCount(MixerEffectType type)
    {
        switch (type)
        {
            case MixerEffectType::LowPass: case MixerEffectType::HighPass: return 2;
            case MixerEffectType::Echo: return 4;
            case MixerEffectType::Reverb: case MixerEffectType::Chorus: case MixerEffectType::Compressor: return 5;
            case MixerEffectType::Distortion: case MixerEffectType::Gain: return 1;
            case MixerEffectType::Limiter: return 2;
            default: return 0;
        }
    }

    const char* MixerEffect::GetParamName(MixerEffectType type, int i)
    {
        static const char* lp[] = { "Cutoff", "Order" };
        static const char* echo[] = { "Delay", "Decay", "Wet Mix", "Dry Mix" };
        static const char* reverb[] = { "Room Size", "Damping", "Wet Mix", "Dry Mix", "Width" };
        static const char* chorus[] = { "Delay", "Depth", "Rate", "Wet Mix", "Dry Mix" };
        static const char* comp[] = { "Threshold", "Ratio", "Attack", "Release", "Makeup Gain" };
        static const char* lim[] = { "Ceiling", "Release" };
        switch (type)
        {
            case MixerEffectType::LowPass: case MixerEffectType::HighPass: return lp[i];
            case MixerEffectType::Echo: return echo[i];
            case MixerEffectType::Reverb: return reverb[i];
            case MixerEffectType::Chorus: return chorus[i];
            case MixerEffectType::Distortion: return "Drive";
            case MixerEffectType::Compressor: return comp[i];
            case MixerEffectType::Limiter: return lim[i];
            case MixerEffectType::Gain: return "Gain";
            default: return "?";
        }
    }

    void MixerEffect::GetParamRange(MixerEffectType type, int i, float& mn, float& mx, const char*& fmt, bool& log)
    {
        log = false;
        fmt = "%.2f";
        mn = 0.0f; mx = 1.0f;
        switch (type)
        {
            case MixerEffectType::LowPass:
            case MixerEffectType::HighPass:
                if (i == 0) { mn = 10.0f; mx = 22000.0f; fmt = "%.0f Hz"; log = true; }
                else { mn = 1.0f; mx = 8.0f; fmt = "%.0f"; }
                break;
            case MixerEffectType::Echo:
                if (i == 0) { mn = 1.0f; mx = 2000.0f; fmt = "%.0f ms"; }
                else if (i == 1) { mn = 0.0f; mx = 0.95f; }
                break;
            case MixerEffectType::Chorus:
                if (i == 0) { mn = 5.0f; mx = 60.0f; fmt = "%.1f ms"; }
                else if (i == 1) { mn = 0.0f; mx = 0.2f; fmt = "%.3f"; }
                else if (i == 2) { mn = 0.05f; mx = 8.0f; fmt = "%.2f Hz"; }
                break;
            case MixerEffectType::Compressor:
                if (i == 0) { mn = -60.0f; mx = 0.0f; fmt = "%.1f dB"; }
                else if (i == 1) { mn = 1.0f; mx = 20.0f; fmt = "%.1f : 1"; }
                else if (i == 2) { mn = 0.1f; mx = 200.0f; fmt = "%.1f ms"; log = true; }
                else if (i == 3) { mn = 5.0f; mx = 2000.0f; fmt = "%.0f ms"; log = true; }
                else { mn = 0.0f; mx = 24.0f; fmt = "%.1f dB"; }
                break;
            case MixerEffectType::Limiter:
                if (i == 0) { mn = -12.0f; mx = 0.0f; fmt = "%.1f dB"; }
                else { mn = 5.0f; mx = 1000.0f; fmt = "%.0f ms"; log = true; }
                break;
            case MixerEffectType::Gain:
                mn = -40.0f; mx = 24.0f; fmt = "%.1f dB";
                break;
            default: break;
        }
    }

    MixerEffect MixerEffect::CreateDefault(MixerEffectType type)
    {
        MixerEffect e;
        e.Type = type;
        switch (type)
        {
            case MixerEffectType::LowPass: e.Params[0] = 5000.0f; e.Params[1] = 2.0f; break;
            case MixerEffectType::HighPass: e.Params[0] = 200.0f; e.Params[1] = 2.0f; break;
            case MixerEffectType::Echo: e.Params[0] = 350.0f; e.Params[1] = 0.45f; e.Params[2] = 0.6f; e.Params[3] = 1.0f; break;
            case MixerEffectType::Reverb: e.Params[0] = 0.6f; e.Params[1] = 0.5f; e.Params[2] = 0.3f; e.Params[3] = 1.0f; e.Params[4] = 1.0f; break;
            case MixerEffectType::Chorus: e.Params[0] = 25.0f; e.Params[1] = 0.03f; e.Params[2] = 0.8f; e.Params[3] = 0.5f; e.Params[4] = 0.8f; break;
            case MixerEffectType::Distortion: e.Params[0] = 0.5f; break;
            case MixerEffectType::Compressor: e.Params[0] = -18.0f; e.Params[1] = 4.0f; e.Params[2] = 10.0f; e.Params[3] = 120.0f; e.Params[4] = 3.0f; break;
            case MixerEffectType::Limiter: e.Params[0] = -0.3f; e.Params[1] = 80.0f; break;
            case MixerEffectType::Gain: e.Params[0] = 0.0f; break;
            default: break;
        }
        return e;
    }

    // ------------------------------------------------------------------ Mikser

    struct AudioMixer::TestTone
    {
        ma_waveform Waveform{};
        ma_sound Sound{};
        double EndTime = 0.0;
        std::string GroupName;
        bool Valid = false;
        ~TestTone()
        {
            if (Valid)
            {
                ma_sound_uninit(&Sound);
                ma_waveform_uninit(&Waveform);
            }
        }
    };

    AudioMixer& AudioMixer::Get()
    {
        static AudioMixer s_Mixer;
        return s_Mixer;
    }

    void AudioMixer::Init()
    {
        if (m_Initialized)
            return;
        if (m_Groups.empty())
            ResetToDefault();
        m_Initialized = true;
        RebuildAll();
        CQ_CORE_INFO("AudioMixer initialized ({0} groups)", m_Groups.size());
    }

    void AudioMixer::Shutdown()
    {
        if (!m_Initialized)
            return;
        m_TestTones.clear();
        // Çocuklar önce (Master en son)
        for (auto it = m_Groups.rbegin(); it != m_Groups.rend(); ++it)
            DestroyRuntime(*it);
        m_Initialized = false;
        ++m_RoutingGeneration;
    }

    void AudioMixer::BeginPlaySession()
    {
        m_PlaySessionState.clear();
        for (const auto& g : m_Groups)
            m_PlaySessionState.push_back({ g.Name, g.VolumeDb, g.Mute });
        m_InPlaySession = true;
    }

    void AudioMixer::EndPlaySession()
    {
        if (!m_InPlaySession)
            return;
        m_InPlaySession = false;
        for (const auto& saved : m_PlaySessionState)
            if (MixerGroup* g = FindGroup(saved.Name))
            {
                g->VolumeDb = saved.VolumeDb;
                g->Mute = saved.Mute;
            }
        m_PlaySessionState.clear();
    }

    void AudioMixer::ResetToDefault()
    {
        bool wasInit = m_Initialized;
        if (wasInit)
            Shutdown();
        m_Groups.clear();
        m_Snapshots.clear();

        MixerGroup master;
        master.Name = "Master";
        master.Color = glm::vec3(0.95f, 0.70f, 0.25f);
        master.Effects.push_back(MixerEffect::CreateDefault(MixerEffectType::Limiter));
        m_Groups.push_back(master);

        auto add = [&](const char* name, glm::vec3 color) {
            MixerGroup g;
            g.Name = name;
            g.Parent = "Master";
            g.Color = color;
            m_Groups.push_back(g);
        };
        add("Music", glm::vec3(0.55f, 0.45f, 0.95f));
        add("SFX", glm::vec3(0.35f, 0.80f, 0.55f));
        add("UI", glm::vec3(0.35f, 0.80f, 0.90f));
        add("Voice", glm::vec3(0.95f, 0.45f, 0.55f));

        // Diyalog konuşurken müziği kıs (örnek ducking)
        m_Groups[1].DuckSource = "Voice";

        CaptureSnapshot("Default");
        if (wasInit)
            Init();
    }

    MixerGroup* AudioMixer::FindGroup(const std::string& name)
    {
        int i = FindGroupIndex(name);
        return i >= 0 ? &m_Groups[(size_t)i] : nullptr;
    }

    int AudioMixer::FindGroupIndex(const std::string& name) const
    {
        for (size_t i = 0; i < m_Groups.size(); ++i)
            if (m_Groups[i].Name == name)
                return (int)i;
        return -1;
    }

    std::string AudioMixer::MakeUniqueName(const std::string& base) const
    {
        if (FindGroupIndex(base) < 0)
            return base;
        for (int i = 1; ; ++i)
        {
            std::string n = base + " " + std::to_string(i);
            if (FindGroupIndex(n) < 0)
                return n;
        }
    }

    bool AudioMixer::IsDescendant(const std::string& group, const std::string& ancestor) const
    {
        std::string cur = group;
        for (int depth = 0; depth < 64; ++depth)
        {
            int i = FindGroupIndex(cur);
            if (i < 0 || m_Groups[(size_t)i].Parent.empty())
                return false;
            cur = m_Groups[(size_t)i].Parent;
            if (cur == ancestor)
                return true;
        }
        return false;
    }

    MixerGroup* AudioMixer::AddGroup(const std::string& name, const std::string& parent)
    {
        MixerGroup g;
        g.Parent = FindGroupIndex(parent) >= 0 ? parent : std::string("Master");
        // Audio Mixer varlığının grubuna eklenen grup o varlığa ait ("<Mikser>/<ad>")
        std::string base = name.empty() ? std::string("Group") : name;
        if (const MixerGroup* p = FindGroup(g.Parent); p && !p->Asset.empty())
        {
            g.Asset = p->Asset;
            const std::string prefix = AssetPrefix(p->Asset) + "/";
            if (base.rfind(prefix, 0) != 0)
                base = prefix + base;
        }
        g.Name = MakeUniqueName(base);
        static const glm::vec3 palette[] = { { 0.35f, 0.62f, 0.95f }, { 0.95f, 0.55f, 0.30f }, { 0.55f, 0.85f, 0.40f },
                                             { 0.80f, 0.45f, 0.90f }, { 0.95f, 0.80f, 0.35f }, { 0.40f, 0.85f, 0.85f } };
        g.Color = palette[m_Groups.size() % 6];
        m_Groups.push_back(g);
        MixerGroup& added = m_Groups.back();
        if (m_Initialized)
        {
            CreateRuntime(added);
            WireGroup(added);
        }
        return &m_Groups.back();
    }

    bool AudioMixer::RemoveGroup(const std::string& name)
    {
        int idx = FindGroupIndex(name);
        if (idx <= 0) // Master silinemez
            return false;
        std::string parent = m_Groups[(size_t)idx].Parent;
        // Çocuklar üst gruba
        for (auto& g : m_Groups)
            if (g.Parent == name)
                g.Parent = parent;
        DestroyRuntime(m_Groups[(size_t)idx]);
        m_Groups.erase(m_Groups.begin() + idx);
        for (auto& g : m_Groups)
        {
            if (g.DuckSource == name)
                g.DuckSource.clear();
            if (g.Parent == parent)
                WireGroup(g);
        }
        for (auto& g : m_Groups)
            WireGroup(g);
        ++m_RoutingGeneration;
        return true;
    }

    bool AudioMixer::RenameGroup(const std::string& oldName, const std::string& newName)
    {
        int idx = FindGroupIndex(oldName);
        if (idx <= 0 || newName.empty())
            return false;
        // Varlık grubu: "<Mikser>/" öneki korunur; varlığın Master'ı yeniden adlandırılamaz
        std::string finalName = newName;
        if (const std::string& asset = m_Groups[(size_t)idx].Asset; !asset.empty())
        {
            const std::string prefix = AssetPrefix(asset) + "/";
            if (oldName == prefix + "Master")
                return false;
            if (finalName.rfind(prefix, 0) != 0)
                finalName = prefix + finalName;
        }
        if (FindGroupIndex(finalName) >= 0)
            return false;
        const std::string& resolvedNew = finalName;
        m_Groups[(size_t)idx].Name = resolvedNew;
        for (auto& g : m_Groups)
        {
            if (g.Parent == oldName) g.Parent = resolvedNew;
            if (g.DuckSource == oldName) g.DuckSource = resolvedNew;
        }
        for (auto& s : m_Snapshots)
        {
            if (auto it = s.VolumesDb.find(oldName); it != s.VolumesDb.end()) { s.VolumesDb[resolvedNew] = it->second; s.VolumesDb.erase(it); }
            if (auto it = s.Mutes.find(oldName); it != s.Mutes.end()) { s.Mutes[resolvedNew] = it->second; s.Mutes.erase(it); }
        }
        ++m_RoutingGeneration; // kaynaklar adla bağlanır
        return true;
    }

    bool AudioMixer::SetGroupParent(const std::string& name, const std::string& parent)
    {
        int idx = FindGroupIndex(name);
        if (idx <= 0 || FindGroupIndex(parent) < 0 || name == parent || IsDescendant(parent, name))
            return false;
        m_Groups[(size_t)idx].Parent = parent;
        WireGroup(m_Groups[(size_t)idx]);
        return true;
    }

    void AudioMixer::CreateRuntime(MixerGroup& group)
    {
        ma_engine* engine = Engine();
        if (!engine || group.RT->Group)
            return;
        group.RT = std::make_shared<MixerGroup::Runtime>();
        auto* sg = new ma_sound_group();
        if (ma_sound_group_init(engine, MA_SOUND_FLAG_NO_SPATIALIZATION, nullptr, sg) != MA_SUCCESS)
        {
            CQ_CORE_ERROR("AudioMixer: failed to create group '{0}'", group.Name);
            delete sg;
            return;
        }
        ma_sound_group_start(sg);
        group.RT->Group = sg;

        auto meter = std::make_shared<CustomAudioNode<MeterNode>>();
        if (meter->Get())
            meter->Get()->Data = group.RT->MeterData;
        group.RT->Meter = meter;

        for (auto& e : group.Effects)
        {
            e.Runtime = CreateEffectNode(e);
            ApplyEffectParams(e);
        }
    }

    void AudioMixer::DestroyRuntime(MixerGroup& group)
    {
        // Bu gruba bağlı test tonları önce kapatılır
        m_TestTones.erase(std::remove_if(m_TestTones.begin(), m_TestTones.end(), [&](const std::shared_ptr<TestTone>& t) {
            return t->GroupName == group.Name;
        }), m_TestTones.end());

        for (auto& e : group.Effects)
            e.Runtime.reset();
        group.RT->Meter.reset();
        if (group.RT->Group)
        {
            ma_sound_group_uninit(group.RT->Group);
            delete group.RT->Group;
            group.RT->Group = nullptr;
        }
    }

    void AudioMixer::WireGroup(MixerGroup& group)
    {
        ma_engine* engine = Engine();
        if (!engine || !group.RT->Group)
            return;

        // Zincir: grup -> efektler -> ölçer -> üst grup (Master: uç nokta)
        std::vector<ma_node*> chain;
        chain.push_back((ma_node*)group.RT->Group);
        if (!group.BypassEffects)
        {
            for (auto& e : group.Effects)
                if (!e.Bypass && e.Runtime && e.Runtime->GetNativeNode())
                    chain.push_back((ma_node*)e.Runtime->GetNativeNode());
        }
        if (group.RT->Meter && group.RT->Meter->GetNativeNode())
            chain.push_back((ma_node*)group.RT->Meter->GetNativeNode());

        // Önce bypass edilmiş efektlerin çıkışlarını kopar
        for (auto& e : group.Effects)
            if (e.Runtime && e.Runtime->GetNativeNode())
                ma_node_detach_output_bus(e.Runtime->GetNativeNode(), 0);

        for (size_t i = 0; i + 1 < chain.size(); ++i)
            ma_node_attach_output_bus(chain[i], 0, chain[i + 1], 0);

        ma_node* target = (ma_node*)ma_engine_get_endpoint(engine);
        if (!group.Parent.empty())
        {
            if (MixerGroup* parent = FindGroup(group.Parent); parent && parent->RT->Group && parent != &group)
                target = (ma_node*)parent->RT->Group;
        }
        ma_node_attach_output_bus(chain.back(), 0, target, 0);
    }

    void AudioMixer::RebuildAll()
    {
        if (!m_Initialized)
            return;
        for (auto& g : m_Groups)
            CreateRuntime(g);
        for (auto& g : m_Groups)
            WireGroup(g);
        ++m_RoutingGeneration;
    }

    void AudioMixer::RebuildChain(const std::string& name)
    {
        MixerGroup* g = FindGroup(name);
        if (!g || !m_Initialized)
            return;
        for (auto& e : g->Effects)
        {
            if (!e.Runtime)
            {
                e.Runtime = CreateEffectNode(e);
                ApplyEffectParams(e);
            }
        }
        WireGroup(*g);
    }

    void AudioMixer::UpdateEffectParams(const std::string& name)
    {
        if (MixerGroup* g = FindGroup(name))
            for (auto& e : g->Effects)
                ApplyEffectParams(e);
    }

    ma_node_base* AudioMixer::GetGroupInput(const std::string& name)
    {
        if (!m_Initialized)
            return nullptr;
        MixerGroup* g = FindGroup(name.empty() ? std::string("Master") : name);
        if (!g)
            g = &m_Groups[0];
        return g->RT->Group ? (ma_node_base*)g->RT->Group : nullptr;
    }

    void AudioMixer::Connect(AudioNode* node, const std::string& group)
    {
        if (!node || !node->GetNativeNode())
            return;
        if (ma_node_base* input = GetGroupInput(group))
            ma_node_attach_output_bus(node->GetNativeNode(), 0, input, 0);
        else
            node->ConnectToEndpoint();
    }

    bool AudioMixer::IsAudible(const MixerGroup& group, bool anySolo) const
    {
        // Kendisi ya da atalarından biri susturulmuşsa duyulmaz
        const MixerGroup* cur = &group;
        for (int depth = 0; cur && depth < 64; ++depth)
        {
            if (cur->Mute)
                return false;
            int p = cur->Parent.empty() ? -1 : FindGroupIndex(cur->Parent);
            cur = p >= 0 ? &m_Groups[(size_t)p] : nullptr;
        }
        if (!anySolo || group.Name == "Master")
            return true;
        // Solo: kendisi/atası/torunu solo ise duyulur
        for (const auto& g : m_Groups)
        {
            if (!g.Solo)
                continue;
            if (g.Name == group.Name || IsDescendant(group.Name, g.Name) || IsDescendant(g.Name, group.Name))
                return true;
        }
        return false;
    }

    void AudioMixer::Update()
    {
        if (!m_Initialized)
            return;
        const double now = NowSeconds();
        float dt = m_LastUpdate > 0.0 ? (float)(now - m_LastUpdate) : 0.016f;
        if (dt < 0.002f)
            return; // aynı karede ikinci çağrı
        dt = std::min(dt, 0.25f);
        m_LastUpdate = now;

        // Süresi dolan test tonları
        m_TestTones.erase(std::remove_if(m_TestTones.begin(), m_TestTones.end(),
                                         [&](const std::shared_ptr<TestTone>& t) { return now >= t->EndTime; }),
                          m_TestTones.end());

        // Snapshot geçişi
        if (m_SnapshotT < 1.0f)
        {
            m_SnapshotT = m_SnapshotDuration > 0.0f ? std::min(1.0f, m_SnapshotT + dt / m_SnapshotDuration) : 1.0f;
            float t = m_SnapshotT * m_SnapshotT * (3.0f - 2.0f * m_SnapshotT);
            for (auto& g : m_Groups)
                g.VolumeDb = g.RT->SnapshotFromDb + (g.RT->SnapshotToDb - g.RT->SnapshotFromDb) * t;
        }

        bool anySolo = false;
        for (auto& g : m_Groups)
            anySolo |= g.Solo;

        for (auto& g : m_Groups)
        {
            auto& rt = *g.RT;

            // Ölçer okuma (tepe: son okumadan beri en yüksek)
            // Ses çıkışı duraklatılınca ölçüm callback'i çalışmaz: son RMS değeri donmasın
            if (AudioEngine::IsInitialized() && AudioEngine::Get().IsPaused())
            {
                rt.MeterData->RmsL.store(0.0f);
                rt.MeterData->RmsR.store(0.0f);
            }
            float pl = rt.MeterData->PeakL.exchange(0.0f), pr = rt.MeterData->PeakR.exchange(0.0f);
            float rl = rt.MeterData->RmsL.load(), rr = rt.MeterData->RmsR.load();
            const float fall = std::pow(10.0f, -24.0f * dt / 20.0f); // 24 dB/s düşüş
            rt.DisplayPeakL = std::max(pl, rt.DisplayPeakL * fall);
            rt.DisplayPeakR = std::max(pr, rt.DisplayPeakR * fall);
            rt.DisplayRmsL = std::max(rl, rt.DisplayRmsL * fall);
            rt.DisplayRmsR = std::max(rr, rt.DisplayRmsR * fall);
            rt.HoldTimer -= dt;
            if (pl >= rt.HoldL || pr >= rt.HoldR || rt.HoldTimer <= 0.0f)
            {
                if (rt.HoldTimer <= 0.0f) { rt.HoldL = 0.0f; rt.HoldR = 0.0f; }
                rt.HoldL = std::max(rt.HoldL, pl);
                rt.HoldR = std::max(rt.HoldR, pr);
                rt.HoldTimer = 1.5f;
            }
            if (pl >= 1.0f || pr >= 1.0f)
                rt.Clipped = true;

            // Ducking
            float duckTarget = 0.0f;
            if (!g.DuckSource.empty())
            {
                if (MixerGroup* src = FindGroup(g.DuckSource))
                {
                    float level = LinearToDb(std::max(src->RT->DisplayRmsL, src->RT->DisplayRmsR));
                    if (level > g.DuckThresholdDb)
                        duckTarget = g.DuckAmountDb;
                }
            }
            float tau = duckTarget < rt.DuckGainDb ? g.DuckAttack : g.DuckRelease;
            float k = 1.0f - std::exp(-dt / std::max(0.001f, tau));
            rt.DuckGainDb += (duckTarget - rt.DuckGainDb) * k;

            if (rt.Group)
            {
                float volume = IsAudible(g, anySolo) ? DbToLinear(std::clamp(g.VolumeDb + rt.DuckGainDb, -80.0f, 24.0f)) : 0.0f;
                ma_sound_group_set_volume(rt.Group, volume);
                ma_sound_group_set_pan(rt.Group, std::clamp(g.Pan, -1.0f, 1.0f));
                ma_sound_group_set_pitch(rt.Group, std::clamp(g.Pitch, 0.1f, 4.0f));
            }
        }
    }

    // ------------------------------------------------------------------ Snapshot

    void AudioMixer::CaptureSnapshot(const std::string& name)
    {
        MixerSnapshot* snap = nullptr;
        for (auto& s : m_Snapshots)
            if (s.Name == name)
                snap = &s;
        if (!snap)
        {
            m_Snapshots.push_back({});
            snap = &m_Snapshots.back();
            snap->Name = name;
        }
        snap->VolumesDb.clear();
        snap->Mutes.clear();
        for (auto& g : m_Groups)
        {
            snap->VolumesDb[g.Name] = g.VolumeDb;
            snap->Mutes[g.Name] = g.Mute;
        }
    }

    void AudioMixer::ApplySnapshot(const std::string& name, float transitionSeconds)
    {
        for (auto& s : m_Snapshots)
        {
            if (s.Name != name)
                continue;
            for (auto& g : m_Groups)
            {
                g.RT->SnapshotFromDb = g.VolumeDb;
                auto it = s.VolumesDb.find(g.Name);
                g.RT->SnapshotToDb = it != s.VolumesDb.end() ? it->second : g.VolumeDb;
                if (auto m = s.Mutes.find(g.Name); m != s.Mutes.end())
                    g.Mute = m->second;
            }
            m_SnapshotDuration = std::max(0.0f, transitionSeconds);
            m_SnapshotT = 0.0f;
            if (m_SnapshotDuration <= 0.0f)
            {
                m_SnapshotT = 1.0f;
                for (auto& g : m_Groups)
                    g.VolumeDb = g.RT->SnapshotToDb;
            }
            return;
        }
    }

    // ------------------------------------------------------------------ Test tonu

    void AudioMixer::PlayTestTone(const std::string& groupName, float frequency, float seconds, float amplitude)
    {
        ma_engine* engine = Engine();
        MixerGroup* g = FindGroup(groupName);
        if (!engine || !g || !g->RT->Group)
            return;
        auto tone = std::make_shared<TestTone>();
        ma_waveform_config cfg = ma_waveform_config_init(ma_format_f32, ma_engine_get_channels(engine), ma_engine_get_sample_rate(engine),
                                                         ma_waveform_type_sine, amplitude, frequency);
        if (ma_waveform_init(&cfg, &tone->Waveform) != MA_SUCCESS)
            return;
        if (ma_sound_init_from_data_source(engine, &tone->Waveform, MA_SOUND_FLAG_NO_SPATIALIZATION, g->RT->Group, &tone->Sound) != MA_SUCCESS)
        {
            ma_waveform_uninit(&tone->Waveform);
            return;
        }
        tone->Valid = true;
        tone->GroupName = g->Name;
        tone->EndTime = NowSeconds() + seconds;
        ma_sound_start(&tone->Sound);
        m_TestTones.push_back(tone);
    }

    void AudioMixer::StopTestTones()
    {
        m_TestTones.clear();
    }

    // ------------------------------------------------------------------ Kayıt

    // Mikser dosyası yazıcı (proje mikseri ve .cqmixer varlıkları aynı biçim)
    static bool WriteMixerFile(const std::filesystem::path& path, const std::vector<MixerGroup>& m_Groups, const std::vector<MixerSnapshot>& m_Snapshots)
    {
        YAML::Emitter out;
        out << YAML::BeginMap;
        out << YAML::Key << "AudioMixer" << YAML::Value << 1;
        out << YAML::Key << "Groups" << YAML::Value << YAML::BeginSeq;
        for (const auto& g : m_Groups)
        {
            out << YAML::BeginMap;
            out << YAML::Key << "Name" << YAML::Value << g.Name;
            out << YAML::Key << "Parent" << YAML::Value << g.Parent;
            out << YAML::Key << "Volume" << YAML::Value << g.VolumeDb;
            out << YAML::Key << "Pan" << YAML::Value << g.Pan;
            out << YAML::Key << "Pitch" << YAML::Value << g.Pitch;
            out << YAML::Key << "Mute" << YAML::Value << g.Mute;
            out << YAML::Key << "Solo" << YAML::Value << g.Solo;
            out << YAML::Key << "BypassEffects" << YAML::Value << g.BypassEffects;
            out << YAML::Key << "Color" << YAML::Value << YAML::Flow << YAML::BeginSeq << g.Color.r << g.Color.g << g.Color.b << YAML::EndSeq;
            out << YAML::Key << "DuckSource" << YAML::Value << g.DuckSource;
            out << YAML::Key << "DuckThreshold" << YAML::Value << g.DuckThresholdDb;
            out << YAML::Key << "DuckAmount" << YAML::Value << g.DuckAmountDb;
            out << YAML::Key << "DuckAttack" << YAML::Value << g.DuckAttack;
            out << YAML::Key << "DuckRelease" << YAML::Value << g.DuckRelease;
            out << YAML::Key << "Effects" << YAML::Value << YAML::BeginSeq;
            for (const auto& e : g.Effects)
            {
                out << YAML::BeginMap;
                out << YAML::Key << "Type" << YAML::Value << MixerEffect::GetTypeName(e.Type);
                out << YAML::Key << "Bypass" << YAML::Value << e.Bypass;
                out << YAML::Key << "Params" << YAML::Value << YAML::Flow << YAML::BeginSeq;
                for (float p : e.Params) out << p;
                out << YAML::EndSeq;
                out << YAML::EndMap;
            }
            out << YAML::EndSeq;
            out << YAML::EndMap;
        }
        out << YAML::EndSeq;
        out << YAML::Key << "Snapshots" << YAML::Value << YAML::BeginSeq;
        for (const auto& s : m_Snapshots)
        {
            out << YAML::BeginMap << YAML::Key << "Name" << YAML::Value << s.Name;
            out << YAML::Key << "Volumes" << YAML::Value << YAML::BeginMap;
            for (const auto& [k, v] : s.VolumesDb) out << YAML::Key << k << YAML::Value << v;
            out << YAML::EndMap;
            out << YAML::Key << "Mutes" << YAML::Value << YAML::BeginMap;
            for (const auto& [k, v] : s.Mutes) out << YAML::Key << k << YAML::Value << v;
            out << YAML::EndMap << YAML::EndMap;
        }
        out << YAML::EndSeq << YAML::EndMap;

        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
        std::filesystem::path tmp = path;
        tmp += ".tmp";
        {
            std::ofstream f(tmp, std::ios::binary);
            if (!f)
                return false;
            f << out.c_str();
        }
        std::filesystem::rename(tmp, path, ec);
        if (ec)
        {
            CQ_CORE_ERROR("AudioMixer save failed: {0}", ec.message());
            return false;
        }
        return true;
    }

    // Mikser dosyası okuyucu
    static bool ParseMixerFile(const std::filesystem::path& path, std::vector<MixerGroup>& outGroups, std::vector<MixerSnapshot>& outSnapshots)
    {
        YAML::Node root;
        try
        {
            root = YAML::LoadFile(path.string());
        }
        catch (const std::exception& e)
        {
            CQ_CORE_ERROR("AudioMixer load failed ({0}): {1}", path.string(), e.what());
            return false;
        }
        if (!root["Groups"])
            return false;

        std::vector<MixerGroup> groups;
        for (const auto& gn : root["Groups"])
        {
            MixerGroup g;
            g.Name = gn["Name"].as<std::string>("Group");
            g.Parent = gn["Parent"].as<std::string>("");
            g.VolumeDb = gn["Volume"].as<float>(0.0f);
            g.Pan = gn["Pan"].as<float>(0.0f);
            g.Pitch = gn["Pitch"].as<float>(1.0f);
            g.Mute = gn["Mute"].as<bool>(false);
            g.Solo = gn["Solo"].as<bool>(false);
            g.BypassEffects = gn["BypassEffects"].as<bool>(false);
            if (auto c = gn["Color"]; c && c.IsSequence() && c.size() == 3)
                g.Color = glm::vec3(c[0].as<float>(), c[1].as<float>(), c[2].as<float>());
            g.DuckSource = gn["DuckSource"].as<std::string>("");
            g.DuckThresholdDb = gn["DuckThreshold"].as<float>(-30.0f);
            g.DuckAmountDb = gn["DuckAmount"].as<float>(-12.0f);
            g.DuckAttack = gn["DuckAttack"].as<float>(0.05f);
            g.DuckRelease = gn["DuckRelease"].as<float>(0.4f);
            for (const auto& en : gn["Effects"])
            {
                std::string type = en["Type"].as<std::string>("");
                for (int t = 0; t < (int)MixerEffectType::Count; ++t)
                {
                    if (type == MixerEffect::GetTypeName((MixerEffectType)t))
                    {
                        MixerEffect e = MixerEffect::CreateDefault((MixerEffectType)t);
                        e.Bypass = en["Bypass"].as<bool>(false);
                        if (auto p = en["Params"]; p && p.IsSequence())
                            for (size_t i = 0; i < std::min<size_t>(5, p.size()); ++i)
                                e.Params[i] = p[i].as<float>(e.Params[i]);
                        g.Effects.push_back(e);
                        break;
                    }
                }
            }
            groups.push_back(std::move(g));
        }
        // Master her zaman ilk ve kök
        auto masterIt = std::find_if(groups.begin(), groups.end(), [](const MixerGroup& g) { return g.Name == "Master"; });
        if (masterIt == groups.end())
        {
            MixerGroup master;
            master.Name = "Master";
            groups.insert(groups.begin(), master);
        }
        else if (masterIt != groups.begin())
        {
            std::rotate(groups.begin(), masterIt, masterIt + 1);
        }
        groups[0].Parent.clear();
        for (size_t i = 1; i < groups.size(); ++i)
            if (groups[i].Parent.empty())
                groups[i].Parent = "Master";

        std::vector<MixerSnapshot> snapshots;
        for (const auto& sn : root["Snapshots"])
        {
            MixerSnapshot s;
            s.Name = sn["Name"].as<std::string>("Snapshot");
            for (const auto& kv : sn["Volumes"]) s.VolumesDb[kv.first.as<std::string>()] = kv.second.as<float>(0.0f);
            for (const auto& kv : sn["Mutes"]) s.Mutes[kv.first.as<std::string>()] = kv.second.as<bool>(false);
            snapshots.push_back(std::move(s));
        }

        outGroups = std::move(groups);
        outSnapshots = std::move(snapshots);
        return true;
    }

    bool AudioMixer::Load(const std::filesystem::path& path)
    {
        std::vector<MixerGroup> groups;
        std::vector<MixerSnapshot> snapshots;
        if (!ParseMixerFile(path, groups, snapshots))
            return false;
        // Yüklü Audio Mixer varlıklarının grupları korunur
        for (auto& g : m_Groups)
            if (!g.Asset.empty())
                groups.push_back(g);
        bool wasInit = m_Initialized;
        if (wasInit)
            Shutdown();
        m_Groups = std::move(groups);
        m_Snapshots = std::move(snapshots);
        m_FilePath = path;
        if (wasInit)
            Init();
        CQ_CORE_INFO("AudioMixer loaded: {0} ({1} groups)", path.string(), m_Groups.size());
        return true;
    }

    bool AudioMixer::Save(const std::filesystem::path& path) const
    {
        // Proje mikseri: yalnız varlığa ait olmayan gruplar
        std::vector<MixerGroup> project;
        for (const auto& g : m_Groups)
            if (g.Asset.empty())
                project.push_back(g);
        return WriteMixerFile(path, project, m_Snapshots);
    }

    std::string AudioMixer::AssetPrefix(const std::filesystem::path& assetPath)
    {
        return assetPath.stem().string();
    }

    bool AudioMixer::LoadAssetMixer(const std::filesystem::path& path)
    {
        std::vector<MixerGroup> groups;
        std::vector<MixerSnapshot> snapshots;
        if (!ParseMixerFile(path, groups, snapshots))
            return false;
        std::string output = "Master";
        try
        {
            YAML::Node root = YAML::LoadFile(path.string());
            if (root["Output"]) output = root["Output"].as<std::string>("Master");
        }
        catch (...) {}
        const std::string key = std::filesystem::weakly_canonical(path).string();
        const std::string prefix = AssetPrefix(path);
        for (auto& g : groups)
        {
            const bool isMaster = g.Name == "Master";
            g.Name = prefix + "/" + g.Name;
            g.Parent = isMaster ? (output.empty() ? std::string("Master") : output) : prefix + "/" + (g.Parent.empty() ? "Master" : g.Parent);
            if (!g.DuckSource.empty() && g.DuckSource.find('/') == std::string::npos && g.DuckSource != "Master")
                g.DuckSource = prefix + "/" + g.DuckSource;
            g.Asset = key;
        }
        const bool wasInit = m_Initialized;
        if (wasInit)
            Shutdown();
        m_Groups.erase(std::remove_if(m_Groups.begin(), m_Groups.end(), [&](const MixerGroup& g) { return g.Asset == key; }), m_Groups.end());
        for (auto& g : groups)
            m_Groups.push_back(std::move(g));
        std::error_code ec;
        m_AssetTimes[key] = std::filesystem::last_write_time(path, ec);
        if (wasInit)
            Init();
        ++m_RoutingGeneration;
        CQ_CORE_INFO("Audio Mixer asset loaded: {0}", path.filename().string());
        return true;
    }

    bool AudioMixer::SaveAssetMixer(const std::filesystem::path& path) const
    {
        const std::string key = std::filesystem::weakly_canonical(path).string();
        const std::string prefix = AssetPrefix(path) + "/";
        std::vector<MixerGroup> groups;
        std::string output = "Master";
        for (const auto& g : m_Groups)
        {
            if (g.Asset != key)
                continue;
            MixerGroup c = g;
            auto strip = [&](std::string& n) { if (n.rfind(prefix, 0) == 0) n = n.substr(prefix.size()); };
            if (c.Name == prefix + "Master")
                output = c.Parent;
            strip(c.Name);
            if (c.Name == "Master")
                c.Parent.clear();
            else
                strip(c.Parent);
            strip(c.DuckSource);
            c.Asset.clear();
            groups.push_back(c);
        }
        if (groups.empty())
            return false;
        if (!WriteMixerFile(path, groups, {}))
            return false;
        // Çıkış grubu (başka miksere yönlendirme)
        try
        {
            YAML::Node root = YAML::LoadFile(path.string());
            root["Output"] = output;
            YAML::Emitter e;
            e << root;
            std::ofstream(path, std::ios::trunc) << e.c_str();
        }
        catch (...) {}
        return true;
    }

    bool AudioMixer::SaveAll() const
    {
        bool ok = SaveProject();
        std::vector<std::string> assets;
        for (const auto& g : m_Groups)
            if (!g.Asset.empty() && std::find(assets.begin(), assets.end(), g.Asset) == assets.end())
                assets.push_back(g.Asset);
        for (const auto& a : assets)
            ok = SaveAssetMixer(a) && ok;
        return ok;
    }

    void AudioMixer::LoadAllAssetMixers()
    {
        if (m_ProjectDirectory.empty())
            return;
        std::error_code ec;
        for (auto it = std::filesystem::recursive_directory_iterator(m_ProjectDirectory / "Assets", std::filesystem::directory_options::skip_permission_denied, ec);
             it != std::filesystem::recursive_directory_iterator(); it.increment(ec))
            if (!ec && it->path().extension() == ".cqmixer")
                LoadAssetMixer(it->path());
    }

    int AudioMixer::CheckAssetChanges()
    {
        int changed = 0;
        std::vector<std::string> gone;
        for (auto& [key, time] : m_AssetTimes)
        {
            std::error_code ec;
            if (!std::filesystem::exists(key, ec))
            {
                gone.push_back(key);
                continue;
            }
            const auto t = std::filesystem::last_write_time(key, ec);
            if (!ec && t != time && LoadAssetMixer(key))
                ++changed;
        }
        for (const auto& key : gone)
        {
            m_AssetTimes.erase(key);
            const bool wasInit = m_Initialized;
            if (wasInit) Shutdown();
            m_Groups.erase(std::remove_if(m_Groups.begin(), m_Groups.end(), [&](const MixerGroup& g) { return g.Asset == key; }), m_Groups.end());
            if (wasInit) Init();
            ++m_RoutingGeneration;
            ++changed;
        }
        return changed;
    }

    void AudioMixer::EnsureProjectLoaded(const std::filesystem::path& projectDirectory)
    {
        if (projectDirectory.empty() || projectDirectory == m_ProjectDirectory)
            return;
        m_ProjectDirectory = projectDirectory;
        std::filesystem::path path = projectDirectory / "ProjectSettings" / "AudioMixer.yaml";
        std::error_code ec;
        if (!std::filesystem::exists(path, ec) || !Load(path))
        {
            ResetToDefault();
            m_FilePath = path;
        }
        // Projedeki Audio Mixer varlıkları (Unity: her .mixer kendi grup ağacı)
        m_Groups.erase(std::remove_if(m_Groups.begin(), m_Groups.end(), [](const MixerGroup& g) { return !g.Asset.empty(); }), m_Groups.end());
        m_AssetTimes.clear();
        LoadAllAssetMixers();
    }
}
