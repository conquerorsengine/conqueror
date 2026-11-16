#include "Core/Localization/Localization.h"
#include "Scene/GodotNodes.h"
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
#include "Renderer/GameViewModes.h"
#include "Core/Base/Version.h"
#include "Scripting/ScriptRuntime.h"
#include <imgui.h>
#include <cstdio>
#include <ctime>
#if defined(CQ_PLAYER_NFD)
    #include <nfd.h>
#endif

// Masaüstü oyuncusu (pencere boyutu değiştirilebilir, dosya iletişim kutusu, sürükle-bırak)
#if !defined(__ANDROID__) && !defined(__EMSCRIPTEN__) && !defined(CQ_PLATFORM_IOS) && !defined(CQ_PLATFORM_QNX)
    #define CQ_RUNTIME_DESKTOP 1
#endif

namespace Conqueror::Runtime
{
    RuntimeLayer::RuntimeLayer(const std::filesystem::path& projectPath, const std::filesystem::path& packagePath)
        : Layer("RuntimeLayer"), m_ProjectPath(projectPath), m_PackagePath(packagePath)
    {
    }

    RuntimeLayer::~RuntimeLayer()
    {
        // Çözme sürüyorsa iptal edilir (yarım klasör sonraki açılışta yeniden çözülür)
        m_UnpackCancel = true;
        if (m_UnpackThread.joinable())
            m_UnpackThread.join();
    }

    void RuntimeLayer::OnAttach()
    {
        // Konsol değişkenlerinin (r.*, ai.ShowNavMesh ...) etkilediği sahne
        ConsoleSystem::SetSceneProvider([this]() { return m_ActiveScene.get(); });
        // Derlenmiş oyun testi için otomasyon komutları (CQ_AUTOMATION ile)
        // game expect_scene <name> | expect_entity <name> [present|absent] | report
        // Taşınabilir paket / oyuncu açılış ekranı: game mode <launcher|unpacking|project|error> | expect_error <metin> | open <paket.cqspace>
        Automation::RegisterCommand("game", [this](const Automation::CommandArgs& a, std::string& err) {
            if (a.size() < 2) { err = "game <expect_scene|expect_entity|mode|expect_error|open|report>"; return false; }
            auto modeName = [this]() {
                switch (m_Mode)
                {
                    case Mode::Launcher: return "launcher";
                    case Mode::Unpacking: return "unpacking";
                    case Mode::Error: return "error";
                    default: return "project";
                }
            };
            if (a[1] == "mode" && a.size() > 2)
            {
                if (a[2] != modeName()) { err = std::string("mode is '") + modeName() + "'" + (m_ErrorText.empty() ? "" : ": " + m_ErrorText); return false; }
                return true;
            }
            if (a[1] == "expect_error" && a.size() > 2)
            {
                std::string want = a[2];
                for (size_t i = 3; i < a.size(); ++i)
                    want += " " + a[i];
                if (m_Mode != Mode::Error) { err = std::string("mode is '") + modeName() + "'"; return false; }
                if (m_ErrorText.find(want) == std::string::npos) { err = "error text is '" + m_ErrorText + "'"; return false; }
                return true;
            }
            if (a[1] == "open" && a.size() > 2)
            {
                std::string path = a[2];
                for (size_t i = 3; i < a.size(); ++i)
                    path += " " + a[i];
                if (m_Mode != Mode::Launcher && m_Mode != Mode::Error) { err = "a game is already open"; return false; }
                m_FromLauncher = true;
                BeginOpenPackage(path);
                return true;
            }
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
            // expect_refuses_engine <sürüm> <metin>: açık paketin bildirimi o motor sürümünü isteseydi oyuncu reddeder miydi
            if (a[1] == "expect_refuses_engine" && a.size() > 3)
            {
                if (m_PackagePath.empty()) { err = "no package is open"; return false; }
                GamePackage::Manifest m = m_Manifest;
                m.EngineVersion = a[2];
                std::string reason;
                if (GamePackage::CheckCompatible(m, reason)) { err = "a package needing " + a[2] + " was accepted"; return false; }
                Automation::Report("GAME refuses: " + reason);
                if (reason.find(a[3]) == std::string::npos) { err = "reason: " + reason; return false; }
                return true;
            }
            if (a[1] == "report")
            {
                auto& window = Application::Get().GetWindow();
                Automation::Report("GAME scene=" + SceneManager::GetActiveSceneName() + " index=" + std::to_string(SceneManager::GetActiveSceneIndex()) +
                                   " window=" + std::to_string(window.GetWidth()) + "x" + std::to_string(window.GetHeight()) + " mode=" + modeName() +
                                   (m_PackagePath.empty() ? std::string() : " package=" + m_PackagePath.string() + " game=" + m_Manifest.Name + " " +
                                                                            m_Manifest.Version));
                return true;
            }
            err = "unknown game command " + a[1];
            return false;
        });

        // Taşınabilir oyun paketi (komut satırı / dosya ilişkilendirmesi) ya da hiçbir oyun yok: oyuncunun açılış ekranı
        if (!m_PackagePath.empty())
        {
            BeginOpenPackage(m_PackagePath);
            return;
        }
        if (m_ProjectPath.empty())
        {
            ShowLauncher();
            return;
        }
        StartProject();
    }

