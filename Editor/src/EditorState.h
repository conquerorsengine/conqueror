#pragma once

#include "Scene/Entity.h"
#include <glm/glm.hpp>

#include <vector>
#include <memory>
#include <string>

namespace Conqueror::Editor
{
    enum class SceneState
    {
        Edit = 0,
        Play,
        Pause,
        Simulate
    };

    enum class GizmoOperation
    {
        None = -1,
        Translate = 0,
        Rotate,
        Scale
    };

    enum class GizmoMode
    {
        Local = 0,
        World
    };

    class EditorState
    {
    public:
        static EditorState& Get()
        {
            static EditorState instance;
            return instance;
        }

        // Selection management
        void SelectEntity(Entity entity);
        void SelectMultipleEntities(const std::vector<Entity>& entities);
        void ToggleEntitySelection(Entity entity);
        void ClearSelection();
        bool IsEntitySelected(Entity entity) const;
        
        Entity GetSelectedEntity() const { return m_SelectedEntities.empty() ? Entity{} : m_SelectedEntities[0]; }
        const std::vector<Entity>& GetSelectedEntities() const { return m_SelectedEntities; }
        bool HasSelection() const { return !m_SelectedEntities.empty(); }
        size_t GetSelectionCount() const { return m_SelectedEntities.size(); }

        // Copy/Paste clipboard
        void CopyEntities(const std::vector<Entity>& entities);
        void CutEntities(const std::vector<Entity>& entities);
        const std::vector<Entity>& GetClipboardEntities() const { return m_ClipboardEntities; }
        bool HasClipboardData() const { return !m_ClipboardEntities.empty(); }
        bool IsCutOperation() const { return m_IsCutOperation; }
        void ClearClipboard();
        void FinalizeCut();

        // Scene state
        void SetSceneState(SceneState state) { m_SceneState = state; }
        SceneState GetSceneState() const { return m_SceneState; }
        bool IsPlaying() const { return m_SceneState == SceneState::Play; }
        bool IsPaused() const { return m_SceneState == SceneState::Pause; }
        bool IsEditing() const { return m_SceneState == SceneState::Edit; }
        bool IsSimulating() const { return m_SceneState == SceneState::Simulate; }

        // Viewport
        void SetViewportSize(const glm::vec2& size) { m_ViewportSize = size; }
        glm::vec2 GetViewportSize() const { return m_ViewportSize; }
        
        void SetViewportBounds(const glm::vec2& min, const glm::vec2& max) 
        { 
            m_ViewportBounds[0] = min; 
            m_ViewportBounds[1] = max; 
        }
        const glm::vec2* GetViewportBounds() const { return m_ViewportBounds; }

        void SetViewportFocused(bool focused) { m_ViewportFocused = focused; }
        bool IsViewportFocused() const { return m_ViewportFocused; }
        
        void SetViewportHovered(bool hovered) { m_ViewportHovered = hovered; }
        bool IsViewportHovered() const { return m_ViewportHovered; }

        // Gizmo
        void SetGizmoOperation(GizmoOperation operation) { m_GizmoOperation = operation; }
        GizmoOperation GetGizmoOperation() const { return m_GizmoOperation; }
        
        void SetGizmoMode(GizmoMode mode) { m_GizmoMode = mode; }
        GizmoMode GetGizmoMode() const { return m_GizmoMode; }

        void ToggleGizmoMode() 
        { 
            m_GizmoMode = (m_GizmoMode == GizmoMode::Local) ? GizmoMode::World : GizmoMode::Local; 
        }

        // Snap settings
        void SetSnapEnabled(bool enabled) { m_SnapEnabled = enabled; }
        bool IsSnapEnabled() const { return m_SnapEnabled; }
        
        void SetSnapValue(float value) { m_SnapValue = value; }
        float GetSnapValue() const { return m_SnapValue; }

        // Undo/Redo
        void PushUndoState(const std::string& state);
        bool CanUndo() const { return !m_UndoStack.empty(); }
        bool CanRedo() const { return !m_RedoStack.empty(); }
        std::string PopUndo();
        std::string PopRedo();
        void PushRedoState(const std::string& state);
        void ClearRedoStack() { m_RedoStack.clear(); }

    private:
        EditorState() = default;
        ~EditorState() = default;
        EditorState(const EditorState&) = delete;
        EditorState& operator=(const EditorState&) = delete;

    private:
        // Selection
        std::vector<Entity> m_SelectedEntities;

        // Clipboard
        std::vector<Entity> m_ClipboardEntities;
        bool m_IsCutOperation = false;

        // Scene state
        SceneState m_SceneState = SceneState::Edit;

        // Viewport
        glm::vec2 m_ViewportSize = { 0.0f, 0.0f };
        glm::vec2 m_ViewportBounds[2] = { { 0.0f, 0.0f }, { 0.0f, 0.0f } };
        bool m_ViewportFocused = false;
        bool m_ViewportHovered = false;

        // Gizmo
        GizmoOperation m_GizmoOperation = GizmoOperation::Translate;
        GizmoMode m_GizmoMode = GizmoMode::Local;

        // Snap
        bool m_SnapEnabled = false;
        float m_SnapValue = 0.5f;

        // Undo/Redo
        std::vector<std::string> m_UndoStack;
        std::vector<std::string> m_RedoStack;
        static constexpr size_t s_MaxUndoSteps = 50;
    };
}
