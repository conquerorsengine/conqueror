#include "AudioFilter.h"
#include "AudioEngine.h"
#include "Buffer/AudioBuffer.h"
#include "DSP/AudioDSPUtils.h"
#include "Core/Logging/Log.h"

#include <miniaudio.h>
#include <algorithm>
#include <cmath>

namespace Conqueror
{
    namespace
    {
        struct DistortionNode
        {
            ma_node_base BaseNode;
            float Drive = 0.5f;
            AudioBuffer ScratchBuffer;
        };

        static void DistortionNodeProcessPCMFrames(ma_node* pNode, const float** ppFramesIn, ma_uint32* pFrameCountIn, float** ppFramesOut, ma_uint32* pFrameCountOut)
        {
            auto* node = reinterpret_cast<DistortionNode*>(pNode);
            if (node == nullptr || ppFramesIn == nullptr || ppFramesOut == nullptr || ppFramesIn[0] == nullptr || ppFramesOut[0] == nullptr)
            {
                *pFrameCountOut = 0;
                *pFrameCountIn = 0;
                return;
            }

            const ma_uint32 frameCount = (*pFrameCountOut < *pFrameCountIn) ? *pFrameCountOut : *pFrameCountIn;
            const ma_uint32 channels = ma_node_get_output_channels(pNode, 0);

            const float* in = ppFramesIn[0];
            float* out = ppFramesOut[0];
            node->ScratchBuffer.Resize(channels, frameCount);
            float* scratch = node->ScratchBuffer.Data();

            for (ma_uint32 i = 0; i < frameCount * channels; ++i)
            {
                scratch[i] = AudioDSP::SoftClip(in[i], node->Drive);
                out[i] = scratch[i];
            }

            *pFrameCountIn = frameCount;
            *pFrameCountOut = frameCount;
        }

        static ma_node_vtable g_DistortionNodeVTable =
        {
            DistortionNodeProcessPCMFrames,
            nullptr,
            1,
            1,
            0
        };

        // --- Gain Node ---
        struct GainNode
        {
            ma_node_base BaseNode;
            float Gain = 1.0f;
        };

        static void GainNodeProcessPCMFrames(ma_node* pNode, const float** ppFramesIn, ma_uint32* pFrameCountIn, float** ppFramesOut, ma_uint32* pFrameCountOut)
        {
            GainNode* node = (GainNode*)pNode;
            ma_uint32 frameCount = (*pFrameCountOut < *pFrameCountIn) ? *pFrameCountOut : *pFrameCountIn;
            ma_uint32 channels = ma_node_get_output_channels(pNode, 0);

            const float* in = ppFramesIn[0];
            float* out = ppFramesOut[0];

            for (ma_uint32 i = 0; i < frameCount * channels; ++i)
            {
                out[i] = in[i] * node->Gain;
            }

            *pFrameCountIn = frameCount;
            *pFrameCountOut = frameCount;
        }

        static ma_node_vtable g_GainNodeVTable =
        {
            GainNodeProcessPCMFrames,
            nullptr,
            1,
            1,
            0
        };

        // --- Pan Node ---
        struct PanNode
        {
            ma_node_base BaseNode;
            float Pan = 0.0f; // -1.0 to 1.0
        };

        static void PanNodeProcessPCMFrames(ma_node* pNode, const float** ppFramesIn, ma_uint32* pFrameCountIn, float** ppFramesOut, ma_uint32* pFrameCountOut)
        {
            PanNode* node = (PanNode*)pNode;
            ma_uint32 frameCount = (*pFrameCountOut < *pFrameCountIn) ? *pFrameCountOut : *pFrameCountIn;
            ma_uint32 channels = ma_node_get_output_channels(pNode, 0);

            const float* in = ppFramesIn[0];
            float* out = ppFramesOut[0];

            if (channels == 2)
            {
                float leftMultiplier = std::clamp(1.0f - node->Pan, 0.0f, 1.0f);
                float rightMultiplier = std::clamp(1.0f + node->Pan, 0.0f, 1.0f);

                for (ma_uint32 i = 0; i < frameCount; ++i)
                {
                    out[i * 2 + 0] = in[i * 2 + 0] * leftMultiplier;
                    out[i * 2 + 1] = in[i * 2 + 1] * rightMultiplier;
                }
            }
            else
            {
                // Fallback for non-stereo
                for (ma_uint32 i = 0; i < frameCount * channels; ++i)
                    out[i] = in[i];
            }

            *pFrameCountIn = frameCount;
            *pFrameCountOut = frameCount;
        }

