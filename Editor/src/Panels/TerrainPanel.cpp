#include "Core/Project/Project.h"
#include "Assets/AssetUI.h"
#include "ObjectPicker.h"
#include "TerrainPanel.h"
#include "EditorState.h"
#include "Scene/Scene.h"
#include "Scene/Components.h"
#include "Renderer/RHI/Texture.h"
#include "Core/Logging/Log.h"
#include "UI/EditorUI.h"

#include <imgui.h>
#include <IconsFontAwesome6.h>
#include <nfd.h>

#include <algorithm>
#include <cmath>

namespace Conqueror::Editor
{
    namespace
    {
        const char* kToolNames[] = { "Raise / Lower", "Smooth", "Flatten", "Noise", "Paint Texture" };
        const char* kToolIcons[] = { ICON_FA_MOUNTAIN, ICON_FA_WATER, ICON_FA_RULER_HORIZONTAL, ICON_FA_DICE, ICON_FA_PAINTBRUSH };
        const char* kToolHelp[] = {
            "Click and drag to raise terrain. Hold Shift to lower.",
            "Click and drag to smooth out bumps.",
            "Drag to flatten towards the target height. Ctrl+Click samples the height under the cursor.",
            "Drag to add natural noise detail. Shift inverts.",
            "Drag to paint the selected texture layer."
        };
    }

    Entity TerrainPanel::CreateTerrain(Scene* scene, int resolution, float size)
    {
        if (!scene)
            return {};
        Entity e = scene->CreateEntity("Terrain");
        TerrainComponent t;
        t.Width = size;
        t.Length = size;
        t.Height = size * 0.3f;
        TerrainSystem::Initialize(t, resolution, 0.0f);
        // Varsayılan katmanlar: çimen, kaya, kar, kum (renk tonu; doku sonradan sürüklenir)
        t.LayerCount = 4;
        t.Layers[0].Tint = glm::vec3(0.36f, 0.52f, 0.24f);
        t.Layers[1].Tint = glm::vec3(0.46f, 0.43f, 0.40f);
        t.Layers[1].Roughness = 0.9f;
        t.Layers[2].Tint = glm::vec3(0.92f, 0.94f, 0.97f);
        t.Layers[2].Roughness = 0.6f;
        t.Layers[3].Tint = glm::vec3(0.82f, 0.74f, 0.52f);
        e.AddComponent<TerrainComponent>(t);
        // Merkezi orijine al
        e.GetComponent<TransformComponent>().Position = glm::vec3(-size * 0.5f, 0.0f, -size * 0.5f);
        return e;
    }

    Entity TerrainPanel::GetTarget() const
    {
        if (!m_Context)
            return {};
        Entity sel = EditorState::Get().GetSelectedEntity();
        if (sel && sel.GetScene() == m_Context && sel.HasComponent<TerrainComponent>())
            return sel;
        auto view = m_Context->m_Registry.view<TerrainComponent>();
        if (!view.empty())
            return Entity{ view.front(), m_Context };
        return {};
    }

    TerrainBrushOp TerrainPanel::CurrentOp(bool invert) const
    {
        switch (m_Tool)
        {
            case Tool::RaiseLower: return invert ? TerrainBrushOp::Lower : TerrainBrushOp::Raise;
            case Tool::Smooth: return TerrainBrushOp::Smooth;
            case Tool::Flatten: return TerrainBrushOp::Flatten;
            case Tool::Noise: return TerrainBrushOp::Noise;
            case Tool::Paint: return TerrainBrushOp::Paint;
            default: return TerrainBrushOp::Raise;
        }
    }

    // ------------------------------------------------------------------ Geri al

    void TerrainPanel::PushUndo(Entity e)
    {
        if (!e || !e.HasComponent<TerrainComponent>())
            return;
        auto& t = e.GetComponent<TerrainComponent>();
        Snapshot s;
        s.EntityID = (uint64_t)e.GetComponent<IDComponent>().ID;
        s.Heights = t.Heights;
        s.Splat = t.Splat;
        m_UndoStack.push_back(std::move(s));
        if (m_UndoStack.size() > 24)
            m_UndoStack.pop_front();
        m_RedoStack.clear();
    }

