#include "Sampling.h"
#include "Random.h"
#include <cmath>

namespace Conqueror::Math
{
    static float DistanceSq(const CQVec2& a, const CQVec2& b) {
        float dx = a.x - b.x;
        float dy = a.y - b.y;
        return dx*dx + dy*dy;
    }

    static bool IsValidPoint(const CQVec2& p, const CQVec2& region, float radiusSq, float cellSize, int** grid, int cols, int rows, const std::vector<CQVec2>& points) {
        if (p.x >= 0 && p.x < region.x && p.y >= 0 && p.y < region.y) {
            int cellX = (int)(p.x / cellSize);
            int cellY = (int)(p.y / cellSize);
            int searchStartX = std::max(0, cellX - 2);
            int searchEndX = std::min(cellX + 2, cols - 1);
            int searchStartY = std::max(0, cellY - 2);
            int searchEndY = std::min(cellY + 2, rows - 1);

            for (int x = searchStartX; x <= searchEndX; x++) {
                for (int y = searchStartY; y <= searchEndY; y++) {
                    int pointIndex = grid[x][y] - 1;
                    if (pointIndex != -1) {
                        float sqDst = DistanceSq(p, points[pointIndex]);
                        if (sqDst < radiusSq) {
                            return false;
                        }
                    }
                }
            }
            return true;
        }
        return false;
    }

    std::vector<CQVec2> Sampling::PoissonDiskSampling2D(float radius, const CQVec2& sampleRegionSize, int numSamplesBeforeRejection)
    {
        float cellSize = radius / std::sqrt(2.0f);
        int cols = (int)std::ceil(sampleRegionSize.x / cellSize);
        int rows = (int)std::ceil(sampleRegionSize.y / cellSize);

        int** grid = new int*[cols];
        for (int i = 0; i < cols; i++) {
            grid[i] = new int[rows];
            for (int j = 0; j < rows; j++) {
                grid[i][j] = 0;
            }
        }

        std::vector<CQVec2> points;
        std::vector<CQVec2> spawnPoints;

        spawnPoints.push_back(CQVec2(sampleRegionSize.x / 2.0f, sampleRegionSize.y / 2.0f));
        
        while (spawnPoints.size() > 0) {
            int spawnIndex = Random::Int(0, static_cast<int>(spawnPoints.size()) - 1);
            CQVec2 spawnCenter = spawnPoints[spawnIndex];
            bool candidateAccepted = false;

            for (int i = 0; i < numSamplesBeforeRejection; i++) {
                float angle = Random::Float(0.0f, 3.14159f * 2.0f);
                float dirX = std::cos(angle);
                float dirY = std::sin(angle);
                float r = Random::Float(radius, 2.0f * radius);
                CQVec2 candidate(spawnCenter.x + dirX * r, spawnCenter.y + dirY * r);

                if (IsValidPoint(candidate, sampleRegionSize, radius * radius, cellSize, grid, cols, rows, points)) {
                    points.push_back(candidate);
                    spawnPoints.push_back(candidate);
                    grid[(int)(candidate.x / cellSize)][(int)(candidate.y / cellSize)] = points.size();
                    candidateAccepted = true;
                    break;
                }
            }

            if (!candidateAccepted) {
                spawnPoints.erase(spawnPoints.begin() + spawnIndex);
            }
        }

        for (int i = 0; i < cols; i++) {
            delete[] grid[i];
        }
        delete[] grid;

        return points;
    }

    float Sampling::HaltonSequence(int index, int base)
    {
        float f = 1.0f;
        float r = 0.0f;
        int current = index;
        while (current > 0) {
            f = f / base;
            r = r + f * (current % base);
            current = current / base;
        }
        return r;
    }

    CQVec2 Sampling::HammersleySet2D(int i, int N)
    {
        return CQVec2((float)i / (float)N, HaltonSequence(i, 2));
    }

    CQVec3 Sampling::UniformSampleHemisphere(float u1, float u2)
    {
        float z = u1;
        float r = std::sqrt(std::max(0.0f, 1.0f - z * z));
        float phi = 2.0f * 3.14159265f * u2;
        return CQVec3(r * std::cos(phi), r * std::sin(phi), z);
    }

    CQVec3 Sampling::CosineSampleHemisphere(float u1, float u2)
    {
        float r = std::sqrt(u1);
        float theta = 2.0f * 3.14159265f * u2;
        float x = r * std::cos(theta);
        float y = r * std::sin(theta);
        float z = std::sqrt(std::max(0.0f, 1.0f - u1));
        return CQVec3(x, y, z);
    }
}