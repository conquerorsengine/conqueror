#include "EntitySnapshotCommand.h"
#include "Scene/Components.h"

namespace Conqueror::Editor
{
    std::shared_ptr<Scene> EntitySnapshotCommand::Capture(Entity entity)
    {
        if (!entity)
            return nullptr;
        auto snapshot = std::make_shared<Scene>();
        Entity copy = snapshot->CreateEntityWithUUID((uint64_t)entity.GetComponent<IDComponent>().ID, entity.GetComponent<TagComponent>().Tag);
        snapshot->CopyEntityComponents(entity, copy, false);
        return snapshot;
    }

    void EntitySnapshotCommand::Restore(const std::shared_ptr<Scene>& snapshot, Entity target)
    {
        if (!snapshot || !target)
            return;
        auto view = snapshot->m_Registry.view<IDComponent>();
        if (view.empty())
            return;
        Entity source{ view.front(), snapshot.get() };
        Scene* scene = target.GetScene();
        scene->RemoveComponentsNotIn(source, target);
        scene->CopyEntityComponents(source, target, false);
        target.GetComponent<TagComponent>().Tag = source.GetComponent<TagComponent>().Tag;
        scene->UpdateWorldTransforms();
    }

    void EntitySnapshotCommand::Execute()
    {
        if (m_SkipFirstExecute)
        {
            m_SkipFirstExecute = false;
            return;
        }
        Restore(m_After, m_Scene ? m_Scene->FindEntityByUUID(m_UUID) : Entity{});
    }

    void EntitySnapshotCommand::Unexecute()
    {
        Restore(m_Before, m_Scene ? m_Scene->FindEntityByUUID(m_UUID) : Entity{});
    }
}
