#include "SteamPlatform.h"
#include "Core/Logging/Log.h"
#include "Core/Project/Project.h"
#include "Core/Utils/DynamicLibrary.h"

#include <cstring>
#include <unordered_map>

// Steamworks "flat" C API'si dlopen ile çağrılır. Yapı düzenleri Steamworks SDK 1.5x-1.6x başlıklarından
// alınmıştır (steam_api_flat.h, steamnetworkingtypes.h, isteammatchmaking.h). Arayüz sürümleri SDK'ya göre
// değiştiğinden birkaç sürüm adı sırayla denenir.

namespace Conqueror
{
    namespace
    {
        using HSteamPipe = int32_t;
        using HSteamUser = int32_t;
        using SteamAPICall_t = uint64_t;

        // CallbackMsg_t (steam_api_common.h)
        struct CallbackMsg
        {
            HSteamUser User;
            int Callback;
            uint8_t* Param;
            int ParamSize;
        };

        // SteamAPICallCompleted_t (k_iSteamUtilsCallbacks + 3)
        struct APICallCompleted
        {
            SteamAPICall_t Call;
            int CallbackId;
            uint32_t ParamSize;
        };

        constexpr int kCallbackLobbyEnter = 504;               // k_iSteamMatchmakingCallbacks + 4
        constexpr int kCallbackLobbyMatchList = 510;           // + 10
        constexpr int kCallbackLobbyCreated = 513;             // + 13
        constexpr int kCallbackGameLobbyJoinRequested = 333;   // k_iSteamFriendsCallbacks + 33
        constexpr int kCallbackAPICallCompleted = 703;         // k_iSteamUtilsCallbacks + 3
        constexpr int kCallbackSessionRequest = 1251;          // k_iSteamNetworkingMessagesCallbacks + 1
        constexpr int kCallbackSessionFailed = 1252;           // + 2

        constexpr int kIdentityTypeSteamID = 16;
        constexpr size_t kIdentitySize = 136;                  // sizeof(SteamNetworkingIdentity)
        constexpr size_t kMessageIdentityOffset = 16;          // SteamNetworkingMessage_t::m_identityPeer

        struct alignas(8) Identity
        {
            uint8_t Bytes[kIdentitySize] = {};
        };

        using FnInitFlat = int (*)(char* errMsg);              // ESteamAPIInitResult SteamAPI_InitFlat(SteamErrMsg*)
        using FnInit = bool (*)();
        using FnVoid = void (*)();
        using FnBoolVoid = bool (*)();
        using FnGetPipe = HSteamPipe (*)();
        using FnManualRunFrame = void (*)(HSteamPipe);
        using FnManualNext = bool (*)(HSteamPipe, CallbackMsg*);
        using FnManualFree = void (*)(HSteamPipe);
        using FnManualCallResult = bool (*)(HSteamPipe, SteamAPICall_t, void*, int, int, bool*);
        using FnAccessor = void* (*)();
        using FnGetSteamID = uint64_t (*)(void*);
        using FnGetPersona = const char* (*)(void*);
        using FnCreateLobby = SteamAPICall_t (*)(void*, int, int);
        using FnJoinLobby = SteamAPICall_t (*)(void*, uint64_t);
        using FnLeaveLobby = void (*)(void*, uint64_t);
        using FnRequestLobbyList = SteamAPICall_t (*)(void*);
        using FnGetLobbyByIndex = uint64_t (*)(void*, int);
        using FnGetLobbyOwner = uint64_t (*)(void*, uint64_t);
        using FnSetLobbyData = bool (*)(void*, uint64_t, const char*, const char*);
        using FnGetLobbyData = const char* (*)(void*, uint64_t, const char*);
        using FnIdentitySet = void (*)(void*, uint64_t);
        using FnIdentityGet = uint64_t (*)(const void*);
        using FnSendToUser = int (*)(void*, const void*, const void*, uint32_t, int, int);
        using FnReceive = int (*)(void*, int, void**, int);
        using FnSessionUser = bool (*)(void*, const void*);
        using FnMessageRelease = void (*)(void*);
        using FnSessionInfo = int (*)(void*, const void*, void*, void*);
        using FnInitRelay = void (*)(void*);

        struct Api
        {
            DynamicLibrary Lib;
            bool Initialized = false;
            bool ManualDispatch = false;
            std::string Error;
            HSteamPipe Pipe = 0;

