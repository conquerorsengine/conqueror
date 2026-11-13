#pragma once

#include "Scene/Entity.h"
#include <glm/glm.hpp>

#include <vector>
#include <unordered_set>
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
        // Etkin sahneden silinmiş (tanıtıcısı geçersiz) nesneleri seçimden çıkarır; bir şey çıktıysa true
        bool PruneSelection(const Scene* activeScene);
        bool IsEntitySelected(Entity entity) const;
        
        Entity GetSelectedEntity() const { return m_SelectedEntities.empty() ? Entity{} : m_SelectedEntities[0]; }
        const std::vector<Entity>& GetSelectedEntities() const { return m_SelectedEntities; }
        bool HasSelection() const { return !m_SelectedEntities.empty(); }
        size_t GetSelectionCount() const { return m_SelectedEntities.size(); }

        // Varlık (dosya) seçimi: Inspector varlık yerine dosyayı gösterir (Unity: Project penceresi seçimi)
        void SelectAsset(const std::string& path) { m_SelectedAsset = path; m_SelectedAssetSlice.clear(); if (!path.empty()) m_SelectedEntities.clear(); }

        // Editör ileti penceresi (ör. Unity'nin "Can't add script" iletişim kutusu): EditorLayer bir sonraki karede gösterir
        void ShowMessage(const std::string& title, const std::string& text) { m_MessageTitle = title; m_MessageText = text; m_MessagePending = true; }
        bool TakeMessage(std::string& title, std::string& text)
        {
            if (!m_MessagePending)
                return false;
            m_MessagePending = false;
            title = m_MessageTitle;
            text = m_MessageText;
            return true;
        }
        const std::string& GetSelectedAsset() const { return m_SelectedAsset; }
        // Seçili görselin dilimi (İçerik Tarayıcısı > oku); boşsa görselin kendisi
        void SetSelectedAssetSlice(const std::string& slice) { m_SelectedAssetSlice = slice; }
        const std::string& GetSelectedAssetSlice() const { return m_SelectedAssetSlice; }

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

        // Create menüsünden Viewport'a sürüklenirken önizlenen, henüz yerleştirilmemiş nesneler (UUID): geri alma kaydı onları
        // yerleştirilene kadar saymaz (iptal edilen sürükleme geri alma geçmişinde adım bırakmaz)
        void SetPlacementPreview(const std::vector<uint64_t>& ids) { m_PlacementPreview = std::unordered_set<uint64_t>(ids.begin(), ids.end()); }
        void ClearPlacementPreview() { m_PlacementPreview.clear(); }
        const std::unordered_set<uint64_t>& GetPlacementPreview() const { return m_PlacementPreview; }

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
        std::unordered_set<uint64_t> m_PlacementPreview;

        // Selection
        std::vector<Entity> m_SelectedEntities;
        std::string m_SelectedAsset;
        std::string m_SelectedAssetSlice;
        std::string m_MessageTitle, m_MessageText;
        bool m_MessagePending = false;

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
