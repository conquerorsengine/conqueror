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

    Entity DestroyEntityCommand::CloneTree(Scene& destination, Entity source, Entity parent)
    {
        const uint64_t id = (uint64_t)source.GetComponent<IDComponent>().ID;
        Entity copy = destination.CreateEntityWithUUID(id, source.GetComponent<TagComponent>().Tag);
        destination.CopyEntityComponents(source, copy, false);
        if (parent)
            destination.SetEntityParent(copy, parent, false);
        for (Entity child : source.GetChildren())
            CloneTree(destination, child, copy);
        return copy;
    }

    DestroyEntityCommand::DestroyEntityCommand(const std::shared_ptr<Scene>& scene, Entity entity)
        : m_Scene(scene)
    {
        if (!entity.IsValid())
            return;
        m_EntityName = entity.GetComponent<TagComponent>().Tag;
        m_RootUUID = (uint64_t)entity.GetComponent<IDComponent>().ID;
        if (Entity parent = entity.GetParent())
            m_ParentUUID = (uint64_t)parent.GetComponent<IDComponent>().ID;
        m_Backup = std::make_shared<Scene>();
        m_BackupRoot = (uint64_t)CloneTree(*m_Backup, entity, Entity{}).GetComponent<IDComponent>().ID;
    }

    void DestroyEntityCommand::Execute()
    {
        if (m_Destroyed || !m_Scene || !m_Backup)
            return;
        if (Entity e = m_Scene->FindEntityByUUID(m_RootUUID))
            m_Scene->DestroyEntity(e);
        m_Destroyed = true;
    }

    void DestroyEntityCommand::Unexecute()
    {
        if (!m_Destroyed || !m_Scene || !m_Backup)
            return;
        Entity source = m_Backup->FindEntityByUUID(m_BackupRoot);
        Entity parent = m_ParentUUID ? m_Scene->FindEntityByUUID(m_ParentUUID) : Entity{};
        if (source)
            CloneTree(*m_Scene, source, parent);
        m_Scene->UpdateWorldTransforms();
        m_Destroyed = false;
    }

    // Komutlar nesneyi UUID ile bulur: silme/oluşturma geri alınınca nesne yeniden yaratılsa da (yeni entt tanıtıcısı)
    // sonraki geri al / ileri al adımları doğru nesneye uygulanır
    static uint64_t IdOf(Entity e) { return e ? (uint64_t)e.GetComponent<IDComponent>().ID : 0; }
    static Entity Find(Scene* scene, uint64_t id) { return scene && id ? scene->FindEntityByUUID(id) : Entity{}; }

    RenameEntityCommand::RenameEntityCommand(Entity entity, const std::string& newName)
        : m_Scene(entity ? entity.GetScene() : nullptr), m_UUID(IdOf(entity)), m_NewName(newName)
    {
        if (entity)
            m_OldName = entity.GetComponent<TagComponent>().Tag;
    }

    void RenameEntityCommand::Execute()
    {
        if (Entity e = Find(m_Scene, m_UUID))
            e.GetComponent<TagComponent>().Tag = m_NewName;
    }

    void RenameEntityCommand::Unexecute()
    {
        if (Entity e = Find(m_Scene, m_UUID))
            e.GetComponent<TagComponent>().Tag = m_OldName;
    }

    ReparentEntityCommand::ReparentEntityCommand(Entity entity, Entity newParent)
        : m_Scene(entity ? entity.GetScene() : nullptr), m_UUID(IdOf(entity)), m_NewParent(IdOf(newParent))
    {
        if (entity)
            m_OldParent = IdOf(entity.GetParent());
    }

    void ReparentEntityCommand::Execute()
    {
        Entity e = Find(m_Scene, m_UUID);
        if (!e)
            return;
        if (Entity p = Find(m_Scene, m_NewParent))
            e.SetParent(p);
        else
            e.RemoveParent();
    }

    void ReparentEntityCommand::Unexecute()
    {
        Entity e = Find(m_Scene, m_UUID);
        if (!e)
            return;
        if (Entity p = Find(m_Scene, m_OldParent))
            e.SetParent(p);
        else
            e.RemoveParent();
    }

    DuplicateEntityCommand::DuplicateEntityCommand(const std::shared_ptr<Scene>& scene, Entity source)
        : m_Scene(scene), m_SourceUUID(IdOf(source)) {}

    void DuplicateEntityCommand::Execute()
    {
        if (m_Created)
            return;
        if (Entity source = Find(m_Scene.get(), m_SourceUUID))
        {
            m_NewEntity = m_Scene->DuplicateEntity(source);
            m_NewUUID = IdOf(m_NewEntity);
            m_Created = true;
        }
    }

    void DuplicateEntityCommand::Unexecute()
    {
        if (!m_Created)
            return;
        if (Entity e = Find(m_Scene.get(), m_NewUUID))
            m_Scene->DestroyEntity(e);
        m_NewEntity = {};
        m_Created = false;
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
