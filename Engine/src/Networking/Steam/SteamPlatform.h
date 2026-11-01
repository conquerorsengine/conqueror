#pragma once

#include "Core/Base/Base.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace Conqueror
{
    // Steamworks entegrasyonu. steam_api kitaplığı (libsteam_api.so / steam_api64.dll / libsteam_api.dylib)
    // çalışma zamanında yüklenir; SDK derleme bağımlılığı yoktur. Steam istemcisi çalışmıyorsa ya da
    // kitaplık bulunamazsa her şey zararsızca devre dışı kalır (IsAvailable() == false).
    //
    // Geliştirmede oyun klasörüne steam_appid.txt (ör. 480 = Spacewar test uygulaması) konmalıdır.
    class CQ_API SteamPlatform
    {
    public:
        // Kitaplığı yükler ve SteamAPI'yi başlatır (tekrar çağrılabilir)
        static bool Initialize();
        static void Shutdown();
        static bool IsAvailable();
        static const std::string& GetLastError();

        // Her karede: geri çağrıları işler (lobi sonuçları, P2P oturum istekleri)
        static void RunCallbacks();

        static uint64_t GetSteamID();
        static std::string GetPersonaName();

        // ── Lobiler ──
        // Sonuç geri çağrı ile gelir: lobi kimliği (başarısızsa 0)
        static bool CreateLobby(int maxMembers, bool publicLobby);
        static bool JoinLobby(uint64_t lobbyId);
        static void LeaveLobby();
        static bool RequestLobbyList();
        static uint64_t GetCurrentLobby();
        static uint64_t GetLobbyOwner(uint64_t lobbyId);
        static bool SetLobbyData(const std::string& key, const std::string& value);
        static std::string GetLobbyData(uint64_t lobbyId, const std::string& key);

        struct Events
        {
            std::function<void(uint64_t lobbyId)> LobbyCreated;
            std::function<void(uint64_t lobbyId, uint64_t ownerId)> LobbyEntered;
            std::function<void(const std::vector<uint64_t>& lobbies)> LobbyList;
            std::function<void(uint64_t lobbyId)> JoinRequested;   // arkadaş daveti / overlay "Join game"
        };
        static Events& GetEvents();

        // ── P2P mesajları (ISteamNetworkingMessages) — NetTransportSteam kullanır ──
        static bool SendP2P(uint64_t steamId, const void* data, uint32_t size, bool reliable, int channel);
        // Bir kanaldaki bekleyen mesajları alır: (gönderen, veri)
        static void ReceiveP2P(int channel, std::vector<std::pair<uint64_t, std::vector<uint8_t>>>& out);
        static void AcceptP2PSession(uint64_t steamId);
        static void CloseP2PSession(uint64_t steamId);
        static float GetP2PPing(uint64_t steamId);
        // Oturum isteği / hata bildirimleri (taşıyıcı bağlantı olaylarına çevirir)
        static std::function<void(uint64_t steamId)>& OnSessionRequest();
        static std::function<void(uint64_t steamId)>& OnSessionFailed();
    };
}
