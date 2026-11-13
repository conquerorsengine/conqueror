#pragma once

#include "Core/Base/Base.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

// 2D gezinme (Godot NavigationRegion2D / NavigationAgent2D / NavigationObstacle2D / NavigationLink2D): XY düzleminde yürünebilir
// bölgeler (dış çizgi çokgeni) hücre ızgarasına rasterleştirilir; bölgenin içindeki statik 2D çarpıştırıcılar (kutu, daire, kapsül,
// çokgen, katı karolar) ve Carve engeller ajan yarıçapı kadar şişirilip çıkarılır. Örtüşen / bitişik bölgeler birleşir. Yol bulma
// 8 komşulu A* (köşe kesmeden, bağlantılarla) + görüş hattı ile köşe noktalarına indirgeme. Ajanlar yolu izler, birbirinden ve
// kaçınma engellerinden sakınır; hedefe ulaşamazsa en yakın noktaya gider
namespace Conqueror
{
    class Scene;

    struct NavigationRegion2DComponent
    {
        std::vector<glm::vec2> Outline{ { -5.0f, -5.0f }, { 5.0f, -5.0f }, { 5.0f, 5.0f }, { -5.0f, 5.0f } };   // yerel, yürünebilir alan
        float CellSize = 0.1f;          // ızgara çözünürlüğü (birim)
        float AgentRadius = 0.25f;      // kenarlardan ve engellerden bu kadar uzak durulur
        bool ParseColliders = true;     // bölge içindeki statik 2D çarpıştırıcılar ve katı karolar engel
        float TravelCost = 1.0f;        // bu bölgede yürümenin maliyet çarpanı
        bool Enabled = true;
        // Okunur
        int WalkableCells = 0;
        bool Baked = false;
    };

    struct NavigationAgent2DComponent
    {
        glm::vec2 TargetPosition{ 0.0f };
        bool HasTarget = false;               // TargetPosition ayarlanınca (Inspector / betik) true
        float MaxSpeed = 3.0f;                // birim / saniye
        float PathDesiredDistance = 0.15f;    // bir yol noktasına bu kadar yaklaşınca sıradakine geçer
        float TargetDesiredDistance = 0.15f;  // hedefe bu kadar yaklaşınca biter
        float Radius = 0.25f;                 // kaçınma yarıçapı
        bool AvoidanceEnabled = true;
        bool AutoMove = true;                 // nesneyi kendisi yürütür (Character Body 2D / Rigidbody 2D varsa onlarla)
        float RepathInterval = 0.25f;         // hedef hareket ediyorsa yol bu sıklıkta yenilenir (saniye)
        // Okunur
        bool Finished = true;
        bool Reachable = true;
        glm::vec2 NextPathPosition{ 0.0f };
        glm::vec2 Velocity{ 0.0f };
        float PathLength = 0.0f;
        // Çalışma zamanı
        std::vector<glm::vec2> Path;
        int PathIndex = 0;
        glm::vec2 PathTarget{ 1e30f };
        float RepathTimer = 0.0f;
    };

    struct NavigationObstacle2DComponent
    {
        float Radius = 0.5f;                  // daire engel (Size sıfırsa)
        glm::vec2 Size{ 0.0f };               // kutu engel (yerel, tam boyut)
        bool CarveNavigation = true;          // gezinme alanından oyar (hareket edince güncellenir)
        bool AvoidanceEnabled = true;         // ajanlar ondan sakınır
    };

    struct NavigationLink2DComponent
    {
        glm::vec2 StartPosition{ -1.0f, 0.0f };   // yerel
        glm::vec2 EndPosition{ 1.0f, 0.0f };
        bool Bidirectional = true;
        float TravelCost = 1.0f;
        bool Enabled = true;
    };

    namespace Navigation2D
    {
        // Bölgeleri (yeniden) ızgaraya dök; Play başında ve değişiklikte kendiliğinden çağrılır
        CQ_API void Bake(Scene& scene);
        CQ_API bool IsBaked(Scene& scene);
        // Yol: köşe noktaları (başlangıç dahil); hedefe varılamazsa en yakın noktaya, complete = false. Yol yoksa false
        CQ_API bool FindPath(Scene& scene, const glm::vec2& start, const glm::vec2& end, std::vector<glm::vec2>& outPath, bool* complete = nullptr);
        CQ_API bool IsWalkable(Scene& scene, const glm::vec2& point);
        // En yakın yürünebilir nokta (maxDistance içinde)
        CQ_API bool ClosestPoint(Scene& scene, const glm::vec2& point, glm::vec2& out, float maxDistance = 5.0f);
        // Ajanlar (oyunda her kare)
        CQ_API void Update(Scene& scene, float dt);
        CQ_API void OnRuntimeStart(Scene& scene);
        CQ_API void ForgetScene(Scene* scene);
        CQ_API bool SetTarget(Scene& scene, uint32_t entity, const glm::vec2& target);
        // Düzenleyici: seçili bölgelerin yürünebilir alanı, bağlantılar ve ajan yolları
        CQ_API void DrawEditorGizmos(Scene& scene, const std::vector<uint32_t>& selection);
    }

    void RegisterNavigation2DComponents();
}
