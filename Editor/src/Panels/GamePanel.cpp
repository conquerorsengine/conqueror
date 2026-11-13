#include "GamePanel.h"
#include "ViewportPanel.h"
#include "Core/Math/Math.h"
#include "Renderer/RHI/RenderCommand.h"
#include "Renderer/PostProcess.h"
#include "Renderer/Utilities/Renderer3D/Renderer3D.h"
#include "Core/Time/TimeManager.h"
#include "Core/Recording/Recorder.h"
#include "Core/Project/Project.h"
#include "stb_image_write.h"
#include <glad/glad.h>
#include <ctime>
#include <filesystem>
#include "Core/Tiering/QualitySettings.h"
#include "Core/Logging/Log.h"
#include "Core/Debug/DebugDraw.h"
#include "Scene/Scene.h"
#include "Scene/PostProcessVolume.h"
#include "Scene/Entity.h"
#include "Scene/Components.h"

#include "Core/Audio/AudioEngine.h"
#include "GameViewSettings.h"
#include "StatsPanel.h"
#include "UI/EditorUI.h"

#include "Scene/PixelPerfect.h"

#include <imgui.h>
#include <IconsFontAwesome6.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

namespace Conqueror::Editor
{
    GamePanel::GamePanel()
    {
        FramebufferSpecification fbSpec;
        fbSpec.Width = 1280;
        fbSpec.Height = 720;
        fbSpec.Attachments = { FramebufferTextureFormat::RGBA8, FramebufferTextureFormat::RED_INTEGER, FramebufferTextureFormat::Depth };
        m_Framebuffer = Framebuffer::Create(fbSpec);
    }

