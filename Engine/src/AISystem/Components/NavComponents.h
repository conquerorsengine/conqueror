#pragma once

#include <glm/glm.hpp>
#include <vector>

namespace Conqueror
{
    struct NavMeshAgentComponent
    {
        float Speed = 3.5f;
        float AngularSpeed = 120.0f;
        float Acceleration = 8.0f;
        float StoppingDistance = 0.5f;
        float Radius = 0.5f;
        float Height = 2.0f;

        bool AutoBraking = true;
        bool IsStopped = false;

        // Pathfinding durumu
        bool HasPath = false;
        std::vector<glm::vec3> Path;
        int CurrentPathIndex = 0;
        
        glm::vec3 Destination = glm::vec3(0.0f);
        glm::vec3 Velocity = glm::vec3(0.0f);

        NavMeshAgentComponent() = default;
        NavMeshAgentComponent(const NavMeshAgentComponent&) = default;
    };

    struct NavMeshObstacleComponent
    {
        float Radius = 0.5f;
        glm::vec3 Size = glm::vec3(1.0f);
        glm::vec3 Center = glm::vec3(0.0f);
        
        bool Carve = true; // NavMesh'te delik açıp açmayacağı

        NavMeshObstacleComponent() = default;
        NavMeshObstacleComponent(const NavMeshObstacleComponent&) = default;
    };

    struct NavMeshSurfaceComponent
    {
        int AreaType = 0; // 0: Walkable, 1: Not Walkable, 2: Jump vs...
        bool OverrideVoxelSize = false;
        float VoxelSize = 0.166f;
        
        NavMeshSurfaceComponent() = default;
        NavMeshSurfaceComponent(const NavMeshSurfaceComponent&) = default;
    };
}
