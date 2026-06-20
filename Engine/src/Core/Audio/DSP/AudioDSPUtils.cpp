#include "AudioDSPUtils.h"

#include <cmath>

namespace Conqueror::AudioDSP
{
    float Clamp(float value, float minValue, float maxValue)
    {
        if (value < minValue) return minValue;
        if (value > maxValue) return maxValue;
        return value;
    }

    float LinearToDecibels(float linearValue)
    {
        const float clamped = Clamp(linearValue, 0.000001f, 1.0e6f);
        return 20.0f * std::log10(clamped);
    }

    float DecibelsToLinear(float dbValue)
    {
        return std::pow(10.0f, dbValue / 20.0f);
    }

    float SoftClip(float value, float drive)
    {
        const float shapedDrive = 1.0f + Clamp(drive, 0.0f, 1.0f) * 20.0f;
        const float norm = std::tanh(shapedDrive);
        return std::tanh(value * shapedDrive) / (norm > 0.000001f ? norm : 1.0f);
    }

    float TriangleLFO(float phase)
    {
        const float wrapped = phase - std::floor(phase);
        return wrapped < 0.5f ? (wrapped * 4.0f - 1.0f) : (3.0f - wrapped * 4.0f);
    }
    
    float SineLFO(float phase)
    {
        return std::sin(phase * 2.0f * 3.1415926535f);
    }

    float Lerp(float a, float b, float t)
    {
        return a + t * (b - a);
    }
}
