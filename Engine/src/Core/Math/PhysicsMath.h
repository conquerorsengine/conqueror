#pragma once
#include "Core/Base/Base.h"
#include "VecMath.h"
#include "MatrixMath.h"

namespace Conqueror::Math
{
    class CQ_API PhysicsMath
    {
    public:
        // Numerical Integration
        static void ExplicitEuler(CQVec3& position, CQVec3& velocity, const CQVec3& acceleration, float dt);
        static void SemiImplicitEuler(CQVec3& position, CQVec3& velocity, const CQVec3& acceleration, float dt);
        static void VelocityVerlet(CQVec3& position, CQVec3& velocity, const CQVec3& acceleration, float dt);
        static void RK4(CQVec3& position, CQVec3& velocity, const CQVec3& acceleration, float dt);

        // Forces
        static CQVec3 ComputeDragForce(const CQVec3& velocity, float dragCoefficient, float fluidDensity, float crossSectionalArea);
        static CQVec3 ComputeMagnusForce(const CQVec3& velocity, const CQVec3& angularVelocity, float liftCoefficient, float radius, float fluidDensity);
        static CQVec3 ComputeBuoyancyForce(float submergedVolume, float fluidDensity, const CQVec3& gravity);

        // Collisions
        static void ResolveElasticCollision(CQVec3& v1, CQVec3& v2, float m1, float m2, const CQVec3& normal, float restitution);
        static void ResolveInelasticCollision(CQVec3& v1, CQVec3& v2, float m1, float m2, const CQVec3& normal, float restitution);

        // Constraints & Springs
        static void SolveDistanceConstraint(CQVec3& p1, CQVec3& p2, float targetDistance, float stiffness, float dt);
        static void SolvePositionConstraint(CQVec3& p, const CQVec3& target, float stiffness, float dt);
        static void SpringDamper(CQVec3& position, CQVec3& velocity, const CQVec3& targetPosition, const CQVec3& targetVelocity, float stiffness, float damping, float dt);

        // Advanced Rigid Body
        static CQMat4 ComputeBoxInertiaTensor(float mass, const CQVec3& extents);
        static CQMat4 ComputeSphereInertiaTensor(float mass, float radius);
        static CQMat4 ComputeCylinderInertiaTensor(float mass, float radius, float height);
        static CQMat4 ComputeConeInertiaTensor(float mass, float radius, float height);
        static CQMat4 ComputeCapsuleInertiaTensor(float mass, float radius, float height);
        
        static CQVec3 CalculateAngularMomentum(const CQVec3& angularVelocity, const float inertiaTensor[9]);
        static CQVec3 CalculateGyroscopicTorque(const CQVec3& angularVelocity, const float inertiaTensor[9]);
    };
}