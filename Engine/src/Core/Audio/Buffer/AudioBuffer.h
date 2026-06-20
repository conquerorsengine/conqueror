#pragma once

#include "Core/Base/Base.h"

#include <cstdint>
#include <vector>

namespace Conqueror
{
    class CQ_API AudioBuffer
    {
    public:
        AudioBuffer() = default;
        AudioBuffer(uint32_t channels, uint32_t frameCount);

        void Resize(uint32_t channels, uint32_t frameCount);
        void Clear();

        uint32_t GetChannelCount() const { return m_Channels; }
        uint32_t GetFrameCount() const { return m_Frames; }
        uint32_t GetSampleCount() const { return (uint32_t)m_Samples.size(); }

        float* Data() { return m_Samples.data(); }
        const float* Data() const { return m_Samples.data(); }

    private:
        uint32_t m_Channels = 0;
        uint32_t m_Frames = 0;
        std::vector<float> m_Samples;
    };
}
