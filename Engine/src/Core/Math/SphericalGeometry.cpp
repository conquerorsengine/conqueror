#include "SphericalGeometry.h"
#include <cmath>

namespace Conqueror::Math {
    float SphericalGeometry::HaversineDistance(float lat1, float lon1, float lat2, float lon2, float radius) {
        float dlat = lat2 - lat1;
        float dlon = lon2 - lon1;
        float a = std::sin(dlat / 2.0f) * std::sin(dlat / 2.0f) +
                  std::cos(lat1) * std::cos(lat2) *
                  std::sin(dlon / 2.0f) * std::sin(dlon / 2.0f);
        float c = 2.0f * std::atan2(std::sqrt(a), std::sqrt(1.0f - a));
        return radius * c;
    }

    float SphericalGeometry::InitialBearing(float lat1, float lon1, float lat2, float lon2) {
        float y = std::sin(lon2 - lon1) * std::cos(lat2);
        float x = std::cos(lat1) * std::sin(lat2) -
                  std::sin(lat1) * std::cos(lat2) * std::cos(lon2 - lon1);
        return std::atan2(y, x);
    }

    void SphericalGeometry::DestinationPoint(float lat1, float lon1, float dist, float bearing, float radius, float& outLat, float& outLon) {
        float ad = dist / radius; // angular distance
        outLat = std::asin(std::sin(lat1) * std::cos(ad) + std::cos(lat1) * std::sin(ad) * std::cos(bearing));
        outLon = lon1 + std::atan2(std::sin(bearing) * std::sin(ad) * std::cos(lat1),
                                   std::cos(ad) - std::sin(lat1) * std::sin(outLat));
    }

    void SphericalGeometry::CartesianToSpherical(const CQVec3& p, float& outLat, float& outLon, float& outRadius) {
        outRadius = std::sqrt(p.x*p.x + p.y*p.y + p.z*p.z);
        if (outRadius == 0.0f) {
            outLat = 0.0f; outLon = 0.0f; return;
        }
        outLat = std::asin(p.y / outRadius);
        outLon = std::atan2(p.z, p.x);
    }

    CQVec3 SphericalGeometry::SphericalToCartesian(float lat, float lon, float radius) {
        return CQVec3(
            radius * std::cos(lat) * std::cos(lon),
            radius * std::sin(lat),
            radius * std::cos(lat) * std::sin(lon)
        );
    }
}
