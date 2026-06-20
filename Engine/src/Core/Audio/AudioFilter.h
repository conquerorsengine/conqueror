#pragma once

#include "Core/Base/Base.h"

// Forward declaration to avoid exposing miniaudio.h to the rest of the engine
typedef struct ma_node_base ma_node_base;

namespace Conqueror
{
    // Temel Audio Node (Node Graph uyumlu)
    class AudioNode
    {
    public:
        virtual ~AudioNode() = default;
        
        virtual ma_node_base* GetNativeNode() const = 0;
        
        // Bu dugumun cikisini bir sonraki dugumun girisine baglar
        void ConnectTo(AudioNode* nextNode);
        
        // Bu dugumu dogrudan Engine'in ana cikisina (Endpoint) baglar
        void ConnectToEndpoint();

        // Bu dugumun tum cikis baglantilarini keser
        void DetachAllOutputs();
    };

    // Low Pass Filter Node
    class AudioLowPassFilter : public AudioNode
    {
    public:
        AudioLowPassFilter(float cutoffFrequency = 5000.0f, int order = 8);
        ~AudioLowPassFilter();

        void SetParams(float cutoffFrequency, int order);
        
        ma_node_base* GetNativeNode() const override;

    private:
        void* m_Node = nullptr;
    };

    // High Pass Filter Node
    class AudioHighPassFilter : public AudioNode
    {
    public:
        AudioHighPassFilter(float cutoffFrequency = 5000.0f, int order = 8);
        ~AudioHighPassFilter();

        void SetParams(float cutoffFrequency, int order);
        
        ma_node_base* GetNativeNode() const override;

    private:
        void* m_Node = nullptr;
    };

    // Echo / Delay Node
    class AudioEchoFilter : public AudioNode
    {
    public:
        AudioEchoFilter(float delayMs = 500.0f, float decay = 0.5f, float wetMix = 1.0f, float dryMix = 1.0f);
        ~AudioEchoFilter();

        void SetParams(float delayMs, float decay, float wetMix, float dryMix);
        
        ma_node_base* GetNativeNode() const override;

    private:
        void* m_Node = nullptr;
    };

    // Chorus Filter Node (delay tabanli pratik uygulama)
    class AudioChorusFilter : public AudioNode
    {
    public:
        AudioChorusFilter(float delayMs = 40.0f, float depth = 0.03f, float rate = 0.8f, float wetMix = 0.5f, float dryMix = 0.5f);
        ~AudioChorusFilter();

        void SetParams(float delayMs, float depth, float rate, float wetMix, float dryMix);

        ma_node_base* GetNativeNode() const override;

    private:
        void* m_Node = nullptr;
    };

    // Reverb Filter Node (placeholder using delay)
    class AudioReverbFilter : public AudioNode
    {
    public:
        AudioReverbFilter(float roomSize = 1.0f, float damping = 0.5f, float wetMix = 0.5f, float dryMix = 1.0f);
        ~AudioReverbFilter();

        void SetParams(float roomSize, float damping, float wetMix, float dryMix);
        
        ma_node_base* GetNativeNode() const override;

    private:
        void* m_Node = nullptr;
    };

    // Distortion Filter Node (soft clipping custom DSP)
    class AudioDistortionFilter : public AudioNode
    {
    public:
        AudioDistortionFilter(float drive = 0.5f);
        ~AudioDistortionFilter();

        void SetParams(float drive);

        ma_node_base* GetNativeNode() const override;

    private:
        void* m_Node = nullptr;
    };

    class AudioGainNode : public AudioNode
    {
    public:
        AudioGainNode(float gain = 1.0f);
        ~AudioGainNode();
        void SetParams(float gain);
        ma_node_base* GetNativeNode() const override;
    private:
        void* m_Node = nullptr;
    };

    class AudioPanNode : public AudioNode
    {
    public:
        AudioPanNode(float pan = 0.0f);
        ~AudioPanNode();
        void SetParams(float pan);
        ma_node_base* GetNativeNode() const override;
    private:
        void* m_Node = nullptr;
    };
}
