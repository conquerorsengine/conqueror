// 2D görünümde gizmosuz düzenleme: sprite'ı doğrudan tutup taşıma (Move ve Rect araçları) ve Rect Tool (T) tutamaçlarıyla boyutlandırma.
#include "ViewportPanel.h"
#include "EditorState.h"
#include "UndoRedo/UndoRedoManager.h"
#include "UndoRedo/EditorCommand.h"
#include "Renderer/Renderer2D.h"
#include "Scene/Components.h"

#include <ImGuizmo.h>
#include <imgui.h>

#include <algorithm>
#include <cmath>

namespace Conqueror::Editor
{
    namespace
    {
        struct Drag2DRecord
        {
            uint64_t Id = 0;
            TransformComponent Old, New;
            glm::vec2 OldSize{ 0.0f }, NewSize{ 0.0f };
            bool HasSize = false;
        };

        // Bir sürüklemenin tüm nesneleri tek geri alma adımı
        class Drag2DCommand : public EditorCommand
        {
        public:
            Drag2DCommand(std::shared_ptr<Scene> scene, std::vector<Drag2DRecord> records, std::string label)
                : m_Scene(std::move(scene)), m_Records(std::move(records)), m_Label(std::move(label))
            {
            }
            void Execute() override
            {
                if (m_First)
                {
                    m_First = false;
                    return;
                }
                Apply(false);
            }
            void Unexecute() override { Apply(true); }
            std::string GetDescription() const override { return m_Label; }

        private:
            void Apply(bool undo)
            {
                for (const auto& r : m_Records)
                {
                    Entity e = m_Scene->FindEntityByUUID(r.Id);
                    if (!e)
                        continue;
                    const TransformComponent& t = undo ? r.Old : r.New;
                    auto& tc = e.GetComponent<TransformComponent>();
                    tc.Position = t.Position;
                    tc.Rotation = t.Rotation;
                    tc.Scale = t.Scale;
                    if (r.HasSize && e.HasComponent<SpriteRendererComponent>())
                        e.GetComponent<SpriteRendererComponent>().Size = undo ? r.OldSize : r.NewSize;
                }
            }
            std::shared_ptr<Scene> m_Scene;
            std::vector<Drag2DRecord> m_Records;
            std::string m_Label;
            bool m_First = true;
        };

        bool SpriteLocalRect(Entity e, glm::vec2& mn, glm::vec2& mx)
        {
            return e && e.HasComponent<SpriteRendererComponent>() && !e.HasComponent<RectTransformComponent>() &&
                   Renderer2D::GetSpriteLocalBounds(e.GetComponent<SpriteRendererComponent>(), mn, mx);
        }

        const float kHandleX[8] = { 0.0f, 0.5f, 1.0f, 1.0f, 1.0f, 0.5f, 0.0f, 0.0f };
        const float kHandleY[8] = { 0.0f, 0.0f, 0.0f, 0.5f, 1.0f, 1.0f, 1.0f, 0.5f };

        glm::mat4 ParentInverse(Entity e)
        {
            Entity parent = e.GetParent();
            return parent ? glm::inverse(parent.GetComponent<TransformComponent>().WorldTransform) : glm::mat4(1.0f);
        }
    }

    glm::vec2 ViewportPanel::ScreenToWorld2D(const ImVec2& screen) const
    {
        const float nx = (screen.x - m_ViewportBounds[0].x) / m_ViewportSize.x * 2.0f - 1.0f;
        const float ny = 1.0f - (screen.y - m_ViewportBounds[0].y) / m_ViewportSize.y * 2.0f;
        const glm::vec4 p = glm::inverse(m_EditorCamera.GetViewProjection()) * glm::vec4(nx, ny, 0.0f, 1.0f);
        return glm::vec2(p) / p.w;
    }

    glm::vec2 ViewportPanel::WorldToScreen2D(const glm::vec2& world) const
    {
        const glm::vec4 c = m_EditorCamera.GetViewProjection() * glm::vec4(world, 0.0f, 1.0f);
        return { m_ViewportBounds[0].x + (c.x / c.w * 0.5f + 0.5f) * m_ViewportSize.x, m_ViewportBounds[0].y + (0.5f - c.y / c.w * 0.5f) * m_ViewportSize.y };
    }

