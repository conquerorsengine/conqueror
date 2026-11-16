#pragma once

#include "Core/Base/Base.h"
#include "Scene/Entity.h"

#include <string>

// Property Binding (199, Unreal UMG "Bind" / Unity 6 veri bağlama): bir UI öğesinin özelliği başka bir varlığın bileşen alanına
// bağlanır ve her karede ondan okunur (ör. Text <- "Player.Health.Current", Slider Value <- "Player.Health.Current"). Kaynak yolu
// "<Varlık>.<Bileşen>.<Alan>"; bileşen ve alan Inspector'daki adları ya da betik / YAML adlarıyla yazılabilir.
namespace Conqueror
{
    class Scene;

    struct UIBindingComponent
    {
        std::string Text;           // UI Text'in yazısı
        std::string Format = "{0}"; // Text biçimi: {0} kaynak değeri
        std::string Value;          // Slider / Scrollbar değeri
        std::string Fill;           // Image Fill Amount (0..1)
        std::string Color;          // Image / Text rengi (r g b a)
        std::string Active;         // öğe görünür mü (bool)
        std::string Interactable;   // seçilebilir öğe etkileşimli mi (bool)
    };

    namespace UIBindings
    {
        // Kaynak yolunu çöz: değer metni (bulunamazsa false, error neden)
        CQ_API bool Resolve(Scene& scene, const std::string& source, std::string& value, std::string* error = nullptr);
        // Bağlı özellikleri kaynaklardan güncelle (UI yerleşiminden önce, her kare)
        CQ_API void Update(Scene& scene);
        void RegisterComponents();
    }
}
