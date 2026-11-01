#pragma once

#include "Core/Base/Base.h"

#include <glm/glm.hpp>

#include <string>
#include <unordered_map>
#include <vector>

// Unity Input System bileşenleri:
//  - Player Input: proje girdi haritasının (.cqinput) bir bağlamını bu oyuncuya bağlar; eylem değişince aynı
//    varlıktaki betiklere "On<Eylem>(değer)" mesajı gönderir (Send Messages). WASD / ok tuşları / sol çubuktan
//    "OnMove(x, y)", fare / sağ çubuktan "OnLook(dx, dy)" otomatik üretilir. Her oyuncu kendi gamepad'ini okur.
//  - Player Input Manager: yerel çok oyunculu: yeni bir gamepad (ya da klavye) düğmeye basınca oyuncu prefabı
//    oluşturulur ve o cihaz ona atanır; yöneticideki betiklere "OnPlayerJoined(index)" gönderilir.
namespace Conqueror
{
    class Scene;

    enum class PlayerInputBehavior : int { SendMessages = 0, None };

    struct PlayerInputComponent
    {
        std::string ActionMap = "Gameplay";
        int PlayerIndex = 0;              // okunacak gamepad (-1: yok)
        bool UseKeyboardMouse = true;     // klavye + fare bu oyuncuya mı
        PlayerInputBehavior Behavior = PlayerInputBehavior::SendMessages;
        bool GenerateMoveLook = true;     // OnMove / OnLook üret (haritada Move / Look yoksa)

        // Çalışma zamanı
        std::unordered_map<std::string, float> Values;
        std::unordered_map<std::string, float> Previous;
        glm::vec2 Move{ 0.0f }, Look{ 0.0f };
        glm::vec2 PrevMove{ 0.0f };
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
        // Yönetici: elle katılım (Manual) — prefab örneği döner (0 = başarısız)
        CQ_API uint32_t JoinPlayer(Scene& scene, uint32_t manager, int device);
    }

    void RegisterPlayerInputComponents();
}
