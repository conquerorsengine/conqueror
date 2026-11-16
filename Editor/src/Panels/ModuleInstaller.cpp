#include "ModuleInstaller.h"

#include "Core/Automation/Automation.h"
#include "Core/Base/Version.h"
#include "Core/Logging/Log.h"
#include "Core/Project/EditorInstalls.h"
#include "UI/EditorUI.h"

#include <imgui.h>
#include <IconsFontAwesome6.h>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <mutex>
#include <thread>

namespace Conqueror::Editor::ModuleInstaller
{
    namespace fs = std::filesystem;

    namespace
    {
        struct State
        {
            std::mutex Mutex;
            std::shared_ptr<Installs::Job> Job;
            std::string ModuleId;
            std::string Status;
            bool Preparing = false;
        };
        State& S()
        {
            static auto& s = *new State();
            return s;
        }

        fs::path InstallDir() { return Installs::InstallRootOf(fs::current_path()); }

        bool Installed(const std::string& id)
        {
            const auto mods = Installs::InstalledModules(InstallDir());
            return std::find(mods.begin(), mods.end(), id) != mods.end();
        }
    }

    bool Busy()
    {
        State& s = S();
        std::lock_guard<std::mutex> lock(s.Mutex);
        return s.Preparing || (s.Job && !s.Job->Finished);
    }

    std::string Status()
    {
        State& s = S();
        std::lock_guard<std::mutex> lock(s.Mutex);
        if (s.Job && s.Job->Finished)
            return s.Job->Succeeded ? "Installed" : s.Job->GetError();
        return s.Status;
    }

    bool Start(const std::string& platform, std::string& error)
    {
        if (Busy())
        {
            error = "A module is already being installed";
            return false;
        }
        const std::string id = Installs::PlatformModuleId(platform);
        std::string commit;
        Installs::Channel channel = Installs::Channel::Local;
        const std::string version = Installs::ReadInstallVersion(InstallDir(), &commit, &channel);
        std::error_code ec;
        if (!fs::exists(InstallDir() / "conqueror-install.json", ec))
        {
            error = "This editor was built from source, so there is no published " + id +
                    " module for it. Builds compile the player from source, or install a released editor from the Hub (Installs).";
            return false;
        }
        State& s = S();
        {
            std::lock_guard<std::mutex> lock(s.Mutex);
            s.Preparing = true;
            s.ModuleId = id;
            s.Status = "Looking for the module on GitHub Releases...";
            s.Job.reset();
        }
        const fs::path installDir = InstallDir();
        std::thread([id, version, commit, channel, installDir]() {
            State& st = S();
            auto finish = [&](const std::string& status, std::shared_ptr<Installs::Job> job) {
                std::lock_guard<std::mutex> lock(st.Mutex);
                st.Preparing = false;
                st.Status = status;
                st.Job = std::move(job);
            };
            std::string json, err;
            std::vector<Installs::Release> releases;
            if (!Installs::FetchText(Installs::ReleasesApiUrl(), json, err) || !Installs::ParseReleases(json, releases, err))
                return finish("Could not load GitHub Releases: " + err, nullptr);
            for (auto& r : releases)
            {
                const bool match = channel == Installs::Channel::ConquerorX ? r.Kind == Installs::Channel::ConquerorX
                                                                             : (r.Kind != Installs::Channel::ConquerorX && r.Version == version);
                if (!match || r.ManifestUrl.empty())
                    continue;
                std::string manifest;
                if (!Installs::FetchText(r.ManifestUrl, manifest, err) || !Installs::ApplyManifest(r, manifest, err))
                    return finish(err, nullptr);
                if (channel == Installs::Channel::ConquerorX && !commit.empty() && r.Commit != commit)
                    return finish("A newer ConquerorX build exists: update ConquerorX from the Hub first (its modules must match the build).", nullptr);
                Installs::Install install;
                install.Version = version;
                install.Kind = channel;
                install.Commit = commit;
                install.Path = installDir;
                install.Modules = Installs::InstalledModules(installDir);
                Installs::JobRequest req;
                if (!Installs::MakeAddModulesRequest(r, install, { id }, Installs::HubConfigDirectory(), req, err))
                    return finish(err, nullptr);
                return finish("Installing...", Installs::StartJob(req));
            }
            finish("Conqueror's Engine " + version + " was not published on GitHub Releases, so its modules cannot be downloaded.", nullptr);
        }).detach();
        return true;
    }

