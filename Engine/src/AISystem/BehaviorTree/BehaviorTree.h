#pragma once

#include "Blackboard.h"
#include <vector>
#include <memory>

namespace Conqueror
{
    enum class NodeStatus
    {
        Success,
        Failure,
        Running
    };

    class BTNode
    {
    public:
        virtual ~BTNode() = default;
        virtual void Initialize(Blackboard* blackboard) {}
        virtual NodeStatus Tick(Blackboard* blackboard) = 0;
    };

    class BTComposite : public BTNode
    {
    public:
        void AddChild(std::shared_ptr<BTNode> child) { m_Children.push_back(child); }
    protected:
        std::vector<std::shared_ptr<BTNode>> m_Children;
    };

    class BTSelector : public BTComposite
    {
    public:
        virtual NodeStatus Tick(Blackboard* blackboard) override;
    };

    class BTSequence : public BTComposite
    {
    public:
        virtual NodeStatus Tick(Blackboard* blackboard) override;
    };

    class BTDecorator : public BTNode
    {
    public:
        void SetChild(std::shared_ptr<BTNode> child) { m_Child = child; }
    protected:
        std::shared_ptr<BTNode> m_Child;
    };

    class BehaviorTree
    {
    public:
        BehaviorTree() { m_Blackboard = std::make_unique<Blackboard>(); }
        
        void SetRoot(std::shared_ptr<BTNode> root) { m_Root = root; }
        Blackboard* GetBlackboard() { return m_Blackboard.get(); }
        
        void Tick()
        {
            if (m_Root)
                m_Root->Tick(m_Blackboard.get());
        }

    private:
        std::shared_ptr<BTNode> m_Root;
        std::unique_ptr<Blackboard> m_Blackboard;
    };
}
