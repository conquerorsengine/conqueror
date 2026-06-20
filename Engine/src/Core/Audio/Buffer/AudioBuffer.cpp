#include "AudioBuffer.h"

#include <algorithm>

namespace Conqueror
{
    AudioBuffer::AudioBuffer(uint32_t channels, uint32_t frameCount)
    {
        Resize(channels, frameCount);
    }

    void AudioBuffer::Resize(uint32_t channels, uint32_t frameCount)
    {
        m_Channels = channels;
        m_Frames = frameCount;
        m_Samples.resize((size_t)m_Channels * (size_t)m_Frames, 0.0f);
    }

    void AudioBuffer::Clear()
    {
        std::fill(m_Samples.begin(), m_Samples.end(), 0.0f);
    }
}
