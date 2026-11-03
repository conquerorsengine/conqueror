#pragma once

#include "Core/Base/Base.h"
#include "Core/Input/InputActionRuntime.h"

#include <glm/glm.hpp>

#include <string>
#include <unordered_map>
#include <vector>

// Unity Input System bileşenleri:
//  - Player Input: bir Input Actions varlığının (.cqinputactions, "Actions" yuvası) ya da yuva boşsa proje girdi
//    haritasının (Project Settings > Input) bir eylem haritasını bu oyuncuya bağlar. Varlıkla: eylem gerçekleşince
//    (performed; Value eylemlerinde bırakılınca da) betiklere "On<Eylem>(değer)" ya da 2B eylemlerde
//    "On<Eylem>(x, y)" gönderilir (Send Messages: bu nesne, Broadcast Messages: çocuklarıyla birlikte). Haritada
//    Move / Look yoksa WASD / ok tuşları / sol çubuktan "OnMove(x, y)", fare / sağ çubuktan "OnLook(dx, dy)" üretilir.
//    Her oyuncu kendi gamepad'ini okur; Default Scheme seçilirse yalnız o şemanın bağlamaları kullanılır.
//  - Player Input Manager: yerel çok oyunculu: yeni bir gamepad (ya da klavye) düğmeye basınca oyuncu prefabı
//    oluşturulur ve o cihaz ona atanır; yöneticideki betiklere "OnPlayerJoined(index)" gönderilir.
namespace Conqueror
{
    class Scene;

    enum class PlayerInputBehavior : int { SendMessages = 0, None = 1, BroadcastMessages = 2 };

    struct PlayerInputComponent
    {
        // Input Actions varlığı (.cqinputactions); boşsa proje girdi haritası (Project Settings > Input) kullanılır
        std::string Actions;
        std::string ActionMap = "Gameplay";
        std::string DefaultScheme;        // kontrol şeması (boş: hepsi)
        int PlayerIndex = 0;              // okunacak gamepad (-1: yok)
        bool UseKeyboardMouse = true;     // klavye + fare bu oyuncuya mı
        PlayerInputBehavior Behavior = PlayerInputBehavior::SendMessages;
        bool GenerateMoveLook = true;     // OnMove / OnLook üret (haritada Move / Look yoksa)

        // Çalışma zamanı
        std::unordered_map<std::string, float> Values;
        std::unordered_map<std::string, float> Previous;
        glm::vec2 Move{ 0.0f }, Look{ 0.0f };
        glm::vec2 PrevMove{ 0.0f };
        InputActionPlayerState AssetState;   // Actions varlığıyla eylem durumları (aşamalar, etkileşimler)
    };

    enum class PlayerJoinBehavior : int { JoinOnButtonPress = 0, Manual };

    struct PlayerInputManagerComponent
    {
        std::string PlayerPrefab;
        int MaxPlayers = 4;
        PlayerJoinBehavior JoinBehavior = PlayerJoinBehavior::JoinOnButtonPress;
        bool AllowKeyboardPlayer = true;  // Enter / Space ile klavye oyuncusu katılır
        // Çalışma zamanı
        std::vector<int> JoinedDevices;   // gamepad kimlikleri (-1 klavye)
        int PlayerCount = 0;
    };

    namespace PlayerInputs
    {
        // Betiklerin Update'inden önce, her oyun karesinde
        CQ_API void Update(Scene& scene);
        CQ_API float GetValue(const PlayerInputComponent& c, const std::string& action);
        CQ_API bool WasPressed(const PlayerInputComponent& c, const std::string& action);
        CQ_API bool WasReleased(const PlayerInputComponent& c, const std::string& action);
        // Actions varlığıyla: eylemin 2B değeri, aşaması ve bu karedeki aşama geçişleri (yoksa nullptr)
        CQ_API const InputActionState* FindState(const PlayerInputComponent& c, const std::string& action);
        CQ_API glm::vec2 GetVector(const PlayerInputComponent& c, const std::string& action);
        // Eylem haritası / kontrol şeması değiştir (Unity SwitchCurrentActionMap / SwitchCurrentControlScheme)
        CQ_API void SwitchMap(PlayerInputComponent& c, const std::string& map);
        CQ_API void SwitchScheme(PlayerInputComponent& c, const std::string& scheme);
        // Yönetici: elle katılım (Manual) — prefab örneği döner (0 = başarısız)
        CQ_API uint32_t JoinPlayer(Scene& scene, uint32_t manager, int device);
    }

    void RegisterPlayerInputComponents();
}