    void GamePanel::OnUpdate(Timestep ts, bool isPlaying)
    {
        if (!m_TabVisible && !m_Playing)
            return;
        // Panel boyutu değiştiyse hedef çizimden hemen önce boyutlanır: gösterilen kare hiç boş (gri) kalmaz
        if (m_Framebuffer && m_PendingViewportSize.x > 0 && m_PendingViewportSize.y > 0 && m_PendingViewportSize != m_ViewportSize)
        {
            m_Framebuffer->Resize((uint32_t)m_PendingViewportSize.x, (uint32_t)m_PendingViewportSize.y);
            m_ViewportSize = m_PendingViewportSize;
        }
        if (m_Framebuffer && m_ViewportSize.x > 0 && m_ViewportSize.y > 0)
        {
            uint32_t width = static_cast<uint32_t>(m_ViewportSize.x);
            uint32_t height = static_cast<uint32_t>(m_ViewportSize.y);
            PixelPerfect::View pixelView;

            if (m_Context)
            {
                m_Context->SetViewportBounds(m_ViewportBounds[0], m_ViewportBounds[1]);
                m_Context->SetViewportHovered(m_ViewportHovered);
                m_Context->SetUseEditorCameraForScripts(false);

                auto cameraEntity = m_Context->GetPrimaryCameraEntity();
                if (!cameraEntity)
                {
                    auto view = m_Context->m_Registry.view<CameraComponent>();
                    if (!view.empty())
                        cameraEntity = Entity{ *view.begin(), m_Context.get() };
                }

                m_HasCamera = cameraEntity && cameraEntity.HasComponent<CameraComponent>();
                // Yalnızca ekran üstü UI'dan oluşan sahne (ana menü) da "çizen bir şey" sayılır: uyarı yazısı UI'nın üstüne binmesin
                m_HasOverlayUI = !m_Context->m_Registry.view<CanvasComponent>().empty();
                if (cameraEntity && cameraEntity.HasComponent<CameraComponent>())
                {
                    auto& cc = cameraEntity.GetComponent<CameraComponent>();
                    if (!cc.FixedAspectRatio)
                        cc.Camera.SetViewportSize(width, height);

                    auto& tc = cameraEntity.GetComponent<TransformComponent>();
                    glm::mat4 viewMatrix = glm::inverse(tc.WorldTransform);
                    glm::mat4 projMatrix = cc.Camera.GetProjection();
                    m_Context->SetEditorViewProjection(projMatrix * viewMatrix);
                }
                // Pixel Perfect Camera: sahne düşük çözünürlükte çizilip tam sayı katla büyütülebilir
                pixelView = PixelPerfect::Resolve(*m_Context, width, height);
            }
            const uint32_t sceneWidth = pixelView.Active ? pixelView.RenderWidth : width;
            const uint32_t sceneHeight = pixelView.Active ? pixelView.RenderHeight : height;

            m_PostStack.BeginScene(sceneWidth, sceneHeight);
            // Arka plan rengi kameranın Background ayarından (Unity: ortografik kamera = düz mavi)
            glm::vec3 bgSrgb = m_Context ? m_Context->ResolveCameraBackground(m_Context->GetPrimaryCameraEntity()).Color : glm::vec3(0.1f);
            glm::vec3 bg = Math::SRGBToLinear(bgSrgb);
            RenderCommand::SetClearColor({ bg.r, bg.g, bg.b, 1.0f });
            RenderCommand::Clear();
            RenderCamera postCamera;

            if (m_Context && isPlaying)
            {
                m_Context->OnUpdateRuntime(ts, false);
            }
            else if (m_Context)
            {
                m_Context->OnUpdateRuntime(ts, true);
            }

            if (m_Context)
            {
                Entity cameraEntity = m_Context->GetPrimaryCameraEntity();
                if (!cameraEntity)
                {
                    auto view = m_Context->m_Registry.view<CameraComponent>();
                    if (!view.empty())
                        cameraEntity = Entity{ *view.begin(), m_Context.get() };
                }

                if (cameraEntity && cameraEntity.HasComponent<CameraComponent>())
                {
                    auto& tc = cameraEntity.GetComponent<TransformComponent>();
                    auto& cc = cameraEntity.GetComponent<CameraComponent>();
                    glm::mat4 viewMatrix = glm::inverse(tc.WorldTransform);
                    glm::mat4 vp = cc.Camera.GetProjection() * viewMatrix;
                    postCamera = RenderCamera::FromSceneCamera(cc.Camera, tc.WorldTransform, sceneWidth, sceneHeight);
                    DebugDraw::SetCameraPosition(glm::vec3(tc.WorldTransform[3]));
                    DebugDraw::Update(ts);
                    DebugDraw::Render(vp);
                    DebugDraw::Clear();
                }
            }

            m_PostStack.EndScene();

            m_Framebuffer->Bind();
            RenderCommand::SetViewport(0, 0, width, height);
            // Pixel Perfect çerçevesi dışı siyah (letterbox / pillarbox)
            RenderCommand::SetClearColor(pixelView.Active ? glm::vec4(0.0f, 0.0f, 0.0f, 1.0f) : glm::vec4(0.1f, 0.1f, 0.1f, 1.0f));
            RenderCommand::Clear();
            m_Framebuffer->ClearAttachment(1, -1);
            if (pixelView.Active)
                RenderCommand::SetViewport(pixelView.Output.x, pixelView.Output.y, pixelView.Output.z, pixelView.Output.w);
            m_PostStack.SetPointUpscale(pixelView.PointSample);

            // Post Process Volume'lar oyun kamerasının konumuna göre harmanlanır
            PostProcessSettings post = m_Context ? PostProcessVolumes::Resolve(*m_Context, m_Context->GetRenderSettings().Post, postCamera.Position) : PostProcessSettings{};
            // Görünen gökyüzü her zaman ton eşlenir (boş sahnede beyaza patlamasın)
            const bool skyVisible = m_Context && m_Context->GetRenderSettings().Environment.Sky != SkyMode::SolidColor
                                 && m_Context->ResolveCameraBackground(m_Context->GetPrimaryCameraEntity()).DrawSky;
            m_PostStack.Render(post, postCamera, TimeManager::GetTime(), !m_Context || m_Context->HasRenderable3D() || skyVisible);
            RenderCommand::SetViewport(0, 0, width, height);

            // UI (ekran üstü canvas'lar): post-process sonrası, oyun çözünürlüğünde
            if (m_Context)
            {
                m_Context->SetUIScreenSize({ (float)width, (float)height });
                UIInputState input = isPlaying ? m_UIInput : UIInputState{};
                UISystem::Update(*m_Context, { (float)width, (float)height }, input, ts.GetSeconds());
                m_UIInput.MousePressed = m_UIInput.MouseReleased = false;
                m_UIInput.Scroll = 0.0f;
                m_UIInput.Characters.clear();
                m_UIInput.Backspace = m_UIInput.Delete = m_UIInput.Enter = m_UIInput.Escape = m_UIInput.Tab = false;
                m_UIInput.Left = m_UIInput.Right = m_UIInput.Home = m_UIInput.End = false;
                m_UIInput.Up = m_UIInput.Down = m_UIInput.Space = false;
                UISystem::RenderScreenSpace(*m_Context, { (float)width, (float)height });
            }

            // Kayıt ve ekran görüntüsü (son, post-process'li görüntü)
            uint32_t colorTex = m_Framebuffer->GetColorAttachmentRendererID(0);
            Recorder::SubmitFrame(colorTex, width, height, ts.GetSeconds());
            if (m_ScreenshotRequested)
            {
                m_ScreenshotRequested = false;
                std::vector<uint8_t> pixels((size_t)width * height * 4);
                glPixelStorei(GL_PACK_ALIGNMENT, 1);
                glGetTextureImage(colorTex, 0, GL_RGBA, GL_UNSIGNED_BYTE, (GLsizei)pixels.size(), pixels.data());
                for (size_t i = 3; i < pixels.size(); i += 4) pixels[i] = 255;
                auto project = Project::GetActiveProjectDirectory();
                std::filesystem::path dir = (project.empty() ? std::filesystem::current_path() : project) / "Screenshots";
                std::error_code ec;
                std::filesystem::create_directories(dir, ec);
                std::time_t t = std::time(nullptr);
                char name[64];
                std::strftime(name, sizeof(name), "Screenshot_%Y%m%d_%H%M%S.png", std::localtime(&t));
                std::string path = (dir / name).string();
                stbi_flip_vertically_on_write(1);
                bool ok = stbi_write_png(path.c_str(), (int)width, (int)height, 4, pixels.data(), (int)width * 4) != 0;
                stbi_flip_vertically_on_write(0);
                if (ok) CQ_CORE_INFO("Screenshot saved: {0}", path);
                else CQ_CORE_ERROR("Screenshot failed: {0}", path);
            }

            m_Framebuffer->Unbind();
        }
    }

