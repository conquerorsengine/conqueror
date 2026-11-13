#include "ObjectPicker.h"
#include "BuildSettingsPanel.h"
#include "Build/BuildPipeline.h"
#include "UI/EditorUI.h"
#include "Core/Project/Project.h"
#include "Core/Project/ProjectSerializer.h"
#include "Core/Project/PlatformModules.h"
#include "Core/Utils/DynamicLibrary.h"
#include "Renderer/RHI/RendererAPI.h"

#include <IconsFontAwesome6.h>
#include <imgui.h>
#include <nfd.h>

#include <algorithm>
#include <cstdlib>

namespace fs = std::filesystem;

namespace Conqueror::Editor
{
    namespace
    {
        std::string ToProjectRelative(const fs::path& p)
        {
            const fs::path dir = Project::GetActiveProjectDirectory();
            std::error_code ec;
            fs::path rel = fs::relative(p, dir, ec);
            if (ec || rel.empty() || rel.string().rfind("..", 0) == 0)
                return p.generic_string();
            return rel.generic_string();
        }

        bool InputStringField(const char* id, std::string& value, float width = -1.0f)
        {
            char buf[512];
            std::snprintf(buf, sizeof(buf), "%s", value.c_str());
            ImGui::SetNextItemWidth(width);
            if (ImGui::InputText(id, buf, sizeof(buf)))
            {
                value = buf;
                return true;
            }
            return false;
        }

        void OpenFolder(const fs::path& dir)
        {
#if defined(_WIN32)
            std::string cmd = "explorer \"" + dir.string() + "\"";
#elif defined(__APPLE__)
            std::string cmd = "open \"" + dir.string() + "\" &";
#else
            std::string cmd = "xdg-open \"" + dir.string() + "\" >/dev/null 2>&1 &";
#endif
            [[maybe_unused]] int rc = std::system(cmd.c_str());
        }
    }

    fs::path BuildSettingsPanel::EngineBinDir()
    {
        return DynamicLibrary::ExecutableDirectory();
    }

    fs::path BuildSettingsPanel::DefaultOutputDir()
    {
        auto project = Project::GetActive();
        if (!project)
            return {};
        const auto& cfg = project->GetConfig();
        if (!cfg.Build.OutputDirectory.empty())
        {
            fs::path p(cfg.Build.OutputDirectory);
            return p.is_absolute() ? p : project->GetProjectDirectory() / p;
        }
        return project->GetProjectDirectory() / "Builds" / (cfg.TargetPlatform.empty() ? BuildPipeline::HostPlatform() : cfg.TargetPlatform);
    }

    bool BuildSettingsPanel::StartBuild(bool run, std::string* error)
    {
        auto project = Project::GetActive();
        if (!project || project->GetProjectDirectory().empty())
        {
            if (error) *error = "No project is open";
            return false;
        }
        if (BuildPipeline::Get().IsRunning())
        {
            if (error) *error = "A build is already running";
            return false;
        }
        // Derlemeden önce sahne ve proje kaydedilir (Unity de sorar; burada otomatik)
        if (m_SaveProject)
            m_SaveProject();
        BuildOptions o;
        o.ProjectDir = project->GetProjectDirectory();
        o.ProjectFile = Project::GetActiveProjectFile();
        o.Config = project->GetConfig();
        o.OutputDir = DefaultOutputDir();
        o.EngineBinDir = EngineBinDir();
        o.RunAfterBuild = run;
        if (!BuildPipeline::Get().Start(o))
        {
            if (error) *error = "Could not start the build";
            return false;
        }
        m_ShowLog = true;
        return true;
    }

    std::vector<std::string> BuildSettingsPanel::ReadyPlatforms()
    {
        std::vector<std::string> list;
        for (const auto& p : BuildPipeline::Platforms())
            if (BuildPipeline::CanBuild(p.Name))
                list.push_back(p.Name);
        return list;
    }

