#include "Easing.h"
#include <string>

namespace Conqueror::Math::Easing
{
    float CustomCubic(float t, float p1, float p2)
    {
        float u = 1.0f - t;
        return 3.0f * u * u * t * p1 + 3.0f * u * t * t * p2 + t * t * t;
    }

    EasingFunction GetEasingFunction(const char* name)
    {
        std::string n(name);
        if (n == "Linear") return Linear;
        if (n == "QuadIn") return QuadIn;
        if (n == "QuadOut") return QuadOut;
        if (n == "QuadInOut") return QuadInOut;
        if (n == "CubicIn") return CubicIn;
        if (n == "CubicOut") return CubicOut;
        if (n == "CubicInOut") return CubicInOut;
        if (n == "QuartIn") return QuartIn;
        if (n == "QuartOut") return QuartOut;
        if (n == "QuartInOut") return QuartInOut;
        if (n == "QuintIn") return QuintIn;
        if (n == "QuintOut") return QuintOut;
        if (n == "QuintInOut") return QuintInOut;
        if (n == "SineIn") return SineIn;
        if (n == "SineOut") return SineOut;
        if (n == "SineInOut") return SineInOut;
        if (n == "ExpoIn") return ExpoIn;
        if (n == "ExpoOut") return ExpoOut;
        if (n == "ExpoInOut") return ExpoInOut;
        if (n == "CircIn") return CircIn;
        if (n == "CircOut") return CircOut;
        if (n == "CircInOut") return CircInOut;
        if (n == "BackIn") return BackIn;
        if (n == "BackOut") return BackOut;
        if (n == "BackInOut") return BackInOut;
        if (n == "ElasticIn") return ElasticIn;
        if (n == "ElasticOut") return ElasticOut;
        if (n == "ElasticInOut") return ElasticInOut;
        if (n == "BounceIn") return BounceIn;
        if (n == "BounceOut") return BounceOut;
        if (n == "BounceInOut") return BounceInOut;
        if (n == "SmoothStep") return SmoothStep;
        if (n == "SmootherStep") return SmootherStep;
        return Linear;
    }
}
