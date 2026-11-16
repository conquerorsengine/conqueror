#include "VersionControlPanel.h"

#include "Core/Automation/Automation.h"
#include "Core/ImGui/ImGuiLayer.h"
#include "Core/Project/Project.h"
#include "VersionControl/VersionControlDetail.h"

#include <IconsFontAwesome6.h>
#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>

namespace Conqueror::Editor
{
    namespace VC = VersionControl;
    namespace fs = std::filesystem;
    using namespace std::chrono_literals;

    namespace
    {
        const ImVec4 kGreen(0.45f, 0.85f, 0.5f, 1.0f);
        const ImVec4 kRed(1.0f, 0.48f, 0.42f, 1.0f);
        const ImVec4 kYellow(0.95f, 0.8f, 0.35f, 1.0f);
        const ImVec4 kGray(0.6f, 0.62f, 0.66f, 1.0f);

        ImVec4 KindColor(char k)
        {
            switch (k)
            {
                case 'A': return ImVec4(0.36f, 0.80f, 0.45f, 1.0f);
                case 'D': return ImVec4(0.93f, 0.36f, 0.33f, 1.0f);
                case 'R': return ImVec4(0.40f, 0.68f, 0.98f, 1.0f);
                case '?': return ImVec4(0.67f, 0.67f, 0.70f, 1.0f);
                case 'U': return ImVec4(0.95f, 0.45f, 0.85f, 1.0f);
                default: return ImVec4(0.94f, 0.67f, 0.24f, 1.0f);
            }
        }

        const char* KindIcon(char k)
        {
            switch (k)
            {
                case 'A': return ICON_FA_PLUS;
                case 'D': return ICON_FA_MINUS;
                case 'R': return ICON_FA_ARROW_RIGHT_ARROW_LEFT;
                case '?': return ICON_FA_QUESTION;
                case 'U': return ICON_FA_TRIANGLE_EXCLAMATION;
                default: return ICON_FA_PEN;
            }
        }

        std::string SizeText(long long bytes)
        {
            if (bytes < 0)
                return "(none)";
            char buf[64];
            if (bytes >= 1024LL * 1024LL)
                std::snprintf(buf, sizeof(buf), "%.2f MB (%lld bytes)", bytes / (1024.0 * 1024.0), bytes);
            else if (bytes >= 1024)
                std::snprintf(buf, sizeof(buf), "%.1f KB (%lld bytes)", bytes / 1024.0, bytes);
            else
                std::snprintf(buf, sizeof(buf), "%lld bytes", bytes);
            return buf;
        }

        bool ContainsNoCase(const std::string& hay, const char* needle)
        {
            if (!needle || !*needle)
                return true;
            auto lower = [](std::string s) {
                for (char& c : s)
                    c = (char)std::tolower((unsigned char)c);
                return s;
            };
            return lower(hay).find(lower(needle)) != std::string::npos;
        }

        long long FileTime(const std::string& path)
        {
            if (path.empty())
                return 0;
            std::error_code ec;
            const auto t = fs::last_write_time(path, ec);
            if (ec)
                return -1;
            return (long long)t.time_since_epoch().count();
        }

        std::string ProjectRelative(const std::string& path)
        {
            if (!Project::GetActive())
                return path;
            fs::path p(path);
            const fs::path dir = Project::GetActiveProjectDirectory();
            if (p.is_relative())
                p = dir / p;
            std::error_code ec;
            const fs::path rel = fs::relative(p.lexically_normal(), dir, ec);
            if (ec || rel.empty() || rel.generic_string().rfind("..", 0) == 0)
                return p.string();
            return rel == "." ? std::string() : rel.generic_string();
        }

        std::string JoinArgs(const Automation::CommandArgs& a, size_t from)
        {
            std::string s;
            for (size_t i = from; i < a.size(); ++i)
                s += (i > from ? " " : "") + a[i];
            return s;
        }

        // Pencere içindeki küçük bölüm başlığı
        void Section(const char* text)
        {
            ImGui::Spacing();
            ImGui::TextDisabled("%s", text);
            ImGui::Separator();
        }
    }

    VersionControlPanel& VersionControlPanel::Get()
    {
        static VersionControlPanel s_Panel;
        return s_Panel;
    }

    // ------------------------------------------------------------------ İşler (arka plan)

    void VersionControlPanel::Submit(const std::string& label, Work work)
    {
        // İş arka planda istisna atarsa editör çökmesin: durum satırına yazılır
        Work safe = [this, w = std::move(work)]() -> Apply {
            try
            {
                return w();
            }
            catch (const std::exception& e)
            {
                const std::string what = e.what();
                return [this, what]() { SetStatus(false, what); };
            }
        };
        if (m_Sync)
        {
            WaitIdle();
            if (Apply apply = safe())
                apply();
            return;
        }
        if (m_Job.valid())
        {
            m_Queue.push_back({ label, std::move(safe) });
            return;
        }
        m_JobLabel = label;
        m_Job = std::async(std::launch::async, std::move(safe));
    }

    void VersionControlPanel::Poll()
    {
        if (m_Job.valid() && m_Job.wait_for(0ms) == std::future_status::ready)
        {
            Apply apply = m_Job.get();
            if (apply)
                apply();
        }
        if (!m_Job.valid() && !m_Queue.empty())
        {
            Job job = std::move(m_Queue.front());
            m_Queue.pop_front();
            m_JobLabel = job.Label;
            m_Job = std::async(std::launch::async, std::move(job.Run));
        }
    }

    void VersionControlPanel::WaitIdle()
    {
        while (m_Job.valid() || !m_Queue.empty())
        {
            if (m_Job.valid())
            {
                Apply apply = m_Job.get();
                if (apply)
                    apply();
                continue;
            }
            Job job = std::move(m_Queue.front());
            m_Queue.pop_front();
            if (Apply apply = job.Run())
                apply();
        }
    }

    void VersionControlPanel::SetStatus(bool ok, const std::string& text)
    {
        m_StatusOk = ok;
        m_Status = text;
        while (!m_Status.empty() && (m_Status.back() == '\n' || m_Status.back() == '\r'))
            m_Status.pop_back();
    }

    void VersionControlPanel::RefreshAll()
    {
        const VC::Context ctx = VC::CurrentContext();
        m_LastRefresh = std::chrono::steady_clock::now();
        Submit("Refreshing", [this, ctx]() -> Apply {
            std::string error;
            std::vector<VC::Change> changes = VC::ListChanges(ctx, &error);
            VC::RepoInfo info = VC::GetRepoInfo(ctx);
            std::vector<VC::Branch> branches = VC::ListBranches(ctx);
            std::vector<VC::Changelist> lists = VC::PendingChangelists(ctx);
            return [this, changes, error, info, branches, lists]() {
                m_Changes = changes;
                m_ChangesError = error;
                m_Info = info;
                m_Branches = branches;
                m_Changelists = lists;
                // Listeden çıkan dosyalar seçimden de çıkar
                std::set<std::string> present;
                for (const VC::Change& c : m_Changes)
                    present.insert(c.Path);
                for (auto it = m_Selected.begin(); it != m_Selected.end();)
                    it = present.count(*it) ? std::next(it) : m_Selected.erase(it);
                if (m_Changelist != "default" &&
                    std::none_of(m_Changelists.begin(), m_Changelists.end(), [&](const VC::Changelist& l) { return l.Number == m_Changelist; }))
                    m_Changelist = "default";
                m_LastRefresh = std::chrono::steady_clock::now();
            };
        });
    }

    void VersionControlPanel::LoadHistory()
    {
        m_HistoryLoaded = true;
        const VC::Context ctx = VC::CurrentContext();
        const std::string path = m_HistoryPath;
        const int max = m_HistoryMax;
        Submit("Loading history", [this, ctx, path, max]() -> Apply {
            std::string error;
            std::vector<VC::CommitInfo> commits = VC::Log(ctx, path, max, &error);
            return [this, commits, error, path]() {
                if (path != m_HistoryPath)
                    return;
                const std::string selected = m_SelectedCommit >= 0 && m_SelectedCommit < (int)m_Commits.size() ? m_Commits[m_SelectedCommit].Id : std::string();
                m_Commits = commits;
                m_HistoryError = error;
                m_SelectedCommit = -1;
                for (int i = 0; i < (int)m_Commits.size(); ++i)
                    if (!selected.empty() && m_Commits[i].Id == selected)
                        m_SelectedCommit = i;
                if (m_SelectedCommit < 0)
                {
                    m_CommitFiles.clear();
                    m_SelectedCommitFile = -1;
                }
            };
        });
    }

    void VersionControlPanel::LoadCommitFiles(int index)
    {
        if (index < 0 || index >= (int)m_Commits.size())
            return;
        m_SelectedCommit = index;
        m_CommitFiles.clear();
        m_CommitFilesError.clear();
        m_SelectedCommitFile = -1;
        const VC::Context ctx = VC::CurrentContext();
        const std::string id = m_Commits[index].Id;
        Submit("Loading commit", [this, ctx, id]() -> Apply {
            std::string error;
            std::vector<VC::CommitFile> files = VC::CommitFiles(ctx, id, &error);
            return [this, id, files, error]() {
                if (m_SelectedCommit < 0 || m_SelectedCommit >= (int)m_Commits.size() || m_Commits[m_SelectedCommit].Id != id)
                    return;
                m_CommitFiles = files;
                m_CommitFilesError = error;
            };
        });
    }

