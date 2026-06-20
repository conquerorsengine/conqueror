#pragma once

#include "Core/Base/Base.h"
#include <cmath>

namespace Conqueror::Math
{
    // Easing functions for smooth animations
    // All functions take t in range [0, 1] and return value in range [0, 1]
    
    namespace Easing
    {
        // Linear
        CQ_API inline float Linear(float t) { return t; }
        
        // Quadratic
        CQ_API inline float QuadIn(float t) { return t * t; }
        CQ_API inline float QuadOut(float t) { return t * (2.0f - t); }
        CQ_API inline float QuadInOut(float t) 
        { 
            return t < 0.5f ? 2.0f * t * t : -1.0f + (4.0f - 2.0f * t) * t; 
        }
        
        // Cubic
        CQ_API inline float CubicIn(float t) { return t * t * t; }
        CQ_API inline float CubicOut(float t) { float f = t - 1.0f; return f * f * f + 1.0f; }
        CQ_API inline float CubicInOut(float t)
        {
            return t < 0.5f ? 4.0f * t * t * t : (t - 1.0f) * (2.0f * t - 2.0f) * (2.0f * t - 2.0f) + 1.0f;
        }
        
        // Quartic
        CQ_API inline float QuartIn(float t) { return t * t * t * t; }
        CQ_API inline float QuartOut(float t) { float f = t - 1.0f; return 1.0f - f * f * f * f; }
        CQ_API inline float QuartInOut(float t)
        {
            if (t < 0.5f) return 8.0f * t * t * t * t;
            float f = t - 1.0f;
            return 1.0f - 8.0f * f * f * f * f;
        }
        
        // Quintic
        CQ_API inline float QuintIn(float t) { return t * t * t * t * t; }
        CQ_API inline float QuintOut(float t) { float f = t - 1.0f; return 1.0f + f * f * f * f * f; }
        CQ_API inline float QuintInOut(float t)
        {
            if (t < 0.5f) return 16.0f * t * t * t * t * t;
            float f = t - 1.0f;
            return 1.0f + 16.0f * f * f * f * f * f;
        }
        
        // Sine
        CQ_API inline float SineIn(float t) { return 1.0f - std::cos(t * 1.57079632679f); }
        CQ_API inline float SineOut(float t) { return std::sin(t * 1.57079632679f); }
        CQ_API inline float SineInOut(float t) { return 0.5f * (1.0f - std::cos(t * 3.14159265359f)); }
        
        // Exponential
        CQ_API inline float ExpoIn(float t) { return t == 0.0f ? 0.0f : std::pow(2.0f, 10.0f * (t - 1.0f)); }
        CQ_API inline float ExpoOut(float t) { return t == 1.0f ? 1.0f : 1.0f - std::pow(2.0f, -10.0f * t); }
        CQ_API inline float ExpoInOut(float t)
        {
            if (t == 0.0f || t == 1.0f) return t;
            if (t < 0.5f) return 0.5f * std::pow(2.0f, 20.0f * t - 10.0f);
            return 1.0f - 0.5f * std::pow(2.0f, -20.0f * t + 10.0f);
        }
        
        // Circular
        CQ_API inline float CircIn(float t) { return 1.0f - std::sqrt(1.0f - t * t); }
        CQ_API inline float CircOut(float t) { return std::sqrt((2.0f - t) * t); }
        CQ_API inline float CircInOut(float t)
        {
            if (t < 0.5f) return 0.5f * (1.0f - std::sqrt(1.0f - 4.0f * t * t));
            return 0.5f * (std::sqrt(-((2.0f * t) - 3.0f) * ((2.0f * t) - 1.0f)) + 1.0f);
        }
        
        // Back (overshooting)
        CQ_API inline float BackIn(float t)
        {
            constexpr float s = 1.70158f;
            return t * t * ((s + 1.0f) * t - s);
        }
        CQ_API inline float BackOut(float t)
        {
            constexpr float s = 1.70158f;
            float f = t - 1.0f;
            return f * f * ((s + 1.0f) * f + s) + 1.0f;
        }
        CQ_API inline float BackInOut(float t)
        {
            constexpr float s = 1.70158f * 1.525f;
            if (t < 0.5f)
            {
                float f = 2.0f * t;
                return 0.5f * (f * f * ((s + 1.0f) * f - s));
            }
            float f = 2.0f * t - 2.0f;
            return 0.5f * (f * f * ((s + 1.0f) * f + s) + 2.0f);
        }
        
        // Elastic (spring-like)
        CQ_API inline float ElasticIn(float t)
        {
            if (t == 0.0f || t == 1.0f) return t;
            return -std::pow(2.0f, 10.0f * (t - 1.0f)) * std::sin((t - 1.1f) * 5.0f * 3.14159265359f);
        }
        CQ_API inline float ElasticOut(float t)
        {
            if (t == 0.0f || t == 1.0f) return t;
            return std::pow(2.0f, -10.0f * t) * std::sin((t - 0.1f) * 5.0f * 3.14159265359f) + 1.0f;
        }
        CQ_API inline float ElasticInOut(float t)
        {
            if (t == 0.0f || t == 1.0f) return t;
            if (t < 0.5f)
                return -0.5f * std::pow(2.0f, 20.0f * t - 10.0f) * std::sin((20.0f * t - 11.125f) * 1.39626340159f);
            return 0.5f * std::pow(2.0f, -20.0f * t + 10.0f) * std::sin((20.0f * t - 11.125f) * 1.39626340159f) + 1.0f;
        }
        
        // Bounce
        CQ_API inline float BounceOut(float t)
        {
            if (t < 1.0f / 2.75f)
                return 7.5625f * t * t;
            else if (t < 2.0f / 2.75f)
            {
                float f = t - 1.5f / 2.75f;
                return 7.5625f * f * f + 0.75f;
            }
            else if (t < 2.5f / 2.75f)
            {
                float f = t - 2.25f / 2.75f;
                return 7.5625f * f * f + 0.9375f;
            }
            else
            {
                float f = t - 2.625f / 2.75f;
                return 7.5625f * f * f + 0.984375f;
            }
        }
        CQ_API inline float BounceIn(float t) { return 1.0f - BounceOut(1.0f - t); }
        CQ_API inline float BounceInOut(float t)
        {
            if (t < 0.5f) return 0.5f * BounceIn(t * 2.0f);
            return 0.5f * BounceOut(t * 2.0f - 1.0f) + 0.5f;
        }
        
        // Smoothstep (Hermite interpolation)
        CQ_API inline float SmoothStep(float t) { return t * t * (3.0f - 2.0f * t); }
        CQ_API inline float SmootherStep(float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }
        
        // Custom easing with control points (Bezier-like)
        CQ_API float CustomCubic(float t, float p1, float p2);
        
        // Easing function pointer type
        using EasingFunction = float(*)(float);
        
        // Get easing function by name
        CQ_API EasingFunction GetEasingFunction(const char* name);
    }
}
