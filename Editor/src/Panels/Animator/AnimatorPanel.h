#pragma once

#include "Core/Base/Base.h"
#include "Scene/Entity.h"
#include "Core/Animation/AnimationController.h"

#include <memory>
#include <string>

namespace Conqueror::Editor
{
    struct AnimatorSelectionInfo
    {
        bool HasSelection = false;
        bool IsState = false;
        bool IsTransition = false;
        std::string StateName;
        std::string TransitionFrom;
        std::string TransitionTo;
    };

    class AnimatorPanel
    {
    public:
        AnimatorPanel();
        ~AnimatorPanel();

        void SetContext(class Scene* scene);
        void SetSelectedEntity(Entity entity);
        void OnImGuiRender();
        void OpenController(const std::string& filepath);

        bool IsOpen() const { return m_IsOpen; }
        void SetOpen(bool open) { m_IsOpen = open; }

        static AnimatorSelectionInfo& GetSelectionInfo() { static AnimatorSelectionInfo s_Info; return s_Info; }
        static class AnimationController* GetActiveController() { return s_ActiveController; }

    private:
        static AnimationController* s_ActiveController;

        void DrawToolbar();
        void DrawParameters();
        void DrawLayers();
        void DrawNodeEditor();
        void DrawStateProperties();
        void DrawTransitionProperties();

        void SyncGraphFromController();
        void RebuildGraphFromSubState(int subStateNodeID);
        void RebuildGraphFromSubStateByName(const std::string& subStateName);
        void RebuildGraphFromBlendTree(const std::string& stateName);
        int GetCurrentSubStateID();
        void AddState(const char* name, float x, float y);
        void AddTransition(const char* from, const char* to);
        void RemoveState(const char* name);

        class Scene* m_Context = nullptr;
        Entity m_SelectedEntity;
        bool m_IsOpen = false;

        std::shared_ptr<AnimationController> m_Controller;
        std::string m_ControllerFilePath;

        int m_SelectedNodeID = -1;
        std::vector<int> m_SelectedNodeIDs;
        std::string m_SelectedStateName;
        std::string m_SelectedTransitionFrom;
        std::string m_SelectedTransitionTo;

        bool m_TransitionMode = false;
        int m_TransitionSourceNodeID = -1;
        bool m_Dirty = false;
        int m_ActiveTab = 0;
        int m_SelectedLayerIndex = 0;
        std::string m_ClipboardStateName;
        bool m_HasClipboard = false;
        bool m_ClipboardIsStateMachine = false;
        std::vector<AnimState> m_ClipboardStates;
        std::vector<AnimTransition> m_ClipboardTransitions;
        std::vector<AnimSubStateData> m_ClipboardSubStates;
        struct NavEntry { int NodeID; std::string Name; bool IsLayer; };
        std::vector<NavEntry> m_NavigationStack;
        bool m_InSubStateView = false;
        std::string m_CurrentSubStateName;
        bool m_BlendTreeView = false;
        std::string m_BlendTreeStateName;
    };
}
