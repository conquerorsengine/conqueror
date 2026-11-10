#include "RuntimeLayer.h"
#ifdef __ANDROID__
    #include "Platform/Android/AndroidPlatform.h"
#endif
#include "Scene/PixelPerfect.h"
#include "Scene/PostProcessVolume.h"
#include "Core/Math/Math.h"
#include <algorithm>
#include "Core/Project/Project.h"
#include "Core/Utils/Platform.h"
#include "Core/Project/ProjectSerializer.h"
#include "Core/Console/ConsoleVariables.h"
#include "Core/Serialization/SceneSerializer.h"
#include "Core/Logging/Log.h"
#include "Core/Application.h"
#include "Renderer/Renderer.h"
#include "Core/Events/MouseEvent.h"
#include "Core/Events/KeyEvent.h"
#include "Core/Input/Input.h"
#include "Renderer/Renderer2D.h"
#include "Renderer/Utilities/Renderer3D/Renderer3D.h"
#include "Renderer/RHI/RenderCommand.h"
#include "Renderer/PostProcess.h"
#include "Core/Time/TimeManager.h"
#include "Core/Events/ApplicationEvent.h"
#include "Core/Input/GameInput.h"
#include "Scene/SceneManager.h"
#include <GLFW/glfw3.h>
#include "Core/Automation/Automation.h"
#include "Scene/Components.h"
#include <cstdlib>
#include <glm/gtc/matrix_transform.hpp>
#include "Renderer/RHI/Texture.h"
#include "Core/Utils/FileSystem.h"

namespace Conqueror::Runtime
{
    RuntimeLayer::RuntimeLayer(const std::filesystem::path& projectPath)
        : Layer("RuntimeLayer"), m_ProjectPath(projectPath)
    {
    }

    void RuntimeLayer::OnAttach()
    {
        // Konsol değişkenlerinin (r.*, ai.ShowNavMesh ...) etkilediği sahne
        ConsoleSystem::SetSceneProvider([this]() { return m_ActiveScene.get(); });
        // Derlenmiş oyun testi için otomasyon komutları (CQ_AUTOMATION ile)
        // game expect_scene <name> | expect_entity <name> [present|absent] | report
        Automation::RegisterCommand("game", [this](const Automation::CommandArgs& a, std::string& err) {
            if (a.size() < 2) { err = "game <expect_scene|expect_entity|report>"; return false; }
            if (a[1] == "expect_scene" && a.size() > 2)
            {
                const std::string name = SceneManager::GetActiveSceneName();
                if (name != a[2]) { err = "scene is '" + name + "'"; return false; }
                return true;
            }
            if (a[1] == "expect_entity" && a.size() > 2)
            {
                bool found = false;
                if (m_ActiveScene)
                    m_ActiveScene->m_Registry.view<TagComponent>().each([&](auto, auto& tag) { if (tag.Tag == a[2]) found = true; });
                const bool want = a.size() < 4 || a[3] != "absent";
                if (found != want) { err = a[2] + (want ? " missing" : " still present"); return false; }
                return true;
            }
            if (a[1] == "report")
            {
                auto& window = Application::Get().GetWindow();
                Automation::Report("GAME scene=" + SceneManager::GetActiveSceneName() + " index=" + std::to_string(SceneManager::GetActiveSceneIndex()) +
                                   " window=" + std::to_string(window.GetWidth()) + "x" + std::to_string(window.GetHeight()));
                return true;
            }
            err = "unknown game command " + a[1];
            return false;
        });

        SetupSplash();
        if (m_SplashActive)
            return;   // proje, logo tam görünür olunca OnUpdate'te yüklenir
        if (!LoadProject())
        {
            CQ_CORE_ERROR("Failed to load project at runtime!");
            Application::Get().Close();
        }
        else
        {
            m_ProjectLoaded = true;
            uint32_t width = Application::Get().GetWindow().GetWidth();
            uint32_t height = Application::Get().GetWindow().GetHeight();
            RenderCommand::SetViewport(0, 0, width, height);
        }
    }

