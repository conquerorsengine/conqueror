#include "GameWindow.h"

#include "Core/Application.h"
#include "Core/Logging/Log.h"
#include "Renderer/GraphicsAPIs/OpenGL/GLProfile.h"
#include "Renderer/RHI/Shader.h"
#include "Renderer/RenderTargetRegistry.h"
#include "Scene/CameraRenderTarget.h"
#include "Scene/ComponentRegistry.h"
#include "Scene/Components.h"
#include "Scene/Entity.h"
#include "Scene/Scene.h"
#include "Scene/Signals.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <IconsFontAwesome6.h>

#include <algorithm>
#include <memory>
#include <vector>

namespace Conqueror
{
    namespace
    {
        const char* kBlitVertex = R"(
            #version 450 core
            out vec2 v_UV;
            void main()
            {
                vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
                v_UV = p;
                gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
            }
        )";
        const char* kBlitFragment = R"(
            #version 450 core
            in vec2 v_UV;
            layout(location = 0) out vec4 o_Color;
            uniform sampler2D u_Source;
            void main() { o_Color = vec4(texture(u_Source, v_UV).rgb, 1.0); }
        )";
        std::shared_ptr<Shader>& BlitShader()
        {
            static std::shared_ptr<Shader> shader;
            return shader;
        }

        GLFWwindow* MainWindow() { return static_cast<GLFWwindow*>(Application::Get().GetWindow().GetNativeWindow()); }

        std::string TargetName(uint32_t entity) { return "GameWindow" + std::to_string(entity); }

        void Destroy(GameWindowComponent& w)
        {
            if (!w.Handle)
                return;
            GLFWwindow* main = MainWindow();
            GLFWwindow* win = static_cast<GLFWwindow*>(w.Handle);
            if (w.VertexArray)
            {
                glfwMakeContextCurrent(win);
                glDeleteVertexArrays(1, &w.VertexArray);
                w.VertexArray = 0;
                glfwMakeContextCurrent(main);
            }
            glfwDestroyWindow(win);
            w.Handle = nullptr;
            w.Open = false;
            w.Focused = false;
            w.AppliedSize = glm::vec2(0.0f);
            w.AppliedPosition = glm::vec2(-1e9f);
            w.AppliedTitle.clear();
            w.AppliedVisible = false;
        }

        bool Create(GameWindowComponent& w)
        {
            GLFWwindow* main = MainWindow();
            if (!main || glfwGetCurrentContext() != main)
                return false;
            glfwDefaultWindowHints();
            GLProfile::ApplyWindowHints();
            glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
            glfwWindowHint(GLFW_DECORATED, w.Borderless ? GLFW_FALSE : GLFW_TRUE);
            glfwWindowHint(GLFW_RESIZABLE, w.Resizable ? GLFW_TRUE : GLFW_FALSE);
            glfwWindowHint(GLFW_FLOATING, w.AlwaysOnTop ? GLFW_TRUE : GLFW_FALSE);
            const int width = std::max(1, (int)w.Size.x), height = std::max(1, (int)w.Size.y);
            GLFWwindow* win = glfwCreateWindow(width, height, w.Title.c_str(), nullptr, main);   // bağlam paylaşılır (dokular ortak)
            glfwDefaultWindowHints();
            if (!win)
            {
                CQ_CORE_WARN("[Window] Could not create the window \"{0}\"", w.Title);
                return false;
            }
            glfwMakeContextCurrent(win);
            glfwSwapInterval(0);   // ana döngüyü dikey eşitlemeyle bekletmesin
            glGenVertexArrays(1, &w.VertexArray);
            glfwMakeContextCurrent(main);
            w.Handle = win;
            w.Open = true;
            w.AppliedTitle = w.Title;
            w.AppliedSize = glm::vec2((float)width, (float)height);
            w.AppliedTop = w.AlwaysOnTop;
            return true;
        }

        // Kameranın doku hedefi pencerenin çerçeve boyutunda
        void BindCamera(Scene& scene, uint32_t entity, GameWindowComponent& w, int fbWidth, int fbHeight)
        {
            if (!w.Camera)
                return;
            Entity cam = scene.FindEntityByUUID(w.Camera);
            if (!cam || !cam.HasComponent<CameraComponent>())
                return;
            if (!cam.HasComponent<RenderTargetComponent>())
                cam.AddComponent<RenderTargetComponent>();
            auto& rt = cam.GetComponent<RenderTargetComponent>();
            rt.Asset.clear();
            rt.Name = TargetName(entity);
            rt.Width = std::max(1, fbWidth);
            rt.Height = std::max(1, fbHeight);
            rt.UpdateMode = w.Visible ? RenderTargetUpdate::Always : RenderTargetUpdate::Disabled;
        }

        void Present(uint32_t entity, GameWindowComponent& w, int fbWidth, int fbHeight)
        {
            GLFWwindow* main = MainWindow();
            GLFWwindow* win = static_cast<GLFWwindow*>(w.Handle);
            if (!BlitShader())
                BlitShader() = Shader::Create("GameWindowBlit", kBlitVertex, kBlitFragment);
            auto target = RenderTargetRegistry::Find(TargetName(entity));
            const bool program = BlitShader() && BlitShader()->IsValid();
            glfwMakeContextCurrent(win);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glViewport(0, 0, fbWidth, fbHeight);
            glDisable(GL_DEPTH_TEST);
            glDisable(GL_BLEND);
            glDisable(GL_CULL_FACE);
            glDisable(GL_SCISSOR_TEST);
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            if (target && program && target->GetRendererID())
            {
                BlitShader()->Bind();
                glBindTextureUnit(0, target->GetRendererID());
                BlitShader()->SetInt("u_Source", 0);
                glBindVertexArray(w.VertexArray);
                glDrawArrays(GL_TRIANGLES, 0, 3);
                glBindVertexArray(0);
            }
            if (w.ProbeRequest)
            {
                // Test: gösterilen görüntünün ortası (takastan önce arka arabellek)
                const int pw = std::max(1, fbWidth / 2), ph = std::max(1, fbHeight / 2);
                std::vector<unsigned char> px((size_t)pw * ph * 4);
                glPixelStorei(GL_PACK_ALIGNMENT, 1);
                glReadPixels(fbWidth / 4, fbHeight / 4, pw, ph, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
                double sum = 0.0;
                for (size_t i = 0; i < px.size(); i += 4)
                    sum += (px[i] + px[i + 1] + px[i + 2]) / 3.0;
                w.ProbeMean = (float)(sum / std::max<size_t>(1, px.size() / 4));
                w.ProbeRequest = false;
            }
            glfwSwapBuffers(win);
            glfwMakeContextCurrent(main);
            ++w.Frames;
        }

        std::vector<Scene*>& Scenes()
        {
            static std::vector<Scene*> scenes;
            return scenes;
        }
    }

    namespace GameWindows
    {
        void Update(Scene& scene)
        {
            auto view = scene.m_Registry.view<GameWindowComponent>();
            if (view.begin() == view.end())
                return;
            if (std::find(Scenes().begin(), Scenes().end(), &scene) == Scenes().end())
                Scenes().push_back(&scene);
            std::vector<uint32_t> closeRequests;
            for (auto e : view)
            {
                auto& w = view.get<GameWindowComponent>(e);
                const bool active = scene.IsEntityActive(e);
                if (!active)
                {
                    Destroy(w);
                    continue;
                }
                if (!w.Handle && !Create(w))
                    continue;
                GLFWwindow* win = static_cast<GLFWwindow*>(w.Handle);
                // Özellik değişiklikleri
                if (w.Title != w.AppliedTitle)
                {
                    glfwSetWindowTitle(win, w.Title.c_str());
                    w.AppliedTitle = w.Title;
                }
                if (w.AlwaysOnTop != w.AppliedTop)
                {
                    glfwSetWindowAttrib(win, GLFW_FLOATING, w.AlwaysOnTop ? GLFW_TRUE : GLFW_FALSE);
                    w.AppliedTop = w.AlwaysOnTop;
                }
                glfwSetWindowAttrib(win, GLFW_DECORATED, w.Borderless ? GLFW_FALSE : GLFW_TRUE);
                glfwSetWindowAttrib(win, GLFW_RESIZABLE, w.Resizable ? GLFW_TRUE : GLFW_FALSE);
                int cw = 0, ch = 0;
                glfwGetWindowSize(win, &cw, &ch);
                const glm::vec2 current((float)cw, (float)ch);
                if (w.Size != w.AppliedSize)
                {
                    glfwSetWindowSize(win, std::max(1, (int)w.Size.x), std::max(1, (int)w.Size.y));
                    w.AppliedSize = w.Size;
                }
                else if (current != w.AppliedSize && cw > 0 && ch > 0)
                {
                    w.Size = current;   // kullanıcı kenardan boyutlandırdı
                    w.AppliedSize = current;
                }
                if (w.Position != w.AppliedPosition)
                {
                    glfwSetWindowPos(win, (int)w.Position.x, (int)w.Position.y);
                    w.AppliedPosition = w.Position;
                }
                if (w.Visible != w.AppliedVisible)
                {
                    if (w.Visible) glfwShowWindow(win); else glfwHideWindow(win);
                    w.AppliedVisible = w.Visible;
                }
                w.Focused = glfwGetWindowAttrib(win, GLFW_FOCUSED) != 0;
                if (glfwWindowShouldClose(win))
                {
                    glfwSetWindowShouldClose(win, GLFW_FALSE);
                    closeRequests.push_back((uint32_t)e);
                    if (w.HideOnClose)
                        w.Visible = false;
                }
                int fbw = 0, fbh = 0;
                glfwGetFramebufferSize(win, &fbw, &fbh);
                BindCamera(scene, (uint32_t)e, w, fbw, fbh);
                if (w.Visible && fbw > 0 && fbh > 0)
                    Present((uint32_t)e, w, fbw, fbh);
            }
            // Bildirimler en sonda (betik pencereyi / nesneyi silebilir)
            for (uint32_t id : closeRequests)
            {
                Signals::Emit(scene, id, "close_requested", nullptr, 0);
                if (scene.m_Registry.valid((entt::entity)id))
                    scene.InvokeScriptMethod(id, "OnCloseRequested", nullptr, 0);
            }
        }

        void ForgetScene(Scene* scene)
        {
            auto it = std::find(Scenes().begin(), Scenes().end(), scene);
            if (it == Scenes().end())
                return;
            Scenes().erase(it);
            auto view = scene->m_Registry.view<GameWindowComponent>();
            for (auto e : view)
                Destroy(view.get<GameWindowComponent>(e));
            if (Scenes().empty())
                BlitShader().reset();
        }

        void Shutdown()
        {
            for (Scene* scene : std::vector<Scene*>(Scenes()))
                ForgetScene(scene);
            BlitShader().reset();
        }

        bool Show(Scene& scene, uint32_t entity, bool visible)
        {
            auto* w = scene.m_Registry.valid((entt::entity)entity) ? scene.m_Registry.try_get<GameWindowComponent>((entt::entity)entity) : nullptr;
            if (!w)
                return false;
            w->Visible = visible;
            return true;
        }

        bool GrabFocus(Scene& scene, uint32_t entity)
        {
            auto* w = scene.m_Registry.valid((entt::entity)entity) ? scene.m_Registry.try_get<GameWindowComponent>((entt::entity)entity) : nullptr;
            if (!w || !w->Handle)
                return false;
            glfwFocusWindow(static_cast<GLFWwindow*>(w->Handle));
            return true;
        }
    }

    template<> void Scene::OnComponentAdded<GameWindowComponent>(Entity, GameWindowComponent&) {}

    void RegisterGameWindowComponents()
    {
        using C = GameWindowComponent;
        ComponentDef<C>("Window", "GameWindowComponent", "Rendering", ICON_FA_WINDOW_RESTORE)
            .Describe("Godot Window: a separate operating-system window while the game runs. The Camera renders into it (give it its own "
                      "camera, not the main one); it can be moved and resized, and its close button emits close_requested and calls "
                      "OnCloseRequested on this object's scripts. Scripts: GameWindowShow / Hide / SetTitle / SetSize / GetSize / "
                      "GrabFocus / HasFocus. (Unity only has Display.Activate for a second monitor.)")
            .Field("Title", "Title", &C::Title)
            .Field("Size", "Size", &C::Size)
            .Field("Position", "Position", &C::Position).Tooltip("Screen position (on Wayland the compositor places windows itself)")
            .Field("Visible", "Visible", &C::Visible)
            .Field("Borderless", "Borderless", &C::Borderless)
            .Field("AlwaysOnTop", "Always On Top", &C::AlwaysOnTop)
            .Field("Resizable", "Resizable", &C::Resizable)
            .Field("HideOnClose", "Hide On Close", &C::HideOnClose).Tooltip("The close button hides the window; off: only the signal / OnCloseRequested")
            .Field("Camera", "Camera", &C::Camera).Tooltip("The camera that renders into this window")
            .Field("Open", "Open", &C::Open).ReadOnly()
            .Field("Focused", "Focused", &C::Focused).ReadOnly()
            .Field("Frames", "Frames", &C::Frames).ReadOnly()
            // Test (otomasyon): sonraki gösterimde pencerenin ortası ölçülür
            .Field("ProbeRequest", "Probe Request", &C::ProbeRequest).NoSerialize().VisibleIf([](const C&) { return false; })
            .Field("ProbeMean", "Probe Mean", &C::ProbeMean).ReadOnly().VisibleIf([](const C&) { return false; });
    }
}
