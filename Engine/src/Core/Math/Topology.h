#pragma once
#include "Core/Base/Base.h"
#include "VecMath.h"
#include <vector>

namespace Conqueror::Math {
    struct Face {
        std::vector<int> vertexIndices;
    };

    class CQ_API Topology {
    public:
        // Mesh topolojisi temel bilgileri
        static int CalculateEulerCharacteristic(int vertexCount, int edgeCount, int faceCount);
        
        // Çokgenin alanını hesaplama (herhangi bir düzlemsel çokgen)
        static float CalculateFaceArea(const std::vector<CQVec3>& vertices, const Face& face);

        // Catmull-Clark Yüzey Bölme Algoritması (Basitleştirilmiş)
        // Yeni köşe pozisyonlarını ve yeni yüzeyleri üretir
        static void CatmullClarkSubdivision(
            const std::vector<CQVec3>& inVertices, 
            const std::vector<Face>& inFaces,
            std::vector<CQVec3>& outVertices,
            std::vector<Face>& outFaces
        );
    };
}