    bool BuildSettingsPanel::StartBuildAll(std::string* error)
    {
        auto project = Project::GetActive();
        if (!project || project->GetProjectDirectory().empty())
        {
            if (error) *error = "No project is open";
            return false;
        }
        if (BuildPipeline::Get().IsRunning())
        {
            if (error) *error = "A build is already running";
            return false;
        }
        const auto platforms = ReadyPlatforms();
        if (platforms.empty())
        {
            if (error) *error = "No platform module is ready on this computer";
            return false;
        }
        if (m_SaveProject)
            m_SaveProject();
        BuildOptions o;
        o.ProjectDir = project->GetProjectDirectory();
        o.ProjectFile = Project::GetActiveProjectFile();
        o.Config = project->GetConfig();
        // Her platform kendi klasörüne: <çıktı klasörü ya da Builds>/<Platform>
        const auto& cfg = project->GetConfig();
        fs::path base = project->GetProjectDirectory() / "Builds";
        if (!cfg.Build.OutputDirectory.empty())
        {
            fs::path p(cfg.Build.OutputDirectory);
            base = p.is_absolute() ? p : project->GetProjectDirectory() / p;
        }
        o.OutputDir = base;
        o.EngineBinDir = EngineBinDir();
        if (!BuildPipeline::Get().StartAll(o, platforms))
        {
            if (error) *error = "Could not start the builds";
            return false;
        }
        m_ShowLog = true;
        return true;
    }

    void BuildSettingsPanel::SaveIfDirty()
    {
        if (!m_Dirty)
            return;
        m_Dirty = false;
        auto project = Project::GetActive();
        if (!project)
            return;
        // Yalnızca proje dosyası (sahneyi kaydetmeden)
        const fs::path file = Project::GetActiveProjectFile();
        if (!file.empty())
            ProjectSerializer(project).Serialize(file);
    }

