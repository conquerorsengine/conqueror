#include "Voronoi.h"
#include <cmath>
#include <limits>

namespace Conqueror::Math {
    std::vector<CQVec2> Voronoi::LloydRelaxation(const std::vector<CQVec2>& points, int iterations, const CQVec2& minBounds, const CQVec2& maxBounds) {
        std::vector<CQVec2> currentPts = points;
        
        // Extremely simplified numeric Voronoi centroid approximation for engine integration
        // (Real geometric Lloyd involves intersection of half-planes, here we use a discrete grid approach for speed)
        
        int gridW = 100;
        int gridH = 100;
        float stepX = (maxBounds.x - minBounds.x) / gridW;
        float stepY = (maxBounds.y - minBounds.y) / gridH;

        for (int iter = 0; iter < iterations; ++iter) {
            std::vector<CQVec2> centroids(currentPts.size(), CQVec2(0,0));
            std::vector<int> counts(currentPts.size(), 0);

            for (int y = 0; y < gridH; ++y) {
                for (int x = 0; x < gridW; ++x) {
                    CQVec2 sample(minBounds.x + x * stepX + stepX * 0.5f, 
                                  minBounds.y + y * stepY + stepY * 0.5f);
                    
                    int closestIdx = 0;
                    float minDist = std::numeric_limits<float>::max();
                    
                    for (size_t i = 0; i < currentPts.size(); ++i) {
                        float dist = (sample.x - currentPts[i].x)*(sample.x - currentPts[i].x) + 
                                     (sample.y - currentPts[i].y)*(sample.y - currentPts[i].y);
                        if (dist < minDist) {
                            minDist = dist;
                            closestIdx = i;
                        }
                    }
                    
                    centroids[closestIdx].x += sample.x;
                    centroids[closestIdx].y += sample.y;
                    counts[closestIdx]++;
                }
            }

            for (size_t i = 0; i < currentPts.size(); ++i) {
                if (counts[i] > 0) {
                    currentPts[i].x = centroids[i].x / counts[i];
                    currentPts[i].y = centroids[i].y / counts[i];
                }
            }
        }
        
        return currentPts;
    }
}
