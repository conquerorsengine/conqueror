#include "ProjectSettingsPanel.h"
#include "Scene/Scene.h"
#include "Core/Project/Project.h"
#include "Core/Project/ProjectSettings.h"
#include "Core/Tiering/QualitySettings.h"
#include "Core/Input/Input.h"
#include "Core/Input/GameInput.h"
#include "Core/Utils/FileSystem.h"
#include "AISystem/Navigation/NavMesh.h"
#include "AISystem/Navigation/NavAgents.h"
#include "UI/EditorUI.h"
#include "Build/BuildPipeline.h"

#include <IconsFontAwesome6.h>
#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cstring>

// Project Settings: Player, Quality, Graphics, Audio, Input, Navigation, Editor, Script Execution Order (Unity eşdeğerleri)
namespace Conqueror::Editor
{
    void ProjectSettingsPanel::DrawPlayer()
    {
        auto& config = Project::GetActive()->GetConfig();
        auto& p = config.Player;
        UI::SubHeader("Identity");
        m_Dirty |= UI::InputText("Company Name", config.Organization);
        m_Dirty |= UI::InputText("Product Name", config.Name);
        m_Dirty |= UI::InputText("Version", config.Version);
        m_Dirty |= UI::InputText("Default Icon", p.Icon, "Project-relative .png used for the window and the executable.");

        UI::SubHeader("Resolution and Presentation");
        static const char* modes[] = { "Windowed", "Fullscreen Window (borderless)", "Exclusive Fullscreen" };
        m_Dirty |= UI::Combo("Fullscreen Mode", p.FullscreenMode, modes, 3);
        m_Dirty |= UI::DragInt("Default Width", p.Width, 1.0f, 160, 16384);
        m_Dirty |= UI::DragInt("Default Height", p.Height, 1.0f, 120, 16384);
        m_Dirty |= UI::Checkbox("Resizable Window", p.Resizable);
        m_Dirty |= UI::Checkbox("Run In Background", p.RunInBackground, "Keep running when the window loses focus (needed for multiplayer hosts).");
        m_Dirty |= UI::Checkbox("VSync", p.VSync);

        UI::SubHeader("Splash Image");
        {
            auto& sp = p.Splash;
            m_Dirty |= UI::Checkbox("Show Splash Screen", sp.Show, "Logos fade in and out while the first scene loads (built games only).");
            ImGui::BeginDisabled(!sp.Show);
            m_Dirty |= UI::InputText("Splash Logo", sp.Logo, "Project-relative .png shown first (transparent background looks best). Empty: engine logo only.");
            m_Dirty |= UI::Checkbox("Show Engine Logo", sp.ShowEngineLogo, "\"Conqueror's Engine\" logo after your logo.");
            m_Dirty |= UI::DragFloat("Duration Per Logo", sp.Duration, 0.05f, 0.5f, 10.0f, "%.1f s");
            UI::BeginProperty("Background");
            float bg[3] = { sp.BackgroundR, sp.BackgroundG, sp.BackgroundB };
            if (ImGui::ColorEdit3("##splashbg", bg))
            {
                sp.BackgroundR = bg[0];
                sp.BackgroundG = bg[1];
                sp.BackgroundB = bg[2];
                m_Dirty = true;
            }
            UI::EndProperty();
            ImGui::EndDisabled();
        }

        UI::SubHeader("Other Settings");
        // Build Settings ile aynı liste
        static const char* apis[] = { "Auto", "OpenGL", "Vulkan", "Direct3D11", "Direct3D12", "Metal" };
        int api = 0;
        for (int i = 0; i < 6; ++i)
            if (p.GraphicsAPI == apis[i])
                api = i;
        if (UI::Combo("Graphics API", api, apis, 6,
                      "Desktop builds and the editor (restart required). Auto = Direct3D 11 on Windows, Metal on macOS, OpenGL on "
                      "Linux and FreeBSD. Android, Automotive and QNX always use OpenGL ES 3; Web uses its own setting below."))
        {
            p.GraphicsAPI = apis[api];
            m_Dirty = true;
        }

        UI::SubHeader("Platform Settings");
        DrawPlatformSettings();
    }

