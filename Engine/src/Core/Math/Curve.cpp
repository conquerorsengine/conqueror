#include "Curve.h"

namespace Conqueror::Math
{
    glm::vec3 BezierCurve::Cubic(const glm::vec3& p0, const glm::vec3& p1, 
                                  const glm::vec3& p2, const glm::vec3& p3, float t)
    {
        float u = 1.0f - t;
        float tt = t * t;
        float uu = u * u;
        float uuu = uu * u;
        float ttt = tt * t;

        glm::vec3 p = uuu * p0;
        p += 3.0f * uu * t * p1;
        p += 3.0f * u * tt * p2;
        p += ttt * p3;

        return p;
    }

    glm::vec3 BezierCurve::Quadratic(const glm::vec3& p0, const glm::vec3& p1, 
                                      const glm::vec3& p2, float t)
    {
        float u = 1.0f - t;
        float tt = t * t;
        float uu = u * u;

        glm::vec3 p = uu * p0;
        p += 2.0f * u * t * p1;
        p += tt * p2;

        return p;
    }

    glm::vec3 CatmullRomSpline::Interpolate(const glm::vec3& p0, const glm::vec3& p1, 
                                             const glm::vec3& p2, const glm::vec3& p3, float t)
    {
        float t2 = t * t;
        float t3 = t2 * t;

        return 0.5f * (
            (2.0f * p1) +
            (-p0 + p2) * t +
            (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 +
            (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3
        );
    }

    glm::vec3 HermiteSpline::Interpolate(const glm::vec3& p0, const glm::vec3& t0,
                                          const glm::vec3& p1, const glm::vec3& t1, float t)
    {
        float t2 = t * t;
        float t3 = t2 * t;

        float h00 = 2.0f * t3 - 3.0f * t2 + 1.0f;
        float h10 = t3 - 2.0f * t2 + t;
        float h01 = -2.0f * t3 + 3.0f * t2;
        float h11 = t3 - t2;

        return h00 * p0 + h10 * t0 + h01 * p1 + h11 * t1;
    }

    glm::vec3 HermiteSpline::Tangent(const glm::vec3& p0, const glm::vec3& t0,
                                     const glm::vec3& p1, const glm::vec3& t1, float t)
    {
        float t2 = t * t;

        float h00_prime = 6.0f * t2 - 6.0f * t;
        float h10_prime = 3.0f * t2 - 4.0f * t + 1.0f;
        float h01_prime = -6.0f * t2 + 6.0f * t;
        float h11_prime = 3.0f * t2 - 2.0f * t;

        return h00_prime * p0 + h10_prime * t0 + h01_prime * p1 + h11_prime * t1;
    }

    glm::vec3 BSpline::Interpolate(const std::vector<glm::vec3>& controlPoints, int degree, float t, bool closed)
    {
        if (controlPoints.empty()) return glm::vec3(0.0f);
        if (controlPoints.size() == 1) return controlPoints[0];
        
        int n = controlPoints.size() - 1;
        if (degree > n) degree = n;

        std::vector<glm::vec3> pts = controlPoints;
        if (closed) {
            for (int i = 0; i < degree; ++i) {
                pts.push_back(controlPoints[i]);
            }
            n = pts.size() - 1;
        }

        int m = n + degree + 1;
        std::vector<float> knots(m + 1);
        
        if (closed) {
            for (int i = 0; i <= m; ++i) knots[i] = (float)i / m;
        } else {
            for (int i = 0; i <= degree; ++i) knots[i] = 0.0f;
            for (int i = degree + 1; i < m - degree; ++i) knots[i] = (float)(i - degree) / (m - 2 * degree);
            for (int i = m - degree; i <= m; ++i) knots[i] = 1.0f;
        }

        t = glm::clamp(t, 0.0f, 1.0f);
        if (t == 1.0f && !closed) return pts.back();

        float u = t * knots[m - degree];
        if (closed) {
            float minU = knots[degree];
            float maxU = knots[m - degree];
            u = minU + t * (maxU - minU);
        }

        int k = degree;
        while (k < m - degree && knots[k + 1] <= u) k++;

        std::vector<glm::vec3> d(degree + 1);
        for (int j = 0; j <= degree; ++j) {
            d[j] = pts[k - degree + j];
        }

        for (int r = 1; r <= degree; ++r) {
            for (int j = degree; j >= r; --j) {
                float alpha = (u - knots[k - degree + j]) / (knots[k + j + 1 - r] - knots[k - degree + j]);
                d[j] = (1.0f - alpha) * d[j - 1] + alpha * d[j];
            }
        }

        return d[degree];
    }

    glm::vec3 CurveUtils::BezierCubicTangent(const glm::vec3& p0, const glm::vec3& p1, 
                                             const glm::vec3& p2, const glm::vec3& p3, float t)
    {
        float u = 1.0f - t;
        float uu = u * u;
        float tt = t * t;

        glm::vec3 tangent = -3.0f * uu * p0;
        tangent += 3.0f * (u * u - 2.0f * u * t) * p1;
        tangent += 3.0f * (2.0f * u * t - tt) * p2;
        tangent += 3.0f * tt * p3;

        return glm::normalize(tangent);
    }

    glm::vec3 CurveUtils::BezierQuadraticTangent(const glm::vec3& p0, const glm::vec3& p1, 
                                                 const glm::vec3& p2, float t)
    {
        float u = 1.0f - t;
        glm::vec3 tangent = 2.0f * u * (p1 - p0) + 2.0f * t * (p2 - p1);
        return glm::normalize(tangent);
    }

    float CurveUtils::BezierCubicLength(const glm::vec3& p0, const glm::vec3& p1, 
                                        const glm::vec3& p2, const glm::vec3& p3, int segments)
    {
        float length = 0.0f;
        glm::vec3 prevPoint = BezierCurve::Cubic(p0, p1, p2, p3, 0.0f);
        
        for (int i = 1; i <= segments; ++i) {
            float t = (float)i / segments;
            glm::vec3 currentPoint = BezierCurve::Cubic(p0, p1, p2, p3, t);
            length += glm::length(currentPoint - prevPoint);
            prevPoint = currentPoint;
        }
        
        return length;
    }

    glm::vec3 NURBS::Interpolate(const std::vector<glm::vec3>& controlPoints, const std::vector<float>& weights, const std::vector<float>& knots, int degree, float t)
    {
        if (controlPoints.empty() || weights.size() != controlPoints.size()) return glm::vec3(0.0f);
        
        int n = controlPoints.size() - 1;
        int m = n + degree + 1;
        
        if (knots.size() != m + 1) return glm::vec3(0.0f);

        t = glm::clamp(t, knots[degree], knots[m - degree]);

        int k = degree;
        while (k < m - degree && knots[k + 1] <= t) k++;

        std::vector<glm::vec4> cw(degree + 1);
        for (int j = 0; j <= degree; ++j) {
            int idx = k - degree + j;
            cw[j] = glm::vec4(controlPoints[idx] * weights[idx], weights[idx]);
        }

        for (int r = 1; r <= degree; ++r) {
            for (int j = degree; j >= r; --j) {
                float alpha = (t - knots[k - degree + j]) / (knots[k + j + 1 - r] - knots[k - degree + j]);
                cw[j] = (1.0f - alpha) * cw[j - 1] + alpha * cw[j];
            }
        }

        if (cw[degree].w == 0.0f) return glm::vec3(0.0f);
        return glm::vec3(cw[degree]) / cw[degree].w;
    }

    glm::vec3 TCBSpline::Interpolate(const glm::vec3& p0, const glm::vec3& p1, const glm::vec3& p2, const glm::vec3& p3, 
                                     float t, float tension, float continuity, float bias)
    {
        float t2 = t * t;
        float t3 = t2 * t;

        float h00 = 2.0f * t3 - 3.0f * t2 + 1.0f;
        float h10 = t3 - 2.0f * t2 + t;
        float h01 = -2.0f * t3 + 3.0f * t2;
        float h11 = t3 - t2;

        float c1 = (1.0f - tension) * (1.0f + continuity) * (1.0f + bias) / 2.0f;
        float c2 = (1.0f - tension) * (1.0f - continuity) * (1.0f - bias) / 2.0f;
        float c3 = (1.0f - tension) * (1.0f - continuity) * (1.0f + bias) / 2.0f;
        float c4 = (1.0f - tension) * (1.0f + continuity) * (1.0f - bias) / 2.0f;

        glm::vec3 m0 = c1 * (p1 - p0) + c2 * (p2 - p1);
        glm::vec3 m1 = c3 * (p2 - p1) + c4 * (p3 - p2);

        return h00 * p1 + h10 * m0 + h01 * p2 + h11 * m1;
    }
}
