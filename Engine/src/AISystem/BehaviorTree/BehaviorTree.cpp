#include "BehaviorTree.h"

namespace Conqueror
{
    NodeStatus BTSelector::Tick(Blackboard* blackboard)
    {
        for (auto& child : m_Children)
        {
            NodeStatus status = child->Tick(blackboard);
            if (status != NodeStatus::Failure)
            {
                return status; // Success veya Running dönerse direkt çık.
            }
        }
        return NodeStatus::Failure; // Bütün çocuklar fail olursa fail döner.
    }

    NodeStatus BTSequence::Tick(Blackboard* blackboard)
    {
        for (auto& child : m_Children)
        {
            NodeStatus status = child->Tick(blackboard);
            if (status != NodeStatus::Success)
            {
                return status; // Failure veya Running dönerse direkt çık.
            }
        }
        return NodeStatus::Success; // Bütün çocuklar başarılı olursa success.
    }
}
