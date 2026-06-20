#include "PhysicsMath.h"

namespace Conqueror::Math
{
    void PhysicsMath::ExplicitEuler(CQVec3& position, CQVec3& velocity, const CQVec3& acceleration, float dt)
    {
        position = Vec3Add(position, Vec3Mul(velocity, dt));
        velocity = Vec3Add(velocity, Vec3Mul(acceleration, dt));
    }

    void PhysicsMath::SemiImplicitEuler(CQVec3& position, CQVec3& velocity, const CQVec3& acceleration, float dt)
    {
        velocity = Vec3Add(velocity, Vec3Mul(acceleration, dt));
        position = Vec3Add(position, Vec3Mul(velocity, dt));
    }

    void PhysicsMath::VelocityVerlet(CQVec3& position, CQVec3& velocity, const CQVec3& acceleration, float dt)
    {
        position = Vec3Add(position, Vec3Add(Vec3Mul(velocity, dt), Vec3Mul(acceleration, 0.5f * dt * dt)));
        velocity = Vec3Add(velocity, Vec3Mul(acceleration, dt));
    }

    void PhysicsMath::RK4(CQVec3& position, CQVec3& velocity, const CQVec3& acceleration, float dt)
    {
        CQVec3 k1v = acceleration;
        CQVec3 k1x = velocity;

        CQVec3 k2v = acceleration;
        CQVec3 k2x = Vec3Add(velocity, Vec3Mul(k1v, 0.5f * dt));

        CQVec3 k3v = acceleration;
        CQVec3 k3x = Vec3Add(velocity, Vec3Mul(k2v, 0.5f * dt));

        CQVec3 k4v = acceleration;
        CQVec3 k4x = Vec3Add(velocity, Vec3Mul(k3v, dt));

        CQVec3 dx = Vec3Mul(Vec3Add(k1x, Vec3Add(Vec3Mul(k2x, 2.0f), Vec3Add(Vec3Mul(k3x, 2.0f), k4x))), dt / 6.0f);
        CQVec3 dv = Vec3Mul(Vec3Add(k1v, Vec3Add(Vec3Mul(k2v, 2.0f), Vec3Add(Vec3Mul(k3v, 2.0f), k4v))), dt / 6.0f);

        position = Vec3Add(position, dx);
        velocity = Vec3Add(velocity, dv);
    }

    CQVec3 PhysicsMath::ComputeDragForce(const CQVec3& velocity, float dragCoefficient, float fluidDensity, float crossSectionalArea)
    {
        float speed = Vec3Length(velocity);
        if (speed < 0.0001f) return CQVec3(0,0,0);
        float dragMag = 0.5f * fluidDensity * speed * speed * dragCoefficient * crossSectionalArea;
        return Vec3Mul(Vec3Normalize(velocity), -dragMag);
    }

    CQVec3 PhysicsMath::ComputeMagnusForce(const CQVec3& velocity, const CQVec3& angularVelocity, float liftCoefficient, float radius, float fluidDensity)
    {
        CQVec3 cross = Vec3Cross(angularVelocity, velocity);
        float mag = 0.5f * fluidDensity * liftCoefficient * 3.14159f * radius * radius * radius;
        return Vec3Mul(cross, mag);
    }

    CQVec3 PhysicsMath::ComputeBuoyancyForce(float submergedVolume, float fluidDensity, const CQVec3& gravity)
    {
        return Vec3Mul(gravity, -fluidDensity * submergedVolume);
    }

    void PhysicsMath::ResolveElasticCollision(CQVec3& v1, CQVec3& v2, float m1, float m2, const CQVec3& normal, float restitution)
    {
        CQVec3 relVel = Vec3Sub(v1, v2);
        float velAlongNormal = Vec3Dot(relVel, normal);
        if (velAlongNormal > 0) return;

        float j = -(1.0f + restitution) * velAlongNormal;
        j /= (1.0f / m1 + 1.0f / m2);

        CQVec3 impulse = Vec3Mul(normal, j);
        v1 = Vec3Add(v1, Vec3Mul(impulse, 1.0f / m1));
        v2 = Vec3Sub(v2, Vec3Mul(impulse, 1.0f / m2));
    }

    void PhysicsMath::ResolveInelasticCollision(CQVec3& v1, CQVec3& v2, float m1, float m2, const CQVec3& normal, float restitution)
    {
        ResolveElasticCollision(v1, v2, m1, m2, normal, restitution);
    }

    void PhysicsMath::SolveDistanceConstraint(CQVec3& p1, CQVec3& p2, float targetDistance, float stiffness, float dt)
    {
        CQVec3 delta = Vec3Sub(p2, p1);
        float currentDistance = Vec3Length(delta);
        if (currentDistance < 0.0001f) return;

        float error = currentDistance - targetDistance;
        CQVec3 correction = Vec3Mul(Vec3Normalize(delta), error * stiffness * dt * 0.5f);

        p1 = Vec3Add(p1, correction);
        p2 = Vec3Sub(p2, correction);
    }

