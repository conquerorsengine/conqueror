#include "AddonManagerPanel.h"
#include "Addons/AddonManager.h"
#include "Addons/AddonHost.h"
#include "UI/EditorUI.h"
#include "UI/IconNames.h"

#include <imgui.h>
#include <IconsFontAwesome6.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace Conqueror::Editor
{
    namespace
    {
        std::string Lower(std::string s)
        {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });
            return s;
        }

        bool Matches(const AddonManifest& m, const std::string& query)
        {
            if (query.empty())
                return true;
            std::string q = Lower(query);
            if (Lower(m.Name).find(q) != std::string::npos || Lower(m.Description).find(q) != std::string::npos ||
                Lower(m.Category).find(q) != std::string::npos)
                return true;
            for (auto& t : m.Tags)
                if (Lower(t).find(q) != std::string::npos)
                    return true;
            return false;
        }

        void OpenFolder(const std::filesystem::path& dir)
        {
#ifdef CQ_PLATFORM_WINDOWS
            std::string command = "start \"\" \"" + dir.string() + "\"";
#elif defined(CQ_PLATFORM_LINUX)
            std::string command = "xdg-open \"" + dir.string() + "\" >/dev/null 2>&1 &";
#else
            std::string command = "open \"" + dir.string() + "\"";
#endif
            int rc = std::system(command.c_str());
            (void)rc;
        }
    }

    void AddonManagerPanel::OnImGuiRender(bool* open)
    {
        ImGui::SetNextWindowSize(ImVec2(820, 520), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin(ICON_FA_PUZZLE_PIECE " Addon Manager###AddonManager", open))
        {
            ImGui::End();
            return;
        }

        // Üst çubuk: Kurulu / Katalog, arama, yenile
        ImGui::BeginGroup();
        bool installedActive = !m_ShowCatalog;
        if (UI::ToggleButton(ICON_FA_BOX_OPEN "  Installed", installedActive, ImVec2(120, 0)))
            m_ShowCatalog = false;
        ImGui::SameLine();
        bool catalogActive = m_ShowCatalog;
        if (UI::ToggleButton(ICON_FA_STORE "  Catalog", catalogActive, ImVec2(120, 0)))
            m_ShowCatalog = true;
        ImGui::SameLine();
        ImGui::SetNextItemWidth(std::max(120.0f, ImGui::GetContentRegionAvail().x - 120.0f));
        char buf[128];
        std::snprintf(buf, sizeof(buf), "%s", m_Search.c_str());
        if (ImGui::InputTextWithHint("##addonsearch", ICON_FA_MAGNIFYING_GLASS "  Search addons", buf, sizeof(buf)))
            m_Search = buf;
        ImGui::SameLine();
        const bool reload = ImGui::Button(ICON_FA_ARROWS_ROTATE "  Reload", ImVec2(-1, 0));
        ImGui::EndGroup();
        ImGui::Separator();

        const float listW = std::max(220.0f, ImGui::GetContentRegionAvail().x * 0.34f);
        ImGui::BeginChild("##addonlist", ImVec2(listW, 0), ImGuiChildFlags_Borders);
        DrawList();
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("##addondetails", ImVec2(0, 0), ImGuiChildFlags_Borders);
        DrawDetails();
        ImGui::EndChild();
        ImGui::End();
        if (reload)
            AddonHost::Get().ReloadAll();
    }

    void AddonManagerPanel::DrawList()
    {
        const auto& list = m_ShowCatalog ? AddonManager::GetCatalog() : AddonManager::GetInstalled();
        if (list.empty())
        {
            ImGui::Spacing();
            ImGui::TextDisabled(m_ShowCatalog ? "The catalog is empty." : "No addons installed.");
            if (!m_ShowCatalog)
            {
                ImGui::TextDisabled("Install one from the Catalog or");
                ImGui::TextDisabled("create a new addon below.");
            }
        }
        for (const auto& m : list)
        {
            if (!Matches(m, m_Search))
                continue;
            ImGui::PushID(m.Id.c_str());
            const bool selected = m.Id == m_SelectedId;
            ImVec2 cursor = ImGui::GetCursorScreenPos();
            const float rowH = ImGui::GetTextLineHeight() * 2.0f + 10.0f;
            if (ImGui::Selectable("##row", selected, 0, ImVec2(0, rowH)))
                m_SelectedId = m.Id;
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const bool enabled = !m_ShowCatalog && AddonManager::IsEnabled(m.Id);
            const bool installed = AddonManager::FindInstalled(m.Id) != nullptr;
            ImU32 iconCol = m_ShowCatalog ? ImGui::GetColorU32(UI::Colors::Accent) : enabled ? ImGui::GetColorU32(UI::Colors::Success)
                                                                                              : ImGui::GetColorU32(ImGuiCol_TextDisabled);
            dl->AddText(ImGui::GetFont(), ImGui::GetFontSize() * 1.35f, ImVec2(cursor.x + 6, cursor.y + 6), iconCol, UI::IconFromName(m.Icon));
            dl->AddText(ImVec2(cursor.x + 36, cursor.y + 4), ImGui::GetColorU32(ImGuiCol_Text), m.Name.c_str());
            std::string sub = m.Version + "  |  " + m.Category;
            if (m_ShowCatalog && installed) sub += "  |  installed";
            if (!m_ShowCatalog && !enabled) sub += "  |  disabled";
            dl->AddText(ImVec2(cursor.x + 36, cursor.y + 6 + ImGui::GetTextLineHeight()), ImGui::GetColorU32(ImGuiCol_TextDisabled), sub.c_str());
            ImGui::PopID();
        }

        // Alt: yeni addon
        ImGui::SetCursorPosY(std::max(ImGui::GetCursorPosY(), ImGui::GetWindowHeight() - 64.0f));
        ImGui::Separator();
        ImGui::TextDisabled("New addon");
        ImGui::SetNextItemWidth(-90.0f);
        char nameBuf[96];
        std::snprintf(nameBuf, sizeof(nameBuf), "%s", m_NewName.c_str());
        if (ImGui::InputText("##newaddon", nameBuf, sizeof(nameBuf)))
            m_NewName = nameBuf;
        ImGui::SameLine();
        if (UI::AccentButton(ICON_FA_PLUS " Create", ImVec2(-1, 0)))
        {
            std::string id, error;
            if (AddonManager::CreateNew(m_NewName, &id, &error))
            {
                m_ShowCatalog = false;
                m_SelectedId = id;
                AddonHost::Get().ReloadAll();
                if (const AddonManifest* m = AddonManager::FindInstalled(id); m && OpenFile && !m->EditorScript.empty())
                    OpenFile(m->Directory / m->EditorScript);
            }
            else
                m_LastError = error;
        }
    }

    void AddonManagerPanel::DrawDetails()
    {
        const AddonManifest* m = m_ShowCatalog ? AddonManager::FindInCatalog(m_SelectedId) : AddonManager::FindInstalled(m_SelectedId);
        if (!m)
        {
            ImGui::Spacing();
            UI::InfoBox("Select an addon to see its details.\nAddons add menu items, ADDONS-tab tools, editor panels, script components and prefabs.",
                        UI::InfoType::Info);
            if (!m_LastError.empty())
                UI::InfoBox(m_LastError.c_str(), UI::InfoType::Error);
            return;
        }
        const bool installed = AddonManager::FindInstalled(m->Id) != nullptr;
        const AddonManifest* catalogEntry = AddonManager::FindInCatalog(m->Id);
        const AddonManifest* installedEntry = AddonManager::FindInstalled(m->Id);

        // Başlık
        ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.9f);
        ImGui::TextColored(UI::Colors::Accent, "%s", UI::IconFromName(m->Icon));
        ImGui::PopFont();
        ImGui::SameLine();
        ImGui::BeginGroup();
        ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.3f);
        ImGui::TextUnformatted(m->Name.c_str());
        ImGui::PopFont();
        ImGui::TextDisabled("v%s  |  %s  |  %s", m->Version.c_str(), m->Author.empty() ? "Unknown author" : m->Author.c_str(), m->Id.c_str());
        ImGui::EndGroup();
        ImGui::Spacing();
        if (!m->Tags.empty())
        {
            for (size_t i = 0; i < m->Tags.size(); ++i)
            {
                if (i) ImGui::SameLine();
                UI::Badge(m->Tags[i].c_str(), UI::Colors::AccentDim);
            }
            ImGui::Spacing();
        }
        ImGui::TextWrapped("%s", m->Description.c_str());
        if (!m->Dependencies.empty())
        {
            std::string deps;
            for (const auto& d : m->Dependencies)
            {
                const AddonManifest* dm = AddonManager::FindInCatalog(d);
                deps += (deps.empty() ? "" : ", ") + (dm ? dm->Name : d);
            }
            ImGui::TextDisabled(ICON_FA_LINK "  Requires: %s (installed automatically)", deps.c_str());
        }
        ImGui::Spacing();

        // Eylemler
        if (m_ShowCatalog)
        {
            const bool outdated = installedEntry && catalogEntry && installedEntry->Version != catalogEntry->Version;
            if (!installed)
            {
                if (UI::AccentButton(ICON_FA_DOWNLOAD "  Install", ImVec2(160, 0)))
                {
                    // Kurulum listeleri yeniden tarar: 'm' geçersizleşir, bu karede çizimi bitir
                    std::string id = m->Id, error;
                    if (AddonManager::Install(id, &error))
                        AddonHost::Get().ReloadAll();
                    else
                        m_LastError = error;
                    return;
                }
            }
            else if (outdated)
            {
                if (UI::AccentButton(ICON_FA_ARROWS_ROTATE "  Update", ImVec2(160, 0)))
                {
                    std::string id = m->Id, error;
                    if (AddonManager::Install(id, &error))
                        AddonHost::Get().ReloadAll();
                    else
                        m_LastError = error;
                    return;
                }
            }
            else
            {
                UI::StatusDot(UI::Colors::Success);
                ImGui::SameLine();
                ImGui::TextUnformatted("Installed in this project");
                ImGui::SameLine();
                if (ImGui::SmallButton("Show"))
                    m_ShowCatalog = false;
            }
        }
        else
        {
            bool enabled = AddonManager::IsEnabled(m->Id);
            if (UI::ToggleButton(enabled ? ICON_FA_TOGGLE_ON "  Enabled" : ICON_FA_TOGGLE_OFF "  Disabled", enabled, ImVec2(140, 0)))
            {
                AddonManager::SetEnabled(m->Id, enabled);
                AddonHost::Get().ReloadAll();
                return;
            }
            ImGui::SameLine();
            if (!m->EditorScript.empty() && ImGui::Button(ICON_FA_CODE "  Edit Script") && OpenFile)
                OpenFile(m->Directory / m->EditorScript);
            ImGui::SameLine();
            if (ImGui::Button(ICON_FA_FOLDER_OPEN "  Folder"))
                OpenFolder(m->Directory);
            ImGui::SameLine();
            if (m_ConfirmUninstall != m->Id)
            {
                if (UI::DangerButton(ICON_FA_TRASH "  Uninstall"))
                    m_ConfirmUninstall = m->Id;
            }
            else
            {
                ImGui::TextColored(UI::Colors::Warning, "Delete Packages/%s?", m->Directory.filename().string().c_str());
                ImGui::SameLine();
                if (ImGui::SmallButton("Yes, delete"))
                {
                    std::string id = m->Id, error;
                    m_ConfirmUninstall.clear();
                    if (AddonManager::Uninstall(id, &error))
                    {
                        m_SelectedId.clear();
                        AddonHost::Get().ReloadAll();
                    }
                    else
                        m_LastError = error;
                    return;
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Cancel"))
                    m_ConfirmUninstall.clear();
            }
        }
        if (!m_LastError.empty())
            UI::InfoBox(m_LastError.c_str(), UI::InfoType::Error);
        ImGui::Spacing();

        // Çalışma durumu (kurulu + etkin)
        if (!m_ShowCatalog)
        {
            if (auto* inst = AddonHost::Get().FindInstance(m->Id))
            {
                if (!inst->Error.empty())
                    UI::InfoBox(inst->Error.c_str(), UI::InfoType::Error);
                else if (inst->Loaded)
                    UI::InfoBox("Editor script loaded.", UI::InfoType::Success);
                if (UI::BeginSection("Contributes", true, ICON_FA_LIST))
                {
                    for (auto& item : inst->Menu)
                        ImGui::BulletText("Menu  Addons > %s", item.Path.c_str());
                    for (auto& b : inst->Buttons)
                        ImGui::BulletText("Tool button  %s %s", UI::IconFromName(b.Icon), b.Label.c_str());
                    for (auto& p : inst->Panels)
                    {
                        ImGui::BulletText("Panel  %s", p.Title.c_str());
                        ImGui::SameLine();
                        ImGui::PushID(p.Title.c_str());
                        if (ImGui::SmallButton(p.Open ? "Close" : "Open"))
                        {
                            p.Open = !p.Open;
                            p.Focus = p.Open;
                        }
                        ImGui::PopID();
                    }
                    for (auto& c : m->Components)
                        ImGui::BulletText("Component  %s", std::filesystem::path(c).stem().string().c_str());
                    for (auto& ins : m->Inserts)
                        ImGui::BulletText("Insert  %s %s", UI::IconFromName(ins.Icon), ins.Name.c_str());
                    if (inst->Menu.empty() && inst->Buttons.empty() && inst->Panels.empty() && m->Components.empty() && m->Inserts.empty())
                        ImGui::TextDisabled("Nothing registered yet.");
                    UI::EndSection();
                }
                if (!inst->Messages.empty() && UI::BeginSection("Messages", false, ICON_FA_MESSAGE))
                {
                    for (auto it = inst->Messages.rbegin(); it != inst->Messages.rend(); ++it)
                        ImGui::TextWrapped("%s", it->c_str());
                    UI::EndSection();
                }
            }
            else if (!AddonManager::IsEnabled(m->Id))
            {
                UI::InfoBox("Disabled: its tools, panels and menu items are not loaded.", UI::InfoType::Warning);
            }
        }
        else
        {
            if (UI::BeginSection("Contents", true, ICON_FA_LIST))
            {
                if (!m->EditorScript.empty())
                    ImGui::BulletText("Editor script  %s", m->EditorScript.c_str());
                for (auto& c : m->Components)
                    ImGui::BulletText("Component  %s", std::filesystem::path(c).stem().string().c_str());
                for (auto& ins : m->Inserts)
                    ImGui::BulletText("Insert  %s %s", UI::IconFromName(ins.Icon), ins.Name.c_str());
                UI::EndSection();
            }
        }
        ImGui::Spacing();
        ImGui::TextDisabled("%s", m->Directory.string().c_str());
    }
}
