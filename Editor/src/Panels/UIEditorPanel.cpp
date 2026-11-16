#include "UIEditorPanel.h"

#include "EditorState.h"
#include "Renderer/RHI/RenderCommand.h"
#include "Scene/Components.h"
#include "UI/UIComponents.h"
#include "UI/UISystem.h"
#include "UndoRedo/EntitySnapshotCommand.h"
#include "UndoRedo/UndoRedoManager.h"

#include <glad/glad.h>
#include <imgui.h>
#include <IconsFontAwesome6.h>

#include "stb_image_write.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace Conqueror::Editor
{
    namespace
    {
        struct Preset { const char* Name; int W, H; };
        const Preset kPresets[] = {
            { "1920 x 1080 (Full HD)", 1920, 1080 }, { "1280 x 720 (HD)", 1280, 720 }, { "2560 x 1440 (QHD)", 2560, 1440 },
            { "3840 x 2160 (4K)", 3840, 2160 }, { "1280 x 800 (Steam Deck)", 1280, 800 }, { "2340 x 1080 (Phone, landscape)", 2340, 1080 },
            { "1080 x 2340 (Phone, portrait)", 1080, 2340 }, { "2048 x 1536 (Tablet)", 2048, 1536 }, { "1024 x 768 (4:3)", 1024, 768 },
        };
        constexpr int kPresetCount = (int)(sizeof(kPresets) / sizeof(kPresets[0]));

        bool IsScreenCanvas(Entity e)
        {
            return e && e.HasComponent<CanvasComponent>() && e.GetComponent<CanvasComponent>().Mode != CanvasComponent::RenderMode::WorldSpace;
        }

        // Canvas'ın altındaki UI öğeleri, çizim sırasıyla (derinlik önce; sonraki üstte)
        void Collect(Entity e, std::vector<Entity>& out)
        {
            for (Entity c : e.GetChildren())
            {
                if (c.HasComponent<RectTransformComponent>())
                    out.push_back(c);
                Collect(c, out);
            }
        }
    }

    UIEditorPanel::UIEditorPanel()
    {
        FramebufferSpecification spec;
        spec.Width = (uint32_t)m_Resolution.x;
        spec.Height = (uint32_t)m_Resolution.y;
        spec.Attachments = { FramebufferTextureFormat::RGBA8, FramebufferTextureFormat::Depth };
        m_Framebuffer = Framebuffer::Create(spec);
    }

    Entity UIEditorPanel::GetCanvasEntity() const
    {
        if (!m_Context || m_Canvas == 0)
            return {};
        Entity e = m_Context->FindEntityByUUID(m_Canvas);
        return IsScreenCanvas(e) ? e : Entity{};
    }

    bool UIEditorPanel::Open(Entity e)
    {
        // UI öğesinden yukarı doğru canvas'ı bul
        for (Entity p = e; p; p = p.GetParent())
            if (IsScreenCanvas(p))
            {
                m_Canvas = (uint64_t)p.GetComponent<IDComponent>().ID;
                m_Open = true;
                // Canvas Scaler'ın referans çözünürlüğü tasarım boyutudur (Unity / UMG gibi)
                if (p.HasComponent<CanvasScalerComponent>())
                {
                    const auto& cs = p.GetComponent<CanvasScalerComponent>();
                    if (cs.ScaleMode == CanvasScalerComponent::UIScaleMode::ScaleWithScreenSize && cs.ReferenceResolution.x > 0 && cs.ReferenceResolution.y > 0)
                        SetResolution((int)cs.ReferenceResolution.x, (int)cs.ReferenceResolution.y);
                }
                m_Fit = true;
                ImGui::SetWindowFocus("###UI Editor");
                return true;
            }
        return false;
    }

    void UIEditorPanel::SetResolution(int width, int height)
    {
        m_Resolution = { std::clamp(width, 64, 4096), std::clamp(height, 64, 4096) };
        m_Preset = -1;
        for (int i = 0; i < kPresetCount; ++i)
            if (kPresets[i].W == m_Resolution.x && kPresets[i].H == m_Resolution.y)
                m_Preset = i;
    }

    float UIEditorPanel::GetScaleFactor() const
    {
        Entity canvas = GetCanvasEntity();
        return canvas ? UISystem::GetCanvasScaleFactor(canvas, glm::vec2(m_Resolution)) * m_Dpi : 1.0f;
    }

    void UIEditorPanel::OnRender()
    {
        Entity canvas = GetCanvasEntity();
        if (!m_Open || !m_Visible || !canvas || m_Tab != 0)
            return;
        const auto& spec = m_Framebuffer->GetSpecification();
        if ((int)spec.Width != m_Resolution.x || (int)spec.Height != m_Resolution.y)
            m_Framebuffer->Resize((uint32_t)m_Resolution.x, (uint32_t)m_Resolution.y);
        m_Framebuffer->Bind();
        RenderCommand::SetViewport(0, 0, (uint32_t)m_Resolution.x, (uint32_t)m_Resolution.y);
        RenderCommand::SetClearColor({ 0.18f, 0.19f, 0.22f, 1.0f });
        RenderCommand::Clear();
        // Yalnız bu canvas; DPI ölçeği tasarım boyutunu küçültür (aynı piksel, daha yüksek yoğunluk önizlemesi)
        const glm::vec2 size = glm::vec2(m_Resolution) / m_Dpi;
        UISystem::SetSoloCanvas(m_Canvas);
        UISystem::Update(*m_Context, size, UIInputState{}, 0.0f);
        glViewport(0, 0, m_Resolution.x, m_Resolution.y);
        UISystem::RenderScreenSpace(*m_Context, size);
        UISystem::SetSoloCanvas(0);
        m_Framebuffer->Unbind();
        SnapshotRects();
    }

    void UIEditorPanel::SnapshotRects()
    {
        m_Rects.clear();
        Entity canvas = GetCanvasEntity();
        if (!canvas)
            return;
        const float s = UISystem::GetCanvasScaleFactor(canvas, glm::vec2(m_Resolution) / m_Dpi) * m_Dpi;
        std::vector<Entity> elements;
        Collect(canvas, elements);
        // ComputedRect ebeveyninin dikdörtgenine göreli (sol-alt köken): canvas pikseli = ebeveyn zinciri boyunca toplam
        std::unordered_map<uint64_t, glm::vec2> origin;
        for (Entity e : elements)   // Collect ebeveyni çocuktan önce verir
        {
            const glm::vec4 r = e.GetComponent<RectTransformComponent>().ComputedRect;
            glm::vec2 base(0.0f);
            Entity parent = e.GetParent();
            if (parent && parent != canvas)
                if (auto it = origin.find((uint64_t)parent.GetComponent<IDComponent>().ID); it != origin.end())
                    base = it->second;
            const glm::vec2 o = base + glm::vec2(r.x, r.y);
            const uint64_t id = (uint64_t)e.GetComponent<IDComponent>().ID;
            origin[id] = o;
            m_Rects[id] = glm::vec4(o.x * s, o.y * s, r.z * s, r.w * s);
        }
    }

    void UIEditorPanel::Relayout()
    {
        if (!GetCanvasEntity())
            return;
        m_Context->UpdateWorldTransforms();
        UISystem::SetSoloCanvas(m_Canvas);
        UISystem::Update(*m_Context, glm::vec2(m_Resolution) / m_Dpi, UIInputState{}, 0.0f);
        UISystem::SetSoloCanvas(0);
        SnapshotRects();
    }

    glm::vec4 UIEditorPanel::ElementRect(Entity e) const
    {
        if (!e || !e.HasComponent<RectTransformComponent>())
            return glm::vec4(0.0f);
        auto it = m_Rects.find((uint64_t)e.GetComponent<IDComponent>().ID);
        return it != m_Rects.end() ? it->second : glm::vec4(0.0f);
    }

    glm::vec2 UIEditorPanel::ToScreen(const glm::vec2& p, const glm::vec2& imageMin) const
    {
        return { imageMin.x + p.x * m_Zoom, imageMin.y + ((float)m_Resolution.y - p.y) * m_Zoom };
    }

    glm::vec2 UIEditorPanel::ToCanvas(const glm::vec2& s, const glm::vec2& imageMin) const
    {
        return { (s.x - imageMin.x) / m_Zoom, (float)m_Resolution.y - (s.y - imageMin.y) / m_Zoom };
    }

    Entity UIEditorPanel::PickAt(const glm::vec2& p) const
    {
        Entity canvas = GetCanvasEntity();
        if (!canvas)
            return {};
        std::vector<Entity> elements;
        Collect(canvas, elements);
        Entity hit;
        for (Entity e : elements)
        {
            const auto& rt = e.GetComponent<RectTransformComponent>();
            if (!rt.Visible || !m_Context->IsEntityActive(e))
                continue;
            const glm::vec4 r = ElementRect(e);
            if (p.x >= r.x && p.x <= r.x + r.z && p.y >= r.y && p.y <= r.y + r.w)
                hit = e;   // sonraki (üstte çizilen) kazanır
        }
        return hit;
    }

    Entity UIEditorPanel::PickForClick(const glm::vec2& p) const
    {
        Entity hit = PickAt(p);
        if (!hit)
            return {};
        Entity canvas = GetCanvasEntity();
        Entity owner;
        for (Entity q = hit.GetParent(); q && q != canvas; q = q.GetParent())
            if (UISystem::IsSelectable(q))
                owner = q;
        Entity selected = EditorState::Get().GetSelectedEntity();
        if (owner && selected != owner && selected != hit)
            return owner;
        return hit;
    }

    void UIEditorPanel::BeginEdit(Entity e)
    {
        m_EditBefore = EntitySnapshotCommand::Capture(e);
        m_EditEntity = (uint64_t)e.GetComponent<IDComponent>().ID;
    }

    void UIEditorPanel::EndEdit(Entity e, const char* label)
    {
        if (!m_EditBefore || !e)
            return;
        UndoRedoManager::Get().Execute(std::make_shared<EntitySnapshotCommand>(m_Context, m_EditEntity, m_EditBefore, EntitySnapshotCommand::Capture(e), label));
        m_EditBefore.reset();
    }

    bool UIEditorPanel::MoveSelected(const glm::vec2& delta)
    {
        Entity e = EditorState::Get().GetSelectedEntity();
        if (!e || !e.HasComponent<RectTransformComponent>() || e == GetCanvasEntity())
            return false;
        const float s = std::max(1e-4f, GetScaleFactor());
        e.GetComponent<RectTransformComponent>().AnchoredPosition += delta / s;
        return true;
    }

    bool UIEditorPanel::ResizeSelected(int handle, const glm::vec2& delta)
    {
        Entity e = EditorState::Get().GetSelectedEntity();
        if (!e || !e.HasComponent<RectTransformComponent>() || e == GetCanvasEntity() || handle < 0 || handle > 7)
            return false;
        auto& rt = e.GetComponent<RectTransformComponent>();
        const glm::vec2 d = delta / std::max(1e-4f, GetScaleFactor());
        // Kulp düzeni: 0 sol-alt, 1 alt, 2 sağ-alt, 3 sağ, 4 sağ-üst, 5 üst, 6 sol-üst, 7 sol
        const bool left = handle == 0 || handle == 6 || handle == 7, right = handle == 2 || handle == 3 || handle == 4;
        const bool bottom = handle == 0 || handle == 1 || handle == 2, top = handle == 4 || handle == 5 || handle == 6;
        if (left) { rt.SizeDelta.x -= d.x; rt.AnchoredPosition.x += d.x * (1.0f - rt.Pivot.x); }
        if (right) { rt.SizeDelta.x += d.x; rt.AnchoredPosition.x += d.x * rt.Pivot.x; }
        if (bottom) { rt.SizeDelta.y -= d.y; rt.AnchoredPosition.y += d.y * (1.0f - rt.Pivot.y); }
        if (top) { rt.SizeDelta.y += d.y; rt.AnchoredPosition.y += d.y * rt.Pivot.y; }
        return true;
    }

    Entity UIEditorPanel::AddElement(const std::string& type, const glm::vec2& p)
    {
        Entity canvas = GetCanvasEntity();
        if (!canvas)
            return {};
        // Ebeveyn: seçili kap (Panel, Image, Layout Group, Scroll View içeriği); seçili öğe bir denetimse (düğme, yazı ...) onun kardeşi
        // olur (Unreal UMG gibi). Canvas dışında bir şey seçiliyse canvas
        Entity parent = EditorState::Get().GetSelectedEntity();
        bool underCanvas = false;
        for (Entity q = parent; q; q = q.GetParent())
            if (q == canvas)
                underCanvas = true;
        if (!underCanvas)
            parent = canvas;
        else if (parent != canvas && (parent.HasComponent<UIButtonComponent>() || parent.HasComponent<UITextComponent>() ||
                                      parent.HasComponent<UIToggleComponent>() || parent.HasComponent<UISliderComponent>() ||
                                      parent.HasComponent<UIInputFieldComponent>() || parent.HasComponent<UIDropdownComponent>() ||
                                      parent.HasComponent<UIScrollbarComponent>()))
            parent = parent.GetParent() ? parent.GetParent() : canvas;
        Entity e = UISystem::CreateElement(*m_Context, type, parent);
        if (!e)
            return {};
        if (p.x >= 0.0f && e.HasComponent<RectTransformComponent>())
        {
            // Bırakılan noktaya: yeni öğenin tasarım yerleşimindeki yerinden kaydırılır
            Relayout();
            auto& rt = e.GetComponent<RectTransformComponent>();
            const glm::vec4 r = ElementRect(e);
            const glm::vec2 center(r.x + r.z * rt.Pivot.x, r.y + r.w * rt.Pivot.y);
            rt.AnchoredPosition += (p - center) / std::max(1e-4f, GetScaleFactor());
            Relayout();
        }
        // Geri alma: editörün oluşturma algılayıcısı (komutsuz oluşturulan varlıklar) kaydeder
        if (OnSelect)
            OnSelect(e);
        return e;
    }

    bool UIEditorPanel::SaveImage(const std::string& path) const
    {
        m_Framebuffer->Bind();
        std::vector<unsigned char> px((size_t)m_Resolution.x * m_Resolution.y * 4);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, m_Resolution.x, m_Resolution.y, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
        m_Framebuffer->Unbind();
        stbi_flip_vertically_on_write(1);
        return stbi_write_png(path.c_str(), m_Resolution.x, m_Resolution.y, 4, px.data(), m_Resolution.x * 4) != 0;
    }

    void UIEditorPanel::DrawToolbar()
    {
        // Canvas seçici: sahnedeki ekran üstü canvas'lar
        Entity canvas = GetCanvasEntity();
        ImGui::SetNextItemWidth(200.0f);
        const std::string current = canvas ? canvas.GetComponent<TagComponent>().Tag : std::string("(no canvas)");
        if (ImGui::BeginCombo("##uicanvas", (ICON_FA_TABLE_CELLS_LARGE "  " + current).c_str()))
        {
            auto view = m_Context->m_Registry.view<CanvasComponent>();
            for (auto h : view)
            {
                Entity c{ h, m_Context.get() };
                if (!IsScreenCanvas(c))
                    continue;
                const uint64_t id = (uint64_t)c.GetComponent<IDComponent>().ID;
                if (ImGui::Selectable((c.GetComponent<TagComponent>().Tag + "##" + std::to_string(id)).c_str(), id == m_Canvas))
                    Open(c);
            }
            if (ImGui::Selectable(ICON_FA_PLUS "  New Canvas"))
                Open(UISystem::CreateCanvas(*m_Context, "Canvas"));
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(210.0f);
        const std::string res = std::to_string(m_Resolution.x) + " x " + std::to_string(m_Resolution.y);
        if (ImGui::BeginCombo("##uires", (ICON_FA_DISPLAY "  " + (m_Preset >= 0 ? std::string(kPresets[m_Preset].Name) : res)).c_str()))
        {
            for (int i = 0; i < kPresetCount; ++i)
                if (ImGui::Selectable(kPresets[i].Name, i == m_Preset))
                {
                    SetResolution(kPresets[i].W, kPresets[i].H);
                    m_Fit = true;
                }
            ImGui::Separator();
            int wh[2] = { m_Resolution.x, m_Resolution.y };
            ImGui::SetNextItemWidth(150.0f);
            if (ImGui::InputInt2("Custom", wh, ImGuiInputTextFlags_EnterReturnsTrue))
                SetResolution(wh[0], wh[1]);
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("Design resolution (Canvas Scaler reference resolution when it scales with the screen)");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(90.0f);
        static const float dpis[] = { 1.0f, 1.25f, 1.5f, 2.0f, 3.0f };
        char dpiLabel[16];
        std::snprintf(dpiLabel, sizeof(dpiLabel), "%.0f%% DPI", m_Dpi * 100.0f);
        if (ImGui::BeginCombo("##uidpi", dpiLabel))
        {
            for (float d : dpis)
            {
                char l[16];
                std::snprintf(l, sizeof(l), "%.0f%%", d * 100.0f);
                if (ImGui::Selectable(l, d == m_Dpi))
                    m_Dpi = d;
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(140.0f);
        static const char* safe[] = { "No safe zone", "TV title safe (90%)", "Phone notch" };
        ImGui::Combo("##uisafe", &m_SafeZone, safe, 3);
        ImGui::SameLine();
        ImGui::Checkbox("Anchors", &m_ShowAnchors);
        ImGui::SameLine();
        ImGui::Checkbox("Navigation", &m_ShowNavigation);
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("Show where keyboard / gamepad navigation goes from each selectable (Unity: Visualize)");
        ImGui::SameLine();
        if (ImGui::SmallButton(ICON_FA_EXPAND " Fit"))
            m_Fit = true;
        ImGui::SameLine();
        ImGui::TextDisabled("%.0f%%", m_Zoom * 100.0f);
    }

    void UIEditorPanel::DrawNavigation(ImDrawList* dl, const glm::vec2& imageMin)
    {
        // Her seçilebilir öğeden dört yöndeki komşusuna ok (UISystem'in uzamsal kuralı / açık hedefler)
        Entity canvas = GetCanvasEntity();
        std::vector<Entity> elements;
        Collect(canvas, elements);
        std::vector<std::pair<Entity, glm::vec2>> selectables;
        for (Entity e : elements)
            if (UISystem::IsSelectable(e) && e.GetComponent<RectTransformComponent>().Visible)
            {
                const glm::vec4 r = ElementRect(e);
                selectables.push_back({ e, glm::vec2(r.x + r.z * 0.5f, r.y + r.w * 0.5f) });
            }
        const ImU32 col = IM_COL32(250, 200, 60, 220);
        for (const auto& [e, c] : selectables)
            for (const glm::ivec2 dir : { glm::ivec2(1, 0), glm::ivec2(-1, 0), glm::ivec2(0, 1), glm::ivec2(0, -1) })
            {
                Entity n = UISystem::NavigationTarget(*m_Context, e, dir);
                if (!n)
                    continue;
                const glm::vec4 r = ElementRect(n);
                const glm::vec2 to(r.x + r.z * 0.5f, r.y + r.w * 0.5f);
                // Çift yönlü oklar üst üste binmesin: hafif yana kaydır
                const glm::vec2 side = glm::vec2(-(float)dir.y, (float)dir.x) * 4.0f;
                const glm::vec2 a = ToScreen(c, imageMin) + side, b = ToScreen(to, imageMin) + side;
                dl->AddLine(ImVec2(a.x, a.y), ImVec2(b.x, b.y), col, 1.5f);
                const glm::vec2 d = glm::normalize(b - a + glm::vec2(1e-4f));
                const glm::vec2 p(-d.y, d.x);
                dl->AddTriangleFilled(ImVec2(b.x, b.y), ImVec2(b.x - d.x * 9 + p.x * 4, b.y - d.y * 9 + p.y * 4),
                                      ImVec2(b.x - d.x * 9 - p.x * 4, b.y - d.y * 9 - p.y * 4), col);
            }
    }

    void UIEditorPanel::DrawOverlays(ImDrawList* dl, const glm::vec2& imageMin)
    {
        const glm::vec2 a = ToScreen({ 0.0f, (float)m_Resolution.y }, imageMin), b = ToScreen({ (float)m_Resolution.x, 0.0f }, imageMin);
        dl->AddRect(ImVec2(a.x, a.y), ImVec2(b.x, b.y), IM_COL32(120, 125, 140, 255));
        // Güvenli alan
        if (m_SafeZone > 0)
        {
            glm::vec4 inset(0.0f);   // sol, alt, sağ, üst (tasarım pikseli)
            if (m_SafeZone == 1)
                inset = glm::vec4(m_Resolution.x, m_Resolution.y, m_Resolution.x, m_Resolution.y) * 0.05f;
            else
            {
                const bool landscape = m_Resolution.x >= m_Resolution.y;
                const float notch = (float)std::min(m_Resolution.x, m_Resolution.y) * 0.06f;
                inset = landscape ? glm::vec4(notch, notch * 0.4f, notch, 0.0f) : glm::vec4(0.0f, notch * 0.6f, 0.0f, notch * 1.6f);
            }
            const glm::vec2 sa = ToScreen({ inset.x, (float)m_Resolution.y - inset.w }, imageMin);
            const glm::vec2 sb = ToScreen({ (float)m_Resolution.x - inset.z, inset.y }, imageMin);
            dl->AddRect(ImVec2(sa.x, sa.y), ImVec2(sb.x, sb.y), IM_COL32(80, 200, 120, 200), 0.0f, 0, 1.5f);
        }
        Entity selected = EditorState::Get().GetSelectedEntity();
        Entity canvas = GetCanvasEntity();
        bool inCanvas = false;
        for (Entity q = selected; q; q = q.GetParent())
            inCanvas |= q == canvas;
        if (!selected || !inCanvas || selected == canvas || !selected.HasComponent<RectTransformComponent>())
            return;
        const glm::vec4 r = ElementRect(selected);
        const glm::vec2 ra = ToScreen({ r.x, r.y + r.w }, imageMin), rb = ToScreen({ r.x + r.z, r.y }, imageMin);
        dl->AddRect(ImVec2(ra.x, ra.y), ImVec2(rb.x, rb.y), IM_COL32(70, 150, 255, 255), 0.0f, 0, 2.0f);
        // Kulplar
        const glm::vec2 hs[8] = { { r.x, r.y }, { r.x + r.z * 0.5f, r.y }, { r.x + r.z, r.y }, { r.x + r.z, r.y + r.w * 0.5f },
                                  { r.x + r.z, r.y + r.w }, { r.x + r.z * 0.5f, r.y + r.w }, { r.x, r.y + r.w }, { r.x, r.y + r.w * 0.5f } };
        for (const glm::vec2& h : hs)
        {
            const glm::vec2 s = ToScreen(h, imageMin);
            dl->AddRectFilled(ImVec2(s.x - 4, s.y - 4), ImVec2(s.x + 4, s.y + 4), IM_COL32(255, 255, 255, 255));
            dl->AddRect(ImVec2(s.x - 4, s.y - 4), ImVec2(s.x + 4, s.y + 4), IM_COL32(70, 150, 255, 255));
        }
        // Çapalar: ebeveyn dikdörtgeninde AnchorMin / AnchorMax (Unity'nin çapa çiçeği)
        Entity parent = selected.GetParent();
        if (m_ShowAnchors && parent && parent.HasComponent<RectTransformComponent>())
        {
            const auto& rt = selected.GetComponent<RectTransformComponent>();
            const glm::vec4 pr = parent == canvas ? glm::vec4(0, 0, (float)m_Resolution.x, (float)m_Resolution.y) : ElementRect(parent);
            const ImU32 col = IM_COL32(255, 255, 255, 230);
            for (const glm::vec2 an : { rt.AnchorMin, glm::vec2(rt.AnchorMax.x, rt.AnchorMin.y), rt.AnchorMax, glm::vec2(rt.AnchorMin.x, rt.AnchorMax.y) })
            {
                const glm::vec2 s = ToScreen({ pr.x + pr.z * an.x, pr.y + pr.w * an.y }, imageMin);
                const float dx = an.x <= 0.5f ? -1.0f : 1.0f, dy = an.y <= 0.5f ? 1.0f : -1.0f;   // ekran y aşağı
                dl->AddTriangleFilled(ImVec2(s.x, s.y), ImVec2(s.x + dx * 9, s.y), ImVec2(s.x, s.y + dy * 9), col);
            }
        }
    }

    void UIEditorPanel::DrawDesigner()
    {
        Entity canvas = GetCanvasEntity();
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        ImGui::BeginChild("##uidesign", avail, ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const ImVec2 size = ImGui::GetContentRegionAvail();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y), IM_COL32(24, 25, 29, 255));
        if (!canvas)
        {
            ImGui::SetCursorScreenPos(ImVec2(origin.x + 20, origin.y + 20));
            ImGui::TextDisabled("Open a Screen Space canvas: double-click it in the Hierarchy, use the canvas list above or");
            ImGui::SetCursorScreenPos(ImVec2(origin.x + 20, origin.y + 40));
            if (ImGui::Button(ICON_FA_PLUS "  Create a Canvas"))
                Open(UISystem::CreateCanvas(*m_Context, "Canvas"));
            ImGui::EndChild();
            return;
        }
        if (m_Fit && size.x > 20 && size.y > 20)
        {
            m_Zoom = std::min((size.x - 40.0f) / (float)m_Resolution.x, (size.y - 40.0f) / (float)m_Resolution.y);
            m_Pan = glm::vec2(0.0f);
            m_Fit = false;
        }
        const glm::vec2 imageSize = glm::vec2(m_Resolution) * m_Zoom;
        const glm::vec2 imageMin = glm::vec2(origin.x + (size.x - imageSize.x) * 0.5f, origin.y + (size.y - imageSize.y) * 0.5f) + m_Pan;
        ImGui::SetCursorScreenPos(origin);
        ImGui::InvisibleButton("##uisurface", size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
        const bool hovered = ImGui::IsItemHovered();
        dl->AddImage((ImTextureID)(uint64_t)m_Framebuffer->GetColorAttachmentRendererID(0), ImVec2(imageMin.x, imageMin.y),
                     ImVec2(imageMin.x + imageSize.x, imageMin.y + imageSize.y), ImVec2(0, 1), ImVec2(1, 0));
        dl->PushClipRect(origin, ImVec2(origin.x + size.x, origin.y + size.y), true);
        DrawOverlays(dl, imageMin);
        if (m_ShowNavigation)
            DrawNavigation(dl, imageMin);
        // Fareyle üzerindeki öğe (ince çerçeve)
        const ImGuiIO& io = ImGui::GetIO();
        const glm::vec2 mouse(io.MousePos.x, io.MousePos.y);
        const glm::vec2 cp = ToCanvas(mouse, imageMin);
        if (hovered && m_DragHandle == -2)
            if (Entity h = PickAt(cp))
            {
                const glm::vec4 r = ElementRect(h);
                const glm::vec2 a = ToScreen({ r.x, r.y + r.w }, imageMin), b = ToScreen({ r.x + r.z, r.y }, imageMin);
                dl->AddRect(ImVec2(a.x, a.y), ImVec2(b.x, b.y), IM_COL32(150, 190, 255, 140));
            }
        dl->PopClipRect();

        // Yakınlaştırma (imlecin altındaki nokta yerinde kalır) ve kaydırma (orta / sağ sürükleme)
        if (hovered && io.MouseWheel != 0.0f)
        {
            const float old = m_Zoom;
            m_Zoom = std::clamp(m_Zoom * (io.MouseWheel > 0 ? 1.15f : 1.0f / 1.15f), 0.05f, 8.0f);
            const glm::vec2 centerMin = glm::vec2(origin.x + (size.x - m_Resolution.x * m_Zoom) * 0.5f, origin.y + (size.y - m_Resolution.y * m_Zoom) * 0.5f);
            const glm::vec2 keep = (mouse - imageMin) / old;   // görüntü pikseli
            m_Pan = mouse - keep * m_Zoom - centerMin;
        }
        if (ImGui::IsItemActive() && (ImGui::IsMouseDragging(ImGuiMouseButton_Middle) || ImGui::IsMouseDragging(ImGuiMouseButton_Right)))
            m_Pan += glm::vec2(io.MouseDelta.x, io.MouseDelta.y);

        // Seçme / taşıma / boyutlandırma (sol)
        Entity selected = EditorState::Get().GetSelectedEntity();
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        {
            m_DragHandle = -2;
            if (selected && selected.HasComponent<RectTransformComponent>() && selected != canvas)
            {
                const glm::vec4 r = ElementRect(selected);
                const glm::vec2 hs[8] = { { r.x, r.y }, { r.x + r.z * 0.5f, r.y }, { r.x + r.z, r.y }, { r.x + r.z, r.y + r.w * 0.5f },
                                          { r.x + r.z, r.y + r.w }, { r.x + r.z * 0.5f, r.y + r.w }, { r.x, r.y + r.w }, { r.x, r.y + r.w * 0.5f } };
                for (int i = 0; i < 8; ++i)
                {
                    const glm::vec2 s = ToScreen(hs[i], imageMin);
                    if (std::fabs(s.x - mouse.x) <= 6.0f && std::fabs(s.y - mouse.y) <= 6.0f)
                        m_DragHandle = i;
                }
            }
            if (m_DragHandle == -2)
            {
                Entity hit = PickForClick(cp);
                if (hit && OnSelect)
                    OnSelect(hit);
                else if (!hit && OnSelect)
                    OnSelect(canvas);
                if (hit)
                    m_DragHandle = -1;
            }
            if (m_DragHandle != -2)
            {
                BeginEdit(EditorState::Get().GetSelectedEntity());
                m_DragLast = cp;
            }
        }
        if (m_DragHandle != -2 && ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            const glm::vec2 d = cp - m_DragLast;
            if (d != glm::vec2(0.0f))
            {
                if (m_DragHandle == -1)
                    MoveSelected(d);
                else
                    ResizeSelected(m_DragHandle, d);
                m_DragLast = cp;
            }
        }
        if (m_DragHandle != -2 && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            EndEdit(EditorState::Get().GetSelectedEntity(), m_DragHandle == -1 ? "Move UI Element" : "Resize UI Element");
            m_DragHandle = -2;
        }
        // Klavye: oklar 1 piksel (Shift: 10), Delete siler, F sığdırır
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !io.WantTextInput && selected && selected != canvas &&
            selected.HasComponent<RectTransformComponent>())
        {
            const float step = io.KeyShift ? 10.0f : 1.0f;
            glm::vec2 nudge(0.0f);
            if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) nudge.x -= step;
            if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) nudge.x += step;
            if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) nudge.y += step;
            if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) nudge.y -= step;
            if (nudge != glm::vec2(0.0f))
            {
                BeginEdit(selected);
                MoveSelected(nudge);
                EndEdit(selected, "Nudge UI Element");
            }
            if (ImGui::IsKeyPressed(ImGuiKey_Delete) && OnDeleteSelected)
                OnDeleteSelected();
        }
        if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_F))
            m_Fit = true;

        // Palette'ten bırakma
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("UI_PALETTE_ITEM"))
                AddElement(std::string((const char*)payload->Data, (size_t)payload->DataSize - 1), cp);
            ImGui::EndDragDropTarget();
        }
        ImGui::EndChild();
    }

    void UIEditorPanel::OnImGuiRender()
    {
        if (!m_Open)
        {
            m_Visible = false;
            return;
        }
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4, 4));
        m_Visible = ImGui::Begin(ICON_FA_OBJECT_GROUP " UI Editor###UI Editor", &m_Open);
        ImGui::PopStyleVar();
        if (!m_Visible || !m_Context)
        {
            ImGui::End();
            return;
        }
        // Sekmeler: Designer | Graph | Animations (Unreal UMG)
        if (ImGui::BeginTabBar("##uitabs"))
        {
            if (ImGui::BeginTabItem(ICON_FA_PEN_RULER " Designer")) { m_Tab = 0; ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem(ICON_FA_DIAGRAM_PROJECT " Graph")) { m_Tab = 1; ImGui::EndTabItem(); }
            if (ImGui::BeginTabItem(ICON_FA_FILM " Animations")) { m_Tab = 2; ImGui::EndTabItem(); }
            ImGui::EndTabBar();
        }
        Entity canvas = GetCanvasEntity();
        if (m_Tab == 0)
        {
            DrawToolbar();
            DrawDesigner();
        }
        else if (m_Tab == 1)
        {
            ImGui::TextWrapped("The canvas's behaviour: its Visual Script graph (button clicks, value changes, bindings).");
            if (ImGui::Button(ICON_FA_DIAGRAM_PROJECT "  Open the canvas graph") && canvas && OnOpenGraph)
                OnOpenGraph(canvas);
        }
        else
        {
            ImGui::TextWrapped("UI animations are Timeline / Animation clips on the canvas or its elements (fade, slide, scale).");
            if (ImGui::Button(ICON_FA_FILM "  Open the Timeline") && OnOpenAnimations)
                OnOpenAnimations();
        }
        ImGui::End();
    }
}
