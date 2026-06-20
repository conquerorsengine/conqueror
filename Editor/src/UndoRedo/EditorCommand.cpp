#include "EditorCommand.h"
#include "Scene/Components.h"

namespace Conqueror::Editor
{
    CreateEntityCommand::CreateEntityCommand(const std::shared_ptr<Scene>& scene, const std::string& name)
        : m_Scene(scene), m_Name(name) {}

    void CreateEntityCommand::Execute()
    {
        if (!m_Created)
        {
            m_Entity = m_Scene->CreateEntity(m_Name);
            m_Created = true;
        }
        else
        {
            m_Scene->CreateEntityWithUUID(m_Entity.GetComponent<IDComponent>().ID, m_Name);
        }
    }

    void CreateEntityCommand::Unexecute()
    {
        if (m_Created && m_Entity)
        {
            m_Scene->DestroyEntity(m_Entity);
            m_Created = false;
        }
    }

    DestroyEntityCommand::DestroyEntityCommand(const std::shared_ptr<Scene>& scene, Entity entity)
        : m_Scene(scene), m_Entity(entity)
    {
        if (entity)
        {
            m_EntityName = entity.GetComponent<TagComponent>().Tag;

            if (entity.HasComponent<TransformComponent>())
                m_Snapshot.Transform = entity.GetComponent<TransformComponent>();

            if (entity.HasComponent<LayerComponent>())
                m_Snapshot.Layer = entity.GetComponent<LayerComponent>();
        }
    }

    void DestroyEntityCommand::Execute()
    {
        if (m_Entity && !m_Destroyed)
        {
            m_Scene->DestroyEntity(m_Entity);
            m_Destroyed = true;
        }
    }

    void DestroyEntityCommand::Unexecute()
    {
        if (m_Destroyed)
        {
            m_Entity = m_Scene->CreateEntity(m_EntityName);
            if (m_Entity)
            {
                m_Entity.GetComponent<TransformComponent>() = m_Snapshot.Transform;
                m_Entity.GetComponent<LayerComponent>() = m_Snapshot.Layer;
            }
            m_Destroyed = false;
        }
    }

    RenameEntityCommand::RenameEntityCommand(Entity entity, const std::string& newName)
        : m_Entity(entity), m_NewName(newName)
    {
        if (entity)
            m_OldName = entity.GetComponent<TagComponent>().Tag;
    }

    void RenameEntityCommand::Execute()
    {
        if (m_Entity)
            m_Entity.GetComponent<TagComponent>().Tag = m_NewName;
    }

    void RenameEntityCommand::Unexecute()
    {
        if (m_Entity)
            m_Entity.GetComponent<TagComponent>().Tag = m_OldName;
    }

    ReparentEntityCommand::ReparentEntityCommand(Entity entity, Entity newParent)
        : m_Entity(entity), m_NewParent(newParent)
    {
        if (entity)
        {
            m_OldParent = entity.GetParent();
            m_HadOldParent = (bool)m_OldParent;
        }
    }

    void ReparentEntityCommand::Execute()
    {
        if (m_Entity)
        {
            if (m_NewParent)
                m_Entity.SetParent(m_NewParent);
            else
                m_Entity.RemoveParent();
        }
    }

    void ReparentEntityCommand::Unexecute()
    {
        if (m_Entity)
        {
            if (m_HadOldParent)
                m_Entity.SetParent(m_OldParent);
            else
                m_Entity.RemoveParent();
        }
    }

    DuplicateEntityCommand::DuplicateEntityCommand(const std::shared_ptr<Scene>& scene, Entity source)
        : m_Scene(scene), m_Source(source) {}

    void DuplicateEntityCommand::Execute()
    {
        if (!m_Created)
        {
            m_NewEntity = m_Scene->DuplicateEntity(m_Source);
            m_Created = true;
        }
        else
        {
            m_NewEntity = m_Scene->DuplicateEntity(m_Source);
        }
    }

    void DuplicateEntityCommand::Unexecute()
    {
        if (m_Created && m_NewEntity)
        {
            m_Scene->DestroyEntity(m_NewEntity);
            m_Created = false;
        }
    }

    ModifyTransformCommand::ModifyTransformCommand(Entity entity, const TransformComponent& oldTransform, const TransformComponent& newTransform)
        : m_Entity(entity), m_OldTransform(oldTransform), m_NewTransform(newTransform) {}

    void ModifyTransformCommand::Execute()
    {
        if (m_Entity && m_Entity.HasComponent<TransformComponent>())
            m_Entity.GetComponent<TransformComponent>() = m_NewTransform;
    }

    void ModifyTransformCommand::Unexecute()
    {
        if (m_Entity && m_Entity.HasComponent<TransformComponent>())
            m_Entity.GetComponent<TransformComponent>() = m_OldTransform;
    }
}
