#pragma once

#include "Core/Base/Base.h"

#include <memory>
#include <string>
#include <vector>

// Behavior Tree (.cqbt) + Blackboard (.cqblackboard), Unreal karşılıkları: Behavior Tree bileşeni her kare ağacı çalıştırır.
namespace Conqueror
{
    class Scene;
    namespace Scripting { class NativeRegistry; }

    struct BehaviorTreeComponent
    {
        struct Runtime;
        std::string Tree;
        bool Enabled = true;
        float TickInterval = 0.0f;
        std::string RunningTask;      // bilgi

        std::shared_ptr<Runtime> RT;
    };

    namespace BehaviorTrees
    {
        constexpr const char* Extension = ".cqbt";
        constexpr const char* Kind = "BehaviorTree";
        CQ_API void Update(Scene& scene, float dt);
        CQ_API std::vector<int> ActiveNodes(const std::string& path);
        void RegisterComponents();
        void RegisterNatives(Scripting::NativeRegistry* vm);
    }
}
