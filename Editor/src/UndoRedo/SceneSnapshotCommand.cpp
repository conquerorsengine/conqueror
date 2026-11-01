#include "SceneSnapshotCommand.h"
#include "Core/Serialization/SceneSerializer.h"

#include <sstream>
#include <unordered_set>

namespace Conqueror::Editor
{
    std::string SceneSnapshotCommand::Capture(const std::shared_ptr<Scene>& scene)
    {
        if (!scene)
            return {};
        std::ostringstream out;
        SceneSerializer(scene).SerializeToStream(out);
        return out.str();
    }

    void SceneSnapshotCommand::Restore(const std::shared_ptr<Scene>& scene, const std::string& snapshot)
    {
        if (!scene || snapshot.empty())
            return;
        scene->ClearEntities();
        std::istringstream in(snapshot);
        SceneSerializer(scene).DeserializeFromStream(in);
        scene->UpdateWorldTransforms();
    }

    void SceneSnapshotCommand::Execute()
    {
        if (m_SkipFirstExecute)
        {
            m_SkipFirstExecute = false;
            return;
        }
        Restore(m_Scene, m_After);
        if (m_OnRestored)
            m_OnRestored();
    }

    void SceneSnapshotCommand::Unexecute()
    {
        Restore(m_Scene, m_Before);
        if (m_OnRestored)
            m_OnRestored();
    }

    void CreateEntitiesCommand::Execute()
    {
        if (m_SkipFirstExecute)
        {
            m_SkipFirstExecute = false;
            return;
        }
        SceneSnapshotCommand::Restore(m_Scene, m_After);
        if (m_OnRestored)
            m_OnRestored();
    }

    void CreateEntitiesCommand::Unexecute()
    {
        if (!m_Scene)
            return;
        // Oluşturulan nesnelerin altına alınmış ESKİ nesneler (ör. "Create Empty Parent") silinmez: üst nesnenin
        // yerine geri taşınır
        std::unordered_set<uint64_t> created(m_Created.begin(), m_Created.end());
        for (uint64_t id : m_Roots)
        {
            Entity root = m_Scene->FindEntityByUUID(id);
            if (!root)
                continue;
            for (Entity child : root.GetChildren())
            {
                const uint64_t cid = (uint64_t)child.GetComponent<IDComponent>().ID;
                if (created.count(cid))
                    continue;
                auto& ct = child.GetComponent<TransformComponent>();
                const auto& rt = root.GetComponent<TransformComponent>();
                ct.Position += rt.Position;
                if (Entity up = root.GetParent())
                    child.SetParent(up);
                else
                    child.RemoveParent();
            }
            m_Scene->DestroyEntity(root);
        }
        m_Scene->UpdateWorldTransforms();
        if (m_OnRestored)
            m_OnRestored();
    }
}
