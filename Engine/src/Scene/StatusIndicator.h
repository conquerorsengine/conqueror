#pragma once

#include "Core/Base/Base.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

// Status Indicator (Godot 4.3 StatusIndicator; Unity / Unreal'da yok): oyun çalışırken sistem tepsisinde (bildirim alanı) simge.
// Simge, ipucu ve menü öğeleri; tıklayınca "pressed" sinyali (düğme, konum) ve betiklerde OnStatusIndicatorPressed, menü öğesi
// seçilince "menu_item_pressed" ve OnStatusMenuItemPressed. Linux / BSD: D-Bus StatusNotifierItem + com.canonical.dbusmenu
// (KDE, XFCE, LXQt, GNOME'da AppIndicator eklentisi), Windows: Shell_NotifyIcon. libdbus çalışma zamanında yüklenir.
namespace Conqueror
{
    class Scene;

    struct StatusIndicatorComponent
    {
        std::string Icon;                       // doku (boşsa motor simgesi)
        std::string Tooltip = "Game";
        bool Visible = true;
        std::vector<std::string> MenuItems;     // "-" ayraç

        // Çalışma zamanı
        void* Native = nullptr;
        bool Registered = false;                // tepsi barındırıcısına kaydoldu (yoksa simge görünmez)
        int Presses = 0;
        int LastButton = 0;                     // 1 sol, 2 sağ, 3 orta (Godot MouseButton)
        glm::vec2 LastPosition{ 0.0f };
        int LastMenuItem = -1;
    };

    namespace StatusIndicators
    {
        CQ_API void Update(Scene& scene);
        CQ_API void ForgetScene(Scene* scene);
        CQ_API bool Supported();
        // Test (otomasyon): kendi hizmetimize ayrı bir D-Bus bağlantısından istek gönderir; yanıtlar sonraki karelerde okunur
        CQ_API bool ProbeSend(Scene& scene, uint32_t entity, const std::string& request, int argument = 0);
        CQ_API bool ProbeValue(const std::string& key, std::string& out);
    }

    void RegisterStatusIndicatorComponents();
}