    void TerrainPanel::Undo()
    {
        if (m_UndoStack.empty() || !m_Context)
            return;
        Snapshot s = std::move(m_UndoStack.back());
        m_UndoStack.pop_back();
        Entity e = m_Context->FindEntityByUUID(s.EntityID);
        if (!e || !e.HasComponent<TerrainComponent>())
            return;
        auto& t = e.GetComponent<TerrainComponent>();
        Snapshot redo{ s.EntityID, t.Heights, t.Splat };
        m_RedoStack.push_back(std::move(redo));
        if (s.Heights.size() == t.Heights.size()) t.Heights = std::move(s.Heights);
        if (s.Splat.size() == t.Splat.size()) t.Splat = std::move(s.Splat);
        t.MeshDirty = true;
        t.SplatDirty = true;
        t.ColliderVersion++;
    }

    void TerrainPanel::Redo()
    {
        if (m_RedoStack.empty() || !m_Context)
            return;
        Snapshot s = std::move(m_RedoStack.back());
        m_RedoStack.pop_back();
        Entity e = m_Context->FindEntityByUUID(s.EntityID);
        if (!e || !e.HasComponent<TerrainComponent>())
            return;
        auto& t = e.GetComponent<TerrainComponent>();
        Snapshot undo{ s.EntityID, t.Heights, t.Splat };
        m_UndoStack.push_back(std::move(undo));
        if (s.Heights.size() == t.Heights.size()) t.Heights = std::move(s.Heights);
        if (s.Splat.size() == t.Splat.size()) t.Splat = std::move(s.Splat);
        t.MeshDirty = true;
        t.SplatDirty = true;
        t.ColliderVersion++;
    }

    void TerrainPanel::ApplyStroke(Entity e, const glm::vec2& localCenter, float dt, bool invert)
    {
        if (!e || !e.HasComponent<TerrainComponent>())
            return;
        TerrainSystem::ApplyBrush(e.GetComponent<TerrainComponent>(), CurrentOp(invert), m_Brush, localCenter, dt);
    }

    // ------------------------------------------------------------------ Viewport