    void VersionControlPanel::RunOp(const std::string& label, std::function<VC::Result(const VC::Context&)> op, bool changesFiles, bool history,
                                    std::function<void(const VC::Result&)> done)
    {
        const VC::Context ctx = VC::CurrentContext();
        const std::string scene = changesFiles && CurrentScenePath ? CurrentScenePath() : std::string();
        const long long before = FileTime(scene);
        Submit(label, [this, ctx, op, scene, before, history, done]() -> Apply {
            const VC::Result r = op(ctx);
            return [this, r, scene, before, history, done]() {
                SetStatus(r.Ok, r.Output);
                m_LastOk = r.Ok;
                if (!scene.empty() && FileTime(scene) != before)
                    m_SceneChanged = true;
                if (done)
                    done(r);
                VC::Refresh(false);   // Content Browser / Inspector durum simgeleri
                RefreshAll();
                if (history && m_HistoryLoaded)
                    LoadHistory();
            };
        });
    }

    void VersionControlPanel::SetDiff(VC::Diff diff)
    {
        m_Diff = std::move(diff);
        m_SideRows.clear();
        // Yan yana satırlar: bağlam iki yanda; ardışık silinen / eklenen blokları satır satır eşlenir
        const auto& lines = m_Diff.Lines;
        size_t i = 0;
        while (i < lines.size())
        {
            const char k = lines[i].Kind;
            if (k == '@')
            {
                m_SideRows.push_back({ (int)i, (int)i, true });
                ++i;
            }
            else if (k == ' ')
            {
                m_SideRows.push_back({ (int)i, (int)i, false });
                ++i;
            }
            else
            {
                std::vector<int> minus, plus;
                while (i < lines.size() && lines[i].Kind == '-')
                    minus.push_back((int)i++);
                while (i < lines.size() && lines[i].Kind == '+')
                    plus.push_back((int)i++);
                const size_t n = std::max(minus.size(), plus.size());
                for (size_t r = 0; r < n; ++r)
                    m_SideRows.push_back({ r < minus.size() ? minus[r] : -1, r < plus.size() ? plus[r] : -1, false });
            }
        }
        m_ShowDiff = true;
        m_FocusDiff = true;
    }

    // ------------------------------------------------------------------ Dışarıdan açma

    void VersionControlPanel::Open(Tab tab)
    {
        m_IsOpen = true;
        m_RequestTab = (int)tab;
        ImGui::SetWindowFocus("###VersionControl");
    }

    void VersionControlPanel::ShowHistory(const std::string& path)
    {
        m_HistoryPath = ProjectRelative(path);
        m_SelectedCommit = -1;
        m_CommitFiles.clear();
        m_Commits.clear();
        m_HistoryFilter[0] = '\0';
        Open(Tab::History);
        LoadHistory();
    }

    void VersionControlPanel::ShowDiff(const std::string& path)
    {
        const VC::Context ctx = VC::CurrentContext();
        Submit("Diff", [this, ctx, path]() -> Apply {
            VC::Diff d = VC::DiffWorkingFile(ctx, path);
            return [this, d]() { SetDiff(d); };
        });
    }

    // ------------------------------------------------------------------ Changes işlemleri

    std::vector<VC::Change> VersionControlPanel::VisibleChanges() const
    {
        std::vector<VC::Change> v;
        for (const VC::Change& c : m_Changes)
            if (VC::Mode() != 2 || c.Changelist == m_Changelist)
                v.push_back(c);
        return v;
    }

    std::vector<VC::Change> VersionControlPanel::CheckedChanges() const
    {
        std::vector<VC::Change> v;
        for (const VC::Change& c : VisibleChanges())
            if (!m_Unchecked.count(c.Path))
                v.push_back(c);
        return v;
    }

    std::vector<VC::Change> VersionControlPanel::SelectedChanges() const
    {
        std::vector<VC::Change> v;
        for (const VC::Change& c : VisibleChanges())
            if (m_Selected.count(c.Path))
                v.push_back(c);
        return v;
    }

    void VersionControlPanel::DoCommit()
    {
        const std::vector<VC::Change> files = CheckedChanges();
        const std::string message = m_Message;
        const std::string list = m_Changelist;
        RunOp(VC::Mode() == 2 ? "Submitting" : "Committing",
              [files, message, list](const VC::Context& ctx) { return VC::Commit(ctx, files, message, list); }, false, true,
              [this](const VC::Result& r) {
                  if (r.Ok)
                  {
                      m_Message[0] = '\0';
                      m_Changelist = "default";
                  }
              });
    }

    void VersionControlPanel::DoShelve()
    {
        const std::vector<VC::Change> files = CheckedChanges();
        const std::string message = m_Message;
        const std::string list = m_Changelist;
        RunOp("Shelving", [files, message, list](const VC::Context& ctx) { return VC::Shelve(ctx, files, message, list); }, false, false);
    }

    void VersionControlPanel::DoRevertSelected()
    {
        const std::vector<VC::Change> files = SelectedChanges();
        RunOp("Reverting", [files](const VC::Context& ctx) { return VC::RevertChanges(ctx, files); }, true, false);
    }

    void VersionControlPanel::DoDiffChange(const VC::Change& change)
    {
        const VC::Context ctx = VC::CurrentContext();
        Submit("Diff", [this, ctx, change]() -> Apply {
            VC::Diff d = VC::DiffWorkingCopy(ctx, change);
            return [this, d]() { SetDiff(d); };
        });
    }

    void VersionControlPanel::DoDiffCommitFile(int fileIndex)
    {
        if (m_SelectedCommit < 0 || m_SelectedCommit >= (int)m_Commits.size() || fileIndex < 0 || fileIndex >= (int)m_CommitFiles.size())
            return;
        const VC::Context ctx = VC::CurrentContext();
        const std::string id = m_Commits[m_SelectedCommit].Id;
        const VC::CommitFile file = m_CommitFiles[fileIndex];
        Submit("Diff", [this, ctx, id, file]() -> Apply {
            VC::Diff d = VC::DiffInCommit(ctx, id, file);
            return [this, d]() { SetDiff(d); };
        });
    }

    void VersionControlPanel::DoSwitch(const VC::Branch& branch)
    {
        RunOp("Switching branch", [branch](const VC::Context& ctx) { return VC::SwitchBranch(ctx, branch); }, true, true);
    }

    void VersionControlPanel::DoDelete(const std::string& name, bool force)
    {
        RunOp("Deleting branch", [name, force](const VC::Context& ctx) { return VC::DeleteBranch(ctx, name, force); }, false, false,
              [this, name](const VC::Result& r) {
                  if (r.NeedsForce)
                  {
                      m_DeleteBranch = name;
                      m_DeleteNeedsForce = true;
                      m_OpenDeletePopup = true;
                  }
              });
    }

    // ------------------------------------------------------------------ Çizim

    void VersionControlPanel::OnImGuiRender()
    {
        Poll();
        const std::string key = (Project::GetActive() ? Project::GetActiveProjectDirectory().string() : std::string()) + "|" + std::to_string(VC::Mode());
        if (key != m_ContextKey)
        {
            // Başka proje ya da kip: eski sonuçlar atılır
            m_ContextKey = key;
            m_Changes.clear();
            m_Selected.clear();
            m_Unchecked.clear();
            m_Commits.clear();
            m_CommitFiles.clear();
            m_SelectedCommit = -1;
            m_HistoryLoaded = false;
            m_HistoryPath.clear();
            m_Info = {};
            m_Branches.clear();
            m_Changelists.clear();
            m_Changelist = "default";
            m_Status.clear();
            m_SceneChanged = false;
            m_LastRefresh = {};
        }
        if (m_ShowDiff)
            DrawDiffWindow();
        if (!m_IsOpen)
            return;
        if (VC::IsEnabled() && !Busy() && std::chrono::steady_clock::now() - m_LastRefresh > 5s)
            RefreshAll();

        ImGui::SetNextWindowSize(ImVec2(880, 580), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin(ICON_FA_CODE_BRANCH "  Version Control###VersionControl", &m_IsOpen))
        {
            ImGui::End();
            return;
        }
        DrawToolbar();
        if (!VC::IsEnabled())
        {
            ImGui::End();
            return;
        }
        if (m_SceneChanged)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, kYellow);
            ImGui::TextUnformatted(ICON_FA_TRIANGLE_EXCLAMATION "  The open scene was changed on disk.");
            ImGui::PopStyleColor();
            ImGui::SameLine();
            if (ImGui::SmallButton("Reload Scene"))
            {
                if (ReloadScene)
                    ReloadScene();
                m_SceneChanged = false;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Keep Editor Version"))
                m_SceneChanged = false;
        }
        if (!m_Status.empty())
        {
            const int lines = 1 + (int)std::count(m_Status.begin(), m_Status.end(), '\n');
            const float h = std::min(lines, 5) * ImGui::GetTextLineHeightWithSpacing() + ImGui::GetStyle().WindowPadding.y * 2.0f;
            ImGui::BeginChild("##vcsstatus", ImVec2(0, h), ImGuiChildFlags_Borders);
            if (ImGui::SmallButton(ICON_FA_XMARK "##clearstatus"))
                m_Status.clear();
            ImGui::SameLine();
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextColored(m_StatusOk ? kGreen : kRed, "%s", m_Status.c_str());
            ImGui::PopTextWrapPos();
            ImGui::EndChild();
        }