    void ViewportPanel::Handle2DDrag()
    {
        ImGuiIO& io = ImGui::GetIO();
        auto& state = EditorState::Get();
        const bool rectTool = m_GizmoType == kGizmoRect;
        if (!m_Context || !m_EditorCamera.Is2D() || (!rectTool && m_GizmoType != ImGuizmo::TRANSLATE) || m_ViewportSize.x <= 0.0f || m_ViewportSize.y <= 0.0f)
        {
            m_Drag2D = {};
            return;
        }
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 clipMin(m_ViewportBounds[0].x, m_ViewportBounds[0].y), clipMax(m_ViewportBounds[1].x, m_ViewportBounds[1].y);

        Entity primary = state.GetSelectedEntity();
        glm::vec2 mn(0.0f), mx(0.0f);
        const bool hasRect = rectTool && SpriteLocalRect(primary, mn, mx);
        int hoverHandle = -1;
        bool insideRect = false;
        if (hasRect)
        {
            const glm::mat4& world = primary.GetComponent<TransformComponent>().WorldTransform;
            auto corner = [&](float fx, float fy) {
                const glm::vec2 local = mn + (mx - mn) * glm::vec2(fx, fy);
                return WorldToScreen2D(glm::vec2(world * glm::vec4(local, 0.0f, 1.0f)));
            };
            const glm::vec2 quad[4] = { corner(0, 0), corner(1, 0), corner(1, 1), corner(0, 1) };
            dl->PushClipRect(clipMin, clipMax, true);
            for (int i = 0; i < 4; ++i)
            {
                const glm::vec2& a = quad[i];
                const glm::vec2& b = quad[(i + 1) % 4];
                dl->AddLine(ImVec2(a.x, a.y), ImVec2(b.x, b.y), IM_COL32(60, 150, 255, 255), 1.5f);
            }
            float best = 9.0f;
            for (int h = 0; h < 8; ++h)
            {
                const glm::vec2 p = corner(kHandleX[h], kHandleY[h]);
                const float d = glm::length(glm::vec2(io.MousePos.x, io.MousePos.y) - p);
                if (d < best && m_ImageHovered)
                {
                    best = d;
                    hoverHandle = h;
                }
            }
            for (int h = 0; h < 8; ++h)
            {
                const glm::vec2 p = corner(kHandleX[h], kHandleY[h]);
                dl->AddCircleFilled(ImVec2(p.x, p.y), h == hoverHandle ? 6.0f : 4.5f, h == hoverHandle ? IM_COL32(255, 255, 255, 255) : IM_COL32(60, 150, 255, 255), 14);
                dl->AddCircle(ImVec2(p.x, p.y), h == hoverHandle ? 6.0f : 4.5f, IM_COL32(12, 14, 20, 255), 14);
            }
            dl->PopClipRect();
            // Fare dikdörtgenin içinde mi (nesnenin yerel uzayında)
            const glm::vec4 local = glm::inverse(world) * glm::vec4(ScreenToWorld2D(io.MousePos), 0.0f, 1.0f);
            insideRect = local.x >= mn.x && local.x <= mx.x && local.y >= mn.y && local.y <= mx.y;
        }
        if (m_Drag2D.Mode == Drag2DMode::None && hoverHandle >= 0)
        {
            const bool diagonal = hoverHandle % 2 == 0;
            ImGui::SetMouseCursor(diagonal ? ((hoverHandle == 0 || hoverHandle == 4) ? ImGuiMouseCursor_ResizeNESW : ImGuiMouseCursor_ResizeNWSE)
                                           : (hoverHandle == 3 || hoverHandle == 7) ? ImGuiMouseCursor_ResizeEW : ImGuiMouseCursor_ResizeNS);
        }

        const bool overGizmo = !rectTool && state.HasSelection() && (ImGuizmo::IsOver() || ImGuizmo::IsUsing());
        const bool press = m_ImageHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !io.KeyAlt && !io.KeyCtrl && !io.KeyShift;
        if (press && m_Drag2D.Mode == Drag2DMode::None && !overGizmo && !ImGuizmo::IsViewManipulateHovered())
        {
            if (hoverHandle >= 0)
            {
                // Tutamaç: seçim değişmez, karşı köşe sabit boyutlandırma
                m_PickPending = false;
                auto& tc = primary.GetComponent<TransformComponent>();
                auto& sr = primary.GetComponent<SpriteRendererComponent>();
                m_Drag2D = {};
                m_Drag2D.Mode = Drag2DMode::Resize;
                m_Drag2D.Target = primary;
                m_Drag2D.Start = io.MousePos;
                m_Drag2D.Handle = hoverHandle;
                m_Drag2D.StartMin = mn;
                m_Drag2D.StartMax = mx;
                m_Drag2D.AxisX = glm::normalize(glm::vec2(tc.WorldTransform[0]));
                m_Drag2D.AxisY = glm::normalize(glm::vec2(tc.WorldTransform[1]));
                m_Drag2D.WorldScale = glm::vec2(glm::length(glm::vec3(tc.WorldTransform[0])), glm::length(glm::vec3(tc.WorldTransform[1])));
                m_Drag2D.UsesSize = sr.DrawMode != SpriteDrawMode::Simple;
                // Tutamaç fareyle tam birlikte gitsin: basılan nokta ile tutamaç arasındaki fark
                const glm::vec2 handleLocal = mn + (mx - mn) * glm::vec2(kHandleX[hoverHandle], kHandleY[hoverHandle]);
                m_Drag2D.GrabOffset = glm::vec2(tc.WorldTransform * glm::vec4(handleLocal, 0.0f, 1.0f)) - ScreenToWorld2D(io.MousePos);
                Drag2DItem item;
                item.Id = primary.GetComponent<IDComponent>().ID;
                item.Old = tc;
                item.OldSize = sr.Size;
                item.ParentInverse = ParentInverse(primary);
                m_Drag2D.Items.push_back(item);
            }
            else
            {
                Entity target = hasRect && insideRect ? primary : PickAt(io.MousePos);
                glm::vec2 a, b;
                if (target && SpriteLocalRect(target, a, b))
                {
                    m_Drag2D = {};
                    m_Drag2D.Mode = Drag2DMode::Pending;
                    m_Drag2D.Target = target;
                    m_Drag2D.Start = io.MousePos;
                }
            }
        }

        if (m_Drag2D.Mode == Drag2DMode::None)
            return;
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            // Bitti: değişen nesneler tek geri alma adımında
            std::vector<Drag2DRecord> records;
            for (const auto& item : m_Drag2D.Items)
            {
                Entity e = m_Context->FindEntityByUUID(item.Id);
                if (!e)
                    continue;
                const auto& tc = e.GetComponent<TransformComponent>();
                Drag2DRecord r;
                r.Id = item.Id;
                r.Old = item.Old;
                r.New = tc;
                r.OldSize = item.OldSize;
                r.HasSize = e.HasComponent<SpriteRendererComponent>();
                if (r.HasSize)
                    r.NewSize = e.GetComponent<SpriteRendererComponent>().Size;
                if (tc.Position != item.Old.Position || tc.Scale != item.Old.Scale || (r.HasSize && r.NewSize != r.OldSize))
                    records.push_back(r);
            }
            if (!records.empty())
                UndoRedoManager::Get().Execute(std::make_shared<Drag2DCommand>(m_Context, std::move(records), m_Drag2D.Mode == Drag2DMode::Resize ? "Resize" : "Move"));
            m_Drag2D = {};
            return;
        }