        static ma_node_vtable g_PanNodeVTable =
        {
            PanNodeProcessPCMFrames,
            nullptr,
            1,
            1,
            0
        };
    }

    // ---------------------------------------------------------
    // AudioNode Base
    // ---------------------------------------------------------
    void AudioNode::ConnectTo(AudioNode* nextNode)
    {
        if (GetNativeNode() && nextNode && nextNode->GetNativeNode())
        {
            ma_node_attach_output_bus(GetNativeNode(), 0, nextNode->GetNativeNode(), 0);
        }
    }

    void AudioNode::ConnectToEndpoint()
    {
        if (GetNativeNode())
        {
            ma_engine* engine = AudioEngine::Get().GetNativeEngine();
            if (engine)
            {
                ma_node_attach_output_bus(GetNativeNode(), 0, ma_engine_get_endpoint(engine), 0);
            }
        }
    }

    void AudioNode::DetachAllOutputs()
    {
        if (GetNativeNode())
        {
            ma_node_base* node = GetNativeNode();
            ma_uint32 outputBusCount = ma_node_get_output_bus_count(node);
            for (ma_uint32 i = 0; i < outputBusCount; ++i)
            {
                ma_node_detach_output_bus(node, i);
            }
        }
    }

    // ---------------------------------------------------------
    // AudioLowPassFilter
    // ---------------------------------------------------------
    AudioLowPassFilter::AudioLowPassFilter(float cutoffFrequency, int order)
    {
        ma_engine* engine = AudioEngine::Get().GetNativeEngine();
        if (!engine) return;

        ma_uint32 channels = ma_engine_get_channels(engine);
        ma_uint32 sampleRate = ma_engine_get_sample_rate(engine);

        ma_lpf_node_config config = ma_lpf_node_config_init(channels, sampleRate, cutoffFrequency, order);
        
        ma_lpf_node* node = new ma_lpf_node();
        if (ma_lpf_node_init(ma_engine_get_node_graph(engine), &config, nullptr, node) != MA_SUCCESS)
        {
            CQ_CORE_ERROR("AudioLowPassFilter node init failed.");
            delete node;
        }
        else
        {
            m_Node = node;
        }
    }

    AudioLowPassFilter::~AudioLowPassFilter()
    {
        if (m_Node)
        {
            ma_lpf_node* node = static_cast<ma_lpf_node*>(m_Node);
            ma_lpf_node_uninit(node, nullptr);
            delete node;
            m_Node = nullptr;
        }
    }

    void AudioLowPassFilter::SetParams(float cutoffFrequency, int order)
    {
        if (m_Node)
        {
            ma_engine* engine = AudioEngine::Get().GetNativeEngine();
            ma_uint32 channels = ma_engine_get_channels(engine);
            ma_uint32 sampleRate = ma_engine_get_sample_rate(engine);
            
            ma_lpf_config config = ma_lpf_config_init(ma_format_f32, channels, sampleRate, cutoffFrequency, order);
            ma_lpf_node_reinit(&config, static_cast<ma_lpf_node*>(m_Node));
        }
    }

    ma_node_base* AudioLowPassFilter::GetNativeNode() const
    {
        return m_Node ? &static_cast<ma_lpf_node*>(m_Node)->baseNode : nullptr;
    }

