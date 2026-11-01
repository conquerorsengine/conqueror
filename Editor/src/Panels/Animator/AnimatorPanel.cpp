#include "AnimatorPanel.h"
#include "UI/EditorUI.h"
#include "EditorState.h"

#include "Scene/Scene.h"
#include "Scene/Components.h"
#include "Core/Animation/AnimationStateMachine.h"
#include "Core/Project/Project.h"
#include "Core/Logging/Log.h"
#include "Core/Automation/Automation.h"
#include "Renderer/Utilities/Renderer3D/ModelLoader.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <nfd.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>

namespace Conqueror::Editor
{
    AnimatorPanel* AnimatorPanel::s_Instance = nullptr;

    namespace
    {
        constexpr float kNodeW = 170.0f;
        constexpr float kNodeH = 44.0f;
        constexpr float kSpecialW = 120.0f;
        constexpr size_t kMaxUndo = 100;

        const ImU32 kColState = IM_COL32(68, 71, 78, 255);
        const ImU32 kColDefault = IM_COL32(205, 120, 40, 255);
        const ImU32 kColEntry = IM_COL32(52, 150, 72, 255);
        const ImU32 kColAny = IM_COL32(48, 150, 160, 255);
        const ImU32 kColExit = IM_COL32(170, 52, 52, 255);
        const ImU32 kColUp = IM_COL32(110, 90, 160, 255);
        const ImU32 kColSub = IM_COL32(78, 84, 104, 255);
        const ImU32 kColBlend = IM_COL32(64, 78, 96, 255);
        const ImU32 kColSelected = IM_COL32(80, 160, 255, 255);
        const ImU32 kColTransition = IM_COL32(205, 208, 214, 255);
        const ImU32 kColTransitionSel = IM_COL32(80, 160, 255, 255);
        const ImU32 kColLive = IM_COL32(70, 170, 255, 255);

        float DistToSegment(const ImVec2& p, const ImVec2& a, const ImVec2& b)
        {
            ImVec2 ab(b.x - a.x, b.y - a.y);
            ImVec2 ap(p.x - a.x, p.y - a.y);
            float len2 = ab.x * ab.x + ab.y * ab.y;
            float t = len2 > 1e-6f ? std::clamp((ab.x * ap.x + ab.y * ap.y) / len2, 0.0f, 1.0f) : 0.0f;
            float dx = p.x - (a.x + ab.x * t);
            float dy = p.y - (a.y + ab.y * t);
            return std::sqrt(dx * dx + dy * dy);
        }

        // Dikdörtgen kenarı ile merkezden çıkan ışının kesişimi
        ImVec2 RectEdgePoint(const ImVec2& center, const ImVec2& half, const ImVec2& toward)
        {
            ImVec2 d(toward.x - center.x, toward.y - center.y);
            float ax = std::abs(d.x) > 1e-5f ? half.x / std::abs(d.x) : 1e9f;
            float ay = std::abs(d.y) > 1e-5f ? half.y / std::abs(d.y) : 1e9f;
            float t = std::min(ax, ay);
            if (t > 1.0f)
                t = 1.0f;
            return ImVec2(center.x + d.x * t, center.y + d.y * t);
        }

        void DrawArrowHead(ImDrawList* dl, const ImVec2& at, const ImVec2& dir, float size, ImU32 color)
        {
            float len = std::sqrt(dir.x * dir.x + dir.y * dir.y);
            if (len < 1e-4f)
                return;
            ImVec2 u(dir.x / len, dir.y / len);
            ImVec2 n(-u.y, u.x);
            ImVec2 tip(at.x + u.x * size * 0.6f, at.y + u.y * size * 0.6f);
            ImVec2 b1(at.x - u.x * size * 0.6f + n.x * size * 0.55f, at.y - u.y * size * 0.6f + n.y * size * 0.55f);
            ImVec2 b2(at.x - u.x * size * 0.6f - n.x * size * 0.55f, at.y - u.y * size * 0.6f - n.y * size * 0.55f);
            dl->AddTriangleFilled(tip, b1, b2, color);
        }

        const char* ParamTypeLabel(AnimParameterType t)
        {
            switch (t)
            {
                case AnimParameterType::Float: return "Float";
                case AnimParameterType::Int: return "Int";
                case AnimParameterType::Bool: return "Bool";
                case AnimParameterType::Trigger: return "Trigger";
            }
            return "?";
        }

        bool IsSpecialName(const std::string& n)
        {
            return n == AnimNodeNames::Entry || n == AnimNodeNames::AnyState || n == AnimNodeNames::Exit;
        }

        std::string ProjectRelative(const std::string& path)
        {
            auto dir = Project::GetActiveProjectDirectory();
            if (dir.empty())
                return path;
            std::error_code ec;
            auto rel = std::filesystem::relative(path, dir, ec);
            if (!ec && !rel.empty() && rel.native()[0] != '.')
                return rel.generic_string();
            return path;
        }
    }

    // ===================================================================== Yaşam döngüsü

    AnimatorPanel::AnimatorPanel()
    {
        s_Instance = this;
    }

    AnimatorPanel::~AnimatorPanel()
    {
        if (m_Dirty && m_Controller && !m_ControllerFilePath.empty())
            Save();
        if (s_Instance == this)
            s_Instance = nullptr;
    }

    AnimationController* AnimatorPanel::GetActiveController()
    {
        return s_Instance ? s_Instance->m_Controller.get() : nullptr;
    }

    void AnimatorPanel::SetContext(Scene* scene)
    {
        if (m_Context != scene)
        {
            m_Context = scene;
            if (!m_LockTarget)
                m_TargetEntity = {};
        }
    }

    void AnimatorPanel::SetSelectedEntity(Entity entity)
    {
        // Sahnede başka bir şey seçildi: Inspector entity'yi göstersin
        if (entity != m_SelectedEntity)
            ClearSelection();
        m_SelectedEntity = entity;
        if (m_LockTarget)
            return;
        if (entity && entity.HasComponent<AnimationComponent>())
        {
            m_TargetEntity = entity;
            auto& ac = entity.GetComponent<AnimationComponent>();
            // Entity'nin controller'ını otomatik aç
            if (ac.Controller && ac.Controller != m_Controller)
            {
                if (m_Dirty && m_Controller && !m_ControllerFilePath.empty())
                    Save();
                m_Controller = ac.Controller;
                m_ControllerFilePath = ac.ControllerFilePath;
                m_UndoStack.clear();
                m_RedoStack.clear();
                m_Dirty = false;
                ResetNavigation();
                ClearSelection();
            }
        }
    }

    void AnimatorPanel::OpenController(const std::string& filepath)
    {
        if (m_Dirty && m_Controller && !m_ControllerFilePath.empty())
            Save();

        // Aynı dosyayı kullanan entity varsa aynı nesneyi paylaş (canlı düzenleme)
        std::shared_ptr<AnimationController> controller;
        std::string rel = ProjectRelative(filepath);
        if (m_Context)
        {
            auto view = m_Context->m_Registry.view<AnimationComponent>();
            for (auto e : view)
            {
                auto& ac = view.get<AnimationComponent>(e);
                if (ac.Controller && (ac.ControllerFilePath == filepath || ac.ControllerFilePath == rel))
                {
                    controller = ac.Controller;
                    if (!m_LockTarget)
                        m_TargetEntity = Entity{ e, m_Context };
                    break;
                }
            }
        }
        if (!controller)
            controller = AnimationController::Deserialize(filepath);
        if (!controller)
        {
            CQ_CORE_ERROR("Animator: could not open {0}", filepath);
            return;
        }
        m_Controller = controller;
        m_ControllerFilePath = filepath;
        m_UndoStack.clear();
        m_RedoStack.clear();
        m_Dirty = false;
        m_IsOpen = true;
        m_LayerIndex = 0;
        ResetNavigation();
        ClearSelection();
    }

    void AnimatorPanel::NewController(const std::string& filepath)
    {
        m_Controller = AnimationController::CreateDefault();
        m_ControllerFilePath = filepath;
        m_UndoStack.clear();
        m_RedoStack.clear();
        m_LayerIndex = 0;
        ResetNavigation();
        ClearSelection();
        m_IsOpen = true;
        m_Dirty = true;
        if (!filepath.empty())
            Save();
    }

    bool AnimatorPanel::Save()
    {
        if (!m_Controller)
            return false;
        if (m_ControllerFilePath.empty())
        {
            nfdu8char_t* outPath = nullptr;
            nfdfilteritem_t filters[1] = { { "Animator Controller", "cqcont" } };
            std::string defaultDir = Project::GetActiveProjectDirectory().string();
            if (NFD_SaveDialogU8(&outPath, filters, 1, defaultDir.empty() ? nullptr : defaultDir.c_str(), "NewController.cqcont") != NFD_OKAY)
                return false;
            m_ControllerFilePath = outPath;
            NFD_FreePathU8(outPath);
        }
        bool ok = m_Controller->Serialize(m_ControllerFilePath);
        if (ok)
            m_Dirty = false;
        return ok;
    }

    // ===================================================================== Hedef

    const Model* AnimatorPanel::TargetModel() const
    {
        auto pick = [](Entity e) -> const Model*
        {
            if (e && e.HasComponent<ModelComponent>())
            {
                auto& mc = e.GetComponent<ModelComponent>();
                if (mc.ModelData && !mc.ModelData->Animations.empty())
                    return mc.ModelData.get();
            }
            return nullptr;
        };
        if (m_TargetEntity && m_Context && m_TargetEntity.GetScene() == m_Context)
            if (const Model* m = pick(m_TargetEntity))
                return m;
        // Bu controller'ı kullanan herhangi bir entity
        if (m_Context && m_Controller)
        {
            auto view = m_Context->m_Registry.view<AnimationComponent, ModelComponent>();
            for (auto e : view)
                if (view.get<AnimationComponent>(e).Controller == m_Controller)
                    if (const Model* m = pick(Entity{ e, m_Context }))
                        return m;
        }
        return nullptr;
    }

    AnimationComponent* AnimatorPanel::TargetComponent() const
    {
        if (!m_Context || !m_Controller)
            return nullptr;
        Entity target = m_TargetEntity;
        if (target && target.GetScene() == m_Context && target.HasComponent<AnimationComponent>())
        {
            auto& ac = target.GetComponent<AnimationComponent>();
            if (ac.Controller == m_Controller)
                return &ac;
        }
        // Play modunda sahne klonlanır: aynı controller'ı kullanan ilk entity
        auto view = m_Context->m_Registry.view<AnimationComponent>();
        for (auto e : view)
        {
            auto& ac = view.get<AnimationComponent>(e);
            if (ac.Controller == m_Controller)
                return &ac;
        }
        return nullptr;
    }

    bool AnimatorPanel::IsLive() const
    {
        auto* ac = TargetComponent();
        return ac && !ac->LayerRuntime.empty() && !EditorState::Get().IsEditing();
    }

    // ===================================================================== Gezinme

    AnimLayer* AnimatorPanel::CurrentLayer()
    {
        if (!m_Controller || m_Controller->Layers.empty())
            return nullptr;
        m_LayerIndex = std::clamp(m_LayerIndex, 0, (int)m_Controller->Layers.size() - 1);
        return &m_Controller->Layers[(size_t)m_LayerIndex];
    }

    AnimStateMachine* AnimatorPanel::CurrentMachine()
    {
        AnimLayer* layer = CurrentLayer();
        if (!layer)
            return nullptr;
        AnimStateMachine* machine = layer;
        for (size_t i = 0; i < m_Path.size(); ++i)
        {
            AnimStateMachine* next = nullptr;
            for (auto& sub : machine->SubStates)
                if (sub.Name == m_Path[i])
                    next = &sub;
            if (!next)
            {
                m_Path.resize(i); // yol artık geçersiz (silindi/yeniden adlandırıldı)
                break;
            }
            machine = next;
        }
        return machine;
    }

    void AnimatorPanel::EnterSubMachine(const std::string& name)
    {
        m_Path.push_back(name);
        m_BlendTreeState.clear();
        ClearSelection();
        m_RequestFrameAll = true;
    }

    void AnimatorPanel::NavigateUp()
    {
        if (!m_BlendTreeState.empty())
            m_BlendTreeState.clear();
        else if (!m_Path.empty())
            m_Path.pop_back();
        ClearSelection();
        m_RequestFrameAll = true;
    }

    void AnimatorPanel::ResetNavigation()
    {
        m_Path.clear();
        m_BlendTreeState.clear();
        m_RequestFrameAll = true;
    }

    // ===================================================================== Seçim

    bool AnimatorPanel::IsSelected(const std::string& name) const
    {
        return std::find(m_SelectedNodes.begin(), m_SelectedNodes.end(), name) != m_SelectedNodes.end();
    }

    void AnimatorPanel::ClearSelection()
    {
        m_SelectionKind = SelectionKind::None;
        m_SelectedNodes.clear();
        m_SelectedTransition = {};
        SyncSelectionInfo();
    }

    void AnimatorPanel::SelectNode(const std::string& name)
    {
        m_SelectionKind = SelectionKind::Node;
        m_SelectedNodes = { name };
        m_SelectedTransition = {};
        SyncSelectionInfo();
    }

    void AnimatorPanel::SyncSelectionInfo()
    {
        auto& info = GetSelectionInfo();
        info = AnimatorSelectionInfo{};
        if (m_SelectionKind == SelectionKind::Node && m_SelectedNodes.size() == 1 && !IsSpecialName(m_SelectedNodes[0]))
        {
            info.HasSelection = true;
            info.IsState = true;
            info.StateName = m_SelectedNodes[0];
        }
        else if (m_SelectionKind == SelectionKind::Transition && m_SelectedTransition.Machine &&
                 m_SelectedTransition.Index >= 0 && m_SelectedTransition.Index < (int)m_SelectedTransition.Machine->Transitions.size())
        {
            const auto& t = m_SelectedTransition.Machine->Transitions[(size_t)m_SelectedTransition.Index];
            info.HasSelection = true;
            info.IsTransition = true;
            info.TransitionFrom = t.FromState;
            info.TransitionTo = t.ToState;
        }
    }

    // ===================================================================== Düzenleme

    void AnimatorPanel::PushUndo()
    {
        if (!m_Controller)
            return;
        m_UndoStack.push_back(*m_Controller);
        if (m_UndoStack.size() > kMaxUndo)
            m_UndoStack.pop_front();
        m_RedoStack.clear();
    }

    void AnimatorPanel::MarkDirty(const char*)
    {
        m_Dirty = true;
        m_AutoSaveTimer = 0.0f;
    }

    void AnimatorPanel::Undo()
    {
        if (!m_Controller || m_UndoStack.empty())
            return;
        m_RedoStack.push_back(*m_Controller);
        *m_Controller = m_UndoStack.back();
        m_UndoStack.pop_back();
        ClearSelection();
        CurrentMachine();
        MarkDirty("undo");
    }

