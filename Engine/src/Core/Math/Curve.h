#pragma once

#include "Core/Base/Base.h"
#include <glm/glm.hpp>
#include <vector>

namespace Conqueror::Math
{
    // Bezier Curve
    class CQ_API BezierCurve
    {
    public:
        // Cubic Bezier (4 nokta)
        static glm::vec3 Cubic(const glm::vec3& p0, const glm::vec3& p1, 
                               const glm::vec3& p2, const glm::vec3& p3, float t);
        
        // Quadratic Bezier (3 nokta)
        static glm::vec3 Quadratic(const glm::vec3& p0, const glm::vec3& p1, 
                                   const glm::vec3& p2, float t);
    };

    // Catmull-Rom Spline
    class CQ_API CatmullRomSpline
    {
    public:
        static glm::vec3 Interpolate(const glm::vec3& p0, const glm::vec3& p1, 
                                     const glm::vec3& p2, const glm::vec3& p3, float t);
    };

    // Hermite Spline
    class CQ_API HermiteSpline
    {
    public:
        static glm::vec3 Interpolate(const glm::vec3& p0, const glm::vec3& t0,
                                     const glm::vec3& p1, const glm::vec3& t1, float t);
        
        static glm::vec3 Tangent(const glm::vec3& p0, const glm::vec3& t0,
                                 const glm::vec3& p1, const glm::vec3& t1, float t);
    };

    // B-Spline Curve
    class CQ_API BSpline
    {
    public:
        static glm::vec3 Interpolate(const std::vector<glm::vec3>& controlPoints, int degree, float t, bool closed = false);
    };

    // NURBS (Non-Uniform Rational B-Spline)
    class CQ_API NURBS
    {
    public:
        static glm::vec3 Interpolate(const std::vector<glm::vec3>& controlPoints, const std::vector<float>& weights, const std::vector<float>& knots, int degree, float t);
    };

    // TCB Spline (Kochanek-Bartels / Tension-Continuity-Bias)
    class CQ_API TCBSpline
    {
    public:
        static glm::vec3 Interpolate(const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& p2, const glm::vec3& p3, 
                                     float t, float tension, float continuity, float bias);
    };

    // Curve Utilities
    class CQ_API CurveUtils
    {
    public:
        static glm::vec3 BezierCubicTangent(const glm::vec3& p0, const glm::vec3& p1, 
                                            const glm::vec3& p2, const glm::vec3& p3, float t);
                                            
        static glm::vec3 BezierQuadraticTangent(const glm::vec3& p0, const glm::vec3& p1, 
                                                const glm::vec3& p2, float t);
                                                
        static float BezierCubicLength(const glm::vec3& p0, const glm::vec3& p1, 
                                       const glm::vec3& p2, const glm::vec3& p3, int segments = 50);
    };
}
