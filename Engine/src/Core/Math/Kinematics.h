#pragma once
#include "Core/Base/Base.h"
#include "VecMath.h"
#include "QuaternionMath.h"
#include <vector>

namespace Conqueror::Math {
    struct Joint {
        CQVec3 position;
        CQQuat rotation;
        float length;
    };

    class CQ_API Kinematics {
    public:
        // İleri Kinematik (Forward Kinematics)
        static std::vector<CQVec3> ForwardKinematics(const std::vector<Joint>& chain, const CQVec3& rootPosition);

        // Ters Kinematik - FABRIK (Forward And Backward Reaching Inverse Kinematics)
        static void SolveFABRIK(std::vector<CQVec3>& jointPositions, const std::vector<float>& boneLengths, const CQVec3& target, float tolerance = 0.01f, int maxIterations = 10);

        // Ters Kinematik - CCD (Cyclic Coordinate Descent)
        static void SolveCCD(std::vector<CQVec3>& jointPositions, const CQVec3& target, float tolerance = 0.01f, int maxIterations = 10);
    };
}
