#include "MultiplayerPanel.h"
#include "UI/EditorUI.h"
#include "Scene/Scene.h"
#include "Scene/Entity.h"
#include "Scene/Components.h"
#include "Networking/Multiplayer/NetworkSession.h"
#include "Networking/Multiplayer/NetworkComponents.h"
#include "Networking/Steam/SteamPlatform.h"

#include <IconsFontAwesome6.h>
#include <imgui.h>

#include <cstdio>
#include <cstdlib>

namespace Conqueror::Editor
{
    using Net::NetworkSession;

    namespace
    {
        // Sahnedeki NetworkManager ayarları (pencere varsayılanları)
        const NetworkManagerComponent* FindManager(Scene* scene)
        {
            if (!scene)
                return nullptr;
            auto view = scene->m_Registry.view<NetworkManagerComponent>();
            for (auto e : view)
                return &view.get<NetworkManagerComponent>(e);
            return nullptr;
        }
    }

    void MultiplayerPanel::Update(bool playing)
    {
        if (!m_PendingStart)
            return;
        if (!playing)
            return;
        // Play'in ilk karesi betikleri oluşturur; oturumu bir sonraki karede başlat
        if (m_PendingFrames > 0)
        {
            --m_PendingFrames;
            return;
        }
        auto& s = NetworkSession::Get();
        bool ok = false;
        if (m_PendingStart == 3)
            ok = s.StartClient(m_Scene.get(), m_Address, (uint16_t)m_Port, m_Transport);
        else
            ok = s.StartServer(m_Scene.get(), (uint16_t)m_Port, m_PendingStart == 1, m_Transport);
        m_Message = ok ? "" : s.GetLastError();
        m_PendingStart = 0;
    }

    void MultiplayerPanel::OnImGuiRender(bool* open, bool playing)
    {
        ImGui::SetNextWindowSize(ImVec2(420, 560), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin(ICON_FA_TOWER_BROADCAST "  Multiplayer###Multiplayer", open))
        {
            ImGui::End();
            return;
        }
        DrawSessionControls(playing);
        DrawStatus();
        DrawClients();
        DrawObjects();
        DrawSteam();
        ImGui::End();
    }