    // ---------------------------------------------------------
    // AudioHighPassFilter
    // ---------------------------------------------------------
    AudioHighPassFilter::AudioHighPassFilter(float cutoffFrequency, int order)
    {
        ma_engine* engine = AudioEngine::Get().GetNativeEngine();
        if (!engine) return;

        ma_uint32 channels = ma_engine_get_channels(engine);
        ma_uint32 sampleRate = ma_engine_get_sample_rate(engine);

        ma_hpf_node_config config = ma_hpf_node_config_init(channels, sampleRate, cutoffFrequency, order);
        
        ma_hpf_node* node = new ma_hpf_node();
        if (ma_hpf_node_init(ma_engine_get_node_graph(engine), &config, nullptr, node) != MA_SUCCESS)
        {
            CQ_CORE_ERROR("AudioHighPassFilter node init failed.");
            delete node;
        }
        else
        {
            m_Node = node;
        }
    }

    AudioHighPassFilter::~AudioHighPassFilter()
    {
        if (m_Node)
        {
            ma_hpf_node* node = static_cast<ma_hpf_node*>(m_Node);
            ma_hpf_node_uninit(node, nullptr);
            delete node;
            m_Node = nullptr;
        }
    }

    void AudioHighPassFilter::SetParams(float cutoffFrequency, int order)
    {
        if (m_Node)
        {
            ma_engine* engine = AudioEngine::Get().GetNativeEngine();
            ma_uint32 channels = ma_engine_get_channels(engine);
            ma_uint32 sampleRate = ma_engine_get_sample_rate(engine);
            
            ma_hpf_config config = ma_hpf_config_init(ma_format_f32, channels, sampleRate, cutoffFrequency, order);
            ma_hpf_node_reinit(&config, static_cast<ma_hpf_node*>(m_Node));
        }
    }

    ma_node_base* AudioHighPassFilter::GetNativeNode() const
    {
        return m_Node ? &static_cast<ma_hpf_node*>(m_Node)->baseNode : nullptr;
    }

    // ---------------------------------------------------------
    // AudioEchoFilter
    // ---------------------------------------------------------
    AudioEchoFilter::AudioEchoFilter(float delayMs, float decay, float wetMix, float dryMix)
    {
        ma_engine* engine = AudioEngine::Get().GetNativeEngine();
        if (!engine) return;

        ma_uint32 channels = ma_engine_get_channels(engine);
        ma_uint32 sampleRate = ma_engine_get_sample_rate(engine);
        ma_uint32 delayInFrames = (ma_uint32)(sampleRate * (delayMs / 1000.0f));

        ma_delay_node_config config = ma_delay_node_config_init(channels, sampleRate, delayInFrames, decay);
        
        ma_delay_node* node = new ma_delay_node();
        if (ma_delay_node_init(ma_engine_get_node_graph(engine), &config, nullptr, node) != MA_SUCCESS)
        {
            CQ_CORE_ERROR("AudioEchoFilter node init failed.");
            delete node;
        }
        else
        {
            m_Node = node;
            ma_delay_node_set_wet(node, wetMix);
            ma_delay_node_set_dry(node, dryMix);
        }
    }

    AudioEchoFilter::~AudioEchoFilter()
    {
        if (m_Node)
        {
            ma_delay_node* node = static_cast<ma_delay_node*>(m_Node);
            ma_delay_node_uninit(node, nullptr);
            delete node;
            m_Node = nullptr;
        }
    }

    void AudioEchoFilter::SetParams(float delayMs, float decay, float wetMix, float dryMix)
    {
        (void)delayMs; // delayMs miniaudio'da kolayca dinamik olarak değiştirilemez
        if (m_Node)
        {
            ma_delay_node* node = static_cast<ma_delay_node*>(m_Node);
            ma_delay_node_set_decay(node, decay);
            ma_delay_node_set_wet(node, wetMix);
            ma_delay_node_set_dry(node, dryMix);
        }
    }

    ma_node_base* AudioEchoFilter::GetNativeNode() const
    {
        return m_Node ? &static_cast<ma_delay_node*>(m_Node)->baseNode : nullptr;
    }