    bool TerrainPanel::OnViewportTool(const ViewportToolContext& ctx)
    {
        if (m_Tool == Tool::None || !EditorState::Get().IsEditing())
            return false;
        Entity e = GetTarget();
        // Yalnızca terrain seçiliyken aktif (diğer nesneler normal seçilebilsin)
        Entity sel = EditorState::Get().GetSelectedEntity();
        if (!e || sel != e)
        {
            m_Stroking = false;
            return false;
        }
        auto& t = e.GetComponent<TerrainComponent>();
        const glm::mat4& world = e.GetComponent<TransformComponent>().WorldTransform;

        glm::vec3 hit;
        m_HasHit = ctx.Hovered && TerrainSystem::Raycast(t, world, ctx.RayOrigin, ctx.RayDirection, hit);
        if (m_HasHit)
            m_LastHit = hit;

        if (!m_HasHit)
        {
            if (!ctx.MouseDown)
                m_Stroking = false;
            return m_Stroking;
        }

        glm::vec3 local = glm::vec3(glm::inverse(world) * glm::vec4(hit, 1.0f));

        // Fırça halkası (yüzeye oturan)
        if (ctx.DrawList && ctx.WorldToScreen)
        {
            const int segments = 64;
            ImVec2 prev;
            bool prevOk = false;
            const ImU32 col = m_Tool == Tool::Paint ? IM_COL32(120, 200, 255, 230) : IM_COL32(255, 210, 90, 230);
            for (int i = 0; i <= segments; ++i)
            {
                float a = (float)i / segments * 6.2831853f;
                glm::vec2 p(local.x + std::cos(a) * m_Brush.Radius, local.z + std::sin(a) * m_Brush.Radius);
                float h = TerrainSystem::GetHeightLocal(t, std::clamp(p.x, 0.0f, t.Width), std::clamp(p.y, 0.0f, t.Length));
                glm::vec3 w = glm::vec3(world * glm::vec4(p.x, h + 0.05f, p.y, 1.0f));
                ImVec2 s;
                bool ok = ctx.WorldToScreen(w, s);
                if (ok && prevOk)
                    ctx.DrawList->AddLine(prev, s, col, 2.0f);
                prev = s;
                prevOk = ok;
            }
            // İç halka: sert çekirdek
            float inner = m_Brush.Radius * (1.0f - std::clamp(m_Brush.Falloff, 0.0f, 1.0f));
            if (inner > 0.05f)
            {
                prevOk = false;
                for (int i = 0; i <= segments; ++i)
                {
                    float a = (float)i / segments * 6.2831853f;
                    glm::vec2 p(local.x + std::cos(a) * inner, local.z + std::sin(a) * inner);
                    float h = TerrainSystem::GetHeightLocal(t, std::clamp(p.x, 0.0f, t.Width), std::clamp(p.y, 0.0f, t.Length));
                    ImVec2 s;
                    bool ok = ctx.WorldToScreen(glm::vec3(world * glm::vec4(p.x, h + 0.05f, p.y, 1.0f)), s);
                    if (ok && prevOk)
                        ctx.DrawList->AddLine(prev, s, (col & 0x00FFFFFF) | 0x70000000, 1.0f);
                    prev = s;
                    prevOk = ok;
                }
            }
            ImVec2 c;
            if (ctx.WorldToScreen(hit, c))
                ctx.DrawList->AddCircleFilled(c, 3.0f, col);
        }

        // Ctrl+Click (Flatten): hedef yüksekliği örnekle
        if (m_Tool == Tool::Flatten && ctx.Ctrl && ctx.MouseClicked)
        {
            m_Brush.TargetHeight = std::clamp(local.y / std::max(t.Height, 1e-4f), 0.0f, 1.0f);
            return true;
        }

        if (ctx.MouseClicked)
        {
            PushUndo(e);
            m_Stroking = true;
        }
        if (m_Stroking && ctx.MouseDown)
            ApplyStroke(e, glm::vec2(local.x, local.z), std::min(ctx.DeltaTime, 0.05f), ctx.Shift);
        if (!ctx.MouseDown)
            m_Stroking = false;
        return true;
    }

    // ------------------------------------------------------------------ Panel