            FnVoid Shutdown = nullptr;
            FnVoid RunCallbacks = nullptr;
            FnGetPipe GetPipe = nullptr;
            FnManualRunFrame ManualRunFrame = nullptr;
            FnManualNext ManualNext = nullptr;
            FnManualFree ManualFree = nullptr;
            FnManualCallResult ManualCallResult = nullptr;

            void* User = nullptr;
            void* Friends = nullptr;
            void* Matchmaking = nullptr;
            void* Messages = nullptr;
            void* NetUtils = nullptr;

            FnGetSteamID GetSteamID = nullptr;
            FnGetPersona GetPersona = nullptr;
            FnCreateLobby CreateLobby = nullptr;
            FnJoinLobby JoinLobby = nullptr;
            FnLeaveLobby LeaveLobby = nullptr;
            FnRequestLobbyList RequestLobbyList = nullptr;
            FnGetLobbyByIndex GetLobbyByIndex = nullptr;
            FnGetLobbyOwner GetLobbyOwner = nullptr;
            FnSetLobbyData SetLobbyData = nullptr;
            FnGetLobbyData GetLobbyData = nullptr;
            FnIdentitySet IdentitySet = nullptr;
            FnIdentityGet IdentityGet = nullptr;
            FnSendToUser SendToUser = nullptr;
            FnReceive Receive = nullptr;
            FnSessionUser Accept = nullptr;
            FnSessionUser Close = nullptr;
            FnMessageRelease Release = nullptr;
            FnSessionInfo SessionInfo = nullptr;

            uint64_t CurrentLobby = 0;
            std::unordered_map<SteamAPICall_t, int> PendingCalls;  // çağrı -> beklenen geri çağrı kimliği
            SteamPlatform::Events Events;
            std::function<void(uint64_t)> SessionRequest;
            std::function<void(uint64_t)> SessionFailed;
        };

        Api& A()
        {
            static Api api;
            return api;
        }

        void* FindAccessor(const std::vector<const char*>& names)
        {
            for (const char* n : names)
                if (auto fn = A().Lib.Get<FnAccessor>(n))
                    if (void* iface = fn())
                        return iface;
            return nullptr;
        }

        Identity MakeIdentity(uint64_t steamId)
        {
            Identity id;
            if (A().IdentitySet)
            {
                A().IdentitySet(id.Bytes, steamId);
            }
            else
            {
                // Yedek: m_eType=SteamID, m_cbSize=8, birleşimde 64-bit kimlik
                int32_t type = kIdentityTypeSteamID, size = 8;
                std::memcpy(id.Bytes, &type, 4);
                std::memcpy(id.Bytes + 4, &size, 4);
                std::memcpy(id.Bytes + 8, &steamId, 8);
            }
            return id;
        }

        uint64_t IdentitySteamID(const void* identity)
        {
            if (A().IdentityGet)
                return A().IdentityGet(identity);
            uint64_t id = 0;
            std::memcpy(&id, (const uint8_t*)identity + 8, 8);
            return id;
        }

        std::vector<std::filesystem::path> LibraryCandidates()
        {
            const std::string file = DynamicLibrary::PlatformFileName("steam_api");
            std::vector<std::filesystem::path> c;
#ifdef _WIN32
            const std::string winFile = "steam_api64.dll";
            c.push_back(DynamicLibrary::ExecutableDirectory() / winFile);
#endif
            c.push_back(DynamicLibrary::ExecutableDirectory() / file);
            auto project = Project::GetActiveProjectDirectory();
            if (!project.empty())
            {
                c.push_back(project / "Plugins" / file);
#ifdef _WIN32
                c.push_back(project / "Plugins" / winFile);
#endif
            }
            if (const char* sdk = std::getenv("STEAMWORKS_SDK"))
            {
#if defined(_WIN32)
                c.push_back(std::filesystem::path(sdk) / "redistributable_bin" / "win64" / "steam_api64.dll");
#elif defined(__APPLE__)
                c.push_back(std::filesystem::path(sdk) / "redistributable_bin" / "osx" / file);
#else
                c.push_back(std::filesystem::path(sdk) / "redistributable_bin" / "linux64" / file);
#endif
            }
            c.push_back(file); // sistem yolu
            return c;
        }

