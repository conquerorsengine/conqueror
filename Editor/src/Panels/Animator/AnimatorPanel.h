#pragma once

#include "Core/Base/Base.h"
#include "Scene/Entity.h"
#include "Core/Animation/AnimationController.h"

#include <imgui.h>
#include <glm/glm.hpp>

#include <deque>
#include <memory>
#include <string>
#include <vector>

namespace Conqueror
{
    class Scene;
    struct Model;
    struct AnimationComponent;
}

namespace Conqueror::Editor
{
    // Inspector ile paylaşılan seçim bilgisi (eski API)
    struct AnimatorSelectionInfo
    {
        bool HasSelection = false;
        bool IsState = false;
        bool IsTransition = false;
        std::string StateName;
        std::string TransitionFrom;
        std::string TransitionTo;
    };

    // Unity Animator penceresinin karşılığı: katmanlar, parametreler, durum
    // makinesi grafiği (pan/zoom, kutu seçimi, sürükle, kopyala/yapıştır, geri al),
    // sub-state machine gezinmesi, blend tree düzenleyici, canlı oynatma görünümü.
    class AnimatorPanel
    {
    public:
        AnimatorPanel();
        ~AnimatorPanel();

        void SetContext(Scene* scene);
        void SetSelectedEntity(Entity entity);
        void OnImGuiRender();
        void OpenController(const std::string& filepath);
        void NewController(const std::string& filepath);
        bool Save();

        bool IsOpen() const { return m_IsOpen; }
        void SetOpen(bool open) { m_IsOpen = open; }

        // Inspector: animator seçimi varsa özelliklerini çizer ve true döner
        static bool DrawInspectorForSelection();
        static AnimatorSelectionInfo& GetSelectionInfo() { static AnimatorSelectionInfo s_Info; return s_Info; }
        static AnimationController* GetActiveController();

        // Otomasyon / test erişimi
        AnimationController* GetController() { return m_Controller.get(); }
        void CreateStateAt(const std::string& name, const glm::vec2& position);
        void CreateTransition(const std::string& from, const std::string& to);
        void SelectNode(const std::string& name);

    private:
        // ---- Seçim
        enum class SelectionKind { None, Node, Transition, Layer };
        enum class NodeKind { State, BlendTree, SubMachine, Entry, AnyState, Exit, Up };

        struct GraphNode
        {
            NodeKind Kind = NodeKind::State;
            std::string Name;          // durum/alt makine adı ya da özel düğüm adı
            glm::vec2* Position = nullptr; // modeldeki konuma doğrudan işaretçi
            glm::vec2 Size{ 0.0f };
        };

        struct TransitionRef
        {
            AnimStateMachine* Machine = nullptr;
            int Index = -1;
        };

        // ---- Çizim bölümleri
        void DrawToolbar();
        void DrawLeftPane();
        void DrawLayersTab();
        void DrawParametersTab();
        void DrawGraph();
        void DrawBreadcrumb();
        void DrawBlendTreeView();
        void DrawPropertiesPane();
        void DrawStateProperties(AnimState& state);
        void DrawTransitionProperties(AnimTransition& transition);
        void DrawSubMachineProperties(AnimStateMachine& machine);
        void DrawBlendTreeProperties(AnimState& state);
        void DrawConditionsEditor(AnimTransition& transition);
        bool DrawClipCombo(const char* id, int& clipIndex, std::string& clipName);

        // ---- Grafik yardımcıları
        void BuildGraphNodes(std::vector<GraphNode>& nodes);
        AnimStateMachine* CurrentMachine();
        AnimLayer* CurrentLayer();
        ImVec2 ToScreen(const glm::vec2& graph) const;
        glm::vec2 ToGraph(const ImVec2& screen) const;
        GraphNode* HitNode(std::vector<GraphNode>& nodes, const ImVec2& mouse);
        bool HitTransition(const ImVec2& mouse, TransitionRef& out);
        void FrameAll();
        void DrawNode(ImDrawList* dl, const GraphNode& node, bool selected, bool hovered);
        void DrawTransitions(ImDrawList* dl, std::vector<GraphNode>& nodes);
        void DrawLiveOverlay(ImDrawList* dl, const GraphNode& node);
        void HandleGraphContextMenu(std::vector<GraphNode>& nodes);
        void HandleKeyboard();

        // ---- Düzenleme işlemleri
        void MarkDirty(const char* reason);
        void PushUndo();
        void Undo();
        void Redo();
        void DeleteSelection();
        void CopySelection();
        void Paste(const glm::vec2& at);
        void DuplicateSelection();
        void RenameNode(const std::string& oldName, const std::string& newName);
        void CreateSubMachineAt(const std::string& name, const glm::vec2& position);
        void CreateBlendTreeAt(const std::string& name, const glm::vec2& position);
        bool IsSelected(const std::string& name) const;
        void ClearSelection();
        void SyncSelectionInfo();
        void EnterSubMachine(const std::string& name);
        void NavigateUp();
        void ResetNavigation();

        // ---- Hedef entity (klipler + canlı durum)
        const Model* TargetModel() const;
        AnimationComponent* TargetComponent() const;
        bool IsLive() const;

        Scene* m_Context = nullptr;
        Entity m_SelectedEntity;
        Entity m_TargetEntity;
        bool m_LockTarget = false;
        bool m_IsOpen = false;

        std::shared_ptr<AnimationController> m_Controller;
        std::string m_ControllerFilePath;
        bool m_Dirty = false;
        bool m_AutoSave = true;
        float m_AutoSaveTimer = 0.0f;

        // Geri al / yinele (controller anlık görüntüleri)
        std::deque<AnimationController> m_UndoStack;
        std::deque<AnimationController> m_RedoStack;

        // Katman + gezinme
        int m_LayerIndex = 0;
        std::vector<std::string> m_Path;  // aktif katman içindeki alt makine yolu
        std::string m_BlendTreeState;     // boş değilse blend tree görünümü

        // Grafik görünümü
        glm::vec2 m_Pan{ 40.0f, 40.0f };
        float m_Zoom = 1.0f;
        ImVec2 m_CanvasOrigin{ 0, 0 };
        ImVec2 m_CanvasSize{ 0, 0 };
        bool m_RequestFrameAll = true;

        // Seçim
        SelectionKind m_SelectionKind = SelectionKind::None;
        std::vector<std::string> m_SelectedNodes;
        TransitionRef m_SelectedTransition;

        // Etkileşim durumu
        bool m_DraggingNodes = false;
        bool m_DragMoved = false;
        bool m_BoxSelecting = false;
        ImVec2 m_BoxStart{ 0, 0 };
        bool m_MakingTransition = false;
        std::string m_TransitionSource;
        std::string m_ContextNode;
        glm::vec2 m_ContextGraphPos{ 0.0f };
        std::string m_RenameTarget;
        char m_RenameBuffer[128] = {};
        bool m_OpenRenamePopup = false;

        // Pano
        std::vector<AnimState> m_ClipboardStates;
        std::vector<AnimTransition> m_ClipboardTransitions;
        std::vector<AnimStateMachine> m_ClipboardMachines;

        // Parametre arama
        char m_ParamFilter[64] = {};
        int m_LeftTab = 1; // 0 Layers, 1 Parameters
        float m_LeftWidth = 230.0f;
        float m_RightWidth = 300.0f;

        static AnimatorPanel* s_Instance;
    };
}
