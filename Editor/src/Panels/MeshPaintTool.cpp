#include "MeshPaintTool.h"
#include "EditorState.h"
#include "UndoRedo/EditorCommand.h"
#include "UndoRedo/UndoRedoManager.h"
#include "Scene/Scene.h"
#include "Scene/Components.h"
#include "Scene/VertexPaint.h"
#include "UI/EditorUI.h"

#include <imgui.h>
#include <IconsFontAwesome6.h>

#include <algorithm>
#include <cmath>

namespace Conqueror::Editor
{
    namespace
    {
        // Bir darbe ya da doldurma: önceki / sonraki boya verisi
        class VertexPaintCommand : public EditorCommand
        {
        public:
            VertexPaintCommand(std::weak_ptr<Scene> scene, uint64_t entity, std::string before, std::string after, std::string label)
                : m_Scene(std::move(scene)), m_Entity(entity), m_Before(std::move(before)), m_After(std::move(after)), m_Label(std::move(label)) {}

            void Execute() override { Apply(m_After); }
            void Unexecute() override { Apply(m_Before); }
            std::string GetDescription() const override { return m_Label; }

        private:
            void Apply(const std::string& data)
            {
                auto scene = m_Scene.lock();
                if (!scene)
                    return;
                Entity e = scene->FindEntityByUUID(m_Entity);
                if (!e || !e.HasComponent<VertexPaintComponent>())
                    return;
                auto& c = e.GetComponent<VertexPaintComponent>();
                if (c.Data == data)
                    return;
                c.Data = data;
                c.Revision++;
            }

            std::weak_ptr<Scene> m_Scene;
            uint64_t m_Entity;
            std::string m_Before, m_After, m_Label;
        };

        uint64_t UUIDOf(Entity e) { return e && e.HasComponent<IDComponent>() ? (uint64_t)e.GetComponent<IDComponent>().ID : 0; }
    }

    MeshPaintTool& MeshPaintTool::Get()
    {
        static MeshPaintTool tool;
        return tool;
    }

    void MeshPaintTool::BeginStroke(Entity entity)
    {
        m_Stroking = true;
        m_StrokeEntity = UUIDOf(entity);
        m_StrokeBefore = entity.HasComponent<VertexPaintComponent>() ? entity.GetComponent<VertexPaintComponent>().Data : std::string();
    }

    void MeshPaintTool::EndStroke(Entity entity)
    {
        m_Stroking = false;
        if (!entity || !entity.HasComponent<VertexPaintComponent>() || UUIDOf(entity) != m_StrokeEntity)
            return;
        const std::string after = entity.GetComponent<VertexPaintComponent>().Data;
        if (after == m_StrokeBefore)
            return;
        std::weak_ptr<Scene> scene = m_Scene ? m_Scene() : nullptr;
        UndoRedoManager::Get().Execute(std::make_shared<VertexPaintCommand>(scene, m_StrokeEntity, m_StrokeBefore, after, "Vertex Paint"));
    }

