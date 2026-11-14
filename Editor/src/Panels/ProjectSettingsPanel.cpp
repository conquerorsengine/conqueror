#include "ProjectSettingsPanel.h"
#include "VersionControl/VersionControl.h"
#include "Scene/Scene.h"
#include "Core/Logging/Log.h"
#include "Core/Project/Project.h"
#include "Core/Project/ProjectSerializer.h"
#include "Core/Project/ProjectSettings.h"
#include "Core/PhysicsSystem/PhysicsSettings.h"
#include "Core/PhysicsSystem/Physics3D/PhysicsBackend3D.h"
#include "Core/Time/TimeManager.h"
#include "Scripting/ScriptRuntime.h"
#include "UI/EditorUI.h"
#include "Build/BuildPipeline.h"
#include "XR/OpenXR.h"

#include <IconsFontAwesome6.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <nfd.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>

namespace Conqueror::Editor
{
    namespace
    {
        struct PageInfo { const char* Name; const char* Icon; };
        const PageInfo kPages[] = {
            { "General", ICON_FA_SLIDERS },
            { "Player", ICON_FA_GAMEPAD },
            { "Quality", ICON_FA_GEM },
            { "Graphics", ICON_FA_IMAGE },
            { "Audio", ICON_FA_VOLUME_HIGH },
            { "Input", ICON_FA_KEYBOARD },
            { "Physics", ICON_FA_CUBES_STACKED },
            { "Physics 2D", ICON_FA_SHAPES },
            { "Navigation", ICON_FA_MAP },
            { "Localization", ICON_FA_LANGUAGE },
            { "Time", ICON_FA_CLOCK },
            { "Tags & Layers", ICON_FA_TAGS },
            { "Editor", ICON_FA_PEN_TO_SQUARE },
            { "Script Execution Order", ICON_FA_LIST_OL },
            { "Scripting", ICON_FA_CODE },
            { "Autoload", ICON_FA_INFINITY },
            { "Version Control", ICON_FA_CODE_BRANCH },
            { "XR Plug-in Management", ICON_FA_VR_CARDBOARD },
        };

        std::filesystem::path ProjectFilePath()
        {
            auto project = Project::GetActive();
            if (!project)
                return {};
            std::filesystem::path dir = project->GetProjectDirectory();
            std::error_code ec;
            for (auto& entry : std::filesystem::directory_iterator(dir, ec))
                if (entry.path().extension() == ".cqproj")
                    return entry.path();
            return dir / (project->GetConfig().Name + ".cqproj");
        }

        bool InputString(const char* label, std::string& value, const char* tooltip = nullptr)
        {
            return UI::InputText(label, value, tooltip);
        }
    }

    bool ProjectSettingsPanel::SaveProjectFile()
    {
        auto project = Project::GetActive();
        if (!project)
            return false;
        std::filesystem::path path = ProjectFilePath();
        ProjectSerializer serializer(project);
        const bool ok = serializer.Serialize(path);
        if (ok)
            CQ_CORE_INFO("Project settings saved: {0}", path.string());
        return ok;
    }

    bool ProjectSettingsPanel::Save()
    {
        const bool ok = SaveProjectFile();
        if (ok)
            m_Dirty = false;
        return ok;
    }

    void ProjectSettingsPanel::ProbeMuJoCo(bool force)
    {
        const std::string& path = PhysicsSettings::Get().MuJoCoLibraryPath;
        if (m_MuJoCoProbed && !force && path == m_ProbedPath)
            return;
        m_MuJoCoProbed = true;
        m_ProbedPath = path;
        std::string found, version;
        m_MuJoCoFound = Physics3D::ProbeMuJoCo(path, &found, &version);
        m_MuJoCoInfo = found;
        m_MuJoCoVersion = version;
    }

