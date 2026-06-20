#include "SteeringBehaviors.h"

namespace Conqueror
{
    glm::vec3 SteeringBehaviors::Seek(const glm::vec3& currentPos, const glm::vec3& targetPos, const glm::vec3& currentVelocity, float maxSpeed)
    {
        glm::vec3 desiredVelocity = targetPos - currentPos;
        if (glm::length(desiredVelocity) > 0.001f)
            desiredVelocity = glm::normalize(desiredVelocity) * maxSpeed;
        
        return desiredVelocity - currentVelocity;
    }

    glm::vec3 SteeringBehaviors::Flee(const glm::vec3& currentPos, const glm::vec3& targetPos, const glm::vec3& currentVelocity, float maxSpeed)
    {
        glm::vec3 desiredVelocity = currentPos - targetPos;
        if (glm::length(desiredVelocity) > 0.001f)
            desiredVelocity = glm::normalize(desiredVelocity) * maxSpeed;
        
        return desiredVelocity - currentVelocity;
    }

    glm::vec3 SteeringBehaviors::Arrive(const glm::vec3& currentPos, const glm::vec3& targetPos, const glm::vec3& currentVelocity, float maxSpeed, float slowingRadius)
    {
        glm::vec3 desiredVelocity = targetPos - currentPos;
        float distance = glm::length(desiredVelocity);

        if (distance < 0.001f)
            return glm::vec3(0.0f) - currentVelocity;

        desiredVelocity = glm::normalize(desiredVelocity);

        if (distance < slowingRadius)
            desiredVelocity *= maxSpeed * (distance / slowingRadius);
        else
            desiredVelocity *= maxSpeed;

        return desiredVelocity - currentVelocity;
    }

    glm::vec3 SteeringBehaviors::Separation(const glm::vec3& currentPos, const std::vector<glm::vec3>& neighborPositions, float separationRadius)
    {
        glm::vec3 steeringForce(0.0f);
        int count = 0;

        for (const auto& neighborPos : neighborPositions)
        {
            glm::vec3 toNeighbor = currentPos - neighborPos;
            float distance = glm::length(toNeighbor);

            if (distance > 0.001f && distance < separationRadius)
            {
                steeringForce += glm::normalize(toNeighbor) / distance; // Yakın olanlar daha çok iter
                count++;
            }
        }

        if (count > 0)
            steeringForce /= (float)count;

        return steeringForce;
    }

    glm::vec3 SteeringBehaviors::Alignment(const glm::vec3& currentVelocity, const std::vector<glm::vec3>& neighborVelocities)
    {
        glm::vec3 averageVelocity(0.0f);
        if (neighborVelocities.empty())
            return averageVelocity;

        for (const auto& vel : neighborVelocities)
            averageVelocity += vel;

        averageVelocity /= (float)neighborVelocities.size();

        return averageVelocity - currentVelocity;
    }

    glm::vec3 SteeringBehaviors::Cohesion(const glm::vec3& currentPos, const std::vector<glm::vec3>& neighborPositions)
    {
        if (neighborPositions.empty())
            return glm::vec3(0.0f);

        glm::vec3 centerOfMass(0.0f);
        for (const auto& pos : neighborPositions)
            centerOfMass += pos;

        centerOfMass /= (float)neighborPositions.size();

        return centerOfMass - currentPos; // Seek behavior tarzı
    }
}