    void RuntimeLayer::SetupSplash()
    {
        // Proje dosyasındaki açılış ayarı (sahne yüklenmeden önce okunur)
        Ref<Project> project;
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(m_ProjectPath, ec))
            if (entry.path().extension() == ".cqproj")
            {
                auto p = std::make_shared<Project>();
                if (ProjectSerializer(p).Deserialize(entry.path()))
                    project = p;
                break;
            }
        if (!project)
            return;
        const auto& splash = project->GetConfig().Player.Splash;
        // Otomasyonla çalışan derlenmiş oyun testleri gecikmesin (CQ_SPLASH=1 açılış ekranını yine de gösterir)
        const char* force = std::getenv("CQ_SPLASH");
        if (!splash.Show || (Automation::IsActive() && !(force && *force == '1')))
            return;
        m_SplashSettings = splash;
        auto load = [&](const std::string& relative) -> std::shared_ptr<Texture2D> {
            const auto path = relative.empty() ? std::filesystem::path(FileSystem::ResolveResourcePath("Resources/Logo/Conqueror_Logo.png"))
                                               : m_ProjectPath / relative;
            if (!std::filesystem::exists(path, ec))
                return nullptr;
            auto tex = Texture2D::Create(path.string());
            return tex && tex->GetWidth() > 0 ? tex : nullptr;
        };
        // Yüklenemeyen proje logoları atlanır (motor logosu kalır)
        m_SplashSettings.Logos.clear();
        for (const auto& l : splash.Logos)
            if (auto tex = load(l.Path))
            {
                m_SplashTextures[l.Path] = tex;
                m_SplashSettings.Logos.push_back(l);
            }
        if (auto engine = load({}))
            m_SplashTextures[{}] = engine;
        else
            m_SplashSettings.ShowEngineLogo = false;
        if (!splash.BackgroundImage.empty())
            m_SplashBackgroundImage = load(splash.BackgroundImage);
        m_SplashSteps = Splash::BuildSteps(m_SplashSettings);
        // Hiç çizilecek logo yoksa açılış ekranı yok
        bool any = false;
        for (const auto& st : m_SplashSteps)
            for (const auto& it : st.Items)
                any = any || m_SplashTextures.count(it.EngineLogo ? std::string() : it.Image);
        if (!any)
        {
            m_SplashSteps.clear();
            m_SplashTextures.clear();
            return;
        }
        m_SplashActive = true;
        m_SplashTime = 0.0f;
        m_SplashTotal = Splash::TotalDuration(m_SplashSteps);
        m_SplashBackground = { splash.BackgroundR, splash.BackgroundG, splash.BackgroundB };
        CQ_CORE_INFO("Splash screen: {0} step(s), {1:.1f} s", m_SplashSteps.size(), m_SplashTotal);
    }

    void RuntimeLayer::DrawSplash()
    {
        auto& window = Application::Get().GetWindow();
        const float w = (float)std::max(window.GetWidth(), 1u);
        const float h = (float)std::max(window.GetHeight(), 1u);
        RenderCommand::SetViewport(0, 0, (uint32_t)w, (uint32_t)h);
        RenderCommand::SetClearColor({ m_SplashBackground.r, m_SplashBackground.g, m_SplashBackground.b, 1.0f });
        RenderCommand::Clear();
        auto textureOf = [this](const Splash::Item& item) -> std::shared_ptr<Texture2D> {
            auto it = m_SplashTextures.find(item.EngineLogo ? std::string() : item.Image);
            return it != m_SplashTextures.end() ? it->second : nullptr;
        };
        std::vector<Splash::DrawItem> items;
        Splash::Layout(m_SplashSettings, m_SplashSteps, m_SplashTime, { w, h },
                       [&](const Splash::Item& item) {
                           auto tex = textureOf(item);
                           return tex && tex->GetHeight() > 0 ? (float)tex->GetWidth() / (float)tex->GetHeight() : 0.0f;
                       },
                       items);
        const glm::mat4 ortho = glm::ortho(0.0f, w, 0.0f, h, -1.0f, 1.0f);
        Renderer2D::BeginScene(ortho, false);
        Renderer2D::SetOutputMode(0);
        // Yerleşim sol üst kökenli, Renderer2D sol alt
        auto quad = [&](const std::shared_ptr<Texture2D>& tex, const glm::vec2& center, const glm::vec2& size, float alpha) {
            const glm::mat4 transform = glm::scale(glm::translate(glm::mat4(1.0f), glm::vec3(center.x, h - center.y, 0.0f)), glm::vec3(size, 1.0f));
            Renderer2D::DrawQuad(transform, tex, { 0.0f, 0.0f }, { 1.0f, 1.0f }, { 1.0f, 1.0f, 1.0f, alpha });
        };
        if (m_SplashBackgroundImage && m_SplashBackgroundImage->GetHeight() > 0)
        {
            glm::vec2 c, size;
            Splash::CoverRect((float)m_SplashBackgroundImage->GetWidth() / (float)m_SplashBackgroundImage->GetHeight(), { w, h }, c, size);
            quad(m_SplashBackgroundImage, c, size, 1.0f);
        }
        for (const auto& d : items)
            if (auto tex = textureOf(*d.Source))
                quad(tex, d.Center, d.Size, d.Alpha);
        Renderer2D::EndScene();
        Renderer2D::SetOutputMode(-1);
    }

    void RuntimeLayer::OnDetach()
    {
        if (m_ActiveScene)
        {
            m_ActiveScene->OnRuntimeStop();
            m_ActiveScene->StopAllAudio();
        }
    }

    void RuntimeLayer::OnUpdate(Timestep ts)
    {
        if (m_SplashActive)
        {
            // Sahne yüklemesinin uzun karesi süreyi atlatmasın
            m_SplashTime += std::min(ts.GetSeconds(), 1.0f / 20.0f);
            DrawSplash();
            // İlk logo tam görününce proje yüklenir (takılma logonun arkasında kalır)
            if (!m_ProjectLoaded && m_SplashTime >= std::min(0.5f, (m_SplashSteps.empty() ? 1.0f : m_SplashSteps.front().Duration) * 0.35f))
            {
                m_ProjectLoaded = true;
                if (!LoadProject())
                {
                    CQ_CORE_ERROR("Failed to load project at runtime!");
                    Application::Get().Close();
                    m_SplashActive = false;
                    return;
                }
            }
            if (m_ProjectLoaded && m_SplashTime >= m_SplashTotal)
            {
                m_SplashActive = false;
                m_SplashSteps.clear();
                m_SplashTextures.clear();
                m_SplashBackgroundImage.reset();
                CQ_CORE_INFO("Splash screen finished");
            }
            return;
        }
        if (!m_ActiveScene)
            return;

        // Betik istekleri (önceki karede): oyundan çık / sahne değiştir
        if (SceneManager::TakeQuitRequest())
        {
            Application::Get().Close();
            return;
        }
        if (SceneManager::HasPendingLoad())
            SwitchScene(SceneManager::TakePendingLoad());

        auto& window = Application::Get().GetWindow();

        // Oyunun imleç kilidi (editördeki gibi): Esc ya da pencere odağının kaybı kilidi bırakır, imleç pencerenin
        // ortasında çıkar; bırakılmışken oyun fareyi görmez. Pencereye herhangi bir fare tuşuyla tıklamak kilidi geri
        // getirir (o tık oyuna gitmez)
        if (Input::GetCursorMode() != 0)
        {
            const bool focused = glfwGetWindowAttrib(static_cast<GLFWwindow*>(window.GetNativeWindow()), GLFW_FOCUSED) != 0;
            const bool lostFocus = m_WindowWasFocused && !focused;
            m_WindowWasFocused = focused;
            if (Input::GetAppliedCursorMode() != 0 && (Input::IsKeyDown(Key::Escape) || lostFocus))
                Input::ReleaseCursor((float)window.GetWidth() * 0.5f, (float)window.GetHeight() * 0.5f);
            else if (Input::IsCursorReleased() && focused &&
                     (Input::IsMouseButtonDown(Mouse::ButtonLeft) || Input::IsMouseButtonDown(Mouse::ButtonRight) ||
                      Input::IsMouseButtonDown(Mouse::ButtonMiddle)))
            {
                Input::RecaptureCursor();
                GameInput::ConsumeHeldButtons();
            }
        }

        // Klavye/fare ImGui üzerinden (gerçek girdi + otomasyon testleri aynı yoldan)
        GameInput::FeedFromImGui(true, true);

        uint32_t width = std::max(window.GetWidth(), 1u);
        uint32_t height = std::max(window.GetHeight(), 1u);

        // HDR sahne -> post-process -> pencere
        auto& post = PostProcess::Default();
        // Pixel Perfect Camera: düşük çözünürlüklü sahne + tam sayı büyütme / çerçeve
        const PixelPerfect::View pixelView = PixelPerfect::Resolve(*m_ActiveScene, width, height);
        post.BeginScene(pixelView.RenderWidth, pixelView.RenderHeight);
        glm::vec3 bg = Math::SRGBToLinear(m_ActiveScene->ResolveCameraBackground(m_ActiveScene->GetPrimaryCameraEntity()).Color);
        RenderCommand::SetClearColor({ bg.r, bg.g, bg.b, 1.0f });
        RenderCommand::Clear();
        m_ActiveScene->OnUpdateRuntime(ts);
        post.EndScene();

        // Pencere içinde çizim alanı: Android Automotive'de araç çubuklarının arasındaki bölge (pencere boyutu zaten bu alan)
        int outX = 0, outY = 0;
#ifdef __ANDROID__
        {
            int x = 0, y = 0, w = 0, h = 0;
            if (Android::OutputViewport(x, y, w, h))
            {
                outX = x;
                outY = y;
                RenderCommand::SetClearColor({ 0.0f, 0.0f, 0.0f, 1.0f });
                RenderCommand::Clear();
            }
        }
#endif
        RenderCommand::SetViewport(outX, outY, width, height);
        if (pixelView.Active)
        {
            RenderCommand::SetClearColor({ 0.0f, 0.0f, 0.0f, 1.0f });
            RenderCommand::Clear();
            RenderCommand::SetViewport(outX + pixelView.Output.x, outY + pixelView.Output.y, pixelView.Output.z, pixelView.Output.w);
        }
        post.SetPointUpscale(pixelView.PointSample);
        RenderCamera camera;
        m_ActiveScene->GetRuntimeRenderCamera(pixelView.RenderWidth, pixelView.RenderHeight, camera);
        // Görünen gökyüzü her zaman ton eşlenir (boş sahnede beyaza patlamasın)
        const bool skyVisible = m_ActiveScene->GetRenderSettings().Environment.Sky != SkyMode::SolidColor
                             && m_ActiveScene->ResolveCameraBackground(m_ActiveScene->GetPrimaryCameraEntity()).DrawSky;
        post.Render(PostProcessVolumes::Resolve(*m_ActiveScene, m_ActiveScene->GetRenderSettings().Post, camera.Position), camera, TimeManager::GetTime(),
                    m_ActiveScene->HasRenderable3D() || skyVisible);
        RenderCommand::SetViewport(outX, outY, width, height);

        // UI (ekran üstü canvas'lar)
        glm::vec2 mouse = Input::GetMousePosition();
        bool down = Input::IsMouseButtonDown(Mouse::ButtonLeft);
        m_UIInput.Valid = true;
        m_UIInput.MousePosition = glm::vec2(mouse.x, (float)height - mouse.y);
        m_UIInput.MouseDown = down;
        m_UIInput.MousePressed = down && !m_PrevMouseDown;
        m_UIInput.MouseReleased = !down && m_PrevMouseDown;
        m_PrevMouseDown = down;
        m_ActiveScene->SetUIScreenSize({ (float)width, (float)height });
        UISystem::Update(*m_ActiveScene, { (float)width, (float)height }, m_UIInput, ts.GetSeconds());
        UISystem::RenderScreenSpace(*m_ActiveScene, { (float)width, (float)height });
        m_UIInput.Characters.clear();
        m_UIInput.Scroll = 0.0f;
        m_UIInput.Backspace = m_UIInput.Delete = m_UIInput.Enter = m_UIInput.Escape = false;
        m_UIInput.Left = m_UIInput.Right = m_UIInput.Home = m_UIInput.End = false;
        m_UIInput.Up = m_UIInput.Down = m_UIInput.Space = false;
    }

    void RuntimeLayer::OnEvent(Event& e)
    {
        EventDispatcher dispatcher(e);
        dispatcher.Dispatch<KeyTypedEvent>([this](KeyTypedEvent& ev)
        {
            m_UIInput.Characters.push_back((uint32_t)ev.GetKeyCode());
            return false;
        });
        dispatcher.Dispatch<MouseScrolledEvent>([this](MouseScrolledEvent& ev)
        {
            m_UIInput.Scroll += ev.GetYOffset();
            return false;
        });
        dispatcher.Dispatch<KeyPressedEvent>([this](KeyPressedEvent& ev)
        {
            switch (ev.GetKeyCode())
            {
                case Key::Backspace: m_UIInput.Backspace = true; break;
                case Key::Delete: m_UIInput.Delete = true; break;
                case Key::Enter: m_UIInput.Enter = true; break;
                case Key::Escape: m_UIInput.Escape = true; break;
                case Key::Left: m_UIInput.Left = true; break;
                case Key::Right: m_UIInput.Right = true; break;
                case Key::Home: m_UIInput.Home = true; break;
                case Key::End: m_UIInput.End = true; break;
                case Key::Up: m_UIInput.Up = true; break;
                case Key::Down: m_UIInput.Down = true; break;
                case Key::Space: m_UIInput.Space = true; break;
                default: break;
            }
            return false;
        });
        dispatcher.Dispatch<WindowResizeEvent>([this](WindowResizeEvent& ev)
        {
            if (m_ActiveScene && ev.GetWidth() > 0 && ev.GetHeight() > 0)
                m_ActiveScene->OnViewportResize(ev.GetWidth(), ev.GetHeight());
            return false;
        });
    }

    bool RuntimeLayer::LoadProject()
    {
        auto project = std::make_shared<Project>();
        ProjectSerializer serializer(project);
        
        // Find .cqproj file in m_ProjectPath
        std::filesystem::path projectFilePath;
        for (const auto& entry : std::filesystem::directory_iterator(m_ProjectPath))
        {
            if (entry.path().extension() == ".cqproj" || entry.path().extension() == ".cqproject")
            {
                projectFilePath = entry.path();
                break;
            }
        }
        
        if (projectFilePath.empty() || !serializer.Deserialize(projectFilePath))
        {
            CQ_CORE_ERROR("Could not find or deserialize project in {0}", m_ProjectPath.string());
            return false;
        }

        Project::SetActive(project);

        std::filesystem::path startScenePath = project->GetProjectDirectory() / project->GetConfig().StartScene;
        
        if (!std::filesystem::exists(startScenePath))
        {
            CQ_CORE_ERROR("Start scene missing: {0}", startScenePath.string());
            return false;
        }

        m_ActiveScene = std::make_shared<Scene>();
        SceneSerializer sceneSerializer(m_ActiveScene);
        if (!sceneSerializer.Deserialize(startScenePath))
        {
            CQ_CORE_ERROR("Failed to load start scene: {0}", startScenePath.string());
            return false;
        }

        // Oyuncu ayarları: tam ekran, yeniden boyutlandırma, vsync, simge
        const auto& player = project->GetConfig().Player;
        auto& window = Application::Get().GetWindow();
        window.SetVSync(player.VSync);
        window.SetResizable(player.Resizable);
        if (!player.Icon.empty())
            window.SetIcon((project->GetProjectDirectory() / player.Icon).string());
        else
            window.SetIcon(FileSystem::ResolveResourcePath("Resources/Logo/AppIcon/conqueror_256.png"));
        // Steam Deck'te pencere modu yerine kenarlıksız tam ekran (Masaüstü modunda da oyun ekranı doldurur)
        const int fullscreen = player.FullscreenMode != 0 ? player.FullscreenMode : (Platform::IsSteamDeck() ? 1 : 0);
        if (fullscreen != 0)
            window.SetFullscreenMode(fullscreen);

        CQ_CORE_INFO("Project loaded successfully, starting simulation.");
        SceneManager::Reset();
        SceneManager::SetActiveScenePath(startScenePath);
        m_ActiveScene->SetRuntimeScene(true);
        m_ActiveScene->OnRuntimeStart();
        
        // Ensure resize is handled initially
        m_ActiveScene->OnViewportResize(Application::Get().GetWindow().GetWidth(), Application::Get().GetWindow().GetHeight());

        return true;
    }

    void RuntimeLayer::SwitchScene(const std::filesystem::path& path)
    {
        auto scene = std::make_shared<Scene>();
        SceneSerializer serializer(scene);
        if (!serializer.Deserialize(path))
        {
            CQ_CORE_ERROR("LoadScene: could not load {0}", path.string());
            return;
        }
        // Oyun içi geçiş: betik çalışma zamanları (statik durum) korunur
        m_ActiveScene->OnRuntimeStop(false);
        m_ActiveScene->StopAllAudio();
        m_ActiveScene = scene;
        m_ActiveScene->SetRuntimeScene(true);
        m_ActiveScene->OnRuntimeStart(false);
        m_ActiveScene->OnViewportResize(Application::Get().GetWindow().GetWidth(), Application::Get().GetWindow().GetHeight());
        SceneManager::SetActiveScenePath(path);
        CQ_CORE_INFO("Loaded scene {0}", path.filename().string());
    }
}