    void ProjectSettingsPanel::OnImGuiRender()
    {
        ImGui::Begin("Project Settings");
        auto project = Project::GetActive();
        if (!project)
        {
            UI::InfoBox("No active project. Open or create a project from the launcher.", UI::InfoType::Warning);
            ImGui::End();
            return;
        }

        // Üst çubuk: arama + kaydet
        {
            ImGui::SetNextItemWidth(std::max(120.0f, ImGui::GetContentRegionAvail().x - 110.0f));
            ImGui::InputTextWithHint("##settingsSearch", ICON_FA_MAGNIFYING_GLASS "  Search settings", m_Search, sizeof(m_Search));
            ImGui::SameLine();
            ImGui::BeginDisabled(!m_Dirty);
            if (UI::AccentButton(ICON_FA_FLOPPY_DISK "  Save", ImVec2(100.0f, 0.0f)))
                Save();
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayShort))
                ImGui::SetTooltip(m_Dirty ? "Save to %s" : "No unsaved changes (%s)", ProjectFilePath().filename().string().c_str());
        }
        ImGui::Separator();

        // Sol gezinme
        const float navWidth = std::clamp(ImGui::GetContentRegionAvail().x * 0.28f, 130.0f, 190.0f);
        ImGui::BeginChild("##settingsNav", ImVec2(navWidth, 0), true);
        std::string search = m_Search;
        std::transform(search.begin(), search.end(), search.begin(), ::tolower);
        for (const auto& page : kPages)
        {
            std::string lower = page.Name;
            std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            if (!search.empty() && lower.find(search) == std::string::npos)
                continue;
            std::string label = std::string(page.Icon) + "  " + page.Name;
            if (ImGui::Selectable(label.c_str(), m_Page == page.Name, 0, ImVec2(0, ImGui::GetTextLineHeight() + 8.0f)))
                m_Page = page.Name;
        }
        ImGui::EndChild();
        ImGui::SameLine();

        ImGui::BeginChild("##settingsPage", ImVec2(0, 0), false);
        ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.25f);
        ImGui::TextUnformatted(m_Page.c_str());
        ImGui::PopFont();
        ImGui::Separator();
        ImGui::Spacing();
        if (m_Page == "General") DrawGeneral();
        else if (m_Page == "Physics") DrawPhysics();
        else if (m_Page == "Physics 2D") DrawPhysics2D();
        else if (m_Page == "Time") DrawTime();
        else if (m_Page == "Tags & Layers") DrawTagsAndLayers();
        else if (m_Page == "Scripting") DrawScripting();
        else if (m_Page == "Player") DrawPlayer();
        else if (m_Page == "Quality") DrawQuality();
        else if (m_Page == "Graphics") DrawGraphics();
        else if (m_Page == "Audio") DrawAudio();
        else if (m_Page == "Input") DrawInput();
        else if (m_Page == "Navigation") DrawNavigation();
        else if (m_Page == "Localization") DrawLocalization();
        else if (m_Page == "Autoload") DrawAutoload();
        else if (m_Page == "Version Control") DrawVersionControl();
        else if (m_Page == "XR Plug-in Management") DrawXR();
        else if (m_Page == "Editor") DrawEditorSettings();
        else if (m_Page == "Script Execution Order") DrawScriptOrder();
        ImGui::EndChild();
        ImGui::End();
    }

    void ProjectSettingsPanel::DrawVersionControl()
    {
        // Unity Project Settings > Version Control
        auto& vcs = Project::GetActive()->GetConfig().VersionControl;
        static const char* kModes[] = { "None", "Git", "Perforce" };
        m_Dirty |= UI::Combo("Mode", vcs.Mode, kModes, 3,
                             "Git: file locks through Git LFS (git lfs lock), status from git status. Perforce: Check Out (p4 edit) and Lock (p4 lock)");
        if (vcs.Mode == 2)
        {
            m_Dirty |= InputString("Server", vcs.P4Port);
            m_Dirty |= InputString("User Name", vcs.P4User);
            m_Dirty |= InputString("Workspace", vcs.P4Client);
            m_Dirty |= InputString("p4 Executable", vcs.P4Executable);
            ImGui::TextDisabled("Empty fields use the P4PORT / P4USER / P4CLIENT environment or P4CONFIG.");
        }
        if (vcs.Mode != 0)
        {
            static std::string s_Result;
            static bool s_Ok = false;
            if (ImGui::Button(ICON_FA_PLUG "  Test Connection"))
                s_Ok = VersionControl::TestConnection(&s_Result);
            if (!s_Result.empty())
            {
                ImGui::PushTextWrapPos(0.0f);
                ImGui::TextColored(s_Ok ? ImVec4(0.45f, 0.85f, 0.5f, 1.0f) : ImVec4(1.0f, 0.5f, 0.45f, 1.0f), "%s", s_Result.c_str());
                ImGui::PopTextWrapPos();
            }
            ImGui::TextDisabled("Select an asset: the Inspector header shows its state with Check Out / Lock / Unlock / Revert.");
        }
    }

    void ProjectSettingsPanel::DrawXR()
    {
        // Unity Project Settings > XR Plug-in Management (+ OpenXR); Unreal Project Settings > OpenXR / Start in VR
        auto& xr = Project::GetActive()->GetConfig().XR;
        m_Dirty |= UI::Checkbox("Initialize XR on Startup", xr.InitializeOnStartup,
                                "Open a headset session when Play starts and in built games (CQ_OPENXR=1 forces it). Without a headset "
                                "or runtime the XR Device Simulator keeps working.");
        UI::SubHeader("Plug-in Providers");
        m_Dirty |= UI::Checkbox("OpenXR", xr.OpenXR,
                                "The active OpenXR runtime drives the headset: Monado, SteamVR, Meta Quest Link, Windows Mixed Reality, "
                                "Varjo, Pico. XR_RUNTIME_JSON selects another runtime.");
        m_Dirty |= UI::Checkbox("ARCore (Android)", xr.ARCore,
                                "Android phones and tablets: Google ARCore drives the AR camera (pose, field of view, camera image as the "
                                "background), plane detection and hit tests. The Android build adds the ARCore SDK from Google Maven "
                                "(ARCore Additional Terms of Service) and the camera permission. In the editor the AR Simulation runs.");
        UI::SubHeader("Runtime");
        static OpenXR::Status s_Checked;
        static bool s_HasCheck = false;
        if (ImGui::Button(ICON_FA_PLUG "  Check Runtime"))
        {
            // Oturum aç, durumu oku, kapat (oyun başlamadan gözlük ve bağlama sınanır)
            if (!OpenXR::GetStatus().SessionCreated)
            {
                OpenXR::Start();
                s_Checked = OpenXR::GetStatus();
                OpenXR::Stop();
            }
            else
                s_Checked = OpenXR::GetStatus();
            s_HasCheck = true;
        }
        if (s_HasCheck)
        {
            const bool ok = s_Checked.SessionCreated;
            ImGui::PushTextWrapPos(0.0f);
            if (ok)
                ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.5f, 1.0f), "%s - %s, %u x %u per eye, %s", s_Checked.Runtime.c_str(),
                                   s_Checked.System.c_str(), s_Checked.EyeWidth, s_Checked.EyeHeight, s_Checked.Binding.c_str());
            else
                ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.45f, 1.0f), "%s", s_Checked.Error.empty() ? "No OpenXR runtime" : s_Checked.Error.c_str());
            ImGui::PopTextWrapPos();
        }
        ImGui::TextDisabled("Interaction profiles: Khronos Simple, Oculus Touch, Valve Index, HTC Vive.");
    }

    void ProjectSettingsPanel::DrawGeneral()
    {
        auto& config = Project::GetActive()->GetConfig();
        m_Dirty |= InputString("Product Name", config.Name);
        m_Dirty |= InputString("Company", config.Organization);
        m_Dirty |= InputString("Version", config.Version);
        // Build Settings ile aynı platform listesi (konsollar yok); boş = editörün çalıştığı PC
        {
            const auto& list = BuildPipeline::Platforms();
            const std::string hostLabel = "This PC (" + BuildPipeline::HostPlatform() + ")";
            std::vector<const char*> names{ hostLabel.c_str() };
            int platform = 0;
            for (size_t i = 0; i < list.size(); ++i)
            {
                names.push_back(list[i].Name);
                if (config.TargetPlatform == list[i].Name)
                    platform = (int)i + 1;
            }
            if (UI::Combo("Target Platform", platform, names.data(), (int)names.size(),
                          "Platform the Build button makes the game for. Per-platform settings are under Player > Platform Settings."))
            {
                config.TargetPlatform = platform == 0 ? std::string() : std::string(list[platform - 1].Name);
                m_Dirty = true;
            }
        }
        m_Dirty |= InputString("Start Scene", config.StartScene, "Scene loaded when the built game starts (relative to the project).");
        UI::Text("Project Folder", "%s", Project::GetActive()->GetProjectDirectory().string().c_str());
        UI::Text("Project File", "%s", ProjectFilePath().filename().string().c_str());
    }

    void ProjectSettingsPanel::DrawPhysics()
    {
        auto& s = PhysicsSettings::Get();
        bool changed = false;

        UI::SubHeader("Engine");
        static const char* backends[] = { "Jolt (default)", "MuJoCo" };
        int backend = (int)s.Backend;
        if (UI::Combo("3D Physics Engine", backend, backends, 2,
                      "Jolt: fast game physics (characters, joints, triggers).\nMuJoCo: DeepMind's high-accuracy multi-body dynamics (loaded at runtime)."))
        {
            s.Backend = (PhysicsBackend3D)backend;
            changed = true;
        }
        if (s.Backend == PhysicsBackend3D::MuJoCo)
        {
            ProbeMuJoCo(false);
            if (m_MuJoCoFound)
                UI::InfoBox(("MuJoCo " + m_MuJoCoVersion + " found: " + m_MuJoCoInfo).c_str(), UI::InfoType::Success);
            else
                UI::InfoBox(("MuJoCo not available - Play mode will fall back to Jolt.\n" + m_MuJoCoInfo).c_str(), UI::InfoType::Warning);
            UI::BeginProperty("MuJoCo Library", "Folder or file of libmujoco (empty = search MUJOCO_DIR, /opt/mujoco, ~/.mujoco, next to the editor).");
            ImGui::SetNextItemWidth(-ImGui::GetFrameHeight() * 2.0f - 8.0f);
            char buf[512] = {};
            std::strncpy(buf, s.MuJoCoLibraryPath.c_str(), sizeof(buf) - 1);
            if (ImGui::InputTextWithHint("##mjpath", "auto-detect", buf, sizeof(buf)))
            {
                s.MuJoCoLibraryPath = buf;
                changed = true;
            }
            ImGui::SameLine(0, 4);
            if (ImGui::Button(ICON_FA_FOLDER_OPEN "##mjbrowse", ImVec2(ImGui::GetFrameHeight(), 0)))
            {
                nfdu8char_t* out = nullptr;
                if (NFD_PickFolderU8(&out, nullptr) == NFD_OKAY && out)
                {
                    s.MuJoCoLibraryPath = out;
                    NFD_FreePathU8(out);
                    changed = true;
                }
            }
            ImGui::SameLine(0, 4);
            if (ImGui::Button(ICON_FA_ROTATE "##mjprobe", ImVec2(ImGui::GetFrameHeight(), 0)))
                ProbeMuJoCo(true);
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                ImGui::SetTooltip("Detect again");
            UI::EndProperty();
            static const char* integrators[] = { "Euler", "RK4", "Implicit", "Implicit Fast" };
            int integrator = (int)s.Integrator;
            if (UI::Combo("Integrator", integrator, integrators, 4))
            {
                s.Integrator = (MuJoCoIntegrator)integrator;
                changed = true;
            }
            changed |= UI::DragInt("Solver Iterations", s.MuJoCoIterations, 1.0f, 1, 1000);
            changed |= UI::DragFloat("Internal Step Scale", s.MuJoCoTimestepScale, 0.01f, 0.05f, 1.0f, "%.2f",
                                     "MuJoCo sub-step = Fixed Timestep x scale (smaller = more accurate).");
        }

        UI::SubHeader("Simulation");
        changed |= UI::Vec3("Gravity", s.Gravity, 0.05f, 0.0f);
        float hz = 1.0f / std::max(1e-4f, s.FixedTimestep);
        if (UI::DragFloat("Fixed Timestep", s.FixedTimestep, 0.0005f, 0.001f, 0.25f, "%.4f s", "Physics step length. FixedUpdate runs at this rate."))
            changed = true;
        UI::Text("Physics Rate", "%.1f Hz", hz);
        changed |= UI::DragInt("Max Sub Steps", s.MaxSubSteps, 1.0f, 1, 64, "Max fixed steps per frame (prevents spiral of death on slow frames).");
        changed |= UI::Checkbox("Interpolation", s.Interpolation, "Smooth rendering of rigidbodies between fixed steps.");
        changed |= UI::Checkbox("Auto Sync Transforms", s.AutoSyncTransforms, "Script/inspector position changes teleport physics bodies.");
        changed |= UI::Checkbox("Queries Hit Triggers", s.QueriesHitTriggers);

        UI::SubHeader("Solver (Jolt)");
        changed |= UI::DragInt("Velocity Iterations", s.VelocityIterations, 1.0f, 1, 100);
        changed |= UI::DragInt("Position Iterations", s.PositionIterations, 1.0f, 1, 100);
        changed |= UI::DragInt("Collision Steps", s.CollisionSteps, 1.0f, 1, 8, "Sub-steps per fixed step for fast-moving bodies.");
        changed |= UI::DragFloat("Sleep Threshold", s.SleepThreshold, 0.001f, 0.0f, 10.0f, "%.3f m/s");

        UI::SubHeader("Defaults (colliders without Rigidbody)");
        changed |= UI::SliderFloat("Friction", s.DefaultFriction, 0.0f, 1.0f);
        changed |= UI::SliderFloat("Bounciness", s.DefaultRestitution, 0.0f, 1.0f);

        UI::SubHeader("Layer Collision Matrix");
        DrawCollisionMatrix();

        if (changed)
            m_Dirty = true;
    }

    void ProjectSettingsPanel::DrawCollisionMatrix()
    {
        auto& s = PhysicsSettings::Get();
        // Sahnede adı olan katmanlar (Unity: yalnızca adlandırılmış katmanlar gösterilir)
        std::vector<std::pair<int, std::string>> layers;
        if (m_Context)
            for (auto& l : m_Context->GetLayers())
                if (l.ID >= 0 && l.ID < PhysicsSettings::MaxLayers)
                    layers.emplace_back(l.ID, l.Name);
        if (layers.empty())
            layers.emplace_back(0, "Default");
        std::sort(layers.begin(), layers.end());

        const float cell = ImGui::GetFrameHeight();
        float labelWidth = 0.0f;
        for (auto& [id, name] : layers)
            labelWidth = std::max(labelWidth, ImGui::CalcTextSize(name.c_str()).x);
        labelWidth += 12.0f;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const float headerHeight = labelWidth * 0.75f + 6.0f;
        const size_t n = layers.size();
        // Sütun başlıkları (eğik metin yerine dikey yığılmış kısaltma + ipucu)
        for (size_t c = 0; c < n; ++c)
        {
            const std::string& name = layers[n - 1 - c].second;
            ImVec2 base(origin.x + labelWidth + c * cell + cell * 0.25f, origin.y + headerHeight - 4.0f);
            // 45° döndürülmüş metin
            int vtxStart = dl->VtxBuffer.Size;
            dl->AddText(base, ImGui::GetColorU32(ImGuiCol_Text), name.c_str());
            int vtxEnd = dl->VtxBuffer.Size;
            const float a = -0.785398f, ca = std::cos(a), sa = std::sin(a);
            for (int i = vtxStart; i < vtxEnd; ++i)
            {
                ImVec2& p = dl->VtxBuffer[i].pos;
                ImVec2 d(p.x - base.x, p.y - base.y);
                p = ImVec2(base.x + d.x * ca - d.y * sa, base.y + d.x * sa + d.y * ca);
            }
        }
        ImGui::Dummy(ImVec2(labelWidth + n * cell, headerHeight));
        bool changed = false;
        for (size_t r = 0; r < n; ++r)
        {
            const auto& [rowId, rowName] = layers[r];
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(rowName.c_str());
            // Üçgen matris: satır r, sütunlar n-1 .. r
            for (size_t c = 0; c < n - r; ++c)
            {
                const int colId = layers[n - 1 - c].first;
                ImGui::SameLine(labelWidth + c * cell);
                bool on = s.LayersCollide(rowId, colId);
                ImGui::PushID((int)(r * 64 + c));
                if (ImGui::Checkbox("##m", &on))
                {
                    s.SetLayersCollide(rowId, colId, on);
                    changed = true;
                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                    ImGui::SetTooltip("%s / %s", rowName.c_str(), layers[n - 1 - c].second.c_str());
                ImGui::PopID();
            }
        }
        if (changed)
            m_Dirty = true;
        ImGui::Spacing();
        if (ImGui::SmallButton("Enable All"))
        {
            s.CollisionMatrix.fill(0xFFFFFFFFu);
            m_Dirty = true;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Disable All"))
        {
            s.CollisionMatrix.fill(0u);
            m_Dirty = true;
        }
    }

    void ProjectSettingsPanel::DrawPhysics2D()
    {
        auto& s = PhysicsSettings::Get();
        bool changed = UI::Vec2("Gravity", s.Gravity2D, 0.05f);
        UI::InfoBox("2D physics uses Box2D with the same fixed timestep as 3D physics.", UI::InfoType::Info);
        if (changed)
            m_Dirty = true;
    }

    void ProjectSettingsPanel::DrawTime()
    {
        auto& s = PhysicsSettings::Get();
        bool changed = UI::DragFloat("Fixed Timestep", s.FixedTimestep, 0.0005f, 0.001f, 0.25f, "%.4f s");
        float scale = TimeManager::GetTimeScale();
        if (UI::DragFloat("Time Scale", scale, 0.01f, 0.0f, 100.0f))
            TimeManager::SetTimeScale(scale);
        float maxDelta = TimeManager::GetMaxDeltaTime();
        if (UI::DragFloat("Maximum Delta Time", maxDelta, 0.001f, 0.01f, 1.0f, "%.3f s", "Longest frame time passed to Update (hitch guard)."))
            TimeManager::SetMaxDeltaTime(maxDelta);
        if (changed)
            m_Dirty = true;
    }

    void ProjectSettingsPanel::DrawTagsAndLayers()
    {
        UI::SubHeader("Tags (project)");
        {
            const auto& tags = Scene::GetAvailableTags();
            auto& custom = ProjectSettings::CustomTags();
            std::string remove;
            for (size_t i = 0; i < tags.size(); ++i)
            {
                ImGui::PushID((int)i);
                char label[32];
                std::snprintf(label, sizeof(label), "Tag %zu", i);
                UI::BeginProperty(label);
                ImGui::TextUnformatted(tags[i].c_str());
                if (i >= Scene::GetBuiltinTagCount())
                {
                    ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - ImGui::GetFrameHeight());
                    if (ImGui::SmallButton(ICON_FA_TRASH_CAN))
                        remove = tags[i];
                }
                UI::EndProperty();
                ImGui::PopID();
            }
            if (!remove.empty())
            {
                custom.erase(std::remove(custom.begin(), custom.end(), remove), custom.end());
                Scene::SetCustomTags(custom);
                m_Dirty = true;
            }
            UI::BeginProperty("New Tag");
            ImGui::SetNextItemWidth(-70.0f);
            ImGui::InputTextWithHint("##newtag", "name", m_NewTag, sizeof(m_NewTag));
            ImGui::SameLine();
            if (ImGui::Button("Add##addtag", ImVec2(-1, 0)) && m_NewTag[0])
            {
                if (std::find(tags.begin(), tags.end(), std::string(m_NewTag)) == tags.end())
                {
                    custom.push_back(m_NewTag);
                    Scene::SetCustomTags(custom);
                    m_Dirty = true;
                }
                m_NewTag[0] = 0;
            }
            UI::EndProperty();
        }
        if (!m_Context)
        {
            UI::InfoBox("Open a scene to edit its layers.", UI::InfoType::Info);
            return;
        }
        UI::SubHeader("Layers (scene)");
        for (auto& layer : std::vector<Scene::LayerInfo>(m_Context->GetLayers()))
        {
            ImGui::PushID(layer.ID);
            std::string name = layer.Name;
            char label[32];
            std::snprintf(label, sizeof(label), "Layer %d", layer.ID);
            if (layer.ID == 0)
            {
                UI::Text(label, "%s", name.c_str());
            }
            else if (UI::InputText(label, name))
                m_Context->RenameLayer(layer.ID, name);
            ImGui::PopID();
        }
        UI::BeginProperty("New Layer");
        ImGui::SetNextItemWidth(-70.0f);
        ImGui::InputTextWithHint("##newlayer", "name", m_NewLayer, sizeof(m_NewLayer));
        ImGui::SameLine();
        if (ImGui::Button("Add", ImVec2(-1, 0)) && m_NewLayer[0])
        {
            m_Context->AddLayer(m_NewLayer);
            m_NewLayer[0] = 0;
        }
        UI::EndProperty();
    }

    void ProjectSettingsPanel::DrawScripting()
    {
        UI::SubHeader("Script Runtimes");
        for (auto* runtime : Scripting::ScriptRuntimes::All())
        {
            bool ok = runtime->IsAvailable();
            UI::BeginProperty(runtime->GetName());
            UI::StatusDot(ok ? UI::Colors::Success : UI::Colors::Warning);
            ImGui::SameLine();
            if (ok)
                ImGui::TextUnformatted("Available");
            else
                ImGui::TextColored(UI::Colors::Warning, "%s", runtime->GetUnavailableReason().c_str());
            UI::EndProperty();
        }
    }
}