        void HandleCallResult(const APICallCompleted& done)
        {
            Api& a = A();
            auto it = a.PendingCalls.find(done.Call);
            if (it == a.PendingCalls.end())
                return;
            const int expected = it->second;
            a.PendingCalls.erase(it);
            uint8_t buffer[256] = {};
            bool failed = false;
            if (!a.ManualCallResult || done.ParamSize > sizeof(buffer) ||
                !a.ManualCallResult(a.Pipe, done.Call, buffer, (int)done.ParamSize, expected, &failed) || failed)
            {
                if (expected == kCallbackLobbyCreated && a.Events.LobbyCreated)
                    a.Events.LobbyCreated(0);
                return;
            }
            if (expected == kCallbackLobbyCreated)
            {
                // LobbyCreated_t { EResult m_eResult; uint64 m_ulSteamIDLobby; } (Linux/mac 4, Windows 8 bayt paketleme)
                int32_t result = 0;
                std::memcpy(&result, buffer, 4);
#ifdef _WIN32
                const size_t off = 8;
#else
                const size_t off = 4;
#endif
                uint64_t lobby = 0;
                std::memcpy(&lobby, buffer + off, 8);
                if (result == 1 && lobby)
                    a.CurrentLobby = lobby;
                if (a.Events.LobbyCreated)
                    a.Events.LobbyCreated(result == 1 ? lobby : 0);
            }
            else if (expected == kCallbackLobbyEnter)
            {
                // LobbyEnter_t { uint64 lobby; uint32 permissions; bool locked; uint32 response; }
                uint64_t lobby = 0;
                std::memcpy(&lobby, buffer, 8);
                uint32_t response = 0;
                std::memcpy(&response, buffer + 16, 4);
                if (response == 1 && lobby)
                {
                    a.CurrentLobby = lobby;
                    if (a.Events.LobbyEntered)
                        a.Events.LobbyEntered(lobby, a.GetLobbyOwner && a.Matchmaking ? a.GetLobbyOwner(a.Matchmaking, lobby) : 0);
                }
                else if (a.Events.LobbyEntered)
                {
                    a.Events.LobbyEntered(0, 0);
                }
            }
            else if (expected == kCallbackLobbyMatchList)
            {
                uint32_t count = 0;
                std::memcpy(&count, buffer, 4);
                std::vector<uint64_t> lobbies;
                for (uint32_t i = 0; i < count && a.GetLobbyByIndex; ++i)
                    lobbies.push_back(a.GetLobbyByIndex(a.Matchmaking, (int)i));
                if (a.Events.LobbyList)
                    a.Events.LobbyList(lobbies);
            }
        }
    }

