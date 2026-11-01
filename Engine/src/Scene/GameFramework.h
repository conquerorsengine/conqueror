#pragma once

#include "Core/Base/Base.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>

// Unreal oyun çerçevesi parçaları:
//  - Player Start: oyuncunun doğacağı nokta (isteğe bağlı etiket ile seçilir)
//  - Game Mode: oyun başlayınca varsayılan oyuncu prefabını (Default Pawn) bir Player Start'ta oluşturur,
//    kamerasını birincil yapar, betiklerden yeniden doğurma (RestartPlayer) sağlar
//  - Spring Arm: kamera kolu — çocuklarını (kamerayı) hedefin arkasında tutar, araya engel girerse kısalır,
//    konum / dönüş gecikmesiyle yumuşak takip
namespace Conqueror
{
    class Scene;

    struct PlayerStartComponent
    {
        std::string StartTag;            // boş değilse Game Mode yalnızca bu etiketi isteyen doğuşlarda kullanır
        bool Enabled = true;
    };

    enum class PlayerStartChoice : int { First = 0, Random, RoundRobin };

    struct GameModeComponent
    {
        std::string DefaultPawn;         // .cqprefab
        std::string PlayerStartTag;      // boş = herhangi bir Player Start
        PlayerStartChoice ChooseStart = PlayerStartChoice::First;
        bool SpawnOnStart = true;
        bool PossessCamera = true;       // pawn'ın (ya da çocuklarının) kamerasını birincil yap
        float RespawnDelay = 2.0f;

        // Çalışma zamanı
        uint32_t Pawn = 0xFFFFFFFFu;
        int Spawns = 0;
        float RespawnTimer = -1.0f;
        int NextStart = 0;
        bool PendingSpawnEvents = false;   // oyun başındaki doğuşun OnPlayerSpawned / OnPossessed olayları
    };

    struct SpringArmComponent
    {
        float TargetArmLength = 4.0f;
        glm::vec3 SocketOffset{ 0.0f, 0.0f, 0.0f };   // kol uzayında (kolun ucundan kayma)
        glm::vec3 TargetOffset{ 0.0f, 0.0f, 0.0f };   // dünya uzayında (kolun başlangıç noktası kayması)
        bool DoCollisionTest = true;
        float ProbeSize = 0.15f;
        bool EnableCameraLag = false;
        float CameraLagSpeed = 10.0f;
        bool UseControlRotation = false;               // true: ControlYaw / ControlPitch (betik sürer), false: kolun kendi dönüşü
        float ControlYaw = 0.0f;
        float ControlPitch = -15.0f;

        // Çalışma zamanı (bilgi)
        float CurrentLength = 0.0f;
        bool Colliding = false;
        glm::vec3 LaggedSocket{ 0.0f };
        bool HasLagged = false;
    };

    // Bone Attachment (Unreal socket): varlığı atasındaki iskeletli modelin bir kemiğine bağlar (silah, şapka, fener).
    // Her karede animasyondan sonra varlığın dünya dönüşümü = kemik * ofset olur (ölçeği korunur).
    struct BoneAttachmentComponent
    {
        std::string Bone = "hand_r";
        glm::vec3 PositionOffset{ 0.0f };     // kemik uzayında (metre)
        glm::vec3 RotationOffset{ 0.0f };     // kemik uzayında (derece)
    };

    namespace GameFramework
    {
        // Animasyonlar değerlendirildikten sonra (Scene::UpdateAnimators sonunda)
        CQ_API void UpdateBoneAttachments(Scene& scene);
        CQ_API void BeginPlay(Scene& scene);
        CQ_API void Update(Scene& scene, float dt);
        CQ_API void LateUpdate(Scene& scene, float dt);
        // Varsayılan pawn'ı yeniden oluştur (eskisi varsa silinir); oluşturulan varlık ya da NullEntity
        CQ_API uint32_t RestartPlayer(Scene& scene, const std::string& startTag = "");
        CQ_API uint32_t FindPlayerStart(Scene& scene, const std::string& startTag = "");
        CQ_API uint32_t GetPawn(Scene& scene);
        CQ_API void ScheduleRespawn(Scene& scene, float delay = -1.0f);
    }

    void RegisterGameFrameworkComponents();
}
