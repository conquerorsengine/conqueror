#include "Surface.h"
#include <cmath>

namespace Conqueror::Math {
    static int BinomialCoefficient(int n, int k) {
        if (k < 0 || k > n) return 0;
        if (k == 0 || k == n) return 1;
        int res = 1;
        for (int i = 1; i <= k; ++i) {
            res = res * (n - i + 1) / i;
        }
        return res;
    }

    static float BernsteinPolynomial(int n, int i, float t) {
        return BinomialCoefficient(n, i) * std::pow(t, i) * std::pow(1.0f - t, n - i);
    }

    CQVec3 Surface::BilinearInterpolation(const CQVec3& p00, const CQVec3& p10, const CQVec3& p01, const CQVec3& p11, float u, float v) {
        CQVec3 p0 = Vec3Lerp(p00, p10, u);
        CQVec3 p1 = Vec3Lerp(p01, p11, u);
        return Vec3Lerp(p0, p1, v);
    }

    CQVec3 Surface::BezierSurface(const std::vector<std::vector<CQVec3>>& controlPoints, float u, float v) {
        if (controlPoints.empty() || controlPoints[0].empty()) return CQVec3(0,0,0);
        int n = controlPoints.size() - 1;
        int m = controlPoints[0].size() - 1;

        CQVec3 point(0, 0, 0);
        for (int i = 0; i <= n; ++i) {
            float polyU = BernsteinPolynomial(n, i, u);
            for (int j = 0; j <= m; ++j) {
                float polyV = BernsteinPolynomial(m, j, v);
                float factor = polyU * polyV;
                point = Vec3Add(point, Vec3Mul(controlPoints[i][j], factor));
            }
        }
        return point;
    }

    CQVec3 Surface::BezierSurfaceNormal(const std::vector<std::vector<CQVec3>>& controlPoints, float u, float v) {
        if (controlPoints.size() < 2 || controlPoints[0].size() < 2) return CQVec3(0,1,0);
        int n = controlPoints.size() - 1;
        int m = controlPoints[0].size() - 1;

        CQVec3 du(0, 0, 0);
        CQVec3 dv(0, 0, 0);

        for (int i = 0; i <= n - 1; ++i) {
            float polyU = BernsteinPolynomial(n - 1, i, u);
            for (int j = 0; j <= m; ++j) {
                float polyV = BernsteinPolynomial(m, j, v);
                CQVec3 diff = Vec3Sub(controlPoints[i + 1][j], controlPoints[i][j]);
                du = Vec3Add(du, Vec3Mul(diff, n * polyU * polyV));
            }
        }

        for (int i = 0; i <= n; ++i) {
            float polyU = BernsteinPolynomial(n, i, u);
            for (int j = 0; j <= m - 1; ++j) {
                float polyV = BernsteinPolynomial(m - 1, j, v);
                CQVec3 diff = Vec3Sub(controlPoints[i][j + 1], controlPoints[i][j]);
                dv = Vec3Add(dv, Vec3Mul(diff, m * polyU * polyV));
            }
        }

        CQVec3 normal = Vec3Cross(du, dv);
        if (Vec3Length(normal) > 0.0001f) {
            return Vec3Normalize(normal);
        }
        return CQVec3(0, 1, 0);
    }
}