    // ---------------------------------------------------------
    // AudioChorusFilter
    // ---------------------------------------------------------
    AudioChorusFilter::AudioChorusFilter(float delayMs, float depth, float rate, float wetMix, float dryMix)
    {
        (void)rate;
        ma_engine* engine = AudioEngine::Get().GetNativeEngine();
        if (!engine) return;

        ma_uint32 channels = ma_engine_get_channels(engine);
        ma_uint32 sampleRate = ma_engine_get_sample_rate(engine);

        const float effectiveDelay = delayMs + (depth * 20.0f * std::clamp(rate, 0.1f, 4.0f) * 0.25f);
        ma_uint32 delayInFrames = (ma_uint32)(sampleRate * (effectiveDelay / 1000.0f));

        ma_delay_node_config config = ma_delay_node_config_init(channels, sampleRate, delayInFrames, 0.35f);

        ma_delay_node* node = new ma_delay_node();
        if (ma_delay_node_init(ma_engine_get_node_graph(engine), &config, nullptr, node) != MA_SUCCESS)
        {
            CQ_CORE_ERROR("AudioChorusFilter node init failed.");
            delete node;
        }
        else
        {
            m_Node = node;
            ma_delay_node_set_wet(node, wetMix);
            ma_delay_node_set_dry(node, dryMix);
        }
    }

    AudioChorusFilter::~AudioChorusFilter()
    {
        if (m_Node)
        {
            ma_delay_node* node = static_cast<ma_delay_node*>(m_Node);
            ma_delay_node_uninit(node, nullptr);
            delete node;
            m_Node = nullptr;
        }
    }

    void AudioChorusFilter::SetParams(float delayMs, float depth, float rate, float wetMix, float dryMix)
    {
        (void)delayMs;
        (void)depth;
        (void)rate;
        if (m_Node)
        {
            ma_delay_node* node = static_cast<ma_delay_node*>(m_Node);
            ma_delay_node_set_wet(node, wetMix);
            ma_delay_node_set_dry(node, dryMix);
            ma_delay_node_set_decay(node, 0.35f);
        }
    }

    ma_node_base* AudioChorusFilter::GetNativeNode() const
    {
        return m_Node ? &static_cast<ma_delay_node*>(m_Node)->baseNode : nullptr;
    }

    // ---------------------------------------------------------
    // AudioReverbFilter
    // ---------------------------------------------------------
    AudioReverbFilter::AudioReverbFilter(float roomSize, float damping, float wetMix, float dryMix)
    {
        ma_engine* engine = AudioEngine::Get().GetNativeEngine();
        if (!engine) return;

        ma_uint32 channels = ma_engine_get_channels(engine);
        ma_uint32 sampleRate = ma_engine_get_sample_rate(engine);
        ma_uint32 delayInFrames = (ma_uint32)(sampleRate * (roomSize * 0.1f)); 

        // ma_delay_node decay must be < 1.0 to prevent feedback explosion
        float clampedDamping = std::max(0.0f, std::min(damping, 0.95f));
        ma_delay_node_config config = ma_delay_node_config_init(channels, sampleRate, delayInFrames, clampedDamping);
        
        ma_delay_node* node = new ma_delay_node();
        if (ma_delay_node_init(ma_engine_get_node_graph(engine), &config, nullptr, node) != MA_SUCCESS)
        {
            CQ_CORE_ERROR("AudioReverbFilter node init failed.");
            delete node;
        }
        else
        {
            m_Node = node;
            ma_delay_node_set_wet(node, wetMix);
            ma_delay_node_set_dry(node, dryMix);
        }
    }

    AudioReverbFilter::~AudioReverbFilter()
    {
        if (m_Node)
        {
            ma_delay_node* node = static_cast<ma_delay_node*>(m_Node);
            ma_delay_node_uninit(node, nullptr);
            delete node;
            m_Node = nullptr;
        }
    }

    void AudioReverbFilter::SetParams(float roomSize, float damping, float wetMix, float dryMix)
    {
        (void)roomSize;
        if (m_Node)
        {
            ma_delay_node* node = static_cast<ma_delay_node*>(m_Node);
            float clampedDamping = std::max(0.0f, std::min(damping, 0.95f));
            ma_delay_node_set_decay(node, clampedDamping);
            ma_delay_node_set_wet(node, wetMix);
            ma_delay_node_set_dry(node, dryMix);
        }
    }