        const glm::vec2 startWorld = ScreenToWorld2D(m_Drag2D.Start);
        const glm::vec2 mouseWorld = ScreenToWorld2D(io.MousePos);

        if (m_Drag2D.Mode == Drag2DMode::Pending)
        {
            const float dx = io.MousePos.x - m_Drag2D.Start.x, dy = io.MousePos.y - m_Drag2D.Start.y;
            if (dx * dx + dy * dy <= 16.0f)
                return;
            // Sürükleme başladı: seçili değilse seçilir; seçimin parçasıysa bütün seçim taşınır
            auto& selected = state.GetSelectedEntities();
            if (std::find(selected.begin(), selected.end(), m_Drag2D.Target) == selected.end() && m_SelectionCallback)
                m_SelectionCallback(m_Drag2D.Target, false);
            m_PickPending = false;
            for (Entity e : state.GetSelectedEntities())
            {
                if (!e || !e.HasComponent<TransformComponent>() || e.HasComponent<RectTransformComponent>())
                    continue;
                const auto& tc = e.GetComponent<TransformComponent>();
                Drag2DItem item;
                item.Id = e.GetComponent<IDComponent>().ID;
                item.Old = tc;
                item.StartWorld = glm::vec2(tc.WorldTransform[3]);
                item.ParentInverse = ParentInverse(e);
                if (e.HasComponent<SpriteRendererComponent>())
                    item.OldSize = e.GetComponent<SpriteRendererComponent>().Size;
                m_Drag2D.Items.push_back(item);
            }
            m_Drag2D.Mode = Drag2DMode::Move;
        }

