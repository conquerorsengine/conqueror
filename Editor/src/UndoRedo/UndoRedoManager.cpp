#include "UndoRedoManager.h"

namespace Conqueror::Editor
{
    void UndoRedoManager::Execute(std::shared_ptr<EditorCommand> command)
    {
        if (!command)
            return;

        command->Execute();
        m_UndoStack.push_back(command);
        m_RedoStack.clear();

        if (m_UndoStack.size() > s_MaxStack)
            m_UndoStack.erase(m_UndoStack.begin());
    }

    void UndoRedoManager::Undo()
    {
        if (m_UndoStack.empty())
            return;

        auto command = m_UndoStack.back();
        m_UndoStack.pop_back();

        command->Unexecute();
        m_RedoStack.push_back(command);
    }

    void UndoRedoManager::Redo()
    {
        if (m_RedoStack.empty())
            return;

        auto command = m_RedoStack.back();
        m_RedoStack.pop_back();

        command->Execute();
        m_UndoStack.push_back(command);
    }

    std::string UndoRedoManager::GetUndoDescription() const
    {
        if (m_UndoStack.empty())
            return "";
        return m_UndoStack.back()->GetDescription();
    }

    std::string UndoRedoManager::GetRedoDescription() const
    {
        if (m_RedoStack.empty())
            return "";
        return m_RedoStack.back()->GetDescription();
    }

    void UndoRedoManager::Clear()
    {
        m_UndoStack.clear();
        m_RedoStack.clear();
    }
}