    bool SteamPlatform::Initialize()
    {
        Api& a = A();
        if (a.Initialized)
            return true;
        a.Error.clear();
        if (!a.Lib.IsOpen())
        {
            std::string err;
            if (!DynamicLibrary::OpenFirst(a.Lib, LibraryCandidates(), &err))
            {
                a.Error = "Steam API library not found (put " + DynamicLibrary::PlatformFileName("steam_api") +
                          " next to the game or in <project>/Plugins)";
                return false;
            }
        }

        bool ok = false;
        if (auto initFlat = a.Lib.Get<FnInitFlat>("SteamAPI_InitFlat"))
        {
            char msg[1024] = {};
            const int result = initFlat(msg);
            ok = result == 0;   // k_ESteamAPIInitResult_OK
            if (!ok)
                a.Error = std::string("Steam init failed: ") + (msg[0] ? msg : "is the Steam client running?");
        }
        else if (auto init = a.Lib.Get<FnInit>("SteamAPI_Init"))
        {
            ok = init();
            if (!ok)
                a.Error = "Steam init failed (is the Steam client running and steam_appid.txt present?)";
        }
        else
        {
            a.Error = "steam_api library has no SteamAPI_Init entry point";
        }
        if (!ok)
            return false;

        a.Shutdown = a.Lib.Get<FnVoid>("SteamAPI_Shutdown");
        a.RunCallbacks = a.Lib.Get<FnVoid>("SteamAPI_RunCallbacks");
        a.GetPipe = a.Lib.Get<FnGetPipe>("SteamAPI_GetHSteamPipe");
        a.ManualRunFrame = a.Lib.Get<FnManualRunFrame>("SteamAPI_ManualDispatch_RunFrame");
        a.ManualNext = a.Lib.Get<FnManualNext>("SteamAPI_ManualDispatch_GetNextCallback");
        a.ManualFree = a.Lib.Get<FnManualFree>("SteamAPI_ManualDispatch_FreeLastCallback");
        a.ManualCallResult = a.Lib.Get<FnManualCallResult>("SteamAPI_ManualDispatch_GetAPICallResult");
        if (auto manualInit = a.Lib.Get<FnVoid>("SteamAPI_ManualDispatch_Init"); manualInit && a.GetPipe && a.ManualRunFrame && a.ManualNext && a.ManualFree)
        {
            manualInit();
            a.ManualDispatch = true;
            a.Pipe = a.GetPipe();
        }

        a.User = FindAccessor({ "SteamAPI_SteamUser_v023", "SteamAPI_SteamUser_v022", "SteamAPI_SteamUser_v021", "SteamAPI_SteamUser_v020" });
        a.Friends = FindAccessor({ "SteamAPI_SteamFriends_v017", "SteamAPI_SteamFriends_v018" });
        a.Matchmaking = FindAccessor({ "SteamAPI_SteamMatchmaking_v009" });
        a.Messages = FindAccessor({ "SteamAPI_SteamNetworkingMessages_SteamAPI_v002" });
        a.NetUtils = FindAccessor({ "SteamAPI_SteamNetworkingUtils_SteamAPI_v004", "SteamAPI_SteamNetworkingUtils_SteamAPI_v003" });

        a.GetSteamID = a.Lib.Get<FnGetSteamID>("SteamAPI_ISteamUser_GetSteamID");
        a.GetPersona = a.Lib.Get<FnGetPersona>("SteamAPI_ISteamFriends_GetPersonaName");
        a.CreateLobby = a.Lib.Get<FnCreateLobby>("SteamAPI_ISteamMatchmaking_CreateLobby");
        a.JoinLobby = a.Lib.Get<FnJoinLobby>("SteamAPI_ISteamMatchmaking_JoinLobby");
        a.LeaveLobby = a.Lib.Get<FnLeaveLobby>("SteamAPI_ISteamMatchmaking_LeaveLobby");
        a.RequestLobbyList = a.Lib.Get<FnRequestLobbyList>("SteamAPI_ISteamMatchmaking_RequestLobbyList");
        a.GetLobbyByIndex = a.Lib.Get<FnGetLobbyByIndex>("SteamAPI_ISteamMatchmaking_GetLobbyByIndex");
        a.GetLobbyOwner = a.Lib.Get<FnGetLobbyOwner>("SteamAPI_ISteamMatchmaking_GetLobbyOwner");
        a.SetLobbyData = a.Lib.Get<FnSetLobbyData>("SteamAPI_ISteamMatchmaking_SetLobbyData");
        a.GetLobbyData = a.Lib.Get<FnGetLobbyData>("SteamAPI_ISteamMatchmaking_GetLobbyData");
        a.IdentitySet = a.Lib.Get<FnIdentitySet>("SteamAPI_SteamNetworkingIdentity_SetSteamID64");
        a.IdentityGet = a.Lib.Get<FnIdentityGet>("SteamAPI_SteamNetworkingIdentity_GetSteamID64");
        a.SendToUser = a.Lib.Get<FnSendToUser>("SteamAPI_ISteamNetworkingMessages_SendMessageToUser");
        a.Receive = a.Lib.Get<FnReceive>("SteamAPI_ISteamNetworkingMessages_ReceiveMessagesOnChannel");
        a.Accept = a.Lib.Get<FnSessionUser>("SteamAPI_ISteamNetworkingMessages_AcceptSessionWithUser");
        a.Close = a.Lib.Get<FnSessionUser>("SteamAPI_ISteamNetworkingMessages_CloseSessionWithUser");
        a.Release = a.Lib.Get<FnMessageRelease>("SteamAPI_SteamNetworkingMessage_t_Release");
        a.SessionInfo = a.Lib.Get<FnSessionInfo>("SteamAPI_ISteamNetworkingMessages_GetSessionConnectionInfo");
        if (auto relay = a.Lib.Get<FnInitRelay>("SteamAPI_ISteamNetworkingUtils_InitRelayNetworkAccess"); relay && a.NetUtils)
            relay(a.NetUtils);

        a.Initialized = true;
        CQ_CORE_INFO("[Steam] Initialized: {0} ({1})", GetPersonaName(), GetSteamID());
        return true;
    }