    bool GamePanel::IsMaximized() const
    {
        return m_Playing && GameViewSettings::Get().MaximizeOnPlay;
    }

    void GamePanel::DrawToolbar()
    {
        auto& gv = GameViewSettings::Get();
        const float barH = 30.0f;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 start = ImGui::GetCursorScreenPos();
        const float width = ImGui::GetContentRegionAvail().x;
        dl->AddRectFilled(start, ImVec2(start.x + width, start.y + barH), ImGui::GetColorU32(ImGuiCol_MenuBarBg));
        dl->AddLine(ImVec2(start.x, start.y + barH - 1.0f), ImVec2(start.x + width, start.y + barH - 1.0f), ImGui::GetColorU32(ImGuiCol_Border));

        ImGui::SetCursorScreenPos(ImVec2(start.x + 6.0f, start.y + 3.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 5.0f));

        auto toggle = [](const char* label, bool& value, const char* tooltip)
        {
            if (value)
            {
                ImGui::PushStyleColor(ImGuiCol_Button, UI::Colors::AccentDim);
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, UI::Colors::Accent);
            }
            else
            {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_ButtonHovered));
            }
            bool pressed = ImGui::Button(label);
            ImGui::PopStyleColor(2);
            if (pressed)
                value = !value;
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                ImGui::SetTooltip("%s", tooltip);
            return pressed;
        };

        // Sol: ölçek
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("Scale");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(110.0f);
        ImGui::SliderFloat("##GameScale", &gv.Scale, 0.25f, 4.0f, "%.2fx", ImGuiSliderFlags_Logarithmic);
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
            gv.Scale = 1.0f;
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip("Zoom the game view (right click: fit)");

        // Orta: çözünürlük seçici
        {
            const char* current = gv.Current().Name.c_str();
            const float comboW = std::clamp(ImGui::CalcTextSize(current).x + 60.0f, 160.0f, 240.0f);
            const float centerX = start.x + (width - comboW) * 0.5f;
            ImGui::SameLine();
            if (ImGui::GetCursorScreenPos().x < centerX)
                ImGui::SetCursorScreenPos(ImVec2(centerX, ImGui::GetCursorScreenPos().y));
            ImGui::SetNextItemWidth(comboW);
            if (ImGui::BeginCombo("##GameResolution", (std::string(ICON_FA_DISPLAY "  ") + current).c_str()))
            {
                for (int i = 0; i < (int)gv.Presets.size(); ++i)
                {
                    if (i == 5) ImGui::Separator();
                    if (ImGui::Selectable(gv.Presets[(size_t)i].Name.c_str(), gv.Selected == i))
                        gv.Selected = i;
                }
                ImGui::Separator();
                ImGui::TextDisabled("Custom");
                static int custom[2] = { 1600, 900 };
                ImGui::SetNextItemWidth(140.0f);
                ImGui::InputInt2("##CustomRes", custom);
                ImGui::SameLine();
                if (ImGui::Button("Add"))
                {
                    custom[0] = std::clamp(custom[0], 16, 8192);
                    custom[1] = std::clamp(custom[1], 16, 8192);
                    char name[64];
                    snprintf(name, sizeof(name), "%dx%d (Custom)", custom[0], custom[1]);
                    gv.Presets.push_back({ name, custom[0], custom[1], 0.0f });
                    gv.Selected = (int)gv.Presets.size() - 1;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndCombo();
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                ImGui::SetTooltip("Game view resolution / aspect ratio");
        }

        // Sağ: oynatmada büyüt, ses, istatistik
        {
            const float rightW = ImGui::CalcTextSize(ICON_FA_MAXIMIZE " Maximize On Play").x + ImGui::CalcTextSize(ICON_FA_VOLUME_XMARK).x
                               + ImGui::CalcTextSize(ICON_FA_CHART_SIMPLE " Stats").x + 6.0f * 8.0f + 20.0f;
            ImGui::SameLine();
            float x = start.x + width - rightW;
            if (ImGui::GetCursorScreenPos().x < x)
                ImGui::SetCursorScreenPos(ImVec2(x, ImGui::GetCursorScreenPos().y));
            toggle(ICON_FA_MAXIMIZE " Maximize On Play", gv.MaximizeOnPlay, "Maximize the Game view while playing");
            ImGui::SameLine();
            toggle(gv.MuteAudio ? ICON_FA_VOLUME_XMARK : ICON_FA_VOLUME_HIGH, gv.MuteAudio, "Mute game audio");
            ImGui::SameLine();
            toggle(ICON_FA_CHART_SIMPLE " Stats##GameStatsToggle", gv.ShowStats, "Show rendering statistics");
        }

        ImGui::PopStyleVar(3);
        ImGui::SetCursorScreenPos(ImVec2(start.x, start.y + barH));

        // Sessiz durumunu ses motoruna uygula (yalnızca değişince)
        if (gv.MuteAudio != m_WasMuted)
        {
            auto& audio = AudioEngine::Get();
            if (gv.MuteAudio)
            {
                m_SavedVolume = audio.GetMasterVolume();
                audio.SetMasterVolume(0.0f);
            }
            else
            {
                audio.SetMasterVolume(m_SavedVolume > 0.0f ? m_SavedVolume : 1.0f);
            }
            m_WasMuted = gv.MuteAudio;
        }
    }

    void GamePanel::OnImGuiRender()
    {
        auto& gv = GameViewSettings::Get();
        const bool maximized = IsMaximized();

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{ 0, 0 });
        bool visible;
        if (maximized)
        {
            // Oynatırken tüm editör alanını kapla
            const ImGuiViewport* vp = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(vp->WorkPos);
            ImGui::SetNextWindowSize(vp->WorkSize);
            ImGui::SetNextWindowViewport(vp->ID);
            ImGui::SetNextWindowFocus();
            // NoNavInputs: oyun tuşları (ok tuşları, Space...) araç çubuğu widget'larını gezinmesin/değiştirmesin
            visible = ImGui::Begin("Game (Maximized)", nullptr,
                                   ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoDocking |
                                   ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoNavInputs);
        }
        else
        {
            visible = ImGui::Begin("Game", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoNavInputs);
        }
        ImGui::PopStyleVar();
        m_TabVisible = visible;

        if (!visible)
        {
            m_ViewportFocused = false;
            m_ViewportHovered = false;
            m_Settled = false;
            ImGui::End();
            return;
        }

        m_ViewportFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        DrawToolbar();

        // Panel alanı (araç çubuğunun altı)
        ImVec2 areaPos = ImGui::GetCursorScreenPos();
        ImVec2 area = ImGui::GetContentRegionAvail();
        area.x = std::floor(area.x);
        area.y = std::floor(area.y);
        if (area.x < 1.0f || area.y < 1.0f)
        {
            ImGui::End();
            return;
        }
        ImGui::GetWindowDrawList()->AddRectFilled(areaPos, ImVec2(areaPos.x + area.x, areaPos.y + area.y), IM_COL32(12, 12, 14, 255));
        gv.LastGamePanelSize = { area.x, area.y };

        // Render çözünürlüğü (sabit çözünürlük ya da oran) ve ekranda gösterim boyutu
        glm::ivec2 renderSize = gv.RenderSize({ area.x, area.y });
        renderSize = glm::clamp(renderSize, glm::ivec2(1), glm::ivec2(8192));
        float fit = std::min(area.x / (float)renderSize.x, area.y / (float)renderSize.y);
        float displayScale = fit * gv.Scale;
        ImVec2 displaySize(std::floor(renderSize.x * displayScale), std::floor(renderSize.y * displayScale));
        ImVec2 imagePos(std::floor(areaPos.x + (area.x - displaySize.x) * 0.5f), std::floor(areaPos.y + (area.y - displaySize.y) * 0.5f));

        glm::vec2 newViewportSize = { (float)renderSize.x, (float)renderSize.y };
        if (m_ViewportSize.x <= 0.0f || m_ViewportSize.y <= 0.0f)
        {
            // İlk kare: gösterilecek önceki görüntü yok
            m_Framebuffer->Resize((uint32_t)renderSize.x, (uint32_t)renderSize.y);
            m_Framebuffer->Bind();
            RenderCommand::SetClearColor({ 0.1f, 0.1f, 0.1f, 1.0f });
            RenderCommand::Clear();
            m_Framebuffer->ClearAttachment(1, -1);
            m_Framebuffer->Unbind();
            m_ViewportSize = newViewportSize;
        }
        else
            m_PendingViewportSize = newViewportSize;   // OnUpdate çizimden önce uygular; bu karede önceki görüntü alana sığdırılır

        // Görüntü (alan dışına taşarsa kırp)
        ImGui::PushClipRect(areaPos, ImVec2(areaPos.x + area.x, areaPos.y + area.y), true);
        ImGui::SetCursorScreenPos(imagePos);
        uint64_t textureID = m_Framebuffer->GetColorAttachmentRendererID(0);
        // Görünür olunca, oynatma başlayıp bitince ve büyütülünce: boyut oturup o boyutta çizilene kadar eski kare gösterilmez (yalnız koyu
        // alan). İlk kareler yavaş olduğu için eski boyuttaki kare yanlarda koyu bantlı ya da gerilmiş görünüyordu (196)
        if (maximized != m_LastMaximized || m_Playing != m_LastPlaying)
            m_Settled = false;
        m_LastMaximized = maximized;
        m_LastPlaying = m_Playing;
        m_StableFrames = newViewportSize == m_LastPanelSize ? m_StableFrames + 1 : 0;
        m_LastPanelSize = newViewportSize;
        if (!m_Settled && m_StableFrames >= 2 && m_ViewportSize == newViewportSize)
            m_Settled = true;
        if (!m_Settled && m_ViewportSize != newViewportSize)
            ImGui::Dummy(displaySize);
        else
            PanelImage((ImTextureID)textureID, m_ViewportSize, displaySize);
        m_ViewportHovered = ImGui::IsItemHovered();
        // Unity: "No cameras rendering"
        if (!m_HasCamera && !m_HasOverlayUI)
        {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const char* line1 = "No cameras rendering";
            const char* line2 = "Add one with GameObject > Camera, or set a camera as Primary.";
            ImVec2 s1 = ImGui::CalcTextSize(line1), s2 = ImGui::CalcTextSize(line2);
            ImVec2 c(imagePos.x + displaySize.x * 0.5f, imagePos.y + displaySize.y * 0.5f);
            dl->AddText(ImGui::GetFont(), ImGui::GetFontSize() * 1.4f, ImVec2(c.x - s1.x * 0.7f, c.y - s1.y * 1.6f), IM_COL32(220, 222, 228, 230), line1);
            dl->AddText(ImVec2(c.x - s2.x * 0.5f, c.y + 4.0f), IM_COL32(150, 154, 162, 220), line2);
        }
        ImGui::PopClipRect();

        // UI girdisi (görüntü pikseline eşle, sol-alt köken). Basma/bırakma bir sonraki güncellemeye kadar birikir.
        {
            ImGuiIO& io = ImGui::GetIO();
            glm::vec2 rel((io.MousePos.x - imagePos.x) / std::max(displaySize.x, 1.0f), (io.MousePos.y - imagePos.y) / std::max(displaySize.y, 1.0f));
            m_UIInput.Valid = m_ViewportHovered || ImGui::IsMouseDown(ImGuiMouseButton_Left);
            m_UIInput.MousePosition = glm::vec2(rel.x * renderSize.x, (1.0f - rel.y) * renderSize.y);
            m_UIInput.MouseDown = ImGui::IsMouseDown(ImGuiMouseButton_Left);
            if (m_ViewportHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) m_UIInput.MousePressed = true;
            if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) m_UIInput.MouseReleased = true;
            if (m_ViewportHovered) m_UIInput.Scroll += io.MouseWheel;
            if (m_ViewportFocused)
            {
                for (int i = 0; i < io.InputQueueCharacters.Size; ++i)
                    m_UIInput.Characters.push_back((uint32_t)io.InputQueueCharacters[i]);
                m_UIInput.Backspace |= ImGui::IsKeyPressed(ImGuiKey_Backspace);
                m_UIInput.Delete |= ImGui::IsKeyPressed(ImGuiKey_Delete);
                m_UIInput.Enter |= ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter);
                m_UIInput.Escape |= ImGui::IsKeyPressed(ImGuiKey_Escape);
                m_UIInput.Left |= ImGui::IsKeyPressed(ImGuiKey_LeftArrow);
                m_UIInput.Right |= ImGui::IsKeyPressed(ImGuiKey_RightArrow);
                m_UIInput.Home |= ImGui::IsKeyPressed(ImGuiKey_Home);
                m_UIInput.End |= ImGui::IsKeyPressed(ImGuiKey_End);
                m_UIInput.Up |= ImGui::IsKeyPressed(ImGuiKey_UpArrow);
                m_UIInput.Down |= ImGui::IsKeyPressed(ImGuiKey_DownArrow);
                m_UIInput.Space |= ImGui::IsKeyPressed(ImGuiKey_Space, false);
            }
        }

        // Oyun girdisi için görüntü sınırları (ekran koordinatı)
        m_ViewportBounds[0] = { imagePos.x, imagePos.y };
        m_ViewportBounds[1] = { imagePos.x + displaySize.x, imagePos.y + displaySize.y };

        // Çözünürlük bilgisi (görüntü alanın altında küçük metin)
        if (gv.Current().Width > 0 || gv.Scale != 1.0f)
        {
            char info[64];
            snprintf(info, sizeof(info), "%d x %d  (%.0f%%)", renderSize.x, renderSize.y, displayScale * 100.0f);
            ImVec2 ts = ImGui::CalcTextSize(info);
            ImVec2 p(areaPos.x + area.x - ts.x - 8.0f, areaPos.y + area.y - ts.y - 6.0f);
            ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(p.x - 4, p.y - 2), ImVec2(p.x + ts.x + 4, p.y + ts.y + 2), IM_COL32(0, 0, 0, 150), 4.0f);
            ImGui::GetWindowDrawList()->AddText(p, IM_COL32(220, 220, 220, 230), info);
        }

        if (gv.ShowStats && m_StatsPanel)
        {
            if (m_Context)
                m_StatsPanel->SetEntityCount((uint32_t)m_Context->m_Registry.view<IDComponent>().size());
            m_StatsPanel->DrawOverlay("##GameStats", ImVec2(areaPos.x + area.x - 8.0f, areaPos.y + 8.0f), true, m_StatsExpanded, area.y - 16.0f);
        }

        ImGui::End();
    }

    void GamePanel::OnEvent(Event& e)
    {
    }
}
