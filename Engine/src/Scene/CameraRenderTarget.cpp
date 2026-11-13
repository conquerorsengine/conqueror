#include "CameraRenderTarget.h"
#include "Scene/GameWindow.h"
#include "Scene/Scene.h"
#include "Scene/Entity.h"
#include "Scene/Components.h"
#include "Scene/ComponentRegistry.h"
#include "Renderer/RenderTargetRegistry.h"
#include "Renderer/PostProcess.h"
#include "Renderer/RHI/Shader.h"
#include "Core/Profiling/FrameProfiler.h"

#include <IconsFontAwesome6.h>
#include <glad/glad.h>

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace Conqueror
{
    namespace
    {
        // Doğrusal HDR ara hedef (renk RGBA16F + derinlik)
        struct HDRTarget
        {
            GLuint Framebuffer = 0, Color = 0, Depth = 0;
            uint32_t Width = 0, Height = 0;
        };

        std::unordered_map<std::string, HDRTarget> s_HDR;
        std::unordered_map<Scene*, uint64_t> s_LastFrame;
        std::shared_ptr<Shader> s_Resolve;

        const char* kResolveVertex = R"(
            #version 450 core
            layout(location = 0) in vec2 a_Position;
            layout(location = 1) in vec2 a_TexCoord;
            out vec2 v_UV;
            void main()
            {
                v_UV = a_TexCoord;
                gl_Position = vec4(a_Position, 0.0, 1.0);
            }
        )";
        const char* kResolveFragment = R"(
            #version 450 core
            in vec2 v_UV;
            layout(location = 0) out vec4 o_Color;
            uniform sampler2D u_Source;
            uniform float u_Exposure;
            vec3 ACES(vec3 x)
            {
                return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
            }
            void main()
            {
                vec3 hdr = max(texture(u_Source, v_UV).rgb, vec3(0.0)) * u_Exposure;
                o_Color = vec4(pow(ACES(hdr), vec3(1.0 / 2.2)), 1.0);
            }
        )";

        void ReleaseHDR(HDRTarget& t)
        {
            if (t.Framebuffer) glDeleteFramebuffers(1, &t.Framebuffer);
            if (t.Color) glDeleteTextures(1, &t.Color);
            if (t.Depth) glDeleteRenderbuffers(1, &t.Depth);
            t = {};
        }

        void EnsureHDR(HDRTarget& t, uint32_t width, uint32_t height)
        {
            if (t.Framebuffer && t.Width == width && t.Height == height)
                return;
            ReleaseHDR(t);
            t.Width = width;
            t.Height = height;
            glGenTextures(1, &t.Color);
            glBindTexture(GL_TEXTURE_2D, t.Color);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, (GLsizei)width, (GLsizei)height, 0, GL_RGBA, GL_FLOAT, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glBindTexture(GL_TEXTURE_2D, 0);
            glGenRenderbuffers(1, &t.Depth);
            glBindRenderbuffer(GL_RENDERBUFFER, t.Depth);
            glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, (GLsizei)width, (GLsizei)height);
            glGenFramebuffers(1, &t.Framebuffer);
            glBindFramebuffer(GL_FRAMEBUFFER, t.Framebuffer);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t.Color, 0);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, t.Depth);
        }
    }

    namespace CameraRenderTargets
    {
        void Render(Scene& scene)
        {
            auto view = scene.m_Registry.view<CameraComponent, RenderTargetComponent>();
            if (view.begin() == view.end())
                return;
            // Düzenleyici + oyun paneli aynı karede çizebilir: hedefler sahne başına kare başına bir kez
            const uint64_t frame = Profiling::FrameProfiler::GetCurrentFrameIndex();
            uint64_t& last = s_LastFrame[&scene];
            if (last == frame)
                return;
            last = frame;

            if (!s_Resolve)
                s_Resolve = Shader::Create("RenderTargetResolve", kResolveVertex, kResolveFragment);
            if (!s_Resolve || !s_Resolve->IsValid())
                return;

            GLint previousFramebuffer = 0;
            GLint previousViewport[4];
            glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFramebuffer);
            glGetIntegerv(GL_VIEWPORT, previousViewport);
            const GLboolean depthTest = glIsEnabled(GL_DEPTH_TEST);
            const GLboolean blend = glIsEnabled(GL_BLEND);
            const GLboolean cull = glIsEnabled(GL_CULL_FACE);

            for (auto e : view)
            {
                auto& rt = view.get<RenderTargetComponent>(e);
                if (!rt.Asset.empty())
                {
                    // Render Texture varlığı: ad ve boyut dosyadan (Unity Camera.targetTexture)
                    std::string name;
                    int w = 0, h = 0;
                    if (RenderTargetRegistry::ReadAsset(rt.Asset, name, w, h))
                    {
                        rt.Name = name;
                        rt.Width = w;
                        rt.Height = h;
                    }
                }
                if (!scene.IsEntityActive(e) || rt.Name.empty() || rt.UpdateMode == RenderTargetUpdate::Disabled)
                    continue;
                if (rt.UpdateMode == RenderTargetUpdate::Once && rt.RenderedOnce)
                    continue;
                const uint32_t width = (uint32_t)std::clamp(rt.Width, 1, 4096);
                const uint32_t height = (uint32_t)std::clamp(rt.Height, 1, 4096);
                auto target = RenderTargetRegistry::Acquire(rt.Name);
                if (!target)
                    continue;
                target->Resize(width, height);
                HDRTarget& hdr = s_HDR[rt.Name];
                EnsureHDR(hdr, width, height);

                glBindFramebuffer(GL_FRAMEBUFFER, hdr.Framebuffer);
                glViewport(0, 0, (GLsizei)width, (GLsizei)height);
                glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
                glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
                glEnable(GL_DEPTH_TEST);
                scene.RenderSceneFromCamera(e, width, height);

                // Çözüm: ton eşleme + sRGB, herkesin örneklediği dokuya
                glBindFramebuffer(GL_FRAMEBUFFER, target->GetFramebuffer());
                glViewport(0, 0, (GLsizei)width, (GLsizei)height);
                glDisable(GL_DEPTH_TEST);
                glDisable(GL_BLEND);
                glDisable(GL_CULL_FACE);
                s_Resolve->Bind();
                glBindTextureUnit(0, hdr.Color);
                s_Resolve->SetInt("u_Source", 0);
                const float ev = scene.GetRenderSettings().Post.Exposure + rt.Exposure;
                s_Resolve->SetFloat("u_Exposure", std::exp2(ev));
                PostProcess::DrawFullscreenQuad();

                target->RenderCount++;
                rt.RenderedOnce = true;
                rt.Renders++;
            }

            glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)previousFramebuffer);
            glViewport(previousViewport[0], previousViewport[1], previousViewport[2], previousViewport[3]);
            if (depthTest) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
            if (blend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
            if (cull) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
        }

        void Shutdown()
        {
            GameWindows::Shutdown();   // pencerelerin blit gölgelendiricisi de bağlam kapanmadan
            for (auto& [name, hdr] : s_HDR)
                ReleaseHDR(hdr);
            s_HDR.clear();
            s_LastFrame.clear();
            s_Resolve.reset();
            RenderTargetRegistry::Clear();
        }
    }

    void RegisterRenderTargetComponents()
    {
        using C = RenderTargetComponent;
        ComponentDef<C>("Render Target", "RenderTargetComponent", "Cameras", ICON_FA_TV)
            .Describe("Godot SubViewport / Unity Render Texture: this camera renders into the texture rt://<Name> instead of the "
                      "screen. Use that path in any texture slot (Mesh Renderer, Sprite, UI Raw Image, material) for security "
                      "monitors, minimaps, mirrors or portals.")
            .Requires("CameraComponent")
            .Field("Asset", "Target Texture", &C::Asset, FieldKind::Asset).Filter(".cqrt")
                .Tooltip("A Render Texture asset (.cqrt, Create > Render Texture). Sets the name and size below.")
            .Field("Name", "Name", &C::Name).Tooltip("Texture path is rt://<Name>")
            .Field("Width", "Width", &C::Width).Range(1.0f, 4096.0f, 1.0f)
            .Field("Height", "Height", &C::Height).Range(1.0f, 4096.0f, 1.0f)
            .Field("UpdateMode", "Update Mode", &C::UpdateMode).Options({ "Always", "Once", "Disabled" })
            .Field("Exposure", "Exposure (EV)", &C::Exposure).Range(-10.0f, 10.0f, 0.05f)
            .Field("Renders", "Renders", &C::Renders).ReadOnly();
    }
}