    void MultiplayerPanel::DrawSessionControls(bool playing)
    {
        auto& s = NetworkSession::Get();
        if (const NetworkManagerComponent* mgr = FindManager(m_Scene.get()); mgr && !s.IsOnline() && m_Address[0] == '\0')
            std::snprintf(m_Address, sizeof(m_Address), "%s", mgr->Address.c_str());

        if (UI::BeginSection("Session", true, ICON_FA_PLUG))
        {
            const bool online = s.IsOnline();
            ImGui::BeginDisabled(online);
            static const char* transports[] = { "UDP (ENet)", "Steam P2P" };
            UI::Combo("Transport", m_Transport, transports, 2, "Steam P2P needs the Steam client and steam_api next to the editor/game.");
            UI::BeginProperty(m_Transport == 1 ? "Host SteamID" : "Address");
            ImGui::SetNextItemWidth(-1);
            ImGui::InputText("##addr", m_Address, sizeof(m_Address));
            UI::EndProperty();
            if (m_Transport == 0)
                UI::DragInt("Port", m_Port, 1.0f, 1, 65535);
            ImGui::EndDisabled();

            ImGui::Spacing();
            const float w = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 2.0f) / 3.0f;
            if (!online)
            {
                auto start = [&](int mode) {
                    m_Message.clear();
                    if (!playing)
                    {
                        // Unity Netcode gibi oturum Play modunda çalışır: önce Play'e bas
                        m_PendingStart = mode;
                        m_PendingFrames = 2;
                        if (m_RequestPlay)
                            m_RequestPlay();
                        return;
                    }
                    m_PendingStart = mode;
                    m_PendingFrames = 0;
                };
                if (UI::AccentButton(ICON_FA_HOUSE_SIGNAL " Host", ImVec2(w, 30))) start(1);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Server + local player%s", playing ? "" : " (enters Play mode)");
                ImGui::SameLine();
                if (ImGui::Button(ICON_FA_SERVER " Server", ImVec2(w, 30))) start(2);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Dedicated server without a local player");
                ImGui::SameLine();
                if (ImGui::Button(ICON_FA_RIGHT_TO_BRACKET " Join", ImVec2(w, 30))) start(3);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Connect to the address above");
            }
            else
            {
                if (UI::DangerButton(ICON_FA_POWER_OFF " Stop Session", ImVec2(-1, 30)))
                    s.Shutdown();
            }
            if (!m_Message.empty())
                UI::InfoBox(m_Message.c_str(), UI::InfoType::Error);
            else if (!online && !s.GetLastError().empty())
                UI::InfoBox(s.GetLastError().c_str(), UI::InfoType::Warning);
            if (!playing && !online)
                ImGui::TextDisabled("Sessions run in Play mode; the buttons press Play for you.");
            UI::EndSection();
        }
    }

    void MultiplayerPanel::DrawStatus()
    {
        auto& s = NetworkSession::Get();
        if (!UI::BeginSection("Status", true, ICON_FA_SIGNAL))
            return;
        const bool online = s.IsOnline();
        UI::StatusDot(online ? (s.IsConnected() ? UI::Colors::Success : UI::Colors::Warning) : UI::Colors::TextDim);
        ImGui::SameLine();
        ImGui::TextUnformatted(s.GetStatusText().c_str());
        if (online)
        {
            const auto& st = s.GetStats();
            UI::Text("Transport", "%s", s.GetTransportName());
            UI::Text("Local Client", "%u%s", s.GetLocalClientId(), s.IsHost() ? " (host)" : "");
            if (s.GetMode() == Net::NetMode::Client)
                UI::Text("Ping", "%.0f ms", st.RTTMs);
            UI::Text("Upload", "%.2f KB/s", st.SendKBps);
            UI::Text("Download", "%.2f KB/s", st.ReceiveKBps);
            UI::Text("Messages", "%llu sent / %llu received", (unsigned long long)st.MessagesSent, (unsigned long long)st.MessagesReceived);
            UI::Text("Network Objects", "%zu", s.GetObjectCount());
        }
        UI::EndSection();
    }

    void MultiplayerPanel::DrawClients()
    {
        auto& s = NetworkSession::Get();
        if (!s.IsServer())
            return;
        if (!UI::BeginSection("Clients", true, ICON_FA_USERS))
            return;
        const auto& clients = s.GetClients();
        if (clients.empty())
            ImGui::TextDisabled("Waiting for players...");
        else if (ImGui::BeginTable("##clients", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV))
        {
            ImGui::TableSetupColumn("Id", ImGuiTableColumnFlags_WidthFixed, 34.0f);
            ImGui::TableSetupColumn("Name");
            ImGui::TableSetupColumn("Ping", ImGuiTableColumnFlags_WidthFixed, 60.0f);
            ImGui::TableSetupColumn("Player", ImGuiTableColumnFlags_WidthFixed, 70.0f);
            ImGui::TableHeadersRow();
            for (const auto& c : clients)
            {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text("%u", c.ClientId);
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(c.Name.c_str());
                ImGui::TableNextColumn();
                if (c.Connection) ImGui::Text("%.0f ms", c.RTTMs);
                else ImGui::TextDisabled("local");
                ImGui::TableNextColumn();
                if (c.PlayerObject) ImGui::Text("#%llu", (unsigned long long)(c.PlayerObject & 0xFFFFFF));
                else ImGui::TextDisabled("-");
            }
            ImGui::EndTable();
        }
        UI::EndSection();
    }

    void MultiplayerPanel::DrawObjects()
    {
        auto& s = NetworkSession::Get();
        if (!s.IsOnline() || !m_Scene)
            return;
        if (!UI::BeginSection("Network Objects", false, ICON_FA_GLOBE))
            return;
        auto view = m_Scene->m_Registry.view<TagComponent, NetworkObjectComponent>();
        if (ImGui::BeginTable("##netobjs", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY, ImVec2(0, 180)))
        {
            ImGui::TableSetupColumn("Object");
            ImGui::TableSetupColumn("Owner", ImGuiTableColumnFlags_WidthFixed, 60.0f);
            ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_WidthFixed, 70.0f);
            ImGui::TableHeadersRow();
            for (auto e : view)
            {
                const auto& no = view.get<NetworkObjectComponent>(e);
                if (!no.Spawned)
                    continue;
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(view.get<TagComponent>(e).Tag.c_str());
                ImGui::TableNextColumn();
                ImGui::Text("%u", no.OwnerClientId);
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%s", no.Prefab.empty() ? "scene" : "spawned");
            }
            ImGui::EndTable();
        }
        UI::EndSection();
    }

    void MultiplayerPanel::DrawSteam()
    {
        if (!UI::BeginSection("Steam", false, ICON_FA_GAMEPAD))
            return;
        if (!SteamPlatform::IsAvailable())
        {
            if (ImGui::Button(ICON_FA_ROTATE " Connect to Steam", ImVec2(-1, 0)))
                SteamPlatform::Initialize();
            const std::string& err = SteamPlatform::GetLastError();
            UI::InfoBox(err.empty() ? "Steam is not initialized. Needs the Steam client, steam_api next to the editor and steam_appid.txt."
                                    : err.c_str(), UI::InfoType::Info);
            UI::EndSection();
            return;
        }
        UI::Text("Player", "%s", SteamPlatform::GetPersonaName().c_str());
        UI::Text("SteamID", "%llu", (unsigned long long)SteamPlatform::GetSteamID());
        const uint64_t lobby = SteamPlatform::GetCurrentLobby();
        if (lobby)
        {
            UI::Text("Lobby", "%llu", (unsigned long long)lobby);
            UI::Text("Owner", "%llu", (unsigned long long)SteamPlatform::GetLobbyOwner(lobby));
            if (ImGui::Button("Copy Lobby Id"))
                ImGui::SetClipboardText(std::to_string(lobby).c_str());
            ImGui::SameLine();
            if (ImGui::Button("Leave Lobby"))
                SteamPlatform::LeaveLobby();
        }
        else
        {
            UI::DragInt("Lobby Size", m_LobbySize, 1.0f, 1, 250);
            if (ImGui::Button(ICON_FA_PLUS " Create Lobby", ImVec2(-1, 0)))
                SteamPlatform::CreateLobby(m_LobbySize, true);
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 80.0f);
            ImGui::InputTextWithHint("##lobbyid", "Lobby id", m_LobbyId, sizeof(m_LobbyId), ImGuiInputTextFlags_CharsDecimal);
            ImGui::SameLine();
            if (ImGui::Button("Join", ImVec2(-1, 0)))
                SteamPlatform::JoinLobby(std::strtoull(m_LobbyId, nullptr, 10));
        }
        UI::EndSection();
    }
}