    void SteamPlatform::Shutdown()
    {
        Api& a = A();
        if (a.Initialized && a.Shutdown)
            a.Shutdown();
        a.Initialized = false;
        a.ManualDispatch = false;
        a.PendingCalls.clear();
        a.CurrentLobby = 0;
    }

    bool SteamPlatform::IsAvailable() { return A().Initialized; }
    const std::string& SteamPlatform::GetLastError() { return A().Error; }
    SteamPlatform::Events& SteamPlatform::GetEvents() { return A().Events; }
    std::function<void(uint64_t)>& SteamPlatform::OnSessionRequest() { return A().SessionRequest; }
    std::function<void(uint64_t)>& SteamPlatform::OnSessionFailed() { return A().SessionFailed; }

    void SteamPlatform::RunCallbacks()
    {
        Api& a = A();
        if (!a.Initialized)
            return;
        if (!a.ManualDispatch)
        {
            if (a.RunCallbacks)
                a.RunCallbacks();
            return;
        }
        a.ManualRunFrame(a.Pipe);
        CallbackMsg msg{};
        while (a.ManualNext(a.Pipe, &msg))
        {
            switch (msg.Callback)
            {
                case kCallbackAPICallCompleted:
                {
                    APICallCompleted done{};
                    std::memcpy(&done, msg.Param, std::min<size_t>(sizeof(done), (size_t)msg.ParamSize));
                    HandleCallResult(done);
                    break;
                }
                case kCallbackSessionRequest:
                    if (msg.ParamSize >= (int)kIdentitySize && a.SessionRequest)
                        a.SessionRequest(IdentitySteamID(msg.Param));
                    break;
                case kCallbackSessionFailed:
                    // SteamNetworkingMessagesSessionFailed_t { SteamNetConnectionInfo_t m_info; } -> m_identityRemote ilk alan
                    if (msg.ParamSize >= (int)kIdentitySize && a.SessionFailed)
                        a.SessionFailed(IdentitySteamID(msg.Param));
                    break;
                case kCallbackGameLobbyJoinRequested:
                {
                    // GameLobbyJoinRequested_t { CSteamID m_steamIDLobby; CSteamID m_steamIDFriend; }
                    uint64_t lobby = 0;
                    if (msg.ParamSize >= 8)
                        std::memcpy(&lobby, msg.Param, 8);
                    if (lobby && a.Events.JoinRequested)
                        a.Events.JoinRequested(lobby);
                    break;
                }
                default:
                    break;
            }
            a.ManualFree(a.Pipe);
        }
    }

    uint64_t SteamPlatform::GetSteamID()
    {
        Api& a = A();
        return a.Initialized && a.User && a.GetSteamID ? a.GetSteamID(a.User) : 0;
    }

    std::string SteamPlatform::GetPersonaName()
    {
        Api& a = A();
        if (!a.Initialized || !a.Friends || !a.GetPersona)
            return "";
        const char* n = a.GetPersona(a.Friends);
        return n ? n : "";
    }

    bool SteamPlatform::CreateLobby(int maxMembers, bool publicLobby)
    {
        Api& a = A();
        if (!a.Initialized || !a.Matchmaking || !a.CreateLobby || !a.ManualDispatch)
            return false;
        // ELobbyType: 0 private, 1 friends only, 2 public
        const SteamAPICall_t call = a.CreateLobby(a.Matchmaking, publicLobby ? 2 : 1, std::max(1, maxMembers));
        if (!call)
            return false;
        a.PendingCalls[call] = kCallbackLobbyCreated;
        return true;
    }

    bool SteamPlatform::JoinLobby(uint64_t lobbyId)
    {
        Api& a = A();
        if (!a.Initialized || !a.Matchmaking || !a.JoinLobby || !a.ManualDispatch || !lobbyId)
            return false;
        const SteamAPICall_t call = a.JoinLobby(a.Matchmaking, lobbyId);
        if (!call)
            return false;
        a.PendingCalls[call] = kCallbackLobbyEnter;
        return true;
    }

    void SteamPlatform::LeaveLobby()
    {
        Api& a = A();
        if (a.Initialized && a.Matchmaking && a.LeaveLobby && a.CurrentLobby)
            a.LeaveLobby(a.Matchmaking, a.CurrentLobby);
        a.CurrentLobby = 0;
    }

