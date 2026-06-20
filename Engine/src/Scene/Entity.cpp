#include "Entity.h"

namespace Conqueror
{
    Entity::Entity(entt::entity handle, Scene* scene)
        : m_EntityHandle(handle), m_Scene(scene)
    {
    }

    void Entity::SetParent(Entity parent)
    {
        if (m_Scene)
            m_Scene->SetEntityParent(*this, parent);
    }

    void Entity::RemoveParent()
    {
        if (m_Scene)
            m_Scene->RemoveEntityParent(*this);
    }

    Entity Entity::GetParent() const
    {
        if (m_Scene)
            return m_Scene->GetEntityParent(*this);
        return Entity{};
    }

    std::vector<Entity> Entity::GetChildren() const
    {
        if (m_Scene)
            return m_Scene->GetEntityChildren(*this);
        return {};
    }

    bool Entity::HasChildren() const
    {
        if (m_Scene)
            return m_Scene->HasChildren(*this);
        return false;
    }
}
