#include "Kinematics.h"
#include <cmath>

namespace Conqueror::Math {
    std::vector<CQVec3> Kinematics::ForwardKinematics(const std::vector<Joint>& chain, const CQVec3& rootPosition) {
        std::vector<CQVec3> positions;
        if (chain.empty()) return positions;

        positions.push_back(rootPosition);
        CQVec3 currentPos = rootPosition;
        CQQuat currentRot = QuatIdentity();

        for (size_t i = 0; i < chain.size(); ++i) {
            currentRot = QuatMultiply(currentRot, chain[i].rotation);
            CQVec3 dir = QuatMultiplyVec3(currentRot, CQVec3(0, 1, 0)); // Local Y up
            currentPos = Vec3Add(currentPos, Vec3Mul(dir, chain[i].length));
            positions.push_back(currentPos);
        }
        return positions;
    }

    void Kinematics::SolveFABRIK(std::vector<CQVec3>& jointPositions, const std::vector<float>& boneLengths, const CQVec3& target, float tolerance, int maxIterations) {
        if (jointPositions.size() < 2 || jointPositions.size() - 1 != boneLengths.size()) return;
        
        int numJoints = jointPositions.size();
        float totalLength = 0.0f;
        for (float len : boneLengths) totalLength += len;

        CQVec3 root = jointPositions[0];
        float distToTarget = Vec3Distance(root, target);

        // Ulaşılamazsa tamamen uzat
        if (distToTarget >= totalLength) {
            CQVec3 dir = Vec3Normalize(Vec3Sub(target, root));
            for (int i = 1; i < numJoints; ++i) {
                jointPositions[i] = Vec3Add(jointPositions[i - 1], Vec3Mul(dir, boneLengths[i - 1]));
            }
            return;
        }

        for (int iter = 0; iter < maxIterations; ++iter) {
            if (Vec3Distance(jointPositions.back(), target) <= tolerance) break;

            // Backward pass
            jointPositions.back() = target;
            for (int i = numJoints - 2; i >= 0; --i) {
                CQVec3 dir = Vec3Normalize(Vec3Sub(jointPositions[i], jointPositions[i + 1]));
                jointPositions[i] = Vec3Add(jointPositions[i + 1], Vec3Mul(dir, boneLengths[i]));
            }

            // Forward pass
            jointPositions[0] = root;
            for (int i = 1; i < numJoints; ++i) {
                CQVec3 dir = Vec3Normalize(Vec3Sub(jointPositions[i], jointPositions[i - 1]));
                jointPositions[i] = Vec3Add(jointPositions[i - 1], Vec3Mul(dir, boneLengths[i - 1]));
            }
        }
    }

    void Kinematics::SolveCCD(std::vector<CQVec3>& jointPositions, const CQVec3& target, float tolerance, int maxIterations) {
        if (jointPositions.size() < 2) return;
        
        int numJoints = jointPositions.size();
        for (int iter = 0; iter < maxIterations; ++iter) {
            if (Vec3Distance(jointPositions.back(), target) <= tolerance) break;

            for (int i = numJoints - 2; i >= 0; --i) {
                CQVec3 effector = jointPositions.back();
                CQVec3 toEffector = Vec3Sub(effector, jointPositions[i]);
                CQVec3 toTarget = Vec3Sub(target, jointPositions[i]);

                float distEffector = Vec3Length(toEffector);
                float distTarget = Vec3Length(toTarget);

                if (distEffector > 0.0001f && distTarget > 0.0001f) {
                    toEffector = Vec3Div(toEffector, distEffector);
                    toTarget = Vec3Div(toTarget, distTarget);

                    float dotProd = Vec3Dot(toEffector, toTarget);
                    dotProd = std::clamp(dotProd, -1.0f, 1.0f);
                    float angle = std::acos(dotProd);

                    if (angle > 0.0001f) {
                        CQVec3 axis = Vec3Cross(toEffector, toTarget);
                        if (Vec3Length(axis) > 0.0001f) {
                            axis = Vec3Normalize(axis);
                            CQQuat rotation = QuatFromAxisAngle(axis, angle);
                            
                            // Rotasyonu i'den sonraki tüm eklemlere uygula
                            for (int j = i + 1; j < numJoints; ++j) {
                                CQVec3 localPos = Vec3Sub(jointPositions[j], jointPositions[i]);
                                localPos = QuatMultiplyVec3(rotation, localPos);
                                jointPositions[j] = Vec3Add(jointPositions[i], localPos);
                            }
                        }
                    }
                }
            }
        }
    }
}