    bool SteamPlatform::RequestLobbyList()
    {
        Api& a = A();
        if (!a.Initialized || !a.Matchmaking || !a.RequestLobbyList || !a.ManualDispatch)
            return false;
        const SteamAPICall_t call = a.RequestLobbyList(a.Matchmaking);
        if (!call)
            return false;
        a.PendingCalls[call] = kCallbackLobbyMatchList;
        return true;
    }

    uint64_t SteamPlatform::GetCurrentLobby() { return A().CurrentLobby; }

    uint64_t SteamPlatform::GetLobbyOwner(uint64_t lobbyId)
    {
        Api& a = A();
        return a.Initialized && a.Matchmaking && a.GetLobbyOwner && lobbyId ? a.GetLobbyOwner(a.Matchmaking, lobbyId) : 0;
    }

    bool SteamPlatform::SetLobbyData(const std::string& key, const std::string& value)
    {
        Api& a = A();
        return a.Initialized && a.Matchmaking && a.SetLobbyData && a.CurrentLobby && a.SetLobbyData(a.Matchmaking, a.CurrentLobby, key.c_str(), value.c_str());
    }

    std::string SteamPlatform::GetLobbyData(uint64_t lobbyId, const std::string& key)
    {
        Api& a = A();
        if (!a.Initialized || !a.Matchmaking || !a.GetLobbyData || !lobbyId)
            return "";
        const char* v = a.GetLobbyData(a.Matchmaking, lobbyId, key.c_str());
        return v ? v : "";
    }

    bool SteamPlatform::SendP2P(uint64_t steamId, const void* data, uint32_t size, bool reliable, int channel)
    {
        Api& a = A();
        if (!a.Initialized || !a.Messages || !a.SendToUser)
            return false;
        const Identity id = MakeIdentity(steamId);
        // k_nSteamNetworkingSend_Reliable(8) | AutoRestartBrokenSession(32); güvenilmez: NoNagle(1) | AutoRestart
        const int flags = reliable ? (8 | 32) : (1 | 32);
        return a.SendToUser(a.Messages, id.Bytes, data, size, flags, channel) == 1;   // k_EResultOK
    }

    void SteamPlatform::ReceiveP2P(int channel, std::vector<std::pair<uint64_t, std::vector<uint8_t>>>& out)
    {
        Api& a = A();
        if (!a.Initialized || !a.Messages || !a.Receive || !a.Release)
            return;
        void* messages[64];
        for (;;)
        {
            const int n = a.Receive(a.Messages, channel, messages, 64);
            if (n <= 0)
                break;
            for (int i = 0; i < n; ++i)
            {
                const uint8_t* m = static_cast<const uint8_t*>(messages[i]);
                const uint8_t* data = nullptr;
                int size = 0;
                std::memcpy(&data, m, sizeof(void*));
                std::memcpy(&size, m + sizeof(void*), sizeof(int));
                const uint64_t sender = IdentitySteamID(m + kMessageIdentityOffset);
                if (data && size > 0)
                    out.emplace_back(sender, std::vector<uint8_t>(data, data + size));
                a.Release(messages[i]);
            }
            if (n < 64)
                break;
        }
    }

    void SteamPlatform::AcceptP2PSession(uint64_t steamId)
    {
        Api& a = A();
        if (a.Initialized && a.Messages && a.Accept)
        {
            const Identity id = MakeIdentity(steamId);
            a.Accept(a.Messages, id.Bytes);
        }
    }

    void SteamPlatform::CloseP2PSession(uint64_t steamId)
    {
        Api& a = A();
        if (a.Initialized && a.Messages && a.Close)
        {
            const Identity id = MakeIdentity(steamId);
            a.Close(a.Messages, id.Bytes);
        }
    }

    float SteamPlatform::GetP2PPing(uint64_t steamId)
    {
        Api& a = A();
        if (!a.Initialized || !a.Messages || !a.SessionInfo)
            return 0.0f;
        const Identity id = MakeIdentity(steamId);
        // SteamNetConnectionRealTimeStatus_t { ESteamNetworkingConnectionState m_eState; int m_nPing; ... }
        alignas(8) uint8_t status[512] = {};
        a.SessionInfo(a.Messages, id.Bytes, nullptr, status);
        int ping = 0;
        std::memcpy(&ping, status + 4, 4);
        return ping > 0 ? (float)ping : 0.0f;
    }
}
