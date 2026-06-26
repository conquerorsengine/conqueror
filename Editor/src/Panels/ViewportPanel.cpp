#include "ViewportPanel.h"
#include "Renderer/RHI/RenderCommand.h"
#include "Renderer/PostProcess.h"
#include "Core/Tiering/QualitySettings.h"
#include "Scene/Scene.h"
#include "Scene/Components.h"
#include "EditorState.h"

#include <imgui.h>
#include <ImGuizmo.h>

namespace Conqueror::Editor
{
    ViewportPanel::ViewportPanel()
    {
        FramebufferSpecification fbSpec;
        fbSpec.Width = 1280;
        fbSpec.Height = 720;
        fbSpec.Attachments = { FramebufferTextureFormat::RGBA8, FramebufferTextureFormat::RED_INTEGER, FramebufferTextureFormat::Depth };
        m_Framebuffer = Framebuffer::Create(fbSpec);
    }

    void ViewportPanel::OnUpdate(Timestep ts)
    {
        // Gizmo kullanılıyorsa kamera update'ini engelle
        if (m_ViewportFocused && !ImGuizmo::IsUsing())
            m_EditorCamera.OnUpdate(ts);

        // Render sadece geçerli framebuffer varsa
        if (m_Framebuffer && m_ViewportSize.x > 0 && m_ViewportSize.y > 0)
        {
            uint32_t width = static_cast<uint32_t>(m_ViewportSize.x);
            uint32_t height = static_cast<uint32_t>(m_ViewportSize.y);

            // 1. Sahneyi PostProcess framebuffer'a render et
            PostProcess::BeginScene(width, height);
            RenderCommand::SetClearColor({ 0.1f, 0.1f, 0.1f, 1 });
            RenderCommand::Clear();

            if (m_Context)
                m_Context->OnUpdateEditor(ts, m_EditorCamera, GetViewportBounds());

            PostProcess::EndScene();

            // 2. Anti-Aliasing uygula ve viewport framebuffer'a çiz
            m_Framebuffer->Bind();
            RenderCommand::SetClearColor({ 0.1f, 0.1f, 0.1f, 1 });
            RenderCommand::Clear();
            m_Framebuffer->ClearAttachment(1, -1);

            const auto& qualityPreset = QualitySettings::GetPreset();
            if (qualityPreset.EnableSMAA)
            {
                PostProcess::ApplySMAA();
            }
            else if (qualityPreset.EnableFXAA)
            {
                PostProcess::ApplyFXAA();
            }
            else
            {
                PostProcess::ApplyFXAA(); // Varsayilan AA (Fallback)
            }
            
            // 3. Halo/Flare efekti (eğer varsa)
            if (m_Context)
            {
                auto haloTexture = m_Context->GetHaloTexture();
                float haloStrength = m_Context->GetHaloStrength();
                float flareStrength = m_Context->GetFlareStrength();
                
                if (m_Context->IsHaloEnabled() || m_Context->IsFlareEnabled())
                {
                    // Sun source light'ın screen space pozisyonunu hesapla
                    Entity sunSource = m_Context->GetSunSourceEntity();
                    if (sunSource && sunSource.HasComponent<DirectionalLightComponent>())
                    {
                        auto& dirLight = sunSource.GetComponent<DirectionalLightComponent>();
                        
                        // Directional light için direction vektörünü kullan (sonsuz uzaklıkta)
                        glm::vec3 lightDir = glm::normalize(dirLight.Direction);
                        
                        // Light direction'ı view space'e çevir
                        glm::mat4 view = m_EditorCamera.GetViewMatrix();
                        glm::vec3 viewSpaceLightDir = glm::mat3(view) * (-lightDir); // Negatif çünkü ışık kaynağına bakıyoruz
                        
                        // View space direction'ı NDC'ye project et (w=0 çünkü direction)
                        glm::mat4 projection = glm::perspective(glm::radians(45.0f), 
                                                                m_ViewportSize.x / m_ViewportSize.y, 
                                                                0.1f, 1000.0f);
                        
                        // Direction'ı uzak bir noktaya çevir (projection için)
                        float farDistance = 500.0f;
                        glm::vec3 viewSpacePos = viewSpaceLightDir * farDistance;
                        glm::vec4 clipSpace = projection * glm::vec4(viewSpacePos, 1.0f);
                        
                        // Perspective divide
                        glm::vec2 screenPos;
                        bool lightVisible = false;
                        
                        if (clipSpace.w > 0.0f) // Kameranın önünde
                        {
                            glm::vec3 ndc = glm::vec3(clipSpace) / clipSpace.w;
                            
                            // NDC -> Screen (0-1)
                            screenPos = glm::vec2(ndc.x * 0.5f + 0.5f, ndc.y * 0.5f + 0.5f);
                            
                            // Light görünür mü? (ekran içinde)
                            lightVisible = (screenPos.x >= -0.5f && screenPos.x <= 1.5f &&
                                          screenPos.y >= -0.5f && screenPos.y <= 1.5f);
                        }
                        else
                        {
                            // Kameranın arkasında, ekran merkezine koy
                            screenPos = glm::vec2(0.5f, 0.5f);
                            lightVisible = false;
                        }
                        
                        PostProcess::ApplyHaloFlare(screenPos, dirLight.Color, lightVisible, 
                                                    m_Context->IsHaloEnabled(), haloTexture, haloStrength,
                                                    m_Context->IsFlareEnabled(), flareStrength,
                                                    m_Context->GetFlareElements());
                    }
                }
            }

            m_Framebuffer->Unbind();
        }
    }