    void PhysicsMath::SolvePositionConstraint(CQVec3& p, const CQVec3& target, float stiffness, float dt)
    {
        CQVec3 delta = Vec3Sub(target, p);
        p = Vec3Add(p, Vec3Mul(delta, stiffness * dt));
    }

    void PhysicsMath::SpringDamper(CQVec3& position, CQVec3& velocity, const CQVec3& targetPosition, const CQVec3& targetVelocity, float stiffness, float damping, float dt)
    {
        CQVec3 posError = Vec3Sub(targetPosition, position);
        CQVec3 velError = Vec3Sub(targetVelocity, velocity);
        CQVec3 force = Vec3Add(Vec3Mul(posError, stiffness), Vec3Mul(velError, damping));
        velocity = Vec3Add(velocity, Vec3Mul(force, dt));
        position = Vec3Add(position, Vec3Mul(velocity, dt));
    }

    CQMat4 PhysicsMath::ComputeBoxInertiaTensor(float mass, const CQVec3& extents)
    {
        float x2 = extents.x * extents.x * 4.0f;
        float y2 = extents.y * extents.y * 4.0f;
        float z2 = extents.z * extents.z * 4.0f;
        float factor = mass / 12.0f;
        
        CQMat4 mat; // default is identity
        mat[0] = factor * (y2 + z2);
        mat[5] = factor * (x2 + z2);
        mat[10] = factor * (x2 + y2);
        return mat;
    }

    CQMat4 PhysicsMath::ComputeSphereInertiaTensor(float mass, float radius)
    {
        float i = (2.0f / 5.0f) * mass * radius * radius;
        CQMat4 mat;
        mat[0] = i; mat[5] = i; mat[10] = i;
        return mat;
    }

    CQMat4 PhysicsMath::ComputeCylinderInertiaTensor(float mass, float radius, float height)
    {
        float i_xx_zz = (1.0f / 12.0f) * mass * (3.0f * radius * radius + height * height);
        float i_yy = (1.0f / 2.0f) * mass * radius * radius;
        CQMat4 mat;
        mat[0] = i_xx_zz; mat[5] = i_yy; mat[10] = i_xx_zz;
        return mat;
    }

    CQMat4 PhysicsMath::ComputeConeInertiaTensor(float mass, float radius, float height)
    {
        float i_xx_zz = (3.0f / 80.0f) * mass * (4.0f * radius * radius + height * height);
        float i_yy = (3.0f / 10.0f) * mass * radius * radius;
        CQMat4 mat;
        mat[0] = i_xx_zz; mat[5] = i_yy; mat[10] = i_xx_zz;
        return mat;
    }

    CQMat4 PhysicsMath::ComputeCapsuleInertiaTensor(float mass, float radius, float height)
    {
        float cylMass = mass * (height / (height + (4.0f/3.0f)*radius));
        float sphMass = mass - cylMass;
        
        float i_yy = (0.5f * cylMass * radius * radius) + (0.4f * sphMass * radius * radius);
        
        float cyl_xx_zz = (1.0f/12.0f) * cylMass * (3.0f*radius*radius + height*height);
        float sph_xx_zz = sphMass * (0.4f*radius*radius + 0.25f*height*height + 0.375f*radius*height);
        
        float i_xx_zz = cyl_xx_zz + sph_xx_zz;
        
        CQMat4 mat;
        mat[0] = i_xx_zz; mat[5] = i_yy; mat[10] = i_xx_zz;
        return mat;
    }

    CQVec3 PhysicsMath::CalculateAngularMomentum(const CQVec3& angularVelocity, const float inertiaTensor[9])
    {
        CQVec3 L;
        L.x = inertiaTensor[0] * angularVelocity.x + inertiaTensor[1] * angularVelocity.y + inertiaTensor[2] * angularVelocity.z;
        L.y = inertiaTensor[3] * angularVelocity.x + inertiaTensor[4] * angularVelocity.y + inertiaTensor[5] * angularVelocity.z;
        L.z = inertiaTensor[6] * angularVelocity.x + inertiaTensor[7] * angularVelocity.y + inertiaTensor[8] * angularVelocity.z;
        return L;
    }

    CQVec3 PhysicsMath::CalculateGyroscopicTorque(const CQVec3& angularVelocity, const float inertiaTensor[9])
    {
        CQVec3 L = CalculateAngularMomentum(angularVelocity, inertiaTensor);
        return Vec3Cross(angularVelocity, L);
    }
}