    bool Draw(const std::string& platform)
    {
        const std::string id = Installs::PlatformModuleId(platform);
        const Installs::ModuleInfo* info = Installs::FindModule(id);
        if (!info)
            return false;
        if (!Installs::PrebuiltPlayerDir(fs::current_path(), platform).empty())
        {
            UI::InfoBox((std::string(ICON_FA_CIRCLE_CHECK "  ") + info->Name + " is installed: builds use its prebuilt player (no toolchain needed).").c_str(),
                        UI::InfoType::Success);
            return true;
        }
        State& s = S();
        std::shared_ptr<Installs::Job> job;
        std::string status;
        bool preparing = false;
        {
            std::lock_guard<std::mutex> lock(s.Mutex);
            if (s.ModuleId == id)
            {
                job = s.Job;
                status = s.Status;
                preparing = s.Preparing;
            }
        }
        if (job && !job->Finished)
        {
            ImGui::ProgressBar(job->Progress, ImVec2(-1, 0), job->GetStep().c_str());
            if (ImGui::SmallButton("Cancel"))
                job->Cancel = true;
            return false;
        }
        if (preparing)
        {
            ImGui::TextDisabled("%s", status.c_str());
            return false;
        }
        if (Installed(id))
            return false;   // modül var ama bu platformun oyuncusu yok (ör. yalnız araçlar)
        if (ImGui::Button((std::string(ICON_FA_DOWNLOAD "  Install ") + info->Name).c_str()))
        {
            std::string error;
            if (!Start(platform, error))
            {
                std::lock_guard<std::mutex> lock(s.Mutex);
                s.ModuleId = id;
                s.Status = error;
            }
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))
            ImGui::SetTooltip("Downloads the prebuilt %s player of this editor version from GitHub Releases (like Unity Hub > Add modules).", platform.c_str());
        if (job && job->Finished)
            ImGui::TextColored(job->Succeeded ? ImVec4(0.42f, 0.85f, 0.52f, 1) : ImVec4(1, 0.47f, 0.43f, 1), "%s",
                               job->Succeeded ? "Installed" : job->GetError().c_str());
        else if (!status.empty())
            ImGui::TextWrapped("%s", status.c_str());
        return false;
    }

    void RegisterAutomation()
    {
        Automation::RegisterCommand("module_install", [](const Automation::CommandArgs& a, std::string& err) {
            if (a.size() < 2) { err = "module_install start <platform> | busy | expect_status <text> | expect_installed <platform>"; return false; }
            if (a[1] == "start" && a.size() > 2)
            {
                if (Start(a[2], err))
                    return true;
                State& s = S();
                std::lock_guard<std::mutex> lock(s.Mutex);
                s.Status = err;   // beklenen hata da durum satırında görünür
                return true;
            }
            if (a[1] == "busy")
            {
                if (Busy()) { err = "busy"; return false; }
                return true;
            }
            if (a[1] == "expect_status" && a.size() > 2)
            {
                const std::string st = Status();
                Automation::Report("module_install: " + st);
                if (st.find(a[2]) == std::string::npos) { err = "status: " + st; return false; }
                return true;
            }
            if (a[1] == "expect_installed" && a.size() > 2)
            {
                if (Installs::PrebuiltPlayerDir(fs::current_path(), a[2]).empty()) { err = a[2] + " module is not installed"; return false; }
                return true;
            }
            err = "unknown module_install command";
            return false;
        });
    }
}
