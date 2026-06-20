#pragma once

#include "Core/Base/Base.h"

namespace Conqueror::AudioDSP
{
    float CQ_API Clamp(float value, float minValue, float maxValue);
    float CQ_API LinearToDecibels(float linearValue);
    float CQ_API DecibelsToLinear(float dbValue);
    float CQ_API SoftClip(float value, float drive);
    float CQ_API TriangleLFO(float phase);
    float CQ_API SineLFO(float phase);
    float CQ_API Lerp(float a, float b, float t);
}
