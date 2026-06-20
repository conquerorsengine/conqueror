#pragma once

#include <glm/glm.hpp>
#include "AISystem/Components/NavComponents.h"

namespace Conqueror
{
    class SteeringBehaviors
    {
    public:
        // Klasik Craig Reynolds Steering Davranışları
        static glm::vec3 Seek(const glm::vec3& currentPos, const glm::vec3& targetPos, const glm::vec3& currentVelocity, float maxSpeed);
        static glm::vec3 Flee(const glm::vec3& currentPos, const glm::vec3& targetPos, const glm::vec3& currentVelocity, float maxSpeed);
        static glm::vec3 Arrive(const glm::vec3& currentPos, const glm::vec3& targetPos, const glm::vec3& currentVelocity, float maxSpeed, float slowingRadius);
        
        // Sürü (Flocking) Davranışları
        static glm::vec3 Separation(const glm::vec3& currentPos, const std::vector<glm::vec3>& neighborPositions, float separationRadius);
        static glm::vec3 Alignment(const glm::vec3& currentVelocity, const std::vector<glm::vec3>& neighborVelocities);
        static glm::vec3 Cohesion(const glm::vec3& currentPos, const std::vector<glm::vec3>& neighborPositions);
    };
}