    void RuntimeLayer::StartProject()
    {
        m_Mode = Mode::Project;
        m_LoadError.clear();
        SetupSplash();
        if (m_SplashActive)
            return;   // proje, logo tam görünür olunca OnUpdate'te yüklenir
        m_ProjectLoaded = true;
        if (!LoadProject())
            ProjectLoadFailed();
        else
        {
            uint32_t width = Application::Get().GetWindow().GetWidth();
            uint32_t height = Application::Get().GetWindow().GetHeight();
            RenderCommand::SetViewport(0, 0, width, height);
        }
    }

    void RuntimeLayer::ProjectLoadFailed()
    {
        CQ_CORE_ERROR("Failed to load project at runtime!");
        m_SplashActive = false;
        m_ProjectLoaded = false;
        if (m_ActiveScene)
        {
            m_ActiveScene->OnRuntimeStop();
            m_ActiveScene->StopAllAudio();
            m_ActiveScene.reset();
        }
        // Bağımsız derleme (oyun klasörü): eskisi gibi kapanır. Paketten açılan oyun: hata ekranı (başka oyun açılabilir)
        if (m_PackagePath.empty())
        {
            Application::Get().Close();
            return;
        }
        if (Project::GetActive())
            Project::SetActive(nullptr);
        ShowError("\"" + (m_Manifest.Name.empty() ? m_PackagePath.filename().string() : m_Manifest.Name) + "\" could not be started",
                  m_LoadError.empty() ? std::string("The game's start scene could not be loaded. See the log for details.") : m_LoadError);
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
                    ProjectLoadFailed();
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
        // Oyuncunun kendi ekranları (açılış, paket çözülürken, hata): yalnız arayüz (OnImGuiRender)
        if (m_Mode == Mode::Unpacking)
        {
            if (m_UnpackDone)
                FinishOpenPackage();
            ClearScreen();
            return;
        }
        if (m_Mode == Mode::Launcher || m_Mode == Mode::Error)
        {
            PollOpenRequests();
            ClearScreen();
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
        // Development Build: Unreal gibi F1 tel kafes / F2 ışıksız / F3 ışıklı / F4 yalnız ışık (son oyunda yok)
        const bool devViewModes = Project::GetActive() && Project::GetActive()->GetConfig().Build.DevelopmentBuild;
        if (devViewModes)
            GameViewModes::HandleHotkeys();
        else
            GameViewModes::Set(GameViewModes::Mode::Lit);
        GameViewModes::BeginScene();
        m_ActiveScene->OnUpdateRuntime(ts);
        GameViewModes::EndScene();
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
        PostProcessSettings resolvedPost = PostProcessVolumes::Resolve(*m_ActiveScene, m_ActiveScene->GetRenderSettings().Post, camera.Position);
        if (!GameViewModes::PostEffectsAllowed())
            resolvedPost.Enabled = false;
        post.Render(resolvedPost, camera, TimeManager::GetTime(), m_ActiveScene->HasRenderable3D() || skyVisible);
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
        // Açılış / hata ekranında pencereye bırakılan .cqspace açılır
        dispatcher.Dispatch<WindowFileDropEvent>([this](WindowFileDropEvent& ev)
        {
            if (m_Mode != Mode::Launcher && m_Mode != Mode::Error)
                return false;
            for (const auto& p : ev.GetPaths())
                if (GamePackage::IsPackagePath(p))
                {
                    m_FromLauncher = true;
                    BeginOpenPackage(p);
                    return true;
                }
            if (!ev.GetPaths().empty())
                m_LauncherMessage = std::filesystem::path(ev.GetPaths().front()).filename().string() + " is not a Conqueror game (.cqspace).";
            return true;
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
            m_LoadError = "The game's project file could not be read.";
            return false;
        }

        Project::SetActive(project);
        Localization::SetRemapsActive(true);   // dile göre doku / ses / yazı tipi karşılıkları (Project Settings > Localization > Remaps)

        // Taşınabilir paket: betik dillerinin çalışma zamanları bu oyuncuda var mı (Python, .NET SDK, JDK); betik motoru artık kurulu
        if (!m_PackagePath.empty())
        {
            std::string reason;
            if (!GamePackage::CheckFeaturesAvailable(m_Manifest, reason))
            {
                CQ_CORE_ERROR("[GamePackage] {0}", reason);
                m_LoadError = reason;
                return false;
            }
        }

        std::filesystem::path startScenePath = project->GetProjectDirectory() / project->GetConfig().StartScene;
        
        if (!std::filesystem::exists(startScenePath))
        {
            CQ_CORE_ERROR("Start scene missing: {0}", startScenePath.string());
            m_LoadError = "The start scene " + project->GetConfig().StartScene + " is missing.";
            return false;
        }

        m_ActiveScene = std::make_shared<Scene>();
        SceneSerializer sceneSerializer(m_ActiveScene);
        if (!sceneSerializer.Deserialize(startScenePath))
        {
            CQ_CORE_ERROR("Failed to load start scene: {0}", startScenePath.string());
            m_LoadError = "The start scene " + project->GetConfig().StartScene + " could not be loaded.";
            m_ActiveScene.reset();
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
        GodotNodes::InstantiateAutoloads(*m_ActiveScene);   // Project Settings > Autoload: sahneden önce, sahne değişince kalır
        m_ActiveScene->OnRuntimeStart();
        
        // Ensure resize is handled initially
        m_ActiveScene->OnViewportResize(Application::Get().GetWindow().GetWidth(), Application::Get().GetWindow().GetHeight());

        return true;
    }

    void RuntimeLayer::SwitchScene(const std::filesystem::path& path)
    {
        auto scene = std::make_shared<Scene>();
        // DontDestroyOnLoad / Autoload nesneleri yeni sahneye (yüklemeden önce: aynı tanıtıcılar boş sahnede serbest)
        if (m_ActiveScene)
            scene->AdoptPersistent(*m_ActiveScene);
        SceneSerializer serializer(scene);
        if (!serializer.Deserialize(path))
        {
            CQ_CORE_ERROR("LoadScene: could not load {0}", path.string());
            return;
        }
        if (m_ActiveScene)
            scene->FinishAdoption(*m_ActiveScene);
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

    // ---- Taşınabilir oyun paketi ve oyuncunun açılış ekranı ----

    void RuntimeLayer::ClearScreen()
    {
        auto& window = Application::Get().GetWindow();
        RenderCommand::SetViewport(0, 0, std::max(window.GetWidth(), 1u), std::max(window.GetHeight(), 1u));
        RenderCommand::SetClearColor({ 0.067f, 0.071f, 0.086f, 1.0f });
        RenderCommand::Clear();
    }

    void RuntimeLayer::BeginOpenPackage(const std::filesystem::path& package)
    {
        if (m_UnpackThread.joinable())
            m_UnpackThread.join();
        std::error_code ec;
        std::filesystem::path abs = std::filesystem::absolute(package, ec);
        m_PackagePath = ec ? package : abs.lexically_normal();
        m_Mode = Mode::Unpacking;
        m_LauncherMessage.clear();
        m_UnpackDone = false;
        m_UnpackCancel = false;
        m_UnpackProgress = 0.0f;
        m_UnpackOk = false;
        m_UnpackError.clear();
        m_Manifest = {};
        CQ_CORE_INFO("[GamePackage] Opening {0}", m_PackagePath.string());
        // Bildirim, uyumluluk (motor sürümü, özellikler) ve ilk açılışta önbelleğe çözme arka planda: büyük oyunda pencere donmasın
        const std::string file = m_PackagePath.string();
        m_UnpackThread = std::thread([this, file]() {
            GamePackage::Manifest manifest;
            std::filesystem::path dir;
            std::string error;
            const bool ok = GamePackage::Open(file, manifest, dir, error, [this](float p) {
                m_UnpackProgress = p;
                return !m_UnpackCancel.load();
            });
            m_Manifest = manifest;
            m_UnpackedDir = dir;
            m_UnpackError = error;
            m_UnpackOk = ok;
            m_UnpackDone = true;   // ana iş parçacığı bunu gördükten sonra (join ile) alanları okur
        });
    }

    void RuntimeLayer::FinishOpenPackage()
    {
        if (m_UnpackThread.joinable())
            m_UnpackThread.join();
        if (!m_UnpackOk)
        {
            const std::string name = m_Manifest.Name.empty() ? m_PackagePath.filename().string() : m_Manifest.Name;
            ShowError("\"" + name + "\" cannot be opened", m_UnpackError);
            return;
        }
        GamePackage::AddRecent(m_PackagePath.string(), m_Manifest);
        m_ProjectPath = m_UnpackedDir;
        CQ_CORE_INFO("[GamePackage] {0} {1} (made with {2}, needs engine {3}+), scripts: {4}", m_Manifest.Name, m_Manifest.Version,
                     m_Manifest.CreatedBy, m_Manifest.EngineVersion, m_Manifest.ScriptLanguages.empty() ? std::string("none") : [&]() {
                         std::string s;
                         for (const auto& l : m_Manifest.ScriptLanguages)
                             s += (s.empty() ? "" : ", ") + l;
                         return s;
                     }());
        // Açılış ekranından açılan oyun: pencere başlığı, boyutu ve betik hata ayıklaması burada (komut satırında RuntimeApp yapar)
        if (m_FromLauncher)
        {
            std::string text;
            auto project = std::make_shared<Project>();
            if (GamePackage::ReadFile(m_PackagePath.string(), m_Manifest.ProjectFile, text) &&
                ProjectSerializer(project).DeserializeText(text, m_UnpackedDir / m_Manifest.ProjectFile))
            {
                const auto& cfg = project->GetConfig();
                auto& window = Application::Get().GetWindow();
                window.SetTitle(cfg.Name.empty() ? m_Manifest.Name : cfg.Name);
                if (cfg.Build.DevelopmentBuild && cfg.Build.ScriptDebugging)
                    Scripting::ScriptDebugMode() = true;
#if defined(CQ_RUNTIME_DESKTOP)
                if (cfg.Player.Width > 0 && cfg.Player.Height > 0 && cfg.Player.FullscreenMode == 0)
                    glfwSetWindowSize(static_cast<GLFWwindow*>(window.GetNativeWindow()), cfg.Player.Width, cfg.Player.Height);
#endif
                if (!cfg.Player.GraphicsAPI.empty() && cfg.Player.GraphicsAPI != "Auto")
                    CQ_CORE_WARN("[GamePackage] {0} asks for the {1} graphics API; opened from the player's start screen it uses the current one "
                                 "(open the .cqspace file directly to use {1})", m_Manifest.Name, cfg.Player.GraphicsAPI);
            }
        }
        StartProject();
    }

    void RuntimeLayer::ShowError(const std::string& title, const std::string& message)
    {
        m_Mode = Mode::Error;
        m_ErrorTitle = title;
        m_ErrorText = message;
        CQ_CORE_ERROR("[GamePackage] {0}: {1}", title, message);
        std::fprintf(stderr, "%s\n%s\n", title.c_str(), message.c_str());
        Application::Get().GetWindow().SetTitle("Conqueror Player");
    }

    void RuntimeLayer::ShowLauncher()
    {
        m_Mode = Mode::Launcher;
        m_Recent = GamePackage::LoadRecent();
        m_FoundGames.clear();
        std::error_code ec;
        for (const auto& folder : GamePackage::GameFolders())
            for (const auto& entry : std::filesystem::directory_iterator(folder, ec))
                if (entry.is_regular_file(ec) && GamePackage::IsPackagePath(entry.path().string()))
                    m_FoundGames.push_back(entry.path());
        std::sort(m_FoundGames.begin(), m_FoundGames.end());
        Application::Get().GetWindow().SetTitle("Conqueror Player");
    }

    void RuntimeLayer::PollOpenRequests()
    {
        // Platform katmanından gelen istek (iOS belge açma ...)
        std::string request;
        if (GamePackage::TakeOpenRequest(request))
        {
            m_FromLauncher = true;
            BeginOpenPackage(request);
            return;
        }
        // Linux dosya iletişim kutusunun sonucu
        std::string picked, error;
        {
            std::lock_guard lock(m_Dialog->Mutex);
            if (m_Dialog->Done)
            {
                m_Dialog->Done = false;
                picked = m_Dialog->Result;
                error = m_Dialog->Error;
            }
        }
        if (!error.empty())
            m_LauncherMessage = error;
        if (!picked.empty())
        {
            m_FromLauncher = true;
            BeginOpenPackage(picked);
        }
    }

    void RuntimeLayer::OpenFileDialog()
    {
        m_LauncherMessage.clear();
#if defined(CQ_PLAYER_NFD)
        // Windows / macOS: sistemin dosya iletişim kutusu (kalıcı pencere, ana iş parçacığında)
        if (NFD_Init() != NFD_OKAY)
        {
            m_LauncherMessage = std::string("The file dialog could not be opened: ") + (NFD_GetError() ? NFD_GetError() : "unknown error");
            return;
        }
        nfdu8char_t* out = nullptr;
        nfdu8filteritem_t filters[1] = { { "Conqueror Game", "cqspace" } };
        const nfdresult_t result = NFD_OpenDialogU8(&out, filters, 1, nullptr);
        std::string path;
        if (result == NFD_OKAY && out)
        {
            path = out;
            NFD_FreePathU8(out);
        }
        else if (result == NFD_ERROR)
            m_LauncherMessage = std::string("The file dialog could not be opened: ") + (NFD_GetError() ? NFD_GetError() : "unknown error");
        NFD_Quit();
        if (!path.empty())
        {
            m_FromLauncher = true;
            BeginOpenPackage(path);
        }
#elif defined(CQ_RUNTIME_DESKTOP) && !defined(_WIN32) && !defined(__APPLE__)
        // Linux / BSD: zenity ya da kdialog alt süreci (oyuncu GTK / Qt'ye bağlanmaz); kareler sürsün diye ayrı iş parçacığında
        {
            std::lock_guard lock(m_Dialog->Mutex);
            if (m_Dialog->Running)
                return;
            m_Dialog->Running = true;
            m_Dialog->Done = false;
            m_Dialog->Result.clear();
            m_Dialog->Error.clear();
        }
        std::thread([state = m_Dialog]() {
            const char* command =
                "if command -v zenity >/dev/null 2>&1; then zenity --file-selection --title='Open a Conqueror game' "
                "--file-filter='Conqueror games (*.cqspace) | *.cqspace' --file-filter='All files | *' 2>/dev/null; "
                "elif command -v kdialog >/dev/null 2>&1; then kdialog --title 'Open a Conqueror game' --getopenfilename \"$HOME\" "
                "'*.cqspace|Conqueror games (*.cqspace)' 2>/dev/null; else exit 127; fi";
            std::string output;
            int code = -1;
            if (FILE* pipe = popen(command, "r"))
            {
                char buffer[4096];
                size_t n = 0;
                while ((n = std::fread(buffer, 1, sizeof(buffer), pipe)) > 0)
                    output.append(buffer, n);
                const int status = pclose(pipe);
                code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
            }
            while (!output.empty() && (output.back() == '\n' || output.back() == '\r'))
                output.pop_back();
            std::lock_guard lock(state->Mutex);
            state->Running = false;
            state->Done = true;
            if (code == 127)
                state->Error = "No file dialog is available on this computer (install zenity or kdialog). Type the path of the .cqspace file "
                               "below or drop it on this window.";
            else if (code == 0)
                state->Result = output;
        }).detach();
#else
        m_LauncherMessage = "Open a .cqspace file with the Conqueror player from your file manager, browser or Files app.";
#endif
    }

    void RuntimeLayer::OnImGuiRender()
    {
        if (m_Mode == Mode::Launcher)
            DrawLauncher();
        else if (m_Mode == Mode::Unpacking)
            DrawUnpacking();
        else if (m_Mode == Mode::Error)
            DrawError();
    }

    namespace
    {
        // Oyuncunun ekranları: ana pencereyi dolduran, kaydedilmeyen pencere; içerik ortada en fazla 760 px genişlikte
        bool BeginPlayerScreen(const char* id, float& column)
        {
            const ImGuiViewport* vp = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(vp->WorkPos);
            ImGui::SetNextWindowSize(vp->WorkSize);
            ImGui::SetNextWindowViewport(vp->ID);
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.067f, 0.071f, 0.086f, 1.0f));
            const bool open = ImGui::Begin(id, nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                                        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoDocking);
            ImGui::PopStyleColor();
            column = std::min(760.0f, std::max(200.0f, vp->WorkSize.x - 32.0f));
            const float indent = std::max(16.0f, (vp->WorkSize.x - column) * 0.5f);
            ImGui::SetCursorPosY(std::max(24.0f, vp->WorkSize.y * 0.08f));
            ImGui::Indent(indent);
            return open;
        }

        void Heading(const char* text, float scale)
        {
            ImGui::SetWindowFontScale(scale);
            ImGui::TextUnformatted(text);
            ImGui::SetWindowFontScale(1.0f);
        }

        std::string FormatAge(int64_t when)
        {
            const int64_t now = (int64_t)std::time(nullptr);
            const int64_t d = std::max<int64_t>(0, now - when);
            if (when <= 0)
                return {};
            if (d < 120)
                return "just now";
            if (d < 7200)
                return std::to_string(d / 60) + " minutes ago";
            if (d < 172800)
                return std::to_string(d / 3600) + " hours ago";
            return std::to_string(d / 86400) + " days ago";
        }
    }

    void RuntimeLayer::DrawLauncher()
    {
        float column = 0.0f;
        BeginPlayerScreen("##ConquerorPlayer", column);
        Heading("Conqueror Player", 1.8f);
        ImGui::TextDisabled("Conqueror Engine %s - plays portable games (.cqspace) made with this engine version or older", CQ_ENGINE_VERSION);
        ImGui::Dummy(ImVec2(0.0f, 12.0f));

        bool dialogRunning = false;
        {
            std::lock_guard lock(m_Dialog->Mutex);
            dialogRunning = m_Dialog->Running;
        }
#if defined(CQ_PLAYER_NFD) || (defined(CQ_RUNTIME_DESKTOP) && !defined(_WIN32) && !defined(__APPLE__))
        ImGui::BeginDisabled(dialogRunning);
        if (ImGui::Button(dialogRunning ? "Waiting for the file dialog..." : "Open a .cqspace...", ImVec2(260.0f, 40.0f)))
            OpenFileDialog();
        ImGui::EndDisabled();
#else
        (void)dialogRunning;
        ImGui::TextWrapped("Open a .cqspace file with the Conqueror player from your file manager, browser or Files app.");
#endif
        ImGui::Dummy(ImVec2(0.0f, 4.0f));
        ImGui::SetNextItemWidth(std::max(120.0f, column - 90.0f));
        const bool enter = ImGui::InputTextWithHint("##path", "or type the path of a .cqspace file", m_PathInput, sizeof(m_PathInput),
                                                    ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        if ((ImGui::Button("Open", ImVec2(80.0f, 0.0f)) || enter) && m_PathInput[0])
        {
            std::string path = m_PathInput;
            while (!path.empty() && (path.back() == ' ' || path.back() == '"' || path.back() == '\''))
                path.pop_back();
            while (!path.empty() && (path.front() == ' ' || path.front() == '"' || path.front() == '\''))
                path.erase(path.begin());
            m_FromLauncher = true;
            BeginOpenPackage(path);
            ImGui::End();
            return;
        }
#if defined(CQ_RUNTIME_DESKTOP)
        ImGui::TextDisabled("You can also drop a .cqspace file on this window.");
#endif
        if (!m_LauncherMessage.empty())
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.78f, 0.35f, 1.0f));
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + column);
            ImGui::TextWrapped("%s", m_LauncherMessage.c_str());
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();
        }

