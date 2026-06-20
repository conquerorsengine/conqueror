#pragma once

#include "EditorCommand.h"
#include <vector>
#include <memory>
#include <string>

namespace Conqueror::Editor
{
    class UndoRedoManager
    {
    public:
        static UndoRedoManager& Get()
        {
            static UndoRedoManager instance;
            return instance;
        }

        void Execute(std::shared_ptr<EditorCommand> command);
        void Undo();
        void Redo();

        bool CanUndo() const { return !m_UndoStack.empty(); }
        bool CanRedo() const { return !m_RedoStack.empty(); }

        std::string GetUndoDescription() const;
        std::string GetRedoDescription() const;

        void Clear();

    private:
        UndoRedoManager() = default;

        std::vector<std::shared_ptr<EditorCommand>> m_UndoStack;
        std::vector<std::shared_ptr<EditorCommand>> m_RedoStack;
        static constexpr size_t s_MaxStack = 100;
    };
}
