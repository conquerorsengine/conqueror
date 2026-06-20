#pragma once
#include "Core/Base/Base.h"
#include "VecMath.h"

namespace Conqueror::Math {
    class CQ_API SphericalGeometry {
    public:
        // Returns the great-circle distance between two points on a sphere
        // lat/lon must be in radians. Radius is the sphere's radius (e.g. Earth = 6371.0)
        static float HaversineDistance(float lat1, float lon1, float lat2, float lon2, float radius);
        
        // Initial bearing (forward azimuth) from point 1 to point 2 in radians
        static float InitialBearing(float lat1, float lon1, float lat2, float lon2);
        
        // Destination point given start, distance, bearing, and radius
        static void DestinationPoint(float lat1, float lon1, float dist, float bearing, float radius, float& outLat, float& outLon);
        
        // Converts 3D Cartesian coordinates on a sphere to Lat/Lon
        static void CartesianToSpherical(const CQVec3& p, float& outLat, float& outLon, float& outRadius);
        
        // Converts Lat/Lon to 3D Cartesian coordinates
        static CQVec3 SphericalToCartesian(float lat, float lon, float radius);
    };
}
