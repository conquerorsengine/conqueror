#include "SpriteEditorPanel.h"
#include "ContentBrowserPanel.h"
#include "AssetInspector.h"
#include "Renderer/RHI/Texture.h"
#include "Renderer/RHI/TextureSettings.h"
#include "Core/Logging/Log.h"
#include "UI/EditorUI.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <IconsFontAwesome6.h>
#include <stb_image.h>

#include <algorithm>
#include <cmath>

namespace Conqueror::Editor
{
    namespace
    {
        const char* kPivotNames[] = { "Center", "Top Left", "Top", "Top Right", "Left", "Right", "Bottom Left", "Bottom", "Bottom Right", "Custom" };

        glm::vec4 NormalizeRect(const glm::vec4& r)
        {
            glm::vec4 o = r;
            if (o.z < 0.0f) { o.x += o.z; o.z = -o.z; }
            if (o.w < 0.0f) { o.y += o.w; o.w = -o.w; }
            return o;
        }

        int FindPivotPreset(const glm::vec2& p)
        {
            static const glm::vec2 presets[] = { { 0.5f, 0.5f }, { 0, 1 }, { 0.5f, 1 }, { 1, 1 }, { 0, 0.5f }, { 1, 0.5f }, { 0, 0 }, { 0.5f, 0 }, { 1, 0 } };
            for (int i = 0; i < 9; ++i)
                if (std::fabs(presets[i].x - p.x) < 1e-4f && std::fabs(presets[i].y - p.y) < 1e-4f)
                    return i;
            return 9;
        }
    }

    glm::vec2 SpriteEditorPanel::PivotPreset(int preset) const
    {
        static const glm::vec2 presets[] = { { 0.5f, 0.5f }, { 0, 1 }, { 0.5f, 1 }, { 1, 1 }, { 0, 0.5f }, { 1, 0.5f }, { 0, 0 }, { 0.5f, 0 }, { 1, 0 } };
        return preset >= 0 && preset < 9 ? presets[preset] : glm::vec2(0.5f);
    }

    // ------------------------------------------------------------------ Yükleme / kayıt

    bool SpriteEditorPanel::Open(const std::filesystem::path& texturePath)
    {
        if (m_Dirty && texturePath != m_TexturePath)
        {
            m_PendingOpen = texturePath;
            m_ConfirmDiscard = true;
            return false;
        }

        int w = 0, h = 0, ch = 0;
        stbi_set_flip_vertically_on_load(0);
        unsigned char* data = stbi_load(texturePath.string().c_str(), &w, &h, &ch, 4);
        if (!data)
        {
            CQ_CORE_ERROR("Sprite Editor: cannot load image {0}", texturePath.string());
            return false;
        }
        m_Pixels.assign(data, data + (size_t)w * h * 4);
        stbi_image_free(data);
        m_Width = w;
        m_Height = h;
        m_TexturePath = texturePath;
        m_ReadOnly = ContentBrowserPanel::IsReadOnly(texturePath);
        m_Texture = Texture2D::Create(texturePath.string());

        m_Sheet = SpriteSheet{};
        m_Sheet.TexturePath = texturePath.string();
        m_Sheet.Load();
        m_Selected = m_Sheet.Sprites.empty() ? -1 : 0;
        m_UndoStack.clear();
        m_RedoStack.clear();
        m_Dirty = false;
        m_RequestFrame = true;
        m_PolyPath = m_PolyVertex = -1;
        m_SelectedBone = m_SecondarySelected = -1;
        m_SelectedVerts.clear();
        m_BonePlacing = false;
        m_PoseRotation.clear();
        m_PoseOffset.clear();
        m_SecondaryPreview.reset();
        m_SecondaryPreviewPath.clear();
        if (m_Mode != EditorMode::Sprite && m_Mode != EditorMode::Secondary)
            EnsureSingleSprite();
        CQ_CORE_INFO("Sprite Editor: opened {0} ({1}x{2}, {3} sprites)", texturePath.filename().string(), w, h, m_Sheet.Sprites.size());
        return true;
    }

    bool SpriteEditorPanel::Apply()
    {
        if (m_TexturePath.empty() || m_ReadOnly)
            return false;
        // Boş adları doldur, isimleri benzersiz yap
        std::string base = m_TexturePath.stem().string();
        for (size_t i = 0; i < m_Sheet.Sprites.size(); ++i)
        {
            auto& s = m_Sheet.Sprites[i];
            if (s.Name.empty())
                s.Name = base + "_" + std::to_string(i);
            for (size_t j = 0; j < i; ++j)
                if (m_Sheet.Sprites[j].Name == s.Name)
                {
                    s.Name = m_Sheet.MakeUniqueName(s.Name);
                    break;
                }
        }
        if (!m_Sheet.Save())
            return false;
        // Birden çok dilim: Sprite Mode = Multiple (içe aktarma ayarı yoksa görsel Sprite türüne geçer: mipmapsız, net)
        if (m_Sheet.Sprites.size() > 1)
        {
            TextureImportSettings import;
            const bool hasMeta = TextureImportSettings::Load(m_TexturePath.string(), import);
            if (!hasMeta)
                import = TextureImportSettings::DefaultsFor(TextureType::Sprite);
            if (!hasMeta || import.SpriteMode != SpriteImportMode::Multiple)
            {
                import.SpriteMode = SpriteImportMode::Multiple;
                import.Save(m_TexturePath.string());
                Texture2D::ReloadFromDisk(m_TexturePath.string());
                AssetInspector::Invalidate();
            }
        }
        m_Dirty = false;
        CQ_CORE_INFO("Sprite Editor: saved {0} sprites -> {1}", m_Sheet.Sprites.size(), SpriteSheet::MetaPathFor(m_Sheet.TexturePath).string());
        return true;
    }

    void SpriteEditorPanel::Revert()
    {
        auto path = m_TexturePath;
        m_Dirty = false;
        if (!path.empty())
            Open(path);
    }

    // ------------------------------------------------------------------ Geri al

    void SpriteEditorPanel::PushUndo()
    {
        m_UndoStack.push_back(m_Sheet.Sprites);
        if (m_UndoStack.size() > 100)
            m_UndoStack.erase(m_UndoStack.begin());
        m_RedoStack.clear();
    }

