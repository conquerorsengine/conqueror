#pragma once

#include "Core/Base/Base.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

// 2D iskelet (Godot Skeleton2D / Bone2D + IK değişiklikleri, Unity 2D Animation IK): kemikler sıradan varlık
// hiyerarşisidir (sprite'lar kemiğin altına), Bone 2D kemiği yerel +X boyunca Length uzunluğunda tanımlar ve
// düzenleyicide kemik gizmo'su çizer. IK 2D uç kemiğe eklenir; zinciri (CCD ya da iki kemik analitik) hedefe uzatır.
namespace Conqueror
{
    class Scene;

    struct Bone2DComponent
    {
        float Length = 1.0f;                  // yerel +X boyunca (dünya ölçeğiyle çarpılır)
        bool ShowGizmo = true;
        glm::vec4 GizmoColor{ 0.95f, 0.75f, 0.3f, 0.9f };
    };

    enum class IK2DSolver : int { CCD = 0, TwoBone };

    struct IK2DComponent
    {
        bool Enabled = true;
        IK2DSolver Solver = IK2DSolver::CCD;
        uint64_t Target = 0;
        int ChainLength = 2;                  // döndürülen eklem sayısı (CCD)
        int Iterations = 10;
        bool FlipBend = false;                // iki kemik: dirsek / diz hangi yana
        float Weight = 1.0f;
        bool RunInEditor = true;

        // Çalışma zamanı
        float Error = 0.0f;                   // uç ile hedef arası (bilgi)
    };

    namespace Skeleton2D
    {
        CQ_API void Update(Scene& scene, bool runtime);
        CQ_API void DrawEditorGizmos(Scene& scene, const std::vector<uint32_t>& selection);
    }

    void RegisterSkeleton2DComponents();
}