        if (ImGui::BeginTabBar("##vcstabs"))
        {
            auto flags = [this](Tab t) { return m_RequestTab == (int)t ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None; };
            const std::string changesLabel = std::string(ICON_FA_PEN "  Changes") + (m_Changes.empty() ? std::string() : " (" + std::to_string(VisibleChanges().size()) + ")") +
                                             "###vcschanges";
            if (ImGui::BeginTabItem(changesLabel.c_str(), nullptr, flags(Tab::Changes)))
            {
                DrawChanges();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem(ICON_FA_CLOCK_ROTATE_LEFT "  History###vcshistory", nullptr, flags(Tab::History)))
            {
                if (!m_HistoryLoaded)
                    LoadHistory();
                DrawHistory();
                ImGui::EndTabItem();
            }
            const char* syncLabel = VC::Mode() == 2 ? ICON_FA_CLOUD_ARROW_DOWN "  Sync###vcssync" : ICON_FA_CODE_BRANCH "  Branches & Sync###vcssync";
            if (ImGui::BeginTabItem(syncLabel, nullptr, flags(Tab::Sync)))
            {
                DrawSync();
                ImGui::EndTabItem();
            }
            m_RequestTab = -1;
            ImGui::EndTabBar();
        }
        ImGui::End();
    }

    void VersionControlPanel::DrawToolbar()
    {
        if (!VC::IsEnabled())
        {
            ImGui::TextDisabled(ICON_FA_CODE_BRANCH "  Version control is off");
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted("Choose Git or Perforce in Project Settings > Version Control. The editor then runs the git or p4 "
                                   "command-line tools: changes and commits, history, diffs, pull / push and branches appear here.");
            ImGui::PopTextWrapPos();
            return;
        }
        const bool busy = Busy();
        if (VC::Mode() == 1)
        {
            if (!m_Info.IsRepo && !m_Info.Error.empty())
            {
                ImGui::TextColored(kRed, "%s", m_Info.Error.c_str());
                if (m_Info.Error.find("not inside a Git repository") != std::string::npos)
                {
                    ImGui::SameLine();
                    if (ImGui::Button(ICON_FA_PLUS "  Initialize Repository"))
                    {
                        std::string message;
                        const bool ok = VC::InitGitRepository(&message);
                        SetStatus(ok, ok ? std::string("Created a Git repository with an initial commit of the project.") : message);
                        RefreshAll();
                    }
                }
            }
            else
            {
                ImGui::AlignTextToFramePadding();
                ImGui::Text(ICON_FA_CODE_BRANCH "  %s%s", m_Info.Branch.empty() ? "..." : m_Info.Branch.c_str(), m_Info.Detached ? " (detached)" : "");
                if (!m_Info.Upstream.empty())
                {
                    ImGui::SameLine();
                    ImGui::TextDisabled("-> %s", m_Info.Upstream.c_str());
                    ImGui::SameLine();
                    ImGui::TextColored(m_Info.Ahead ? kYellow : kGray, ICON_FA_ARROW_UP " %d", m_Info.Ahead);
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("%d commit(s) to push", m_Info.Ahead);
                    ImGui::SameLine();
                    ImGui::TextColored(m_Info.Behind ? kYellow : kGray, ICON_FA_ARROW_DOWN " %d", m_Info.Behind);
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("%d commit(s) to pull (after Fetch)", m_Info.Behind);
                }
                else if (!m_Info.Branch.empty())
                {
                    ImGui::SameLine();
                    ImGui::TextDisabled(m_Info.Remotes.empty() ? "(no remote)" : "(not pushed yet)");
                }
                if (!m_Info.Operation.empty())
                {
                    ImGui::SameLine();
                    ImGui::TextColored(kRed, ICON_FA_TRIANGLE_EXCLAMATION " %s in progress", m_Info.Operation.c_str());
                }
                ImGui::SameLine();
                ImGui::BeginDisabled(busy);
                if (ImGui::Button(ICON_FA_DOWNLOAD "  Fetch"))
                    RunOp("Fetching", [](const VC::Context& c) { return VC::Fetch(c); }, false, false);
                ImGui::SameLine();
                if (ImGui::Button(ICON_FA_CLOUD_ARROW_DOWN "  Pull"))
                {
                    const bool rebase = m_PullRebase;
                    RunOp("Pulling", [rebase](const VC::Context& c) { return VC::Pull(c, rebase); }, true, true);
                }
                if (ImGui::IsItemHovered())
                    ImGui::SetTooltip("git pull --%s (change it in Branches & Sync); local changes are stashed and put back", m_PullRebase ? "rebase" : "no-rebase (merge)");
                ImGui::SameLine();
                if (ImGui::Button(ICON_FA_CLOUD_ARROW_UP "  Push"))
                    RunOp("Pushing", [](const VC::Context& c) { return VC::Push(c); }, false, false);
                ImGui::EndDisabled();
            }
        }
        else
        {
            ImGui::AlignTextToFramePadding();
            if (!m_Info.IsRepo && !m_Info.Error.empty())
                ImGui::TextColored(kRed, "%s", m_Info.Error.c_str());
            else
            {
                ImGui::Text(ICON_FA_SERVER "  %s", m_Info.Workspace.empty() ? "..." : m_Info.Workspace.c_str());
                if (!m_Info.Stream.empty())
                {
                    ImGui::SameLine();
                    ImGui::TextDisabled("stream %s", m_Info.Stream.c_str());
                }
                if (!m_Info.Server.empty())
                {
                    ImGui::SameLine();
                    ImGui::TextDisabled("@ %s", m_Info.Server.c_str());
                }
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(busy);
            if (ImGui::Button(ICON_FA_CLOUD_ARROW_DOWN "  Get Latest"))
                RunOp("Getting latest", [](const VC::Context& c) { return VC::GetLatest(c); }, true, true);
            ImGui::EndDisabled();
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(busy);
        if (ImGui::Button(ICON_FA_ARROWS_ROTATE "  Refresh"))
        {
            RefreshAll();
            if (m_HistoryLoaded)
                LoadHistory();
            VC::Refresh(false);
        }
        ImGui::EndDisabled();
        if (busy)
        {
            ImGui::SameLine();
            ImGui::TextColored(kYellow, ICON_FA_SPINNER "  %s...", m_JobLabel.c_str());
        }
    }

    void VersionControlPanel::DrawChanges()
    {
        const bool perforce = VC::Mode() == 2;
        const bool busy = Busy();
        if (perforce)
        {
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted("Changelist");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(320.0f);
            std::string preview = m_Changelist == "default" ? std::string("default") : m_Changelist;
            for (const auto& l : m_Changelists)
                if (l.Number == m_Changelist)
                    preview += "  " + l.Description.substr(0, l.Description.find('\n'));
            if (ImGui::BeginCombo("##changelist", preview.c_str()))
            {
                if (ImGui::Selectable("default", m_Changelist == "default"))
                    m_Changelist = "default";
                for (const auto& l : m_Changelists)
                {
                    const std::string label = l.Number + "  " + l.Description.substr(0, l.Description.find('\n'));
                    if (ImGui::Selectable(label.c_str(), m_Changelist == l.Number))
                    {
                        m_Changelist = l.Number;
                        std::snprintf(m_Message, sizeof(m_Message), "%s", l.Description.c_str());
                    }
                }
                ImGui::EndCombo();
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Submit the default changelist (a numbered one is made for the checked files) or a pending numbered changelist");
        }
        const std::vector<VC::Change> visible = VisibleChanges();
        const float bottom = ImGui::GetFrameHeightWithSpacing() * 1.2f + 92.0f + ImGui::GetTextLineHeightWithSpacing();
        const ImGuiTableFlags tableFlags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable;
        if (ImGui::BeginTable("##changes", perforce ? 5 : 4, tableFlags, ImVec2(0, std::max(80.0f, ImGui::GetContentRegionAvail().y - bottom))))
        {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("##check", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize, ImGui::GetFrameHeight());
            ImGui::TableSetupColumn("##icon", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize, ImGui::GetFontSize() * 1.3f);
            ImGui::TableSetupColumn("Path", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 110.0f);
            if (perforce)
                ImGui::TableSetupColumn("Changelist", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            // Başlık: ilk sütunda hepsini seç kutusu
            ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
            ImGui::TableSetColumnIndex(0);
            bool all = !visible.empty() && std::none_of(visible.begin(), visible.end(), [&](const VC::Change& c) { return m_Unchecked.count(c.Path) > 0; });
            if (ImGui::Checkbox("##all", &all))
                for (const VC::Change& c : visible)
                {
                    if (all)
                        m_Unchecked.erase(c.Path);
                    else
                        m_Unchecked.insert(c.Path);
                }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Include all / none");
            for (int col = 1; col < (perforce ? 5 : 4); ++col)
            {
                ImGui::TableSetColumnIndex(col);
                ImGui::TableHeader(ImGui::TableGetColumnName(col));
            }
            int index = 0;
            for (const VC::Change& c : visible)
            {
                ImGui::PushID(c.Path.c_str());
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                const bool selected = m_Selected.count(c.Path) > 0;
                const ImVec2 cell = ImGui::GetCursorPos();
                if (ImGui::Selectable("##row", selected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap | ImGuiSelectableFlags_AllowDoubleClick,
                                      ImVec2(0, ImGui::GetFrameHeight())))
                {
                    const ImGuiIO& io = ImGui::GetIO();
                    if (io.KeyShift && !m_LastClicked.empty())
                    {
                        // Aralık seçimi (son tıklanandan buna)
                        int a = -1, b = index;
                        for (int k = 0; k < (int)visible.size(); ++k)
                            if (visible[k].Path == m_LastClicked)
                                a = k;
                        if (a < 0)
                            a = index;
                        if (!io.KeyCtrl)
                            m_Selected.clear();
                        for (int k = std::min(a, b); k <= std::max(a, b); ++k)
                            m_Selected.insert(visible[k].Path);
                    }
                    else if (io.KeyCtrl)
                    {
                        if (selected)
                            m_Selected.erase(c.Path);
                        else
                            m_Selected.insert(c.Path);
                        m_LastClicked = c.Path;
                    }
                    else
                    {
                        m_Selected = { c.Path };
                        m_LastClicked = c.Path;
                    }
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                        DoDiffChange(c);
                }
                if (ImGui::BeginPopupContextItem("##changectx"))
                {
                    if (!m_Selected.count(c.Path))
                        m_Selected = { c.Path };
                    if (ImGui::MenuItem(ICON_FA_CODE_COMPARE "  Diff"))
                        DoDiffChange(c);
                    if (ImGui::MenuItem(ICON_FA_CLOCK_ROTATE_LEFT "  Show History", nullptr, false, c.Kind != '?' && c.Kind != 'A'))
                        ShowHistory(c.Path);
                    ImGui::Separator();
                    if (ImGui::MenuItem(ICON_FA_ROTATE_LEFT "  Revert...", nullptr, false, !busy))
                        m_ConfirmRevert = true;
                    ImGui::EndPopup();
                }
                ImGui::SetCursorPos(cell);   // kutu satır seçiminin üstünde (AllowOverlap: tıklama kutuya gider)
                bool include = !m_Unchecked.count(c.Path);
                if (ImGui::Checkbox("##inc", &include))
                {
                    if (include)
                        m_Unchecked.erase(c.Path);
                    else
                        m_Unchecked.insert(c.Path);
                }
                ImGui::TableSetColumnIndex(1);
                ImGui::AlignTextToFramePadding();
                ImGui::TextColored(KindColor(c.Kind), "%s", KindIcon(c.Kind));
                ImGui::TableSetColumnIndex(2);
                ImGui::AlignTextToFramePadding();
                if (c.Kind == 'R' && !c.OldRepoPath.empty())
                    ImGui::Text("%s  <-  %s", c.Path.c_str(), fs::path(c.OldRepoPath).filename().string().c_str());
                else
                    ImGui::TextUnformatted(c.Path.c_str());
                ImGui::TableSetColumnIndex(3);
                ImGui::AlignTextToFramePadding();
                ImGui::TextColored(KindColor(c.Kind), "%s", VC::ChangeKindName(c.Kind));
                if (perforce)
                {
                    ImGui::TableSetColumnIndex(4);
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextDisabled("%s", c.Changelist.c_str());
                }
                ImGui::PopID();
                ++index;
            }
            ImGui::EndTable();
        }
        if (visible.empty())
            ImGui::TextDisabled("%s", !m_ChangesError.empty() ? m_ChangesError.c_str()
                                                              : perforce ? "No checked-out files in this changelist (Check Out / Add in the Inspector)."
                                                                         : "No changes: the working copy matches the last commit.");
        else
            ImGui::TextDisabled("%d file(s), %d checked, %d selected. Double-click a file for its diff.", (int)visible.size(), (int)CheckedChanges().size(),
                                (int)SelectedChanges().size());

        ImGui::InputTextMultiline("##message", m_Message, sizeof(m_Message), ImVec2(-1, 70));
        if (!m_Message[0] && !ImGui::IsItemActive())
        {
            // Yer tutucu (çok satırlı kutunun ipucu yok)
            const ImVec2 min = ImGui::GetItemRectMin();
            ImGui::GetWindowDrawList()->AddText(ImVec2(min.x + 6, min.y + 4), ImGui::GetColorU32(ImGuiCol_TextDisabled),
                                                perforce ? "Changelist description" : "Commit message");
        }
        const int checked = (int)CheckedChanges().size();
        const int selectedCount = (int)SelectedChanges().size();
        ImGui::BeginDisabled(busy || checked == 0 || !m_Message[0]);
        const std::string commitLabel = std::string(perforce ? ICON_FA_CLOUD_ARROW_UP "  Submit" : ICON_FA_CODE_COMMIT "  Commit") + " " + std::to_string(checked) +
                                        (checked == 1 ? " file" : " files");
        if (ImGui::Button(commitLabel.c_str()))
            DoCommit();
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip(perforce ? "p4 submit of the checked files with this description" : "git add + git commit of the checked files with this message");
        if (perforce)
        {
            ImGui::SameLine();
            ImGui::BeginDisabled(busy || checked == 0 || !m_Message[0]);
            if (ImGui::Button(ICON_FA_BOX_ARCHIVE "  Shelve"))
                DoShelve();
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("p4 shelve: stores the checked files on the server in a numbered changelist; they stay checked out");
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(busy || selectedCount == 0);
        if (ImGui::Button((std::string(ICON_FA_ROTATE_LEFT "  Revert Selected (") + std::to_string(selectedCount) + ")").c_str()))
            m_ConfirmRevert = true;
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_CODE_COMPARE "  Diff") && selectedCount > 0)
            DoDiffChange(SelectedChanges().front());
        ImGui::EndDisabled();

        if (m_ConfirmRevert)
        {
            ImGui::OpenPopup("Revert files?##vcs");
            m_ConfirmRevert = false;
        }
        if (ImGui::BeginPopupModal("Revert files?##vcs", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            const auto sel = SelectedChanges();
            ImGui::Text("Revert %d file(s)? Their local changes are lost.", (int)sel.size());
            for (size_t i = 0; i < sel.size() && i < 8; ++i)
                ImGui::TextColored(KindColor(sel[i].Kind), "  %s %s", KindIcon(sel[i].Kind), sel[i].Path.c_str());
            if (sel.size() > 8)
                ImGui::TextDisabled("  ... and %d more", (int)sel.size() - 8);
            if (!perforce)
                ImGui::TextDisabled("Added files are only un-added (kept); files that are not versioned are left alone.");
            if (ImGui::Button("Revert", ImVec2(120, 0)))
            {
                DoRevertSelected();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0)))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }

    void VersionControlPanel::DrawHistory()
    {
        const bool perforce = VC::Mode() == 2;
        ImGui::AlignTextToFramePadding();
        if (m_HistoryPath.empty())
            ImGui::TextUnformatted(ICON_FA_LAYER_GROUP "  Project history");
        else
        {
            ImGui::Text(ICON_FA_FILE_LINES "  History of %s", m_HistoryPath.c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("Show Project History"))
            {
                m_HistoryPath.clear();
                m_SelectedCommit = -1;
                m_CommitFiles.clear();
                LoadHistory();
            }
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(220.0f);
        ImGui::InputTextWithHint("##historyfilter", ICON_FA_MAGNIFYING_GLASS " Filter (message, author, id)", m_HistoryFilter, sizeof(m_HistoryFilter));
        ImGui::SameLine();
        ImGui::TextDisabled("%d commit(s)", (int)m_Commits.size());
        if ((int)m_Commits.size() >= m_HistoryMax)
        {
            ImGui::SameLine();
            if (ImGui::SmallButton("Load More"))
            {
                m_HistoryMax *= 2;
                LoadHistory();
            }
        }
        if (!m_HistoryError.empty())
            ImGui::TextColored(kRed, "%s", m_HistoryError.c_str());

        const float listHeight = std::max(100.0f, ImGui::GetContentRegionAvail().y * 0.55f);
        const ImGuiTableFlags flags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable;
        if (ImGui::BeginTable("##commits", 4, flags, ImVec2(0, listHeight)))
        {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn(perforce ? "Change" : "Commit", ImGuiTableColumnFlags_WidthFixed, perforce ? 90.0f : 75.0f);
            ImGui::TableSetupColumn("Author", ImGuiTableColumnFlags_WidthFixed, 130.0f);
            ImGui::TableSetupColumn("Date", ImGuiTableColumnFlags_WidthFixed, 120.0f);
            ImGui::TableSetupColumn("Message", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();
            for (int i = 0; i < (int)m_Commits.size(); ++i)
            {
                const VC::CommitInfo& c = m_Commits[i];
                if (m_HistoryFilter[0] && !ContainsNoCase(c.Message, m_HistoryFilter) && !ContainsNoCase(c.Author, m_HistoryFilter) &&
                    !ContainsNoCase(c.Id, m_HistoryFilter))
                    continue;
                ImGui::PushID(i);
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                const std::string id = c.FileRevision.empty() ? c.ShortId : c.ShortId + " #" + c.FileRevision;
                if (ImGui::Selectable(id.c_str(), m_SelectedCommit == i, ImGuiSelectableFlags_SpanAllColumns))
                    LoadCommitFiles(i);
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(c.Author.c_str());
                ImGui::TableSetColumnIndex(2);
                ImGui::TextDisabled("%s", c.Date.c_str());
                ImGui::TableSetColumnIndex(3);
                ImGui::TextUnformatted(c.Summary.c_str());
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        if (m_Commits.empty() && m_HistoryError.empty())
            ImGui::TextDisabled(Busy() ? "Loading..." : "No commits yet.");

        // Seçili kaydın ayrıntısı ve dosyaları
        ImGui::BeginChild("##commitdetail", ImVec2(0, 0), ImGuiChildFlags_Borders);
        if (m_SelectedCommit >= 0 && m_SelectedCommit < (int)m_Commits.size())
        {
            const VC::CommitInfo& c = m_Commits[m_SelectedCommit];
            ImGui::TextDisabled("%s %s", perforce ? "Change" : "Commit", c.Id.c_str());
            ImGui::TextDisabled("%s%s%s%s  %s", c.Author.c_str(), c.Email.empty() ? "" : (perforce ? " @ " : " <"), c.Email.c_str(),
                                c.Email.empty() || perforce ? "" : ">", c.Date.c_str());
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted(c.Message.c_str());
            ImGui::PopTextWrapPos();
            ImGui::Separator();
            if (!m_CommitFilesError.empty())
                ImGui::TextColored(kRed, "%s", m_CommitFilesError.c_str());
            ImGui::TextDisabled("%d file(s); double-click one for the change it made", (int)m_CommitFiles.size());
            for (int i = 0; i < (int)m_CommitFiles.size(); ++i)
            {
                const VC::CommitFile& f = m_CommitFiles[i];
                ImGui::PushID(i);
                ImGui::TextColored(KindColor(f.Kind), "%s", KindIcon(f.Kind));
                ImGui::SameLine();
                std::string label = f.Path;
                if (!f.Revision.empty())
                    label += "#" + f.Revision;
                if (!f.OldRepoPath.empty())
                    label += "  <-  " + f.OldRepoPath;
                if (ImGui::Selectable(label.c_str(), m_SelectedCommitFile == i, ImGuiSelectableFlags_AllowDoubleClick))
                {
                    m_SelectedCommitFile = i;
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                        DoDiffCommitFile(i);
                }
                if (ImGui::BeginPopupContextItem("##filectx"))
                {
                    if (ImGui::MenuItem(ICON_FA_CODE_COMPARE "  Diff"))
                        DoDiffCommitFile(i);
                    if (!perforce && f.Path.rfind("/", 0) != 0 && ImGui::MenuItem(ICON_FA_CLOCK_ROTATE_LEFT "  Show History"))
                        ShowHistory(f.Path);
                    ImGui::EndPopup();
                }
                ImGui::PopID();
            }
        }
        else
            ImGui::TextDisabled("Select a commit to see its message and files.");
        ImGui::EndChild();
    }

    void VersionControlPanel::DrawSync()
    {
        const bool busy = Busy();
        if (VC::Mode() == 2)
        {
            Section("Workspace");
            if (ImGui::BeginTable("##p4info", 2, ImGuiTableFlags_SizingFixedFit))
            {
                auto row = [](const char* k, const std::string& v) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextDisabled("%s", k);
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextUnformatted(v.empty() ? "-" : v.c_str());
                };
                row("Workspace", m_Info.Workspace);
                row("Stream", m_Info.Stream.empty() ? std::string("(classic workspace, no stream)") : m_Info.Stream);
                row("Root", m_Info.Root);
                row("User", m_Info.User);
                row("Server", m_Info.Server);
                ImGui::EndTable();
            }
            ImGui::Spacing();
            ImGui::BeginDisabled(busy);
            if (ImGui::Button(ICON_FA_CLOUD_ARROW_DOWN "  Get Latest"))
                RunOp("Getting latest", [](const VC::Context& c) { return VC::GetLatest(c); }, true, true);
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::TextDisabled("p4 sync of the project folder");
            Section("Pending changelists");
            if (m_Changelists.empty())
                ImGui::TextDisabled("Only the default changelist.");
            for (const auto& l : m_Changelists)
                ImGui::Text("%s  %s", l.Number.c_str(), l.Description.substr(0, l.Description.find('\n')).c_str());
            return;
        }

        // ---- Git: geçerli dal, eşitleme
        Section("Current branch");
        ImGui::Text(ICON_FA_CODE_BRANCH "  %s", m_Info.Branch.c_str());
        ImGui::SameLine();
        if (!m_Info.Upstream.empty())
            ImGui::TextDisabled("tracks %s: %d to push, %d to pull", m_Info.Upstream.c_str(), m_Info.Ahead, m_Info.Behind);
        else
            ImGui::TextDisabled(m_Info.Remotes.empty() ? "no remote: add one below to push" : "no upstream yet: Push sets it");
        ImGui::BeginDisabled(busy);
        if (ImGui::Button(ICON_FA_DOWNLOAD "  Fetch"))
            RunOp("Fetching", [](const VC::Context& c) { return VC::Fetch(c); }, false, false);
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_CLOUD_ARROW_DOWN "  Pull"))
        {
            const bool rebase = m_PullRebase;
            RunOp("Pulling", [rebase](const VC::Context& c) { return VC::Pull(c, rebase); }, true, true);
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Rebase", m_PullRebase))
            m_PullRebase = true;
        ImGui::SameLine();
        if (ImGui::RadioButton("Merge", !m_PullRebase))
            m_PullRebase = false;
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_CLOUD_ARROW_UP "  Push"))
            RunOp("Pushing", [](const VC::Context& c) { return VC::Push(c); }, false, false);
        ImGui::EndDisabled();

        // ---- Süren rebase / merge: çakışmalar
        if (!m_Info.Operation.empty())
        {
            ImGui::Spacing();
            ImGui::TextColored(kRed, ICON_FA_TRIANGLE_EXCLAMATION "  %s in progress: %d conflicted file(s)", m_Info.Operation.c_str(), (int)m_Info.Conflicts.size());
            ImGui::TextDisabled(m_Info.Operation == "rebase" ? "Mine = your commit being replayed, Theirs = the pulled branch."
                                                             : "Mine = your branch, Theirs = the branch being merged in.");
            if (ImGui::BeginTable("##conflicts", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV))
            {
                ImGui::TableSetupColumn("File", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("##actions", ImGuiTableColumnFlags_WidthFixed, 420.0f);
                for (const std::string& f : m_Info.Conflicts)
                {
                    ImGui::PushID(f.c_str());
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextColored(KindColor('U'), ICON_FA_TRIANGLE_EXCLAMATION "  %s", f.c_str());
                    ImGui::TableSetColumnIndex(1);
                    ImGui::BeginDisabled(busy);
                    if (ImGui::SmallButton(ICON_FA_CODE_COMPARE " Diff"))
                    {
                        for (const VC::Change& c : m_Changes)
                            if (c.RepoPath == f)
                                DoDiffChange(c);
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Use Mine"))
                        RunOp("Resolving", [f](const VC::Context& c) { return VC::ResolveConflict(c, f, VC::Resolve::Mine); }, true, false);
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Use Theirs"))
                        RunOp("Resolving", [f](const VC::Context& c) { return VC::ResolveConflict(c, f, VC::Resolve::Theirs); }, true, false);
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Mark Resolved"))
                        RunOp("Resolving", [f](const VC::Context& c) { return VC::ResolveConflict(c, f, VC::Resolve::MarkResolved); }, true, false);
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("You edited the file and removed the conflict markers yourself");
                    ImGui::EndDisabled();
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
            ImGui::BeginDisabled(busy);
            if (ImGui::Button(ICON_FA_CHECK "  Continue"))
                RunOp("Continuing", [](const VC::Context& c) { return VC::ContinueOperation(c); }, true, true);
            ImGui::SameLine();
            if (ImGui::Button(ICON_FA_XMARK "  Abort"))
                RunOp("Aborting", [](const VC::Context& c) { return VC::AbortOperation(c); }, true, true);
            ImGui::EndDisabled();
        }

        // ---- Dallar
        Section("Branches");
        const float tableHeight = std::max(90.0f, ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeightWithSpacing() * 4.5f);
        if (ImGui::BeginTable("##branches", 4, ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable,
                              ImVec2(0, tableHeight)))
        {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("Branch", ImGuiTableColumnFlags_WidthFixed, 200.0f);
            ImGui::TableSetupColumn("Upstream", ImGuiTableColumnFlags_WidthFixed, 190.0f);
            ImGui::TableSetupColumn("Last commit", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("##actions", ImGuiTableColumnFlags_WidthFixed, 130.0f);
            ImGui::TableHeadersRow();
            for (const VC::Branch& b : m_Branches)
            {
                ImGui::PushID(b.Name.c_str());
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::AlignTextToFramePadding();
                if (b.Current)
                    ImGui::TextColored(kGreen, ICON_FA_CHECK "  %s", b.Name.c_str());
                else
                    ImGui::Text("%s  %s", b.Remote ? ICON_FA_CLOUD : ICON_FA_CODE_BRANCH, b.Name.c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::AlignTextToFramePadding();
                ImGui::TextDisabled("%s %s", b.Upstream.c_str(), b.Track.c_str());
                ImGui::TableSetColumnIndex(2);
                ImGui::AlignTextToFramePadding();
                ImGui::TextDisabled("%s", b.Date.c_str());
                ImGui::SameLine();
                ImGui::TextUnformatted(b.Subject.c_str());
                ImGui::TableSetColumnIndex(3);
                ImGui::BeginDisabled(busy || b.Current);
                if (ImGui::SmallButton("Switch"))
                    DoSwitch(b);
                ImGui::EndDisabled();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                    ImGui::SetTooltip(b.Current ? "Current branch"
                                                : m_Info.UncommittedTracked > 0 ? "You have uncommitted changes: commit or revert them first"
                                                                                : b.Remote ? "Creates a local branch tracking it and switches to it"
                                                                                           : "git switch");
                if (!b.Remote)
                {
                    ImGui::SameLine();
                    ImGui::BeginDisabled(busy || b.Current);
                    if (ImGui::SmallButton("Delete"))
                    {
                        m_DeleteBranch = b.Name;
                        m_DeleteNeedsForce = false;
                        m_OpenDeletePopup = true;
                    }
                    ImGui::EndDisabled();
                }
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        ImGui::SetNextItemWidth(220.0f);
        const bool enter = ImGui::InputTextWithHint("##newbranch", "new branch name", m_NewBranch, sizeof(m_NewBranch), ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        ImGui::Checkbox("Switch to it", &m_SwitchToNew);
        ImGui::SameLine();
        ImGui::BeginDisabled(busy || !m_NewBranch[0]);
        if (ImGui::Button(ICON_FA_PLUS "  Create Branch") || (enter && m_NewBranch[0]))
        {
            const std::string name = m_NewBranch;
            const bool sw = m_SwitchToNew;
            RunOp("Creating branch", [name, sw](const VC::Context& c) { return VC::CreateBranch(c, name, sw); }, false, true, [this](const VC::Result& r) {
                if (r.Ok)
                    m_NewBranch[0] = '\0';
            });
        }
        ImGui::EndDisabled();

        // ---- Uzak depolar
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("Remotes:");
        ImGui::SameLine();
        std::string remotes;
        for (const std::string& r : m_Info.Remotes)
            remotes += (remotes.empty() ? "" : ", ") + r;
        ImGui::TextUnformatted(remotes.empty() ? "(none)" : remotes.c_str());
        ImGui::SameLine();
        ImGui::SetNextItemWidth(90.0f);
        ImGui::InputTextWithHint("##remotename", "name", m_RemoteName, sizeof(m_RemoteName));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(std::max(120.0f, ImGui::GetContentRegionAvail().x - 130.0f));
        ImGui::InputTextWithHint("##remoteurl", "URL (https://... or git@...)", m_RemoteUrl, sizeof(m_RemoteUrl));
        ImGui::SameLine();
        ImGui::BeginDisabled(busy || !m_RemoteName[0] || !m_RemoteUrl[0]);
        if (ImGui::Button(ICON_FA_PLUS "  Add Remote"))
        {
            const std::string name = m_RemoteName, url = m_RemoteUrl;
            RunOp("Adding remote", [name, url](const VC::Context& c) { return VC::AddRemote(c, name, url); }, false, false, [this](const VC::Result& r) {
                if (r.Ok)
                    m_RemoteUrl[0] = '\0';
            });
        }
        ImGui::EndDisabled();

        if (m_OpenDeletePopup)
        {
            ImGui::OpenPopup("Delete branch?##vcs");
            m_OpenDeletePopup = false;
        }
        if (ImGui::BeginPopupModal("Delete branch?##vcs", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            if (m_DeleteNeedsForce)
            {
                ImGui::TextColored(kYellow, "Branch %s has commits that are not merged into the current branch.", m_DeleteBranch.c_str());
                ImGui::TextUnformatted("Force Delete removes it anyway; those commits are lost unless another branch contains them.");
                if (ImGui::Button("Force Delete", ImVec2(130, 0)))
                {
                    DoDelete(m_DeleteBranch, true);
                    ImGui::CloseCurrentPopup();
                }
            }
            else
            {
                ImGui::Text("Delete the local branch %s?", m_DeleteBranch.c_str());
                if (ImGui::Button("Delete", ImVec2(130, 0)))
                {
                    DoDelete(m_DeleteBranch, false);
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(130, 0)))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
    }

    void VersionControlPanel::DrawDiffWindow()
    {
        ImGui::SetNextWindowSize(ImVec2(960, 620), ImGuiCond_FirstUseEver);
        if (m_FocusDiff)
        {
            ImGui::SetNextWindowFocus();
            m_FocusDiff = false;
        }
        if (!ImGui::Begin(ICON_FA_CODE_COMPARE "  Diff###VersionControlDiff", &m_ShowDiff))
        {
            ImGui::End();
            return;
        }
        const VC::Diff& d = m_Diff;
        ImGui::Text(ICON_FA_FILE_LINES "  %s", d.Title.c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("%s  ->  %s", d.OldLabel.c_str(), d.NewLabel.c_str());
        if (!d.Binary && !d.Identical && d.Ok)
        {
            ImGui::SameLine();
            ImGui::TextColored(KindColor('A'), "+%d", d.Added);
            ImGui::SameLine();
            ImGui::TextColored(KindColor('D'), "-%d", d.Removed);
            ImGui::SameLine(0.0f, 24.0f);
            if (ImGui::RadioButton("Side by side", m_SideBySide))
                m_SideBySide = true;
            ImGui::SameLine();
            if (ImGui::RadioButton("Unified", !m_SideBySide))
                m_SideBySide = false;
        }
        ImGui::Separator();
        if (!d.Error.empty())
        {
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextColored(kRed, "%s", d.Error.c_str());
            ImGui::PopTextWrapPos();
            ImGui::End();
            return;
        }
        if (d.Binary)
        {
            ImGui::TextUnformatted(ICON_FA_FILE_LINES "  Binary file changed.");
            ImGui::Text("%s: %s", d.OldLabel.c_str(), SizeText(d.OldSize).c_str());
            ImGui::Text("%s: %s", d.NewLabel.c_str(), SizeText(d.NewSize).c_str());
            if (d.OldSize >= 0 && d.NewSize >= 0)
                ImGui::TextDisabled("Difference: %+lld bytes", d.NewSize - d.OldSize);
            ImGui::End();
            return;
        }
        if (d.Identical)
        {
            ImGui::TextDisabled("No content changes.");
            ImGui::End();
            return;
        }
        ImFont* mono = ImGuiLayer::GetFont(ImGuiLayer::FontRole::Mono);
        if (mono)
            ImGui::PushFont(mono, 0.0f);
        const float lineH = ImGui::GetTextLineHeightWithSpacing();
        const float numW = ImGui::CalcTextSize("00000").x;
        const ImU32 addBg = IM_COL32(46, 160, 67, 55), delBg = IM_COL32(220, 60, 60, 55), hunkBg = IM_COL32(80, 120, 200, 45);
        const ImU32 addFg = ImGui::GetColorU32(KindColor('A')), delFg = ImGui::GetColorU32(KindColor('D'));
        const ImU32 dim = ImGui::GetColorU32(ImGuiCol_TextDisabled), text = ImGui::GetColorU32(ImGuiCol_Text);
        ImGui::BeginChild("##diffbody", ImVec2(0, 0), ImGuiChildFlags_None, m_SideBySide ? ImGuiWindowFlags_None : ImGuiWindowFlags_HorizontalScrollbar);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float width = ImGui::GetContentRegionAvail().x;
        auto number = [&](float x, float y, int n) {
            if (n <= 0)
                return;
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%d", n);
            dl->AddText(ImVec2(x + numW - ImGui::CalcTextSize(buf).x, y), dim, buf);
        };
        ImGuiListClipper clipper;
        if (m_SideBySide)
        {
            clipper.Begin((int)m_SideRows.size(), lineH);
            const float half = width * 0.5f;
            while (clipper.Step())
                for (int r = clipper.DisplayStart; r < clipper.DisplayEnd; ++r)
                {
                    const SideRow& row = m_SideRows[r];
                    const ImVec2 p = ImGui::GetCursorScreenPos();
                    if (row.Header)
                    {
                        dl->AddRectFilled(p, ImVec2(p.x + width, p.y + lineH), hunkBg);
                        dl->AddText(ImVec2(p.x + 4, p.y), dim, d.Lines[row.Left].Text.c_str());
                    }
                    else
                    {
                        for (int side = 0; side < 2; ++side)
                        {
                            const int li = side == 0 ? row.Left : row.Right;
                            const float x0 = p.x + side * half;
                            const ImVec2 a(x0, p.y), b(x0 + half - 2.0f, p.y + lineH);
                            if (li < 0)
                            {
                                dl->AddRectFilled(a, b, IM_COL32(128, 128, 128, 20));
                                continue;
                            }
                            const VC::DiffLine& l = d.Lines[li];
                            if (l.Kind != ' ')
                                dl->AddRectFilled(a, b, l.Kind == '+' ? addBg : delBg);
                            number(x0 + 2.0f, p.y, side == 0 ? l.OldLine : l.NewLine);
                            dl->PushClipRect(ImVec2(x0 + numW + 8.0f, p.y), b, true);
                            dl->AddText(ImVec2(x0 + numW + 10.0f, p.y), l.Kind == '+' ? addFg : l.Kind == '-' ? delFg : text, l.Text.c_str());
                            dl->PopClipRect();
                        }
                        dl->AddLine(ImVec2(p.x + half - 1.0f, p.y), ImVec2(p.x + half - 1.0f, p.y + lineH), IM_COL32(128, 128, 128, 70));
                    }
                    ImGui::Dummy(ImVec2(width, lineH));
                }
        }
        else
        {
            clipper.Begin((int)d.Lines.size(), lineH);
            while (clipper.Step())
                for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
                {
                    const VC::DiffLine& l = d.Lines[i];
                    const ImVec2 p = ImGui::GetCursorScreenPos();
                    const float textW = ImGui::CalcTextSize(l.Text.c_str()).x + numW * 2.0f + 40.0f;
                    if (l.Kind != ' ')
                        dl->AddRectFilled(p, ImVec2(p.x + std::max(width, textW), p.y + lineH), l.Kind == '+' ? addBg : l.Kind == '-' ? delBg : hunkBg);
                    if (l.Kind == '@')
                        dl->AddText(ImVec2(p.x + 4, p.y), dim, l.Text.c_str());
                    else
                    {
                        number(p.x + 2.0f, p.y, l.OldLine);
                        number(p.x + numW + 10.0f, p.y, l.NewLine);
                        const char prefix[2] = { l.Kind, 0 };
                        const ImU32 color = l.Kind == '+' ? addFg : l.Kind == '-' ? delFg : text;
                        dl->AddText(ImVec2(p.x + numW * 2.0f + 18.0f, p.y), color, prefix);
                        dl->AddText(ImVec2(p.x + numW * 2.0f + 30.0f, p.y), color, l.Text.c_str());
                    }
                    ImGui::Dummy(ImVec2(textW, lineH));
                }
        }
        ImGui::EndChild();
        if (mono)
            ImGui::PopFont();
        ImGui::End();
    }

    // ------------------------------------------------------------------ Otomasyon

    void VersionControlPanel::RegisterAutomation()
    {
        using Args = Automation::CommandArgs;
        // vcsui <alt komut>: Version Control penceresinin işleri eşzamanlı (testler); "vcs" (EditorLayer) kip / durum komutlarıdır.
        // expect_fail kendini çağırır: işleyici statik std::function'da
        static std::function<bool(const Args&, std::string&)> s_Handler;
        Automation::RegisterCommand("vcsui", [](const Args& a, std::string& err) { return s_Handler(a, err); });
        s_Handler = [this](const Args& a, std::string& err) -> bool {
            struct SyncGuard
            {
                bool& Flag;
                bool Old;
                SyncGuard(bool& f) : Flag(f), Old(f) { Flag = true; }
                ~SyncGuard() { Flag = Old; }
            } guard(m_Sync);
            WaitIdle();
            if (a.size() < 2)
            {
                err = "vcsui open|close|close_diff|refresh|expect_changes|expect_change|check|check_all|select|message|commit|revert_selected|shelve|"
                      "changelist|expect_changelists|expect_scene_changed|"
                      "history|expect_history|expect_commit|expect_author|select_commit|expect_commit_files|expect_commit_file|diff|diff_commit|"
                      "expect_diff|diff_view|fetch|pull|push|get_latest|expect_ahead|expect_branch|branch|expect_branch_exists|expect_conflicts|"
                      "expect_operation|resolve|continue|abort|remote_add|expect_fail|expect_status|expect_workspace|test_*";
                return false;
            }
            const std::string& cmd = a[1];
            const fs::path projectDir = Project::GetActive() ? Project::GetActiveProjectDirectory() : fs::path();
            auto findChange = [this](const std::string& path) -> const VC::Change* {
                for (const VC::Change& c : m_Changes)
                    if (c.Path == path)
                        return &c;
                return nullptr;
            };
            auto opResult = [this, &err]() {
                Automation::Report("VCS UI: " + m_Status);
                if (!m_LastOk)
                    err = m_Status.empty() ? std::string("operation failed") : m_Status;
                return m_LastOk;
            };
            // Test yardımcısı: proje klasöründe kabuk komutu (yalnız test_* komutları; git deposu / dosya hazırlığı)
            auto shell = [&](const std::string& dir, const std::string& command) {
                std::string out;
                const int code = VC::Detail::Run(dir, command, out);
                if (code != 0)
                    err = out;
                return code == 0;
            };

            if (cmd == "open")
            {
                Open(a.size() > 2 && a[2] == "history" ? Tab::History : a.size() > 2 && a[2] == "sync" ? Tab::Sync : Tab::Changes);
                RefreshAll();
                return true;
            }
            if (cmd == "close")
            {
                m_IsOpen = false;
                m_ShowDiff = false;
                return true;
            }
            if (cmd == "close_diff")
            {
                m_ShowDiff = false;
                return true;
            }
            if (cmd == "refresh")
            {
                RefreshAll();
                return true;
            }
            if (cmd == "expect_changes" && a.size() > 2)
            {
                RefreshAll();
                const int want = std::atoi(a[2].c_str());
                const int got = (int)VisibleChanges().size();
                Automation::Report("VCS UI changes: " + std::to_string(got));
                if (got != want) { err = "changes " + std::to_string(got) + ", expected " + a[2] + (m_ChangesError.empty() ? "" : " (" + m_ChangesError + ")"); return false; }
                return true;
            }
            if (cmd == "expect_change" && a.size() > 3)
            {
                RefreshAll();
                const VC::Change* c = findChange(a[2]);
                const std::string want = JoinArgs(a, 3);
                const std::string got = c ? VC::ChangeKindName(c->Kind) : "none";
                Automation::Report("VCS UI change " + a[2] + " = " + got);
                if (got != want) { err = a[2] + ": '" + got + "', expected '" + want + "'"; return false; }
                return true;
            }
            if (cmd == "check" && a.size() > 3)
            {
                if (a[3] == "on") m_Unchecked.erase(a[2]); else m_Unchecked.insert(a[2]);
                return true;
            }
            if (cmd == "check_all" && a.size() > 2)
            {
                for (const VC::Change& c : m_Changes)
                    if (a[2] == "on") m_Unchecked.erase(c.Path); else m_Unchecked.insert(c.Path);
                return true;
            }
            if (cmd == "select" && a.size() > 2)
            {
                if (a[2] == "none") m_Selected.clear(); else m_Selected.insert(a[2]);
                return true;
            }
            if (cmd == "message")
            {
                std::snprintf(m_Message, sizeof(m_Message), "%s", JoinArgs(a, 2).c_str());
                return true;
            }
            if (cmd == "changelist" && a.size() > 2)
            {
                m_Changelist = a[2];
                return true;
            }
            if (cmd == "expect_changelists" && a.size() > 2)
            {
                RefreshAll();
                if ((int)m_Changelists.size() != std::atoi(a[2].c_str())) { err = "pending changelists: " + std::to_string(m_Changelists.size()); return false; }
                return true;
            }
            if (cmd == "commit") { RefreshAll(); DoCommit(); return opResult(); }   // test_write'tan sonra liste güncel olsun
            if (cmd == "shelve") { DoShelve(); return opResult(); }
            if (cmd == "revert_selected") { DoRevertSelected(); return opResult(); }
            if (cmd == "history")
            {
                m_HistoryPath = a.size() > 2 ? ProjectRelative(JoinArgs(a, 2)) : std::string();
                m_SelectedCommit = -1;
                Open(Tab::History);
                LoadHistory();
                Automation::Report("VCS UI history: " + std::to_string(m_Commits.size()) + " commit(s)" + (m_HistoryError.empty() ? "" : " - " + m_HistoryError));
                return m_HistoryError.empty() ? true : (err = m_HistoryError, false);
            }
            if ((cmd == "expect_history" || cmd == "expect_history_min") && a.size() > 2)
            {
                const int want = std::atoi(a[2].c_str()), got = (int)m_Commits.size();
                if (cmd == "expect_history" ? got != want : got < want) { err = "history has " + std::to_string(got) + " commit(s), expected " + a[2]; return false; }
                return true;
            }
            if ((cmd == "expect_commit" || cmd == "expect_author") && a.size() > 3)
            {
                const int i = std::atoi(a[2].c_str());
                if (i < 0 || i >= (int)m_Commits.size()) { err = "no commit " + a[2]; return false; }
                const std::string want = JoinArgs(a, 3);
                const std::string got = cmd == "expect_commit" ? m_Commits[i].Message : m_Commits[i].Author;
                Automation::Report("VCS UI commit " + a[2] + ": " + m_Commits[i].ShortId + " " + m_Commits[i].Author + " " + m_Commits[i].Date + " " + m_Commits[i].Summary);
                if (got.find(want) == std::string::npos) { err = "commit " + a[2] + ": '" + got + "' does not contain '" + want + "'"; return false; }
                return true;
            }
            if (cmd == "select_commit" && a.size() > 2)
            {
                const int i = std::atoi(a[2].c_str());
                if (i < 0 || i >= (int)m_Commits.size()) { err = "no commit " + a[2]; return false; }
                LoadCommitFiles(i);
                return m_CommitFilesError.empty() ? true : (err = m_CommitFilesError, false);
            }
            if (cmd == "expect_commit_files" && a.size() > 2)
            {
                std::string list;
                for (const auto& f : m_CommitFiles)
                    list += " " + f.Path;
                Automation::Report("VCS UI commit files:" + list);
                if ((int)m_CommitFiles.size() != std::atoi(a[2].c_str())) { err = "commit files:" + list; return false; }
                return true;
            }
            if (cmd == "expect_commit_file" && a.size() > 2)
            {
                for (const auto& f : m_CommitFiles)
                    if (f.Path == a[2] || f.DepotPath == a[2] || (f.Path.size() > a[2].size() && f.Path.compare(f.Path.size() - a[2].size(), a[2].size(), a[2]) == 0))
                    {
                        if (a.size() > 3 && VC::ChangeKindName(f.Kind) != JoinArgs(a, 3)) { err = a[2] + " is " + VC::ChangeKindName(f.Kind); return false; }
                        return true;
                    }
                err = a[2] + " is not in the commit";
                return false;
            }
            if (cmd == "diff" && a.size() > 2)
            {
                RefreshAll();
                const VC::Change* c = findChange(JoinArgs(a, 2));
                if (c)
                    DoDiffChange(*c);
                else
                    ShowDiff(JoinArgs(a, 2));
                Automation::Report("VCS UI diff " + m_Diff.Title + ": +" + std::to_string(m_Diff.Added) + " -" + std::to_string(m_Diff.Removed) +
                                   (m_Diff.Binary ? " binary" : "") + (m_Diff.Identical ? " identical" : ""));
                return m_Diff.Error.empty() ? true : (err = m_Diff.Error, false);
            }
            if (cmd == "diff_commit" && a.size() > 3)
            {
                const int i = std::atoi(a[2].c_str());
                if (i < 0 || i >= (int)m_Commits.size()) { err = "no commit " + a[2]; return false; }
                if (m_SelectedCommit != i)
                    LoadCommitFiles(i);
                const std::string path = JoinArgs(a, 3);
                for (int k = 0; k < (int)m_CommitFiles.size(); ++k)
                    if (m_CommitFiles[k].Path == path || m_CommitFiles[k].DepotPath == path ||
                        (m_CommitFiles[k].Path.size() > path.size() && m_CommitFiles[k].Path.compare(m_CommitFiles[k].Path.size() - path.size(), path.size(), path) == 0))
                    {
                        DoDiffCommitFile(k);
                        Automation::Report("VCS UI diff " + m_Diff.Title + ": +" + std::to_string(m_Diff.Added) + " -" + std::to_string(m_Diff.Removed) +
                                           (m_Diff.Binary ? " binary" : ""));
                        return m_Diff.Error.empty() ? true : (err = m_Diff.Error, false);
                    }
                err = path + " is not in commit " + a[2];
                return false;
            }
            if (cmd == "expect_diff" && a.size() > 2)
            {
                const std::string& what = a[2];
                if (what == "binary")
                {
                    if (!m_Diff.Binary) { err = "the diff is not binary"; return false; }
                    Automation::Report("VCS UI binary sizes " + std::to_string(m_Diff.OldSize) + " -> " + std::to_string(m_Diff.NewSize));
                    if (a.size() > 4 && (m_Diff.OldSize != std::atoll(a[3].c_str()) || m_Diff.NewSize != std::atoll(a[4].c_str())))
                    {
                        err = "sizes " + std::to_string(m_Diff.OldSize) + " -> " + std::to_string(m_Diff.NewSize);
                        return false;
                    }
                    if (a.size() == 4 && a[3] == "grew" && !(m_Diff.OldSize >= 0 && m_Diff.NewSize > m_Diff.OldSize))
                    {
                        err = "sizes " + std::to_string(m_Diff.OldSize) + " -> " + std::to_string(m_Diff.NewSize);
                        return false;
                    }
                    return true;
                }
                if (what == "identical")
                    return m_Diff.Identical ? true : (err = "the diff is not empty", false);
                if (what == "counts" && a.size() > 4)
                {
                    if (m_Diff.Added != std::atoi(a[3].c_str()) || m_Diff.Removed != std::atoi(a[4].c_str()))
                    {
                        err = "+" + std::to_string(m_Diff.Added) + " -" + std::to_string(m_Diff.Removed);
                        return false;
                    }
                    return true;
                }
                const char kind = what == "added" ? '+' : what == "removed" ? '-' : ' ';
                const std::string text = JoinArgs(a, 3);
                for (const VC::DiffLine& l : m_Diff.Lines)
                    if (l.Kind == kind && l.Text.find(text) != std::string::npos)
                        return true;
                err = "no " + what + " line with '" + text + "' in the diff of " + m_Diff.Title;
                return false;
            }
            if (cmd == "diff_view" && a.size() > 2)
            {
                m_SideBySide = a[2] != "unified";
                return true;
            }
            if (cmd == "fetch") { RunOp("Fetching", [](const VC::Context& c) { return VC::Fetch(c); }, false, false); return opResult(); }
            if (cmd == "pull")
            {
                const bool rebase = !(a.size() > 2 && a[2] == "merge");
                RunOp("Pulling", [rebase](const VC::Context& c) { return VC::Pull(c, rebase); }, true, true);
                return opResult();
            }
            if (cmd == "push") { RunOp("Pushing", [](const VC::Context& c) { return VC::Push(c); }, false, false); return opResult(); }
            if (cmd == "get_latest") { RunOp("Getting latest", [](const VC::Context& c) { return VC::GetLatest(c); }, true, true); return opResult(); }
            if (cmd == "continue") { RunOp("Continuing", [](const VC::Context& c) { return VC::ContinueOperation(c); }, true, true); return opResult(); }
            if (cmd == "abort") { RunOp("Aborting", [](const VC::Context& c) { return VC::AbortOperation(c); }, true, true); return opResult(); }
            if (cmd == "resolve" && a.size() > 3)
            {
                std::string repo = a[2];
                for (const VC::Change& c : m_Changes)
                    if (c.Path == a[2])
                        repo = c.RepoPath;
                const VC::Resolve how = a[3] == "mine" ? VC::Resolve::Mine : a[3] == "theirs" ? VC::Resolve::Theirs : VC::Resolve::MarkResolved;
                RunOp("Resolving", [repo, how](const VC::Context& c) { return VC::ResolveConflict(c, repo, how); }, true, false);
                return opResult();
            }
            if (cmd == "remote_add" && a.size() > 3)
            {
                const std::string name = a[2], url = a[3];
                RunOp("Adding remote", [name, url](const VC::Context& c) { return VC::AddRemote(c, name, url); }, false, false);
                return opResult();
            }
            if (cmd == "branch" && a.size() > 3)
            {
                const std::string name = a[3];
                if (a[2] == "create")
                {
                    const bool sw = a.size() > 4 && a[4] == "switch";
                    RunOp("Creating branch", [name, sw](const VC::Context& c) { return VC::CreateBranch(c, name, sw); }, false, true);
                    return opResult();
                }
                if (a[2] == "switch")
                {
                    RefreshAll();
                    VC::Branch target;
                    target.Name = name;
                    for (const VC::Branch& b : m_Branches)
                        if (b.Name == name)
                            target = b;
                    DoSwitch(target);
                    return opResult();
                }
                if (a[2] == "delete")
                {
                    DoDelete(name, a.size() > 4 && a[4] == "force");
                    return opResult();
                }
            }
            if (cmd == "expect_branch" && a.size() > 2)
            {
                RefreshAll();
                Automation::Report("VCS UI branch: " + m_Info.Branch);
                return m_Info.Branch == a[2] ? true : (err = "current branch is '" + m_Info.Branch + "'", false);
            }
            if (cmd == "expect_branch_exists" && a.size() > 2)
            {
                RefreshAll();
                const bool want = !(a.size() > 3 && a[3] == "no");
                const bool got = std::any_of(m_Branches.begin(), m_Branches.end(), [&](const VC::Branch& b) { return b.Name == a[2]; });
                return got == want ? true : (err = "branch " + a[2] + (got ? " exists" : " does not exist"), false);
            }
            if (cmd == "expect_ahead" && a.size() > 3)
            {
                RefreshAll();
                Automation::Report("VCS UI ahead " + std::to_string(m_Info.Ahead) + " behind " + std::to_string(m_Info.Behind) + " upstream " + m_Info.Upstream);
                if (m_Info.Ahead != std::atoi(a[2].c_str()) || m_Info.Behind != std::atoi(a[3].c_str()))
                {
                    err = "ahead " + std::to_string(m_Info.Ahead) + " behind " + std::to_string(m_Info.Behind);
                    return false;
                }
                return true;
            }
            if (cmd == "expect_conflicts" && a.size() > 2)
            {
                RefreshAll();
                std::string list;
                for (const auto& f : m_Info.Conflicts)
                    list += " " + f;
                Automation::Report("VCS UI conflicts:" + list);
                return (int)m_Info.Conflicts.size() == std::atoi(a[2].c_str()) ? true : (err = "conflicts:" + list, false);
            }
            if (cmd == "expect_operation" && a.size() > 2)
            {
                RefreshAll();
                const std::string got = m_Info.Operation.empty() ? std::string("none") : m_Info.Operation;
                return got == a[2] ? true : (err = "operation '" + got + "'", false);
            }
            if (cmd == "expect_status" && a.size() > 2)
            {
                const std::string want = JoinArgs(a, 2);
                return m_Status.find(want) != std::string::npos ? true : (err = "status '" + m_Status + "' does not contain '" + want + "'", false);
            }
            if ((cmd == "expect_workspace" || cmd == "expect_stream") && a.size() > 2)
            {
                RefreshAll();
                const std::string& got = cmd == "expect_workspace" ? m_Info.Workspace : m_Info.Stream;
                return got == a[2] ? true : (err = cmd.substr(7) + " '" + got + "'", false);
            }
            if (cmd == "expect_fail" && a.size() > 2)
            {
                Args rest(a.begin() + 1, a.end());
                rest[0] = "vcsui";
                std::string inner;
                if (s_Handler(rest, inner))
                {
                    err = "expected '" + JoinArgs(a, 2) + "' to fail";
                    return false;
                }
                Automation::Report("VCS UI failed as expected: " + inner);
                return true;
            }
            if (cmd == "expect_scene_changed")
            {
                const bool want = !(a.size() > 2 && a[2] == "no");
                return m_SceneChanged == want ? true : (err = std::string("scene changed banner ") + (m_SceneChanged ? "shown" : "not shown"), false);
            }

            // ---- Test yardımcıları (yerel geçici depolar; ağ yok)
            if (projectDir.empty()) { err = "no project"; return false; }
            const std::string dir = projectDir.string();
            if (cmd == "test_identity" && a.size() > 3)
                return shell(dir, "git config user.name " + VC::Detail::Quote(a[2]) + " && git config user.email " + VC::Detail::Quote(a[3]));
            if (cmd == "test_remote_init")
            {
                // Proje içinde .vcstest/remote.git çıplak depo = origin (.git/info/exclude ile durum listesine girmez)
                return shell(dir, "mkdir -p .vcstest && git init -q --bare .vcstest/remote.git && mkdir -p .git/info && "
                                  "echo .vcstest/ >> .git/info/exclude");
            }
            if (cmd == "test_remote_commit" && a.size() > 3)
            {
                // Başka bir geliştirici: .vcstest/clone'da dosyaya satır ekler, kaydeder, gönderir
                const std::string branch = m_Info.Branch.empty() ? std::string("master") : m_Info.Branch;
                const std::string rel = a[2];
                const std::string text = JoinArgs(a, 3);
                const std::string ident = "-c user.name=Other -c user.email=other@localhost";
                return shell(dir, "if [ -d .vcstest/clone ]; then git -C .vcstest/clone pull -q --no-rebase; else git clone -q -b " + VC::Detail::Quote(branch) +
                                      " .vcstest/remote.git .vcstest/clone; fi && mkdir -p \"$(dirname .vcstest/clone/" + VC::Detail::Quote(rel) + ")\" && printf '%s\\n' " +
                                      VC::Detail::Quote(text) + " >> .vcstest/clone/" + VC::Detail::Quote(rel) + " && git -C .vcstest/clone add -A && git -C .vcstest/clone " +
                                      ident + " commit -q -m " + VC::Detail::Quote("Remote change to " + rel) + " && git -C .vcstest/clone push -q");
            }
            if (cmd == "test_write" && a.size() > 3)
            {
                fs::path p = projectDir / a[2];
                std::error_code ec;
                fs::create_directories(p.parent_path(), ec);
                std::ofstream out(p, std::ios::app | std::ios::binary);
                out << JoinArgs(a, 3) << "\n";
                return (bool)out;
            }
            if (cmd == "test_append_bytes" && a.size() > 3)
            {
                std::ofstream out(projectDir / a[2], std::ios::app | std::ios::binary);
                const std::string zeros((size_t)std::max(0, std::atoi(a[3].c_str())), '\0');
                out.write(zeros.data(), (std::streamsize)zeros.size());
                return (bool)out;
            }
            err = "unknown vcsui command '" + cmd + "'";
            return false;
        };
    }
}