    void SpriteEditorPanel::Undo()
    {
        if (m_UndoStack.empty())
            return;
        m_RedoStack.push_back(m_Sheet.Sprites);
        m_Sheet.Sprites = m_UndoStack.back();
        m_UndoStack.pop_back();
        m_Selected = std::min(m_Selected, (int)m_Sheet.Sprites.size() - 1);
        ValidateModeSelection();
        MarkDirty();
    }

    void SpriteEditorPanel::Redo()
    {
        if (m_RedoStack.empty())
            return;
        m_UndoStack.push_back(m_Sheet.Sprites);
        m_Sheet.Sprites = m_RedoStack.back();
        m_RedoStack.pop_back();
        m_Selected = std::min(m_Selected, (int)m_Sheet.Sprites.size() - 1);
        ValidateModeSelection();
        MarkDirty();
    }

    // ------------------------------------------------------------------ Dilimleme

    void SpriteEditorPanel::AddSprites(const std::vector<glm::vec4>& rects, bool replace)
    {
        PushUndo();
        if (replace)
            m_Sheet.Sprites.clear();
        std::string base = m_TexturePath.stem().string();
        for (const auto& r : rects)
        {
            // "Güvenli" mod: mevcut bir dilimle çakışanları atla
            if (!replace)
            {
                bool overlap = false;
                for (const auto& s : m_Sheet.Sprites)
                    if (r.x < s.Rect.x + s.Rect.z && s.Rect.x < r.x + r.z && r.y < s.Rect.y + s.Rect.w && s.Rect.y < r.y + r.w)
                        overlap = true;
                if (overlap)
                    continue;
            }
            SpriteDef d;
            d.Rect = r;
            d.Pivot = PivotPreset(m_SlicePivot);
            d.Name = m_Sheet.MakeUniqueName(base + "_" + std::to_string(m_Sheet.Sprites.size()));
            m_Sheet.Sprites.push_back(d);
        }
        m_Selected = m_Sheet.Sprites.empty() ? -1 : 0;
        MarkDirty();
    }

    void SpriteEditorPanel::SliceGridBySize(glm::ivec2 cell, glm::ivec2 offset, glm::ivec2 padding)
    {
        if (m_Pixels.empty())
            return;
        AddSprites(SpriteSheet::SliceGrid(m_Width, m_Height, glm::max(cell, glm::ivec2(1)), offset, padding, m_Pixels.data(), m_SkipEmpty), !m_SliceKeepExisting);
    }

    void SpriteEditorPanel::SliceGridByCount(glm::ivec2 count)
    {
        count = glm::max(count, glm::ivec2(1));
        glm::ivec2 cell((m_Width - m_Offset.x - m_Padding.x * (count.x - 1)) / count.x, (m_Height - m_Offset.y - m_Padding.y * (count.y - 1)) / count.y);
        SliceGridBySize(cell, m_Offset, m_Padding);
    }

    void SpriteEditorPanel::SliceAutomatic()
    {
        if (m_Pixels.empty())
            return;
        AddSprites(SpriteSheet::SliceAutomatic(m_Pixels.data(), m_Width, m_Height, m_MinSize), !m_SliceKeepExisting);
    }

    // ------------------------------------------------------------------ Koordinatlar

    glm::vec2 SpriteEditorPanel::TexToScreen(const glm::vec2& px) const
    {
        glm::vec2 center(m_CanvasMin.x + m_CanvasSize.x * 0.5f + m_Pan.x, m_CanvasMin.y + m_CanvasSize.y * 0.5f + m_Pan.y);
        return { center.x + (px.x - m_Width * 0.5f) * m_Zoom, center.y - (px.y - m_Height * 0.5f) * m_Zoom };
    }

    glm::vec2 SpriteEditorPanel::ScreenToTex(const glm::vec2& s) const
    {
        glm::vec2 center(m_CanvasMin.x + m_CanvasSize.x * 0.5f + m_Pan.x, m_CanvasMin.y + m_CanvasSize.y * 0.5f + m_Pan.y);
        return { (s.x - center.x) / m_Zoom + m_Width * 0.5f, -(s.y - center.y) / m_Zoom + m_Height * 0.5f };
    }

    void SpriteEditorPanel::FrameAll()
    {
        if (m_Width <= 0 || m_CanvasSize.x <= 0.0f)
            return;
        m_Zoom = std::min((m_CanvasSize.x - 40.0f) / m_Width, (m_CanvasSize.y - 40.0f) / m_Height);
        m_Zoom = std::clamp(m_Zoom, 0.05f, 64.0f);
        m_Pan = glm::vec2(0.0f);
    }

    // ------------------------------------------------------------------ Çizim

