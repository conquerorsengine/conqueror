#include "ViewportPanel.h"
#include "Core/Math/Math.h"
#include "StatsPanel.h"
#include "GameViewSettings.h"
#include "Renderer/RHI/RenderCommand.h"
#include "Renderer/PostProcess.h"
#include "Renderer/PathTracer.h"
#include "Renderer/EditorGrid.h"
#include "Renderer/Utilities/Renderer3D/Renderer3D.h"
#include "Core/Audio/AudioEngine.h"
#include "Core/Time/TimeManager.h"
#include "Core/Tiering/QualitySettings.h"
#include "Scene/Scene.h"
#include "Scene/PostProcessVolume.h"
#include "Scene/ScenePicking.h"
#include "UI/UISystem.h"
#include "Scene/Components.h"
#include "EditorState.h"
#include "UI/EditorUI.h"
#include "AssetActions.h"
#include "Scripting/ScriptRuntime.h"

#include <imgui.h>
#include <ImGuizmo.h>
#include <IconsFontAwesome6.h>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cfloat>
#include <cmath>

namespace Conqueror::Editor
{
    namespace
    {
        constexpr float kToolbarHeight = 30.0f;

        // Araç çubuğu düğmesi: etkinse vurgu renginde
        bool ToolbarButton(const char* label, bool active, const char* tooltip, float width = 0.0f)
        {
            const ImVec4* colors = ImGui::GetStyle().Colors;
            if (active)
            {
                ImGui::PushStyleColor(ImGuiCol_Button, UI::Colors::AccentDim);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, UI::Colors::Accent);
            }
            else
            {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, colors[ImGuiCol_ButtonHovered]);
            }
            bool pressed = ImGui::Button(label, ImVec2(width, kToolbarHeight - 6.0f));
            ImGui::PopStyleColor(2);
            if (tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                ImGui::SetTooltip("%s", tooltip);
            return pressed;
        }

        // Açılır ok (yanındaki düğmenin ayarları)
        bool DropdownArrow(const char* id, const char* tooltip)
        {
            ImGui::SameLine(0.0f, 0.0f);
            ImGui::PushID(id);
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(3.0f, ImGui::GetStyle().FramePadding.y));
            bool pressed = ImGui::Button(ICON_FA_CARET_DOWN, ImVec2(0.0f, kToolbarHeight - 6.0f));
            ImGui::PopStyleVar();
            ImGui::PopStyleColor();
            ImGui::PopID();
            if (tooltip && ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                ImGui::SetTooltip("%s", tooltip);
            return pressed;
        }

        void ToolbarSeparator()
        {
            ImGui::SameLine();
            ImVec2 p = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddLine(ImVec2(p.x, p.y + 5.0f), ImVec2(p.x, p.y + kToolbarHeight - 11.0f),
                                                ImGui::GetColorU32(ImGuiCol_Separator));
            ImGui::Dummy(ImVec2(1.0f, 1.0f));
            ImGui::SameLine();
        }

