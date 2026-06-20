#include "EditorState.h"
#include <algorithm>

namespace Conqueror::Editor
{
    void EditorState::SelectEntity(Entity entity)
    {
        m_SelectedEntities.clear();
        if (entity)
            m_SelectedEntities.push_back(entity);
    }

    void EditorState::SelectMultipleEntities(const std::vector<Entity>& entities)
    {
        m_SelectedEntities = entities;
    }

    void EditorState::ToggleEntitySelection(Entity entity)
    {
        if (!entity)
            return;

        auto it = std::find(m_SelectedEntities.begin(), m_SelectedEntities.end(), entity);
        
        if (it != m_SelectedEntities.end())
        {
            m_SelectedEntities.erase(it);
        }
        else
        {
            m_SelectedEntities.push_back(entity);
        }
    }

    void EditorState::ClearSelection()
    {
        m_SelectedEntities.clear();
    }

    bool EditorState::IsEntitySelected(Entity entity) const
    {
        return std::find(m_SelectedEntities.begin(), m_SelectedEntities.end(), entity) != m_SelectedEntities.end();
    }

    void EditorState::CopyEntities(const std::vector<Entity>& entities)
    {
        m_ClipboardEntities = entities;
        m_IsCutOperation = false;
    }

    void EditorState::CutEntities(const std::vector<Entity>& entities)
    {
        m_ClipboardEntities = entities;
        m_IsCutOperation = true;
    }

    void EditorState::ClearClipboard()
    {
        m_ClipboardEntities.clear();
        m_IsCutOperation = false;
    }

    void EditorState::FinalizeCut()
    {
        m_IsCutOperation = false;
        m_ClipboardEntities.clear();
    }

    void EditorState::PushUndoState(const std::string& state)
    {
        m_UndoStack.push_back(state);
        if (m_UndoStack.size() > s_MaxUndoSteps)
            m_UndoStack.erase(m_UndoStack.begin());
    }

    std::string EditorState::PopUndo()
    {
        if (m_UndoStack.empty())
            return {};
        std::string state = m_UndoStack.back();
        m_UndoStack.pop_back();
        return state;
    }

    std::string EditorState::PopRedo()
    {
        if (m_RedoStack.empty())
            return {};
        std::string state = m_RedoStack.back();
        m_RedoStack.pop_back();
        return state;
    }

    void EditorState::PushRedoState(const std::string& state)
    {
        m_RedoStack.push_back(state);
    }
}