        if (m_Drag2D.Mode == Drag2DMode::Move)
        {
            glm::vec2 delta = mouseWorld - startWorld;
            if (state.IsSnapEnabled() != io.KeyCtrl && state.GetSnapValue() > 0.0f)
                delta = glm::round(delta / state.GetSnapValue()) * state.GetSnapValue();
            for (const auto& item : m_Drag2D.Items)
            {
                Entity e = m_Context->FindEntityByUUID(item.Id);
                if (!e)
                    continue;
                auto& tc = e.GetComponent<TransformComponent>();
                const glm::vec4 local = item.ParentInverse * glm::vec4(item.StartWorld + delta, item.Old.WorldTransform[3].z, 1.0f);
                tc.Position.x = local.x;
                tc.Position.y = local.y;
            }
            return;
        }

        // Boyutlandırma: karşı kenar / köşe dünyada sabit kalır; Alt: merkeze göre, Shift: en-boy oranı korunur
        Entity e = m_Drag2D.Target;
        if (!e || !e.HasComponent<SpriteRendererComponent>() || m_Drag2D.Items.empty())
        {
            m_Drag2D = {};
            return;
        }
        const Drag2DItem& item = m_Drag2D.Items[0];
        auto& tc = e.GetComponent<TransformComponent>();
        auto& sr = e.GetComponent<SpriteRendererComponent>();
        const glm::vec2 startLocal = m_Drag2D.StartMax - m_Drag2D.StartMin;
        const glm::vec2 startWorldSize = startLocal * m_Drag2D.WorldScale;
        const glm::vec2 f(kHandleX[m_Drag2D.Handle], kHandleY[m_Drag2D.Handle]);
        const bool symmetric = io.KeyAlt;
        const glm::vec2 anchorFrac = symmetric ? glm::vec2(0.5f) : glm::vec2(f.x == 0.5f ? 0.5f : 1.0f - f.x, f.y == 0.5f ? 0.5f : 1.0f - f.y);
        const glm::vec2 startPos = glm::vec2(item.Old.WorldTransform[3]);
        const glm::vec2 anchorLocal = m_Drag2D.StartMin + startLocal * anchorFrac;
        const glm::vec2 anchorWorld = startPos + m_Drag2D.AxisX * (anchorLocal.x * m_Drag2D.WorldScale.x) + m_Drag2D.AxisY * (anchorLocal.y * m_Drag2D.WorldScale.y);
        const glm::vec2 v = mouseWorld + m_Drag2D.GrabOffset - anchorWorld;
        const float signX = f.x > anchorFrac.x ? 1.0f : f.x < anchorFrac.x ? -1.0f : 0.0f, signY = f.y > anchorFrac.y ? 1.0f : f.y < anchorFrac.y ? -1.0f : 0.0f;
        const float span = symmetric ? 2.0f : 1.0f;
        glm::vec2 newSize = startWorldSize;
        if (signX != 0.0f)
            newSize.x = std::max(glm::dot(v, m_Drag2D.AxisX) * signX * span, 0.01f);
        if (signY != 0.0f)
            newSize.y = std::max(glm::dot(v, m_Drag2D.AxisY) * signY * span, 0.01f);
        if (io.KeyShift && startWorldSize.x > 0.0f && startWorldSize.y > 0.0f)
        {
            const float ratio = signX != 0.0f && signY != 0.0f ? std::max(newSize.x / startWorldSize.x, newSize.y / startWorldSize.y)
                                : signX != 0.0f ? newSize.x / startWorldSize.x : newSize.y / startWorldSize.y;
            newSize = startWorldSize * ratio;
        }
        glm::vec2 newMin = m_Drag2D.StartMin, newMax = m_Drag2D.StartMax, worldScale = m_Drag2D.WorldScale;
        if (m_Drag2D.UsesSize)
        {
            sr.Size = glm::max(newSize / m_Drag2D.WorldScale, glm::vec2(0.0001f));
            Renderer2D::GetSpriteLocalBounds(sr, newMin, newMax);
        }
        else
        {
            const glm::vec2 factor = newSize / glm::max(startWorldSize, glm::vec2(1e-5f));
            tc.Scale.x = item.Old.Scale.x * factor.x;
            tc.Scale.y = item.Old.Scale.y * factor.y;
            worldScale *= factor;
        }
        const glm::vec2 newAnchor = newMin + (newMax - newMin) * anchorFrac;
        const glm::vec2 newPos = anchorWorld - m_Drag2D.AxisX * (newAnchor.x * worldScale.x) - m_Drag2D.AxisY * (newAnchor.y * worldScale.y);
        const glm::vec4 local = item.ParentInverse * glm::vec4(newPos, item.Old.WorldTransform[3].z, 1.0f);
        tc.Position.x = local.x;
        tc.Position.y = local.y;
    }
}
