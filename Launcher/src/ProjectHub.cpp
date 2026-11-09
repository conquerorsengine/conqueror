#include "ProjectHub.h"
#include "HubTemplates.h"
#include "Core/Project/PlatformModules.h"
#include "HubLearn.h"
#include "Core/Logging/Log.h"
#include "Core/Base/Version.h"
#include "Core/Automation/Automation.h"
#include "Addons/AddonManager.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <IconsFontAwesome6.h>
#include <nfd.h>
#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <functional>
#include <cmath>

namespace Conqueror::Editor
{
    namespace
    {
        const char* const kDefaultProjectName = "My Game";
        namespace fs = std::filesystem;
        using Launcher::GetTemplates;
        using Launcher::HubTemplate;

        const ImVec4 kAccent(0.26f, 0.59f, 0.98f, 1.0f);
        const ImVec4 kAccentDim(0.2f, 0.45f, 0.78f, 1.0f);
        const ImVec4 kTextDim(0.62f, 0.64f, 0.68f, 1.0f);
        const ImU32 kCardBg = IM_COL32(32, 34, 39, 255);
        const ImU32 kCardHover = IM_COL32(40, 43, 50, 255);
        const ImU32 kSidebarBg = IM_COL32(22, 23, 27, 255);

        std::string Lower(std::string s)
        {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
            return s;
        }

        ImU32 Col(const glm::vec4& c) { return ImGui::ColorConvertFloat4ToU32(ImVec4(c.r, c.g, c.b, c.a)); }

        bool AccentButton(const char* label, const ImVec2& size)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, kAccentDim);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kAccent);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.16f, 0.4f, 0.72f, 1.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
            bool pressed = ImGui::Button(label, size);
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(3);
            return pressed;
        }

        void RevealInFileManager(const fs::path& path)
        {
#ifdef CQ_PLATFORM_WINDOWS
            std::string cmd = "explorer \"" + path.string() + "\"";
#elif defined(CQ_PLATFORM_LINUX)
            std::string cmd = "xdg-open \"" + path.string() + "\" >/dev/null 2>&1 &";
#else
            std::string cmd = "open \"" + path.string() + "\"";
#endif
            int rc = std::system(cmd.c_str());
            (void)rc;
        }

        // .cqproj içinden ad ve başlangıç sahnesi
        bool ReadProjectFile(const fs::path& dir, std::string& name, std::string& startScene)
        {
            std::error_code ec;
            if (!fs::is_directory(dir, ec))
                return false;
            for (auto& entry : fs::directory_iterator(dir, ec))
            {
                if (entry.path().extension() != ".cqproj")
                    continue;
                try
                {
                    YAML::Node n = YAML::LoadFile(entry.path().string());
                    if (n["Project"] && n["Project"]["Name"]) name = n["Project"]["Name"].as<std::string>();
                    if (n["Project"] && n["Project"]["StartScene"]) startScene = n["Project"]["StartScene"].as<std::string>();
                }
                catch (...)
                {
                    name = entry.path().stem().string();
                }
                return true;
            }
            return false;
        }

        // Kart görseli: şablonun ekran görüntüsü (Unreal'deki gibi; kutuyu kırparak doldurur) + köşede şablon renginde ikon
        // rozeti. Resim yoksa şablon degradesi + büyük ikon.
        // Simge, görünen glif sınırlarına göre tam ortalanır (metin kutusu ölçüsü değil: Font Awesome glifleri satır
        // yüksekliği ve ilerleme genişliği içinde ortada durmaz)
        void DrawCenteredIcon(ImDrawList* dl, ImFont* font, float size, ImVec2 c, const std::string& icon, ImU32 col)
        {
            unsigned int cp = 0;
            ImTextCharFromUtf8(&cp, icon.c_str(), nullptr);
            ImFontBaked* baked = font->GetFontBaked(size);
            if (const ImFontGlyph* g = baked ? baked->FindGlyphNoFallback((ImWchar)cp) : nullptr)
            {
                const float k = size / baked->Size;
                dl->AddText(font, size, ImVec2(std::round(c.x - (g->X0 + g->X1) * 0.5f * k), std::round(c.y - (g->Y0 + g->Y1) * 0.5f * k)),
                            col, icon.c_str());
                return;
            }
            const ImVec2 ts = font->CalcTextSizeA(size, FLT_MAX, 0.0f, icon.c_str());
            dl->AddText(font, size, ImVec2(c.x - ts.x * 0.5f, c.y - ts.y * 0.5f), col, icon.c_str());
        }

        void DrawTemplateArt(ImDrawList* dl, ImVec2 a, ImVec2 b, const HubTemplate& t, float iconScale, Texture2D* thumb)
        {
            ImFont* font = ImGui::GetFont();
            if (thumb && thumb->GetWidth() > 0 && thumb->GetHeight() > 0)
            {
                const float w = b.x - a.x, h = b.y - a.y;
                const float boxAspect = w / std::max(1.0f, h);
                const float texAspect = (float)thumb->GetWidth() / (float)thumb->GetHeight();
                float u0 = 0.0f, u1 = 1.0f, v0 = 0.0f, v1 = 1.0f;     // görüntü uzayı (v = 0 üst)
                if (texAspect > boxAspect)
                {
                    const float f = boxAspect / texAspect;
                    u0 = (1.0f - f) * 0.5f;
                    u1 = 1.0f - u0;
                }
                else
                {
                    const float f = texAspect / boxAspect;
                    v0 = (1.0f - f) * 0.5f;
                    v1 = 1.0f - v0;
                }
                // Doku yüklenirken dikey çevrilir (proje küçük resimleriyle aynı): v ters
                dl->AddImageRounded((ImTextureID)(uint64_t)thumb->GetRendererID(), a, b, ImVec2(u0, 1.0f - v0), ImVec2(u1, 1.0f - v1),
                                    IM_COL32_WHITE, 6.0f);
                const float r = std::min(h * 0.15f, 22.0f);
                const ImVec2 c(a.x + r + 8.0f, b.y - r - 8.0f);
                ImVec4 col = ImVec4(t.ColorB.r, t.ColorB.g, t.ColorB.b, 0.92f);
                dl->AddCircleFilled(c, r, ImGui::ColorConvertFloat4ToU32(col));
                dl->AddCircle(c, r, Col(t.ColorA), 0, 1.5f);
                DrawCenteredIcon(dl, font, r * 1.05f, c, t.Icon, IM_COL32(255, 255, 255, 235));
                return;
            }
            dl->AddRectFilledMultiColor(a, b, Col(t.ColorA), Col(t.ColorA), Col(t.ColorB), Col(t.ColorB));
            DrawCenteredIcon(dl, font, ImGui::GetFontSize() * iconScale, ImVec2((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f), t.Icon,
                             IM_COL32(255, 255, 255, 215));
        }
    }

    // ------------------------------------------------------------------ Kurulum

    fs::path ProjectHub::ConfigDirectory()
    {
        if (const char* overrideDir = std::getenv("CQ_HUB_CONFIG_DIR"); overrideDir && overrideDir[0])
            return fs::path(overrideDir);
#ifdef CQ_PLATFORM_WINDOWS
        if (const char* appdata = std::getenv("APPDATA"))
            return fs::path(appdata) / "ConquerorEngine";
#elif defined(__APPLE__)
        if (const char* home = std::getenv("HOME"))
            return fs::path(home) / "Library" / "Application Support" / "ConquerorEngine";
#else
        if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && xdg[0])
            return fs::path(xdg) / "ConquerorEngine";
        if (const char* home = std::getenv("HOME"))
            return fs::path(home) / ".config" / "ConquerorEngine";