    // Platform sekmeleri (Unity Player Settings): her platformun kendi ayarları ve nasıl derlendiği. Konsollar yok.
    void ProjectSettingsPanel::DrawPlatformSettings()
    {
        auto& config = Project::GetActive()->GetConfig();
        auto& p = config.Player;
        // Kimlik önerisi: derlemenin kullandığıyla aynı (com.<şirket>.<ürün>)
        const std::string suggestedId = BuildPipeline::DefaultIdentifier(config);
        auto status = [](const char* platform) {
            for (const auto& info : BuildPipeline::Platforms())
                if (std::strcmp(info.Name, platform) == 0)
                {
                    const bool buildable = BuildPipeline::CanBuild(platform);
                    const bool notYet = std::strncmp(info.Status, "Not buildable", 13) == 0;
                    UI::InfoBox(info.Status, notYet ? UI::InfoType::Warning : buildable ? UI::InfoType::Success : UI::InfoType::Info);
                }
        };
        auto idField = [&](const char* label, std::string& value) {
            const std::string tip = "Reverse-DNS identifier. Empty: " + suggestedId;
            m_Dirty |= UI::InputText(label, value, tip.c_str());
            if (value.empty())
                UI::Text("Used", "%s", suggestedId.c_str());
        };
        if (!ImGui::BeginTabBar("##platformTabs", ImGuiTabBarFlags_FittingPolicyScroll))
            return;
        // Dar panelde (sağ yuvaya yerleşik) sekmeler yalnız simge: hepsi tek satıra sığar, ad ipucunda (Unity'deki gibi)
        const char* names[] = { "Windows", "macOS", "Linux", "FreeBSD", "Android", "iOS", "Web", "QNX" };
        float fullWidth = 0.0f;
        for (const char* n : names)
            fullWidth += ImGui::CalcTextSize(n).x + ImGui::GetFontSize() * 1.6f + ImGui::GetStyle().FramePadding.x * 2.0f + ImGui::GetStyle().ItemInnerSpacing.x;
        const bool iconsOnly = fullWidth > ImGui::GetContentRegionAvail().x;
        auto tab = [&](const char* icon, const char* name) {
            const std::string label = std::string(icon) + (iconsOnly ? std::string() : std::string(" ") + name) + "###tab" + name;
            const bool open = ImGui::BeginTabItem(label.c_str());
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                ImGui::SetTooltip("%s", name);
            return open;
        };
        if (tab(ICON_FA_DESKTOP, "Windows"))
        {
            status("Windows");
            UI::Text("Graphics", "%s", p.GraphicsAPI == "Auto" ? "Auto (Direct3D 11)" : p.GraphicsAPI.c_str());
            ImGui::TextDisabled("Uses the identity, icon, resolution and presentation settings above.");
            ImGui::EndTabItem();
        }
        if (tab(ICON_FA_LAPTOP, "macOS"))
        {
            status("macOS");
            auto& m = p.Mac;
            idField("Bundle Identifier", m.BundleIdentifier);
            m_Dirty |= UI::InputText("Build Number", m.BuildNumber, "CFBundleVersion: increase it for every upload.");
            m_Dirty |= UI::InputText("Minimum macOS", m.MinimumVersion, "LSMinimumSystemVersion, e.g. 11.0 (Big Sur, first Apple Silicon release).");
            static const char* cats[] = { "public.app-category.games", "public.app-category.action-games", "public.app-category.adventure-games",
                                          "public.app-category.puzzle-games", "public.app-category.racing-games",
                                          "public.app-category.simulation-games", "public.app-category.strategy-games",
                                          "public.app-category.education", "public.app-category.entertainment" };
            int cat = 0;
            for (int i = 0; i < 9; ++i)
                if (m.Category == cats[i])
                    cat = i;
            if (UI::Combo("Category", cat, cats, 9, "LSApplicationCategoryType (Mac App Store and Finder)."))
            {
                m.Category = cats[cat];
                m_Dirty = true;
            }
            m_Dirty |= UI::InputText("Camera Usage", m.CameraUsage, "NSCameraUsageDescription: shown when the game first uses the camera. Empty = no camera.");
            m_Dirty |= UI::InputText("Microphone Usage", m.MicrophoneUsage, "NSMicrophoneUsageDescription: shown when the game first uses the microphone (voice chat).");
            ImGui::EndTabItem();
        }
        static const char* servers[] = { "Auto (Wayland, else X11)", "Wayland", "X11" };
        if (tab(ICON_FA_TERMINAL, "Linux"))
        {
            status("Linux");
            m_Dirty |= UI::Combo("Display Server", p.Linux.DisplayServer, servers, 3,
                                 "Window system of the built game. The CQ_DISPLAY_SERVER environment variable (wayland / x11) overrides it.");
            ImGui::Spacing();
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextDisabled("Steam Deck: the Linux build runs at 1280x800 borderless with Deck button glyphs and the on-screen keyboard.");
            ImGui::TextDisabled("Automotive Grade Linux: Wayland-only Yocto build (Tools/AGL/meta-conqueror).");
            ImGui::PopTextWrapPos();
            ImGui::EndTabItem();
        }
        if (tab(ICON_FA_SERVER, "FreeBSD"))
        {
            status("FreeBSD");
            m_Dirty |= UI::Combo("Display Server", p.BSD.DisplayServer, servers, 3,
                                 "Window system of the built game. The CQ_DISPLAY_SERVER environment variable (wayland / x11) overrides it.");
            ImGui::EndTabItem();
        }
        if (tab(ICON_FA_MOBILE_SCREEN, "Android"))
        {
            status("Android");
            auto& a = p.Android;
            idField("Package Name", a.PackageName);
            m_Dirty |= UI::DragInt("Version Code", a.VersionCode, 1.0f, 1, 2100000000, "Must grow with every Play Store upload. The version name is the Version above.");
            static const char* orient[] = { "Landscape (auto rotate)", "Landscape", "Portrait (auto rotate)", "Portrait", "Any" };
            m_Dirty |= UI::Combo("Orientation", a.Orientation, orient, 5);
            UI::BeginProperty("Target Architectures", "ARM64: phones, tablets, cars. x86-64: emulator and Chromebooks. At least one is built.");
            bool arm = a.ARM64, x64 = a.X86_64;
            if (ImGui::Checkbox("ARM64", &arm) && (arm || x64)) { a.ARM64 = arm; m_Dirty = true; }
            ImGui::SameLine();
            if (ImGui::Checkbox("x86-64", &x64) && (arm || x64)) { a.X86_64 = x64; m_Dirty = true; }
            UI::EndProperty();
            UI::Text("Minimum API Level", "26 (Android 8.0)");
            UI::Text("Target API Level", "36 (Automotive: 34)");
            UI::SubHeader("Publishing (signing)");
            m_Dirty |= UI::InputText("Keystore", a.Keystore, "Release keystore (.jks / .keystore). Empty: the debug key in ~/.android/debug.keystore.");
            m_Dirty |= UI::InputText("Key Alias", a.KeyAlias, "Key inside the keystore. Empty: androiddebugkey.");
            UI::InfoBox("The keystore password is never saved in the project: set CQ_ANDROID_KEYSTORE_PASS before building with a release key.",
                        UI::InfoType::Info);
            ImGui::EndTabItem();
        }
        if (tab(ICON_FA_TABLET_SCREEN_BUTTON, "iOS"))
        {
            status("iOS");
            auto& i = p.IOS;
            idField("Bundle Identifier", i.BundleIdentifier);
            m_Dirty |= UI::InputText("Build Number", i.BuildNumber, "CFBundleVersion: increase it for every TestFlight / App Store upload.");
            static const char* devices[] = { "iPhone + iPad", "iPhone only", "iPad only" };
            m_Dirty |= UI::Combo("Target Device", i.TargetDevice, devices, 3);
            m_Dirty |= UI::InputText("Minimum iOS", i.MinimumVersion, "Deployment target, e.g. 15.0.");
            static const char* orient[] = { "Landscape", "Portrait", "Any" };
            m_Dirty |= UI::Combo("Orientation", i.Orientation, orient, 3);
            m_Dirty |= UI::InputText("Signing Team ID", i.TeamID, "Apple Developer team (10 characters).");
            m_Dirty |= UI::InputText("Camera Usage", i.CameraUsage, "NSCameraUsageDescription: required for AR and camera features.");
            m_Dirty |= UI::Checkbox("Requires ARKit", i.RequiresARKit, "Only devices with ARKit can install the game (Augmented Reality template).");
            m_Dirty |= UI::Checkbox("Hide Status Bar", i.StatusBarHidden);
            if (i.RequiresARKit && i.CameraUsage.empty())
                UI::InfoBox("ARKit needs a Camera Usage text.", UI::InfoType::Warning);
            ImGui::EndTabItem();
        }
        if (tab(ICON_FA_GLOBE, "Web"))
        {
            status("Web");
            auto& w = p.Web;
            static const char* gapis[] = { "WebGL 2", "WebGPU" };
            m_Dirty |= UI::Combo("Graphics API", w.GraphicsAPI, gapis, 2, "WebGL 2 runs in every current browser; WebGPU needs Chrome / Edge 113+, Safari 26+ or Firefox 141+.");
            static const char* templates[] = { "Default (title and fullscreen button)", "Minimal (canvas only)", "Fullscreen" };
            m_Dirty |= UI::Combo("Page Template", w.Template, templates, 3);
            static const char* comp[] = { "Disabled", "Gzip", "Brotli" };
            m_Dirty |= UI::Combo("Compression", w.Compression, comp, 3, "Brotli is smallest; the web server must send Content-Encoding for it.");
            m_Dirty |= UI::DragInt("Initial Memory", w.MemoryMB, 1.0f, 32, 4096, "Starting WebAssembly heap in MB (it grows when needed).");
            m_Dirty |= UI::Checkbox("Multithreading", w.Threads, "Uses SharedArrayBuffer: the page must be served with COOP / COEP headers.");
            m_Dirty |= UI::Checkbox("Data Caching", w.DataCaching, "Keeps the game data in the browser (IndexedDB) so the second start is fast.");
            ImGui::EndTabItem();
        }
        if (tab(ICON_FA_GAUGE_HIGH, "QNX"))
        {
            status("QNX");
            auto& q = p.QNX;
            static const char* sdps[] = { "SDP 7.1", "SDP 8.0" };
            m_Dirty |= UI::Combo("QNX SDP", q.SDP, sdps, 2);
            static const char* archs[] = { "aarch64le", "x86_64" };
            m_Dirty |= UI::Combo("Architecture", q.Architecture, archs, 2, "Passed to the toolchain as CQ_QNX_ARCH.");
            m_Dirty |= UI::Checkbox("Fill Display", q.FillDisplay, "Off: the window uses the Default Width / Height above (CQ_QNX_WINDOWED=1).");
            m_Dirty |= UI::InputText("Screen App ID", q.ScreenAppID, "QNX Screen window id string (CQ_APP_ID).");
            const std::string cmd = std::string("cmake -S . -B build-qnx -G Ninja -DCMAKE_TOOLCHAIN_FILE=Tools/QNX/qnx.toolchain.cmake -DCQ_QNX_ARCH=") + archs[std::clamp(q.Architecture, 0, 1)];
            ImGui::Spacing();
            ImGui::TextDisabled("Player build (QNX SDP environment loaded):");
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted(cmd.c_str());
            ImGui::PopTextWrapPos();
            if (ImGui::SmallButton(ICON_FA_COPY " Copy command"))
                ImGui::SetClipboardText(cmd.c_str());
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    void ProjectSettingsPanel::DrawQuality()
    {
        auto& q = ProjectSettings::Quality();
        static const char* levels[] = { "Low", "Medium", "High", "Ultra", "Custom" };
        if (UI::Combo("Quality Level", q.Level, levels, 5, "Applies right away in the editor and is used by the built game."))
        {
            if (q.Level == (int)QualityLevel::Custom)
                q.Custom = QualitySettings::GetPreset();
            ProjectSettings::Apply();
            m_Dirty = true;
        }
        // Hazır düzeyde bir değer değişirse Custom'a geçilir (değerler kopyalanır)
        QualityPreset preset = QualitySettings::GetPreset();
        bool changed = false;
        UI::SubHeader("Rendering");
        static const char* msaa[] = { "Disabled", "2x", "4x", "8x" };
        int msaaIndex = preset.AntiAliasing >= 8 ? 3 : preset.AntiAliasing >= 4 ? 2 : preset.AntiAliasing >= 2 ? 1 : 0;
        if (UI::Combo("Anti Aliasing (MSAA)", msaaIndex, msaa, 4))
        {
            preset.AntiAliasing = msaaIndex == 0 ? 1 : 1 << msaaIndex;
            changed = true;
        }
        changed |= UI::Checkbox("Ambient Occlusion", preset.EnableSSAO);
        changed |= UI::Checkbox("Bloom", preset.EnableBloom);
        changed |= UI::DragInt("Anisotropic Filtering", preset.AnisotropicFiltering, 1.0f, 1, 16, "Applies to textures loaded after the change.");
        changed |= UI::DragFloat("LOD Bias", preset.LODBias, 0.01f, 0.1f, 8.0f, "%.2f", "Higher keeps detailed LOD levels farther away.");
        changed |= UI::DragInt("Particle Budget", preset.MaxParticles, 10.0f, 1, 100000, "Upper limit of particles per system.");
        UI::SubHeader("Shadows (upper limits)");
        static const char* res[] = { "512", "1024", "2048", "4096" };
        int resIndex = preset.ShadowResolution >= 4096 ? 3 : preset.ShadowResolution >= 2048 ? 2 : preset.ShadowResolution >= 1024 ? 1 : 0;
        if (UI::Combo("Shadow Resolution", resIndex, res, 4))
        {
            preset.ShadowResolution = 512 << resIndex;
            changed = true;
        }
        changed |= UI::DragInt("Shadow Cascades", preset.ShadowCascades, 0.1f, 1, 4);
        changed |= UI::DragFloat("Shadow Distance", preset.ShadowDistance, 1.0f, 1.0f, 10000.0f, "%.0f m");
        if (changed)
        {
            q.Level = (int)QualityLevel::Custom;
            q.Custom = preset;
            ProjectSettings::Apply();
            m_Dirty = true;
        }
        UI::SubHeader("Frame Rate");
        m_Dirty |= UI::DragInt("Target Frame Rate", q.TargetFrameRate, 1.0f, 0, 1000, "0 = unlimited (limited by VSync).");
    }

    void ProjectSettingsPanel::DrawGraphics()
    {
        auto& g = ProjectSettings::Graphics();
        UI::SubHeader("New Scene Defaults");
        static const char* tonemappers[] = { "None", "Neutral", "ACES", "AgX", "Reinhard" };
        m_Dirty |= UI::Combo("Tonemapper", g.DefaultTonemapper, tonemappers, 5);
        static const char* skies[] = { "Procedural Sky", "Solid Color" };
        m_Dirty |= UI::Combo("Background", g.DefaultSky, skies, 2);
        UI::InfoBox("Per-scene rendering (sky, fog, shadows, post processing) is edited in the Lighting window.", UI::InfoType::Info);
    }

    void ProjectSettingsPanel::DrawAudio()
    {
        auto& a = ProjectSettings::Audio();
        bool changed = false;
        changed |= UI::SliderFloat("Global Volume", a.GlobalVolume, 0.0f, 2.0f);
        changed |= UI::DragFloat("Doppler Factor", a.DopplerFactor, 0.01f, 0.0f, 10.0f, "%.2f", "Multiplies the Doppler effect of every 3D sound.");
        changed |= UI::DragFloat("Volume Rolloff Scale", a.RolloffScale, 0.01f, 0.0f, 10.0f, "%.2f", "Multiplies how fast 3D sounds fade with distance.");
        changed |= UI::Checkbox("Disable Audio", a.DisableAudio, "Mutes the engine (dedicated servers, tests).");
        if (changed)
        {
            ProjectSettings::Apply();
            m_Dirty = true;
        }
    }

    void ProjectSettingsPanel::DrawInput()
    {
        auto names = Input::GetContextNames();
        if (ProjectSettings::CustomInput())
            UI::InfoBox("This project uses its own input map (saved in the project file).", UI::InfoType::Info);
        else
            UI::InfoBox("This project uses the engine's default input map. Any change makes a project copy.", UI::InfoType::Info);
        if (ProjectSettings::CustomInput() && ImGui::Button(ICON_FA_ROTATE_LEFT " Reset To Engine Defaults"))
        {
            Input::ClearContexts();
            Input::LoadConfig(FileSystem::ResolveResourcePath("Engine/src/Core/Input/default.cqinput"));
            ProjectSettings::CustomInput() = false;
            m_Dirty = true;
            return;
        }
        if (m_InputContext.empty() || std::find(names.begin(), names.end(), m_InputContext) == names.end())
            m_InputContext = names.empty() ? std::string() : names.front();

        UI::BeginProperty("Action Map");
        if (ImGui::BeginCombo("##inputctx", m_InputContext.c_str()))
        {
            for (auto& n : names)
                if (ImGui::Selectable(n.c_str(), n == m_InputContext))
                    m_InputContext = n;
            ImGui::EndCombo();
        }
        UI::EndProperty();
        UI::BeginProperty("New Action Map");
        ImGui::SetNextItemWidth(-70.0f);
        ImGui::InputTextWithHint("##newctx", "name", m_NewInputName, sizeof(m_NewInputName));
        ImGui::SameLine();
        if (ImGui::Button("Add##addctx", ImVec2(-1, 0)) && m_NewInputName[0])
        {
            Input::CreateContext(m_NewInputName);
            m_InputContext = m_NewInputName;
            m_NewInputName[0] = 0;
            ProjectSettings::CustomInput() = true;
            m_Dirty = true;
        }
        UI::EndProperty();

        InputContext* ctx = Input::GetContextMutable(m_InputContext);
        if (!ctx)
            return;
        UI::SubHeader("Actions");
        std::vector<std::string> actions;
        for (auto& [n, a] : ctx->Actions)
            actions.push_back(n);
        std::sort(actions.begin(), actions.end());
        bool changed = false;
        std::string removeAction;
        for (const auto& name : actions)
        {
            InputAction& a = ctx->Actions[name];
            ImGui::PushID(name.c_str());
            const bool open = ImGui::TreeNodeEx("##action", ImGuiTreeNodeFlags_SpanAvailWidth, "%s", name.c_str());
            ImGui::SameLine(ImGui::GetContentRegionMax().x - ImGui::GetFrameHeight());
            if (ImGui::SmallButton(ICON_FA_TRASH_CAN))
                removeAction = name;
            if (open)
            {
                // Tuşlar
                for (size_t k = 0; k < a.Keys.size(); ++k)
                {
                    ImGui::PushID((int)k);
                    UI::BeginProperty(k == 0 ? "Keys" : "");
                    ImGui::TextUnformatted(Input::GetKeyName(a.Keys[k]).c_str());
                    ImGui::SameLine();
                    if (ImGui::SmallButton(ICON_FA_XMARK))
                    {
                        a.Keys.erase(a.Keys.begin() + (long)k);
                        changed = true;
                    }
                    UI::EndProperty();
                    ImGui::PopID();
                }
                UI::BeginProperty(a.Keys.empty() ? "Keys" : "");
                const bool listening = m_ListenAction == m_InputContext + "/" + name;
                if (ImGui::Button(listening ? "Press a key... (Esc cancels)" : ICON_FA_PLUS " Add Key", ImVec2(-1, 0)))
                    m_ListenAction = listening ? std::string() : m_InputContext + "/" + name;
                if (listening)
                    for (int key = ImGuiKey_NamedKey_BEGIN; key < ImGuiKey_NamedKey_END; ++key)
                        if (ImGui::IsKeyPressed((ImGuiKey)key, false))
                        {
                            if (key != ImGuiKey_Escape)
                            {
                                int code = GameInput::KeyCodeFromImGuiKey(key);
                                if (code >= 0 && std::find(a.Keys.begin(), a.Keys.end(), (KeyCode)code) == a.Keys.end())
                                {
                                    a.Keys.push_back((KeyCode)code);
                                    changed = true;
                                }
                            }
                            m_ListenAction.clear();
                            break;
                        }
                UI::EndProperty();
                // Gamepad
                static const char* buttons[] = { "A", "B", "X", "Y", "LB", "RB", "Back", "Start", "Guide", "L-Stick", "R-Stick", "D-Up", "D-Right", "D-Down", "D-Left" };
                for (size_t b = 0; b < a.GamepadButtons.size(); ++b)
                {
                    ImGui::PushID(100 + (int)b);
                    int btn = std::clamp(a.GamepadButtons[b], 0, 14);
                    if (UI::Combo(b == 0 ? "Gamepad Buttons" : "", btn, buttons, 15)) { a.GamepadButtons[b] = btn; changed = true; }
                    ImGui::PopID();
                }
                static const char* axes[] = { "Left Stick X", "Left Stick Y", "Right Stick X", "Right Stick Y", "Left Trigger", "Right Trigger" };
                for (size_t x = 0; x < a.GamepadAxes.size(); ++x)
                {
                    ImGui::PushID(200 + (int)x);
                    int axis = std::clamp(a.GamepadAxes[x], 0, 5);
                    if (UI::Combo(x == 0 ? "Gamepad Axes" : "", axis, axes, 6)) { a.GamepadAxes[x] = axis; changed = true; }
                    ImGui::PopID();
                }
                UI::BeginProperty("");
                if (ImGui::Button(ICON_FA_GAMEPAD " + Button"))
                {
                    a.GamepadButtons.push_back(0);
                    changed = true;
                }
                ImGui::SameLine();
                if (ImGui::Button(ICON_FA_GAMEPAD " + Axis"))
                {
                    a.GamepadAxes.push_back(0);
                    a.Type = InputActionType::Axis1D;
                    changed = true;
                }
                UI::EndProperty();
                changed |= UI::DragFloat("Dead Zone", a.DeadZone, 0.01f, 0.0f, 0.95f);
                changed |= UI::DragFloat("Scale", a.Scale, 0.01f, -10.0f, 10.0f, "%.2f", "Multiplies the value (-1 inverts an axis).");
                changed |= UI::DragFloat("Hold Time", a.RequiredHoldTime, 0.01f, 0.0f, 10.0f, "%.2f s", "The action triggers after being held this long.");
                changed |= UI::DragInt("Player Index", a.PlayerIndex, 0.1f, 0, 15, "0 = any player; otherwise only that Player Input reads it.");
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        if (!removeAction.empty())
        {
            ctx->Actions.erase(removeAction);
            changed = true;
        }
        UI::BeginProperty("New Action");
        ImGui::SetNextItemWidth(-70.0f);
        ImGui::InputTextWithHint("##newaction", "e.g. Jump, Fire, Move", m_NewActionName, sizeof(m_NewActionName));
        ImGui::SameLine();
        if (ImGui::Button("Add##addaction", ImVec2(-1, 0)) && m_NewActionName[0] && !ctx->Actions.count(m_NewActionName))
        {
            InputAction a;
            a.Name = m_NewActionName;
            ctx->Actions[a.Name] = a;
            m_NewActionName[0] = 0;
            changed = true;
        }
        UI::EndProperty();
        if (changed)
        {
            ProjectSettings::CustomInput() = true;
            m_Dirty = true;
        }
    }

    void ProjectSettingsPanel::DrawNavigation()
    {
        auto& nav = ProjectSettings::Navigation();
        UI::SubHeader("Areas");
        UI::InfoBox("Agents prefer cheap areas: a path through an area with cost 3 is used only when it saves 3x the distance. "
                    "Not Walkable is never crossed.", UI::InfoType::Info);
        bool changed = false;
        for (int i = 0; i < 32; ++i)
        {
            auto& area = nav.Areas[(size_t)i];
            if (i >= 8 && area.Name.empty() && area.Cost == 1.0f)
                continue;
            ImGui::PushID(i);
            char label[32];
            std::snprintf(label, sizeof(label), "Area %d", i);
            UI::BeginProperty(label);
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.6f);
            if (i <= 2)
                ImGui::TextUnformatted(area.Name.c_str());
            else
            {
                char buf[64] = {};
                std::strncpy(buf, area.Name.c_str(), sizeof(buf) - 1);
                if (ImGui::InputTextWithHint("##name", "unused", buf, sizeof(buf)))
                {
                    area.Name = buf;
                    changed = true;
                }
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-1);
            if (i == NavArea_NotWalkable)
                ImGui::TextDisabled("-");
            else
                changed |= ImGui::DragFloat("##cost", &area.Cost, 0.05f, 0.01f, 1000.0f, "cost %.2f");
            UI::EndProperty();
            ImGui::PopID();
        }
        if (changed)
            m_Dirty = true;
        UI::SubHeader("Bake");
        UI::InfoBox("Agent radius, height, max slope and step height are set on the NavMesh Surface component.", UI::InfoType::Info);
        if (m_Context && ImGui::Button(ICON_FA_MAP " Bake NavMesh", ImVec2(-1, 0)))
            NavAgents::Bake(*m_Context);
        if (m_Context)
            UI::Text("Baked Cells", "%zu", m_Context->GetNavMesh().GetCellCount());
    }

    void ProjectSettingsPanel::DrawEditorSettings()
    {
        auto& e = ProjectSettings::Editor();
        static const char* modes[] = { "3D", "2D" };
        m_Dirty |= UI::Combo("Default Behavior Mode", e.DefaultBehaviorMode, modes, 2,
                             "New scenes start in this mode: 2D uses an orthographic camera and the 2D Scene view.");
        m_Dirty |= UI::Checkbox("New Scene: Camera + Light", e.NewSceneWithCameraAndLight, "File > New Scene adds a Main Camera (and a Directional Light in 3D).");
        m_Dirty |= UI::DragInt("Auto Save (minutes)", e.AutoSaveMinutes, 0.1f, 0, 120, "Saves the open scene when it has unsaved changes (0 = off).");
        m_Dirty |= UI::InputText("External Script Editor", e.ExternalScriptEditor,
                                 "Command that opens scripts, e.g. code, kate, gedit, notepad++ (empty = built-in code editor).");
    }

    void ProjectSettingsPanel::DrawScriptOrder()
    {
        auto& order = ProjectSettings::ScriptOrder();
        UI::InfoBox("Scripts with a lower number run first (Start, Update, LateUpdate, FixedUpdate). Unlisted scripts use 0.", UI::InfoType::Info);
        bool changed = false;
        int remove = -1;
        for (size_t i = 0; i < order.size(); ++i)
        {
            ImGui::PushID((int)i);
            UI::BeginProperty(order[i].ClassName.c_str());
            ImGui::SetNextItemWidth(-ImGui::GetFrameHeight() - 6.0f);
            changed |= ImGui::DragInt("##order", &order[i].Order, 1.0f, -10000, 10000);
            ImGui::SameLine();
            if (ImGui::Button(ICON_FA_TRASH_CAN, ImVec2(ImGui::GetFrameHeight(), 0)))
                remove = (int)i;
            UI::EndProperty();
            ImGui::PopID();
        }
        if (remove >= 0)
        {
            order.erase(order.begin() + remove);
            changed = true;
        }
        UI::BeginProperty("Add Script");
        ImGui::SetNextItemWidth(-70.0f);
        ImGui::InputTextWithHint("##addscript", "class name", m_NewScriptOrder, sizeof(m_NewScriptOrder));
        ImGui::SameLine();
        if (ImGui::Button("Add##addorder", ImVec2(-1, 0)) && m_NewScriptOrder[0])
        {
            const std::string name = m_NewScriptOrder;
            if (std::none_of(order.begin(), order.end(), [&](const ScriptOrderEntry& e) { return e.ClassName == name; }))
            {
                order.push_back({ name, order.empty() ? -100 : order.back().Order + 100 });
                changed = true;
            }
            m_NewScriptOrder[0] = 0;
        }
        UI::EndProperty();
        if (changed)
        {
            std::stable_sort(order.begin(), order.end(), [](const ScriptOrderEntry& a, const ScriptOrderEntry& b) { return a.Order < b.Order; });
            ProjectSettings::InvalidateScriptOrderCache();
            m_Dirty = true;
        }
    }
}
