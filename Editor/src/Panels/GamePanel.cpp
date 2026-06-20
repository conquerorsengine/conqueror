#include "GamePanel.h"
#include "Renderer/RHI/RenderCommand.h"
#include "Renderer/PostProcess.h"
#include "Core/Tiering/QualitySettings.h"
#include "Core/Logging/Log.h"
#include "Scene/Scene.h"
#include "Scene/Entity.h"
#include "Scene/Components.h"

#include <imgui.h>

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
        if (m_Framebuffer && m_ViewportSize.x > 0 && m_ViewportSize.y > 0)
        {
            uint32_t width = static_cast<uint32_t>(m_ViewportSize.x);
            uint32_t height = static_cast<uint32_t>(m_ViewportSize.y);

            PostProcess::BeginScene(width, height);
            RenderCommand::SetClearColor({ 0.1f, 0.1f, 0.1f, 1.0f });
            RenderCommand::Clear();

            if (m_Context)
            {
                auto cameraEntity = m_Context->GetPrimaryCameraEntity();
                if (!cameraEntity)
                {
                    auto view = m_Context->m_Registry.view<CameraComponent>();
                    if (!view.empty())
                        cameraEntity = Entity{ *view.begin(), m_Context.get() };
                }

                if (cameraEntity && cameraEntity.HasComponent<CameraComponent>())
                {
                    auto& cc = cameraEntity.GetComponent<CameraComponent>();
                    if (!cc.FixedAspectRatio)
                        cc.Camera.SetViewportSize(width, height);
                }

                m_Context->OnUpdateRuntime(ts, true);
            }

            PostProcess::EndScene();

            m_Framebuffer->Bind();
            RenderCommand::SetViewport(0, 0, width, height);
            RenderCommand::SetClearColor({ 0.1f, 0.1f, 0.1f, 1.0f });
            RenderCommand::Clear();
            m_Framebuffer->ClearAttachment(1, -1);

            const auto& qualityPreset = QualitySettings::GetPreset();
            if (qualityPreset.EnableSMAA)
                PostProcess::ApplySMAA();
            else if (qualityPreset.EnableFXAA)
                PostProcess::ApplyFXAA();
            else
                PostProcess::ApplyFXAA();

            m_Framebuffer->Unbind();
        }
    }

    void GamePanel::OnImGuiRender()
    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{ 0, 0 });
        ImGui::Begin("Game", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        
        auto viewportMinRegion = ImGui::GetWindowContentRegionMin();
        auto viewportMaxRegion = ImGui::GetWindowContentRegionMax();
        auto viewportOffset = ImGui::GetWindowPos();
        m_ViewportBounds[0] = { viewportMinRegion.x + viewportOffset.x, viewportMinRegion.y + viewportOffset.y };
        m_ViewportBounds[1] = { viewportMaxRegion.x + viewportOffset.x, viewportMaxRegion.y + viewportOffset.y };

        m_ViewportFocused = ImGui::IsWindowFocused();
        m_ViewportHovered = ImGui::IsWindowHovered();

        ImVec2 viewportPanelSize = ImGui::GetContentRegionAvail();
        
        glm::vec2 newViewportSize = { static_cast<float>(static_cast<int>(viewportPanelSize.x)), 
                                      static_cast<float>(static_cast<int>(viewportPanelSize.y)) };

        if (newViewportSize.x > 0.0f && newViewportSize.y > 0.0f)
        {
            uint32_t newWidth = static_cast<uint32_t>(newViewportSize.x);
            uint32_t newHeight = static_cast<uint32_t>(newViewportSize.y);
            
            m_ViewportSize = newViewportSize;

            if (m_Framebuffer->GetSpecification().Width != newWidth ||
                m_Framebuffer->GetSpecification().Height != newHeight)
            {
                CQ_CORE_INFO("[GamePanel] Viewport resize: {}x{} -> {}x{}", 
                    m_Framebuffer->GetSpecification().Width, m_Framebuffer->GetSpecification().Height,
                    newWidth, newHeight);
                m_Framebuffer->Resize(newWidth, newHeight);
                
                m_Framebuffer->Bind();
                RenderCommand::SetClearColor({ 0.1f, 0.1f, 0.1f, 1.0f });
                RenderCommand::Clear();
                m_Framebuffer->ClearAttachment(1, -1);
                m_Framebuffer->Unbind();
            }
        }

        if (m_ViewportSize.x > 0.0f && m_ViewportSize.y > 0.0f)
        {
            uint64_t textureID = m_Framebuffer->GetColorAttachmentRendererID(0);
            ImGui::Image(reinterpret_cast<void*>(textureID), 
                        ImVec2{ m_ViewportSize.x, m_ViewportSize.y }, 
                        ImVec2{ 0, 1 }, ImVec2{ 1, 0 });
        }
        
        ImGui::End();
        ImGui::PopStyleVar();
    }

    void GamePanel::OnEvent(Event& e)
    {
    }
}