    void BuildSettingsPanel::OnImGuiRender(bool* open)
    {
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowSize(ImVec2(std::min(820.0f, vp->WorkSize.x * 0.9f), std::min(900.0f, vp->WorkSize.y * 0.9f)), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x * 0.5f, vp->WorkPos.y + vp->WorkSize.y * 0.5f), ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
        if (!ImGui::Begin(ICON_FA_BOXES_PACKING "  Build Settings###BuildSettings", open))
        {
            ImGui::End();
            SaveIfDirty();
            return;
        }
        if (!Project::GetActive() || Project::GetActiveProjectDirectory().empty())
        {
            UI::InfoBox("Open or create a project to build a game.", UI::InfoType::Info);
            ImGui::End();
            return;
        }
        DrawScenes();
        ImGui::Spacing();
        if (ImGui::BeginTable("##buildlayout", 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable))
        {
            ImGui::TableSetupColumn("Platform", ImGuiTableColumnFlags_WidthFixed, 190.0f);
            ImGui::TableSetupColumn("Settings", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            DrawPlatforms();
            ImGui::TableNextColumn();
            DrawPlayerSettings();
            ImGui::EndTable();
        }
        ImGui::Spacing();
        DrawBuildSection();
        ImGui::End();
        SaveIfDirty();
    }

    void BuildSettingsPanel::DrawScenes()
    {
        auto& build = Project::GetActive()->GetConfig().Build;
        ImGui::TextUnformatted(ICON_FA_LAYER_GROUP "  Scenes In Build");
        ImGui::BeginChild("##scenes", ImVec2(0, 170), ImGuiChildFlags_Borders);
        if (build.Scenes.empty())
            ImGui::TextDisabled("No scenes. The project's start scene is used. Drag scenes here from the Content Browser.");
        int index = 0;
        int removeAt = -1;
        for (int i = 0; i < (int)build.Scenes.size(); ++i)
        {
            auto& s = build.Scenes[(size_t)i];
            ImGui::PushID(i);
            if (ImGui::Checkbox("##en", &s.Enabled))
                MarkDirty();
            ImGui::SameLine();
            const std::string label = ICON_FA_GRIP_VERTICAL "  " + s.Path;
            ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_AllowOverlap, ImVec2(ImGui::GetContentRegionAvail().x - 60.0f, 0));
            // Sürükleyerek sıralama
            if (ImGui::BeginDragDropSource())
            {
                ImGui::SetDragDropPayload("BUILD_SCENE_ROW", &i, sizeof(int));
                ImGui::TextUnformatted(s.Path.c_str());
                ImGui::EndDragDropSource();
            }
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("BUILD_SCENE_ROW"))
                {
                    const int from = *(const int*)p->Data;
                    if (from != i && from >= 0 && from < (int)build.Scenes.size())
                    {
                        auto row = build.Scenes[(size_t)from];
                        build.Scenes.erase(build.Scenes.begin() + from);
                        build.Scenes.insert(build.Scenes.begin() + i, row);
                        MarkDirty();
                    }
                }
                ImGui::EndDragDropTarget();
            }
            ImGui::SameLine(ImGui::GetContentRegionMax().x - 52.0f);
            if (s.Enabled)
                ImGui::TextDisabled("%d", index++);
            else
                ImGui::TextDisabled("-");
            ImGui::SameLine(ImGui::GetContentRegionMax().x - 22.0f);
            if (ImGui::SmallButton(ICON_FA_XMARK))
                removeAt = i;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Remove from build");
            ImGui::PopID();
        }
        if (removeAt >= 0)
        {
            build.Scenes.erase(build.Scenes.begin() + removeAt);
            MarkDirty();
        }
        ImGui::EndChild();
        // İçerik tarayıcısından sahne bırakma
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM"))
            {
                fs::path path(std::string((const char*)p->Data, (size_t)p->DataSize - 1));
                if (path.extension() == ".cqscene")
                {
                    const std::string rel = ToProjectRelative(path);
                    if (std::none_of(build.Scenes.begin(), build.Scenes.end(), [&](const BuildSceneEntry& e) { return e.Path == rel; }))
                    {
                        build.Scenes.push_back({ rel, true });
                        MarkDirty();
                    }
                }
            }
            ImGui::EndDragDropTarget();
        }
        if (ImGui::Button(ICON_FA_PLUS " Add Open Scenes"))
        {
            const fs::path current = m_CurrentScene ? m_CurrentScene() : fs::path{};
            if (current.empty())
                m_Message = "Save the open scene first";
            else
            {
                const std::string rel = ToProjectRelative(current);
                if (std::none_of(build.Scenes.begin(), build.Scenes.end(), [&](const BuildSceneEntry& e) { return e.Path == rel; }))
                {
                    build.Scenes.push_back({ rel, true });
                    MarkDirty();
                }
            }
        }
        ImGui::SameLine();
        ImGui::TextDisabled("Index 0 is the first scene of the game. LoadScene(\"Name\") / LoadScene(1) switches scenes.");
    }

    void BuildSettingsPanel::DrawPlatforms()
    {
        auto& cfg = Project::GetActive()->GetConfig();
        ImGui::TextUnformatted("Platform");
        const std::string host = BuildPipeline::HostPlatform();
        const std::string target = cfg.TargetPlatform.empty() ? host : cfg.TargetPlatform;
        // Project Settings > Player ile aynı liste (konsollar yok); burada derlenemeyenler soluk
        const BuildPipeline::PlatformInfo* current = nullptr;
        for (const auto& p : BuildPipeline::Platforms())
        {
            const bool selected = target == p.Name;
            if (selected)
                current = &p;
            std::string label = std::string(p.Icon) + "  " + p.Name;
            if (p.Name == host)
                label += "  (this PC)";
            const bool buildable = BuildPipeline::CanBuild(p.Name);
            if (!buildable)
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            if (ImGui::Selectable(label.c_str(), selected, 0, ImVec2(0, 26)))
            {
                cfg.TargetPlatform = p.Name;
                MarkDirty();
            }
            if (!buildable)
                ImGui::PopStyleColor();
        }
        if (current)
        {
            // Platform modülü: bu bilgisayarda gereken araç zinciri (Unity Hub "Modules")
            const PlatformModules::Module module = PlatformModules::Detect(current->Name);
            const bool ready = module.State == PlatformModules::Status::Ready;
            std::string text = current->Status;
            text += std::string("\nModule: ") + PlatformModules::StatusName(module.State) + " - " + module.Toolchain;
            if (!module.Detail.empty())
                text += "\n" + module.Detail;
            if (!module.Install.empty())
                text += "\nInstall: " + module.Install;
            UI::InfoBox(text.c_str(), ready ? UI::InfoType::Success : module.State == PlatformModules::Status::Missing ? UI::InfoType::Warning : UI::InfoType::Info);
        }
        ImGui::TextDisabled("Platform specific settings: Project Settings > Player > Platform Settings.");
    }

    void BuildSettingsPanel::DrawPlayerSettings()
    {
        auto& cfg = Project::GetActive()->GetConfig();
        auto& player = cfg.Player;
        UI::SubHeader("Player");
        UI::BeginProperty("Product Name");
        if (InputStringField("##product", cfg.Name)) MarkDirty();
        UI::EndProperty();
        UI::BeginProperty("Company");
        if (InputStringField("##company", cfg.Organization)) MarkDirty();
        UI::EndProperty();
        UI::BeginProperty("Version");
        if (InputStringField("##version", cfg.Version)) MarkDirty();
        UI::EndProperty();

        UI::SubHeader("Window");
        if (UI::DragInt("Width", player.Width, 1.0f, 160, 7680)) MarkDirty();
        if (UI::DragInt("Height", player.Height, 1.0f, 120, 4320)) MarkDirty();
        static const char* modes[] = { "Windowed", "Fullscreen Window", "Exclusive Fullscreen" };
        if (UI::Combo("Fullscreen Mode", player.FullscreenMode, modes, 3)) MarkDirty();
        if (UI::Checkbox("Resizable", player.Resizable)) MarkDirty();
        if (UI::Checkbox("VSync", player.VSync)) MarkDirty();
        if (UI::Checkbox("Run In Background", player.RunInBackground, "Keep running when the window loses focus (needed for multiplayer hosts).")) MarkDirty();

        UI::SubHeader("Rendering");
        {
            static const char* apis[] = { "Auto", "OpenGL", "Vulkan", "Direct3D11", "Direct3D12", "Metal" };
            int current = 0;
            for (int i = 0; i < 6; ++i)
                if (player.GraphicsAPI == apis[i])
                    current = i;
            if (UI::Combo("Graphics API", current, apis, 6,
                          "Used by the built game and by the editor for this project (restart required). Auto = Direct3D 11 on Windows, "
                          "Metal on macOS, OpenGL on Linux. Direct3D 12 needs Windows 10+ (falls back to Direct3D 11). Command line --opengl / --vulkan / --d3d11 / --d3d12 / --metal or CQ_RENDERER overrides it."))
            {
                player.GraphicsAPI = apis[current];
                MarkDirty();
            }
            const char* running = RendererAPI::GetBackendName();
            const bool vulkanBuilt = RendererAPI::IsBackendCompiled(RendererAPI::Backend::Vulkan);
            const bool d3dBuilt = RendererAPI::IsBackendCompiled(RendererAPI::Backend::Direct3D11);
            std::string info = std::string("Editor is running on ") + running + ".";
            if (!vulkanBuilt)
                info += " This build has no Vulkan support (Vulkan SDK was not found at compile time).";
            const bool wantsD3D = player.GraphicsAPI == "Direct3D11" || player.GraphicsAPI == "Direct3D12";
            if (wantsD3D && !d3dBuilt)
                info += " Direct3D is only available in Windows builds; other platforms fall back to OpenGL.";
            UI::InfoBox(info.c_str(), (player.GraphicsAPI == "Vulkan" && !vulkanBuilt) || (wantsD3D && !d3dBuilt)
                                          ? UI::InfoType::Warning : UI::InfoType::Info);
        }

        UI::SubHeader("Branding");
        UI::BeginProperty("Icon", "A .png in the project, shown in the title bar / taskbar.");
        if (InputStringField("##icon", player.Icon, ImGui::GetContentRegionAvail().x - ImGui::GetFrameHeight() - ImGui::GetStyle().ItemSpacing.x))
            MarkDirty();
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* p = ImGui::AcceptDragDropPayload("CONTENT_BROWSER_ITEM"))
            {
                fs::path path(std::string((const char*)p->Data, (size_t)p->DataSize - 1));
                if (path.extension() == ".png")
                {
                    player.Icon = ToProjectRelative(path);
                    MarkDirty();
                }
            }
            ImGui::EndDragDropTarget();
        }
        ImGui::SameLine();
        if (std::string picked; ObjectPicker::Button("iconpick", "Texture", { ".png" }, player.Icon, picked))
        {
            player.Icon = picked.empty() ? std::string() : ToProjectRelative(picked);
            MarkDirty();
        }
        UI::EndProperty();
    }

    void BuildSettingsPanel::DrawBuildSection()
    {
        auto& cfg = Project::GetActive()->GetConfig();
        auto& pipeline = BuildPipeline::Get();
        const bool running = pipeline.IsRunning();

        UI::SubHeader("Output");
        UI::BeginProperty("Folder", "Relative to the project. Empty: Builds/<Platform>.");
        const float browseW = ImGui::CalcTextSize(ICON_FA_FOLDER_OPEN).x + ImGui::GetStyle().FramePadding.x * 2.0f + 4.0f;
        if (InputStringField("##outdir", cfg.Build.OutputDirectory, ImGui::GetContentRegionAvail().x - browseW))
            MarkDirty();
        ImGui::SameLine(0, 4.0f);
        if (ImGui::Button(ICON_FA_FOLDER_OPEN))
        {
            nfdu8char_t* out = nullptr;
            const std::string start = DefaultOutputDir().parent_path().string();
            if (NFD_PickFolderU8(&out, start.c_str()) == NFD_OKAY && out)
            {
                cfg.Build.OutputDirectory = ToProjectRelative(fs::path(out));
                NFD_FreePathU8(out);
                MarkDirty();
            }
        }
        UI::EndProperty();
        UI::Text("Build Location", "%s", DefaultOutputDir().string().c_str());
        if (UI::Checkbox("Development Build", cfg.Build.DevelopmentBuild, "Keeps the log file and console output in the build."))
            MarkDirty();
        // Unity: Script Debugging / Wait For Managed Debugger (yalnız Development Build ile); Unreal: DebugGame yapılandırması
        ImGui::BeginDisabled(!cfg.Build.DevelopmentBuild);
        ImGui::Indent(12.0f);
        if (UI::Checkbox("Script Debugging", cfg.Build.ScriptDebugging,
                         "Compiles the scripts (C++, C, Rust, Zig, C#) without optimization and with full debug info, so a debugger attached to the game can step through them"))
            MarkDirty();
        if (UI::Checkbox("Wait For Debugger", cfg.Build.WaitForDebugger,
                         "The game waits at startup (showing its process id in the log) until a debugger attaches"))
            MarkDirty();
        ImGui::Unindent(12.0f);
        ImGui::EndDisabled();
        if (UI::Checkbox("Leave AssetBundle Assets Out", cfg.Build.ExcludeBundledAssets,
                         "Assets with an AssetBundle name are not copied into the game; they arrive in their bundles (Build > Build AssetBundles, LoadAssetBundle)"))
            MarkDirty();

        ImGui::Spacing();
        const bool canBuild = BuildPipeline::CanBuild(cfg.TargetPlatform);
        ImGui::BeginDisabled(running || !canBuild);
        const float w = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
        if (ImGui::Button(ICON_FA_HAMMER "  Build", ImVec2(w, 34)))
        {
            std::string err;
            if (!StartBuild(false, &err)) m_Message = err;
        }
        ImGui::SameLine();
        if (UI::AccentButton(ICON_FA_PLAY "  Build And Run", ImVec2(w, 34)))
        {
            std::string err;
            if (!StartBuild(true, &err)) m_Message = err;
        }
        ImGui::EndDisabled();
        {
            // Tek tıkla tüm hazır platformlar
            const auto ready = ReadyPlatforms();
            std::string label = std::string(ICON_FA_LAYER_GROUP "  Build All Ready Platforms (") + std::to_string(ready.size()) + ")";
            ImGui::BeginDisabled(running || ready.empty());
            if (ImGui::Button(label.c_str(), ImVec2(-1, 30)))
            {
                std::string err;
                if (!StartBuildAll(&err)) m_Message = err;
            }
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayShort))
            {
                std::string tip = "Builds every platform whose module is ready on this computer, one after another, into Builds/<Platform>:";
                for (const auto& p : ready)
                    tip += "\n  " + p;
                ImGui::SetTooltip("%s", tip.c_str());
            }
        }
        if (!m_Message.empty())
            UI::InfoBox(m_Message.c_str(), UI::InfoType::Warning);

        if (running)
        {
            ImGui::ProgressBar(pipeline.GetProgress(), ImVec2(-90.0f, 0), pipeline.GetStep().c_str());
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(-1, 0)))
                pipeline.Cancel();
        }
        else if (pipeline.HasResult() && !pipeline.GetAllResults().empty())
        {
            const BuildReport total = pipeline.GetResult();
            char head[160];
            std::snprintf(head, sizeof(head), "Build All: %s in %.1f s", total.Success ? "all platforms succeeded" : "some platforms failed", total.Seconds);
            UI::InfoBox(head, total.Success ? UI::InfoType::Success : UI::InfoType::Error);
            for (const auto& [platform, r] : pipeline.GetAllResults())
            {
                ImGui::PushID(platform.c_str());
                ImGui::TextColored(r.Success ? UI::Colors::Success : UI::Colors::Error, r.Success ? ICON_FA_CHECK : ICON_FA_XMARK);
                ImGui::SameLine();
                if (r.Success)
                    ImGui::Text("%s  %.1f s, %.1f MB", platform.c_str(), r.Seconds, (double)r.Bytes / (1024.0 * 1024.0));
                else
                    ImGui::TextWrapped("%s: %s", platform.c_str(), r.Error.c_str());
                if (r.Success && !r.Executable.empty())
                {
                    ImGui::SameLine();
                    if (ImGui::SmallButton(ICON_FA_FOLDER_OPEN))
                        OpenFolder(r.Executable.parent_path());
                }
                ImGui::PopID();
            }
        }
        else if (pipeline.HasResult())
        {
            const BuildReport r = pipeline.GetResult();
            if (r.Success)
            {
                char buf[256];
                std::snprintf(buf, sizeof(buf), "Build succeeded in %.1f s: %zu files, %.1f MB", r.Seconds, r.Files, (double)r.Bytes / (1024.0 * 1024.0));
                UI::InfoBox(buf, UI::InfoType::Success);
                if (ImGui::Button(ICON_FA_FOLDER_OPEN " Open Build Folder"))
                    OpenFolder(r.Executable.parent_path());
                ImGui::SameLine();
                if (ImGui::Button(ICON_FA_PLAY " Run"))
                    BuildPipeline::Launch(r.Executable);
            }
            else
            {
                UI::InfoBox(("Build failed: " + r.Error).c_str(), UI::InfoType::Error);
            }
        }
        if (m_ShowLog || running)
        {
            if (ImGui::CollapsingHeader("Build Log", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::BeginChild("##buildlog", ImVec2(0, 140), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar);
                for (const auto& line : pipeline.GetLog())
                {
                    const bool err = line.rfind("ERROR", 0) == 0;
                    if (err) ImGui::PushStyleColor(ImGuiCol_Text, UI::Colors::Error);
                    ImGui::TextUnformatted(line.c_str());
                    if (err) ImGui::PopStyleColor();
                }
                if (running && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 20.0f)
                    ImGui::SetScrollHereY(1.0f);
                ImGui::EndChild();
            }
        }
    }
}
