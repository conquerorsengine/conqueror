#pragma once

#include "Core/Base/Base.h"

#include <memory>
#include <string>

namespace Conqueror
{
    class Texture2D;

    // Ekrandaki tuş simgeleri (Steam Input / Unity Input System glyph karşılığı). Son kullanılan aygıt (klavye-fare
    // ya da kumanda) ve kumandanın türü (Xbox, PlayStation, Nintendo, Steam Deck) her karede izlenir; arayüz
    // "Zıpla: <glyph=a|key_space>" gibi yazıldığında oyuncunun elindeki aygıtın simgesi çizilir.
    // Simgeler: Resources/Textures/Glyphs/<stil>/<ad>.png (Tools/Glyphs/make_glyphs.py üretir).
    namespace InputGlyphs
    {
        enum class Style : int { Keyboard = 0, Xbox, PlayStation, Nintendo, SteamDeck };

        // Her kare (Input::Update sonrası): son kullanılan aygıtı günceller
        CQ_API void Update();
        CQ_API bool UsingGamepad();
        // Bağlı kumandanın türü (adından; Steam Deck'te Steam'in sanal kumandası da Deck sayılır)
        CQ_API Style GamepadStyle(int gamepadID = 0);
        // Şu an gösterilecek stil (klavye ya da kumanda türü); input.GlyphStyle cvar'ı ile sabitlenebilir
        CQ_API Style ActiveStyle();
        CQ_API const char* StyleName(Style style);
        CQ_API bool ParseStyle(const std::string& name, Style& out);
        // Deneme / ayarlar için: stil sabitle ("auto" = otomatik)
        CQ_API void SetForcedStyle(const std::string& name);
        CQ_API std::string GetForcedStyle();

        // Simge belirteci: "a", "dpad_up", "key_space", "mouse_left", "a|key_space" (kumanda | klavye) ya da bir
        // Input eylem adı (ör. "Jump": eylemin ilk kumanda / klavye bağı). Simge dosyasının yolu, yoksa boş.
        CQ_API std::string Resolve(const std::string& spec);
        CQ_API std::shared_ptr<Texture2D> Texture(const std::string& spec);
        // Yazı karşılığı (simge çizilemeyen yerler için): "A", "Cross", "Space" ...
        CQ_API std::string Label(const std::string& spec);

        // Doku önbelleğini bırakır (GL bağlamı kapanmadan, Application yıkıcısında)
        CQ_API void Shutdown();

        // Konsol değişkeni input.GlyphStyle
        CQ_API void RegisterConsoleVariables();
    }
}