    void TerrainPanel::OnImGuiRender(bool* open)
    {
        if (!ImGui::Begin(ICON_FA_MOUNTAIN_SUN " Terrain###Terrain", open))
        {
            ImGui::End();
            return;
        }

        Entity e = GetTarget();
        if (!e)
        {
            ImGui::Spacing();
            UI::InfoBox("No terrain in the scene.", UI::InfoType::Info);
            if (UI::AccentButton(ICON_FA_PLUS "  Create Terrain", ImVec2(-1, 0)))
            {
                Entity created = CreateTerrain(m_Context, m_NewResolution);
                if (created)
                    EditorState::Get().SelectEntity(created);
            }
            ImGui::End();
            return;
        }

        auto& t = e.GetComponent<TerrainComponent>();
        TerrainSystem::EnsureData(t);
        const bool selected = EditorState::Get().GetSelectedEntity() == e;

        // Başlık
        ImGui::TextColored(UI::Colors::Accent, ICON_FA_MOUNTAIN_SUN);
        ImGui::SameLine();
        ImGui::TextUnformatted(e.GetComponent<TagComponent>().Tag.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("%dx%d  |  %.0fx%.0f m", t.Resolution, t.Resolution, t.Width, t.Length);
        if (!selected)
        {
            ImGui::SameLine();
            if (ImGui::SmallButton("Select"))
                EditorState::Get().SelectEntity(e);
        }

        DrawToolbar(t);
        if (!selected && m_Tool != Tool::None)
            UI::InfoBox("Select the terrain to edit it in the Scene view.", UI::InfoType::Warning);

        ImGui::BeginChild("##terrainbody");
        DrawBrushSettings(t);
        DrawLayers(t);
        DrawGenerate(t, e);
        DrawSettings(t, e);
        ImGui::EndChild();

        // Kısayollar (panel odaktayken)
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput)
        {
            if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z, false)) Undo();
            if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y, false)) Redo();
            if (ImGui::IsKeyPressed(ImGuiKey_LeftBracket)) m_Brush.Radius = std::max(0.5f, m_Brush.Radius * 0.85f);
            if (ImGui::IsKeyPressed(ImGuiKey_RightBracket)) m_Brush.Radius = std::min(500.0f, m_Brush.Radius * 1.18f);
        }
        ImGui::End();
    }

    void TerrainPanel::DrawToolbar(TerrainComponent& t)
    {
        (void)t;
        ImGui::Spacing();
        const float bw = (ImGui::GetContentRegionAvail().x - 4.0f * 4.0f - 30.0f * 2.0f - 8.0f) / 5.0f;
        for (int i = 0; i < (int)Tool::Count; ++i)
        {
            if (i > 0) ImGui::SameLine(0.0f, 4.0f);
            bool active = (int)m_Tool == i;
            if (active)
            {
                ImGui::PushStyleColor(ImGuiCol_Button, UI::Colors::AccentDim);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, UI::Colors::Accent);
            }
            if (ImGui::Button(kToolIcons[i], ImVec2(std::max(28.0f, bw), 30.0f)))
                m_Tool = active ? Tool::None : (Tool)i;
            if (active)
                ImGui::PopStyleColor(2);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                ImGui::SetTooltip("%s", kToolNames[i]);
        }
        ImGui::SameLine(0.0f, 8.0f);
        ImGui::BeginDisabled(m_UndoStack.empty());
        if (ImGui::Button(ICON_FA_ROTATE_LEFT, ImVec2(30, 30))) Undo();
        ImGui::EndDisabled();
        ImGui::SameLine(0.0f, 4.0f);
        ImGui::BeginDisabled(m_RedoStack.empty());
        if (ImGui::Button(ICON_FA_ROTATE_RIGHT, ImVec2(30, 30))) Redo();
        ImGui::EndDisabled();

        if (m_Tool != Tool::None)
        {
            ImGui::TextColored(UI::Colors::Accent, "%s", kToolNames[(int)m_Tool]);
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextDisabled("%s", kToolHelp[(int)m_Tool]);
            ImGui::PopTextWrapPos();
        }
    }

    void TerrainPanel::DrawBrushSettings(TerrainComponent& t)
    {
        if (m_Tool == Tool::None || !UI::BeginSection("Brush", true, ICON_FA_PAINTBRUSH))
            return;
        // Brush varlığı (Unity: fırça şekli maskeden)
        UI::BeginProperty("Brush Shape", "A .cqbrush asset (mask texture). Empty = round brush.");
        {
            std::string shape = m_Brush.Brush.empty() ? std::string() : Editor::AssetUI::ToProjectRelative(m_Brush.Brush);
            if (Editor::AssetUI::AssetSlot("brushshape", "Brush", { TerrainAssets::BrushExtension }, shape))
                m_Brush.Brush = shape.empty() ? std::string() : Editor::AssetUI::Resolve(shape).string();
        }
        UI::EndProperty();
        UI::SliderFloat("Size", m_Brush.Radius, 0.5f, std::max(t.Width, t.Length) * 0.5f, "%.1f m");
        UI::SliderFloat("Strength", m_Brush.Strength, 0.01f, 1.0f, "%.2f");
        UI::SliderFloat("Falloff", m_Brush.Falloff, 0.0f, 1.0f, "%.2f");
        if (m_Tool == Tool::Flatten)
        {
            float meters = m_Brush.TargetHeight * t.Height;
            if (UI::SliderFloat("Target Height", meters, 0.0f, t.Height, "%.2f m"))
                m_Brush.TargetHeight = meters / std::max(t.Height, 1e-4f);
        }
        if (m_Tool == Tool::Noise)
            UI::SliderFloat("Noise Scale", m_Brush.NoiseScale, 0.01f, 2.0f, "%.2f");
        if (m_Tool == Tool::Paint)
        {
            UI::BeginProperty("Layer");
            for (int i = 0; i < t.LayerCount; ++i)
            {
                if (i > 0) ImGui::SameLine(0.0f, 4.0f);
                const auto& l = t.Layers[(size_t)i];
                ImVec4 c(l.Tint.r, l.Tint.g, l.Tint.b, 1.0f);
                bool active = m_Brush.Layer == i;
                ImGui::PushID(i);
                ImVec2 p = ImGui::GetCursorScreenPos();
                if (ImGui::InvisibleButton("##layer", ImVec2(36, 36)))
                    m_Brush.Layer = i;
                ImDrawList* dl = ImGui::GetWindowDrawList();
                if (l.Albedo)
                    dl->AddImageRounded((ImTextureID)(uint64_t)l.Albedo->GetRendererID(), p, ImVec2(p.x + 36, p.y + 36), ImVec2(0, 1), ImVec2(1, 0),
                                        ImGui::GetColorU32(c), 4.0f);
                else
                    dl->AddRectFilled(p, ImVec2(p.x + 36, p.y + 36), ImGui::GetColorU32(c), 4.0f);
                dl->AddRect(p, ImVec2(p.x + 36, p.y + 36), active ? ImGui::GetColorU32(UI::Colors::Accent) : IM_COL32(0, 0, 0, 160), 4.0f, 0, active ? 3.0f : 1.0f);
                char num[4];
                snprintf(num, sizeof(num), "%d", i + 1);
                dl->AddText(ImVec2(p.x + 3, p.y + 1), IM_COL32(255, 255, 255, 220), num);
                ImGui::PopID();
            }
            UI::EndProperty();
        }
        ImGui::TextDisabled("[ / ] change size   |   Shift inverts");
        UI::EndSection();
    }

    void TerrainPanel::DrawLayers(TerrainComponent& t)
    {
        if (!UI::BeginSection("Texture Layers", m_Tool == Tool::Paint, ICON_FA_LAYER_GROUP))
            return;
        int layerCount = t.LayerCount;
        if (UI::SliderInt("Layer Count", layerCount, 1, TerrainComponent::MaxLayers))
        {
            t.LayerCount = layerCount;
            t.SplatDirty = true;
        }
        for (int i = 0; i < t.LayerCount; ++i)
        {
            auto& l = t.Layers[(size_t)i];
            ImGui::PushID(i);
            ImGui::Separator();
            ImGui::Text("Layer %d", i + 1);
            ImGui::SameLine();
            std::string tex = l.TexturePath.empty() ? std::string("(color)") : std::filesystem::path(l.TexturePath).filename().string();
            ImGui::TextDisabled("%s", tex.c_str());
            // Terrain Layer varlığı (Unity: katmanlar varlıktır, araziler arasında paylaşılır)
            const TerrainLayer beforeEdit = l;
            UI::BeginProperty("Terrain Layer", "A .cqterrainlayer asset shared by terrains. Empty = this terrain's own values.");
            {
                std::string asset = l.Asset;
                if (Editor::AssetUI::AssetSlot("layerasset", "Terrain Layer", { TerrainAssets::LayerExtension }, asset))
                {
                    l.Asset = Editor::AssetUI::Resolve(asset).string();
                    if (asset.empty()) l.Asset.clear();
                    t.SplatDirty = true;
                }
            }
            UI::EndProperty();
            if (l.Asset.empty() && ImGui::SmallButton(ICON_FA_FLOPPY_DISK " Save as Terrain Layer") && Project::GetActive())
            {
                std::error_code ec;
                const std::filesystem::path dir = Project::GetActiveProjectDirectory() / "Assets" / "Terrain Layers";
                std::filesystem::create_directories(dir, ec);
                std::string name = l.TexturePath.empty() ? "Layer " + std::to_string(i + 1) : std::filesystem::path(l.TexturePath).stem().string();
                std::filesystem::path file = dir / (name + TerrainAssets::LayerExtension);
                for (int n = 1; std::filesystem::exists(file, ec); ++n)
                    file = dir / (name + " " + std::to_string(n) + TerrainAssets::LayerExtension);
                if (TerrainAssets::SaveLayer(file.string(), l))
                    l.Asset = file.string();
            }

            UI::BeginProperty("Albedo", "Drag a texture from the Content Browser");
            ImVec2 p = ImGui::GetCursorScreenPos();
            ImGui::InvisibleButton("##tex", ImVec2(44, 44));
            ImDrawList* dl = ImGui::GetWindowDrawList();
            if (l.Albedo)
                dl->AddImage((ImTextureID)(uint64_t)l.Albedo->GetRendererID(), p, ImVec2(p.x + 44, p.y + 44), ImVec2(0, 1), ImVec2(1, 0));
            else
                dl->AddRectFilled(p, ImVec2(p.x + 44, p.y + 44), ImGui::GetColorU32(ImVec4(l.Tint.r, l.Tint.g, l.Tint.b, 1.0f)), 4.0f);
            dl->AddRect(p, ImVec2(p.x + 44, p.y + 44), IM_COL32(0, 0, 0, 160), 4.0f);
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM"))
                {
                    l.TexturePath = (const char*)payload->Data;
                    l.Albedo.reset();
                }
                ImGui::EndDragDropTarget();
            }
            ImGui::SameLine();
            if (std::string picked; ObjectPicker::Button("layerpick", "Texture", ObjectPicker::ImageExtensions(), l.TexturePath, picked))
            {
                l.TexturePath = picked;
                l.Albedo.reset();
            }
            ImGui::SameLine();
            if (!l.TexturePath.empty() && ImGui::SmallButton(ICON_FA_XMARK " Clear"))
            {
                l.TexturePath.clear();
                l.Albedo.reset();
            }
            UI::EndProperty();
            UI::Color3("Tint", l.Tint);
            UI::DragFloat("Tiling", l.Tiling, 0.1f, 0.1f, 1000.0f, "%.1f");
            UI::SliderFloat("Roughness", l.Roughness, 0.0f, 1.0f);
            UI::SliderFloat("Metallic", l.Metallic, 0.0f, 1.0f);
            // Varlıklı katman: düzenlemeler dosyaya (diğer araziler de güncellenir)
            if (!l.Asset.empty() && (l.TexturePath != beforeEdit.TexturePath || l.Tint != beforeEdit.Tint || l.Tiling != beforeEdit.Tiling ||
                                     l.Roughness != beforeEdit.Roughness || l.Metallic != beforeEdit.Metallic))
                TerrainAssets::SaveLayer(l.Asset, l);
            if (ImGui::SmallButton("Fill Entire Terrain"))
            {
                PushUndo(GetTarget());
                TerrainSystem::FillLayer(t, i);
            }
            ImGui::PopID();
        }
        ImGui::Spacing();
        if (UI::ButtonFullWidth(ICON_FA_WAND_MAGIC_SPARKLES "  Auto Paint (slope / height)"))
        {
            PushUndo(GetTarget());
            TerrainSystem::AutoPaint(t);
        }
        UI::EndSection();
    }

    void TerrainPanel::DrawGenerate(TerrainComponent& t, Entity e)
    {
        if (!UI::BeginSection("Generate", false, ICON_FA_DICE))
            return;
        UI::DragInt("Seed", m_Noise.Seed, 1.0f, 0, 999999);
        ImGui::SameLine();
        if (ImGui::SmallButton(ICON_FA_SHUFFLE))
            m_Noise.Seed = (int)(ImGui::GetTime() * 1000.0) % 999999;
        UI::SliderFloat("Frequency", m_Noise.Frequency, 0.2f, 16.0f, "%.2f");
        UI::SliderInt("Octaves", m_Noise.Octaves, 1, 10);
        UI::SliderFloat("Persistence", m_Noise.Persistence, 0.1f, 0.9f);
        UI::SliderFloat("Lacunarity", m_Noise.Lacunarity, 1.5f, 3.5f);
        UI::SliderFloat("Amplitude", m_Noise.Amplitude, 0.0f, 1.0f);
        UI::SliderFloat("Base Height", m_Noise.BaseHeight, 0.0f, 1.0f);
        UI::Checkbox("Ridged (mountains)", m_Noise.Ridged);
        UI::SliderFloat("Island Falloff", m_Noise.IslandFalloff, 0.0f, 1.0f);
        UI::SliderInt("Erosion Passes", m_Noise.ErosionIterations, 0, 200);
        if (UI::AccentButton(ICON_FA_MOUNTAIN "  Generate Heightmap", ImVec2(-1, 0)))
        {
            PushUndo(e);
            TerrainSystem::GenerateNoise(t, m_Noise);
            TerrainSystem::AutoPaint(t);
        }
        float bw = (ImGui::GetContentRegionAvail().x - 8.0f) / 3.0f;
        if (ImGui::Button("Smooth", ImVec2(bw, 0))) { PushUndo(e); TerrainSystem::SmoothAll(t, 2); }
        ImGui::SameLine(0.0f, 4.0f);
        if (ImGui::Button("Erode", ImVec2(bw, 0))) { PushUndo(e); TerrainSystem::ThermalErosion(t, 40); }
        ImGui::SameLine(0.0f, 4.0f);
        if (ImGui::Button("Flatten", ImVec2(bw, 0))) { PushUndo(e); TerrainSystem::Flatten(t, 0.0f); }
        UI::EndSection();
    }

    void TerrainPanel::DrawSettings(TerrainComponent& t, Entity e)
    {
        if (!UI::BeginSection("Settings", false, ICON_FA_GEAR))
            return;
        bool sizeChanged = false;
        sizeChanged |= UI::DragFloat("Width", t.Width, 0.5f, 1.0f, 100000.0f, "%.1f m");
        sizeChanged |= UI::DragFloat("Length", t.Length, 0.5f, 1.0f, 100000.0f, "%.1f m");
        sizeChanged |= UI::DragFloat("Height", t.Height, 0.2f, 0.1f, 10000.0f, "%.1f m");
        if (sizeChanged)
        {
            t.MeshDirty = true;
            t.ColliderVersion++;
        }

        static const int resolutions[] = { 33, 65, 129, 257, 513, 1025 };
        UI::BeginProperty("Resolution", "Heightmap samples per side (resampled when changed)");
        if (ImGui::BeginCombo("##res", std::to_string(t.Resolution).c_str()))
        {
            for (int r : resolutions)
                if (ImGui::Selectable(std::to_string(r).c_str(), r == t.Resolution))
                {
                    PushUndo(e);
                    m_UndoStack.clear(); // boyut değişti; eski anlık görüntüler uyumsuz
                    TerrainSystem::Resample(t, r);
                }
            ImGui::EndCombo();
        }
        UI::EndProperty();
        UI::Checkbox("Cast Shadows", t.CastShadows);
        UI::Checkbox("Receive Shadows", t.ReceiveShadows);
        UI::Checkbox("Collider", t.EnableCollider);

        ImGui::Spacing();
        float bw = (ImGui::GetContentRegionAvail().x - 4.0f) * 0.5f;
        if (ImGui::Button(ICON_FA_FILE_IMPORT " Import Heightmap", ImVec2(bw, 0)))
        {
            nfdu8char_t* out = nullptr;
            nfdfilteritem_t filters[] = { { "Heightmap", "png,raw,r16" } };
            if (NFD_OpenDialogU8(&out, filters, 1, nullptr) == NFD_OKAY)
            {
                PushUndo(e);
                if (!TerrainSystem::ImportHeightmap(t, out))
                    CQ_CORE_ERROR("Terrain: heightmap import failed: {0}", out);
                NFD_FreePathU8(out);
            }
        }
        ImGui::SameLine(0.0f, 4.0f);
        if (ImGui::Button(ICON_FA_FILE_EXPORT " Export Heightmap", ImVec2(bw, 0)))
        {
            nfdu8char_t* out = nullptr;
            nfdfilteritem_t filters[] = { { "PNG (8-bit)", "png" }, { "RAW 16-bit", "raw" } };
            if (NFD_SaveDialogU8(&out, filters, 2, nullptr, "heightmap.png") == NFD_OKAY)
            {
                if (!TerrainSystem::ExportHeightmap(t, out))
                    CQ_CORE_ERROR("Terrain: heightmap export failed: {0}", out);
                NFD_FreePathU8(out);
            }
        }
        UI::EndSection();
    }
}