        const char* ShadingIcon(ViewportShading s)
        {
            switch (s)
            {
                case ViewportShading::Wireframe: return ICON_FA_DRAW_POLYGON;
                case ViewportShading::Unlit: return ICON_FA_LIGHTBULB;
                case ViewportShading::Normals: return ICON_FA_ARROWS_UP_TO_LINE;
                case ViewportShading::LightingOnly: return ICON_FA_SUN;
                case ViewportShading::MetallicRoughness: return ICON_FA_GEM;
                case ViewportShading::AmbientOcclusion: return ICON_FA_CIRCLE_HALF_STROKE;
                case ViewportShading::PathTracing: return ICON_FA_WAND_MAGIC_SPARKLES;
                default: return ICON_FA_CUBE;
            }
        }
    }

    const char* ViewportPanel::GetShadingName(ViewportShading shading)
    {
        switch (shading)
        {
            case ViewportShading::Shaded: return "Shaded";
            case ViewportShading::Wireframe: return "Wireframe";
            case ViewportShading::Unlit: return "Unlit";
            case ViewportShading::Normals: return "Normals";
            case ViewportShading::LightingOnly: return "Lighting Only";
            case ViewportShading::MetallicRoughness: return "Metallic / Roughness";
            case ViewportShading::AmbientOcclusion: return "Ambient Occlusion";
            case ViewportShading::PathTracing: return "Path Tracing";
            default: return "?";
        }
    }

    ViewportPanel::ViewportPanel()
    {
        FramebufferSpecification fbSpec;
        fbSpec.Width = 1280;
        fbSpec.Height = 720;
        fbSpec.Attachments = { FramebufferTextureFormat::RGBA8, FramebufferTextureFormat::RED_INTEGER, FramebufferTextureFormat::Depth };
        m_Framebuffer = Framebuffer::Create(fbSpec);
    }

    void ViewportPanel::ApplyViewportSize(const glm::vec2& size)
    {
        m_ViewportSize = size;
        m_Framebuffer->Resize((uint32_t)size.x, (uint32_t)size.y);
        m_EditorCamera.SetViewportSize(size.x, size.y);
        if (m_Context)
            m_Context->OnViewportResize((uint32_t)size.x, (uint32_t)size.y);
    }

    void ViewportPanel::OnUpdate(Timestep ts)
    {
        // Panel boyutu değiştiyse hedefler çizimden hemen önce boyutlanır: gösterilen kare hiç boş (gri) kalmaz
        if (m_PendingViewportSize.x > 0.0f && m_PendingViewportSize.y > 0.0f && m_PendingViewportSize != m_ViewportSize)
            ApplyViewportSize(m_PendingViewportSize);

        // Gizmo kullanılıyorsa, viewport odakta değilse veya ImGui text girişi aktifse kamera güncellenmez (yazı yazarken kamera
        // kaçmasın). Oyun çalışırken de WASD/QE ile gezilir: klavye oyuna yalnız Game görünümü odaktayken gider.
        const bool wantText = ImGui::GetIO().WantTextInput;
        // Tekerlek: yalnız fare görünümün üstündeyken (odak başka paneldeyken de; Inspector'da kaydırınca kamera oynamaz)
        m_EditorCamera.SetScrollEnabled(m_ViewportHovered);
        if (m_ViewportFocused && !ImGuizmo::IsUsing() && !ImGuizmo::IsUsingViewManipulate() && !wantText)
            m_EditorCamera.OnUpdate(ts, true);
        else if (m_ViewportHovered && !wantText)
            m_EditorCamera.UpdateScrollOnly();

        if (!m_Framebuffer || m_ViewportSize.x <= 0 || m_ViewportSize.y <= 0)
            return;

        const uint32_t width = static_cast<uint32_t>(m_ViewportSize.x);
        const uint32_t height = static_cast<uint32_t>(m_ViewportSize.y);

        // 1. Sahneyi bu görünümün HDR hedefine çiz (görünüme özel çizim modu ile)
        m_PostStack.BeginScene(width, height);
        // HDR hattı doğrusal (tam sRGB eğrisi; çıkışta LinearToSRGB ile birebir geri döner)
        // 2D görünümde Unity Scene görünümü gibi düz gri arka plan; 3D'de ortamın arka plan rengi (gökyüzü üstüne çizilir)
        // Skybox kapalıyken 3D'de de aynı Unity Scene grisi (eskiden neredeyse siyah kalıyordu)
        glm::vec3 bgSrgb = (m_EditorCamera.Is2D() || !m_ViewOptions.Skybox) ? Scene::EditorBackground2D()
                         : (m_Context ? m_Context->GetRenderSettings().Environment.BackgroundColor : glm::vec3(0.1f));
        glm::vec3 bg = Math::SRGBToLinear(bgSrgb);
        RenderCommand::SetClearColor({ bg.r, bg.g, bg.b, 1.0f });
        RenderCommand::Clear();

        if (m_Context)
        {
            Renderer3D::DebugView debugView = Renderer3D::DebugView::Shaded;
            switch (m_Shading)
            {
                case ViewportShading::Unlit: debugView = Renderer3D::DebugView::Unlit; break;
                case ViewportShading::Normals: debugView = Renderer3D::DebugView::Normals; break;
                case ViewportShading::LightingOnly: debugView = Renderer3D::DebugView::LightingOnly; break;
                case ViewportShading::MetallicRoughness: debugView = Renderer3D::DebugView::MetallicRoughness; break;
                case ViewportShading::AmbientOcclusion: debugView = Renderer3D::DebugView::AmbientOcclusion; break;
                default: break;
            }
            Renderer3D::SetDebugView(debugView);
            Renderer3D::SetWireframe(m_Shading == ViewportShading::Wireframe);
            {
                // Izgara, gizmo ve gösterim seçenekleri sahneyle birlikte TAA'dan geçer: kapatılan ızgara / gizmo çizgisi geçmişte kalıp
                // bir süre yavaşça siliniyordu. Seçenek değişince zamansal geçmiş yeniden başlar (Realtime görüntüyü değiştirmez)
                const EditorViewOptions& a = m_AppliedViewOptions;
                const EditorViewOptions& b = m_ViewOptions;
                const bool changed = a.Grid != b.Grid || a.Gizmos != b.Gizmos || a.Skybox != b.Skybox || a.Fog != b.Fog || a.Shadows != b.Shadows
                                     || a.PostEffects != b.PostEffects || a.Sprites2D != b.Sprites2D || a.UI != b.UI || a.Colliders != b.Colliders
                                     || a.AllColliders != b.AllColliders || m_AppliedShading != m_Shading;
                if (changed && m_ViewOptionsApplied)
                    m_PostStack.ResetTemporalHistory();
                m_AppliedViewOptions = m_ViewOptions;
                m_AppliedShading = m_Shading;
                m_ViewOptionsApplied = true;
            }
            m_Context->SetEditorViewOptions(m_ViewOptions);
            {
                std::vector<uint32_t> selection;
                for (auto& e : EditorState::Get().GetSelectedEntities())
                    if (e && e.GetScene() == m_Context.get())
                        selection.push_back((uint32_t)e);
                m_Context->SetEditorSelection(std::move(selection));
            }

            m_Context->OnUpdateEditor(ts, m_EditorCamera, GetViewportBounds());

            // Game görünümü etkilenmesin
            Renderer3D::SetDebugView(Renderer3D::DebugView::Shaded);
            Renderer3D::SetWireframe(false);
        }

        m_PostStack.EndScene();

        // Path Tracing: raster kare (ışıklar / ortam / dönüşümler bu karede güncellendi) ışın izlenmiş birikimle değiştirilir
        if (m_Context && m_Shading == ViewportShading::PathTracing && !m_EditorCamera.Is2D())
            PathTracer::Render(*m_Context, RenderCamera::FromEditorCamera(m_EditorCamera, width, height), m_PostStack.GetSceneColorTexture());

        // 2. Post-process (bloom, SSAO, ton eşleme, renk, AA) -> viewport framebuffer
        m_Framebuffer->Bind();
        RenderCommand::SetClearColor({ 0.1f, 0.1f, 0.1f, 1 });
        RenderCommand::Clear();
        m_Framebuffer->ClearAttachment(1, -1);

        // Post Process Volume'lar sahne görünümünde de (editör kamerasının konumuna göre) etkilidir
        PostProcessSettings post = m_Context ? PostProcessVolumes::Resolve(*m_Context, m_Context->GetRenderSettings().Post, m_EditorCamera.GetPosition()) : PostProcessSettings{};
        // Hata ayıklama görünümlerinde ton eşleme dışındaki efektler yanıltıcıdır
        if (!m_ViewOptions.PostEffects || (m_Shading != ViewportShading::Shaded && m_Shading != ViewportShading::Wireframe))
            post.Enabled = false;
        // Ton eşleme yalnızca saf 2D sahnede kapalı; görünen bir gökyüzü (HDR) her zaman ton eşlenir.
        // (Boş sahnede gökyüzünün güneşli kısmı kırpılıp beyaza patlıyordu.)
        const bool skyVisible = m_Context && !m_EditorCamera.Is2D() && m_ViewOptions.Skybox
                             && m_Context->GetRenderSettings().Environment.Sky != SkyMode::SolidColor;
        m_PostStack.Render(post, RenderCamera::FromEditorCamera(m_EditorCamera, width, height), TimeManager::GetTime(),
                           !m_Context || m_Context->HasRenderable3D() || skyVisible);

        // 3. Halo/Flare efekti (güneş ekranda ise)
        if (m_Context && m_ViewOptions.PostEffects && (m_Context->IsHaloEnabled() || m_Context->IsFlareEnabled()))
        {
            Entity sunSource = m_Context->GetSunSourceEntity();
            if (sunSource && sunSource.HasComponent<DirectionalLightComponent>())
            {
                auto& dirLight = sunSource.GetComponent<DirectionalLightComponent>();
                glm::vec3 lightDir = glm::normalize(dirLight.Direction);
                // Güneşe doğru, kameradan çok uzak bir nokta
                glm::vec3 sunPoint = m_EditorCamera.GetPosition() - lightDir * 500.0f;
                glm::vec4 clip = m_EditorCamera.GetViewProjection() * glm::vec4(sunPoint, 1.0f);

                glm::vec2 screenPos(0.5f);
                bool lightVisible = false;
                if (clip.w > 0.0f)
                {
                    glm::vec3 ndc = glm::vec3(clip) / clip.w;
                    screenPos = glm::vec2(ndc.x * 0.5f + 0.5f, ndc.y * 0.5f + 0.5f);
                    lightVisible = screenPos.x >= -0.5f && screenPos.x <= 1.5f && screenPos.y >= -0.5f && screenPos.y <= 1.5f;
                }

                m_PostStack.ApplyHaloFlare(screenPos, dirLight.Color, lightVisible,
                                           m_Context->IsHaloEnabled(), m_Context->GetHaloTexture(), m_Context->GetHaloStrength(),
                                           m_Context->IsFlareEnabled(), m_Context->GetFlareStrength(),
                                           m_Context->GetFlareElements());
            }
        }

        // 4. Işık / kamera simgeleri son görüntünün üstüne (Unity gibi): TAA'ya girmez, büyürken / dönerken iz bırakmaz
        if (m_Context)
        {
            RenderCommand::SetViewport(0, 0, width, height);
            m_Context->RenderEditorIcons(m_EditorCamera, GetViewportBounds());
        }

        m_Framebuffer->Unbind();
    }

    void ViewportPanel::OnImGuiRender()
    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{ 0, 0 });
        const bool visible = ImGui::Begin("Viewport", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::PopStyleVar();
        m_TabVisible = visible;

        if (!visible)
        {
            // Sekme gizli: kamera/seçim girdisi alma, render etme
            m_ViewportFocused = false;
            m_ViewportHovered = false;
            m_ImageHovered = false;
            m_ViewportSize = { 0.0f, 0.0f };
            ImGui::End();
            return;
        }

        m_ViewportFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

        DrawToolbar();

        // Görüntü alanı: araç çubuğunun altı
        ImVec2 imagePos = ImGui::GetCursorScreenPos();
        ImVec2 avail = ImGui::GetContentRegionAvail();
        glm::vec2 newViewportSize = { (float)(int)avail.x, (float)(int)avail.y };

        m_ViewportBounds[0] = { imagePos.x, imagePos.y };
        m_ViewportBounds[1] = { imagePos.x + newViewportSize.x, imagePos.y + newViewportSize.y };
        EditorState::Get().SetViewportBounds(m_ViewportBounds[0], m_ViewportBounds[1]);

        const bool validSize = newViewportSize.x > 0.0f && newViewportSize.y > 0.0f;
        if (validSize)
        {
            if (m_ViewportSize.x <= 0.0f || m_ViewportSize.y <= 0.0f)
            {
                // İlk kare: gösterilecek önceki görüntü yok
                ApplyViewportSize(newViewportSize);
                m_Framebuffer->Bind();
                RenderCommand::SetClearColor({ 0.1f, 0.1f, 0.1f, 1.0f });
                RenderCommand::Clear();
                m_Framebuffer->ClearAttachment(1, -1);
                m_Framebuffer->Unbind();
            }
            else
                m_PendingViewportSize = newViewportSize;   // OnUpdate çizimden önce uygular; bu karede önceki görüntü panele sığdırılır
        }

        if (m_ViewportSize.x > 0.0f && m_ViewportSize.y > 0.0f)
        {
            uint64_t textureID = m_Framebuffer->GetColorAttachmentRendererID(0);
            const glm::vec2 shownSize = validSize ? newViewportSize : m_ViewportSize;
            ImGui::Image(reinterpret_cast<void*>(textureID), ImVec2{ shownSize.x, shownSize.y }, ImVec2{ 0, 1 }, ImVec2{ 1, 0 });
            m_ImageHovered = ImGui::IsItemHovered();
            // Sağ tık da görünüme odaklanır (Unity Scene): basılı tutup WASD ile uçmak için önce sol tıklamak gerekmez
            if (m_ImageHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
                ImGui::SetWindowFocus();
            if (m_Shading == ViewportShading::PathTracing && !m_EditorCamera.Is2D())
            {
                // Birikim durumu (Unreal: örnek sayısı / üst sınır)
                const auto& pt = PathTracer::GetSettings();
                char info[160];
                const int spp = PathTracer::GetSampleCount();
                snprintf(info, sizeof(info), "Path Tracing  %d / %d samples%s  |  %d triangles  |  %d bounces", spp, pt.MaxSamples,
                         spp >= pt.MaxSamples ? " (converged)" : "", PathTracer::GetTriangleCount(), pt.MaxBounces);
                const ImVec2 mn = ImGui::GetItemRectMin(), mx = ImGui::GetItemRectMax();
                const ImVec2 sz = ImGui::CalcTextSize(info);
                const ImVec2 pos(mn.x + 10.0f, mx.y - sz.y - 12.0f);
                ImDrawList* dl = ImGui::GetWindowDrawList();
                dl->AddRectFilled(ImVec2(pos.x - 6.0f, pos.y - 4.0f), ImVec2(pos.x + sz.x + 6.0f, pos.y + sz.y + 4.0f), IM_COL32(0, 0, 0, 150), 4.0f);
                dl->AddText(pos, IM_COL32(235, 235, 235, 255), info);
            }
            // İçerik Tarayıcısından sahneye bırak: prefab / model / sprite / ses (fare ışınının zeminle kesiştiği noktaya)
            if (ImGui::BeginDragDropTarget())
            {
                const ImVec2 imgMin = ImGui::GetItemRectMin();
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM"))
                {
                    std::string path((const char*)payload->Data, payload->DataSize - 1);
                    // Malzeme / fizik malzemesi: farenin altındaki nesneye atanır (Unity: malzemeyi nesneye sürükle)
                    if (AssetActions::IsAppliedToEntity(path))
                    {
                        if (Entity hit = PickAt(ImGui::GetMousePos()))
                            if (AssetActions::ApplyToEntity(hit, path) && m_SelectionCallback)
                                m_SelectionCallback(hit, false);
                        ImGui::EndDragDropTarget();
                        goto dropDone;
                    }
                    glm::vec3 drop = DropPointOnGround(glm::vec2(ImGui::GetMousePos().x - imgMin.x, ImGui::GetMousePos().y - imgMin.y));
                    // Betik: seçili nesneye eklenir; diğerleri bırakılan noktada yeni nesne
                    Entity selected = EditorState::Get().GetSelectedEntity();
                    Entity target = Scripting::ScriptRuntimes::IsScriptFile(path) && selected ? selected : Entity{};
                    Entity created = m_Context ? AssetActions::SpawnFromAsset(m_Context.get(), path, target, target ? nullptr : &drop) : Entity{};
                    if (created && m_SelectionCallback)
                        m_SelectionCallback(created, false);
                }
                ImGui::EndDragDropTarget();
            }
        dropDone:;
        }
        else
        {
            m_ImageHovered = false;
        }
        m_ViewportHovered = m_ImageHovered;
        EditorState::Get().SetViewportHovered(m_ViewportHovered);
        EditorState::Get().SetViewportFocused(m_ViewportFocused);

        ImGuizmo::SetDrawlist();
        ImGuizmo::SetRect(m_ViewportBounds[0].x, m_ViewportBounds[0].y,
                          m_ViewportBounds[1].x - m_ViewportBounds[0].x, m_ViewportBounds[1].y - m_ViewportBounds[0].y);

        DrawOverlays();

        // Harici düzenleme aracı (Terrain fırçası vb.)
        m_ToolConsumed = false;
        if (m_ToolHook && m_ViewportSize.x > 0.0f)
        {
            ImGuiIO& io = ImGui::GetIO();
            ViewportToolContext ctx;
            ctx.Hovered = m_ImageHovered && !ImGuizmo::IsViewManipulateHovered();
            float nx = (io.MousePos.x - m_ViewportBounds[0].x) / m_ViewportSize.x * 2.0f - 1.0f;
            float ny = 1.0f - (io.MousePos.y - m_ViewportBounds[0].y) / m_ViewportSize.y * 2.0f;
            glm::mat4 invVP = glm::inverse(m_EditorCamera.GetViewProjection());
            glm::vec4 n0 = invVP * glm::vec4(nx, ny, -1.0f, 1.0f), n1 = invVP * glm::vec4(nx, ny, 1.0f, 1.0f);
            if (std::abs(n0.w) > 1e-8f && std::abs(n1.w) > 1e-8f)
            {
                ctx.RayOrigin = glm::vec3(n0) / n0.w;
                ctx.RayDirection = glm::normalize(glm::vec3(n1) / n1.w - ctx.RayOrigin);
            }
            ctx.MouseDown = ImGui::IsMouseDown(ImGuiMouseButton_Left) && !io.KeyAlt;
            ctx.MouseClicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !io.KeyAlt;
            ctx.MouseReleased = ImGui::IsMouseReleased(ImGuiMouseButton_Left);
            ctx.Shift = io.KeyShift;
            ctx.Ctrl = io.KeyCtrl;
            ctx.DeltaTime = io.DeltaTime;
            ctx.DrawList = ImGui::GetWindowDrawList();
            const glm::mat4 vp = m_EditorCamera.GetViewProjection();
            const glm::vec2 b0 = m_ViewportBounds[0], size = m_ViewportSize;
            ctx.WorldToScreen = [vp, b0, size](const glm::vec3& w, ImVec2& out) {
                glm::vec4 c = vp * glm::vec4(w, 1.0f);
                if (c.w <= 1e-4f)
                    return false;
                glm::vec3 ndc = glm::vec3(c) / c.w;
                out = ImVec2(b0.x + (ndc.x * 0.5f + 0.5f) * size.x, b0.y + (1.0f - (ndc.y * 0.5f + 0.5f)) * size.y);
                return true;
            };
            ctx.DrawList->PushClipRect(ImVec2(m_ViewportBounds[0].x, m_ViewportBounds[0].y), ImVec2(m_ViewportBounds[1].x, m_ViewportBounds[1].y), true);
            m_ToolConsumed = m_ToolHook(ctx);
            ctx.DrawList->PopClipRect();
        }

        if (!m_ToolConsumed)
        {
            DrawGizmo();
            HandlePicking();
        }
        else
        {
            m_PickPending = false;
        }
        DrawViewCube();

        // Kısayol: F = seçileni kadraja al
        if (m_ViewportFocused && !ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_F, false) &&
            !ImGui::GetIO().KeyCtrl && !ImGui::GetIO().KeyAlt)
            FrameSelected();

        // Stats katmanı (görüntünün sol üstüne sabit, katlanabilir)
        if (m_StatsPanel && m_ShowStats && *m_ShowStats && m_ViewportSize.x > 120.0f)
        {
            if (m_Context)
                m_StatsPanel->SetEntityCount((uint32_t)m_Context->m_Registry.view<IDComponent>().size());
            m_StatsPanel->DrawOverlay("##SceneStats", ImVec2(imagePos.x + 8.0f, imagePos.y + 8.0f), false, m_StatsExpanded,
                                      m_ViewportSize.y - 16.0f);
        }

        ImGui::End();
    }

    // ------------------------------------------------------------------ Araç çubuğu

    void ViewportPanel::DrawToolbar()
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 start = ImGui::GetCursorScreenPos();
        const float width = ImGui::GetContentRegionAvail().x;
        dl->AddRectFilled(start, ImVec2(start.x + width, start.y + kToolbarHeight), ImGui::GetColorU32(ImGuiCol_MenuBarBg));
        dl->AddLine(ImVec2(start.x, start.y + kToolbarHeight - 1.0f), ImVec2(start.x + width, start.y + kToolbarHeight - 1.0f),
                    ImGui::GetColorU32(ImGuiCol_Border));

        ImGui::SetCursorScreenPos(ImVec2(start.x + 6.0f, start.y + 3.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(3.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);

        // ---- Gölgelendirme modu
        {
            char label[96];
            snprintf(label, sizeof(label), "%s %s  " ICON_FA_CARET_DOWN, ShadingIcon(m_Shading), GetShadingName(m_Shading));
            if (ToolbarButton(label, false, "Draw Mode"))
                ImGui::OpenPopup("##ShadingPopup");
            if (ImGui::BeginPopup("##ShadingPopup"))
            {
                ImGui::TextDisabled("Shading Mode");
                static const ViewportShading kOrder[] = { ViewportShading::Shaded, ViewportShading::Wireframe, ViewportShading::PathTracing,
                                                          ViewportShading::Unlit, ViewportShading::Normals, ViewportShading::LightingOnly,
                                                          ViewportShading::MetallicRoughness, ViewportShading::AmbientOcclusion };
                static_assert(sizeof(kOrder) / sizeof(kOrder[0]) == (size_t)ViewportShading::Count, "every shading mode in the menu");
                for (int i = 0; i < (int)ViewportShading::Count; ++i)
                {
                    auto mode = kOrder[i];
                    if (i == 3)
                    {
                        ImGui::Separator();
                        ImGui::TextDisabled("Debug Views");
                    }
                    char item[96];
                    snprintf(item, sizeof(item), "%s  %s", ShadingIcon(mode), GetShadingName(mode));
                    if (ImGui::MenuItem(item, nullptr, m_Shading == mode))
                        m_Shading = mode;
                }
                if (m_Shading == ViewportShading::PathTracing)
                {
                    // Unreal Post Process > Path Tracing ayarları
                    ImGui::Separator();
                    ImGui::TextDisabled("Path Tracing");
                    auto& pt = PathTracer::GetSettings();
                    ImGui::SetNextItemWidth(160.0f);
                    ImGui::SliderInt("Max Bounces", &pt.MaxBounces, 0, 32);
                    ImGui::SetNextItemWidth(160.0f);
                    ImGui::DragInt("Samples Per Pixel", &pt.MaxSamples, 16.0f, 1, 65536);
                    ImGui::SetNextItemWidth(160.0f);
                    ImGui::SliderInt("Samples Per Frame", &pt.SamplesPerFrame, 1, 16);
                    ImGui::SetNextItemWidth(160.0f);
                    ImGui::DragFloat("Max Path Intensity", &pt.MaxPathIntensity, 0.5f, 0.01f, 10000.0f, "%.1f");
                    if (ImGui::Button(ICON_FA_ROTATE "  Restart Accumulation"))
                        PathTracer::Reset();
                }
                ImGui::EndPopup();
            }
        }

        ToolbarSeparator();

        // ---- 2D / 3D
        {
            const bool is2D = m_EditorCamera.Is2D();
            if (ToolbarButton(is2D ? "2D" : "3D", is2D, is2D ? "Switch to 3D view" : "Switch to 2D view", 34.0f))
                m_EditorCamera.SetCameraMode(is2D ? EditorCameraMode::Perspective3D : EditorCameraMode::Orthographic2D);
        }

        // ---- Efektler (gökyüzü, sis, gölgeler, post-process)
        {
            ImGui::SameLine();
            bool anyEffect = m_ViewOptions.Skybox || m_ViewOptions.Fog || m_ViewOptions.PostEffects;
            if (ToolbarButton(ICON_FA_WAND_MAGIC_SPARKLES, anyEffect, "Toggle skybox, fog and post-processing"))
            {
                bool on = !anyEffect;
                m_ViewOptions.Skybox = m_ViewOptions.Fog = m_ViewOptions.PostEffects = on;
            }
            if (DropdownArrow("fx", "Scene view effects"))
                ImGui::OpenPopup("##EffectsPopup");
            if (ImGui::BeginPopup("##EffectsPopup"))
            {
                ImGui::TextDisabled("Scene View Effects");
                ImGui::Checkbox("Skybox", &m_ViewOptions.Skybox);
                ImGui::Checkbox("Fog", &m_ViewOptions.Fog);
                ImGui::Checkbox("Shadows", &m_ViewOptions.Shadows);
                ImGui::Checkbox("Post Processing", &m_ViewOptions.PostEffects);
                ImGui::Checkbox("2D Sprites / Text", &m_ViewOptions.Sprites2D);
                ImGui::Checkbox("UI (Screen Canvases)", &m_ViewOptions.UI);
                ImGui::Separator();
                ImGui::Checkbox("Realtime (Always Refresh)", &m_ViewOptions.Realtime);
                ImGui::EndPopup();
            }
        }

        // ---- Realtime: tüm efektler (parçacıklar, animasyonlu malzemeler) düzenleyicide canlı
        {
            ImGui::SameLine();
            if (ToolbarButton(ICON_FA_CLOCK " Realtime##RealtimeToggle", m_ViewOptions.Realtime,
                              "Realtime: play all particle systems and time-based materials in the Scene view.\n"
                              "Off: only the selected particle system is previewed (Unity behaviour)."))
                m_ViewOptions.Realtime = !m_ViewOptions.Realtime;
        }

        // ---- Ses
        {
            ImGui::SameLine();
            if (ToolbarButton(m_AudioMuted ? ICON_FA_VOLUME_XMARK : ICON_FA_VOLUME_HIGH, !m_AudioMuted, m_AudioMuted ? "Unmute audio" : "Mute audio"))
            {
                m_AudioMuted = !m_AudioMuted;
                auto& audio = AudioEngine::Get();
                if (m_AudioMuted)
                {
                    m_SavedMasterVolume = audio.GetMasterVolume();
                    audio.SetMasterVolume(0.0f);
                }
                else
                {
                    audio.SetMasterVolume(m_SavedMasterVolume > 0.0f ? m_SavedMasterVolume : 1.0f);
                }
            }
        }

        ToolbarSeparator();

        // ---- Gizmolar
        {
            if (ToolbarButton(ICON_FA_LOCATION_CROSSHAIRS " Gizmos", m_ViewOptions.Gizmos, "Toggle gizmos"))
                m_ViewOptions.Gizmos = !m_ViewOptions.Gizmos;
            if (DropdownArrow("gizmos", "Gizmo settings"))
                ImGui::OpenPopup("##GizmosPopup");
            if (ImGui::BeginPopup("##GizmosPopup"))
            {
                ImGui::TextDisabled("Gizmos");
                ImGui::Checkbox("Light & Camera Icons", &m_ViewOptions.Gizmos);
                ImGui::Checkbox("Selection Outline", &m_ShowSelectionOutline);
                ImGui::Checkbox("Canvas Bounds", &m_ShowCanvasBounds);
                ImGui::Checkbox("Orientation Cube", &m_ShowViewCube);
                ImGui::Separator();
                ImGui::TextDisabled("Physics");
                ImGui::Checkbox("Selected Colliders & Joints", &m_ViewOptions.Colliders);
                ImGui::Checkbox("All Colliders", &m_ViewOptions.AllColliders);
                ImGui::EndPopup();
            }
        }

        // ---- Izgara
        {
            ImGui::SameLine();
            if (ToolbarButton(ICON_FA_BORDER_ALL, m_ViewOptions.Grid, "Toggle grid"))
                m_ViewOptions.Grid = !m_ViewOptions.Grid;
            if (DropdownArrow("grid", "Grid settings"))
                ImGui::OpenPopup("##GridPopup");
            if (ImGui::BeginPopup("##GridPopup"))
            {
                auto& grid = EditorGrid::Settings();
                ImGui::TextDisabled("Grid");
                ImGui::Checkbox("Show Grid", &m_ViewOptions.Grid);
                ImGui::SetNextItemWidth(160.0f);
                ImGui::DragFloat("Cell Size", &grid.BaseSize, 0.01f, 0.01f, 1000.0f, "%.2f");
                ImGui::SetNextItemWidth(160.0f);
                ImGui::SliderFloat("Opacity", &grid.Opacity, 0.0f, 1.0f, "%.2f");
                ImGui::SetNextItemWidth(160.0f);
                ImGui::DragFloat("Fade Distance", &grid.FadeDistance, 1.0f, 0.0f, 100000.0f, grid.FadeDistance <= 0.0f ? "Auto" : "%.0f");
                ImGui::SetNextItemWidth(160.0f);
                ImGui::ColorEdit3("Minor Lines", &grid.MinorColor.x, ImGuiColorEditFlags_NoInputs);
                ImGui::SameLine();
                ImGui::ColorEdit3("Major Lines", &grid.MajorColor.x, ImGuiColorEditFlags_NoInputs);
                if (ImGui::Button("Match Snap Value"))
                    grid.BaseSize = std::max(0.01f, EditorState::Get().GetSnapValue());
                ImGui::SameLine();
                if (ImGui::Button("Reset"))
                    grid = EditorGridSettings{};
                ImGui::EndPopup();
            }
        }

        // ---- Kamera
        {
            ImGui::SameLine();
            if (ToolbarButton(ICON_FA_VIDEO, false, "Scene camera settings"))
                ImGui::OpenPopup("##CameraPopup");
            if (ImGui::BeginPopup("##CameraPopup"))
            {
                ImGui::TextDisabled("Scene Camera");
                float fov = m_EditorCamera.GetFOV();
                ImGui::SetNextItemWidth(170.0f);
                if (ImGui::SliderFloat("Field of View", &fov, 10.0f, 120.0f, "%.0f deg"))
                    m_EditorCamera.SetFOV(fov);
                float clip[2] = { m_EditorCamera.GetNearClip(), m_EditorCamera.GetFarClip() };
                ImGui::SetNextItemWidth(170.0f);
                if (ImGui::DragFloat2("Clipping (Near/Far)", clip, 0.05f, 0.001f, 1000000.0f, "%.3f"))
                {
                    clip[0] = std::max(0.001f, clip[0]);
                    clip[1] = std::max(clip[0] + 0.01f, clip[1]);
                    m_EditorCamera.SetClipPlanes(clip[0], clip[1]);
                }
                if (m_EditorCamera.Is2D())
                {
                    float ortho = m_EditorCamera.GetOrthographicSize();
                    ImGui::SetNextItemWidth(170.0f);
                    if (ImGui::DragFloat("Orthographic Size", &ortho, 0.1f, 0.01f, 100000.0f))
                        m_EditorCamera.SetOrthographicSize(std::max(0.01f, ortho));
                }
                else
                {
                    float dist = m_EditorCamera.GetDistance();
                    ImGui::SetNextItemWidth(170.0f);
                    if (ImGui::DragFloat("Orbit Distance", &dist, 0.1f, 0.05f, 100000.0f))
                        m_EditorCamera.SetDistance(std::max(0.05f, dist));
                }
                ImGui::Separator();
                if (ImGui::MenuItem(ICON_FA_CROSSHAIRS "  Frame Selected", "F"))
                    FrameSelected();
                if (ImGui::MenuItem(ICON_FA_HOUSE "  Reset View"))
                {
                    m_EditorCamera.SetFocalPoint(glm::vec3(0.0f));
                    m_EditorCamera.SetDistance(10.0f);
                    m_EditorCamera.SetPitch(0.5235987f);
                    m_EditorCamera.SetYaw(0.7853981f);
                    m_EditorCamera.SetOrthographicSize(10.0f);
                }
                if (!m_EditorCamera.Is2D())
                {
                    ImGui::Separator();
                    ImGui::TextDisabled("Align View");
                    struct { const char* Name; float Pitch, Yaw; } views[] = {
                        { "Top", 1.5697963f, 0.0f }, { "Bottom", -1.5697963f, 0.0f },
                        { "Front", 0.0f, 0.0f }, { "Back", 0.0f, 3.14159265f },
                        { "Right", 0.0f, -1.5707963f }, { "Left", 0.0f, 1.5707963f },
                    };
                    for (int i = 0; i < 6; ++i)
                    {
                        if (i % 2) ImGui::SameLine();
                        if (ImGui::Button(views[i].Name, ImVec2(80.0f, 0.0f)))
                        {
                            m_EditorCamera.SetPitch(views[i].Pitch);
                            m_EditorCamera.SetYaw(views[i].Yaw);
                        }
                    }
                }
                ImGui::EndPopup();
            }
        }

        // ---- Orta: oyun çözünürlüğü (Canvas çerçevesi bu orana göre çizilir)
        {
            auto& gv = GameViewSettings::Get();
            const char* current = gv.Current().Name.c_str();
            const float comboW = std::clamp(ImGui::CalcTextSize(current).x + 56.0f, 150.0f, 230.0f);
            const float centerX = start.x + (width - comboW) * 0.5f;
            ImGui::SameLine();
            if (ImGui::GetCursorScreenPos().x < centerX)
                ImGui::SetCursorScreenPos(ImVec2(centerX, ImGui::GetCursorScreenPos().y));
            ImGui::SetNextItemWidth(comboW);
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 5.0f));
            if (ImGui::BeginCombo("##Resolution", (std::string(ICON_FA_DISPLAY "  ") + current).c_str()))
            {
                for (int i = 0; i < (int)gv.Presets.size(); ++i)
                {
                    if (i == 5) ImGui::Separator();
                    if (ImGui::Selectable(gv.Presets[(size_t)i].Name.c_str(), gv.Selected == i))
                        gv.Selected = i;
                }
                ImGui::EndCombo();
            }
            ImGui::PopStyleVar();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                ImGui::SetTooltip("Game resolution / aspect (also used by the Game view and Canvas frame)");
        }

        // ---- Sağ: istatistik, kadraj
        {
            const float rightW = 2.0f * (kToolbarHeight + 2.0f) + 8.0f;
            ImGui::SameLine();
            float x = start.x + width - rightW;
            if (ImGui::GetCursorScreenPos().x < x)
                ImGui::SetCursorScreenPos(ImVec2(x, ImGui::GetCursorScreenPos().y));
            if (ToolbarButton(ICON_FA_CROSSHAIRS, false, "Frame Selected (F)", kToolbarHeight - 2.0f))
                FrameSelected();
            ImGui::SameLine();
            bool statsOn = m_ShowStats && *m_ShowStats;
            if (ToolbarButton(ICON_FA_CHART_SIMPLE, statsOn, "Statistics overlay", kToolbarHeight - 2.0f) && m_ShowStats)
                *m_ShowStats = !*m_ShowStats;
        }

        ImGui::PopStyleVar(2);
        ImGui::SetCursorScreenPos(ImVec2(start.x, start.y + kToolbarHeight));
    }

    // ------------------------------------------------------------------ Katmanlar

    void ViewportPanel::DrawWorldLine(ImDrawList* drawList, const glm::vec3& a, const glm::vec3& b, ImU32 color, float thickness) const
    {
        const glm::mat4 viewProj = m_EditorCamera.GetViewProjection();
        glm::vec4 p1 = viewProj * glm::vec4(a, 1.0f);
        glm::vec4 p2 = viewProj * glm::vec4(b, 1.0f);

        // Yakın düzlem kırpması
        const float nearW = 0.001f;
        if (p1.w < nearW && p2.w < nearW)
            return;
        if (p1.w < nearW || p2.w < nearW)
        {
            float t = (p1.w - nearW) / (p1.w - p2.w);
            glm::vec4 hit = p1 + t * (p2 - p1);
            if (p1.w < nearW) p1 = hit; else p2 = hit;
        }

        auto toScreen = [&](const glm::vec4& p) {
            glm::vec3 ndc = glm::vec3(p) / p.w;
            return ImVec2(m_ViewportBounds[0].x + (ndc.x * 0.5f + 0.5f) * m_ViewportSize.x,
                          m_ViewportBounds[0].y + (1.0f - (ndc.y * 0.5f + 0.5f)) * m_ViewportSize.y);
        };
        drawList->AddLine(toScreen(p1), toScreen(p2), color, thickness);
    }

    void ViewportPanel::DrawOverlays()
    {
        if (!m_Context || m_ViewportSize.x <= 0.0f)
            return;

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->PushClipRect(ImVec2(m_ViewportBounds[0].x, m_ViewportBounds[0].y), ImVec2(m_ViewportBounds[1].x, m_ViewportBounds[1].y), true);
        if (m_ShowCanvasBounds && m_ViewOptions.Gizmos)
            DrawCanvasBounds(drawList);
        if (m_ShowSelectionOutline)
            DrawSelectionOutline(drawList);
        drawList->PopClipRect();
    }

    void ViewportPanel::DrawCanvasBounds(ImDrawList* drawList)
    {
        // Canvas çerçevesi: Game görünümü çözünürlüğüne göre (UISystem ile aynı ölçek)
        const glm::vec2 screen = m_Context->GetUIScreenSize();
        auto canvasView = m_Context->m_Registry.view<CanvasComponent, TransformComponent>();
        for (auto entity : canvasView)
        {
            Entity canvas{ entity, m_Context.get() };
            glm::vec2 size = UISystem::GetCanvasSize(canvas, screen);
            glm::mat4 m = UISystem::GetCanvasWorldMatrix(canvas, screen);
            glm::vec3 c[4] = { glm::vec3(m * glm::vec4(0, 0, 0, 1)), glm::vec3(m * glm::vec4(size.x, 0, 0, 1)),
                               glm::vec3(m * glm::vec4(size.x, size.y, 0, 1)), glm::vec3(m * glm::vec4(0, size.y, 0, 1)) };
            for (int i = 0; i < 4; ++i)
                DrawWorldLine(drawList, c[i], c[(i + 1) % 4], IM_COL32(60, 230, 90, 210), 2.0f);
        }
    }

    void ViewportPanel::DrawSelectionOutline(ImDrawList* drawList)
    {
        const auto& selected = EditorState::Get().GetSelectedEntities();
        for (size_t i = 0; i < selected.size(); ++i)
        {
            Entity e = selected[i];
            if (!e || !e.HasComponent<TransformComponent>())
                continue;
            glm::vec3 mn, mx;
            if (!ScenePicking::GetLocalBounds(e, mn, mx))
                continue;

            // Yerel kutuyu dünya dönüşümüyle çiz (döndürülmüş nesnelerde sıkı çerçeve)
            const glm::mat4& world = e.GetComponent<TransformComponent>().WorldTransform;
            glm::vec3 corners[8];
            for (int k = 0; k < 8; ++k)
            {
                glm::vec3 local((k & 1) ? mx.x : mn.x, (k & 2) ? mx.y : mn.y, (k & 4) ? mx.z : mn.z);
                corners[k] = glm::vec3(world * glm::vec4(local, 1.0f));
            }
            const ImU32 color = i == 0 ? IM_COL32(255, 150, 30, 230) : IM_COL32(255, 200, 90, 170);
            static const int edges[12][2] = { {0,1},{2,3},{4,5},{6,7},{0,2},{1,3},{4,6},{5,7},{0,4},{1,5},{2,6},{3,7} };
            for (auto& edge : edges)
                DrawWorldLine(drawList, corners[edge[0]], corners[edge[1]], color, i == 0 ? 1.8f : 1.2f);
        }
    }

    // ------------------------------------------------------------------ Gizmo

    void ViewportPanel::DrawGizmo()
    {
        auto& selectedEntities = EditorState::Get().GetSelectedEntities();
        Entity primaryEntity = EditorState::Get().GetSelectedEntity();
        if (!m_Context || !primaryEntity || !primaryEntity.HasComponent<TransformComponent>() || m_GizmoType == -1)
            return;

        std::vector<Entity> selectedCopy(selectedEntities.begin(), selectedEntities.end());
        const bool is2D = m_EditorCamera.Is2D();
        ImGuizmo::SetOrthographic(is2D);

        // UI öğesi: yalnızca taşıma; dünya deltası AnchoredPosition'a çevrilir
        if (primaryEntity.HasComponent<RectTransformComponent>())
        {
            glm::mat4 world = primaryEntity.GetComponent<TransformComponent>().WorldTransform;
            glm::mat4 before = world;
            glm::mat4 view = m_EditorCamera.GetViewMatrix(), proj = m_EditorCamera.GetProjectionMatrix();
            float snapValues[3] = { 1.0f, 1.0f, 1.0f };
            bool snap = EditorState::Get().IsSnapEnabled() != ImGui::GetIO().KeyCtrl;
            ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(proj), ImGuizmo::TRANSLATE_X | ImGuizmo::TRANSLATE_Y, ImGuizmo::LOCAL,
                                 glm::value_ptr(world), nullptr, snap ? snapValues : nullptr);
            if (ImGuizmo::IsUsing())
            {
                // Mutlak sürükleme: başlangıç konumuna göre toplam yer değiştirme (kare kare delta birikmez)
                if (!m_UIDragActive)
                {
                    m_UIDragActive = true;
                    m_UIDragStartWorld = glm::vec3(before[3]);
                    m_UIDragStartAnchors.clear();
                    for (auto& e : selectedCopy)
                        if (e && e.HasComponent<RectTransformComponent>())
                            m_UIDragStartAnchors.emplace_back((uint32_t)e, e.GetComponent<RectTransformComponent>().AnchoredPosition);
                }
                const glm::vec3 total = glm::vec3(world[3]) - m_UIDragStartWorld;
                for (auto& [id, start] : m_UIDragStartAnchors)
                {
                    Entity e{ (entt::entity)id, m_Context.get() };
                    if (!m_Context->m_Registry.valid((entt::entity)id) || !e.HasComponent<RectTransformComponent>())
                        continue;
                    e.GetComponent<RectTransformComponent>().AnchoredPosition = start;
                    UISystem::MoveElementWorld(e, total, m_Context->GetUIScreenSize());
                }
                primaryEntity.GetComponent<TransformComponent>().WorldTransform = world;
            }
            else
                m_UIDragActive = false;
            return;
        }

        auto& tc = primaryEntity.GetComponent<TransformComponent>();
        glm::mat4 oldLocalTransform = tc.GetTransform();

        // Gizmo dünya uzayında çizilir; ebeveyn matrisi
        glm::mat4 parentTransform = glm::mat4(1.0f);
        Entity parent = m_Context->GetEntityParent(primaryEntity);
        if (parent && parent.HasComponent<TransformComponent>())
            parentTransform = parent.GetComponent<TransformComponent>().WorldTransform;

        glm::mat4 transform = parentTransform * oldLocalTransform;
        glm::mat4 cameraProjection = m_EditorCamera.GetProjectionMatrix();
        glm::mat4 cameraView = m_EditorCamera.GetViewMatrix();

        // Snap: araç çubuğundan ya da Ctrl basılıyken
        auto& es = EditorState::Get();
        bool snap = es.IsSnapEnabled() != ImGui::GetIO().KeyCtrl;
        float snapValue = es.GetSnapValue();
        if (m_GizmoType == ImGuizmo::ROTATE)
            snapValue = 15.0f;
        float snapValues[3] = { snapValue, snapValue, snapValue };
        ImGuizmo::MODE mode = (es.GetGizmoMode() == GizmoMode::World && m_GizmoType != ImGuizmo::SCALE) ? ImGuizmo::WORLD : ImGuizmo::LOCAL;
        ImGuizmo::Manipulate(glm::value_ptr(cameraView), glm::value_ptr(cameraProjection),
                             (ImGuizmo::OPERATION)m_GizmoType, mode, glm::value_ptr(transform), nullptr, snap ? snapValues : nullptr);

        if (!ImGuizmo::IsUsing())
            return;

        // Dünya matrisini yerel matrise çevir
        glm::mat4 newLocalTransform = glm::inverse(parentTransform) * transform;

        glm::vec3 position, rotation, scale;
        ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(newLocalTransform), glm::value_ptr(position), glm::value_ptr(rotation), glm::value_ptr(scale));
        glm::vec3 oldPos, oldRot, oldScale;
        ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(oldLocalTransform), glm::value_ptr(oldPos), glm::value_ptr(oldRot), glm::value_ptr(oldScale));

        glm::vec3 deltaPos = position - oldPos;
        glm::vec3 deltaRot = rotation - oldRot;
        glm::vec3 deltaScale = scale - oldScale;

        tc.Position = position;
        if (is2D)
        {
            tc.Position.z = oldPos.z;
            tc.Rotation = glm::vec3(0.0f, 0.0f, glm::radians(rotation.z));
        }
        else
        {
            tc.Rotation = glm::radians(rotation);
        }
        tc.Scale = scale;

        // Diğer seçili varlıklara aynı delta
        for (auto& entity : selectedCopy)
        {
            if (entity == primaryEntity || !entity || !entity.HasComponent<TransformComponent>())
                continue;
            auto& etc = entity.GetComponent<TransformComponent>();
            etc.Position += deltaPos;
            if (!is2D)
                etc.Rotation += glm::radians(deltaRot);
            else
                etc.Rotation.z += glm::radians(deltaRot.z);
            etc.Scale += deltaScale;
        }
    }

    void ViewportPanel::DrawViewCube()
    {
        if (!m_ShowViewCube || m_EditorCamera.Is2D() || m_ViewportSize.x < 200.0f || m_ViewportSize.y < 200.0f)
            return;

        const float size = 96.0f;
        ImVec2 pos(m_ViewportBounds[1].x - size - 8.0f, m_ViewportBounds[0].y + 8.0f);
        glm::mat4 view = m_EditorCamera.GetViewMatrix();
        const glm::mat4 before = view;
        ImGuizmo::ViewManipulate(glm::value_ptr(view), m_EditorCamera.GetDistance(), pos, ImVec2(size, size), 0x00000000);

        if (view == before)
            return;

        // Yeni görünüm matrisinden kamera açılarını çıkar (EditorCamera: quat(-pitch, -yaw, 0))
        glm::mat4 camWorld = glm::inverse(view);
        glm::vec3 forward = glm::normalize(-glm::vec3(camWorld[2]));
        glm::vec3 position = glm::vec3(camWorld[3]);
        float pitch = std::asin(std::clamp(-forward.y, -1.0f, 1.0f));
        float yaw = -std::atan2(-forward.x, -forward.z);
        // Tam dikey bakışta yaw belirsizdir; kameranın sağ vektöründen al
        if (std::abs(forward.y) > 0.9999f)
        {
            glm::vec3 right = glm::vec3(camWorld[0]);
            yaw = -std::atan2(-right.z, right.x);
        }
        pitch = std::clamp(pitch, -1.5697963f, 1.5697963f);

        glm::vec3 focal = position + forward * m_EditorCamera.GetDistance();
        m_EditorCamera.SetPitch(pitch);
        m_EditorCamera.SetYaw(yaw);
        m_EditorCamera.SetFocalPoint(focal);
    }

    // ------------------------------------------------------------------ Seçim

    Entity ViewportPanel::PickAt(const ImVec2& screenPos)
    {
        if (!m_Context || m_ViewportSize.x <= 0.0f || m_ViewportSize.y <= 0.0f)
            return {};

        float nx = (screenPos.x - m_ViewportBounds[0].x) / m_ViewportSize.x * 2.0f - 1.0f;
        float ny = 1.0f - (screenPos.y - m_ViewportBounds[0].y) / m_ViewportSize.y * 2.0f;
        glm::mat4 invVP = glm::inverse(m_EditorCamera.GetViewProjection());
        glm::vec4 nearP = invVP * glm::vec4(nx, ny, -1.0f, 1.0f);
        glm::vec4 farP = invVP * glm::vec4(nx, ny, 1.0f, 1.0f);
        if (std::abs(nearP.w) < 1e-8f || std::abs(farP.w) < 1e-8f)
            return {};
        glm::vec3 origin = glm::vec3(nearP) / nearP.w;
        glm::vec3 target = glm::vec3(farP) / farP.w;
        glm::vec3 dir = target - origin;
        if (glm::dot(dir, dir) < 1e-12f)
            return {};

        PickResult result;
        if (ScenePicking::Raycast(*m_Context, origin, glm::normalize(dir), result, m_ViewOptions.Gizmos))
            return result.Hit;
        return {};
    }

    void ViewportPanel::HandlePicking()
    {
        ImGuiIO& io = ImGui::GetIO();
        const bool overGizmo = (m_GizmoType != -1 && EditorState::Get().HasSelection() && (ImGuizmo::IsOver() || ImGuizmo::IsUsing()))
                               || ImGuizmo::IsViewManipulateHovered() || ImGuizmo::IsUsingViewManipulate();

        if (m_ImageHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !io.KeyAlt && !overGizmo)
        {
            m_PickPending = true;
            m_PickStart = io.MousePos;
        }

        if (!m_PickPending)
            return;

        // Sürükleme olursa (kamera/gizmo) seçim iptal
        ImVec2 d(io.MousePos.x - m_PickStart.x, io.MousePos.y - m_PickStart.y);
        if (d.x * d.x + d.y * d.y > 16.0f || ImGuizmo::IsUsing())
        {
            m_PickPending = false;
            return;
        }

        if (!ImGui::IsMouseReleased(ImGuiMouseButton_Left))
            return;
        m_PickPending = false;

        Entity hit = PickAt(m_PickStart);
        const bool additive = io.KeyCtrl || io.KeyShift;
        if (!hit && additive)
            return; // boşluğa Ctrl+tık seçimi korur
        if (m_SelectionCallback)
            m_SelectionCallback(hit, additive);
        else if (hit)
            EditorState::Get().SelectEntity(hit);
        else
            EditorState::Get().ClearSelection();
        m_SelectedEntity = EditorState::Get().GetSelectedEntity();

        if (hit && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            FrameSelected();
    }

    glm::vec3 ViewportPanel::DropPointOnGround(const glm::vec2& local)
    {
        if (m_ViewportSize.x <= 0.0f || m_ViewportSize.y <= 0.0f)
            return m_EditorCamera.GetFocalPoint();
        float nx = local.x / m_ViewportSize.x * 2.0f - 1.0f;
        float ny = 1.0f - local.y / m_ViewportSize.y * 2.0f;
        glm::mat4 inv = glm::inverse(m_EditorCamera.GetViewProjection());
        glm::vec4 n0 = inv * glm::vec4(nx, ny, -1.0f, 1.0f), n1 = inv * glm::vec4(nx, ny, 1.0f, 1.0f);
        if (std::abs(n0.w) < 1e-8f || std::abs(n1.w) < 1e-8f)
            return m_EditorCamera.GetFocalPoint();
        glm::vec3 o = glm::vec3(n0) / n0.w;
        glm::vec3 d = glm::normalize(glm::vec3(n1) / n1.w - o);
        const bool is2D = m_EditorCamera.Is2D();
        const int axis = is2D ? 2 : 1;
        if (std::abs(d[axis]) > 1e-6f)
        {
            float t = -o[axis] / d[axis];
            if (t > 0.0f && t < 10000.0f)
            {
                glm::vec3 p = o + d * t;
                p[axis] = 0.0f;
                return p;
            }
        }
        return m_EditorCamera.GetFocalPoint();
    }

    void ViewportPanel::FrameSelected()
    {
        const auto& selected = EditorState::Get().GetSelectedEntities();
        if (selected.empty() || !m_Context)
            return;

        glm::vec3 mn(FLT_MAX), mx(-FLT_MAX);
        bool any = false;
        for (Entity e : selected)
        {
            if (!e || !e.HasComponent<TransformComponent>())
                continue;
            glm::vec3 bmin, bmax;
            if (!ScenePicking::GetWorldBounds(e, bmin, bmax))
            {
                glm::vec3 p = glm::vec3(e.GetComponent<TransformComponent>().WorldTransform[3]);
                bmin = p - glm::vec3(0.5f);
                bmax = p + glm::vec3(0.5f);
            }
            mn = glm::min(mn, bmin);
            mx = glm::max(mx, bmax);
            any = true;
        }
        if (!any)
            return;

        glm::vec3 center = (mn + mx) * 0.5f;
        float radius = std::max(0.25f, glm::length(mx - mn) * 0.5f);

        if (m_EditorCamera.Is2D())
        {
            float aspect = m_ViewportSize.y > 0.0f ? m_ViewportSize.x / m_ViewportSize.y : 1.0f;
            glm::vec3 ext = (mx - mn) * 0.5f;
            m_EditorCamera.SetOrthographicSize(std::max(0.5f, std::max(ext.y, ext.x / aspect) * 1.35f));
            m_EditorCamera.SetFocalPoint(glm::vec3(center.x, center.y, 0.0f));
        }
        else
        {
            float halfFov = glm::radians(m_EditorCamera.GetFOV()) * 0.5f;
            float distance = radius / std::sin(std::max(0.05f, halfFov)) * 1.6f;
            m_EditorCamera.SetFocalPoint(center);
            m_EditorCamera.SetDistance(std::max(distance, m_EditorCamera.GetNearClip() * 4.0f));
        }
        m_PostStack.ResetHistory();
    }

    void ViewportPanel::OnEvent(Event& e)
    {
        // Gizmo kullanılıyorsa kamera event'lerini engelle
        if (ImGuizmo::IsUsing() || ImGuizmo::IsUsingViewManipulate())
            return;
        m_EditorCamera.OnEvent(e);
    }
}
