#pragma once

#include "Core/Base/Base.h"

#include <glm/glm.hpp>

#include <memory>
#include <string>
#include <vector>

// Create menüsündeki varlıkların çalışma zamanı parçaları: Flipbook, Force Feedback, Touch Interface, Camera Shake, Curve Table
namespace Conqueror
{
    class Scene;
    class Texture2D;
    namespace Scripting { class NativeRegistry; }

    // Flipbook oynatıcı: aynı nesnedeki Sprite Renderer'ın görselini kare kare değiştirir (Unreal Paper2D Flipbook)
    struct FlipbookComponent
    {
        std::string Flipbook;         // .cqflipbook
        bool Playing = true;
        float Speed = 1.0f;

        // Çalışma zamanı
        float Time = 0.0f;
        int Frame = -1;
    };

    namespace AssetRuntime
    {
        CQ_API void Update(Scene& scene, float dt);
        CQ_API void Reset();
        CQ_API void RegisterComponents();
        void RegisterNatives(Scripting::NativeRegistry* vm);

        // Touch Interface: ekran denetimleri
        struct TouchControl
        {
            std::string Name, Type;     // Joystick / Button
            glm::vec2 Center{ 0.5f };   // ekran oranı (0..1, sol alt köken)
            float Radius = 0.08f;       // ekran yüksekliğine oran
            int AxisX = -1, AxisY = -1, Button = -1;
            glm::vec2 Knob{ 0.0f };
            bool Held = false;
        };
        CQ_API bool ActivateTouchInterface(const std::string& path);
        CQ_API void DeactivateTouchInterface();
        CQ_API std::vector<TouchControl>& ActiveTouchControls();
    }
}