        // Son açılan oyunlar ve oyun klasörlerinde bulunanlar: tıklayınca açılır
        std::string open;
        int remove = -1;
        auto gameRow = [&](const std::string& path, const std::string& name, const std::string& detail, int index) {
            std::error_code ec;
            const bool exists = std::filesystem::is_regular_file(path, ec);
            ImGui::PushID(path.c_str());
            const float rowWidth = column - (index >= 0 ? 90.0f : 0.0f);
            ImGui::BeginDisabled(!exists);
            if (ImGui::Selectable("##game", false, ImGuiSelectableFlags_AllowOverlap, ImVec2(rowWidth, ImGui::GetTextLineHeight() * 2.4f)))
                open = path;
            ImGui::EndDisabled();
            const ImVec2 min = ImGui::GetItemRectMin();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddText(ImVec2(min.x + 8.0f, min.y + 3.0f), ImGui::GetColorU32(exists ? ImGuiCol_Text : ImGuiCol_TextDisabled), name.c_str());
            const std::string sub = exists ? detail : detail + " (file not found)";
            dl->AddText(ImVec2(min.x + 8.0f, min.y + 3.0f + ImGui::GetTextLineHeightWithSpacing()), ImGui::GetColorU32(ImGuiCol_TextDisabled),
                        sub.c_str());
            if (index >= 0)
            {
                ImGui::SameLine();
                if (ImGui::Button("Remove", ImVec2(80.0f, 0.0f)))
                    remove = index;
            }
            ImGui::PopID();
        };
        ImGui::Dummy(ImVec2(0.0f, 12.0f));
        Heading("Recent games", 1.25f);
        ImGui::Separator();
        if (m_Recent.empty())
            ImGui::TextDisabled("No games opened yet.");
        for (int i = 0; i < (int)m_Recent.size(); ++i)
        {
            const auto& r = m_Recent[(size_t)i];
            std::string detail = r.Path;
            if (const std::string age = FormatAge(r.LastOpened); !age.empty())
                detail += "  -  " + age;
            gameRow(r.Path, (r.Name.empty() ? std::filesystem::path(r.Path).stem().string() : r.Name) + (r.Version.empty() ? "" : "  " + r.Version),
                    detail, i);
        }
        if (!m_FoundGames.empty())
        {
            ImGui::Dummy(ImVec2(0.0f, 12.0f));
            Heading("Games in the player's folders", 1.25f);
            ImGui::Separator();
            for (const auto& g : m_FoundGames)
                gameRow(g.string(), g.stem().string(), g.parent_path().string(), -1);
        }
        ImGui::End();
        if (remove >= 0 && remove < (int)m_Recent.size())
        {
            GamePackage::RemoveRecent(m_Recent[(size_t)remove].Path);
            m_Recent = GamePackage::LoadRecent();
        }
        if (!open.empty())
        {
            m_FromLauncher = true;
            BeginOpenPackage(open);
        }
    }

    void RuntimeLayer::DrawUnpacking()
    {
        float column = 0.0f;
        BeginPlayerScreen("##ConquerorPlayerOpening", column);
        Heading("Conqueror Player", 1.8f);
        ImGui::Dummy(ImVec2(0.0f, 12.0f));
        ImGui::Text("Opening %s ...", m_PackagePath.filename().string().c_str());
        ImGui::SetNextItemWidth(column);
        ImGui::ProgressBar(std::clamp(m_UnpackProgress.load(), 0.0f, 1.0f), ImVec2(column, 0.0f));
        ImGui::TextDisabled("The first start unpacks the game once; later starts are immediate.");
        ImGui::End();
    }

    void RuntimeLayer::DrawError()
    {
        float column = 0.0f;
        BeginPlayerScreen("##ConquerorPlayerError", column);
        Heading("Conqueror Player", 1.8f);
        ImGui::Dummy(ImVec2(0.0f, 12.0f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.45f, 0.4f, 1.0f));
        Heading(m_ErrorTitle.c_str(), 1.25f);
        ImGui::PopStyleColor();
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + column);
        ImGui::TextWrapped("%s", m_ErrorText.c_str());
        if (!m_PackagePath.empty())
            ImGui::TextDisabled("%s", m_PackagePath.string().c_str());
        ImGui::PopTextWrapPos();
        ImGui::Dummy(ImVec2(0.0f, 12.0f));
        bool launcher = ImGui::Button("Open Another Game", ImVec2(200.0f, 36.0f));
#if defined(CQ_RUNTIME_DESKTOP)
        ImGui::SameLine();
        if (ImGui::Button("Quit", ImVec2(120.0f, 36.0f)))
            Application::Get().Close();
#endif
        ImGui::End();
        if (launcher)
        {
            m_PackagePath.clear();
            m_Manifest = {};
            ShowLauncher();
        }
    }
}