    void SpriteEditorPanel::OnImGuiRender(bool* open)
    {
        ImGui::SetNextWindowSize(ImVec2(1100, 700), ImGuiCond_FirstUseEver);
        std::string title = std::string(ICON_FA_OBJECT_UNGROUP " Sprite Editor") + (m_Dirty ? " *" : "") + "###Sprite Editor";
        if (!ImGui::Begin(title.c_str(), open, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
        {
            ImGui::End();
            return;
        }

        if (m_ConfirmDiscard)
        {
            ImGui::OpenPopup("Unapplied Changes");
            m_ConfirmDiscard = false;
        }
        if (ImGui::BeginPopupModal("Unapplied Changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("Unapplied changes for '%s'.", m_TexturePath.filename().string().c_str());
            if (UI::AccentButton("Apply", ImVec2(100, 0))) { Apply(); Open(m_PendingOpen); ImGui::CloseCurrentPopup(); }
            ImGui::SameLine();
            if (ImGui::Button("Discard", ImVec2(100, 0))) { m_Dirty = false; Open(m_PendingOpen); ImGui::CloseCurrentPopup(); }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(100, 0))) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        if (m_TexturePath.empty() || !m_Texture)
        {
            ImVec2 avail = ImGui::GetContentRegionAvail();
            const char* msg = "Drop a texture here, or right click an image in the Content Browser -> Open in Sprite Editor";
            ImGui::SetCursorPos(ImVec2(std::max(8.0f, (avail.x - ImGui::CalcTextSize(msg).x) * 0.5f), avail.y * 0.45f));
            ImGui::TextDisabled("%s", msg);
            ImGui::SetCursorPos(ImVec2(0, 0));
            ImGui::InvisibleButton("##drop", avail);
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM"))
                    Open((const char*)p->Data);
                ImGui::EndDragDropTarget();
            }
            ImGui::End();
            return;
        }

        DrawToolbar();

        const float sidebarW = 300.0f;
        ImVec2 avail = ImGui::GetContentRegionAvail();
        ImGui::BeginChild("##SpriteCanvas", ImVec2(avail.x - sidebarW - 6.0f, avail.y), ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        DrawCanvas();
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("##SpriteSidebar", ImVec2(0, avail.y), ImGuiChildFlags_None);
        DrawSidebar();
        ImGui::EndChild();

        // Kısayollar
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput)
        {
            auto& io = ImGui::GetIO();
            if (!m_ReadOnly && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z, false)) { if (io.KeyShift) Redo(); else Undo(); }
            if (!m_ReadOnly && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y, false)) Redo();
            if (!m_ReadOnly && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) Apply();
            if (ImGui::IsKeyPressed(ImGuiKey_F, false)) FrameAll();
            if (m_Mode != EditorMode::Sprite)
                HandleModeShortcuts();
            else if (!m_ReadOnly && m_Selected >= 0 && m_Selected < (int)m_Sheet.Sprites.size())
            {
                if (ImGui::IsKeyPressed(ImGuiKey_Delete, false))
                {
                    PushUndo();
                    m_Sheet.Sprites.erase(m_Sheet.Sprites.begin() + m_Selected);
                    m_Selected = std::min(m_Selected, (int)m_Sheet.Sprites.size() - 1);
                    MarkDirty();
                }
                glm::vec2 nudge(0.0f);
                float step = io.KeyShift ? 10.0f : 1.0f;
                if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) nudge.x -= step;
                if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) nudge.x += step;
                if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) nudge.y += step;
                if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) nudge.y -= step;
                if (nudge != glm::vec2(0.0f))
                {
                    PushUndo();
                    auto& r = m_Sheet.Sprites[(size_t)m_Selected].Rect;
                    r.x = std::clamp(r.x + nudge.x, 0.0f, (float)m_Width - r.z);
                    r.y = std::clamp(r.y + nudge.y, 0.0f, (float)m_Height - r.w);
                    MarkDirty();
                }
            }
        }

        ImGui::End();
    }

    void SpriteEditorPanel::DrawToolbar()
    {
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 4.0f));
        // Mod menüsü (Unity: sol üstteki açılır liste)
        static const char* const kModes[] = { "Sprite Editor", "Custom Outline", "Custom Physics Shape", "Secondary Textures", "Skinning Editor" };
        int mode = (int)m_Mode;
        ImGui::SetNextItemWidth(186.0f);
        if (ImGui::Combo("##EditorMode", &mode, kModes, 5))
            SetMode((EditorMode)mode);
        ImGui::SameLine();
        ImGui::BeginDisabled(m_ReadOnly);
        if (m_Mode == EditorMode::Sprite)
        {
            if (ImGui::Button(ICON_FA_TABLE_CELLS " Slice " ICON_FA_CARET_DOWN))
                ImGui::OpenPopup("##SlicePopup");
            DrawSlicePopup();
            ImGui::SameLine();
            ImGui::BeginDisabled(m_Selected < 0);
            if (ImGui::Button(ICON_FA_CROP_SIMPLE " Trim") && m_Selected >= 0)
            {
                PushUndo();
                auto& r = m_Sheet.Sprites[(size_t)m_Selected].Rect;
                r = SpriteSheet::Trim(m_Pixels.data(), m_Width, m_Height, r);
                MarkDirty();
            }
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("Shrink the selected rect to its opaque pixels");
            ImGui::SameLine();
        }
        else if (m_Mode == EditorMode::Outline || m_Mode == EditorMode::Physics)
        {
            DrawPolygonToolbar();
            ImGui::SameLine();
        }
        ImGui::BeginDisabled(m_UndoStack.empty());
        if (ImGui::Button(ICON_FA_ROTATE_LEFT)) Undo();
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(m_RedoStack.empty());
        if (ImGui::Button(ICON_FA_ROTATE_RIGHT)) Redo();
        ImGui::EndDisabled();
        ImGui::EndDisabled();

        ImGui::SameLine(0.0f, 20.0f);
        // Dar panelde araçlar görünüm düğmeleriyle ve Revert / Apply ile çakışmasın: görünüm düğmeleri alt satıra iner
        if (ImGui::GetCursorPosX() + 340.0f + 200.0f > ImGui::GetWindowContentRegionMax().x)
            ImGui::NewLine();
        ImGui::Checkbox("Pixel Grid", &m_ShowGrid);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(120.0f);
        float zoomPct = m_Zoom * 100.0f;
        if (ImGui::SliderFloat("##zoom", &zoomPct, 5.0f, 6400.0f, "%.0f%%", ImGuiSliderFlags_Logarithmic))
            m_Zoom = zoomPct / 100.0f;
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_EXPAND " Fit"))
            FrameAll();

        // Sağ: geri al / uygula
        const float rightW = 200.0f;
        ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 10.0f, ImGui::GetWindowContentRegionMax().x - rightW));
        ImGui::BeginDisabled(!m_Dirty || m_ReadOnly);
        if (ImGui::Button("Revert", ImVec2(90, 0)))
            Revert();
        ImGui::SameLine();
        if (UI::AccentButton(ICON_FA_CHECK " Apply", ImVec2(100, 0)))
            Apply();
        ImGui::EndDisabled();
        ImGui::PopStyleVar();
        ImGui::Separator();
    }

    void SpriteEditorPanel::DrawSlicePopup()
    {
        if (!ImGui::BeginPopup("##SlicePopup"))
            return;
        ImGui::TextDisabled("Slice");
        const char* types[] = { "Automatic", "Grid By Cell Size", "Grid By Cell Count" };
        ImGui::SetNextItemWidth(200.0f);
        ImGui::Combo("Type", &m_SliceType, types, 3);
        if (m_SliceType == 0)
        {
            ImGui::SetNextItemWidth(200.0f);
            ImGui::DragInt("Minimum Size", &m_MinSize, 0.2f, 1, 512);
        }
        else
        {
            ImGui::SetNextItemWidth(200.0f);
            if (m_SliceType == 1)
                ImGui::DragInt2("Pixel Size", &m_CellSize.x, 0.5f, 1, 8192);
            else
                ImGui::DragInt2("Column & Row", &m_CellCount.x, 0.1f, 1, 512);
            ImGui::SetNextItemWidth(200.0f);
            ImGui::DragInt2("Offset", &m_Offset.x, 0.5f, 0, 8192);
            ImGui::SetNextItemWidth(200.0f);
            ImGui::DragInt2("Padding", &m_Padding.x, 0.5f, 0, 8192);
            bool keepEmpty = !m_SkipEmpty;
            if (ImGui::Checkbox("Keep Empty Rects", &keepEmpty))
                m_SkipEmpty = !keepEmpty;
        }
        ImGui::SetNextItemWidth(200.0f);
        ImGui::Combo("Pivot", &m_SlicePivot, kPivotNames, 9);
        const char* methods[] = { "Delete Existing", "Safe (keep existing)" };
        int method = m_SliceKeepExisting ? 1 : 0;
        ImGui::SetNextItemWidth(200.0f);
        if (ImGui::Combo("Method", &method, methods, 2))
            m_SliceKeepExisting = method == 1;
        ImGui::Spacing();
        if (UI::AccentButton("Slice", ImVec2(-1, 0)))
        {
            if (m_SliceType == 0) SliceAutomatic();
            else if (m_SliceType == 1) SliceGridBySize(m_CellSize, m_Offset, m_Padding);
            else SliceGridByCount(m_CellCount);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    void SpriteEditorPanel::DrawCanvas()
    {
        ImGuiIO& io = ImGui::GetIO();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        m_CanvasMin = ImGui::GetCursorScreenPos();
        m_CanvasSize = ImGui::GetContentRegionAvail();
        if (m_CanvasSize.x < 10.0f || m_CanvasSize.y < 10.0f)
            return;
        if (m_RequestFrame)
        {
            FrameAll();
            m_RequestFrame = false;
        }

        ImGui::InvisibleButton("##canvas", m_CanvasSize, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle | ImGuiButtonFlags_MouseButtonRight);
        const bool hovered = ImGui::IsItemHovered();
        const bool active = ImGui::IsItemActive();
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM"))
                Open((const char*)p->Data);
            ImGui::EndDragDropTarget();
        }

        const ImVec2 canvasMax(m_CanvasMin.x + m_CanvasSize.x, m_CanvasMin.y + m_CanvasSize.y);
        dl->PushClipRect(m_CanvasMin, canvasMax, true);
        dl->AddRectFilled(m_CanvasMin, canvasMax, IM_COL32(32, 33, 37, 255));

        // Yakınlaştırma (imleç etrafında)
        if (hovered && io.MouseWheel != 0.0f)
        {
            glm::vec2 before = ScreenToTex({ io.MousePos.x, io.MousePos.y });
            m_Zoom = std::clamp(m_Zoom * std::pow(1.15f, io.MouseWheel), 0.05f, 64.0f);
            glm::vec2 after = TexToScreen(before);
            m_Pan += glm::vec2(io.MousePos.x, io.MousePos.y) - after;
        }

        // Doku + dama deseni
        glm::vec2 tmin = TexToScreen({ 0.0f, (float)m_Height });
        glm::vec2 tmax = TexToScreen({ (float)m_Width, 0.0f });
        {
            const float cell = 12.0f;
            ImVec2 a(std::max(tmin.x, m_CanvasMin.x), std::max(tmin.y, m_CanvasMin.y));
            ImVec2 b(std::min(tmax.x, canvasMax.x), std::min(tmax.y, canvasMax.y));
            for (float y = a.y; y < b.y; y += cell)
                for (float x = a.x; x < b.x; x += cell)
                {
                    bool dark = (((int)std::floor((x - tmin.x) / cell) + (int)std::floor((y - tmin.y) / cell)) & 1) != 0;
                    dl->AddRectFilled(ImVec2(x, y), ImVec2(std::min(x + cell, b.x), std::min(y + cell, b.y)),
                                      dark ? IM_COL32(52, 52, 56, 255) : IM_COL32(70, 70, 75, 255));
                }
            // Secondary Textures: seçili ikincil doku gösterilir; Skinning Editor: ağı olan dilimde doku soluk, ağ üstte tam renk çizilir
            uint32_t shownTexture = m_Texture->GetRendererID();
            if (m_Mode == EditorMode::Secondary && m_SecondaryPreview)
                shownTexture = m_SecondaryPreview->GetRendererID();
            const bool dimForSkin = m_Mode == EditorMode::Skin && m_Selected >= 0 && m_Selected < (int)m_Sheet.Sprites.size() &&
                                    !m_Sheet.Sprites[(size_t)m_Selected].Skin.Vertices.empty();
            dl->AddImage((ImTextureID)(uint64_t)shownTexture, ImVec2(tmin.x, tmin.y), ImVec2(tmax.x, tmax.y), ImVec2(0, 1), ImVec2(1, 0),
                         dimForSkin ? IM_COL32(255, 255, 255, 60) : IM_COL32_WHITE);
            dl->AddRect(ImVec2(tmin.x - 1, tmin.y - 1), ImVec2(tmax.x + 1, tmax.y + 1), IM_COL32(0, 0, 0, 180));
        }

        // Piksel ızgarası (yeterince yakınken)
        if (m_ShowGrid && m_Zoom >= 8.0f)
        {
            ImU32 gc = IM_COL32(255, 255, 255, 22);
            glm::vec2 a = ScreenToTex({ m_CanvasMin.x, canvasMax.y }), b = ScreenToTex({ canvasMax.x, m_CanvasMin.y });
            for (int x = std::max(0, (int)a.x); x <= std::min(m_Width, (int)b.x + 1); ++x)
            {
                glm::vec2 s0 = TexToScreen({ (float)x, 0.0f }), s1 = TexToScreen({ (float)x, (float)m_Height });
                dl->AddLine(ImVec2(s0.x, s0.y), ImVec2(s1.x, s1.y), gc);
            }
            for (int y = std::max(0, (int)a.y); y <= std::min(m_Height, (int)b.y + 1); ++y)
            {
                glm::vec2 s0 = TexToScreen({ 0.0f, (float)y }), s1 = TexToScreen({ (float)m_Width, (float)y });
                dl->AddLine(ImVec2(s0.x, s0.y), ImVec2(s1.x, s1.y), gc);
            }
        }

        const glm::vec2 mouseTex = ScreenToTex({ io.MousePos.x, io.MousePos.y });
        const glm::vec2 mouseTexSnapped = glm::clamp(glm::round(mouseTex), glm::vec2(0.0f), glm::vec2((float)m_Width, (float)m_Height));
        const float handle = 5.0f;

        if (m_Mode != EditorMode::Sprite)
        {
            DrawModeCanvas(hovered);
            DrawCanvasInfo(hovered, mouseTex);
            dl->PopClipRect();
            return;
        }

        // Sprite dikdörtgenleri
        for (int i = 0; i < (int)m_Sheet.Sprites.size(); ++i)
        {
            const auto& s = m_Sheet.Sprites[(size_t)i];
            glm::vec2 a = TexToScreen({ s.Rect.x, s.Rect.y + s.Rect.w }), b = TexToScreen({ s.Rect.x + s.Rect.z, s.Rect.y });
            const bool sel = i == m_Selected;
            dl->AddRect(ImVec2(a.x, a.y), ImVec2(b.x, b.y), sel ? IM_COL32(80, 170, 255, 255) : IM_COL32(255, 255, 255, 150), 0.0f, 0, sel ? 2.0f : 1.0f);
            if (!sel && (b.x - a.x) > 30.0f && m_Zoom >= 1.0f)
                dl->AddText(ImVec2(a.x + 3.0f, a.y + 2.0f), IM_COL32(255, 255, 255, 120), s.Name.c_str());
        }

        // Seçili: kenarlıklar, tutamaçlar, pivot
        DragMode hoverMode = DragMode::None;
        if (m_Selected >= 0 && m_Selected < (int)m_Sheet.Sprites.size())
        {
            const auto& s = m_Sheet.Sprites[(size_t)m_Selected];
            glm::vec2 a = TexToScreen({ s.Rect.x, s.Rect.y + s.Rect.w }), b = TexToScreen({ s.Rect.x + s.Rect.z, s.Rect.y });

            // 9-slice kenarlıkları (yeşil)
            const ImU32 bc = IM_COL32(90, 230, 120, 230);
            float bl = TexToScreen({ s.Rect.x + s.Border.x, 0 }).x, br = TexToScreen({ s.Rect.x + s.Rect.z - s.Border.z, 0 }).x;
            float bb = TexToScreen({ 0, s.Rect.y + s.Border.y }).y, bt = TexToScreen({ 0, s.Rect.y + s.Rect.w - s.Border.w }).y;
            dl->AddLine(ImVec2(bl, a.y), ImVec2(bl, b.y), bc, 1.5f);
            dl->AddLine(ImVec2(br, a.y), ImVec2(br, b.y), bc, 1.5f);
            dl->AddLine(ImVec2(a.x, bb), ImVec2(b.x, bb), bc, 1.5f);
            dl->AddLine(ImVec2(a.x, bt), ImVec2(b.x, bt), bc, 1.5f);

            // Tutamaçlar
            struct H { DragMode Mode; ImVec2 P; };
            H handles[] = {
                { DragMode::ResizeLT, ImVec2(a.x, a.y) }, { DragMode::ResizeRT, ImVec2(b.x, a.y) },
                { DragMode::ResizeLB, ImVec2(a.x, b.y) }, { DragMode::ResizeRB, ImVec2(b.x, b.y) },
                { DragMode::ResizeL, ImVec2(a.x, (a.y + b.y) * 0.5f) }, { DragMode::ResizeR, ImVec2(b.x, (a.y + b.y) * 0.5f) },
                { DragMode::ResizeT, ImVec2((a.x + b.x) * 0.5f, a.y) }, { DragMode::ResizeB, ImVec2((a.x + b.x) * 0.5f, b.y) },
            };
            glm::vec2 pv = TexToScreen({ s.Rect.x + s.Pivot.x * s.Rect.z, s.Rect.y + s.Pivot.y * s.Rect.w });
            auto near = [&](const ImVec2& p, float r) { return std::fabs(io.MousePos.x - p.x) <= r && std::fabs(io.MousePos.y - p.y) <= r; };
            // Küçük dilimde pivot tutamacı bütün dilimi kaplar: taşımak isterken pivot sürüklenmesin
            if (!m_ReadOnly && b.x - a.x >= 28.0f && b.y - a.y >= 28.0f && near(ImVec2(pv.x, pv.y), 7.0f)) hoverMode = DragMode::Pivot;
            if (!m_ReadOnly)
            for (auto& h : handles)
            {
                bool hot = hoverMode == DragMode::None && near(h.P, handle + 2.0f);
                if (hot) hoverMode = h.Mode;
                dl->AddRectFilled(ImVec2(h.P.x - handle, h.P.y - handle), ImVec2(h.P.x + handle, h.P.y + handle), hot ? IM_COL32(255, 255, 255, 255) : IM_COL32(80, 170, 255, 255));
                dl->AddRect(ImVec2(h.P.x - handle, h.P.y - handle), ImVec2(h.P.x + handle, h.P.y + handle), IM_COL32(10, 10, 12, 255));
            }
            // Kenarlık çizgileri sürüklenebilir
            if (!m_ReadOnly && hoverMode == DragMode::None && io.MousePos.y > a.y && io.MousePos.y < b.y)
            {
                if (std::fabs(io.MousePos.x - bl) < 4.0f && s.Border.x > 0.0f) hoverMode = DragMode::BorderL;
                else if (std::fabs(io.MousePos.x - br) < 4.0f && s.Border.z > 0.0f) hoverMode = DragMode::BorderR;
            }
            if (!m_ReadOnly && hoverMode == DragMode::None && io.MousePos.x > a.x && io.MousePos.x < b.x)
            {
                if (std::fabs(io.MousePos.y - bb) < 4.0f && s.Border.y > 0.0f) hoverMode = DragMode::BorderB;
                else if (std::fabs(io.MousePos.y - bt) < 4.0f && s.Border.w > 0.0f) hoverMode = DragMode::BorderT;
            }
            if (!m_ReadOnly && hoverMode == DragMode::None && io.MousePos.x > a.x && io.MousePos.x < b.x && io.MousePos.y > a.y && io.MousePos.y < b.y)
                hoverMode = DragMode::Move;

            // Pivot (daire)
            dl->AddCircle(ImVec2(pv.x, pv.y), 6.0f, IM_COL32(10, 10, 12, 255), 16, 3.0f);
            dl->AddCircle(ImVec2(pv.x, pv.y), 6.0f, hoverMode == DragMode::Pivot ? IM_COL32(255, 255, 255, 255) : IM_COL32(80, 170, 255, 255), 16, 1.8f);
        }

        // İmleç şekli
        switch (m_Drag != DragMode::None ? m_Drag : hoverMode)
        {
            case DragMode::ResizeL: case DragMode::ResizeR: case DragMode::BorderL: case DragMode::BorderR: ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW); break;
            case DragMode::ResizeT: case DragMode::ResizeB: case DragMode::BorderT: case DragMode::BorderB: ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS); break;
            case DragMode::ResizeLT: case DragMode::ResizeRB: ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNWSE); break;
            case DragMode::ResizeRT: case DragMode::ResizeLB: ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNESW); break;
            case DragMode::Move: case DragMode::Pivot: if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll); break;
            default: break;
        }

        // Fare basıldı: hangi işlem?
        if (hovered && (ImGui::IsMouseClicked(ImGuiMouseButton_Middle) || (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && io.KeyAlt)))
            m_Drag = DragMode::Pan;
        else if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && m_ReadOnly)
        {
            // Salt okunur: yalnız seçim
            m_Selected = -1;
            float bestArea = 1e30f;
            for (int i = 0; i < (int)m_Sheet.Sprites.size(); ++i)
            {
                const auto& r = m_Sheet.Sprites[(size_t)i].Rect;
                if (mouseTex.x >= r.x && mouseTex.x <= r.x + r.z && mouseTex.y >= r.y && mouseTex.y <= r.y + r.w && r.z * r.w < bestArea)
                {
                    bestArea = r.z * r.w;
                    m_Selected = i;
                }
            }
        }
        else if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            m_DragStartTex = mouseTexSnapped;
            m_DragPushedUndo = false;
            if (hoverMode != DragMode::None && hoverMode != DragMode::Move)
            {
                m_Drag = hoverMode;
            }
            else
            {
                // Tıklanan dikdörtgeni seç (küçük olan öncelikli)
                int hit = -1;
                float bestArea = 1e30f;
                for (int i = 0; i < (int)m_Sheet.Sprites.size(); ++i)
                {
                    const auto& r = m_Sheet.Sprites[(size_t)i].Rect;
                    if (mouseTex.x >= r.x && mouseTex.x <= r.x + r.z && mouseTex.y >= r.y && mouseTex.y <= r.y + r.w && r.z * r.w < bestArea)
                    {
                        bestArea = r.z * r.w;
                        hit = i;
                    }
                }
                if (hit >= 0 && (hit == m_Selected || hoverMode != DragMode::Move))
                {
                    m_Selected = hit;
                    m_Drag = DragMode::Move;
                }
                else if (hit >= 0)
                {
                    m_Selected = hit;
                    m_Drag = DragMode::Move;
                }
                else
                {
                    m_Selected = -1;
                    m_Drag = DragMode::Create;
                }
            }
            if (m_Selected >= 0)
            {
                m_DragStartRect = m_Sheet.Sprites[(size_t)m_Selected].Rect;
                m_DragStartBorder = m_Sheet.Sprites[(size_t)m_Selected].Border;
            }
        }

        // Sürükleme
        if (m_Drag == DragMode::Pan)
        {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Middle) || ImGui::IsMouseDown(ImGuiMouseButton_Left))
                m_Pan += glm::vec2(io.MouseDelta.x, io.MouseDelta.y);
            else
                m_Drag = DragMode::None;
        }
        else if (m_Drag == DragMode::Create)
        {
            glm::vec4 r = NormalizeRect(glm::vec4(m_DragStartTex, mouseTexSnapped - m_DragStartTex));
            glm::vec2 a = TexToScreen({ r.x, r.y + r.w }), b = TexToScreen({ r.x + r.z, r.y });
            dl->AddRectFilled(ImVec2(a.x, a.y), ImVec2(b.x, b.y), IM_COL32(80, 170, 255, 40));
            dl->AddRect(ImVec2(a.x, a.y), ImVec2(b.x, b.y), IM_COL32(80, 170, 255, 255), 0.0f, 0, 1.5f);
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
            {
                if (r.z >= 2.0f && r.w >= 2.0f)
                {
                    PushUndo();
                    SpriteDef d;
                    d.Rect = r;
                    d.Name = m_Sheet.MakeUniqueName(m_TexturePath.stem().string() + "_" + std::to_string(m_Sheet.Sprites.size()));
                    m_Sheet.Sprites.push_back(d);
                    m_Selected = (int)m_Sheet.Sprites.size() - 1;
                    MarkDirty();
                }
                m_Drag = DragMode::None;
            }
        }
        else if (m_Drag != DragMode::None && m_Selected >= 0 && m_Selected < (int)m_Sheet.Sprites.size())
        {
            if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
            {
                m_Drag = DragMode::None;
            }
            else
            {
                glm::vec2 delta = mouseTexSnapped - m_DragStartTex;
                if (!m_DragPushedUndo && (delta.x != 0.0f || delta.y != 0.0f))
                {
                    // Değişiklik başlamadan önceki durum
                    auto current = m_Sheet.Sprites;
                    current[(size_t)m_Selected].Rect = m_DragStartRect;
                    current[(size_t)m_Selected].Border = m_DragStartBorder;
                    m_UndoStack.push_back(current);
                    m_RedoStack.clear();
                    m_DragPushedUndo = true;
                }
                auto& s = m_Sheet.Sprites[(size_t)m_Selected];
                glm::vec4 r = m_DragStartRect;
                float l = r.x, bt = r.y, rt = r.x + r.z, tp = r.y + r.w;
                const float W = (float)m_Width, Hh = (float)m_Height;
                switch (m_Drag)
                {
                    case DragMode::Move:
                        l = std::clamp(r.x + delta.x, 0.0f, W - r.z); bt = std::clamp(r.y + delta.y, 0.0f, Hh - r.w);
                        rt = l + r.z; tp = bt + r.w;
                        break;
                    case DragMode::ResizeL: l = std::min(mouseTexSnapped.x, rt - 1.0f); break;
                    case DragMode::ResizeR: rt = std::max(mouseTexSnapped.x, l + 1.0f); break;
                    case DragMode::ResizeB: bt = std::min(mouseTexSnapped.y, tp - 1.0f); break;
                    case DragMode::ResizeT: tp = std::max(mouseTexSnapped.y, bt + 1.0f); break;
                    case DragMode::ResizeLB: l = std::min(mouseTexSnapped.x, rt - 1.0f); bt = std::min(mouseTexSnapped.y, tp - 1.0f); break;
                    case DragMode::ResizeRB: rt = std::max(mouseTexSnapped.x, l + 1.0f); bt = std::min(mouseTexSnapped.y, tp - 1.0f); break;
                    case DragMode::ResizeLT: l = std::min(mouseTexSnapped.x, rt - 1.0f); tp = std::max(mouseTexSnapped.y, bt + 1.0f); break;
                    case DragMode::ResizeRT: rt = std::max(mouseTexSnapped.x, l + 1.0f); tp = std::max(mouseTexSnapped.y, bt + 1.0f); break;
                    case DragMode::Pivot:
                    {
                        glm::vec2 p = (mouseTex - glm::vec2(r.x, r.y)) / glm::max(glm::vec2(r.z, r.w), glm::vec2(1.0f));
                        if (io.KeyCtrl) p = glm::round(p * 4.0f) / 4.0f; // çeyreklere yapış
                        s.Pivot = glm::clamp(p, glm::vec2(0.0f), glm::vec2(1.0f));
                        MarkDirty();
                        break;
                    }
                    case DragMode::BorderL: s.Border.x = std::clamp(mouseTexSnapped.x - r.x, 0.0f, r.z - s.Border.z); MarkDirty(); break;
                    case DragMode::BorderR: s.Border.z = std::clamp(r.x + r.z - mouseTexSnapped.x, 0.0f, r.z - s.Border.x); MarkDirty(); break;
                    case DragMode::BorderB: s.Border.y = std::clamp(mouseTexSnapped.y - r.y, 0.0f, r.w - s.Border.w); MarkDirty(); break;
                    case DragMode::BorderT: s.Border.w = std::clamp(r.y + r.w - mouseTexSnapped.y, 0.0f, r.w - s.Border.y); MarkDirty(); break;
                    default: break;
                }
                if (m_Drag != DragMode::Pivot && m_Drag < DragMode::BorderL)
                {
                    l = std::clamp(l, 0.0f, W); rt = std::clamp(rt, 0.0f, W);
                    bt = std::clamp(bt, 0.0f, Hh); tp = std::clamp(tp, 0.0f, Hh);
                    glm::vec4 nr(l, bt, rt - l, tp - bt);
                    if (nr != s.Rect)
                    {
                        s.Rect = nr;
                        MarkDirty();
                    }
                }
            }
        }
        if (!active && m_Drag != DragMode::None && !ImGui::IsMouseDown(ImGuiMouseButton_Left) && !ImGui::IsMouseDown(ImGuiMouseButton_Middle))
            m_Drag = DragMode::None;

        DrawCanvasInfo(hovered, mouseTex);
        dl->PopClipRect();
    }

    void SpriteEditorPanel::DrawCanvasInfo(bool hovered, const glm::vec2& mouseTex)
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 canvasMax(m_CanvasMin.x + m_CanvasSize.x, m_CanvasMin.y + m_CanvasSize.y);
        // Bilgi çubuğu
        {
            char info[128];
            if (hovered && ImGui::IsMousePosValid())
                snprintf(info, sizeof(info), "%d x %d   |   %zu sprites   |   cursor %d, %d   |   %.0f%%", m_Width, m_Height, m_Sheet.Sprites.size(),
                         (int)std::floor(mouseTex.x), (int)std::floor(mouseTex.y), m_Zoom * 100.0f);
            else
                snprintf(info, sizeof(info), "%d x %d   |   %zu sprites   |   %.0f%%", m_Width, m_Height, m_Sheet.Sprites.size(), m_Zoom * 100.0f);
            ImVec2 p(m_CanvasMin.x + 8.0f, canvasMax.y - ImGui::GetTextLineHeight() - 6.0f);
            ImVec2 ts = ImGui::CalcTextSize(info);
            dl->AddRectFilled(ImVec2(p.x - 4, p.y - 2), ImVec2(p.x + ts.x + 4, p.y + ts.y + 2), IM_COL32(0, 0, 0, 150), 4.0f);
            dl->AddText(p, IM_COL32(220, 220, 225, 255), info);
        }
        if (m_ReadOnly)
        {
            const char* msg = "Editing is disabled because the asset is not editable.";
            const ImVec2 ts = ImGui::CalcTextSize(msg);
            const float iconW = 26.0f;
            const ImVec2 p(m_CanvasMin.x + (m_CanvasSize.x - ts.x - iconW) * 0.5f, m_CanvasMin.y + 16.0f);
            dl->AddRectFilled(ImVec2(p.x - 14.0f, p.y - 9.0f), ImVec2(p.x + ts.x + iconW + 12.0f, p.y + ts.y + 9.0f), IM_COL32(24, 24, 27, 240), 7.0f);
            dl->AddRect(ImVec2(p.x - 14.0f, p.y - 9.0f), ImVec2(p.x + ts.x + iconW + 12.0f, p.y + ts.y + 9.0f), IM_COL32(74, 74, 80, 255), 7.0f);
            dl->AddText(p, IM_COL32(255, 200, 60, 255), ICON_FA_TRIANGLE_EXCLAMATION);
            dl->AddText(ImVec2(p.x + iconW, p.y), IM_COL32(226, 226, 230, 255), msg);
        }
    }

    void SpriteEditorPanel::DrawSpriteList()
    {
        if (UI::BeginSection("Sprites", true, ICON_FA_TABLE_CELLS))
        {
            ImGui::BeginChild("##spritelist", ImVec2(0, m_Mode == EditorMode::Sprite ? 170.0f : 96.0f), ImGuiChildFlags_Borders);
            for (int i = 0; i < (int)m_Sheet.Sprites.size(); ++i)
            {
                ImGui::PushID(i);
                const auto& s = m_Sheet.Sprites[(size_t)i];
                // Küçük önizleme
                float th = ImGui::GetTextLineHeight() + 4.0f;
                glm::vec2 uv0(s.Rect.x / m_Width, (s.Rect.y + s.Rect.w) / m_Height), uv1((s.Rect.x + s.Rect.z) / m_Width, s.Rect.y / m_Height);
                ImVec2 cur = ImGui::GetCursorScreenPos();
                if (ImGui::Selectable("##sel", i == m_Selected, 0, ImVec2(0, th)))
                    SelectSprite(i);
                ImGui::GetWindowDrawList()->AddImage((ImTextureID)(uint64_t)m_Texture->GetRendererID(), ImVec2(cur.x + 2, cur.y + 1),
                                                     ImVec2(cur.x + th, cur.y + th - 1), ImVec2(uv0.x, uv0.y), ImVec2(uv1.x, uv1.y));
                ImGui::GetWindowDrawList()->AddText(ImVec2(cur.x + th + 6.0f, cur.y + 2.0f), ImGui::GetColorU32(ImGuiCol_Text), s.Name.c_str());
                ImGui::PopID();
            }
            if (m_Sheet.Sprites.empty())
                ImGui::TextDisabled("No sprites. Use Slice or drag on the texture.");
            ImGui::EndChild();
            UI::EndSection();
        }
    }

    void SpriteEditorPanel::DrawSidebar()
    {
        if (m_Mode == EditorMode::Sprite && UI::BeginSection("Texture", true, ICON_FA_IMAGE))
        {
            UI::Text("File", "%s", m_TexturePath.filename().string().c_str());
            UI::Text("Size", "%d x %d", m_Width, m_Height);
            ImGui::BeginDisabled(m_ReadOnly);
            if (UI::DragFloat("Pixels Per Unit", m_Sheet.PixelsPerUnit, 1.0f, 1.0f, 10000.0f, "%.0f"))
                MarkDirty();
            const char* filters[] = { "Bilinear", "Point (no filter)" };
            int f = (int)m_Sheet.FilterMode;
            if (UI::Combo("Filter Mode", f, filters, 2))
            {
                m_Sheet.FilterMode = (SpriteSheet::Filter)f;
                MarkDirty();
            }
            ImGui::EndDisabled();
            UI::EndSection();
        }

        DrawSpriteList();

        if (m_Mode == EditorMode::Outline || m_Mode == EditorMode::Physics)
        {
            DrawPolygonSidebar();
            return;
        }
        if (m_Mode == EditorMode::Secondary)
        {
            DrawSecondarySidebar();
            return;
        }
        if (m_Mode == EditorMode::Skin)
        {
            DrawSkinSidebar();
            return;
        }

        if (m_Selected >= 0 && m_Selected < (int)m_Sheet.Sprites.size() && UI::BeginSection("Selected Sprite", true, ICON_FA_OBJECT_UNGROUP))
        {
            // Düzenleme başlamadan önceki durum (tek geri al adımı için)
            if (!ImGui::IsAnyItemActive())
            {
                m_SidebarSnapshot = m_Sheet.Sprites;
                m_SidebarEditing = false;
            }
            auto& s = m_Sheet.Sprites[(size_t)m_Selected];
            bool changed = false;
            ImGui::BeginDisabled(m_ReadOnly);
            changed |= UI::InputText("Name", s.Name);
            glm::vec2 pos(s.Rect.x, s.Rect.y), size(s.Rect.z, s.Rect.w);
            if (UI::Vec2("Position", pos, 1.0f)) { s.Rect.x = std::clamp(std::round(pos.x), 0.0f, (float)m_Width - s.Rect.z); s.Rect.y = std::clamp(std::round(pos.y), 0.0f, (float)m_Height - s.Rect.w); changed = true; }
            if (UI::Vec2("Size", size, 1.0f)) { s.Rect.z = std::clamp(std::round(size.x), 1.0f, (float)m_Width - s.Rect.x); s.Rect.w = std::clamp(std::round(size.y), 1.0f, (float)m_Height - s.Rect.y); changed = true; }
            UI::BeginProperty("Border", "9-slice borders in pixels: Left, Bottom, Right, Top");
            float border[4] = { s.Border.x, s.Border.y, s.Border.z, s.Border.w };
            if (ImGui::DragFloat4("##border", border, 0.5f, 0.0f, std::max(s.Rect.z, s.Rect.w), "%.0f"))
            {
                s.Border = glm::clamp(glm::round(glm::vec4(border[0], border[1], border[2], border[3])), glm::vec4(0.0f), glm::vec4(s.Rect.z, s.Rect.w, s.Rect.z, s.Rect.w));
                changed = true;
            }
            UI::EndProperty();
            int preset = FindPivotPreset(s.Pivot);
            if (UI::Combo("Pivot", preset, kPivotNames, 10) && preset < 9)
            {
                s.Pivot = PivotPreset(preset);
                changed = true;
            }
            if (UI::Vec2("Custom Pivot", s.Pivot, 0.01f))
            {
                s.Pivot = glm::clamp(s.Pivot, glm::vec2(0.0f), glm::vec2(1.0f));
                changed = true;
            }
            if (changed)
            {
                if (!m_SidebarEditing)
                {
                    m_UndoStack.push_back(m_SidebarSnapshot);
                    m_RedoStack.clear();
                    m_SidebarEditing = true;
                }
                MarkDirty();
            }

            // Önizleme
            ImGui::Spacing();
            float maxW = ImGui::GetContentRegionAvail().x, maxH = 140.0f;
            float scale = std::min(maxW / std::max(1.0f, s.Rect.z), maxH / std::max(1.0f, s.Rect.w));
            ImVec2 sz(s.Rect.z * scale, s.Rect.w * scale);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (maxW - sz.x) * 0.5f);
            glm::vec2 uv0(s.Rect.x / m_Width, (s.Rect.y + s.Rect.w) / m_Height), uv1((s.Rect.x + s.Rect.z) / m_Width, s.Rect.y / m_Height);
            ImGui::Image((ImTextureID)(uint64_t)m_Texture->GetRendererID(), sz, ImVec2(uv0.x, uv0.y), ImVec2(uv1.x, uv1.y));

            ImGui::Spacing();
            if (UI::DangerButton(ICON_FA_TRASH " Delete Sprite", ImVec2(-1, 0)))
            {
                PushUndo();
                m_Sheet.Sprites.erase(m_Sheet.Sprites.begin() + m_Selected);
                m_Selected = std::min(m_Selected, (int)m_Sheet.Sprites.size() - 1);
                MarkDirty();
            }
            ImGui::EndDisabled();
            UI::EndSection();
        }
    }
}