    void AnimatorPanel::Redo()
    {
        if (!m_Controller || m_RedoStack.empty())
            return;
        m_UndoStack.push_back(*m_Controller);
        *m_Controller = m_RedoStack.back();
        m_RedoStack.pop_back();
        ClearSelection();
        CurrentMachine();
        MarkDirty("redo");
    }

    void AnimatorPanel::CreateStateAt(const std::string& name, const glm::vec2& position)
    {
        AnimStateMachine* machine = CurrentMachine();
        AnimLayer* layer = CurrentLayer();
        if (!machine || !layer)
            return;
        PushUndo();
        AnimState state;
        state.Name = AnimationController::MakeUniqueName(*layer, name);
        state.EditorPosition = position;
        machine->States.push_back(state);
        if (machine->DefaultState.empty())
            machine->DefaultState = state.Name;
        SelectNode(state.Name);
        MarkDirty("create state");
    }

    void AnimatorPanel::CreateBlendTreeAt(const std::string& name, const glm::vec2& position)
    {
        AnimStateMachine* machine = CurrentMachine();
        AnimLayer* layer = CurrentLayer();
        if (!machine || !layer)
            return;
        PushUndo();
        AnimState state;
        state.Name = AnimationController::MakeUniqueName(*layer, name);
        state.Type = AnimStateType::BlendTree;
        state.EditorPosition = position;
        // Kullanılabilir ilk float parametreyi bağla, yoksa "Blend" oluştur
        for (const auto& p : m_Controller->Parameters)
            if (p.Type == AnimParameterType::Float) { state.BlendParameter = p.Name; break; }
        if (state.BlendParameter.empty())
        {
            AnimParameter p;
            p.Name = "Blend";
            p.Type = AnimParameterType::Float;
            m_Controller->Parameters.push_back(p);
            state.BlendParameter = p.Name;
        }
        machine->States.push_back(state);
        if (machine->DefaultState.empty())
            machine->DefaultState = state.Name;
        SelectNode(state.Name);
        MarkDirty("create blend tree");
    }

    void AnimatorPanel::CreateSubMachineAt(const std::string& name, const glm::vec2& position)
    {
        AnimStateMachine* machine = CurrentMachine();
        AnimLayer* layer = CurrentLayer();
        if (!machine || !layer)
            return;
        PushUndo();
        AnimStateMachine sub;
        sub.Name = AnimationController::MakeUniqueName(*layer, name);
        sub.EditorPosition = position;
        machine->SubStates.push_back(sub);
        SelectNode(sub.Name);
        MarkDirty("create sub machine");
    }

    void AnimatorPanel::CreateTransition(const std::string& from, const std::string& to)
    {
        AnimStateMachine* machine = CurrentMachine();
        if (!machine || from.empty() || to.empty())
            return;
        if (from == AnimNodeNames::Exit || to == AnimNodeNames::Entry || to == AnimNodeNames::AnyState)
            return;
        if (from == AnimNodeNames::Entry && to == AnimNodeNames::Exit)
            return;
        PushUndo();
        AnimTransition t;
        t.FromState = from;
        t.ToState = to;
        // Unity varsayılanları: durumdan geçiş exit time'lı; Any State / Entry koşul bekler
        t.HasExitTime = !(from == AnimNodeNames::AnyState || from == AnimNodeNames::Entry);
        t.ExitTime = 0.75f;
        t.Duration = (from == AnimNodeNames::Entry) ? 0.0f : 0.25f;
        machine->Transitions.push_back(t);
        m_SelectionKind = SelectionKind::Transition;
        m_SelectedNodes.clear();
        m_SelectedTransition = { machine, (int)machine->Transitions.size() - 1 };
        SyncSelectionInfo();
        MarkDirty("create transition");
    }

    void AnimatorPanel::RenameNode(const std::string& oldName, const std::string& newName)
    {
        AnimLayer* layer = CurrentLayer();
        if (!layer || newName.empty() || oldName == newName || IsSpecialName(oldName))
            return;
        if (AnimationController::IsNameUsed(*layer, newName))
        {
            CQ_CORE_WARN("Animator: name '{0}' is already used in this layer", newName);
            return;
        }
        PushUndo();
        AnimationController::RenameNode(*layer, oldName, newName);
        for (auto& p : m_Path)
            if (p == oldName)
                p = newName;
        for (auto& s : m_SelectedNodes)
            if (s == oldName)
                s = newName;
        if (m_BlendTreeState == oldName)
            m_BlendTreeState = newName;
        SyncSelectionInfo();
        MarkDirty("rename");
    }

    void AnimatorPanel::DeleteSelection()
    {
        AnimStateMachine* machine = CurrentMachine();
        if (!machine)
            return;

        if (m_SelectionKind == SelectionKind::Transition)
        {
            auto ref = m_SelectedTransition;
            if (ref.Machine && ref.Index >= 0 && ref.Index < (int)ref.Machine->Transitions.size())
            {
                PushUndo();
                ref.Machine->Transitions.erase(ref.Machine->Transitions.begin() + ref.Index);
                MarkDirty("delete transition");
            }
            ClearSelection();
            return;
        }

        std::vector<std::string> toDelete;
        for (const auto& n : m_SelectedNodes)
            if (!IsSpecialName(n))
                toDelete.push_back(n);
        if (toDelete.empty())
            return;

        PushUndo();
        auto isDeleted = [&](const std::string& n) { return std::find(toDelete.begin(), toDelete.end(), n) != toDelete.end(); };
        machine->States.erase(std::remove_if(machine->States.begin(), machine->States.end(),
            [&](const AnimState& s) { return isDeleted(s.Name); }), machine->States.end());
        machine->SubStates.erase(std::remove_if(machine->SubStates.begin(), machine->SubStates.end(),
            [&](const AnimStateMachine& m) { return isDeleted(m.Name); }), machine->SubStates.end());
        // Silinen düğümlere giden/gelen geçişler (tüm katmanda)
        std::function<void(AnimStateMachine&)> scrub = [&](AnimStateMachine& m)
        {
            m.Transitions.erase(std::remove_if(m.Transitions.begin(), m.Transitions.end(),
                [&](const AnimTransition& t) { return isDeleted(t.FromState) || isDeleted(t.ToState); }), m.Transitions.end());
            if (isDeleted(m.DefaultState))
                m.DefaultState = m.States.empty() ? std::string() : m.States.front().Name;
            for (auto& sub : m.SubStates)
                scrub(sub);
        };
        if (AnimLayer* layer = CurrentLayer())
            scrub(*layer);
        if (std::find(toDelete.begin(), toDelete.end(), m_BlendTreeState) != toDelete.end())
            m_BlendTreeState.clear();
        ClearSelection();
        MarkDirty("delete nodes");
    }

    void AnimatorPanel::CopySelection()
    {
        AnimStateMachine* machine = CurrentMachine();
        if (!machine || m_SelectionKind != SelectionKind::Node)
            return;
        m_ClipboardStates.clear();
        m_ClipboardTransitions.clear();
        m_ClipboardMachines.clear();
        for (const auto& s : machine->States)
            if (IsSelected(s.Name))
                m_ClipboardStates.push_back(s);
        for (const auto& m : machine->SubStates)
            if (IsSelected(m.Name))
                m_ClipboardMachines.push_back(m);
        // Yalnızca seçili düğümler arasındaki geçişler
        for (const auto& t : machine->Transitions)
            if (IsSelected(t.FromState) && IsSelected(t.ToState))
                m_ClipboardTransitions.push_back(t);
    }

    void AnimatorPanel::Paste(const glm::vec2& at)
    {
        AnimStateMachine* machine = CurrentMachine();
        AnimLayer* layer = CurrentLayer();
        if (!machine || !layer || (m_ClipboardStates.empty() && m_ClipboardMachines.empty()))
            return;
        PushUndo();

        // Kopyaların merkezi farenin olduğu yere gelsin
        glm::vec2 center(0.0f);
        int count = 0;
        for (const auto& s : m_ClipboardStates) { center += s.EditorPosition; ++count; }
        for (const auto& m : m_ClipboardMachines) { center += m.EditorPosition; ++count; }
        center /= (float)std::max(count, 1);
        glm::vec2 offset = at - center;

        std::vector<std::pair<std::string, std::string>> renames;
        std::vector<std::string> newSelection;
        for (auto s : m_ClipboardStates)
        {
            std::string newName = AnimationController::MakeUniqueName(*layer, s.Name);
            renames.push_back({ s.Name, newName });
            s.Name = newName;
            s.EditorPosition += offset;
            machine->States.push_back(s);
            newSelection.push_back(newName);
        }
        for (auto m : m_ClipboardMachines)
        {
            std::string newName = AnimationController::MakeUniqueName(*layer, m.Name);
            renames.push_back({ m.Name, newName });
            m.Name = newName;
            m.EditorPosition += offset;
            machine->SubStates.push_back(m);
            newSelection.push_back(newName);
        }
        auto mapName = [&](const std::string& n)
        {
            for (auto& [a, b] : renames)
                if (a == n)
                    return b;
            return n;
        };
        for (auto t : m_ClipboardTransitions)
        {
            t.FromState = mapName(t.FromState);
            t.ToState = mapName(t.ToState);
            machine->Transitions.push_back(t);
        }
        m_SelectionKind = SelectionKind::Node;
        m_SelectedNodes = newSelection;
        SyncSelectionInfo();
        MarkDirty("paste");
    }

    void AnimatorPanel::DuplicateSelection()
    {
        CopySelection();
        glm::vec2 center(0.0f);
        int n = 0;
        for (const auto& s : m_ClipboardStates) { center += s.EditorPosition; ++n; }
        for (const auto& m : m_ClipboardMachines) { center += m.EditorPosition; ++n; }
        if (n > 0)
            Paste(center / (float)n + glm::vec2(30.0f, 30.0f));
    }

    // ===================================================================== Inspector köprüsü

    bool AnimatorPanel::DrawInspectorForSelection()
    {
        if (!s_Instance || !s_Instance->m_Controller || !s_Instance->m_IsOpen)
            return false;
        auto& self = *s_Instance;
        if (self.m_SelectionKind == SelectionKind::None)
            return false;
        if (self.m_SelectionKind == SelectionKind::Node && (self.m_SelectedNodes.size() != 1 || IsSpecialName(self.m_SelectedNodes[0])))
            return false;
        ImGui::PushID("AnimatorInspector");
        UI::Badge("ANIMATOR", UI::Colors::Accent);
        ImGui::SameLine();
        ImGui::TextDisabled("%s", std::filesystem::path(self.m_ControllerFilePath).filename().string().c_str());
        ImGui::Separator();
        self.DrawPropertiesPane();
        ImGui::PopID();
        return true;
    }

    // ===================================================================== Ana pencere

