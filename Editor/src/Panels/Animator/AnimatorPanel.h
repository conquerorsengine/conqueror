#pragma once

#include "Core/Base/Base.h"
#include "Scene/Entity.h"
#include "Core/Animation/AnimationController.h"

#include <memory>
#include <string>

namespace Conqueror::Editor
{
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

    private:
        void DrawToolbar();
        void DrawParameters();
        void DrawNodeEditor();
        void DrawStateProperties();
        void DrawTransitionProperties();

        void SyncGraphFromController();
        void AddState(const char* name, float x, float y);
        void AddTransition(const char* from, const char* to);
        void RemoveState(const char* name);

        class Scene* m_Context = nullptr;
        Entity m_SelectedEntity;
        bool m_IsOpen = false;

        std::shared_ptr<AnimationController> m_Controller;
        std::string m_ControllerFilePath;

        int m_SelectedNodeID = -1;
        int m_SelectedLinkID = -1;
        std::string m_SelectedStateName;
        std::string m_SelectedTransitionFrom;
        std::string m_SelectedTransitionTo;

        int m_NextNodeID = 1;
    };
}