    void MeshPaintTool::DrawInspector(Entity entity)
    {
        ImGui::Spacing();
        if (m_Active)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.35f, 0.25f, 0.9f));
            if (ImGui::Button(ICON_FA_PAINTBRUSH "  Painting (click to stop)", ImVec2(-FLT_MIN, 0)))
                m_Active = false;
            ImGui::PopStyleColor();
        }
        else if (ImGui::Button(ICON_FA_PAINTBRUSH "  Paint in Viewport", ImVec2(-FLT_MIN, 0)))
            m_Active = true;
        UI::Color4("Brush Color", Color);
        UI::SliderFloat("Brush Radius", Radius, 0.01f, 20.0f, "%.2f m");
        UI::SliderFloat("Strength", Strength, 0.01f, 1.0f);
        UI::SliderFloat("Falloff", Falloff, 0.0f, 1.0f);
        UI::BeginProperty("Channels", "Which color channels the brush changes (alpha is often used as a material blend mask)");
        ImGui::Checkbox("R", &PaintR); ImGui::SameLine();
        ImGui::Checkbox("G", &PaintG); ImGui::SameLine();
        ImGui::Checkbox("B", &PaintB); ImGui::SameLine();
        ImGui::Checkbox("A", &PaintA);
        UI::EndProperty();
        const float w = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
        auto fill = [&](const glm::vec4& color, const char* label) {
            BeginStroke(entity);
            VertexPaint::Fill(entity, color);
            EndStroke(entity);
            (void)label;
        };
        if (ImGui::Button(ICON_FA_FILL_DRIP "  Fill", ImVec2(w, 0)))
            fill(Color, "Vertex Paint Fill");
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_ERASER "  Clear", ImVec2(w, 0)))
            fill(glm::vec4(1.0f), "Vertex Paint Clear");
        if (m_Active)
            ImGui::TextDisabled("Drag on the object in the viewport. Shift = erase to white.");
    }

    bool MeshPaintTool::OnViewportTool(const ViewportToolContext& ctx)
    {
        if (!m_Active || !EditorState::Get().IsEditing())
        {
            m_Stroking = false;
            return false;
        }
        Entity e = EditorState::Get().GetSelectedEntity();
        if (!e || !e.HasComponent<VertexPaintComponent>())
        {
            m_Stroking = false;
            return false;
        }
        float distance = 0.0f;
        glm::vec3 normal(0.0f, 1.0f, 0.0f);
        const bool hit = ctx.Hovered && VertexPaint::Raycast(e, ctx.RayOrigin, ctx.RayDirection, distance, normal);
        if (ctx.MouseReleased && m_Stroking)
            EndStroke(e);
        if (!hit)
            return m_Stroking || (ctx.Hovered && ctx.MouseDown);
        const glm::vec3 point = ctx.RayOrigin + ctx.RayDirection * distance;

        // Fırça halkası: yüzeye teğet daire
        if (ctx.DrawList && ctx.WorldToScreen)
        {
            glm::vec3 t = glm::abs(normal.y) < 0.95f ? glm::normalize(glm::cross(normal, glm::vec3(0, 1, 0))) : glm::vec3(1, 0, 0);
            glm::vec3 b = glm::cross(normal, t);
            const ImU32 col = ImGui::GetColorU32(ImVec4(Color.r, Color.g, Color.b, 0.95f));
            for (int ring = 0; ring < 2; ++ring)
            {
                const float r = ring == 0 ? Radius : Radius * (1.0f - std::clamp(Falloff, 0.0f, 1.0f));
                if (r < 0.01f)
                    continue;
                ImVec2 prev;
                bool prevOk = false;
                for (int i = 0; i <= 48; ++i)
                {
                    const float a = (float)i / 48.0f * 6.2831853f;
                    ImVec2 s;
                    const bool ok = ctx.WorldToScreen(point + normal * 0.01f + (t * std::cos(a) + b * std::sin(a)) * r, s);
                    if (ok && prevOk)
                        ctx.DrawList->AddLine(prev, s, ring == 0 ? col : (col & 0x00FFFFFF) | 0x80000000, ring == 0 ? 2.0f : 1.0f);
                    prev = s;
                    prevOk = ok;
                }
            }
        }

        if (ctx.MouseClicked)
            BeginStroke(e);
        if (m_Stroking && ctx.MouseDown)
        {
            const uint32_t mask = (PaintR ? 1u : 0u) | (PaintG ? 2u : 0u) | (PaintB ? 4u : 0u) | (PaintA ? 8u : 0u);
            // Zamana bağlı akış: aynı yerde beklemek rengi giderek yoğunlaştırır
            const float flow = std::clamp(Strength * std::max(ctx.DeltaTime, 1.0f / 240.0f) * 8.0f, 0.0f, 1.0f);
            VertexPaint::PaintSphere(e, point, Radius, ctx.Shift ? glm::vec4(1.0f) : Color, flow, Falloff, mask);
        }
        return true;
    }
}