    void AnimatorPanel::OnImGuiRender()
    {
        if (!m_IsOpen)
            return;

        // Otomatik kaydetme (son değişiklikten 2 sn sonra)
        if (m_Dirty && m_AutoSave && !m_ControllerFilePath.empty())
        {
            m_AutoSaveTimer += ImGui::GetIO().DeltaTime;
            if (m_AutoSaveTimer > 2.0f && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
                Save();
        }

        std::string title = std::string("Animator") + (m_Dirty ? " *" : "") + "###Animator";
        ImGui::SetNextWindowSize(ImVec2(1100, 650), ImGuiCond_FirstUseEver);
        bool open = true;
        if (!ImGui::Begin(title.c_str(), &open, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
        {
            ImGui::End();
            if (!open) m_IsOpen = false;
            return;
        }
        if (!open)
            m_IsOpen = false;

        DrawToolbar();

        if (!m_Controller)
        {
            ImGui::Spacing();
            UI::InfoBox("No Animator Controller is open. Select an entity with an Animation component, double-click a .cqcont file in the Content Browser, or create a new controller.", UI::InfoType::Info);
            if (UI::AccentButton("Create New Controller..."))
            {
                nfdu8char_t* outPath = nullptr;
                nfdfilteritem_t filters[1] = { { "Animator Controller", "cqcont" } };
                std::string defaultDir = Project::GetActiveProjectDirectory().string();
                if (NFD_SaveDialogU8(&outPath, filters, 1, defaultDir.empty() ? nullptr : defaultDir.c_str(), "NewController.cqcont") == NFD_OKAY)
                {
                    NewController(outPath);
                    NFD_FreePathU8(outPath);
                }
            }
            ImGui::End();
            return;
        }

        // Üç bölmeli düzen: sol (katman/parametre) | grafik | sağ (özellikler)
        float avail = ImGui::GetContentRegionAvail().x;
        float leftW = std::clamp(m_LeftWidth, 160.0f, avail * 0.4f);
        float rightW = std::clamp(m_RightWidth, 220.0f, avail * 0.45f);
        float graphW = std::max(100.0f, avail - leftW - rightW - 12.0f);

        ImGui::BeginChild("##animLeft", ImVec2(leftW, 0), ImGuiChildFlags_Borders);
        DrawLeftPane();
        ImGui::EndChild();

        ImGui::SameLine(0, 0);
        ImGui::InvisibleButton("##splitL", ImVec2(6.0f, ImGui::GetContentRegionAvail().y));
        if (ImGui::IsItemActive())
            m_LeftWidth = std::clamp(m_LeftWidth + ImGui::GetIO().MouseDelta.x, 160.0f, 500.0f);
        if (ImGui::IsItemHovered() || ImGui::IsItemActive())
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        ImGui::SameLine(0, 0);

        ImGui::BeginChild("##animGraph", ImVec2(graphW, 0), ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        DrawBreadcrumb();
        if (!m_BlendTreeState.empty())
            DrawBlendTreeView();
        else
            DrawGraph();
        ImGui::EndChild();

        ImGui::SameLine(0, 0);
        ImGui::InvisibleButton("##splitR", ImVec2(6.0f, ImGui::GetContentRegionAvail().y));
        if (ImGui::IsItemActive())
            m_RightWidth = std::clamp(m_RightWidth - ImGui::GetIO().MouseDelta.x, 220.0f, 600.0f);
        if (ImGui::IsItemHovered() || ImGui::IsItemActive())
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        ImGui::SameLine(0, 0);

        ImGui::BeginChild("##animRight", ImVec2(0, 0), ImGuiChildFlags_Borders);
        DrawPropertiesPane();
        ImGui::EndChild();

        HandleKeyboard();
        ImGui::End();
    }

    void AnimatorPanel::DrawToolbar()
    {
        if (ImGui::Button("New"))
        {
            nfdu8char_t* outPath = nullptr;
            nfdfilteritem_t filters[1] = { { "Animator Controller", "cqcont" } };
            std::string defaultDir = Project::GetActiveProjectDirectory().string();
            if (NFD_SaveDialogU8(&outPath, filters, 1, defaultDir.empty() ? nullptr : defaultDir.c_str(), "NewController.cqcont") == NFD_OKAY)
            {
                NewController(outPath);
                NFD_FreePathU8(outPath);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Open"))
        {
            nfdu8char_t* outPath = nullptr;
            nfdfilteritem_t filters[1] = { { "Animator Controller", "cqcont" } };
            std::string defaultDir = Project::GetActiveProjectDirectory().string();
            if (NFD_OpenDialogU8(&outPath, filters, 1, defaultDir.empty() ? nullptr : defaultDir.c_str()) == NFD_OKAY)
            {
                OpenController(outPath);
                NFD_FreePathU8(outPath);
            }
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(!m_Controller);
        if (ImGui::Button(m_Dirty ? "Save *" : "Save"))
            Save();
        ImGui::SameLine();
        ImGui::BeginDisabled(m_UndoStack.empty());
        if (ImGui::Button("Undo"))
            Undo();
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(m_RedoStack.empty());
        if (ImGui::Button("Redo"))
            Redo();
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Frame"))
            m_RequestFrameAll = true;
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::Checkbox("Auto Save", &m_AutoSave);
        ImGui::SameLine();
        ImGui::Checkbox("Lock", &m_LockTarget);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
            ImGui::SetTooltip("Keep showing the current target even when another object is selected.");

        // Hedef bilgisi
        ImGui::SameLine();
        if (m_TargetEntity && m_Context && m_TargetEntity.GetScene() == m_Context && m_TargetEntity.HasComponent<TagComponent>())
        {
            UI::Badge(IsLive() ? "LIVE" : "TARGET", IsLive() ? UI::Colors::Success : UI::Colors::Accent);
            ImGui::SameLine();
            ImGui::TextUnformatted(m_TargetEntity.GetComponent<TagComponent>().Tag.c_str());
        }
        if (m_Controller && !m_ControllerFilePath.empty())
        {
            ImGui::SameLine();
            ImGui::TextDisabled("  %s", ProjectRelative(m_ControllerFilePath).c_str());
        }
        ImGui::Separator();
    }

    void AnimatorPanel::DrawLeftPane()
    {
        if (ImGui::BeginTabBar("##animLeftTabs"))
        {
            if (ImGui::BeginTabItem("Layers", nullptr, m_LeftTab == 0 ? ImGuiTabItemFlags_SetSelected : 0))
            {
                DrawLayersTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Parameters", nullptr, m_LeftTab == 1 ? ImGuiTabItemFlags_SetSelected : 0))
            {
                DrawParametersTab();
                ImGui::EndTabItem();
            }
            m_LeftTab = -1;
            ImGui::EndTabBar();
        }
    }

    void AnimatorPanel::DrawLayersTab()
    {
        auto& layers = m_Controller->Layers;
        if (ImGui::Button("+ Layer", ImVec2(-FLT_MIN, 0)))
        {
            PushUndo();
            AnimLayer layer;
            layer.Name = "New Layer";
            int n = 1;
            auto exists = [&](const std::string& name) { for (auto& l : layers) if (l.Name == name) return true; return false; };
            while (exists(layer.Name)) layer.Name = "New Layer " + std::to_string(n++);
            layer.Weight = 1.0f;
            layers.push_back(layer);
            m_LayerIndex = (int)layers.size() - 1;
            ResetNavigation();
            ClearSelection();
            MarkDirty("add layer");
        }
        ImGui::Spacing();

        int moveFrom = -1, moveTo = -1, removeIndex = -1;
        for (int i = 0; i < (int)layers.size(); ++i)
        {
            auto& layer = layers[(size_t)i];
            ImGui::PushID(i);
            bool selected = (i == m_LayerIndex);
            ImVec2 start = ImGui::GetCursorScreenPos();
            float w = ImGui::GetContentRegionAvail().x;
            float h = ImGui::GetFrameHeightWithSpacing() * 2.2f;
            ImGui::GetWindowDrawList()->AddRectFilled(start, ImVec2(start.x + w, start.y + h),
                selected ? IM_COL32(45, 70, 105, 255) : IM_COL32(38, 40, 45, 255), 4.0f);
            if (ImGui::InvisibleButton("##layerSel", ImVec2(w, h)))
            {
                if (m_LayerIndex != i)
                {
                    m_LayerIndex = i;
                    ResetNavigation();
                    ClearSelection();
                }
            }
            // Sürükle-bırak ile sıralama
            if (ImGui::BeginDragDropSource())
            {
                ImGui::SetDragDropPayload("ANIM_LAYER", &i, sizeof(int));
                ImGui::Text("%s", layer.Name.c_str());
                ImGui::EndDragDropSource();
            }
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("ANIM_LAYER"))
                {
                    moveFrom = *(const int*)p->Data;
                    moveTo = i;
                }
                ImGui::EndDragDropTarget();
            }
            if (ImGui::BeginPopupContextItem("##layerctx"))
            {
                if (ImGui::MenuItem("Delete", nullptr, false, layers.size() > 1))
                    removeIndex = i;
                if (ImGui::MenuItem("Move Up", nullptr, false, i > 0)) { moveFrom = i; moveTo = i - 1; }
                if (ImGui::MenuItem("Move Down", nullptr, false, i + 1 < (int)layers.size())) { moveFrom = i; moveTo = i + 1; }
                ImGui::EndPopup();
            }

            ImGui::SetCursorScreenPos(ImVec2(start.x + 6, start.y + 4));
            char nameBuf[128];
            snprintf(nameBuf, sizeof(nameBuf), "%s", layer.Name.c_str());
            ImGui::SetNextItemWidth(w - 80.0f);
            if (ImGui::InputText("##lname", nameBuf, sizeof(nameBuf), ImGuiInputTextFlags_EnterReturnsTrue) && nameBuf[0])
            {
                PushUndo();
                layer.Name = nameBuf;
                MarkDirty("rename layer");
            }
            ImGui::SameLine();
            bool mute = layer.Mute, solo = layer.Solo;
            if (UI::ToggleButton("M", mute)) { PushUndo(); layer.Mute = mute; MarkDirty("mute"); }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Mute");
            ImGui::SameLine(0, 2);
            if (UI::ToggleButton("S", solo)) { PushUndo(); layer.Solo = solo; MarkDirty("solo"); }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) ImGui::SetTooltip("Solo");

            ImGui::SetCursorScreenPos(ImVec2(start.x + 6, start.y + ImGui::GetFrameHeightWithSpacing() + 4));
            ImGui::BeginDisabled(i == 0);
            ImGui::SetNextItemWidth((w - 18.0f) * 0.55f);
            float weight = layer.Weight;
            if (ImGui::SliderFloat("##w", &weight, 0.0f, 1.0f, "W %.2f"))
            {
                if (ImGui::IsItemActivated()) PushUndo();
                layer.Weight = weight;
                MarkDirty("weight");
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-FLT_MIN);
            int blending = (int)layer.Blending;
            const char* blendModes[] = { "Override", "Additive" };
            if (ImGui::Combo("##blend", &blending, blendModes, 2))
            {
                PushUndo();
                layer.Blending = (AnimLayerBlending)blending;
                MarkDirty("blending");
            }
            ImGui::EndDisabled();

            // Canlı: katmanın aktif durumu
            if (IsLive())
            {
                auto* ac = TargetComponent();
                if (ac && i < (int)ac->LayerRuntime.size())
                {
                    ImGui::SetCursorScreenPos(ImVec2(start.x + w - 8.0f - 60.0f, start.y + h - 6.0f));
                    float frac = std::fmod(std::max(ac->LayerRuntime[(size_t)i].CurrentNormalized, 0.0f), 1.0f);
                    ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(start.x + 4, start.y + h - 4), ImVec2(start.x + 4 + (w - 8) * frac, start.y + h - 2), kColLive);
                }
            }

            ImGui::SetCursorScreenPos(ImVec2(start.x, start.y + h + 4));
            ImGui::PopID();
        }

        if (moveFrom >= 0 && moveTo >= 0 && moveFrom != moveTo && moveFrom > 0 && moveTo > 0)
        {
            PushUndo();
            AnimLayer moved = layers[(size_t)moveFrom];
            layers.erase(layers.begin() + moveFrom);
            layers.insert(layers.begin() + moveTo, moved);
            m_LayerIndex = moveTo;
            MarkDirty("reorder layers");
        }
        else if (moveFrom == 0 || moveTo == 0)
        {
            if (moveFrom >= 0 && moveTo >= 0 && moveFrom != moveTo)
                CQ_CORE_WARN("Animator: the base layer must stay first");
        }
        if (removeIndex >= 0 && layers.size() > 1)
        {
            PushUndo();
            layers.erase(layers.begin() + removeIndex);
            m_LayerIndex = std::clamp(m_LayerIndex, 0, (int)layers.size() - 1);
            ResetNavigation();
            ClearSelection();
            MarkDirty("delete layer");
        }
    }

    void AnimatorPanel::DrawParametersTab()
    {
        auto& params = m_Controller->Parameters;
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 32.0f);
        ImGui::InputTextWithHint("##pfilter", "Search", m_ParamFilter, sizeof(m_ParamFilter));
        ImGui::SameLine();
        if (ImGui::Button("+", ImVec2(28, 0)))
            ImGui::OpenPopup("##addparam");
        if (ImGui::BeginPopup("##addparam"))
        {
            const AnimParameterType types[] = { AnimParameterType::Float, AnimParameterType::Int, AnimParameterType::Bool, AnimParameterType::Trigger };
            for (auto type : types)
            {
                if (ImGui::MenuItem(ParamTypeLabel(type)))
                {
                    PushUndo();
                    AnimParameter p;
                    p.Type = type;
                    std::string base = std::string("New ") + ParamTypeLabel(type);
                    p.Name = base;
                    int n = 1;
                    while (m_Controller->FindParameter(p.Name)) p.Name = base + " " + std::to_string(n++);
                    params.push_back(p);
                    MarkDirty("add parameter");
                }
            }
            ImGui::EndPopup();
        }
        ImGui::Spacing();

        AnimationComponent* live = IsLive() ? TargetComponent() : nullptr;
        if (live)
            UI::InfoBox("Play mode: values below drive the running animator.", UI::InfoType::Success);

        int removeIndex = -1, moveFrom = -1, moveTo = -1;
        for (int i = 0; i < (int)params.size(); ++i)
        {
            auto& p = params[(size_t)i];
            if (m_ParamFilter[0])
            {
                std::string a = p.Name, f = m_ParamFilter;
                std::transform(a.begin(), a.end(), a.begin(), ::tolower);
                std::transform(f.begin(), f.end(), f.begin(), ::tolower);
                if (a.find(f) == std::string::npos)
                    continue;
            }
            ImGui::PushID(i);
            // Sürükleme tutamacı
            ImGui::Button("::", ImVec2(18, 0));
            if (ImGui::BeginDragDropSource())
            {
                ImGui::SetDragDropPayload("ANIM_PARAM", &i, sizeof(int));
                ImGui::Text("%s", p.Name.c_str());
                ImGui::EndDragDropSource();
            }
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("ANIM_PARAM"))
                {
                    moveFrom = *(const int*)pl->Data;
                    moveTo = i;
                }
                ImGui::EndDragDropTarget();
            }
            ImGui::SameLine();
            char nameBuf[128];
            snprintf(nameBuf, sizeof(nameBuf), "%s", p.Name.c_str());
            float valueW = 62.0f;
            ImGui::SetNextItemWidth(std::max(40.0f, ImGui::GetContentRegionAvail().x - valueW - 28.0f));
            if (ImGui::InputText("##pname", nameBuf, sizeof(nameBuf), ImGuiInputTextFlags_EnterReturnsTrue) && nameBuf[0] && p.Name != nameBuf)
            {
                if (m_Controller->FindParameter(nameBuf))
                    CQ_CORE_WARN("Animator: parameter '{0}' already exists", nameBuf);
                else
                {
                    PushUndo();
                    std::string old = p.Name;
                    m_Controller->RenameParameter(old, nameBuf);
                    if (auto* ac = TargetComponent())
                        for (auto& rv : ac->Parameters)
                            if (rv.Name == old) rv.Name = nameBuf;
                    MarkDirty("rename parameter");
                }
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                ImGui::SetTooltip("%s (%s)", p.Name.c_str(), ParamTypeLabel(p.Type));
            ImGui::SameLine();
            ImGui::SetNextItemWidth(valueW);

            // Değer: canlıyken çalışma zamanı değeri, değilse varsayılan
            AnimParameterRuntimeValue* rv = nullptr;
            if (live)
                for (auto& v : live->Parameters)
                    if (v.Name == p.Name && v.Type == p.Type) rv = &v;
            switch (p.Type)
            {
                case AnimParameterType::Float:
                {
                    float v = rv ? rv->FloatValue : p.DefaultValue;
                    if (ImGui::DragFloat("##v", &v, 0.01f, 0.0f, 0.0f, "%.2f"))
                    {
                        if (rv) rv->FloatValue = v;
                        else { if (ImGui::IsItemActivated()) PushUndo(); p.DefaultValue = v; MarkDirty("param default"); }
                    }
                    break;
                }
                case AnimParameterType::Int:
                {
                    int v = rv ? rv->IntValue : (int)std::lround(p.DefaultValue);
                    if (ImGui::DragInt("##v", &v, 0.2f))
                    {
                        if (rv) rv->IntValue = v;
                        else { PushUndo(); p.DefaultValue = (float)v; MarkDirty("param default"); }
                    }
                    break;
                }
                case AnimParameterType::Bool:
                {
                    bool v = rv ? rv->BoolValue : p.DefaultValue > 0.5f;
                    if (ImGui::Checkbox("##v", &v))
                    {
                        if (rv) rv->BoolValue = v;
                        else { PushUndo(); p.DefaultValue = v ? 1.0f : 0.0f; MarkDirty("param default"); }
                    }
                    break;
                }
                case AnimParameterType::Trigger:
                {
                    bool set = rv && rv->FloatValue > 0.5f;
                    ImGui::PushStyleColor(ImGuiCol_Button, set ? ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive) : ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
                    if (ImGui::Button(set ? "SET" : "Fire", ImVec2(valueW, 0)) && rv)
                        rv->FloatValue = 1.0f;
                    ImGui::PopStyleColor();
                    break;
                }
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("x"))
                removeIndex = i;
            ImGui::PopID();
        }

        if (moveFrom >= 0 && moveTo >= 0 && moveFrom != moveTo)
        {
            PushUndo();
            AnimParameter moved = params[(size_t)moveFrom];
            params.erase(params.begin() + moveFrom);
            params.insert(params.begin() + moveTo, moved);
            MarkDirty("reorder parameters");
        }
        if (removeIndex >= 0)
        {
            PushUndo();
            std::string name = params[(size_t)removeIndex].Name;
            params.erase(params.begin() + removeIndex);
            // Kullanılan koşulları da temizle
            std::function<void(AnimStateMachine&)> scrub = [&](AnimStateMachine& m)
            {
                for (auto& t : m.Transitions)
                    t.Conditions.erase(std::remove_if(t.Conditions.begin(), t.Conditions.end(),
                        [&](const AnimCondition& c) { return c.ParameterName == name; }), t.Conditions.end());
                for (auto& sub : m.SubStates) scrub(sub);
            };
            for (auto& layer : m_Controller->Layers)
                scrub(layer);
            MarkDirty("remove parameter");
        }
        if (params.empty())
            ImGui::TextDisabled("No parameters. Use + to add Float, Int, Bool or Trigger.");
    }

    // ===================================================================== Grafik

    ImVec2 AnimatorPanel::ToScreen(const glm::vec2& g) const
    {
        return ImVec2(m_CanvasOrigin.x + (g.x + m_Pan.x) * m_Zoom, m_CanvasOrigin.y + (g.y + m_Pan.y) * m_Zoom);
    }

    glm::vec2 AnimatorPanel::ToGraph(const ImVec2& s) const
    {
        return glm::vec2((s.x - m_CanvasOrigin.x) / m_Zoom - m_Pan.x, (s.y - m_CanvasOrigin.y) / m_Zoom - m_Pan.y);
    }

    void AnimatorPanel::BuildGraphNodes(std::vector<GraphNode>& nodes)
    {
        nodes.clear();
        AnimStateMachine* machine = CurrentMachine();
        AnimLayer* layer = CurrentLayer();
        if (!machine || !layer)
            return;
        nodes.push_back({ NodeKind::Entry, AnimNodeNames::Entry, &machine->EntryPosition, { kSpecialW, kNodeH * 0.8f } });
        nodes.push_back({ NodeKind::AnyState, AnimNodeNames::AnyState, &machine->AnyStatePosition, { kSpecialW, kNodeH * 0.8f } });
        nodes.push_back({ NodeKind::Exit, AnimNodeNames::Exit, &machine->ExitPosition, { kSpecialW, kNodeH * 0.8f } });
        if (!m_Path.empty())
            nodes.push_back({ NodeKind::Up, std::string("(Up) ") + (m_Path.size() > 1 ? m_Path[m_Path.size() - 2] : layer->Name), &machine->ParentPosition, { kNodeW, kNodeH } });
        for (auto& sub : machine->SubStates)
            nodes.push_back({ NodeKind::SubMachine, sub.Name, &sub.EditorPosition, { kNodeW, kNodeH } });
        for (auto& s : machine->States)
            nodes.push_back({ s.Type == AnimStateType::BlendTree ? NodeKind::BlendTree : NodeKind::State, s.Name, &s.EditorPosition, { kNodeW, kNodeH } });
    }

    AnimatorPanel::GraphNode* AnimatorPanel::HitNode(std::vector<GraphNode>& nodes, const ImVec2& mouse)
    {
        // Üstte çizilen (sonraki) düğüm önce
        for (auto it = nodes.rbegin(); it != nodes.rend(); ++it)
        {
            ImVec2 a = ToScreen(*it->Position);
            ImVec2 b(a.x + it->Size.x * m_Zoom, a.y + it->Size.y * m_Zoom);
            if (mouse.x >= a.x && mouse.x <= b.x && mouse.y >= a.y && mouse.y <= b.y)
                return &*it;
        }
        return nullptr;
    }

    void AnimatorPanel::FrameAll()
    {
        std::vector<GraphNode> nodes;
        BuildGraphNodes(nodes);
        if (nodes.empty() || m_CanvasSize.x < 10.0f)
            return;
        glm::vec2 mn(1e9f), mx(-1e9f);
        for (auto& n : nodes)
        {
            mn = glm::min(mn, *n.Position);
            mx = glm::max(mx, *n.Position + n.Size);
        }
        glm::vec2 size = mx - mn + glm::vec2(80.0f);
        m_Zoom = std::clamp(std::min(m_CanvasSize.x / size.x, m_CanvasSize.y / size.y), 0.3f, 1.4f);
        glm::vec2 center = (mn + mx) * 0.5f;
        m_Pan = glm::vec2(m_CanvasSize.x * 0.5f / m_Zoom, m_CanvasSize.y * 0.5f / m_Zoom) - center;
    }

    void AnimatorPanel::DrawBreadcrumb()
    {
        AnimLayer* layer = CurrentLayer();
        if (!layer)
            return;
        CurrentMachine(); // geçersiz yolları kırp
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 2));
        if (ImGui::SmallButton(layer->Name.c_str()))
        {
            m_Path.clear();
            m_BlendTreeState.clear();
            ClearSelection();
            m_RequestFrameAll = true;
        }
        for (size_t i = 0; i < m_Path.size(); ++i)
        {
            ImGui::SameLine(0, 2);
            ImGui::TextDisabled(">");
            ImGui::SameLine(0, 2);
            ImGui::PushID((int)i);
            if (ImGui::SmallButton(m_Path[i].c_str()))
            {
                m_Path.resize(i + 1);
                m_BlendTreeState.clear();
                ClearSelection();
                m_RequestFrameAll = true;
            }
            ImGui::PopID();
        }
        if (!m_BlendTreeState.empty())
        {
            ImGui::SameLine(0, 2);
            ImGui::TextDisabled(">");
            ImGui::SameLine(0, 2);
            UI::Badge(("Blend Tree: " + m_BlendTreeState).c_str(), UI::Colors::Accent);
        }
        ImGui::PopStyleVar();
    }

