#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <deque>
#include <string>
#include <unordered_map>

namespace Conqueror
{
    // Unity Netcode benzeri bileşenler. NetworkSession (Networking/Multiplayer) bunları çalışma zamanında işler.

    // Sahnedeki oturum ayarları (Unity: NetworkManager)
    struct NetworkManagerComponent
    {
        int Transport = 0;                 // 0 UDP (ENet), 1 Steam (P2P)
        std::string Address = "127.0.0.1";
        int Port = 7777;
        int MaxClients = 16;
        float TickRate = 30.0f;            // durum gönderim sıklığı (Hz)
        float InterpolationDelay = 0.1f;   // istemci ara değerleme gecikmesi (s)
        std::string PlayerPrefab;          // bağlanan her istemci için oluşturulur (sahibi istemci)
        glm::vec3 SpawnPoint{ 0.0f };
        float SpawnSpacing = 2.0f;         // oyuncular X ekseninde aralıklı
        int AutoStart = 0;                 // 0 yok, 1 Host, 2 Server, 3 Client (Play'de / derlenmiş oyunda)
    };

    // Ağ üzerinde eşlenen varlık (Unity: NetworkObject)
    struct NetworkObjectComponent
    {
        std::string Prefab;                // dinamik oluşturma için prefab (sahne nesnelerinde boş)
        bool DestroyWithOwner = true;

        // Çalışma zamanı (serileştirilmez)
        uint64_t NetworkId = 0;
        uint32_t OwnerClientId = 0;        // 0 = sunucu / host
        bool Spawned = false;
    };

    struct NetworkSnapshot
    {
        double Time = 0.0;
        glm::vec3 Position{ 0.0f };
        glm::quat Rotation{ 1.0f, 0.0f, 0.0f, 0.0f };
        glm::vec3 Scale{ 1.0f };
    };

    // Unity NetworkRigidbody: yetkisi olmayan kopyalarda gövde kinematiğe alınır (hareketi ağdan gelir)
    struct NetworkRigidbodyComponent
    {
        bool AutoUpdateKinematicState = true;   // kapalıysa uzak kopya fiziği kendi simüle eder
    };

    // Unity NetworkAnimator: Animation (durum makinesi) parametrelerini yetkiliden diğerlerine eşler
    struct NetworkAnimatorComponent
    {
        bool SyncFloats = true;
        bool SyncInts = true;
        bool SyncBools = true;
        bool SyncTriggers = true;
        float FloatThreshold = 0.01f;
        // Çalışma zamanı: son gönderilen değerler
        std::unordered_map<std::string, float> Sent;
    };

    // Dönüşüm eşleme (Unity: NetworkTransform)
    struct NetworkTransformComponent
    {
        bool SyncPosition = true;
        bool SyncRotation = true;
        bool SyncScale = false;
        int Authority = 0;                 // 0 Server, 1 Owner (istemci kendi nesnesini sürer)
        bool Interpolate = true;
        float PositionThreshold = 0.001f;  // bu kadar değişmeden gönderilmez
        float RotationThreshold = 0.1f;    // derece

        // Çalışma zamanı
        glm::vec3 LastSentPosition{ 1e30f };
        glm::quat LastSentRotation{ 0.0f, 0.0f, 0.0f, 0.0f };
        glm::vec3 LastSentScale{ 1e30f };
        double LastSendTime = -1.0;
        std::deque<NetworkSnapshot> Buffer; // istemcide gelen durumlar (zaman sıralı)
    };
}
