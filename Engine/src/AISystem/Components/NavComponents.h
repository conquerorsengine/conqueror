#pragma once

#include <glm/glm.hpp>
#include <cstdint>
#include <string>
#include <vector>

namespace Conqueror
{
    // Yol üstü nokta: LinkIndex >= 0 ise bu noktaya bir NavMesh Link ile (atlama / tırmanma) varılır
    struct NavPathPoint
    {
        glm::vec3 Position{ 0.0f };
        int LinkIndex = -1;
    };

    struct NavMeshAgentComponent
    {
        float Speed = 3.5f;
        float AngularSpeed = 120.0f;       // derece / s
        float Acceleration = 8.0f;
        float StoppingDistance = 0.5f;
        float Radius = 0.5f;
        float Height = 2.0f;
        float BaseOffset = 0.0f;           // pivot ile zemin arası
        bool AutoBraking = true;
        bool AutoTraverseLinks = true;
        bool AvoidOtherAgents = true;
        int AvoidancePriority = 50;        // düşük = öncelikli (diğerleri ondan kaçar)
        uint32_t AreaMask = 0xFFFFFFFFu;   // yürünebilecek alanlar

        bool IsStopped = false;

        // Yol durumu
        bool HasPath = false;
        std::vector<glm::vec3> Path;       // köşe noktaları (betik / eski API)
        std::vector<NavPathPoint> PathPoints;
        int CurrentPathIndex = 0;
        glm::vec3 Destination = glm::vec3(0.0f);
        glm::vec3 Velocity = glm::vec3(0.0f);
        float RemainingDistance = 0.0f;
        bool PathComplete = true;          // hedefe tam ulaşan yol mu (yoksa en yakın nokta)

        // Bağlantı (link) geçişi
        bool OnLink = false;
        float LinkT = 0.0f;
        glm::vec3 LinkStart{ 0.0f }, LinkEnd{ 0.0f };
        float RepathTimer = 0.0f;

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

    enum class NavCollectObjects : int { All = 0, Children };

    // Unity NavMeshSurface: ajan tipine göre yürünebilir yüzeyi pişirir (voksel yükseklik alanı)
    struct NavMeshSurfaceComponent
    {
        int AreaType = 0;                  // varsayılan alan (0 Walkable, 1 Not Walkable, 2 Jump, 3+ kullanıcı)
        bool OverrideVoxelSize = false;
        float VoxelSize = 0.166f;
        NavCollectObjects CollectObjects = NavCollectObjects::All;
        float AgentRadius = 0.5f;
        float AgentHeight = 2.0f;
        float MaxSlope = 45.0f;            // derece
        float StepHeight = 0.4f;
        bool BakeOnPlay = true;            // oyun başlarken (veri yoksa / sahne değiştiyse) pişir

        NavMeshSurfaceComponent() = default;
        NavMeshSurfaceComponent(const NavMeshSurfaceComponent&) = default;
    };

    // Unity NavMeshModifier: bu nesnenin (ve çocuklarının) geometrisinin alanını değiştirir ya da pişirmeden çıkarır
    struct NavMeshModifierComponent
    {
        bool OverrideArea = true;
        int Area = 1;                      // 1 Not Walkable
        bool IgnoreFromBuild = false;
        bool ApplyToChildren = true;
    };

    // Unity NavMeshModifierVolume: kutu içindeki yüzeylerin alanını değiştirir (su, çamur, yasak bölge)
    struct NavMeshModifierVolumeComponent
    {
        glm::vec3 Size{ 4.0f, 4.0f, 4.0f };
        glm::vec3 Center{ 0.0f };
        int Area = 1;
    };

    // Unity NavMeshLink: iki noktayı bağlar (atlama, merdiven, boşluk); ajan otomatik geçer
    struct NavMeshLinkComponent
    {
        glm::vec3 StartPoint{ 0.0f, 0.0f, -2.0f };   // yerel
        glm::vec3 EndPoint{ 0.0f, 0.0f, 2.0f };
        float Width = 0.0f;
        bool Bidirectional = true;
        float CostModifier = -1.0f;        // < 0: alan maliyeti
        int Area = 2;                      // 2 Jump
    };
}