    void AnimatorPanel::DrawNode(ImDrawList* dl, const GraphNode& node, bool selected, bool hovered)
    {
        ImVec2 a = ToScreen(*node.Position);
        ImVec2 b(a.x + node.Size.x * m_Zoom, a.y + node.Size.y * m_Zoom);
        float rounding = 6.0f * m_Zoom;

        AnimStateMachine* machine = CurrentMachine();
        bool isDefault = machine && (node.Kind == NodeKind::State || node.Kind == NodeKind::BlendTree || node.Kind == NodeKind::SubMachine)
                         && machine->DefaultState == node.Name;

        ImU32 fill = kColState;
        switch (node.Kind)
        {
            case NodeKind::Entry: fill = kColEntry; break;
            case NodeKind::AnyState: fill = kColAny; break;
            case NodeKind::Exit: fill = kColExit; break;
            case NodeKind::Up: fill = kColUp; break;
            case NodeKind::SubMachine: fill = kColSub; break;
            case NodeKind::BlendTree: fill = kColBlend; break;
            default: break;
        }
        if (isDefault)
            fill = kColDefault;

        // Gölge
        dl->AddRectFilled(ImVec2(a.x + 3, a.y + 4), ImVec2(b.x + 3, b.y + 4), IM_COL32(0, 0, 0, 90), rounding);
        if (node.Kind == NodeKind::SubMachine)
        {
            // Alt makine: üst üste iki kart görünümü
            dl->AddRectFilled(ImVec2(a.x + 6 * m_Zoom, a.y - 5 * m_Zoom), ImVec2(b.x + 6 * m_Zoom, b.y - 5 * m_Zoom), IM_COL32(60, 64, 80, 255), rounding);
        }
        dl->AddRectFilled(a, b, fill, rounding);
        if (hovered)
            dl->AddRectFilled(a, b, IM_COL32(255, 255, 255, 18), rounding);
        dl->AddRect(a, b, selected ? kColSelected : IM_COL32(20, 20, 24, 255), rounding, 0, selected ? 2.5f : 1.0f);

        // Metin
        ImFont* font = ImGui::GetFont();
        float fontSize = ImGui::GetFontSize() * std::clamp(m_Zoom, 0.6f, 1.3f);
        std::string label = node.Name;
        if (node.Kind == NodeKind::BlendTree)
            label = "◆ " + label;
        ImVec2 ts = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, label.c_str());
        float maxW = (b.x - a.x) - 10.0f;
        if (ts.x > maxW)
        {
            // Sığmayan adı kısalt
            while (label.size() > 3 && font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, (label + "...").c_str()).x > maxW)
                label.pop_back();
            label += "...";
            ts = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, label.c_str());
        }
        dl->AddText(font, fontSize, ImVec2((a.x + b.x - ts.x) * 0.5f, (a.y + b.y - ts.y) * 0.5f), IM_COL32(240, 242, 246, 255), label.c_str());

        if (m_Zoom > 0.55f && (node.Kind == NodeKind::State || node.Kind == NodeKind::BlendTree))
        {
            // Klip bilgisi küçük yazı
            if (AnimLayer* layer = CurrentLayer())
                if (const AnimState* s = AnimationController::FindState(*layer, node.Name))
                {
                    std::string sub = s->Type == AnimStateType::BlendTree ? (std::to_string(s->BlendChildren.size()) + " motions")
                                                                          : (s->ClipName.empty() ? std::string("(no motion)") : s->ClipName);
                    float small = fontSize * 0.75f;
                    ImVec2 ss = font->CalcTextSizeA(small, FLT_MAX, 0.0f, sub.c_str());
                    if (ss.x < maxW)
                        dl->AddText(font, small, ImVec2((a.x + b.x - ss.x) * 0.5f, b.y - ss.y - 2.0f), IM_COL32(190, 194, 205, 200), sub.c_str());
                }
        }
        DrawLiveOverlay(dl, node);
    }

    void AnimatorPanel::DrawLiveOverlay(ImDrawList* dl, const GraphNode& node)
    {
        if (!IsLive())
            return;
        auto* ac = TargetComponent();
        if (!ac || m_LayerIndex >= (int)ac->LayerRuntime.size())
            return;
        const auto& rt = ac->LayerRuntime[(size_t)m_LayerIndex];
        ImVec2 a = ToScreen(*node.Position);
        ImVec2 b(a.x + node.Size.x * m_Zoom, a.y + node.Size.y * m_Zoom);

        // Bulunulan alt makine düğümü: içindeki bir durum aktifse vurgula
        bool active = rt.CurrentState == node.Name;
        bool next = rt.InTransition && rt.NextState == node.Name;
        if (node.Kind == NodeKind::SubMachine)
        {
            if (AnimStateMachine* machine = CurrentMachine())
                for (auto& sub : machine->SubStates)
                    if (sub.Name == node.Name)
                    {
                        active = AnimationController::FindState(sub, rt.CurrentState) != nullptr;
                        next = rt.InTransition && AnimationController::FindState(sub, rt.NextState) != nullptr;
                    }
        }
        if (!active && !next)
            return;
        float barH = 4.0f * std::max(m_Zoom, 0.7f);
        float frac = active ? rt.CurrentNormalized : rt.NextNormalized;
        frac = frac - std::floor(frac);
        ImU32 color = active ? kColLive : IM_COL32(70, 170, 255, 130);
        dl->AddRect(ImVec2(a.x - 2, a.y - 2), ImVec2(b.x + 2, b.y + 2), color, 7.0f * m_Zoom, 0, 2.0f);
        dl->AddRectFilled(ImVec2(a.x + 4, b.y - barH - 3), ImVec2(b.x - 4, b.y - 3), IM_COL32(0, 0, 0, 120), 2.0f);
        dl->AddRectFilled(ImVec2(a.x + 4, b.y - barH - 3), ImVec2(a.x + 4 + (b.x - a.x - 8) * frac, b.y - 3), color, 2.0f);
    }

    void AnimatorPanel::DrawTransitions(ImDrawList* dl, std::vector<GraphNode>& nodes)
    {
        AnimStateMachine* machine = CurrentMachine();
        if (!machine)
            return;
        auto findNode = [&](const std::string& name) -> GraphNode*
        {
            for (auto& n : nodes)
                if (n.Name == name)
                    return &n;
            return nullptr;
        };

        // Aynı yönde birden çok geçiş: tek ok + "≡" işareti
        struct Edge { std::string From, To; int Count = 0; int FirstIndex = -1; bool Selected = false; bool Muted = true; };
        std::vector<Edge> edges;
        for (int i = 0; i < (int)machine->Transitions.size(); ++i)
        {
            const auto& t = machine->Transitions[(size_t)i];
            Edge* e = nullptr;
            for (auto& existing : edges)
                if (existing.From == t.FromState && existing.To == t.ToState)
                    e = &existing;
            if (!e)
            {
                edges.push_back({ t.FromState, t.ToState, 0, i, false, true });
                e = &edges.back();
            }
            e->Count++;
            e->Muted = e->Muted && t.Mute;
            if (m_SelectionKind == SelectionKind::Transition && m_SelectedTransition.Machine == machine && m_SelectedTransition.Index == i)
                e->Selected = true;
        }

        auto liveTransition = [&](const Edge& e) -> bool
        {
            if (!IsLive())
                return false;
            auto* ac = TargetComponent();
            if (!ac || m_LayerIndex >= (int)ac->LayerRuntime.size())
                return false;
            const auto& rt = ac->LayerRuntime[(size_t)m_LayerIndex];
            return rt.InTransition && rt.ActiveTransition.FromState == e.From && rt.ActiveTransition.ToState == e.To;
        };

        for (const auto& e : edges)
        {
            GraphNode* from = findNode(e.From);
            GraphNode* to = findNode(e.To);
            if (!from || !to)
                continue;
            ImVec2 fa = ToScreen(*from->Position), ta = ToScreen(*to->Position);
            ImVec2 fc(fa.x + from->Size.x * m_Zoom * 0.5f, fa.y + from->Size.y * m_Zoom * 0.5f);
            ImVec2 tc(ta.x + to->Size.x * m_Zoom * 0.5f, ta.y + to->Size.y * m_Zoom * 0.5f);
            ImU32 color = e.Selected ? kColTransitionSel : (e.Muted ? IM_COL32(120, 120, 120, 160) : kColTransition);
            if (liveTransition(e))
                color = kColLive;
            float thickness = (e.Selected ? 3.0f : 2.0f) * std::max(m_Zoom, 0.6f);

            if (from == to)
            {
                // Kendine geçiş: düğümün üstünde küçük döngü
                ImVec2 c(fc.x + from->Size.x * m_Zoom * 0.35f, fa.y);
                dl->AddCircle(c, 12.0f * m_Zoom, color, 20, thickness);
                DrawArrowHead(dl, ImVec2(c.x + 12.0f * m_Zoom, c.y), ImVec2(0, 1), 10.0f * m_Zoom, color);
                continue;
            }

            // Çift yönlü geçişler üst üste binmesin: dik ofset
            ImVec2 d(tc.x - fc.x, tc.y - fc.y);
            float len = std::sqrt(d.x * d.x + d.y * d.y);
            if (len < 1.0f)
                continue;
            ImVec2 nrm(-d.y / len, d.x / len);
            float off = 7.0f * m_Zoom;
            ImVec2 fcO(fc.x + nrm.x * off, fc.y + nrm.y * off);
            ImVec2 tcO(tc.x + nrm.x * off, tc.y + nrm.y * off);
            ImVec2 p0 = RectEdgePoint(fcO, ImVec2(from->Size.x * m_Zoom * 0.5f, from->Size.y * m_Zoom * 0.5f), tcO);
            ImVec2 p1 = RectEdgePoint(tcO, ImVec2(to->Size.x * m_Zoom * 0.5f, to->Size.y * m_Zoom * 0.5f), fcO);
            dl->AddLine(p0, p1, color, thickness);
            ImVec2 mid((p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f);
            float arrow = 11.0f * std::max(m_Zoom, 0.6f);
            if (e.Count > 1)
            {
                DrawArrowHead(dl, ImVec2(mid.x - d.x / len * arrow, mid.y - d.y / len * arrow), d, arrow, color);
                DrawArrowHead(dl, mid, d, arrow, color);
                DrawArrowHead(dl, ImVec2(mid.x + d.x / len * arrow, mid.y + d.y / len * arrow), d, arrow, color);
            }
            else
            {
                DrawArrowHead(dl, mid, d, arrow, color);
            }
        }
    }

    bool AnimatorPanel::HitTransition(const ImVec2& mouse, TransitionRef& out)
    {
        AnimStateMachine* machine = CurrentMachine();
        if (!machine)
            return false;
        std::vector<GraphNode> nodes;
        BuildGraphNodes(nodes);
        float best = 8.0f;
        bool found = false;
        for (int i = 0; i < (int)machine->Transitions.size(); ++i)
        {
            const auto& t = machine->Transitions[(size_t)i];
            GraphNode *from = nullptr, *to = nullptr;
            for (auto& n : nodes)
            {
                if (n.Name == t.FromState) from = &n;
                if (n.Name == t.ToState) to = &n;
            }
            if (!from || !to || from == to)
                continue;
            ImVec2 fa = ToScreen(*from->Position), ta = ToScreen(*to->Position);
            ImVec2 fc(fa.x + from->Size.x * m_Zoom * 0.5f, fa.y + from->Size.y * m_Zoom * 0.5f);
            ImVec2 tc(ta.x + to->Size.x * m_Zoom * 0.5f, ta.y + to->Size.y * m_Zoom * 0.5f);
            ImVec2 d(tc.x - fc.x, tc.y - fc.y);
            float len = std::sqrt(d.x * d.x + d.y * d.y);
            if (len < 1.0f)
                continue;
            ImVec2 nrm(-d.y / len, d.x / len);
            float off = 7.0f * m_Zoom;
            float dist = DistToSegment(mouse, ImVec2(fc.x + nrm.x * off, fc.y + nrm.y * off), ImVec2(tc.x + nrm.x * off, tc.y + nrm.y * off));
            if (dist < best)
            {
                best = dist;
                out = { machine, i };
                found = true;
            }
        }
        return found;
    }

    void AnimatorPanel::DrawGraph()
    {
        AnimStateMachine* machine = CurrentMachine();
        if (!machine)
            return;

        m_CanvasOrigin = ImGui::GetCursorScreenPos();
        m_CanvasSize = ImGui::GetContentRegionAvail();
        if (m_CanvasSize.x < 50.0f || m_CanvasSize.y < 50.0f)
            return;
        if (m_RequestFrameAll)
        {
            FrameAll();
            m_RequestFrameAll = false;
        }

        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 canvasEnd(m_CanvasOrigin.x + m_CanvasSize.x, m_CanvasOrigin.y + m_CanvasSize.y);
        dl->AddRectFilled(m_CanvasOrigin, canvasEnd, IM_COL32(30, 31, 35, 255));

        // Izgara (küçük + büyük)
        float minor = 20.0f * m_Zoom;
        if (minor > 6.0f)
        {
            float ox = std::fmod(m_Pan.x * m_Zoom, minor);
            float oy = std::fmod(m_Pan.y * m_Zoom, minor);
            int idx = 0;
            float startIndexX = std::floor(-m_Pan.x * m_Zoom / minor);
            for (float x = ox; x < m_CanvasSize.x; x += minor, ++idx)
            {
                bool major = ((int)(startIndexX + idx + 1) % 5) == 0;
                dl->AddLine(ImVec2(m_CanvasOrigin.x + x, m_CanvasOrigin.y), ImVec2(m_CanvasOrigin.x + x, canvasEnd.y), major ? IM_COL32(50, 52, 58, 255) : IM_COL32(39, 40, 45, 255));
            }
            idx = 0;
            float startIndexY = std::floor(-m_Pan.y * m_Zoom / minor);
            for (float y = oy; y < m_CanvasSize.y; y += minor, ++idx)
            {
                bool major = ((int)(startIndexY + idx + 1) % 5) == 0;
                dl->AddLine(ImVec2(m_CanvasOrigin.x, m_CanvasOrigin.y + y), ImVec2(canvasEnd.x, m_CanvasOrigin.y + y), major ? IM_COL32(50, 52, 58, 255) : IM_COL32(39, 40, 45, 255));
            }
        }

        ImGui::InvisibleButton("##graphcanvas", m_CanvasSize, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
        const bool canvasHovered = ImGui::IsItemHovered();
        const bool canvasActive = ImGui::IsItemActive();
        ImGuiIO& io = ImGui::GetIO();
        ImVec2 mouse = io.MousePos;

        dl->PushClipRect(m_CanvasOrigin, canvasEnd, true);

        std::vector<GraphNode> nodes;
        BuildGraphNodes(nodes);
        GraphNode* hovered = canvasHovered ? HitNode(nodes, mouse) : nullptr;

        // ---- Pan / zoom
        if (canvasHovered && io.MouseWheel != 0.0f)
        {
            glm::vec2 before = ToGraph(mouse);
            m_Zoom = std::clamp(m_Zoom * (io.MouseWheel > 0 ? 1.12f : 1.0f / 1.12f), 0.25f, 2.5f);
            glm::vec2 after = ToGraph(mouse);
            m_Pan += after - before;
        }
        if (canvasActive && (ImGui::IsMouseDragging(ImGuiMouseButton_Middle) || (io.KeyAlt && ImGui::IsMouseDragging(ImGuiMouseButton_Left))))
            m_Pan += glm::vec2(io.MouseDelta.x, io.MouseDelta.y) / m_Zoom;

        // ---- Sol tık
        if (canvasHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !io.KeyAlt)
        {
            if (m_MakingTransition)
            {
                if (hovered && hovered->Kind != NodeKind::Up)
                    CreateTransition(m_TransitionSource, hovered->Name);
                m_MakingTransition = false;
            }
            else if (hovered)
            {
                if (io.KeyCtrl || io.KeyShift)
                {
                    m_SelectionKind = SelectionKind::Node;
                    m_SelectedTransition = {};
                    auto it = std::find(m_SelectedNodes.begin(), m_SelectedNodes.end(), hovered->Name);
                    if (it != m_SelectedNodes.end()) m_SelectedNodes.erase(it);
                    else m_SelectedNodes.push_back(hovered->Name);
                    SyncSelectionInfo();
                }
                else
                {
                    if (!IsSelected(hovered->Name))
                        SelectNode(hovered->Name);
                    m_DraggingNodes = true;
                    m_DragMoved = false;
                    PushUndo();
                }
            }
            else
            {
                TransitionRef hit;
                if (HitTransition(mouse, hit))
                {
                    m_SelectionKind = SelectionKind::Transition;
                    m_SelectedNodes.clear();
                    m_SelectedTransition = hit;
                    SyncSelectionInfo();
                }
                else
                {
                    if (!io.KeyCtrl)
                        ClearSelection();
                    m_BoxSelecting = true;
                    m_BoxStart = mouse;
                }
            }
        }

        // Çift tık: alt makineye gir / blend tree aç / yukarı çık
        if (canvasHovered && hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
        {
            m_DraggingNodes = false;
            if (hovered->Kind == NodeKind::SubMachine)
                EnterSubMachine(hovered->Name);
            else if (hovered->Kind == NodeKind::BlendTree)
                m_BlendTreeState = hovered->Name;
            else if (hovered->Kind == NodeKind::Up)
                NavigateUp();
            dl->PopClipRect();
            return;
        }

        // Düğüm sürükleme (grid'e 10 birim yaslanır)
        if (m_DraggingNodes)
        {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
            {
                if (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f)
                {
                    for (auto& n : nodes)
                        if (IsSelected(n.Name))
                            *n.Position += glm::vec2(io.MouseDelta.x, io.MouseDelta.y) / m_Zoom;
                    m_DragMoved = true;
                    MarkDirty("move");
                }
            }
            else
            {
                if (m_DragMoved)
                {
                    for (auto& n : nodes)
                        if (IsSelected(n.Name))
                            *n.Position = glm::round(*n.Position / 10.0f) * 10.0f;
                }
                else if (!m_UndoStack.empty())
                {
                    m_UndoStack.pop_back(); // tıklama: hareket yok, geri alma kaydı gereksiz
                }
                m_DraggingNodes = false;
            }
        }

        // Kutu seçimi
        if (m_BoxSelecting)
        {
            ImVec2 a(std::min(m_BoxStart.x, mouse.x), std::min(m_BoxStart.y, mouse.y));
            ImVec2 b(std::max(m_BoxStart.x, mouse.x), std::max(m_BoxStart.y, mouse.y));
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
            {
                dl->AddRectFilled(a, b, IM_COL32(80, 160, 255, 40));
                dl->AddRect(a, b, IM_COL32(80, 160, 255, 160));
            }
            else
            {
                if (std::abs(b.x - a.x) > 4.0f || std::abs(b.y - a.y) > 4.0f)
                {
                    if (!io.KeyCtrl)
                        m_SelectedNodes.clear();
                    for (auto& n : nodes)
                    {
                        ImVec2 na = ToScreen(*n.Position);
                        ImVec2 nb(na.x + n.Size.x * m_Zoom, na.y + n.Size.y * m_Zoom);
                        if (nb.x >= a.x && na.x <= b.x && nb.y >= a.y && na.y <= b.y && !IsSelected(n.Name))
                            m_SelectedNodes.push_back(n.Name);
                    }
                    m_SelectionKind = m_SelectedNodes.empty() ? SelectionKind::None : SelectionKind::Node;
                    m_SelectedTransition = {};
                    SyncSelectionInfo();
                }
                m_BoxSelecting = false;
            }
        }

        // ---- Çizim
        DrawTransitions(dl, nodes);
        if (m_MakingTransition)
        {
            for (auto& n : nodes)
                if (n.Name == m_TransitionSource)
                {
                    ImVec2 a = ToScreen(*n.Position);
                    ImVec2 c(a.x + n.Size.x * m_Zoom * 0.5f, a.y + n.Size.y * m_Zoom * 0.5f);
                    dl->AddLine(c, mouse, kColSelected, 2.0f);
                    DrawArrowHead(dl, mouse, ImVec2(mouse.x - c.x, mouse.y - c.y), 12.0f, kColSelected);
                }
            if (ImGui::IsKeyPressed(ImGuiKey_Escape) || ImGui::IsMouseClicked(ImGuiMouseButton_Right))
                m_MakingTransition = false;
        }
        for (auto& n : nodes)
            DrawNode(dl, n, IsSelected(n.Name), hovered == &n);

        // Boş grafik ipucu
        if (machine->States.empty() && machine->SubStates.empty())
        {
            const char* hint = "Right-click to create a state, or drag an animation clip here.";
            ImVec2 ts = ImGui::CalcTextSize(hint);
            dl->AddText(ImVec2(m_CanvasOrigin.x + (m_CanvasSize.x - ts.x) * 0.5f, m_CanvasOrigin.y + m_CanvasSize.y - ts.y - 12.0f), IM_COL32(150, 150, 160, 200), hint);
        }

        // Zoom göstergesi
        char zoomText[32];
        snprintf(zoomText, sizeof(zoomText), "%d%%", (int)std::round(m_Zoom * 100.0f));
        dl->AddText(ImVec2(canvasEnd.x - 50.0f, canvasEnd.y - 22.0f), IM_COL32(150, 150, 160, 180), zoomText);

        dl->PopClipRect();

        // ---- Bağlam menüsü
        if (canvasHovered && ImGui::IsMouseReleased(ImGuiMouseButton_Right) && !m_MakingTransition && ImGui::GetMouseDragDelta(ImGuiMouseButton_Right).x == 0.0f)
        {
            m_ContextNode = hovered ? hovered->Name : std::string();
            m_ContextGraphPos = ToGraph(mouse);
            if (hovered && !IsSelected(hovered->Name))
                SelectNode(hovered->Name);
            TransitionRef hit;
            if (!hovered && HitTransition(mouse, hit))
            {
                m_SelectionKind = SelectionKind::Transition;
                m_SelectedTransition = hit;
                m_SelectedNodes.clear();
                SyncSelectionInfo();
                ImGui::OpenPopup("##transitionctx");
            }
            else
            {
                ImGui::OpenPopup("##graphctx");
            }
        }
        HandleGraphContextMenu(nodes);

        // Klip sürükle-bırak (Content Browser'dan model/klip)
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ANIM_CLIP"))
            {
                int clipIndex = *(const int*)payload->Data;
                const Model* model = TargetModel();
                if (model && clipIndex >= 0 && clipIndex < (int)model->Animations.size() && model->Animations[(size_t)clipIndex])
                {
                    CreateStateAt(model->Animations[(size_t)clipIndex]->Name, ToGraph(mouse));
                    if (AnimLayer* layer = CurrentLayer())
                        if (AnimState* s = AnimationController::FindState(*layer, m_SelectedNodes.empty() ? "" : m_SelectedNodes[0]))
                        {
                            s->ClipIndex = clipIndex;
                            s->ClipName = model->Animations[(size_t)clipIndex]->Name;
                        }
                }
            }
            ImGui::EndDragDropTarget();
        }

        // Yeniden adlandırma açılır penceresi
        if (m_OpenRenamePopup)
        {
            ImGui::OpenPopup("Rename##animnode");
            m_OpenRenamePopup = false;
        }
        if (ImGui::BeginPopupModal("Rename##animnode", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            if (ImGui::IsWindowAppearing())
                ImGui::SetKeyboardFocusHere();
            bool apply = ImGui::InputText("##renamebox", m_RenameBuffer, sizeof(m_RenameBuffer), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
            if (ImGui::Button("OK", ImVec2(90, 0)) || apply)
            {
                RenameNode(m_RenameTarget, m_RenameBuffer);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(90, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }

    void AnimatorPanel::HandleGraphContextMenu(std::vector<GraphNode>& nodes)
    {
        AnimStateMachine* machine = CurrentMachine();
        if (!machine)
            return;

        if (ImGui::BeginPopup("##graphctx"))
        {
            GraphNode* node = nullptr;
            for (auto& n : nodes)
                if (n.Name == m_ContextNode)
                    node = &n;

            if (node)
            {
                bool canTransitionFrom = node->Kind != NodeKind::Exit && node->Kind != NodeKind::Up;
                if (ImGui::MenuItem("Make Transition", nullptr, false, canTransitionFrom))
                {
                    m_MakingTransition = true;
                    m_TransitionSource = node->Name;
                }
                bool isStateLike = node->Kind == NodeKind::State || node->Kind == NodeKind::BlendTree || node->Kind == NodeKind::SubMachine;
                if (ImGui::MenuItem("Set as Layer Default State", nullptr, machine->DefaultState == node->Name, isStateLike))
                {
                    PushUndo();
                    machine->DefaultState = node->Name;
                    MarkDirty("default state");
                }
                if (node->Kind == NodeKind::SubMachine && ImGui::MenuItem("Open Sub-State Machine"))
                    EnterSubMachine(node->Name);
                if (node->Kind == NodeKind::BlendTree && ImGui::MenuItem("Edit Blend Tree"))
                    m_BlendTreeState = node->Name;
                if (node->Kind == NodeKind::Up && ImGui::MenuItem("Go Up"))
                    NavigateUp();
                ImGui::Separator();
                if (ImGui::MenuItem("Rename", "F2", false, isStateLike))
                {
                    m_RenameTarget = node->Name;
                    snprintf(m_RenameBuffer, sizeof(m_RenameBuffer), "%s", node->Name.c_str());
                    m_OpenRenamePopup = true;
                }
                if (ImGui::MenuItem("Copy", "Ctrl+C", false, isStateLike))
                    CopySelection();
                if (ImGui::MenuItem("Duplicate", "Ctrl+D", false, isStateLike))
                    DuplicateSelection();
                if (ImGui::MenuItem("Delete", "Del", false, isStateLike))
                    DeleteSelection();
            }
            else
            {
                if (ImGui::BeginMenu("Create State"))
                {
                    if (ImGui::MenuItem("Empty"))
                        CreateStateAt("New State", m_ContextGraphPos);
                    if (ImGui::BeginMenu("From Animation Clip"))
                    {
                        const Model* model = TargetModel();
                        if (!model)
                            ImGui::TextDisabled("No model with animations uses this controller");
                        else
                        {
                            for (int i = 0; i < (int)model->Animations.size(); ++i)
                            {
                                if (!model->Animations[(size_t)i])
                                    continue;
                                if (ImGui::MenuItem(model->Animations[(size_t)i]->Name.c_str()))
                                {
                                    CreateStateAt(model->Animations[(size_t)i]->Name, m_ContextGraphPos);
                                    if (AnimLayer* layer = CurrentLayer())
                                        if (AnimState* s = AnimationController::FindState(*layer, m_SelectedNodes.empty() ? "" : m_SelectedNodes[0]))
                                        {
                                            s->ClipIndex = i;
                                            s->ClipName = model->Animations[(size_t)i]->Name;
                                        }
                                }
                            }
                        }
                        ImGui::EndMenu();
                    }
                    if (ImGui::MenuItem("From New Blend Tree"))
                        CreateBlendTreeAt("Blend Tree", m_ContextGraphPos);
                    ImGui::EndMenu();
                }
                if (ImGui::MenuItem("Create Sub-State Machine"))
                    CreateSubMachineAt("New StateMachine", m_ContextGraphPos);
                ImGui::Separator();
                if (ImGui::MenuItem("Paste", "Ctrl+V", false, !m_ClipboardStates.empty() || !m_ClipboardMachines.empty()))
                    Paste(m_ContextGraphPos);
                if (ImGui::MenuItem("Frame All", "F"))
                    m_RequestFrameAll = true;
                if (!m_Path.empty() && ImGui::MenuItem("Go Up"))
                    NavigateUp();
            }
            ImGui::EndPopup();
        }

        if (ImGui::BeginPopup("##transitionctx"))
        {
            if (ImGui::MenuItem("Delete", "Del"))
                DeleteSelection();
            if (m_SelectedTransition.Machine && m_SelectedTransition.Index >= 0 &&
                m_SelectedTransition.Index < (int)m_SelectedTransition.Machine->Transitions.size())
            {
                auto& t = m_SelectedTransition.Machine->Transitions[(size_t)m_SelectedTransition.Index];
                bool mute = t.Mute, solo = t.Solo;
                if (ImGui::MenuItem("Mute", nullptr, &mute)) { PushUndo(); t.Mute = mute; MarkDirty("mute transition"); }
                if (ImGui::MenuItem("Solo", nullptr, &solo)) { PushUndo(); t.Solo = solo; MarkDirty("solo transition"); }
            }
            ImGui::EndPopup();
        }
    }

    void AnimatorPanel::HandleKeyboard()
    {
        if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) || ImGui::GetIO().WantTextInput)
            return;
        auto& io = ImGui::GetIO();
        if (ImGui::IsKeyPressed(ImGuiKey_Delete) || ImGui::IsKeyPressed(ImGuiKey_Backspace))
            DeleteSelection();
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z, false))
        {
            if (io.KeyShift) Redo(); else Undo();
        }
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y, false))
            Redo();
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C, false))
            CopySelection();
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V, false))
            Paste(ToGraph(io.MousePos));
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D, false))
            DuplicateSelection();
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false))
            Save();
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A, false))
        {
            std::vector<GraphNode> nodes;
            BuildGraphNodes(nodes);
            m_SelectedNodes.clear();
            for (auto& n : nodes)
                if (n.Kind == NodeKind::State || n.Kind == NodeKind::BlendTree || n.Kind == NodeKind::SubMachine)
                    m_SelectedNodes.push_back(n.Name);
            m_SelectionKind = m_SelectedNodes.empty() ? SelectionKind::None : SelectionKind::Node;
            SyncSelectionInfo();
        }
        if (!io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F, false))
            m_RequestFrameAll = true;
        if (ImGui::IsKeyPressed(ImGuiKey_F2, false) && m_SelectionKind == SelectionKind::Node && m_SelectedNodes.size() == 1 && !IsSpecialName(m_SelectedNodes[0]))
        {
            m_RenameTarget = m_SelectedNodes[0];
            snprintf(m_RenameBuffer, sizeof(m_RenameBuffer), "%s", m_RenameTarget.c_str());
            m_OpenRenamePopup = true;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false))
        {
            if (m_MakingTransition) m_MakingTransition = false;
            else if (!m_BlendTreeState.empty() || !m_Path.empty()) NavigateUp();
            else ClearSelection();
        }
    }

    // ===================================================================== Özellikler

    bool AnimatorPanel::DrawClipCombo(const char* id, int& clipIndex, std::string& clipName)
    {
        const Model* model = TargetModel();
        bool changed = false;
        std::string preview = clipName.empty() ? std::string("None (Motion)") : clipName;
        if (model && clipIndex >= 0 && clipIndex < (int)model->Animations.size() && model->Animations[(size_t)clipIndex]
            && model->Animations[(size_t)clipIndex]->Name != clipName && !clipName.empty())
            preview += "  (missing)";
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::BeginCombo(id, preview.c_str()))
        {
            if (!model)
                ImGui::TextDisabled("Assign this controller to an entity with an animated model to pick clips.");
            else
            {
                for (int i = 0; i < (int)model->Animations.size(); ++i)
                {
                    const auto& clip = model->Animations[(size_t)i];
                    if (!clip)
                        continue;
                    char label[256];
                    snprintf(label, sizeof(label), "%s  (%.2fs)##clip%d", clip->Name.c_str(), clip->DurationSeconds(), i);
                    if (ImGui::Selectable(label, clipIndex == i))
                    {
                        PushUndo();
                        clipIndex = i;
                        clipName = clip->Name;
                        changed = true;
                        MarkDirty("clip");
                    }
                }
            }
            ImGui::EndCombo();
        }
        // Klip adı kaydırılmışsa (model yeniden içe aktarıldı) adı esas al
        if (model && !clipName.empty())
        {
            bool indexValid = clipIndex >= 0 && clipIndex < (int)model->Animations.size() && model->Animations[(size_t)clipIndex]
                              && model->Animations[(size_t)clipIndex]->Name == clipName;
            if (!indexValid)
                for (int i = 0; i < (int)model->Animations.size(); ++i)
                    if (model->Animations[(size_t)i] && model->Animations[(size_t)i]->Name == clipName)
                    {
                        clipIndex = i;
                        MarkDirty("clip index fix");
                        break;
                    }
        }
        return changed;
    }

    void AnimatorPanel::DrawPropertiesPane()
    {
        AnimLayer* layer = CurrentLayer();
        AnimStateMachine* machine = CurrentMachine();
        if (!layer || !machine)
            return;

        if (m_SelectionKind == SelectionKind::Transition && m_SelectedTransition.Machine &&
            m_SelectedTransition.Index >= 0 && m_SelectedTransition.Index < (int)m_SelectedTransition.Machine->Transitions.size())
        {
            DrawTransitionProperties(m_SelectedTransition.Machine->Transitions[(size_t)m_SelectedTransition.Index]);
            return;
        }

        if (m_SelectionKind == SelectionKind::Node && m_SelectedNodes.size() == 1)
        {
            const std::string& name = m_SelectedNodes[0];
            if (AnimState* state = AnimationController::FindState(*layer, name))
            {
                DrawStateProperties(*state);
                return;
            }
            if (AnimStateMachine* sub = AnimationController::FindSubMachine(*layer, name))
            {
                DrawSubMachineProperties(*sub);
                return;
            }
            if (name == AnimNodeNames::Entry || name == AnimNodeNames::AnyState || name == AnimNodeNames::Exit)
            {
                ImGui::TextUnformatted(name.c_str());
                ImGui::Separator();
                if (name == AnimNodeNames::Entry)
                    UI::InfoBox("Entry: the machine starts in the default (orange) state. Transitions from Entry with conditions can pick a different start state.", UI::InfoType::Info);
                else if (name == AnimNodeNames::AnyState)
                    UI::InfoBox("Any State: transitions from here are checked every frame regardless of the current state (e.g. Hit, Die).", UI::InfoType::Info);
                else
                    UI::InfoBox("Exit: leaves this sub-state machine (or restarts the layer from Entry at the top level).", UI::InfoType::Info);
                return;
            }
        }

        if (m_SelectionKind == SelectionKind::Node && m_SelectedNodes.size() > 1)
        {
            ImGui::Text("%d nodes selected", (int)m_SelectedNodes.size());
            ImGui::Spacing();
            if (UI::DangerButton("Delete Selected", ImVec2(-FLT_MIN, 0)))
                DeleteSelection();
            return;
        }

        // Seçim yok: makine/katman özeti
        ImGui::TextUnformatted(m_Path.empty() ? layer->Name.c_str() : m_Path.back().c_str());
        ImGui::Separator();
        UI::Text("States", "%d", (int)machine->States.size());
        UI::Text("Sub-Machines", "%d", (int)machine->SubStates.size());
        UI::Text("Transitions", "%d", (int)machine->Transitions.size());
        UI::Text("Default State", "%s", machine->DefaultState.empty() ? "(none)" : machine->DefaultState.c_str());
        ImGui::Spacing();
        if (IsLive())
        {
            auto* ac = TargetComponent();
            if (ac && m_LayerIndex < (int)ac->LayerRuntime.size())
            {
                const auto& rt = ac->LayerRuntime[(size_t)m_LayerIndex];
                UI::SubHeader("Live");
                UI::Text("Current", "%s", rt.CurrentState.c_str());
                UI::Text("Normalized Time", "%.2f", rt.CurrentNormalized);
                if (rt.InTransition)
                    UI::Text("Transition", "-> %s (%.0f%%)", rt.NextState.c_str(), 100.0f * rt.TransitionElapsed / std::max(rt.TransitionDuration, 1e-4f));
            }
        }
        UI::InfoBox("Right-click the graph to create states. Right-click a state > Make Transition, then click the target. Double-click sub-machines / blend trees to open them. Middle mouse or Alt+drag pans, wheel zooms.", UI::InfoType::Info);
    }

    void AnimatorPanel::DrawStateProperties(AnimState& state)
    {
        AnimLayer* layer = CurrentLayer();
        AnimStateMachine* owner = nullptr;
        AnimationController::FindState(*layer, state.Name, &owner);

        ImGui::PushID("stateprops");
        // Ad
        char nameBuf[128];
        snprintf(nameBuf, sizeof(nameBuf), "%s", state.Name.c_str());
        UI::BeginProperty("Name");
        if (ImGui::InputText("##name", nameBuf, sizeof(nameBuf), ImGuiInputTextFlags_EnterReturnsTrue) && nameBuf[0])
        {
            std::string old = state.Name;
            RenameNode(old, nameBuf);
            UI::EndProperty();
            ImGui::PopID();
            return; // referans geçersiz olmuş olabilir
        }
        UI::EndProperty();

        UI::BeginProperty("Tag");
        char tagBuf[64];
        snprintf(tagBuf, sizeof(tagBuf), "%s", state.Tag.c_str());
        if (ImGui::InputText("##tag", tagBuf, sizeof(tagBuf), ImGuiInputTextFlags_EnterReturnsTrue))
        {
            PushUndo();
            state.Tag = tagBuf;
            MarkDirty("tag");
        }
        UI::EndProperty();

        if (owner && owner->DefaultState == state.Name)
            UI::Badge("DEFAULT STATE", UI::Colors::Warning);
        else if (owner && ImGui::Button("Set as Default State"))
        {
            PushUndo();
            owner->DefaultState = state.Name;
            MarkDirty("default");
        }

        if (state.Type == AnimStateType::BlendTree)
        {
            ImGui::Spacing();
            if (UI::AccentButton("Open Blend Tree", ImVec2(-FLT_MIN, 0)))
                m_BlendTreeState = state.Name;
            DrawBlendTreeProperties(state);
        }
        else
        {
            UI::SubHeader("Motion");
            UI::BeginProperty("Clip");
            DrawClipCombo("##clip", state.ClipIndex, state.ClipName);
            UI::EndProperty();
            if (const Model* model = TargetModel())
                if (state.ClipIndex >= 0 && state.ClipIndex < (int)model->Animations.size() && model->Animations[(size_t)state.ClipIndex])
                    UI::Text("Length", "%.2f s", model->Animations[(size_t)state.ClipIndex]->DurationSeconds());
        }

        UI::SubHeader("Playback");
        auto beginEdit = [&]() { if (ImGui::IsItemActivated()) PushUndo(); };
        if (UI::DragFloat("Speed", state.Speed, 0.01f, -10.0f, 10.0f, "%.2f")) { beginEdit(); MarkDirty("speed"); }
        auto paramCombo = [&](const char* label, std::string& value, bool floatsOnly)
        {
            UI::BeginProperty(label);
            if (ImGui::BeginCombo("##p", value.empty() ? "None" : value.c_str()))
            {
                if (ImGui::Selectable("None", value.empty())) { PushUndo(); value.clear(); MarkDirty("param"); }
                for (const auto& p : m_Controller->Parameters)
                {
                    if (floatsOnly && p.Type != AnimParameterType::Float && p.Type != AnimParameterType::Int)
                        continue;
                    if (ImGui::Selectable(p.Name.c_str(), value == p.Name)) { PushUndo(); value = p.Name; MarkDirty("param"); }
                }
                ImGui::EndCombo();
            }
            UI::EndProperty();
        };
        ImGui::PushID("speedparam");
        paramCombo("Speed Multiplier", state.SpeedParameter, true);
        ImGui::PopID();
        ImGui::PushID("motionparam");
        paramCombo("Motion Time", state.MotionTimeParameter, true);
        ImGui::PopID();
        if (UI::SliderFloat("Cycle Offset", state.CycleOffset, 0.0f, 1.0f)) { beginEdit(); MarkDirty("cycle"); }
        bool loop = state.Loop;
        if (UI::Checkbox("Loop Time", loop)) { PushUndo(); state.Loop = loop; MarkDirty("loop"); }
        bool mirror = state.Mirror;
        if (UI::Checkbox("Mirror", mirror)) { PushUndo(); state.Mirror = mirror; MarkDirty("mirror"); }
        bool footIK = state.FootIK;
        if (UI::Checkbox("Foot IK", footIK)) { PushUndo(); state.FootIK = footIK; MarkDirty("footik"); }
        bool writeDefaults = state.WriteDefaults;
        if (UI::Checkbox("Write Defaults", writeDefaults)) { PushUndo(); state.WriteDefaults = writeDefaults; MarkDirty("wd"); }

        // Bu durumdan çıkan geçişler (öncelik sırası)
        UI::SubHeader("Transitions");
        std::vector<std::pair<AnimStateMachine*, int>> outgoing;
        std::function<void(AnimStateMachine&)> collect = [&](AnimStateMachine& m)
        {
            for (int i = 0; i < (int)m.Transitions.size(); ++i)
                if (m.Transitions[(size_t)i].FromState == state.Name)
                    outgoing.push_back({ &m, i });
            for (auto& sub : m.SubStates)
                collect(sub);
        };
        collect(*layer);
        if (outgoing.empty())
            ImGui::TextDisabled("No outgoing transitions.");
        int moveUp = -1, moveDown = -1;
        for (int k = 0; k < (int)outgoing.size(); ++k)
        {
            auto [m, i] = outgoing[(size_t)k];
            auto& t = m->Transitions[(size_t)i];
            ImGui::PushID(k);
            std::string label = state.Name + "  ->  " + t.ToState + (t.Mute ? "  [muted]" : "") + (t.Solo ? "  [solo]" : "");
            if (ImGui::Selectable(label.c_str(), false, 0, ImVec2(ImGui::GetContentRegionAvail().x - 52.0f, 0)))
            {
                m_SelectionKind = SelectionKind::Transition;
                m_SelectedNodes.clear();
                m_SelectedTransition = { m, i };
                SyncSelectionInfo();
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("^") && k > 0) moveUp = k;
            ImGui::SameLine();
            if (ImGui::SmallButton("v") && k + 1 < (int)outgoing.size()) moveDown = k;
            ImGui::PopID();
        }
        auto swapTransitions = [&](int a, int b)
        {
            auto [ma, ia] = outgoing[(size_t)a];
            auto [mb, ib] = outgoing[(size_t)b];
            if (ma != mb)
                return;
            PushUndo();
            std::swap(ma->Transitions[(size_t)ia], ma->Transitions[(size_t)ib]);
            MarkDirty("transition order");
        };
        if (moveUp > 0) swapTransitions(moveUp, moveUp - 1);
        if (moveDown >= 0) swapTransitions(moveDown, moveDown + 1);

        // Davranışlar (script hook adları)
        UI::SubHeader("Behaviours");
        int removeIdx = -1;
        for (int bi = 0; bi < (int)state.Behaviours.size(); ++bi)
        {
            ImGui::PushID(bi + 9000);
            char buf[128];
            snprintf(buf, sizeof(buf), "%s", state.Behaviours[(size_t)bi].c_str());
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 28.0f);
            if (ImGui::InputText("##bhv", buf, sizeof(buf), ImGuiInputTextFlags_EnterReturnsTrue))
            {
                PushUndo();
                state.Behaviours[(size_t)bi] = buf;
                MarkDirty("behaviour");
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("x"))
                removeIdx = bi;
            ImGui::PopID();
        }
        if (removeIdx >= 0)
        {
            PushUndo();
            state.Behaviours.erase(state.Behaviours.begin() + removeIdx);
            MarkDirty("behaviour");
        }
        if (ImGui::Button("+ Add Behaviour"))
        {
            PushUndo();
            state.Behaviours.push_back("NewBehaviour");
            MarkDirty("behaviour");
        }

        // Anim Notify'lar: normalize zamanda betiklere OnAnimNotify(ad); süreli olanlar Begin/End (Notify State)
        UI::SubHeader("Notifies");
        int removeNotify = -1;
        for (int ni = 0; ni < (int)state.Notifies.size(); ++ni)
        {
            auto& nt = state.Notifies[(size_t)ni];
            ImGui::PushID(ni + 9500);
            char buf[160];
            snprintf(buf, sizeof(buf), "%s", nt.Name.c_str());
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 28.0f);
            if (ImGui::InputTextWithHint("##ntname", "Name (or Sound:Assets/step.wav)", buf, sizeof(buf), ImGuiInputTextFlags_EnterReturnsTrue))
            {
                PushUndo();
                nt.Name = buf;
                MarkDirty("notify");
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("x"))
                removeNotify = ni;
            const float half = (ImGui::GetContentRegionAvail().x - 28.0f - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
            ImGui::SetNextItemWidth(half);
            if (ImGui::SliderFloat("##nttime", &nt.Time, 0.0f, 1.0f, "at %.3f")) { if (ImGui::IsItemActivated()) PushUndo(); MarkDirty("notify time"); }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Normalized time in the state's clip (0 = start, 1 = end)");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(half);
            if (ImGui::DragFloat("##ntdur", &nt.Duration, 0.005f, 0.0f, 1.0f, nt.Duration > 0.0f ? "state %.3f" : "instant")) { if (ImGui::IsItemActivated()) PushUndo(); MarkDirty("notify duration"); }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Duration > 0 makes a Notify State: OnAnimNotifyBegin / OnAnimNotifyEnd");
            ImGui::PopID();
        }
        if (removeNotify >= 0)
        {
            PushUndo();
            state.Notifies.erase(state.Notifies.begin() + removeNotify);
            MarkDirty("notify");
        }
        if (ImGui::Button("+ Add Notify"))
        {
            PushUndo();
            state.Notifies.push_back({ "Notify" + std::to_string(state.Notifies.size() + 1), 0.5f, 0.0f });
            MarkDirty("notify");
        }

        ImGui::Spacing();
        if (UI::DangerButton("Delete State", ImVec2(-FLT_MIN, 0)))
        {
            SelectNode(state.Name);
            DeleteSelection();
        }
        ImGui::PopID();
    }

    void AnimatorPanel::DrawConditionsEditor(AnimTransition& t)
    {
        UI::SubHeader("Conditions");
        if (t.Conditions.empty())
            ImGui::TextDisabled(t.HasExitTime ? "No conditions: the transition fires at exit time." : "Add a condition or enable Has Exit Time.");

        int removeIdx = -1;
        for (int ci = 0; ci < (int)t.Conditions.size(); ++ci)
        {
            auto& c = t.Conditions[(size_t)ci];
            ImGui::PushID(ci);
            const AnimParameter* param = m_Controller->FindParameter(c.ParameterName);
            float w = ImGui::GetContentRegionAvail().x;

            ImGui::SetNextItemWidth(w * 0.42f);
            if (ImGui::BeginCombo("##param", c.ParameterName.empty() ? "Parameter" : c.ParameterName.c_str()))
            {
                for (const auto& p : m_Controller->Parameters)
                {
                    if (ImGui::Selectable(p.Name.c_str(), c.ParameterName == p.Name))
                    {
                        PushUndo();
                        c.ParameterName = p.Name;
                        // Türe uygun varsayılan mod
                        if (p.Type == AnimParameterType::Bool || p.Type == AnimParameterType::Trigger)
                            c.Mode = AnimConditionMode::If;
                        else if (c.Mode == AnimConditionMode::If || c.Mode == AnimConditionMode::IfNot)
                            c.Mode = AnimConditionMode::Greater;
                        MarkDirty("condition");
                    }
                }
                ImGui::EndCombo();
            }
            if (!param && !c.ParameterName.empty())
            {
                ImGui::SameLine();
                ImGui::TextColored(UI::Colors::Error, "?");
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Parameter '%s' does not exist", c.ParameterName.c_str());
            }
            ImGui::SameLine();

            AnimParameterType type = param ? param->Type : AnimParameterType::Float;
            if (type == AnimParameterType::Trigger)
            {
                ImGui::TextDisabled("(trigger)");
            }
            else if (type == AnimParameterType::Bool)
            {
                const char* modes[] = { "true", "false" };
                int m = c.Mode == AnimConditionMode::IfNot ? 1 : 0;
                ImGui::SetNextItemWidth(w * 0.30f);
                if (ImGui::Combo("##bmode", &m, modes, 2))
                {
                    PushUndo();
                    c.Mode = m == 0 ? AnimConditionMode::If : AnimConditionMode::IfNot;
                    MarkDirty("condition");
                }
            }
            else
            {
                const char* modes[] = { "Greater", "Less", "Equals", "NotEqual" };
                int m = std::clamp((int)c.Mode, 0, 3);
                int count = type == AnimParameterType::Float ? 2 : 4; // float için eşitlik güvenilmez (Unity)
                ImGui::SetNextItemWidth(w * 0.26f);
                if (ImGui::Combo("##mode", &m, modes, count))
                {
                    PushUndo();
                    c.Mode = (AnimConditionMode)m;
                    MarkDirty("condition");
                }
                ImGui::SameLine();
                ImGui::SetNextItemWidth(std::max(40.0f, ImGui::GetContentRegionAvail().x - 26.0f));
                if (type == AnimParameterType::Int)
                {
                    int v = (int)std::lround(c.Threshold);
                    if (ImGui::DragInt("##thr", &v, 0.2f)) { if (ImGui::IsItemActivated()) PushUndo(); c.Threshold = (float)v; MarkDirty("threshold"); }
                }
                else
                {
                    if (ImGui::DragFloat("##thr", &c.Threshold, 0.01f, 0.0f, 0.0f, "%.3f")) { if (ImGui::IsItemActivated()) PushUndo(); MarkDirty("threshold"); }
                }
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("x"))
                removeIdx = ci;
            ImGui::PopID();
        }
        if (removeIdx >= 0)
        {
            PushUndo();
            t.Conditions.erase(t.Conditions.begin() + removeIdx);
            MarkDirty("remove condition");
        }
        if (ImGui::Button("+ Add Condition"))
        {
            PushUndo();
            AnimCondition c;
            if (!m_Controller->Parameters.empty())
            {
                const auto& p = m_Controller->Parameters.front();
                c.ParameterName = p.Name;
                c.Mode = (p.Type == AnimParameterType::Bool || p.Type == AnimParameterType::Trigger) ? AnimConditionMode::If : AnimConditionMode::Greater;
            }
            t.Conditions.push_back(c);
            MarkDirty("add condition");
        }
        if (m_Controller->Parameters.empty())
            UI::InfoBox("Create parameters in the Parameters tab to use them in conditions.", UI::InfoType::Warning);
    }

    void AnimatorPanel::DrawTransitionProperties(AnimTransition& t)
    {
        ImGui::PushID("transprops");
        ImGui::Text("%s  ->  %s", t.FromState.c_str(), t.ToState.c_str());
        ImGui::Separator();

        auto edit = [&](bool changed, const char* what) { if (changed) { if (ImGui::IsItemActivated() || !ImGui::IsItemActive()) PushUndo(); MarkDirty(what); } };

        bool hasExit = t.HasExitTime;
        if (UI::Checkbox("Has Exit Time", hasExit)) { PushUndo(); t.HasExitTime = hasExit; MarkDirty("exit"); }
        ImGui::BeginDisabled(!t.HasExitTime);
        edit(UI::DragFloat("Exit Time", t.ExitTime, 0.01f, 0.0f, 100.0f, "%.2f", "Normalized time of the source state (1 = end of clip)."), "exit time");
        ImGui::EndDisabled();
        bool fixed = t.FixedDuration;
        if (UI::Checkbox("Fixed Duration", fixed, "Duration in seconds; otherwise a fraction of the source state length.")) { PushUndo(); t.FixedDuration = fixed; MarkDirty("fixed"); }
        edit(UI::DragFloat(t.FixedDuration ? "Transition Duration (s)" : "Transition Duration (%)", t.Duration, 0.005f, 0.0f, 30.0f, "%.3f"), "duration");
        edit(UI::SliderFloat("Transition Offset", t.Offset, 0.0f, 1.0f), "offset");
        const char* interrupt[] = { "None", "Current State", "Next State", "Current State Then Next State", "Next State Then Current State" };
        int ir = (int)t.Interruption;
        if (UI::Combo("Interruption Source", ir, interrupt, 5)) { PushUndo(); t.Interruption = (AnimInterruptionSource)ir; MarkDirty("interrupt"); }
        bool ordered = t.OrderedInterruption;
        if (UI::Checkbox("Ordered Interruption", ordered)) { PushUndo(); t.OrderedInterruption = ordered; MarkDirty("ordered"); }
        if (t.FromState == AnimNodeNames::AnyState)
        {
            bool self = t.CanTransitionToSelf;
            if (UI::Checkbox("Can Transition To Self", self)) { PushUndo(); t.CanTransitionToSelf = self; MarkDirty("self"); }
        }
        bool mute = t.Mute, solo = t.Solo;
        if (UI::Checkbox("Mute", mute)) { PushUndo(); t.Mute = mute; MarkDirty("mute"); }
        if (UI::Checkbox("Solo", solo)) { PushUndo(); t.Solo = solo; MarkDirty("solo"); }

        // Zaman çizelgesi önizlemesi: kaynak çubuğu, geçiş bölgesi, hedef çubuğu
        {
            ImGui::Spacing();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImVec2 p = ImGui::GetCursorScreenPos();
            float w = ImGui::GetContentRegionAvail().x;
            float h = 18.0f;
            float exitX = std::clamp(t.HasExitTime ? t.ExitTime : 0.75f, 0.0f, 1.0f);
            float dur = t.FixedDuration ? std::clamp(t.Duration / 1.0f, 0.0f, 1.0f - exitX) : std::clamp(t.Duration, 0.0f, 1.0f - exitX);
            dl->AddRectFilled(p, ImVec2(p.x + w * (exitX + dur), p.y + h), IM_COL32(80, 90, 110, 255), 3.0f);
            dl->AddText(ImVec2(p.x + 4, p.y + 1), IM_COL32(230, 230, 235, 255), t.FromState.c_str());
            ImVec2 q(p.x, p.y + h + 4);
            dl->AddRectFilled(ImVec2(q.x + w * exitX, q.y), ImVec2(q.x + w, q.y + h), IM_COL32(60, 110, 160, 255), 3.0f);
            dl->AddText(ImVec2(q.x + w * exitX + 4, q.y + 1), IM_COL32(230, 230, 235, 255), t.ToState.c_str());
            dl->AddRectFilled(ImVec2(p.x + w * exitX, p.y), ImVec2(p.x + w * (exitX + dur), q.y + h), IM_COL32(80, 160, 255, 60));
            ImGui::Dummy(ImVec2(w, h * 2 + 8));
        }

        DrawConditionsEditor(t);

        ImGui::Spacing();
        if (UI::DangerButton("Delete Transition", ImVec2(-FLT_MIN, 0)))
            DeleteSelection();
        ImGui::PopID();
    }

    void AnimatorPanel::DrawSubMachineProperties(AnimStateMachine& machine)
    {
        ImGui::PushID("subprops");
        char nameBuf[128];
        snprintf(nameBuf, sizeof(nameBuf), "%s", machine.Name.c_str());
        UI::BeginProperty("Name");
        if (ImGui::InputText("##name", nameBuf, sizeof(nameBuf), ImGuiInputTextFlags_EnterReturnsTrue) && nameBuf[0])
        {
            std::string old = machine.Name;
            RenameNode(old, nameBuf);
            UI::EndProperty();
            ImGui::PopID();
            return;
        }
        UI::EndProperty();
        UI::Text("States", "%d", (int)machine.States.size());
        UI::Text("Sub-Machines", "%d", (int)machine.SubStates.size());
        UI::BeginProperty("Default State");
        if (ImGui::BeginCombo("##def", machine.DefaultState.empty() ? "(none)" : machine.DefaultState.c_str()))
        {
            for (const auto& s : machine.States)
                if (ImGui::Selectable(s.Name.c_str(), machine.DefaultState == s.Name)) { PushUndo(); machine.DefaultState = s.Name; MarkDirty("sub default"); }
            for (const auto& s : machine.SubStates)
                if (ImGui::Selectable(s.Name.c_str(), machine.DefaultState == s.Name)) { PushUndo(); machine.DefaultState = s.Name; MarkDirty("sub default"); }
            ImGui::EndCombo();
        }
        UI::EndProperty();
        ImGui::Spacing();
        if (UI::AccentButton("Open", ImVec2(-FLT_MIN, 0)))
            EnterSubMachine(machine.Name);
        if (UI::DangerButton("Delete Sub-State Machine", ImVec2(-FLT_MIN, 0)))
        {
            SelectNode(machine.Name);
            DeleteSelection();
        }
        ImGui::PopID();
    }

    void AnimatorPanel::DrawBlendTreeProperties(AnimState& state)
    {
        UI::SubHeader("Blend Tree");
        const char* types[] = { "1D", "2D Simple Directional", "2D Freeform Directional", "2D Freeform Cartesian", "Direct" };
        int type = (int)state.BlendType;
        if (UI::Combo("Blend Type", type, types, 5)) { PushUndo(); state.BlendType = (AnimBlendTreeType)type; MarkDirty("blend type"); }

        auto paramPicker = [&](const char* label, std::string& value)
        {
            UI::BeginProperty(label);
            if (ImGui::BeginCombo("##bp", value.empty() ? "None" : value.c_str()))
            {
                for (const auto& p : m_Controller->Parameters)
                    if (p.Type == AnimParameterType::Float || p.Type == AnimParameterType::Int)
                        if (ImGui::Selectable(p.Name.c_str(), value == p.Name)) { PushUndo(); value = p.Name; MarkDirty("blend param"); }
                ImGui::EndCombo();
            }
            UI::EndProperty();
        };
        if (state.BlendType != AnimBlendTreeType::Direct)
        {
            ImGui::PushID("bpx");
            paramPicker(state.BlendType == AnimBlendTreeType::Simple1D ? "Parameter" : "Parameter X", state.BlendParameter);
            ImGui::PopID();
            if (state.BlendType != AnimBlendTreeType::Simple1D)
            {
                ImGui::PushID("bpy");
                paramPicker("Parameter Y", state.BlendParameterY);
                ImGui::PopID();
            }
        }

        // Canlı ağırlıklar
        AnimationComponent* live = IsLive() ? TargetComponent() : nullptr;
        std::vector<float> weights;
        AnimationStateMachine::ComputeBlendWeights(state, live, weights);

        UI::SubHeader("Motions");
        int removeIdx = -1;
        for (int i = 0; i < (int)state.BlendChildren.size(); ++i)
        {
            auto& c = state.BlendChildren[(size_t)i];
            ImGui::PushID(i + 500);
            ImGui::BeginGroup();
            DrawClipCombo("##clip", c.ClipIndex, c.ClipName);
            float w = ImGui::GetContentRegionAvail().x;
            if (state.BlendType == AnimBlendTreeType::Simple1D)
            {
                ImGui::SetNextItemWidth(w * 0.45f);
                if (ImGui::DragFloat("##thr", &c.Threshold, 0.01f, 0.0f, 0.0f, "thr %.2f")) { if (ImGui::IsItemActivated()) PushUndo(); MarkDirty("threshold"); }
            }
            else if (state.BlendType == AnimBlendTreeType::Direct)
            {
                ImGui::SetNextItemWidth(w * 0.45f);
                if (ImGui::BeginCombo("##dp", c.DirectParameter.empty() ? "Parameter" : c.DirectParameter.c_str()))
                {
                    for (const auto& p : m_Controller->Parameters)
                        if (p.Type == AnimParameterType::Float)
                            if (ImGui::Selectable(p.Name.c_str(), c.DirectParameter == p.Name)) { PushUndo(); c.DirectParameter = p.Name; MarkDirty("direct"); }
                    ImGui::EndCombo();
                }
            }
            else
            {
                ImGui::SetNextItemWidth(w * 0.45f);
                if (ImGui::DragFloat2("##pos", &c.Position.x, 0.01f, 0.0f, 0.0f, "%.2f")) { if (ImGui::IsItemActivated()) PushUndo(); MarkDirty("position"); }
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(w * 0.25f);
            if (ImGui::DragFloat("##ts", &c.TimeScale, 0.01f, 0.01f, 10.0f, "x%.2f")) { if (ImGui::IsItemActivated()) PushUndo(); MarkDirty("timescale"); }
            ImGui::SameLine();
            if (ImGui::SmallButton("x"))
                removeIdx = i;
            // Ağırlık çubuğu
            float weight = i < (int)weights.size() ? weights[(size_t)i] : 0.0f;
            UI::ProgressBarLabeled(weight, nullptr, UI::Colors::AccentDim);
            ImGui::EndGroup();
            ImGui::Spacing();
            ImGui::PopID();
        }
        if (removeIdx >= 0)
        {
            PushUndo();
            state.BlendChildren.erase(state.BlendChildren.begin() + removeIdx);
            MarkDirty("remove motion");
        }
        if (ImGui::Button("+ Add Motion"))
        {
            PushUndo();
            AnimBlendChild c;
            if (!state.BlendChildren.empty())
            {
                c.Threshold = state.BlendChildren.back().Threshold + 1.0f;
                c.Position = state.BlendChildren.back().Position + glm::vec2(1.0f, 0.0f);
            }
            state.BlendChildren.push_back(c);
            MarkDirty("add motion");
        }
        if (state.BlendType == AnimBlendTreeType::Simple1D && state.BlendChildren.size() > 1)
        {
            ImGui::SameLine();
            if (ImGui::Button("Distribute Thresholds"))
            {
                PushUndo();
                for (size_t i = 0; i < state.BlendChildren.size(); ++i)
                    state.BlendChildren[i].Threshold = (float)i / (float)(state.BlendChildren.size() - 1);
                MarkDirty("distribute");
            }
        }
    }

    void AnimatorPanel::DrawBlendTreeView()
    {
        AnimLayer* layer = CurrentLayer();
        AnimState* state = layer ? AnimationController::FindState(*layer, m_BlendTreeState) : nullptr;
        if (!state || state->Type != AnimStateType::BlendTree)
        {
            m_BlendTreeState.clear();
            return;
        }

        AnimationComponent* live = IsLive() ? TargetComponent() : nullptr;
        std::vector<float> weights;
        AnimationStateMachine::ComputeBlendWeights(*state, live, weights);

        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 origin = ImGui::GetCursorScreenPos();
        ImVec2 size = ImGui::GetContentRegionAvail();
        dl->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y), IM_COL32(30, 31, 35, 255));

        if (state->BlendType == AnimBlendTreeType::Simple1D || state->BlendType == AnimBlendTreeType::Direct)
        {
            // Kök düğüm + çocuklar ağacı
            ImVec2 rootPos(origin.x + 30.0f, origin.y + size.y * 0.5f - 25.0f);
            ImVec2 rootEnd(rootPos.x + 180.0f, rootPos.y + 50.0f);
            dl->AddRectFilled(rootPos, rootEnd, kColBlend, 6.0f);
            std::string title = state->Name;
            dl->AddText(ImVec2(rootPos.x + 10, rootPos.y + 6), IM_COL32(240, 240, 245, 255), title.c_str());
            std::string sub = state->BlendType == AnimBlendTreeType::Direct ? std::string("Direct") : ("1D: " + (state->BlendParameter.empty() ? std::string("?") : state->BlendParameter));
            dl->AddText(ImVec2(rootPos.x + 10, rootPos.y + 26), IM_COL32(180, 185, 195, 255), sub.c_str());

            float childX = rootEnd.x + 120.0f;
            float spacing = std::min(70.0f, (size.y - 20.0f) / std::max<float>(1.0f, (float)state->BlendChildren.size()));
            float startY = origin.y + size.y * 0.5f - spacing * (float)state->BlendChildren.size() * 0.5f;
            for (size_t i = 0; i < state->BlendChildren.size(); ++i)
            {
                const auto& c = state->BlendChildren[i];
                ImVec2 a(childX, startY + spacing * (float)i + 8.0f);
                ImVec2 b(a.x + 220.0f, a.y + spacing - 16.0f);
                float wgt = i < weights.size() ? weights[i] : 0.0f;
                dl->AddBezierCubic(ImVec2(rootEnd.x, (rootPos.y + rootEnd.y) * 0.5f), ImVec2(rootEnd.x + 60, (rootPos.y + rootEnd.y) * 0.5f),
                                   ImVec2(a.x - 60, (a.y + b.y) * 0.5f), ImVec2(a.x, (a.y + b.y) * 0.5f),
                                   ImGui::GetColorU32(ImVec4(0.5f + 0.5f * wgt, 0.6f + 0.4f * wgt, 1.0f, 0.3f + 0.7f * wgt)), 1.5f + 2.0f * wgt);
                dl->AddRectFilled(a, b, kColState, 5.0f);
                dl->AddRectFilled(ImVec2(a.x, b.y - 4), ImVec2(a.x + (b.x - a.x) * wgt, b.y), kColLive, 2.0f);
                std::string label = (c.ClipName.empty() ? std::string("None") : c.ClipName);
                char extra[64];
                if (state->BlendType == AnimBlendTreeType::Simple1D)
                    snprintf(extra, sizeof(extra), "  @ %.2f", c.Threshold);
                else
                    snprintf(extra, sizeof(extra), "  %s", c.DirectParameter.c_str());
                label += extra;
                dl->AddText(ImVec2(a.x + 8, a.y + 6), IM_COL32(235, 236, 240, 255), label.c_str());
            }
        }
        else
        {
            // 2D: konum diyagramı (+ canlı parametre noktası)
            glm::vec2 mn(-1.0f), mx(1.0f);
            for (const auto& c : state->BlendChildren)
            {
                mn = glm::min(mn, c.Position);
                mx = glm::max(mx, c.Position);
            }
            glm::vec2 range = glm::max(mx - mn, glm::vec2(0.5f));
            mn -= range * 0.15f;
            mx += range * 0.15f;
            float sq = std::min(size.x, size.y) - 40.0f;
            ImVec2 a(origin.x + (size.x - sq) * 0.5f, origin.y + (size.y - sq) * 0.5f);
            ImVec2 b(a.x + sq, a.y + sq);
            dl->AddRectFilled(a, b, IM_COL32(38, 40, 46, 255), 4.0f);
            dl->AddRect(a, b, IM_COL32(70, 72, 80, 255), 4.0f);
            auto toScreen = [&](const glm::vec2& p)
            {
                glm::vec2 t = (p - mn) / (mx - mn);
                return ImVec2(a.x + t.x * sq, b.y - t.y * sq);
            };
            ImVec2 o = toScreen(glm::vec2(0.0f));
            dl->AddLine(ImVec2(a.x, o.y), ImVec2(b.x, o.y), IM_COL32(90, 92, 100, 255));
            dl->AddLine(ImVec2(o.x, a.y), ImVec2(o.x, b.y), IM_COL32(90, 92, 100, 255));
            for (size_t i = 0; i < state->BlendChildren.size(); ++i)
            {
                const auto& c = state->BlendChildren[i];
                ImVec2 sp = toScreen(c.Position);
                float wgt = i < weights.size() ? weights[i] : 0.0f;
                dl->AddCircleFilled(sp, 6.0f + 10.0f * wgt, ImGui::GetColorU32(ImVec4(0.3f, 0.6f, 1.0f, 0.25f + 0.6f * wgt)));
                dl->AddCircleFilled(sp, 5.0f, IM_COL32(120, 180, 255, 255));
                dl->AddText(ImVec2(sp.x + 8, sp.y - 8), IM_COL32(220, 222, 230, 255), c.ClipName.empty() ? "None" : c.ClipName.c_str());
            }
            glm::vec2 current(0.0f);
            if (live)
                current = glm::vec2(AnimationStateMachine::GetParameterFloat(*live, state->BlendParameter), AnimationStateMachine::GetParameterFloat(*live, state->BlendParameterY));
            else
            {
                if (auto* px = m_Controller->FindParameter(state->BlendParameter)) current.x = px->DefaultValue;
                if (auto* py = m_Controller->FindParameter(state->BlendParameterY)) current.y = py->DefaultValue;
            }
            ImVec2 cp = toScreen(current);
            dl->AddCircle(cp, 7.0f, IM_COL32(255, 80, 80, 255), 16, 2.0f);
            dl->AddText(ImVec2(a.x + 6, a.y + 4), IM_COL32(180, 182, 190, 255), (state->BlendParameter + " / " + state->BlendParameterY).c_str());
        }

        ImGui::Dummy(size);
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            m_BlendTreeState.clear();
    }
}