    void ViewportPanel::OnImGuiRender()
    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{ 0, 0 });
        ImGui::Begin("Viewport", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        
        auto viewportMinRegion = ImGui::GetWindowContentRegionMin();
        auto viewportMaxRegion = ImGui::GetWindowContentRegionMax();
        auto viewportOffset = ImGui::GetWindowPos();
        m_ViewportBounds[0] = { viewportMinRegion.x + viewportOffset.x, viewportMinRegion.y + viewportOffset.y };
        m_ViewportBounds[1] = { viewportMaxRegion.x + viewportOffset.x, viewportMaxRegion.y + viewportOffset.y };

        m_ViewportFocused = ImGui::IsWindowFocused();
        m_ViewportHovered = ImGui::IsWindowHovered();

        ImVec2 viewportPanelSize = ImGui::GetContentRegionAvail();
        
        // Tam piksel değerleri kullan (float cast yerine int)
        glm::vec2 newViewportSize = { static_cast<float>(static_cast<int>(viewportPanelSize.x)), 
                                      static_cast<float>(static_cast<int>(viewportPanelSize.y)) };

        // Framebuffer'ı sadece boyut gerçekten değiştiğinde resize et
        if (newViewportSize.x > 0.0f && newViewportSize.y > 0.0f)
        {
            uint32_t newWidth = static_cast<uint32_t>(newViewportSize.x);
            uint32_t newHeight = static_cast<uint32_t>(newViewportSize.y);
            
            if (m_Framebuffer->GetSpecification().Width != newWidth ||
                m_Framebuffer->GetSpecification().Height != newHeight)
            {
                m_ViewportSize = newViewportSize;
                m_Framebuffer->Resize(newWidth, newHeight);
                m_EditorCamera.SetViewportSize(m_ViewportSize.x, m_ViewportSize.y);
                
                if (m_Context)
                    m_Context->OnViewportResize(newWidth, newHeight);

                // Resize sonrası hemen clear et (glitch önleme)
                m_Framebuffer->Bind();
                RenderCommand::SetClearColor({ 0.1f, 0.1f, 0.1f, 1.0f });
                RenderCommand::Clear();
                m_Framebuffer->ClearAttachment(1, -1);
                m_Framebuffer->Unbind();
            }
        }

        // Texture render et
        if (m_ViewportSize.x > 0.0f && m_ViewportSize.y > 0.0f)
        {
            uint64_t textureID = m_Framebuffer->GetColorAttachmentRendererID(0);
            ImGui::Image(reinterpret_cast<void*>(textureID), 
                        ImVec2{ m_ViewportSize.x, m_ViewportSize.y }, 
                        ImVec2{ 0, 1 }, ImVec2{ 1, 0 });
        }

        // Canvas borders çiz (ImGui overlay)
        if (m_Context)
        {
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            
            // Halo/Flare debug çizimi (kırmızı daire)
            auto haloTexture = m_Context->GetHaloTexture();
            float haloStrength = m_Context->GetHaloStrength();
            float flareStrength = m_Context->GetFlareStrength();
            
            if ((haloTexture && haloStrength > 0.0f) || flareStrength > 0.0f)
            {
                Entity sunSource = m_Context->GetSunSourceEntity();
                if (sunSource && sunSource.HasComponent<DirectionalLightComponent>())
                {
                    auto& dirLight = sunSource.GetComponent<DirectionalLightComponent>();
                    glm::vec3 lightDir = glm::normalize(dirLight.Direction);
                    glm::mat4 view = m_EditorCamera.GetViewMatrix();
                    glm::vec3 viewSpaceLightDir = glm::mat3(view) * (-lightDir);
                    glm::mat4 projection = glm::perspective(glm::radians(45.0f), 
                                                            m_ViewportSize.x / m_ViewportSize.y, 
                                                            0.1f, 1000.0f);
                    float farDistance = 500.0f;
                    glm::vec3 viewSpacePos = viewSpaceLightDir * farDistance;
                    glm::vec4 clipSpace = projection * glm::vec4(viewSpacePos, 1.0f);
                    
                    if (clipSpace.w > 0.0f)
                    {
                        glm::vec3 ndc = glm::vec3(clipSpace) / clipSpace.w;
                        glm::vec2 screenPos = glm::vec2(ndc.x * 0.5f + 0.5f, ndc.y * 0.5f + 0.5f);
                        
                        // Screen space -> Viewport pixel
                        float screenX = screenPos.x * m_ViewportSize.x;
                        float screenY = screenPos.y * m_ViewportSize.y;
                        
                        ImVec2 center(m_ViewportBounds[0].x + screenX, m_ViewportBounds[0].y + screenY);
                    }
                }
            }
            
            // Canvas borders
            auto& registry = m_Context->m_Registry;
            auto canvasView = registry.view<CanvasComponent, TransformComponent>();
            
            for (auto entity : canvasView)
            {
                auto& transform = canvasView.get<TransformComponent>(entity);
                
                // Canvas world position
                glm::vec3 canvasWorldPos = glm::vec3(transform.WorldTransform[3]);
                
                // Canvas boyutu sabit olmalı (Referans çözünürlük 1920x1080, panel küçülünce küçülmemesi için)
                float baseWidth = 1920.0f / 100.0f;
                float baseHeight = 1080.0f / 100.0f;
                
                // 10x büyüme efekti için debug çizgisini de 10 kat büyüt
                float scaledWidth = baseWidth * transform.Scale.x * 10.0f;
                float scaledHeight = baseHeight * transform.Scale.y * 10.0f;
                
                // Scale 0 ise çizgi nokta kadar olur, gözükmez! Fallback ekleyelim.
                if (scaledWidth <= 0.001f || scaledHeight <= 0.001f)
                {
                    scaledWidth = baseWidth;
                    scaledHeight = baseHeight;
                }
                
                float halfWidth = scaledWidth * 0.5f;
                float halfHeight = scaledHeight * 0.5f;
                
                // 4 köşe (world space)
                glm::vec3 corners[4] = {
                    canvasWorldPos + glm::vec3(-halfWidth, -halfHeight, 0.0f),
                    canvasWorldPos + glm::vec3( halfWidth, -halfHeight, 0.0f),
                    canvasWorldPos + glm::vec3( halfWidth,  halfHeight, 0.0f),
                    canvasWorldPos + glm::vec3(-halfWidth,  halfHeight, 0.0f)
                };
                
                // World space -> Screen space
                glm::mat4 viewProj = m_EditorCamera.GetViewProjection();
                ImU32 color = IM_COL32(0, 255, 0, 200);

                // 4 kenarı tek tek işle ve near plane'e göre clip yap
                for (int i = 0; i < 4; i++)
                {
                    glm::vec4 p1 = viewProj * glm::vec4(corners[i], 1.0f);
                    glm::vec4 p2 = viewProj * glm::vec4(corners[(i + 1) % 4], 1.0f);

                    // Near plane (w = 0.001f) clipping
                    float nearZ = 0.001f;
                    if (p1.w < nearZ && p2.w < nearZ)
                        continue; // İkisi de kameranın arkasında, bu kenarı çizme

                    if (p1.w < nearZ || p2.w < nearZ)
                    {
                        // Biri önde biri arkada, kesişim noktasını bul
                        float t = (p1.w - nearZ) / (p1.w - p2.w);
                        glm::vec4 pIntersect = p1 + t * (p2 - p1);
                        
                        if (p1.w < nearZ) p1 = pIntersect;
                        else p2 = pIntersect;
                    }

                    // NDC'ye çevir (artık ikisi de w >= 0.001f)
                    glm::vec3 ndc1 = glm::vec3(p1) / p1.w;
                    glm::vec3 ndc2 = glm::vec3(p2) / p2.w;

                    // NDC -> Screen
                    float screenX1 = (ndc1.x * 0.5f + 0.5f) * m_ViewportSize.x;
                    float screenY1 = (1.0f - (ndc1.y * 0.5f + 0.5f)) * m_ViewportSize.y;
                    
                    float screenX2 = (ndc2.x * 0.5f + 0.5f) * m_ViewportSize.x;
                    float screenY2 = (1.0f - (ndc2.y * 0.5f + 0.5f)) * m_ViewportSize.y;

                    ImVec2 start(m_ViewportBounds[0].x + screenX1, m_ViewportBounds[0].y + screenY1);
                    ImVec2 end(m_ViewportBounds[0].x + screenX2, m_ViewportBounds[0].y + screenY2);

                    drawList->AddLine(start, end, color, 2.0f);
                }
            }

        }

        // Gizmo render (seçili entity varsa)
        auto& selectedEntities = EditorState::Get().GetSelectedEntities();
        Entity primaryEntity = EditorState::Get().GetSelectedEntity();
        std::vector<Entity> selectedCopy(selectedEntities.begin(), selectedEntities.end());
        
        if (primaryEntity && primaryEntity.HasComponent<TransformComponent>())
        {
            const bool is2D = m_EditorCamera.Is2D();
            ImGuizmo::SetOrthographic(is2D);
            ImGuizmo::SetDrawlist();
            ImGuizmo::SetRect(m_ViewportBounds[0].x, m_ViewportBounds[0].y, 
                             m_ViewportBounds[1].x - m_ViewportBounds[0].x, 
                             m_ViewportBounds[1].y - m_ViewportBounds[0].y);

            auto& tc = primaryEntity.GetComponent<TransformComponent>();
            glm::mat4 oldLocalTransform = tc.GetTransform();
            
            // Gizmo için parent matrisi bul (Gizmo'yu Dünya koordinatında çizmek için)
            glm::mat4 parentTransform = glm::mat4(1.0f);
            Entity parent = m_Context->GetEntityParent(primaryEntity);
            if (parent && parent.HasComponent<TransformComponent>())
            {
                parentTransform = parent.GetComponent<TransformComponent>().WorldTransform;
            }

            // Gizmo'ya vermek üzere Dünya (World) matrisini oluştur
            glm::mat4 transform = parentTransform * oldLocalTransform;
            glm::mat4 oldWorldTransform = transform;

            glm::mat4 cameraProjection = m_EditorCamera.GetProjectionMatrix();
            glm::mat4 cameraView = m_EditorCamera.GetViewMatrix();

            ImGuizmo::Manipulate(glm::value_ptr(cameraView), glm::value_ptr(cameraProjection),
                                (ImGuizmo::OPERATION)m_GizmoType, ImGuizmo::LOCAL, glm::value_ptr(transform));

            if (ImGuizmo::IsUsing())
            {
                // Manipüle edilmiş Dünya matrisini geri Yerel (Local) matrise çevir
                glm::mat4 newLocalTransform = glm::inverse(parentTransform) * transform;

                glm::vec3 position, rotation, scale;
                ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(newLocalTransform), 
                                                     glm::value_ptr(position), 
                                                     glm::value_ptr(rotation), 
                                                     glm::value_ptr(scale));
                
                // Delta hesaplamak için oldLocal'ı parçala
                glm::vec3 oldPos, oldRot, oldScale;
                ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(oldLocalTransform), 
                                                     glm::value_ptr(oldPos), 
                                                     glm::value_ptr(oldRot), 
                                                     glm::value_ptr(oldScale));

                glm::vec3 deltaPos = position - oldPos;
                glm::vec3 deltaRot = rotation - oldRot;
                glm::vec3 deltaScale = scale - oldScale;

                tc.Position = position;
                if (is2D)
                {
                    tc.Position.z = 0.0f;
                    tc.Rotation = glm::vec3(0.0f);
                }
                else
                {
                    tc.Rotation = glm::radians(rotation);
                }
                tc.Scale = scale;

                // Tüm seçili entity'lere delta uygula
                for (auto& entity : selectedCopy)
                {
                    if (entity == primaryEntity || !entity || !entity.HasComponent<TransformComponent>())
                        continue;

                    auto& etc = entity.GetComponent<TransformComponent>();
                    etc.Position += deltaPos;
                    if (!is2D)
                    {
                        etc.Rotation += glm::radians(deltaRot);
                    }
                    etc.Scale += deltaScale;
                }
            }
        }

        // Viewport'a tıklama - seçimi kaldır
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ImGui::IsWindowHovered() && !ImGuizmo::IsOver())
        {
            EditorState::Get().ClearSelection();
            m_SelectedEntity = {};
            CQ_CORE_INFO("Selection cleared");
        }

        // Gizmo toolbar (viewport içinde, image'ın üstünde)
        ImGui::SetCursorPos(ImVec2(10.0f, 30.0f));
        
        float buttonSize = 25.0f;
        ImGui::PushStyleColor(ImGuiCol_Button, m_GizmoType == 7 ? ImVec4(0.3f, 0.5f, 0.8f, 1.0f) : ImVec4(0.2f, 0.2f, 0.2f, 0.8f));
        if (ImGui::Button("T", ImVec2(buttonSize, buttonSize)))
        {
            m_GizmoType = 7;
            CQ_CORE_INFO("Gizmo: TRANSLATE");
        }
        ImGui::PopStyleColor();
        
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, m_GizmoType == 120 ? ImVec4(0.3f, 0.5f, 0.8f, 1.0f) : ImVec4(0.2f, 0.2f, 0.2f, 0.8f));
        if (ImGui::Button("R", ImVec2(buttonSize, buttonSize)))
        {
            m_GizmoType = 120;
            CQ_CORE_INFO("Gizmo: ROTATE");
        }
        ImGui::PopStyleColor();
        
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, m_GizmoType == 896 ? ImVec4(0.3f, 0.5f, 0.8f, 1.0f) : ImVec4(0.2f, 0.2f, 0.2f, 0.8f));
        if (ImGui::Button("S", ImVec2(buttonSize, buttonSize)))
        {
            m_GizmoType = 896;
            CQ_CORE_INFO("Gizmo: SCALE");
        }
        ImGui::PopStyleColor();
        
        // Viewport mode toggle (sağ üst)
        const float modeButtonWidth = 40.0f;
        const float modeButtonX = glm::max(10.0f, m_ViewportSize.x - modeButtonWidth - 10.0f);
        ImGui::SetCursorPos(ImVec2(modeButtonX, 30.0f));

        const bool is2D = m_EditorCamera.Is2D();
        ImGui::PushStyleColor(ImGuiCol_Button, is2D ? ImVec4(0.5f, 0.5f, 0.5f, 0.95f) : ImVec4(0.25f, 0.45f, 0.85f, 0.95f));
        if (ImGui::Button(is2D ? "2D" : "3D", ImVec2(modeButtonWidth, buttonSize)))
        {
            if (is2D)
                m_EditorCamera.SetCameraMode(EditorCameraMode::Perspective3D);
            else
                m_EditorCamera.SetCameraMode(EditorCameraMode::Orthographic2D);

            CQ_CORE_INFO("Viewport mode: {}", m_EditorCamera.Is2D() ? "2D" : "3D");
        }
        ImGui::PopStyleColor();
        
        ImGui::End();
        ImGui::PopStyleVar();
    }

    void ViewportPanel::OnEvent(Event& e)
    {
        // Gizmo kullanılıyorsa kamera event'lerini engelle
        if (ImGuizmo::IsUsing())
        {
            CQ_CORE_INFO("Gizmo blocking camera events");
            return;
        }
        
        m_EditorCamera.OnEvent(e);
    }
}
