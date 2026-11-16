// Hub > Installs (47, Unity Hub "Installs" sayfası): bu bilgisayardaki editör sürümleri, GitHub Releases'tan yeni sürüm kurma
// (Official / Pre-release / ConquerorX kanalları), modül ekleme (platformlar, diller, belgeler; gereksinimleri algılanır), Locate,
// kaldırma ve ConquerorX'in her açılışta en son commit yapısına kendiliğinden güncellenmesi.
#include "ProjectHub.h"

#include "Core/Automation/Automation.h"
#include "Core/Base/Version.h"
#include "Core/Logging/Log.h"
#include "Core/Utils/Platform.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <IconsFontAwesome6.h>
#include <nfd.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <thread>

namespace Conqueror::Editor
{
    namespace fs = std::filesystem;
    using Installs::Channel;

    namespace
    {
        const ImVec4 kAccent(0.26f, 0.59f, 0.98f, 1.0f);
        const ImVec4 kAccentDim(0.2f, 0.45f, 0.78f, 1.0f);
        const ImVec4 kTextDim(0.62f, 0.64f, 0.68f, 1.0f);
        const ImVec4 kWarn(0.98f, 0.72f, 0.25f, 1.0f);
        const ImVec4 kOk(0.42f, 0.85f, 0.52f, 1.0f);
        const ImVec4 kError(1.0f, 0.47f, 0.43f, 1.0f);
        const ImU32 kCardBg = IM_COL32(32, 34, 39, 255);
        const ImU32 kCardHover = IM_COL32(38, 41, 47, 255);

        bool AccentButton(const char* label, const ImVec2& size = ImVec2(0, 0))
        {
            ImGui::PushStyleColor(ImGuiCol_Button, kAccentDim);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, kAccent);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.16f, 0.4f, 0.72f, 1.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
            const bool pressed = ImGui::Button(label, size);
            ImGui::PopStyleVar();
            ImGui::PopStyleColor(3);
            return pressed;
        }

        void OpenExternal(const std::string& target)
        {
#ifdef CQ_PLATFORM_WINDOWS
            std::string cmd = "explorer \"" + target + "\"";
#elif defined(CQ_PLATFORM_LINUX)
            std::string cmd = "xdg-open \"" + target + "\" >/dev/null 2>&1 &";
#else
            std::string cmd = "open \"" + target + "\"";
#endif
            const int rc = std::system(cmd.c_str());
            (void)rc;
        }

        std::string Lower(std::string s)
        {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
            return s;
        }

        std::string SizeText(uint64_t bytes)
        {
            char buf[32];
            if (bytes >= 1024ull * 1024ull * 1024ull)
                std::snprintf(buf, sizeof(buf), "%.1f GB", (double)bytes / (1024.0 * 1024.0 * 1024.0));
            else
                std::snprintf(buf, sizeof(buf), "%.0f MB", std::max(1.0, (double)bytes / (1024.0 * 1024.0)));
            return buf;
        }

        ImU32 ChannelColor(Channel c)
        {
            switch (c)
            {
                case Channel::Official: return IM_COL32(60, 140, 90, 255);
                case Channel::PreRelease: return IM_COL32(170, 120, 40, 255);
                case Channel::ConquerorX: return IM_COL32(130, 80, 200, 255);
                case Channel::Local: return IM_COL32(80, 86, 100, 255);
            }
            return IM_COL32(80, 86, 100, 255);
        }

        const char* ModuleIcon(const Installs::ModuleInfo* m)
        {
            if (!m)
                return ICON_FA_PUZZLE_PIECE;
            const std::string& id = m->Id;
            if (m->Category == "Languages")
                return ICON_FA_CODE;
            if (m->Category == "Documentation")
                return ICON_FA_BOOK;
            if (id == "android" || id == "ios")
                return ICON_FA_MOBILE_SCREEN;
            if (id == "web")
                return ICON_FA_GLOBE;
            if (id == "server")
                return ICON_FA_SERVER;
            if (id == "meta-quest" || id == "android-xr" || id == "visionos")
                return ICON_FA_VR_CARDBOARD;
            if (id == "tvos")
                return ICON_FA_TV;
            if (id == "embedded-linux" || id == "qnx" || id == "harmonyos")
                return ICON_FA_MICROCHIP;
            return ICON_FA_DESKTOP;
        }

        // Rozet: yuvarlatılmış küçük etiket; genişliğini döndürür
        float Badge(ImDrawList* dl, ImVec2 at, const char* text, ImU32 color)
        {
            const ImVec2 ts = ImGui::CalcTextSize(text);
            const ImVec2 b(at.x + ts.x + 12.0f, at.y + ts.y + 4.0f);
            dl->AddRectFilled(at, b, color, 4.0f);
            dl->AddText(ImVec2(at.x + 6.0f, at.y + 2.0f), IM_COL32(240, 242, 246, 255), text);
            return b.x - at.x;
        }

