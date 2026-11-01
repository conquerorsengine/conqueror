#pragma once

#include <functional>
#include <memory>
#include <string>

namespace Conqueror
{
    class Scene;
}

namespace Conqueror::Editor
{
    // Window > Multiplayer: Play modunda host / sunucu / istemci başlatma, bağlantı durumu, istemciler,
    // ağ nesneleri, bant genişliği ve Steam lobileri.
    class MultiplayerPanel
    {
    public:
        void SetContext(const std::shared_ptr<Scene>& scene) { m_Scene = scene; }
        void OnImGuiRender(bool* open, bool playing);

        // Play'e basıldıktan sonra çalıştırılacak eylem (ör. "Play as Host")
        void SetRequestPlay(std::function<void()> fn) { m_RequestPlay = std::move(fn); }
        // Play başladıktan sonra her karede çağrılır: bekleyen başlatma eylemini uygular
        void Update(bool playing);

    private:
        void DrawSessionControls(bool playing);
        void DrawStatus();
        void DrawClients();
        void DrawObjects();
        void DrawSteam();

        std::shared_ptr<Scene> m_Scene;
        std::function<void()> m_RequestPlay;
        char m_Address[128] = "127.0.0.1";
        int m_Port = 7777;
        int m_Transport = 0;
        int m_PendingStart = 0;           // Play'den sonra: 1 host, 2 server, 3 client
        int m_PendingFrames = 0;
        std::string m_Message;
        char m_LobbyId[32] = "";
        int m_LobbySize = 4;
    };
}