    ma_node_base* AudioReverbFilter::GetNativeNode() const
    {
        return m_Node ? &static_cast<ma_delay_node*>(m_Node)->baseNode : nullptr;
    }

    // ---------------------------------------------------------
    // AudioDistortionFilter
    // ---------------------------------------------------------
    AudioDistortionFilter::AudioDistortionFilter(float drive)
    {
        ma_engine* engine = AudioEngine::Get().GetNativeEngine();
        if (!engine) return;

        ma_uint32 channels = ma_engine_get_channels(engine);
        ma_uint32 inputChannels[1] = { channels };
        ma_uint32 outputChannels[1] = { channels };

        ma_node_config config = ma_node_config_init();
        config.vtable = &g_DistortionNodeVTable;
        config.pInputChannels = inputChannels;
        config.pOutputChannels = outputChannels;

        DistortionNode* node = new DistortionNode();
        node->Drive = drive;

        if (ma_node_init(ma_engine_get_node_graph(engine), &config, nullptr, reinterpret_cast<ma_node*>(&node->BaseNode)) != MA_SUCCESS)
        {
            CQ_CORE_ERROR("AudioDistortionFilter node init failed.");
            delete node;
        }
        else
        {
            m_Node = node;
        }
    }

    AudioDistortionFilter::~AudioDistortionFilter()
    {
        if (m_Node)
        {
            auto* node = static_cast<DistortionNode*>(m_Node);
            ma_node_uninit(reinterpret_cast<ma_node*>(&node->BaseNode), nullptr);
            delete node;
            m_Node = nullptr;
        }
    }

    void AudioDistortionFilter::SetParams(float drive)
    {
        if (m_Node)
        {
            auto* node = static_cast<DistortionNode*>(m_Node);
            node->Drive = drive;
        }
    }

    ma_node_base* AudioDistortionFilter::GetNativeNode() const
    {
        return m_Node ? &static_cast<DistortionNode*>(m_Node)->BaseNode : nullptr;
    }

    // --- AudioGainNode ---
    AudioGainNode::AudioGainNode(float gain)
    {
        ma_engine* engine = AudioEngine::Get().GetNativeEngine();
        ma_uint32 channels = ma_engine_get_channels(engine);
        ma_node_config config = ma_node_config_init();
        config.vtable = &g_GainNodeVTable;
        config.pInputChannels = &channels;
        config.pOutputChannels = &channels;

        GainNode* node = new GainNode();
        ma_node_init(ma_engine_get_node_graph(engine), &config, nullptr, &node->BaseNode);
        node->Gain = gain;
        m_Node = node;
    }

    AudioGainNode::~AudioGainNode()
    {
        ma_node_uninit((ma_node*)m_Node, nullptr);
        delete (GainNode*)m_Node;
    }

    void AudioGainNode::SetParams(float gain)
    {
        ((GainNode*)m_Node)->Gain = gain;
    }

    ma_node_base* AudioGainNode::GetNativeNode() const
    {
        return (ma_node_base*)m_Node;
    }

    // --- AudioPanNode ---
    AudioPanNode::AudioPanNode(float pan)
    {
        ma_engine* engine = AudioEngine::Get().GetNativeEngine();
        ma_uint32 channels = ma_engine_get_channels(engine);
        ma_node_config config = ma_node_config_init();
        config.vtable = &g_PanNodeVTable;
        config.pInputChannels = &channels;
        config.pOutputChannels = &channels;

        PanNode* node = new PanNode();
        ma_node_init(ma_engine_get_node_graph(engine), &config, nullptr, &node->BaseNode);
        node->Pan = pan;
        m_Node = node;
    }

    AudioPanNode::~AudioPanNode()
    {
        ma_node_uninit((ma_node*)m_Node, nullptr);
        delete (PanNode*)m_Node;
    }

    void AudioPanNode::SetParams(float pan)
    {
        ((PanNode*)m_Node)->Pan = pan;
    }

    ma_node_base* AudioPanNode::GetNativeNode() const
    {
        return (ma_node_base*)m_Node;
    }
}