        bool Contains(const std::vector<std::string>& v, const std::string& s) { return std::find(v.begin(), v.end(), s) != v.end(); }
    }

    // ------------------------------------------------------------------ veri
    void ProjectHub::ReloadInstalls()
    {
        m_Installs = Installs::LoadInstalls(ConfigDirectory());
        std::error_code ec;
        for (auto& i : m_Installs)
            if (fs::is_directory(i.Path, ec))
            {
                const auto disk = Installs::InstalledModules(i.Path);
                if (!disk.empty() || i.Modules.empty())
                    i.Modules = disk;
            }
        // Hub'ın kendi klasöründeki editör (her zaman listede, en üstte)
        const fs::path self = fs::current_path();
        if (!Installs::EditorExecutable(self).empty() &&
            std::none_of(m_Installs.begin(), m_Installs.end(), [&](const Installs::Install& i) { return fs::equivalent(i.Path, self, ec); }))
        {
            Installs::Install b;
            std::string commit;
            Channel kind = Channel::Local;
            std::error_code e2;
            if (fs::exists(self / "conqueror-install.json", e2))
                Installs::ReadInstallVersion(self, &commit, &kind);   // CI yapısı: kanal + commit
            b.Version = CQ_ENGINE_VERSION;
            b.Kind = kind;
            b.Commit = commit;
            b.Path = self;
            b.Bundled = true;
            b.Modules = Installs::InstalledModules(self);
            m_Installs.insert(m_Installs.begin(), b);
        }
    }

    void ProjectHub::FetchReleases(bool force)
    {
        if (m_ReleasesFetch || (m_ReleasesLoaded && !force))
            return;
        auto fetch = std::make_shared<ReleasesFetch>();
        m_ReleasesFetch = fetch;
        std::thread([fetch]() {
            std::string json, error;
            if (!Installs::FetchText(Installs::ReleasesApiUrl(), json, error))
            {
                fetch->Error = "Could not load versions from GitHub Releases: " + error;
                fetch->Done = true;
                return;
            }
            std::vector<Installs::Release> releases;
            if (!Installs::ParseReleases(json, releases, error))
            {
                fetch->Error = error;
                fetch->Done = true;
                return;
            }
            for (auto& r : releases)
            {
                if (r.ManifestUrl.empty())
                    continue;
                std::string manifest, err;
                if (Installs::FetchText(r.ManifestUrl, manifest, err))
                    Installs::ApplyManifest(r, manifest, err);
                if (!err.empty())
                    CQ_CORE_WARN("[Installs] {0}: {1}", r.Tag, err);
            }
            fetch->Releases = std::move(releases);
            fetch->Done = true;
        }).detach();
    }

    void ProjectHub::PollInstalls()
    {
        if (m_ReleasesFetch && m_ReleasesFetch->Done)
        {
            m_Releases = std::move(m_ReleasesFetch->Releases);
            m_ReleasesError = m_ReleasesFetch->Error;
            m_ReleasesLoaded = true;
            m_ReleasesFetch.reset();
            CQ_CORE_INFO("[Installs] {0} release(s) on GitHub{1}", m_Releases.size(), m_ReleasesError.empty() ? "" : " (" + m_ReleasesError + ")");
            if (!m_ConquerorXChecked)
                CheckConquerorXUpdates();
        }
        bool changed = false;
        for (auto& j : m_Jobs)
        {
            if (!j.Job->Finished || j.Reported)
                continue;
            j.Reported = true;
            changed = true;
            m_InstallsStatus = j.Job->Succeeded ? j.Label + ": done" : j.Label + ": " + j.Job->GetError();
            m_InstallsStatusError = !j.Job->Succeeded;
        }
        if (changed)
        {
            m_Jobs.erase(std::remove_if(m_Jobs.begin(), m_Jobs.end(), [](const HubJob& j) { return j.Reported; }), m_Jobs.end());
            ReloadInstalls();
        }
    }

    const Installs::Release* ProjectHub::FindRelease(const Installs::Install& install) const
    {
        for (const auto& r : m_Releases)
        {
            if (install.Kind == Channel::ConquerorX)
            {
                // ConquerorX modülleri tam olarak aynı commit'in yapısından olmalı
                if (r.Kind == Channel::ConquerorX && r.HasManifest && (install.Commit.empty() || r.Commit == install.Commit))
                    return &r;
            }
            else if (r.Kind != Channel::ConquerorX && r.HasManifest && r.Version == install.Version)
                return &r;
        }
        return nullptr;
    }

    const Installs::Requirement& ProjectHub::CachedRequirement(const std::string& id, const fs::path& installDir)
    {
        const std::string key = id + "|" + installDir.string();
        auto it = m_RequirementCache.find(key);
        if (it == m_RequirementCache.end())
            it = m_RequirementCache.emplace(key, Installs::CheckRequirement(id, installDir)).first;
        return it->second;
    }

    int ProjectHub::ProjectsUsing(const std::string& version) const
    {
        int n = 0;
        for (const auto& p : m_Projects)
            if (!p.Missing && p.EditorVersion == version)
                ++n;
        return n;
    }

    fs::path ProjectHub::EditorForProject(const ProjectInfo& project) const
    {
        std::error_code ec;
        if (!m_OpenWithEditor.empty() && fs::is_regular_file(m_OpenWithEditor, ec))
            return m_OpenWithEditor;
        // Projeyi son kaydeden sürüm kuruluysa o editör (Unity Hub gibi); yoksa Hub'ın kendi editörü
        if (!project.EditorVersion.empty())
        {
            for (int pass = 0; pass < 2; ++pass)
                for (const auto& i : m_Installs)
                {
                    if (i.Version != project.EditorVersion || (pass == 0 && i.Kind != Channel::Official))
                        continue;
                    const fs::path exe = Installs::EditorExecutable(i.Path);
                    if (!exe.empty())
                        return exe;
                }
        }
        return Installs::EditorExecutable(fs::current_path());
    }

    bool ProjectHub::StartInstall(const Installs::Release& release, const std::vector<std::string>& modules, std::string& error)
    {
        Installs::JobRequest req;
        if (!Installs::MakeInstallRequest(release, modules, ConfigDirectory(), req, error))
            return false;
        std::error_code ec;
        if (fs::exists(req.Target, ec))
        {
            error = "Conqueror's Engine " + release.Version + " is already installed in " + req.Target.string() + ".";
            return false;
        }
        HubJob job;
        job.Label = "Installing Conqueror's Engine " + release.Version + (release.Kind == Channel::ConquerorX ? " (ConquerorX)" : "");
        job.Job = Installs::StartJob(req);
        m_Jobs.push_back(job);
        return true;
    }

    bool ProjectHub::StartAddModules(int installIndex, const std::vector<std::string>& modules, std::string& error)
    {
        if (installIndex < 0 || installIndex >= (int)m_Installs.size())
        {
            error = "no such install";
            return false;
        }
        const Installs::Install& install = m_Installs[(size_t)installIndex];
        const Installs::Release* release = FindRelease(install);
        if (!release)
        {
            error = "Conqueror's Engine " + install.Version + " was not published on GitHub Releases, so its modules cannot be downloaded.";
            return false;
        }
        Installs::JobRequest req;
        if (!Installs::MakeAddModulesRequest(*release, install, modules, ConfigDirectory(), req, error))
            return false;
        HubJob job;
        job.Label = "Adding modules to " + install.Version;
        job.Job = Installs::StartJob(req);
        m_Jobs.push_back(job);
        return true;
    }

    void ProjectHub::CheckConquerorXUpdates()
    {
        m_ConquerorXChecked = true;
        const Installs::Release* latest = nullptr;
        for (const auto& r : m_Releases)
            if (r.Kind == Channel::ConquerorX && r.HasManifest && !r.EditorUrl.empty())
                latest = &r;
        if (!latest)
            return;
        for (const auto& i : m_Installs)
        {
            if (i.Kind != Channel::ConquerorX || i.Bundled || i.Located || latest->Commit.empty() || latest->Commit == i.Commit)
                continue;
            if (std::any_of(m_Jobs.begin(), m_Jobs.end(), [&](const HubJob& j) { return j.Job->Request.Target == i.Path; }))
                continue;
            // Kurulu modüllerden yeni yapıda olanlar da güncellenir
            std::vector<std::string> modules;
            for (const auto& m : i.Modules)
                if (std::any_of(latest->Modules.begin(), latest->Modules.end(), [&](const Installs::ModuleInfo& x) { return x.Id == m && !x.Url.empty(); }))
                    modules.push_back(m);
            Installs::JobRequest req;
            std::string error;
            if (!Installs::MakeInstallRequest(*latest, modules, ConfigDirectory(), req, error))
            {
                CQ_CORE_WARN("[Installs] ConquerorX update: {0}", error);
                continue;
            }
            req.Target = i.Path;
            req.Replace = true;
            HubJob job;
            job.Label = "Updating ConquerorX to " + latest->Commit.substr(0, 8);
            job.Job = Installs::StartJob(req);
            m_Jobs.push_back(job);
            CQ_CORE_INFO("[Installs] ConquerorX {0} -> {1}", i.Commit.substr(0, 8), latest->Commit.substr(0, 8));
        }
    }

    bool ProjectHub::LocateInstallAt(const fs::path& dir, std::string& error)
    {
        std::error_code ec;
        if (Installs::EditorExecutable(dir).empty())
        {
            error = "No Conqueror's Engine editor (ConquerorEngine) in " + dir.string();
            return false;
        }
        for (const auto& i : m_Installs)
            if (fs::equivalent(i.Path, dir, ec))
            {
                error = "This editor is already in the list.";
                return false;
            }
        Installs::Install install;
        std::string commit;
        Channel kind = Channel::Local;
        install.Version = Installs::ReadInstallVersion(dir, &commit, &kind);
        if (install.Version.empty())
        {
            error = "Could not read the editor's version in " + dir.string();
            return false;
        }
        install.Kind = kind;
        install.Commit = commit;
        install.Path = dir;
        install.Located = true;
        install.Modules = Installs::InstalledModules(dir);
        if (!Installs::RegisterInstall(ConfigDirectory(), install))
        {
            error = "Could not write the install list";
            return false;
        }
        ReloadInstalls();
        return true;
    }

    void ProjectHub::LocateInstall()
    {
        nfdu8char_t* outPath = nullptr;
        if (NFD_PickFolderU8(&outPath, nullptr) != NFD_OKAY || !outPath)
            return;
        const fs::path dir = fs::path(std::u8string((const char8_t*)outPath));
        NFD_FreePathU8(outPath);
        std::string error;
        if (LocateInstallAt(dir, error))
        {
            m_InstallsStatus = "Added " + dir.string();
            m_InstallsStatusError = false;
        }
        else
        {
            m_InstallsStatus = error;
            m_InstallsStatusError = true;
        }
    }

    // ------------------------------------------------------------------ arayüz
    void ProjectHub::DrawInstallsPage()
    {
        FetchReleases(false);
        const float s = m_UIScale;
        // Başlık + sağda Locate / Install Editor (Unity Hub)
        ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.7f);
        ImGui::TextUnformatted("Installs");
        ImGui::PopFont();
        {
            const float bw = 150.0f * s, gap = 8.0f;
            ImGui::SameLine(ImGui::GetContentRegionMax().x - bw * 2.0f - gap);
            if (ImGui::Button(ICON_FA_FOLDER_OPEN "  Locate", ImVec2(bw, 0)))
                LocateInstall();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
                ImGui::SetTooltip("Add an editor that is already on this computer (for example one you built from source)");
            ImGui::SameLine(0.0f, gap);
            if (AccentButton(ICON_FA_DOWNLOAD "  Install Editor", ImVec2(bw, 0)))
                m_OpenInstallModal = true;
        }
        ImGui::PushStyleColor(ImGuiCol_Text, kTextDim);
        ImGui::TextUnformatted("Editor versions on this computer. New versions are downloaded from the engine's GitHub Releases.");
        ImGui::PopStyleColor();
        ImGui::Spacing();

        // Sekmeler + arama
        if (ImGui::BeginTabBar("##installtabs"))
        {
            const char* tabs[] = { "All", "Official releases", "Pre-releases" };
            for (int i = 0; i < 3; ++i)
                if (ImGui::BeginTabItem(tabs[i]))
                {
                    m_InstallsTab = i;
                    ImGui::EndTabItem();
                }
            ImGui::EndTabBar();
        }
        ImGui::SetNextItemWidth(320.0f * s);
        char search[128];
        std::snprintf(search, sizeof(search), "%s", m_InstallsSearch.c_str());
        if (ImGui::InputTextWithHint("##installsearch", ICON_FA_MAGNIFYING_GLASS "  Search", search, sizeof(search)))
            m_InstallsSearch = search;
        ImGui::Spacing();

        // Sürmekte olan yeni kurulumlar
        for (auto& j : m_Jobs)
        {
            if (j.Job->Request.AddToExisting || j.Job->Request.Replace)
                continue;
            ImGui::PushID(j.Job.get());
            ImGui::TextUnformatted(j.Label.c_str());
            ImGui::ProgressBar(j.Job->Progress, ImVec2(-120.0f * s, 0), j.Job->GetStep().c_str());
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(-1, 0)))
                j.Job->Cancel = true;
            ImGui::PopID();
            ImGui::Spacing();
        }

        const std::string q = Lower(m_InstallsSearch);
        int shown = 0;
        ImGui::BeginChild("##installlist", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() - 4.0f));
        for (int i = 0; i < (int)m_Installs.size(); ++i)
        {
            const auto& inst = m_Installs[(size_t)i];
            if (m_InstallsTab == 1 && inst.Kind != Channel::Official)
                continue;
            if (m_InstallsTab == 2 && inst.Kind != Channel::PreRelease && inst.Kind != Channel::ConquerorX)
                continue;
            if (!q.empty() && Lower(inst.Version + " " + inst.Path.string() + " " + Installs::ChannelName(inst.Kind)).find(q) == std::string::npos)
                continue;
            DrawInstallRow(inst, i);
            ++shown;
        }
        if (shown == 0)
        {
            ImGui::Spacing();
            ImGui::TextDisabled(m_Installs.empty() ? "No editor installed. Click Install Editor to download one, or Locate to add one you built."
                                                   : "No install matches this tab or search.");
        }
        ImGui::EndChild();
        if (!m_InstallsStatus.empty())
            ImGui::TextColored(m_InstallsStatusError ? kError : kTextDim, "%s", m_InstallsStatus.c_str());

        DrawInstallEditorModal();
        DrawAddModulesModal();
    }

    void ProjectHub::DrawInstallRow(const Installs::Install& inst, int index)
    {
        const float s = m_UIScale;
        ImGui::PushID(index);
        const ImVec2 a = ImGui::GetCursorScreenPos();
        const float w = ImGui::GetContentRegionAvail().x;
        const HubJob* running = nullptr;
        for (const auto& j : m_Jobs)
            if ((j.Job->Request.AddToExisting || j.Job->Request.Replace) && j.Job->Request.Target == inst.Path)
                running = &j;
        const float h = (running ? 104.0f : 80.0f) * s;
        ImGui::InvisibleButton("##row", ImVec2(w - 48.0f * s, h));
        const bool hovered = ImGui::IsItemHovered();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(a, ImVec2(a.x + w, a.y + h), hovered ? kCardHover : kCardBg, 8.0f);
        const float fs = ImGui::GetFontSize();
        dl->AddText(ImGui::GetFont(), fs * 1.8f, ImVec2(a.x + 16.0f, a.y + 18.0f * s), ImGui::GetColorU32(kAccent), ICON_FA_CUBE);

        // Ad + kanal rozeti (+ ConquerorX commit'i)
        const float x0 = a.x + 64.0f * s;
        const std::string title = "Conqueror's Engine " + inst.Version;
        dl->AddText(ImGui::GetFont(), fs * 1.1f, ImVec2(x0, a.y + 10.0f * s), IM_COL32(236, 238, 242, 255), title.c_str());
        float bx = x0 + ImGui::GetFont()->CalcTextSizeA(fs * 1.1f, FLT_MAX, 0.0f, title.c_str()).x + 10.0f;
        bx += Badge(dl, ImVec2(bx, a.y + 11.0f * s), Installs::ChannelName(inst.Kind), ChannelColor(inst.Kind)) + 6.0f;
        if (inst.Bundled)
            bx += Badge(dl, ImVec2(bx, a.y + 11.0f * s), "This Hub", IM_COL32(60, 70, 90, 255)) + 6.0f;
        if (!inst.Commit.empty())
            dl->AddText(ImVec2(bx, a.y + 12.0f * s), IM_COL32(150, 154, 162, 255), inst.Commit.substr(0, 8).c_str());

        // Yol ve proje sayısı
        std::error_code ec;
        const bool exists = !Installs::EditorExecutable(inst.Path).empty();
        dl->AddText(ImVec2(x0, a.y + 34.0f * s), exists ? IM_COL32(130, 134, 142, 255) : IM_COL32(255, 120, 110, 255),
                    (exists ? inst.Path.string() : "Missing: " + inst.Path.string()).c_str());
        const int projects = ProjectsUsing(inst.Version);
        std::string info = std::to_string(projects) + (projects == 1 ? " project" : " projects");
        if (!inst.InstalledAt.empty())
            info += "   |   installed " + inst.InstalledAt;
        dl->AddText(ImVec2(x0, a.y + 54.0f * s), IM_COL32(150, 154, 162, 255), info.c_str());

        // Sağda modül rozetleri (Unity'deki platform simgeleri)
        float rx = a.x + w - 60.0f * s;
        int drawn = 0;
        for (auto it = inst.Modules.rbegin(); it != inst.Modules.rend(); ++it)
        {
            const Installs::ModuleInfo* m = Installs::FindModule(*it);
            const std::string label = std::string(ModuleIcon(m)) + " " + (m ? m->Name.substr(0, m->Name.find(" Build Support")) : *it);
            const float lw = ImGui::CalcTextSize(label.c_str()).x + 12.0f;
            if (rx - lw < x0 + 380.0f * s || drawn >= 6)
            {
                const std::string more = "+" + std::to_string((int)inst.Modules.size() - drawn);
                const float mw = ImGui::CalcTextSize(more.c_str()).x + 12.0f;
                Badge(dl, ImVec2(rx - mw, a.y + 32.0f * s), more.c_str(), IM_COL32(52, 56, 66, 255));
                break;
            }
            rx -= lw;
            Badge(dl, ImVec2(rx, a.y + 32.0f * s), label.c_str(), IM_COL32(52, 56, 66, 255));
            rx -= 6.0f;
            ++drawn;
        }
        if (hovered && m_PointerMoved && !inst.Modules.empty() && ImGui::BeginTooltip())
        {
            ImGui::TextUnformatted("Modules:");
            for (const auto& id : inst.Modules)
            {
                const Installs::ModuleInfo* m = Installs::FindModule(id);
                ImGui::BulletText("%s", m ? m->Name.c_str() : id.c_str());
            }
            ImGui::EndTooltip();
        }

        // Yükleme sürüyorsa (modül ekleme / ConquerorX güncellemesi) ilerleme
        if (running)
        {
            ImGui::SetCursorScreenPos(ImVec2(x0, a.y + 74.0f * s));
            ImGui::ProgressBar(running->Job->Progress, ImVec2(w - (64.0f + 140.0f) * s, 0), running->Job->GetStep().c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("Cancel"))
                running->Job->Cancel = true;
        }

        // Manage (⋯) menüsü
        ImGui::SetCursorScreenPos(ImVec2(a.x + w - 40.0f * s, a.y + (h - ImGui::GetFrameHeight()) * 0.5f));
        if (ImGui::Button(ICON_FA_GEAR "##manage", ImVec2(30.0f * s, 0)))
            ImGui::OpenPopup("##managemenu");
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("Manage");
        if (ImGui::BeginPopup("##managemenu"))
        {
            const Installs::Release* release = FindRelease(inst);
            if (ImGui::MenuItem(ICON_FA_PUZZLE_PIECE "  Add modules", nullptr, false, release != nullptr && running == nullptr))
            {
                m_AddModulesFor = index;
                m_OpenAddModulesModal = true;
            }
            if (!release && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip(m_ReleasesLoaded ? "This version was not published on GitHub Releases, so no module downloads exist for it."
                                                   : "Loading versions from GitHub Releases...");
            if (ImGui::MenuItem(ICON_FA_UP_RIGHT_FROM_SQUARE "  Show in File Manager", nullptr, false, exists))
                OpenExternal(inst.Path.string());
            if (ImGui::MenuItem(ICON_FA_COPY "  Copy Path"))
                ImGui::SetClipboardText(inst.Path.string().c_str());
            if (inst.Kind == Channel::ConquerorX && ImGui::MenuItem(ICON_FA_ROTATE "  Check for updates"))
            {
                m_ConquerorXChecked = false;
                FetchReleases(true);
            }
            ImGui::Separator();
            if (inst.Bundled)
                ImGui::MenuItem(ICON_FA_TRASH "  Uninstall", nullptr, false, false);
            else if (ImGui::MenuItem(inst.Located ? ICON_FA_XMARK "  Remove from List" : ICON_FA_TRASH "  Uninstall", nullptr, false, running == nullptr))
            {
                std::string error;
                if (Installs::Uninstall(ConfigDirectory(), inst, error))
                {
                    m_InstallsStatus = (inst.Located ? "Removed " : "Uninstalled ") + inst.Version;
                    m_InstallsStatusError = false;
                }
                else
                {
                    m_InstallsStatus = error;
                    m_InstallsStatusError = true;
                }
                ImGui::EndPopup();
                ImGui::PopID();
                ImGui::SetCursorScreenPos(ImVec2(a.x, a.y + h + 8.0f));
                ReloadInstalls();
                return;
            }
            if (inst.Bundled && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("This is the editor that came with this Hub.");
            ImGui::EndPopup();
        }
        ImGui::SetCursorScreenPos(ImVec2(a.x, a.y + h + 8.0f));
        ImGui::Dummy(ImVec2(0, 0));
        ImGui::PopID();
    }

    void ProjectHub::DrawModulePicker(const Installs::Release& release, const Installs::Install* existing, std::set<std::string>& picked)
    {
        const std::string host = Installs::HostOS();
        const fs::path dir = existing ? existing->Path : fs::path();
        for (const char* category : { "Platforms", "Languages", "Documentation", "Other" })
        {
            bool header = false;
            for (const auto& m : release.Modules)
            {
                if (m.Category != category && !(std::string(category) == "Other" && m.Category != "Platforms" && m.Category != "Languages" &&
                                                 m.Category != "Documentation"))
                    continue;
                if (!header)
                {
                    ImGui::SeparatorText(category);
                    header = true;
                }
                ImGui::PushID(m.Id.c_str());
                const bool installed = existing && Contains(existing->Modules, m.Id);
                const bool available = Installs::ModuleAvailableOnHost(m, host);
                const bool downloadable = !m.Url.empty();
                bool checked = m.BuiltIn || installed || picked.count(m.Id) > 0;
                ImGui::BeginDisabled(m.BuiltIn || installed || !available || !downloadable);
                if (ImGui::Checkbox(m.Name.c_str(), &checked))
                {
                    if (checked)
                        picked.insert(m.Id);
                    else
                        picked.erase(m.Id);
                }
                ImGui::EndDisabled();
                // Sağ: boyut ya da durum
                std::string right = m.BuiltIn ? "Included" : installed ? "Installed" : !available ? "Not available on this OS"
                                  : !downloadable ? "Not in this release" : SizeText(m.Size) + (m.InstalledSize ? " (" + SizeText(m.InstalledSize) + " on disk)" : "");
                ImGui::SameLine(ImGui::GetContentRegionMax().x - ImGui::CalcTextSize(right.c_str()).x - 4.0f);
                ImGui::TextDisabled("%s", right.c_str());
                ImGui::Indent(28.0f * m_UIScale);
                ImGui::PushStyleColor(ImGuiCol_Text, kTextDim);
                ImGui::TextWrapped("%s", m.Description.c_str());
                ImGui::PopStyleColor();
                // Gereksinimler: bulundu / modülle gelir / eksik (nasıl kurulur, lisans)
                for (const auto& rid : m.Requires)
                {
                    const Installs::Requirement& r = CachedRequirement(rid, dir);
                    if (r.Found)
                        ImGui::TextColored(kOk, ICON_FA_CIRCLE_CHECK "  %s: %s", r.Name.c_str(), r.Detail.c_str());
                    else if (Contains(m.Bundles, rid))
                        ImGui::TextColored(kOk, ICON_FA_BOX "  %s: comes with this module (%s)", r.Name.c_str(), r.License.c_str());
                    else
                    {
                        ImGui::TextColored(kWarn, ICON_FA_TRIANGLE_EXCLAMATION "  %s not found. %s", r.Name.c_str(), r.HowTo.c_str());
                        if (!r.Bundleable)
                            ImGui::TextDisabled("     %s", r.License.c_str());
                    }
                }
                ImGui::Unindent(28.0f * m_UIScale);
                ImGui::Spacing();
                ImGui::PopID();
            }
        }
    }

    void ProjectHub::DrawInstallEditorModal()
    {
        const float s = m_UIScale;
        if (m_OpenInstallModal)
        {
            ImGui::OpenPopup("Install Editor##modal");
            m_OpenInstallModal = false;
            m_InstallPicked = -1;
            m_PickedModules.clear();
            m_RequirementCache.clear();
            FetchReleases(false);
        }
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(std::min(820.0f * s, vp->Size.x - 40.0f), std::min(600.0f * s, vp->Size.y - 40.0f)), ImGuiCond_Appearing);
        if (!ImGui::BeginPopupModal("Install Editor##modal", nullptr, ImGuiWindowFlags_NoSavedSettings))
            return;
        const float footer = ImGui::GetFrameHeightWithSpacing() + 10.0f;
        if (m_InstallPicked < 0 || m_InstallPicked >= (int)m_Releases.size())
        {
            m_InstallPicked = -1;
            ImGui::TextUnformatted("Install Conqueror's Engine");
            if (ImGui::BeginTabBar("##releasetabs"))
            {
                const char* tabs[] = { "Official releases", "Pre-releases", "ConquerorX", "Archive" };
                for (int i = 0; i < 4; ++i)
                    if (ImGui::BeginTabItem(tabs[i]))
                    {
                        m_InstallModalTab = i;
                        ImGui::EndTabItem();
                    }
                ImGui::EndTabBar();
            }
            ImGui::BeginChild("##releases", ImVec2(0, -footer));
            if (m_InstallModalTab == 3)
            {
                std::string oldest;
                for (const auto& r : m_Releases)
                    if (r.Kind == Channel::Official && (oldest.empty() || Installs::CompareVersions(r.Version, oldest) < 0))
                        oldest = r.Version;
                const std::string text = Installs::BuildItYourselfText(oldest);
                ImGui::TextWrapped("%s", text.c_str());
                ImGui::Spacing();
                if (ImGui::Button(ICON_FA_COPY "  Copy commands"))
                    ImGui::SetClipboardText(text.c_str());
                ImGui::SameLine();
                if (ImGui::Button(ICON_FA_UP_RIGHT_FROM_SQUARE "  Open source repository"))
                    OpenExternal(Installs::SourceRepositoryUrl());
            }
            else if (m_ReleasesFetch)
                ImGui::TextDisabled("Loading versions from GitHub Releases...");
            else if (!m_ReleasesError.empty())
            {
                ImGui::TextColored(kError, "%s", m_ReleasesError.c_str());
                if (ImGui::Button("Retry"))
                    FetchReleases(true);
            }
            else
            {
                const Channel want = m_InstallModalTab == 0 ? Channel::Official : m_InstallModalTab == 1 ? Channel::PreRelease : Channel::ConquerorX;
                int listed = 0;
                for (int i = 0; i < (int)m_Releases.size(); ++i)
                {
                    const auto& r = m_Releases[(size_t)i];
                    if (r.Kind != want)
                        continue;
                    ++listed;
                    ImGui::PushID(i);
                    ImGui::AlignTextToFramePadding();
                    ImGui::Text("Conqueror's Engine %s", r.Version.c_str());
                    ImGui::SameLine();
                    ImGui::TextDisabled("%s%s%s", r.Date.c_str(), r.Commit.empty() || want != Channel::ConquerorX ? "" : "   commit ",
                                        want == Channel::ConquerorX ? r.Commit.substr(0, 8).c_str() : "");
                    const bool installed = std::any_of(m_Installs.begin(), m_Installs.end(), [&](const Installs::Install& x) {
                        return want == Channel::ConquerorX ? (x.Kind == Channel::ConquerorX && !x.Bundled) : (x.Version == r.Version && !x.Bundled);
                    });
                    const float bw = 110.0f * s;
                    ImGui::SameLine(ImGui::GetContentRegionMax().x - bw * 2.0f - 8.0f);
                    if (ImGui::Button("Release notes", ImVec2(bw, 0)) && !r.Url.empty())
                        OpenExternal(r.Url);
                    ImGui::SameLine();
                    if (installed || r.EditorUrl.empty())
                    {
                        ImGui::BeginDisabled();
                        ImGui::Button(installed ? "Installed" : "Not for this OS", ImVec2(bw, 0));
                        ImGui::EndDisabled();
                    }
                    else if (AccentButton("Install", ImVec2(bw, 0)))
                    {
                        m_InstallPicked = i;
                        m_PickedModules.clear();
                        for (const auto& m : r.Modules)
                            if (m.Default && !m.BuiltIn && !m.Url.empty() && Installs::ModuleAvailableOnHost(m, Installs::HostOS()))
                                m_PickedModules.insert(m.Id);
                    }
                    if (want == Channel::ConquerorX)
                        ImGui::TextDisabled("Built from every commit. When installed, the Hub updates it to the newest commit each time it starts.");
                    ImGui::Separator();
                    ImGui::PopID();
                }
                if (listed == 0)
                    ImGui::TextDisabled(want == Channel::Official ? "No official release has been published yet. See Archive to build a version yourself."
                                        : want == Channel::PreRelease ? "No pre-release has been published yet."
                                                                      : "No ConquerorX build has been published yet.");
            }
            ImGui::EndChild();
            ImGui::SetCursorPosX(ImGui::GetContentRegionMax().x - 110.0f * s);
            if (ImGui::Button("Cancel", ImVec2(110.0f * s, 0)))
                ImGui::CloseCurrentPopup();
        }
        else
        {
            const auto& r = m_Releases[(size_t)m_InstallPicked];
            ImGui::Text("Add modules to Conqueror's Engine %s", r.Version.c_str());
            ImGui::TextDisabled("Pick what you need now; you can add more later from Installs > Manage > Add modules.");
            ImGui::BeginChild("##modules", ImVec2(0, -footer));
            DrawModulePicker(r, nullptr, m_PickedModules);
            ImGui::EndChild();
            uint64_t total = r.EditorSize;
            for (const auto& m : r.Modules)
                if (m_PickedModules.count(m.Id))
                    total += m.Size;
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("Download size: %s", SizeText(total).c_str());
            const float bw = 110.0f * s;
            ImGui::SameLine(ImGui::GetContentRegionMax().x - bw * 2.0f - 8.0f);
            if (ImGui::Button("Back", ImVec2(bw, 0)))
                m_InstallPicked = -1;
            ImGui::SameLine();
            if (AccentButton("Install", ImVec2(bw, 0)))
            {
                std::string error;
                if (StartInstall(r, std::vector<std::string>(m_PickedModules.begin(), m_PickedModules.end()), error))
                {
                    m_InstallsStatus.clear();
                    ImGui::CloseCurrentPopup();
                }
                else
                {
                    m_InstallsStatus = error;
                    m_InstallsStatusError = true;
                }
            }
        }
        ImGui::EndPopup();
    }

    void ProjectHub::DrawAddModulesModal()
    {
        const float s = m_UIScale;
        if (m_OpenAddModulesModal)
        {
            ImGui::OpenPopup("Add modules##modal");
            m_OpenAddModulesModal = false;
            m_PickedModules.clear();
            m_RequirementCache.clear();
        }
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(std::min(820.0f * s, vp->Size.x - 40.0f), std::min(600.0f * s, vp->Size.y - 40.0f)), ImGuiCond_Appearing);
        if (!ImGui::BeginPopupModal("Add modules##modal", nullptr, ImGuiWindowFlags_NoSavedSettings))
            return;
        const Installs::Install* inst = m_AddModulesFor >= 0 && m_AddModulesFor < (int)m_Installs.size() ? &m_Installs[(size_t)m_AddModulesFor] : nullptr;
        const Installs::Release* release = inst ? FindRelease(*inst) : nullptr;
        const float footer = ImGui::GetFrameHeightWithSpacing() + 10.0f;
        if (!inst || !release)
        {
            ImGui::TextWrapped("This version was not published on GitHub Releases, so its modules cannot be downloaded.");
            if (ImGui::Button("Close"))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            return;
        }
        ImGui::Text("Add modules to Conqueror's Engine %s", inst->Version.c_str());
        ImGui::TextDisabled("%s", inst->Path.string().c_str());
        ImGui::BeginChild("##addmodules", ImVec2(0, -footer));
        DrawModulePicker(*release, inst, m_PickedModules);
        ImGui::EndChild();
        uint64_t total = 0;
        for (const auto& m : release->Modules)
            if (m_PickedModules.count(m.Id))
                total += m.Size;
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("Download size: %s", SizeText(total).c_str());
        const float bw = 110.0f * s;
        ImGui::SameLine(ImGui::GetContentRegionMax().x - bw * 2.0f - 8.0f);
        if (ImGui::Button("Cancel", ImVec2(bw, 0)))
            ImGui::CloseCurrentPopup();
        ImGui::SameLine();
        ImGui::BeginDisabled(m_PickedModules.empty());
        if (AccentButton("Install", ImVec2(bw, 0)))
        {
            std::string error;
            if (StartAddModules(m_AddModulesFor, std::vector<std::string>(m_PickedModules.begin(), m_PickedModules.end()), error))
                ImGui::CloseCurrentPopup();
            else
            {
                m_InstallsStatus = error;
                m_InstallsStatusError = true;
            }
        }
        ImGui::EndDisabled();
        ImGui::EndPopup();
    }

    // ------------------------------------------------------------------ otomasyon (testler)
    void ProjectHub::RegisterInstallsAutomation()
    {
        // installs refresh | releases_loaded | expect_releases <n> [official|prerelease|conquerorx] | install <sürüm|conquerorx> [m1,m2]
        //   | jobs_done | expect_installed <sürüm|conquerorx> [m1 m2 ...] | expect_not_installed <sürüm|conquerorx> | add_modules <sürüm> m1,m2
        //   | uninstall <sürüm|conquerorx> | locate <klasör> | api <url> | check_updates | expect_commit <sürüm|conquerorx> <commit>
        //   | modal open|pick <sürüm>|close | add_project <klasör> | expect_project_editor <proje adı> <yolun bir parçası> | expect_status <metin>
        Automation::RegisterCommand("installs", [this](const Automation::CommandArgs& a, std::string& err) {
            if (a.size() < 2) { err = "installs <subcommand>"; return false; }
            const std::string& c = a[1];
            auto findInstall = [&](const std::string& v) -> int {
                for (int i = 0; i < (int)m_Installs.size(); ++i)
                {
                    const auto& x = m_Installs[(size_t)i];
                    if (x.Bundled)
                        continue;
                    if (v == "conquerorx" ? x.Kind == Channel::ConquerorX : x.Version == v)
                        return i;
                }
                return -1;
            };
            auto findRelease = [&](const std::string& v) -> const Installs::Release* {
                for (const auto& r : m_Releases)
                    if (v == "conquerorx" ? r.Kind == Channel::ConquerorX : (r.Version == v && r.Kind != Channel::ConquerorX))
                        return &r;
                return nullptr;
            };
            auto split = [](const std::string& list) {
                std::vector<std::string> out;
                size_t p = 0;
                while (p <= list.size())
                {
                    const size_t q = list.find(',', p);
                    const std::string item = list.substr(p, q == std::string::npos ? std::string::npos : q - p);
                    if (!item.empty())
                        out.push_back(item);
                    if (q == std::string::npos)
                        break;
                    p = q + 1;
                }
                return out;
            };
            if (c == "refresh") { FetchReleases(true); return true; }
            if (c == "releases_loaded")
            {
                if (m_ReleasesFetch || !m_ReleasesLoaded) { err = "loading"; return false; }
                return true;
            }
            if (c == "expect_releases" && a.size() > 2)
            {
                int n = 0;
                for (const auto& r : m_Releases)
                    if (a.size() < 4 || (a[3] == "official" && r.Kind == Channel::Official) || (a[3] == "prerelease" && r.Kind == Channel::PreRelease) ||
                        (a[3] == "conquerorx" && r.Kind == Channel::ConquerorX))
                        ++n;
                Automation::Report("installs: " + std::to_string(n) + " release(s)" + (m_ReleasesError.empty() ? "" : " error: " + m_ReleasesError));
                if (n != std::atoi(a[2].c_str())) { err = std::to_string(n) + " releases" + (m_ReleasesError.empty() ? "" : " (" + m_ReleasesError + ")"); return false; }
                return true;
            }
            if (c == "install" && a.size() > 2)
            {
                const Installs::Release* r = findRelease(a[2]);
                if (!r) { err = "no release " + a[2]; return false; }
                return StartInstall(*r, a.size() > 3 ? split(a[3]) : std::vector<std::string>{}, err);
            }
            if (c == "jobs_done")
            {
                PollInstalls();
                if (!m_Jobs.empty()) { err = std::to_string(m_Jobs.size()) + " job(s) running"; return false; }
                return true;
            }
            if (c == "expect_installed" && a.size() > 2)
            {
                const int i = findInstall(a[2]);
                if (i < 0) { err = a[2] + " is not installed" + (m_InstallsStatus.empty() ? "" : " (" + m_InstallsStatus + ")"); return false; }
                const auto& x = m_Installs[(size_t)i];
                std::string mods;
                for (const auto& m : x.Modules)
                    mods += m + " ";
                Automation::Report("installs: " + x.Version + " at " + x.Path.string() + " modules: " + mods);
                if (Installs::EditorExecutable(x.Path).empty()) { err = "no editor executable in " + x.Path.string(); return false; }
                for (size_t k = 3; k < a.size(); ++k)
                    if (!Contains(x.Modules, a[k])) { err = "module " + a[k] + " missing (have: " + mods + ")"; return false; }
                return true;
            }
            if (c == "expect_not_installed" && a.size() > 2)
            {
                if (findInstall(a[2]) >= 0) { err = a[2] + " is still installed"; return false; }
                return true;
            }
            if (c == "add_modules" && a.size() > 3)
                return StartAddModules(findInstall(a[2]), split(a[3]), err);
            if (c == "uninstall" && a.size() > 2)
            {
                const int i = findInstall(a[2]);
                if (i < 0) { err = a[2] + " is not installed"; return false; }
                if (!Installs::Uninstall(ConfigDirectory(), m_Installs[(size_t)i], err))
                    return false;
                ReloadInstalls();
                return true;
            }
            if (c == "locate" && a.size() > 2)
                return LocateInstallAt(fs::path(a[2]), err);
            if (c == "api" && a.size() > 2)
            {
                Platform::SetEnvironmentVariable("CQ_RELEASES_API", a[2]);
                m_ReleasesLoaded = false;
                return true;
            }
            if (c == "check_updates") { m_ConquerorXChecked = false; FetchReleases(true); return true; }
            if (c == "expect_commit" && a.size() > 3)
            {
                const int i = findInstall(a[2]);
                if (i < 0) { err = a[2] + " is not installed"; return false; }
                Automation::Report("installs: " + a[2] + " commit " + m_Installs[(size_t)i].Commit);
                if (m_Installs[(size_t)i].Commit != a[3]) { err = "commit is " + m_Installs[(size_t)i].Commit; return false; }
                return true;
            }
            if (c == "modal" && a.size() > 2)
            {
                if (a[2] == "open") { m_Page = Page::Installs; m_OpenInstallModal = true; return true; }
                if (a[2] == "tab" && a.size() > 3) { m_InstallModalTab = std::atoi(a[3].c_str()); return true; }
                if (a[2] == "pick" && a.size() > 3)
                {
                    for (int i = 0; i < (int)m_Releases.size(); ++i)
                        if (&m_Releases[(size_t)i] == findRelease(a[3]))
                        {
                            m_InstallPicked = i;
                            m_PickedModules.clear();
                            for (const auto& m : m_Releases[(size_t)i].Modules)
                                if (m.Default && !m.BuiltIn && !m.Url.empty())
                                    m_PickedModules.insert(m.Id);
                            return true;
                        }
                    err = "no release " + a[3];
                    return false;
                }
                if (a[2] == "add_modules" && a.size() > 3) { m_Page = Page::Installs; m_AddModulesFor = findInstall(a[3]); m_OpenAddModulesModal = true; return true; }
                if (a[2] == "close") { ImGui::ClosePopupsExceptModals(); ImGui::CloseCurrentPopup(); return true; }
                err = "modal open|tab <n>|pick <version>|add_modules <version>|close";
                return false;
            }
            if (c == "add_project" && a.size() > 2)
            {
                // Testler: Unity'deki "Add" gibi (dosya penceresi olmadan)
                fs::path dir(a[2]);
                std::error_code ec;
                if (!fs::is_directory(dir, ec)) { err = "no folder " + a[2]; return false; }
                ProjectInfo p(dir.filename().string(), dir, NowString());
                m_Projects.push_back(p);
                SaveConfig();
                RefreshProjectStates();
                return true;
            }
            if (c == "expect_project_editor" && a.size() > 3)
            {
                for (const auto& p : m_Projects)
                    if (p.Name == a[2] || p.Path.filename().string() == a[2])
                    {
                        const std::string exe = EditorForProject(p).string();
                        Automation::Report("installs: project " + a[2] + " (" + p.EditorVersion + ") opens with " + exe);
                        if (exe.find(a[3]) == std::string::npos) { err = "opens with " + exe; return false; }
                        return true;
                    }
                err = "no project " + a[2];
                return false;
            }
            if (c == "expect_status" && a.size() > 2)
            {
                if (m_InstallsStatus.find(a[2]) == std::string::npos) { err = "status: " + m_InstallsStatus; return false; }
                return true;
            }
            err = "unknown installs command " + c;
            return false;
        });
    }
}
