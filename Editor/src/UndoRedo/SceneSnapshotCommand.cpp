#include "SceneSnapshotCommand.h"
#include "Core/Serialization/SceneSerializer.h"

#include <sstream>

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
}