#endif
        return fs::current_path();
    }

    std::string ProjectHub::NowString()
    {
        std::time_t t = std::time(nullptr);
        char buf[32];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", std::localtime(&t));
        return buf;
    }

    std::string ProjectHub::RelativeTime(const std::string& stamp)
    {
        std::tm tm{};
        if (stamp.size() < 16 || std::sscanf(stamp.c_str(), "%d-%d-%d %d:%d", &tm.tm_year, &tm.tm_mon, &tm.tm_mday, &tm.tm_hour, &tm.tm_min) != 5)
            return stamp.empty() ? "Never opened" : stamp;
        tm.tm_year -= 1900;
        tm.tm_mon -= 1;
        tm.tm_isdst = -1;
        std::time_t then = std::mktime(&tm);
        double seconds = std::difftime(std::time(nullptr), then);
        if (seconds < 90) return "Just now";
        if (seconds < 3600) return std::to_string((int)(seconds / 60)) + " minutes ago";
        if (seconds < 86400) return std::to_string((int)(seconds / 3600)) + " hours ago";
        if (seconds < 86400 * 30) return std::to_string((int)(seconds / 86400)) + " days ago";
        return stamp.substr(0, 10);
    }

    ProjectHub::ProjectHub()
    {
        std::error_code ec;
        fs::create_directories(ConfigDirectory(), ec);
        m_ConfigFile = ConfigDirectory() / "hub.yaml";
        if (const char* home = std::getenv("HOME"))
            m_DefaultLocation = (fs::path(home) / "ConquerorProjects").string();
        else if (const char* profile = std::getenv("USERPROFILE"))
            m_DefaultLocation = (fs::path(profile) / "ConquerorProjects").string();
        else
            m_DefaultLocation = (fs::current_path() / "Projects").string();
        LoadConfig();
        m_NewLocation = m_DefaultLocation;
        RefreshProjectStates();

        fs::path logoPath = fs::current_path() / "Resources" / "Logo" / "Conqueror_Logo.png";
        if (fs::exists(logoPath, ec))
            m_LogoTexture = Texture2D::Create(logoPath.string());
        RegisterAutomation();
        CQ_CORE_INFO("[Hub] {0} projects, config {1}", m_Projects.size(), m_ConfigFile.string());
    }

    Texture2D* ProjectHub::TemplateThumbnail(const std::string& id)
    {
        auto it = m_TemplateThumbs.find(id);
        if (it == m_TemplateThumbs.end())
        {
            // Bir kez denenir (yoksa boş girdi kalır, her karede diske bakılmaz)
            std::shared_ptr<Texture2D> tex;
            std::error_code ec;
            for (const char* ext : { ".jpg", ".png" })
            {
                const fs::path path = fs::path("Resources") / "Templates" / "Thumbnails" / (id + ext);
                if (fs::exists(path, ec))
                {
                    tex = Texture2D::Create(path.string());
                    break;
                }
            }
            it = m_TemplateThumbs.emplace(id, tex).first;
        }
        return it->second.get();
    }

    ProjectHub::~ProjectHub()
    {
        for (const char* cmd : { "hub" })
            Automation::UnregisterCommand(cmd);
    }

    void ProjectHub::LoadConfig()
    {
        m_Projects.clear();
        std::error_code ec;
        fs::path file = m_ConfigFile;
        // Eski sürüm: çalışma klasöründeki recent_projects.yaml (bir kez taşınır)
        const bool legacy = !fs::exists(file, ec) && fs::exists(fs::current_path() / "recent_projects.yaml", ec);
        if (legacy)
            file = fs::current_path() / "recent_projects.yaml";
        if (!fs::exists(file, ec))
            return;
        try
        {
            YAML::Node n = YAML::LoadFile(file.string());
            if (auto s = n["Settings"])
            {
                if (s["DefaultLocation"]) m_DefaultLocation = s["DefaultLocation"].as<std::string>();
                if (s["UIScale"]) m_UIScale = std::clamp(s["UIScale"].as<float>(), 0.75f, 2.0f);
                if (s["ShowMissing"]) m_ShowMissing = s["ShowMissing"].as<bool>();
                if (s["GridView"]) m_GridView = s["GridView"].as<bool>();
                if (s["SortMode"]) m_SortMode = std::clamp(s["SortMode"].as<int>(), 0, 2);
            }
            auto list = n["Projects"] ? n["Projects"] : n["RecentProjects"];
            if (list && list.IsSequence())
                for (auto p : list)
                {
                    if (!p["Path"])
                        continue;
                    ProjectInfo info;
                    info.Path = p["Path"].as<std::string>();
                    info.Name = p["Name"] ? p["Name"].as<std::string>() : info.Path.filename().string();
                    info.LastOpened = p["LastOpened"] ? p["LastOpened"].as<std::string>() : "";
                    if (info.LastOpened == "Recently" || info.LastOpened == "Unknown")
                        info.LastOpened.clear();
                    info.Favorite = p["Favorite"] && p["Favorite"].as<bool>();
                    m_Projects.push_back(info);
                }
        }
        catch (const std::exception& e)
        {
            CQ_CORE_ERROR("[Hub] Cannot read {0}: {1}", file.string(), e.what());
        }
        if (legacy)
            SaveConfig();
    }

    void ProjectHub::SaveConfig()
    {
        YAML::Emitter out;
        out << YAML::BeginMap;
        out << YAML::Key << "Settings" << YAML::Value << YAML::BeginMap;
        out << YAML::Key << "DefaultLocation" << YAML::Value << m_DefaultLocation;
        out << YAML::Key << "UIScale" << YAML::Value << m_UIScale;
        out << YAML::Key << "ShowMissing" << YAML::Value << m_ShowMissing;
        out << YAML::Key << "GridView" << YAML::Value << m_GridView;
        out << YAML::Key << "SortMode" << YAML::Value << m_SortMode;
        out << YAML::EndMap;
        out << YAML::Key << "Projects" << YAML::Value << YAML::BeginSeq;
        for (const auto& p : m_Projects)
        {
            out << YAML::BeginMap;
            out << YAML::Key << "Name" << YAML::Value << p.Name;
            out << YAML::Key << "Path" << YAML::Value << p.Path.string();
            out << YAML::Key << "LastOpened" << YAML::Value << p.LastOpened;
            out << YAML::Key << "Favorite" << YAML::Value << p.Favorite;
            out << YAML::EndMap;
        }
        out << YAML::EndSeq << YAML::EndMap;
        std::ofstream f(m_ConfigFile);
        f << out.c_str() << "\n";
    }

    void ProjectHub::RefreshProjectStates()
    {
        for (auto& p : m_Projects)
        {
            std::string name, scene;
            p.Missing = !ReadProjectFile(p.Path, name, scene);
            if (!name.empty())
                p.Name = name;
            p.StartScene = scene;
        }
    }

    void ProjectHub::AddToRecentProjects(const ProjectInfo& project)
    {
        ProjectInfo info = project;
        info.LastOpened = NowString();
        auto it = std::find_if(m_Projects.begin(), m_Projects.end(), [&](const ProjectInfo& p) { return p.Path == info.Path; });
        if (it != m_Projects.end())
        {
            info.Favorite = it->Favorite;
            m_Projects.erase(it);
        }
        info.Missing = false;
        m_Projects.insert(m_Projects.begin(), info);
        if (m_Projects.size() > 64)
            m_Projects.resize(64);
        SaveConfig();
    }

    std::vector<int> ProjectHub::VisibleProjects() const
    {
        std::vector<int> out;
        const std::string q = Lower(m_Search);
        for (int i = 0; i < (int)m_Projects.size(); ++i)
        {
            const auto& p = m_Projects[(size_t)i];
            if (!m_ShowMissing && p.Missing)
                continue;
            if (!q.empty() && Lower(p.Name).find(q) == std::string::npos && Lower(p.Path.string()).find(q) == std::string::npos)
                continue;
            out.push_back(i);
        }
        std::stable_sort(out.begin(), out.end(), [&](int a, int b) {
            const auto& pa = m_Projects[(size_t)a];
            const auto& pb = m_Projects[(size_t)b];
            if (pa.Favorite != pb.Favorite)
                return pa.Favorite;   // yıldızlılar önde
            if (m_SortMode == 1) return Lower(pa.Name) < Lower(pb.Name);
            if (m_SortMode == 2) return pa.Path.string() < pb.Path.string();
            return pa.LastOpened > pb.LastOpened;
        });
        return out;
    }

    // ------------------------------------------------------------------ Eylemler

    void ProjectHub::OpenProject(const ProjectInfo& project)
    {
        std::error_code ec;
        if (!fs::exists(project.Path, ec))
        {
            m_Status = "Project folder not found: " + project.Path.string();
            m_StatusIsError = true;
            return;
        }
        m_SelectedProject = project;
        m_ProjectSelected = true;
        AddToRecentProjects(project);
        if (m_ProjectOpenCallback)
            m_ProjectOpenCallback(project);

        std::string enginePath = (fs::current_path() / "ConquerorEngine").string();
#ifdef CQ_PLATFORM_WINDOWS
        enginePath += ".exe";
#endif
        if (fs::exists(enginePath, ec))
            m_LaunchCommand = "\"" + enginePath + "\" \"" + project.Path.string() + "\"";
        else
            CQ_CORE_ERROR("[Hub] ConquerorEngine not found at {0}", enginePath);
        m_ShouldClose = true;
    }

    void ProjectHub::AddExistingProject()
    {
        nfdu8char_t* outPath = nullptr;
        if (NFD_PickFolderU8(&outPath, m_DefaultLocation.c_str()) != NFD_OKAY || !outPath)
            return;
        fs::path dir = outPath;
        NFD_FreePathU8(outPath);
        std::string name, scene;
        if (!ReadProjectFile(dir, name, scene))
        {
            m_Status = "No .cqproj file in " + dir.string();
            m_StatusIsError = true;
            return;
        }
        ProjectInfo info(name.empty() ? dir.filename().string() : name, dir, "");
        auto it = std::find_if(m_Projects.begin(), m_Projects.end(), [&](const ProjectInfo& p) { return p.Path == dir; });
        if (it == m_Projects.end())
            m_Projects.insert(m_Projects.begin(), info);
        SaveConfig();
        m_Status = "Added " + info.Name;
        m_StatusIsError = false;
    }

    bool ProjectHub::CreateFromTemplate(const std::string& templateId, const std::string& name, const fs::path& location, bool open)
    {
        Launcher::NewProjectOptions o;
        o.Name = name;
        o.Location = location;
        o.TemplateId = templateId;
        o.PhysicsBackend = m_PhysicsBackend;
        o.InstallAddons = m_InstallAddons;
        fs::path dir;
        std::string error;
        if (!Launcher::CreateProject(o, dir, error))
        {
            m_Status = error;
            m_StatusIsError = true;
            return false;
        }
        ProjectInfo info(name, dir, "");
        m_Status = "Created " + name;
        m_StatusIsError = false;
        if (open)
            OpenProject(info);
        else
        {
            AddToRecentProjects(info);
            RefreshProjectStates();
        }
        return true;
    }

    // ------------------------------------------------------------------ Çizim

    void ProjectHub::OnImGuiRender()
    {
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->Pos);
        ImGui::SetNextWindowSize(viewport->Size);
        ImGui::SetNextWindowViewport(viewport->ID);
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoScrollbar;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::GetStyle().FontScaleMain = m_UIScale;
        ImGui::Begin("Conqueror Hub", nullptr, flags);
        ImGui::PopStyleVar(2);

        const float sidebarW = 230.0f * m_UIScale;
        DrawSidebar(sidebarW);
        ImGui::SameLine(0.0f, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(28.0f, 22.0f));
        ImGui::BeginChild("##hubpage", ImVec2(0, 0), ImGuiChildFlags_AlwaysUseWindowPadding);
        switch (m_Page)
        {
            case Page::Projects: DrawProjectsPage(); break;
            case Page::NewProject: DrawNewProjectPage(); break;
            case Page::Learn: DrawLearnPage(); break;
            case Page::Addons: DrawAddonsPage(); break;
            case Page::Settings: DrawSettingsPage(); break;
        }
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::End();
    }

    void ProjectHub::DrawSidebar(float width)
    {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::ColorConvertU32ToFloat4(kSidebarBg));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f, 18.0f));
        // Yan çubuk kaymaz (Unity Hub gibi): içerik sabit, sürüm bilgisi pencerenin altına sığacak şekilde yerleşir
        ImGui::BeginChild("##sidebar", ImVec2(width, 0), ImGuiChildFlags_AlwaysUseWindowPadding,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        if (m_LogoTexture)
        {
            float w = width - 28.0f;
            float h = w * (float)m_LogoTexture->GetHeight() / std::max(1.0f, (float)m_LogoTexture->GetWidth());
            ImGui::Image((ImTextureID)(uint64_t)m_LogoTexture->GetRendererID(), ImVec2(w, h), ImVec2(0, 1), ImVec2(1, 0));
        }
        else
        {
            ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.4f);
            ImGui::TextUnformatted("Conqueror Hub");
            ImGui::PopFont();
        }
        ImGui::Spacing();
        ImGui::Spacing();
        struct Item { const char* Icon; const char* Label; Page P; };
        const Item items[] = {
            { ICON_FA_FOLDER_OPEN, "Projects", Page::Projects }, { ICON_FA_SQUARE_PLUS, "New Project", Page::NewProject },
            { ICON_FA_GRADUATION_CAP, "Learn", Page::Learn }, { ICON_FA_PUZZLE_PIECE, "Addons", Page::Addons },
            { ICON_FA_GEAR, "Settings", Page::Settings },
        };
        for (const auto& it : items)
        {
            const bool active = m_Page == it.P;
            ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.2f, 0.36f, 0.6f, 0.55f));
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.25f, 0.27f, 0.32f, 1.0f));
            std::string label = std::string("  ") + it.Icon + "   " + it.Label;
            if (ImGui::Selectable(label.c_str(), active, 0, ImVec2(0, 34.0f * m_UIScale)))
                m_Page = it.P;
            ImGui::PopStyleColor(2);
        }
        // Alt: sürüm (ayraç + iki satır, alt dolgunun içinde; pencere çok kısaysa menünün altına)
        const ImGuiStyle& style = ImGui::GetStyle();
        const float footerH = style.ItemSpacing.y + 1.0f + ImGui::GetTextLineHeightWithSpacing() + ImGui::GetTextLineHeight();
        ImGui::SetCursorPosY(std::max(ImGui::GetCursorPosY(), ImGui::GetWindowHeight() - style.WindowPadding.y - footerH));
        ImGui::Separator();
        ImGui::TextDisabled("Conqueror's Engine");
        ImGui::TextDisabled("Version %s", EngineVersion);
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
    }

    void ProjectHub::DrawHeader(const char* title, const char* subtitle)
    {
        ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.7f);
        ImGui::TextUnformatted(title);
        ImGui::PopFont();
        if (subtitle && subtitle[0])
        {
            ImGui::PushStyleColor(ImGuiCol_Text, kTextDim);
            ImGui::TextUnformatted(subtitle);
            ImGui::PopStyleColor();
        }
        ImGui::Spacing();
    }

    // ---- Projeler

    void ProjectHub::DrawProjectsPage()
    {
        DrawHeader("Projects", "Open a project, or create a new one from a template.");
        // Araç çubuğu: ara, sırala, görünüm, ekle, yeni
        const float rowH = 34.0f * m_UIScale;
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(10.0f, 8.0f));
        char buf[128];
        std::snprintf(buf, sizeof(buf), "%s", m_Search.c_str());
        ImGui::SetNextItemWidth(std::max(160.0f, ImGui::GetContentRegionAvail().x - 600.0f * m_UIScale));
        if (ImGui::InputTextWithHint("##hubsearch", ICON_FA_MAGNIFYING_GLASS "  Search projects", buf, sizeof(buf)))
            m_Search = buf;
        ImGui::SameLine();
        const char* sorts[] = { "Last opened", "Name", "Location" };
        ImGui::SetNextItemWidth(150.0f * m_UIScale);
        if (ImGui::Combo("##sort", &m_SortMode, sorts, 3))
            SaveConfig();
        ImGui::SameLine();
        if (ImGui::Button(m_GridView ? ICON_FA_LIST : ICON_FA_TABLE_CELLS_LARGE, ImVec2(rowH, 0)))
        {
            m_GridView = !m_GridView;
            SaveConfig();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(m_GridView ? "List view" : "Grid view");
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_FOLDER_PLUS "  Add", ImVec2(0, 0)))
            AddExistingProject();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Add an existing project folder");
        ImGui::SameLine();
        if (AccentButton(ICON_FA_PLUS "  New Project", ImVec2(0, 0)))
            m_Page = Page::NewProject;
        ImGui::PopStyleVar(2);
        ImGui::Spacing();
        if (!m_Status.empty())
        {
            ImGui::TextColored(m_StatusIsError ? ImVec4(1.0f, 0.45f, 0.45f, 1.0f) : ImVec4(0.45f, 0.85f, 0.55f, 1.0f), "%s", m_Status.c_str());
            ImGui::Spacing();
        }

        std::vector<int> visible = VisibleProjects();
        if (visible.empty())
        {
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Text, kTextDim);
            ImGui::TextWrapped(m_Projects.empty() ? "No projects yet. Create your first one from a template - the 2D Platformer and First-Person templates are playable right away."
                                                  : "No project matches the search.");
            ImGui::PopStyleColor();
            if (m_Projects.empty())
            {
                ImGui::Spacing();
                if (AccentButton(ICON_FA_WAND_MAGIC_SPARKLES "  Browse Templates", ImVec2(220, 40)))
                    m_Page = Page::NewProject;
            }
            return;
        }

        ImGui::BeginChild("##projects", ImVec2(0, 0));
        if (m_GridView)
        {
            const float cardW = 250.0f * m_UIScale, cardH = 210.0f * m_UIScale, gap = 16.0f;
            const int columns = std::max(1, (int)((ImGui::GetContentRegionAvail().x + gap) / (cardW + gap)));
            for (int i = 0; i < (int)visible.size(); ++i)
            {
                if (i % columns != 0)
                    ImGui::SameLine(0.0f, gap);
                DrawProjectCard(m_Projects[(size_t)visible[(size_t)i]], visible[(size_t)i], cardW, cardH);
                if (m_PendingRemove >= 0)
                    break;
            }
        }
        else
        {
            for (int idx : visible)
            {
                DrawProjectRow(m_Projects[(size_t)idx], idx);
                if (m_PendingRemove >= 0)
                    break;
            }
        }
        ImGui::EndChild();
        if (m_PendingRemove >= 0 && m_PendingRemove < (int)m_Projects.size())
        {
            m_Projects.erase(m_Projects.begin() + m_PendingRemove);
            SaveConfig();
        }
        m_PendingRemove = -1;
    }

    void ProjectHub::DrawProjectContextMenu(ProjectInfo& project, int index)
    {
        if (!ImGui::BeginPopupContextItem("##projctx"))
            return;
        ImGui::TextDisabled("%s", project.Name.c_str());
        ImGui::Separator();
        if (ImGui::MenuItem(ICON_FA_FOLDER_OPEN "  Open", nullptr, false, !project.Missing))
            OpenProject(project);
        if (ImGui::MenuItem(project.Favorite ? ICON_FA_STAR "  Unpin" : ICON_FA_STAR "  Pin to Top"))
        {
            project.Favorite = !project.Favorite;
            SaveConfig();
        }
        if (ImGui::MenuItem(ICON_FA_UP_RIGHT_FROM_SQUARE "  Show in File Manager", nullptr, false, !project.Missing))
            RevealInFileManager(project.Path);
        if (ImGui::MenuItem(ICON_FA_COPY "  Copy Path"))
            ImGui::SetClipboardText(project.Path.string().c_str());
        ImGui::Separator();
        if (ImGui::MenuItem(ICON_FA_XMARK "  Remove from List"))
            m_PendingRemove = index;
        ImGui::EndPopup();
    }

    void ProjectHub::DrawProjectCard(ProjectInfo& project, int index, float width, float height)
    {
        ImGui::PushID(index);
        ImVec2 a = ImGui::GetCursorScreenPos();
        ImVec2 b(a.x + width, a.y + height);
        const bool clicked = ImGui::InvisibleButton("##card", ImVec2(width, height));
        const bool hovered = ImGui::IsItemHovered();
        DrawProjectContextMenu(project, index);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(a, b, hovered ? kCardHover : kCardBg, 10.0f);

        // Küçük resim: Library/thumbnail.png (editör sahne kaydında yazar) ya da baş harf rozeti
        const float thumbH = height * 0.58f;
        ImVec2 ta(a.x + 8, a.y + 8), tb(b.x - 8, a.y + thumbH);
        if (!project.ThumbnailTried)
        {
            project.ThumbnailTried = true;
            std::error_code ec;
            fs::path thumb = project.Path / "Library" / "thumbnail.png";
            if (fs::exists(thumb, ec))
                project.Thumbnail = Texture2D::Create(thumb.string());
        }
        if (project.Thumbnail)
        {
            dl->AddImageRounded((ImTextureID)(uint64_t)project.Thumbnail->GetRendererID(), ta, tb, ImVec2(0, 1), ImVec2(1, 0), IM_COL32_WHITE, 6.0f);
        }
        else
        {
            const ImU32 hue = ImColor::HSV((float)(std::hash<std::string>{}(project.Name) % 1000) / 1000.0f, 0.45f, 0.55f);
            dl->AddRectFilledMultiColor(ta, tb, hue, hue, IM_COL32(28, 30, 36, 255), IM_COL32(28, 30, 36, 255));
            std::string initials;
            for (size_t i = 0; i < project.Name.size() && initials.size() < 2; ++i)
                if (std::isalnum((unsigned char)project.Name[i]) && (i == 0 || project.Name[i - 1] == ' '))
                    initials += (char)std::toupper((unsigned char)project.Name[i]);
            ImFont* font = ImGui::GetFont();
            float size = ImGui::GetFontSize() * 2.6f;
            ImVec2 ts = font->CalcTextSizeA(size, FLT_MAX, 0.0f, initials.c_str());
            dl->AddText(font, size, ImVec2((ta.x + tb.x - ts.x) * 0.5f, (ta.y + tb.y - ts.y) * 0.5f), IM_COL32(255, 255, 255, 200), initials.c_str());
        }
        if (project.Favorite)
            dl->AddText(ImVec2(tb.x - 22, ta.y + 6), IM_COL32(255, 210, 80, 255), ICON_FA_STAR);
        // Metin
        float y = a.y + thumbH + 8.0f;
        dl->PushClipRect(a, b, true);
        dl->AddText(ImGui::GetFont(), ImGui::GetFontSize() * 1.1f, ImVec2(a.x + 12, y), IM_COL32(235, 236, 240, 255), project.Name.c_str());
        y += ImGui::GetFontSize() * 1.35f;
        std::string when = project.Missing ? std::string(ICON_FA_TRIANGLE_EXCLAMATION " Missing folder") : RelativeTime(project.LastOpened);
        dl->AddText(ImVec2(a.x + 12, y), project.Missing ? IM_COL32(255, 120, 110, 255) : IM_COL32(150, 154, 162, 255), when.c_str());
        y += ImGui::GetTextLineHeightWithSpacing();
        dl->AddText(ImVec2(a.x + 12, y), IM_COL32(110, 114, 122, 255), project.Path.string().c_str());
        dl->PopClipRect();
        if (hovered)
        {
            dl->AddRect(a, b, ImGui::GetColorU32(kAccent), 10.0f, 0, 1.5f);
            ImGui::SetTooltip("%s\n%s", project.Path.string().c_str(), project.Missing ? "The folder no longer exists" : "Click to open, right-click for options");
        }
        if (clicked && !project.Missing)
            OpenProject(project);
        ImGui::PopID();
    }

    void ProjectHub::DrawProjectRow(ProjectInfo& project, int index)
    {
        ImGui::PushID(index);
        const float h = 56.0f * m_UIScale;
        ImVec2 a = ImGui::GetCursorScreenPos();
        const float w = ImGui::GetContentRegionAvail().x;
        const bool clicked = ImGui::InvisibleButton("##row", ImVec2(w, h));
        const bool hovered = ImGui::IsItemHovered();
        DrawProjectContextMenu(project, index);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(a, ImVec2(a.x + w, a.y + h), hovered ? kCardHover : kCardBg, 8.0f);
        dl->AddText(ImVec2(a.x + 14, a.y + 9), project.Favorite ? IM_COL32(255, 210, 80, 255) : IM_COL32(120, 160, 230, 255),
                    project.Favorite ? ICON_FA_STAR : ICON_FA_CUBE);
        dl->AddText(ImGui::GetFont(), ImGui::GetFontSize() * 1.05f, ImVec2(a.x + 42, a.y + 8), IM_COL32(235, 236, 240, 255), project.Name.c_str());
        dl->AddText(ImVec2(a.x + 42, a.y + 30), IM_COL32(120, 124, 132, 255), project.Path.string().c_str());
        std::string when = project.Missing ? std::string(ICON_FA_TRIANGLE_EXCLAMATION " Missing") : RelativeTime(project.LastOpened);
        ImVec2 ws = ImGui::CalcTextSize(when.c_str());
        dl->AddText(ImVec2(a.x + w - ws.x - 16, a.y + (h - ws.y) * 0.5f), project.Missing ? IM_COL32(255, 120, 110, 255) : IM_COL32(150, 154, 162, 255),
                    when.c_str());
        if (clicked && !project.Missing)
            OpenProject(project);
        ImGui::Dummy(ImVec2(0, 4));
        ImGui::PopID();
    }

    // ---- Yeni proje

    void ProjectHub::DrawNewProjectPage()
    {
        DrawHeader("New Project", "Pick a template. Sample templates are playable right away.");
        const float panelW = 330.0f * m_UIScale;
        // Sol sütun: sektörler (Unreal: Games, Film / Video & Live Events, Architecture, Automotive, Simulation)
        // Dar sütun: pencere ilk açıldığında yan yana iki şablon kartı sığsın (uzun ad iki satıra bölünür)
        const float industryW = 150.0f * m_UIScale;
        ImGui::BeginChild("##industries", ImVec2(industryW, 0));
        auto industryIcon = [](const std::string& n) {
            if (n == "Games") return ICON_FA_GAMEPAD;
            if (n == "Architecture") return ICON_FA_BUILDING;
            if (n == "Automotive") return ICON_FA_CAR;
            if (n == "Simulation") return ICON_FA_GLOBE;
            return ICON_FA_FILM;
        };
        for (const auto& ind : Launcher::GetIndustries())
        {
            const bool sel = m_Industry == ind;
            ImGui::PushStyleColor(ImGuiCol_Header, kAccent);
            std::string text = ind;
            if (const size_t amp = text.find(" & "); amp != std::string::npos && ImGui::CalcTextSize(text.c_str()).x > industryW - 40.0f * m_UIScale)
                text = text.substr(0, amp + 2) + "\n      " + text.substr(amp + 3);
            const std::string label = std::string(industryIcon(ind)) + "  " + text + "##" + ind;
            const bool twoLines = text.find('\n') != std::string::npos;
            if (ImGui::Selectable(label.c_str(), sel, 0, ImVec2(0, (twoLines ? 48.0f : 34.0f) * m_UIScale)))
            {
                m_Industry = ind;
                for (const auto& t : GetTemplates())
                    if (t.Industry == ind)
                    {
                        m_TemplateId = t.Id;
                        break;
                    }
            }
            ImGui::PopStyleColor();
        }
        ImGui::EndChild();
        ImGui::SameLine(0.0f, 14.0f);
        ImGui::BeginChild("##templates", ImVec2(ImGui::GetContentRegionAvail().x - panelW - 20.0f, 0));
        std::vector<HubTemplate> templates;
        for (const auto& t : GetTemplates())
            if (t.Industry == m_Industry)
                templates.push_back(t);
        // Esnek kart genişliği: en az 200 px; sığan sütun sayısı kadar kart alanı tam doldurur
        const float gap = 14.0f;
        const float avail = ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ScrollbarSize;
        const float minCardW = 200.0f * m_UIScale;
        const int columns = std::max(1, (int)((avail + gap) / (minCardW + gap)));
        const float cardW = std::min(280.0f * m_UIScale, (avail - gap * (float)(columns - 1)) / (float)columns);
        const float cardH = cardW * 0.95f;
        for (int i = 0; i < (int)templates.size(); ++i)
        {
            const auto& t = templates[(size_t)i];
            if (i % columns != 0)
                ImGui::SameLine(0.0f, gap);
            ImGui::PushID(t.Id.c_str());
            ImVec2 a = ImGui::GetCursorScreenPos();
            ImVec2 b(a.x + cardW, a.y + cardH);
            if (ImGui::InvisibleButton("##tmpl", ImVec2(cardW, cardH)))
                m_TemplateId = t.Id;
            const bool hovered = ImGui::IsItemHovered();
            const bool selected = m_TemplateId == t.Id;
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(a, b, hovered ? kCardHover : kCardBg, 10.0f);
            const float artBottom = a.y + cardH * 0.5f;
            DrawTemplateArt(dl, ImVec2(a.x + 8, a.y + 8), ImVec2(b.x - 8, artBottom), t, 3.2f, TemplateThumbnail(t.Id));
            ImFont* font = ImGui::GetFont();
            const float fs = ImGui::GetFontSize();
            dl->AddText(font, fs * 1.1f, ImVec2(a.x + 12, artBottom + 8), IM_COL32(235, 236, 240, 255), t.Name.c_str());
            const ImU32 badge = t.Category == "Sample" ? IM_COL32(70, 170, 100, 255) : IM_COL32(80, 110, 170, 255);
            ImVec2 bs = ImGui::CalcTextSize(t.Category.c_str());
            ImVec2 ba(b.x - bs.x - 22, artBottom + 10);
            dl->AddRectFilled(ba, ImVec2(ba.x + bs.x + 12, ba.y + bs.y + 4), badge, 6.0f);
            dl->AddText(ImVec2(ba.x + 6, ba.y + 2), IM_COL32(255, 255, 255, 235), t.Category.c_str());
            ImVec4 clip(a.x, a.y, b.x, b.y - 6);
            dl->AddText(font, fs * 0.92f, ImVec2(a.x + 12, artBottom + 10 + fs * 1.5f), IM_COL32(150, 154, 162, 255), t.Description.c_str(), nullptr,
                        cardW - 24.0f, &clip);
            if (selected)
                dl->AddRect(a, b, ImGui::GetColorU32(kAccent), 10.0f, 0, 2.5f);
            else if (hovered)
                dl->AddRect(a, b, IM_COL32(255, 255, 255, 60), 10.0f, 0, 1.0f);
            if (i % columns == columns - 1)
                ImGui::Dummy(ImVec2(0, gap - ImGui::GetStyle().ItemSpacing.y));
            ImGui::PopID();
        }
        ImGui::EndChild();

        // Sağ panel: seçili şablon + seçenekler
        ImGui::SameLine(0.0f, 20.0f);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::ColorConvertU32ToFloat4(kCardBg));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.0f, 14.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 10.0f);
        ImGui::BeginChild("##newproject", ImVec2(panelW, 0), ImGuiChildFlags_AlwaysUseWindowPadding);
        const HubTemplate* t = Launcher::FindTemplate(m_TemplateId);
        if (t)
        {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImVec2 a = ImGui::GetCursorScreenPos();
            // Seçili şablon: 16:9 büyük görsel
            const float artW = ImGui::GetContentRegionAvail().x;
            const float artH = TemplateThumbnail(t->Id) ? std::round(artW * 9.0f / 16.0f) : 120.0f;
            DrawTemplateArt(dl, a, ImVec2(a.x + artW, a.y + artH), *t, 4.0f, TemplateThumbnail(t->Id));
            ImGui::Dummy(ImVec2(0, artH + 8.0f));
            ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.3f);
            ImGui::TextUnformatted(t->Name.c_str());
            ImGui::PopFont();
            ImGui::PushStyleColor(ImGuiCol_Text, kTextDim);
            ImGui::TextWrapped("%s", t->Description.c_str());
            ImGui::PopStyleColor();
            for (const auto& f : t->Features)
            {
                ImGui::TextColored(ImVec4(0.45f, 0.85f, 0.55f, 1.0f), ICON_FA_CHECK);
                ImGui::SameLine();
                ImGui::TextWrapped("%s", f.c_str());
            }
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
        }
        ImGui::TextUnformatted("Project name");
        // Unity Hub gibi: kullanıcı adı elle yazmadıysa kutuda bu konumdaki ilk boş ad görünür ("My Game", doluysa "My Game (1)" ...)
        if (!m_NewNameEdited && --m_NewNameRefresh <= 0)
        {
            m_NewName = Launcher::UniqueProjectName(m_NewLocation, kDefaultProjectName);
            m_NewNameRefresh = 30;
        }
        char nameBuf[128];
        std::snprintf(nameBuf, sizeof(nameBuf), "%s", m_NewName.c_str());
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputText("##pname", nameBuf, sizeof(nameBuf)))
        {
            m_NewName = nameBuf;
            m_NewNameEdited = true;
        }
        ImGui::TextUnformatted("Location");
        char locBuf[512];
        std::snprintf(locBuf, sizeof(locBuf), "%s", m_NewLocation.c_str());
        ImGui::SetNextItemWidth(-40.0f);
        if (ImGui::InputText("##ploc", locBuf, sizeof(locBuf)))
        {
            m_NewLocation = locBuf;
            m_NewNameRefresh = 0;
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_FOLDER "##browse", ImVec2(-1, 0)))
        {
            nfdu8char_t* out = nullptr;
            if (NFD_PickFolderU8(&out, m_NewLocation.c_str()) == NFD_OKAY && out)
            {
                m_NewLocation = out;
                m_NewNameRefresh = 0;
                NFD_FreePathU8(out);
            }
        }
        ImGui::Spacing();
        const char* backends[] = { "Jolt (fast, default)", "MuJoCo (articulated)" };
        ImGui::TextUnformatted("3D physics engine");
        ImGui::SetNextItemWidth(-1);
        ImGui::Combo("##physics", &m_PhysicsBackend, backends, 2);
        if (t && !t->Addons.empty())
        {
            ImGui::Checkbox("Install recommended addons", &m_InstallAddons);
            std::string list;
            for (auto& a : t->Addons) list += (list.empty() ? "" : ", ") + a;
            ImGui::PushStyleColor(ImGuiCol_Text, kTextDim);
            ImGui::TextWrapped("%s", list.c_str());
            ImGui::PopStyleColor();
        }
        ImGui::Spacing();
        std::string why;
        const bool valid = Launcher::IsValidProjectName(m_NewName, &why);
        // Yazılan ad doluysa proje sıradaki ilk boş numarayla oluşur; alttaki yol gerçek hedefi gösterir
        const std::string finalName = valid ? Launcher::UniqueProjectName(m_NewLocation, m_NewName) : m_NewName;
        const fs::path target = fs::path(m_NewLocation) / finalName;
        if (!valid)
            ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.45f, 1.0f), "%s", why.c_str());
        else
        {
            ImGui::PushStyleColor(ImGuiCol_Text, kTextDim);
            ImGui::TextWrapped("%s", target.string().c_str());
            ImGui::PopStyleColor();
        }
        ImGui::Spacing();
        ImGui::BeginDisabled(!valid || !t);
        if (AccentButton(ICON_FA_ROCKET "  Create Project", ImVec2(-1, 40.0f)))
        {
            CreateFromTemplate(m_TemplateId, finalName, m_NewLocation, true);
            m_NewNameEdited = false;
            m_NewNameRefresh = 0;
        }
        ImGui::EndDisabled();
        if (!m_Status.empty() && m_StatusIsError)
            ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.45f, 1.0f), "%s", m_Status.c_str());
        ImGui::EndChild();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();
    }

    // ---- Öğren

    void ProjectHub::DrawLearnPage()
    {
        DrawHeader("Learn", "Guides for the editor and a searchable reference of every script function.");
        const auto& guides = Launcher::GetGuides();
        const float listW = 260.0f * m_UIScale;
        ImGui::BeginChild("##learnlist", ImVec2(listW, 0));
        for (int i = 0; i <= (int)guides.size(); ++i)
        {
            const bool api = i == (int)guides.size();
            const char* icon = api ? ICON_FA_BOOK : guides[(size_t)i].Icon;
            const char* title = api ? "Script API Reference" : guides[(size_t)i].Title;
            const char* summary = api ? "Every engine function available to scripts." : guides[(size_t)i].Summary;
            ImGui::PushID(i);
            ImVec2 a = ImGui::GetCursorScreenPos();
            const float h = 64.0f * m_UIScale;
            if (ImGui::InvisibleButton("##guide", ImVec2(listW - 8.0f, h)))
                m_LearnTopic = i;
            const bool hovered = ImGui::IsItemHovered();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(a, ImVec2(a.x + listW - 8.0f, a.y + h), m_LearnTopic == i ? IM_COL32(40, 62, 98, 255) : hovered ? kCardHover : kCardBg, 8.0f);
            dl->AddText(ImVec2(a.x + 12, a.y + 10), IM_COL32(140, 180, 255, 255), icon);
            dl->AddText(ImVec2(a.x + 40, a.y + 9), IM_COL32(235, 236, 240, 255), title);
            dl->PushClipRect(a, ImVec2(a.x + listW - 12.0f, a.y + h), true);
            dl->AddText(ImVec2(a.x + 40, a.y + 32), IM_COL32(140, 144, 152, 255), summary);
            dl->PopClipRect();
            ImGui::Dummy(ImVec2(0, 2));
            ImGui::PopID();
        }
        ImGui::EndChild();
        ImGui::SameLine(0.0f, 20.0f);
        ImGui::BeginChild("##learnbody", ImVec2(0, 0));
        if (m_LearnTopic >= (int)guides.size())
        {
            ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.35f);
            ImGui::TextUnformatted("Script API Reference");
            ImGui::PopFont();
            Launcher::DrawApiReference(m_ApiSearch);
        }
        else
        {
            const auto& g = guides[(size_t)m_LearnTopic];
            ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.35f);
            ImGui::Text("%s  %s", g.Icon, g.Title);
            ImGui::PopFont();
            ImGui::Separator();
            Launcher::DrawMarkup(g.Body);
        }
        ImGui::EndChild();
    }

    // ---- Addon'lar

    void ProjectHub::DrawAddonsPage()
    {
        DrawHeader("Addons", "Built-in addons you can install into any project from Addons > Addon Manager inside the editor.");
        const auto& catalog = AddonManager::GetCatalog();
        if (catalog.empty())
        {
            ImGui::TextDisabled("The addon catalog (Resources/Addons) is empty.");
            return;
        }
        const float cardW = 360.0f * m_UIScale, cardH = 150.0f * m_UIScale, gap = 14.0f;
        const int columns = std::max(1, (int)((ImGui::GetContentRegionAvail().x + gap) / (cardW + gap)));
        for (int i = 0; i < (int)catalog.size(); ++i)
        {
            const auto& m = catalog[(size_t)i];
            if (i % columns != 0)
                ImGui::SameLine(0.0f, gap);
            ImGui::PushID(i);
            ImVec2 a = ImGui::GetCursorScreenPos();
            ImVec2 b(a.x + cardW, a.y + cardH);
            ImGui::Dummy(ImVec2(cardW, cardH));
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImFont* font = ImGui::GetFont();
            const float fs = ImGui::GetFontSize();
            dl->AddRectFilled(a, b, kCardBg, 10.0f);
            dl->AddText(font, fs * 1.9f, ImVec2(a.x + 16, a.y + 16), IM_COL32(120, 180, 255, 255), ICON_FA_PUZZLE_PIECE);
            dl->AddText(font, fs * 1.1f, ImVec2(a.x + 64, a.y + 14), IM_COL32(235, 236, 240, 255), m.Name.c_str());
            std::string sub = "v" + m.Version + "  |  " + m.Category + "  |  " + m.Author;
            dl->AddText(ImVec2(a.x + 64, a.y + 38), IM_COL32(130, 134, 142, 255), sub.c_str());
            ImVec4 clip(a.x, a.y, b.x, b.y - 8);
            dl->AddText(font, fs * 0.92f, ImVec2(a.x + 16, a.y + 66), IM_COL32(150, 154, 162, 255), m.Description.c_str(), nullptr, cardW - 32.0f, &clip);
            if (i % columns == columns - 1)
                ImGui::Dummy(ImVec2(0, gap - ImGui::GetStyle().ItemSpacing.y));
            ImGui::PopID();
        }
    }

    // ---- Ayarlar

    void ProjectHub::DrawSettingsPage()
    {
        DrawHeader("Settings", nullptr);
        ImGui::BeginChild("##settings", ImVec2(std::min(720.0f * m_UIScale, ImGui::GetContentRegionAvail().x), 0));
        ImGui::SeparatorText("Projects");
        ImGui::TextUnformatted("Default location for new projects");
        char locBuf[512];
        std::snprintf(locBuf, sizeof(locBuf), "%s", m_DefaultLocation.c_str());
        ImGui::SetNextItemWidth(-40.0f);
        if (ImGui::InputText("##defloc", locBuf, sizeof(locBuf)))
        {
            m_DefaultLocation = locBuf;
            SaveConfig();
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_FOLDER "##defbrowse", ImVec2(-1, 0)))
        {
            nfdu8char_t* out = nullptr;
            if (NFD_PickFolderU8(&out, m_DefaultLocation.c_str()) == NFD_OKAY && out)
            {
                m_DefaultLocation = out;
                NFD_FreePathU8(out);
                SaveConfig();
            }
        }
        if (ImGui::Checkbox("Show projects whose folder is missing", &m_ShowMissing))
            SaveConfig();
        if (ImGui::Button(ICON_FA_TRASH "  Clear project list"))
        {
            m_Projects.clear();
            SaveConfig();
        }
        ImGui::SeparatorText("Appearance");
        if (ImGui::SliderFloat("UI Scale", &m_UIScale, 0.75f, 1.75f, "%.2fx"))
            SaveConfig();
        // Platform modülleri (Unity Hub "Installs > Modules"): hangi platforma bu bilgisayardan derlenebilir, eksikse ne kurulur
        ImGui::SeparatorText("Platform Modules");
        ImGui::PushStyleColor(ImGuiCol_Text, kTextDim);
        ImGui::TextWrapped("Build support for each platform (no consoles). Build Settings > Build All Ready Platforms builds every "
                           "platform marked Ready in one click.");
        ImGui::PopStyleColor();
        if (ImGui::BeginTable("##modules", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_SizingStretchProp))
        {
            ImGui::TableSetupColumn("Platform", ImGuiTableColumnFlags_WidthFixed, 150.0f * m_UIScale);
            ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 120.0f * m_UIScale);
            ImGui::TableSetupColumn("Toolchain");
            for (const auto& m : PlatformModules::Detect())
            {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::AlignTextToFramePadding();
                ImGui::TextUnformatted(m.Platform.c_str());
                ImGui::TableNextColumn();
                const bool ready = m.State == PlatformModules::Status::Ready;
                const ImVec4 col = ready ? ImVec4(0.35f, 0.85f, 0.45f, 1.0f)
                                 : m.State == PlatformModules::Status::Missing ? ImVec4(0.98f, 0.72f, 0.25f, 1.0f) : kTextDim;
                ImGui::TextColored(col, "%s %s", ready ? ICON_FA_CIRCLE_CHECK : m.State == PlatformModules::Status::Missing ? ICON_FA_TRIANGLE_EXCLAMATION : ICON_FA_CIRCLE_INFO,
                                   PlatformModules::StatusName(m.State));
                ImGui::TableNextColumn();
                ImGui::TextWrapped("%s", m.Toolchain.c_str());
                ImGui::PushStyleColor(ImGuiCol_Text, kTextDim);
                if (!m.Detail.empty())
                    ImGui::TextWrapped("%s", m.Detail.c_str());
                if (!m.Install.empty())
                    ImGui::TextWrapped("Install: %s", m.Install.c_str());
                ImGui::PopStyleColor();
            }
            ImGui::EndTable();
        }
        ImGui::SeparatorText("About");
        ImGui::Text("Conqueror's Engine %s", EngineVersion);
        ImGui::PushStyleColor(ImGuiCol_Text, kTextDim);
        ImGui::TextWrapped("Rendering: OpenGL 4.6 (shaders compiled through SPIR-V). Scripting: ConquerorScript (JIT), Luau, Python, C#, Java, "
                           "C/C++, Visual Scripting. Physics: Jolt and MuJoCo (3D), Box2D (2D).");
        ImGui::TextWrapped("Third-party: Dear ImGui, ImGuizmo, imnodes, GLFW, glad, glm, EnTT, yaml-cpp, spdlog, stb, FreeType, Assimp, "
                           "Jolt Physics, Box2D, MuJoCo, Luau, sljit, glslang, SPIRV-Cross, Native File Dialog.");
        ImGui::PopStyleColor();
        ImGui::Spacing();
        ImGui::TextDisabled("Hub settings: %s", m_ConfigFile.string().c_str());
        ImGui::EndChild();
    }

    // ------------------------------------------------------------------ Otomasyon

    void ProjectHub::RegisterAutomation()
    {
        Automation::RegisterCommand("hub", [this](const Automation::CommandArgs& a, std::string& err) {
            // hub page <projects|new|learn|addons|settings> | template <id> | create <template> <name> <location>
            //     | expect_projects <n> | search <text> | learn <index> | view <grid|list>
            if (a.size() < 2) { err = "hub page|template|create|expect_projects|search|learn|view"; return false; }
            if (a[1] == "page" && a.size() > 2)
            {
                static const char* names[] = { "projects", "new", "learn", "addons", "settings" };
                for (int i = 0; i < 5; ++i)
                    if (a[2] == names[i]) { m_Page = (Page)i; return true; }
                err = "unknown page";
                return false;
            }
            if (a[1] == "template" && a.size() > 2)
            {
                const HubTemplate* t = Launcher::FindTemplate(a[2]);
                m_TemplateId = a[2];
                if (t)
                    m_Industry = t->Industry;
                return t != nullptr;
            }
            if (a[1] == "create" && a.size() > 4)
            {
                if (!CreateFromTemplate(a[2], a[3], a[4], false)) { err = m_Status; return false; }
                return true;
            }
            // Yeni Proje sayfasının otomatik adı (test): konum, beklenen ad, kutudaki adla oluşturma
            if (a[1] == "location" && a.size() > 2)
            {
                m_NewLocation = a[2];
                m_NewNameEdited = false;
                m_NewNameRefresh = 0;
                return true;
            }
            if (a[1] == "expect_name" && a.size() > 2)
            {
                if (m_NewName != a[2]) { err = "project name is '" + m_NewName + "'"; return false; }
                return true;
            }
            if (a[1] == "create_named")
            {
                if (!CreateFromTemplate(m_TemplateId, Launcher::UniqueProjectName(m_NewLocation, m_NewName), m_NewLocation, false)) { err = m_Status; return false; }
                m_NewNameRefresh = 0;
                return true;
            }
            if (a[1] == "expect_projects" && a.size() > 2)
            {
                int n = (int)VisibleProjects().size();
                if (n != std::atoi(a[2].c_str())) { err = "projects " + std::to_string(n); return false; }
                return true;
            }
            if (a[1] == "search") { m_Search = a.size() > 2 ? a[2] : ""; return true; }
            if (a[1] == "learn" && a.size() > 2) { m_LearnTopic = std::atoi(a[2].c_str()); return true; }
            if (a[1] == "view" && a.size() > 2) { m_GridView = a[2] == "grid"; return true; }
            if (a[1] == "api_search") { m_ApiSearch = a.size() > 2 ? a[2] : ""; return true; }
            err = "unknown hub command";
            return false;
        });
    }
}
