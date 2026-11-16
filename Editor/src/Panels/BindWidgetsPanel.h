#pragma once

#include "Scene/Entity.h"

#include <string>
#include <vector>

// Bind Widgets (199, Unreal UMG'deki panel gibi; Unity / Godot'ta yok): canvas'a (ya da öğelerine) bağlı UI betiklerinin adıyla beklediği
// öğeler. Betik bildirimleri: "@bind Button StartButton" / "@bind? Image Icon" yorumu (her dil), C# [BindWidget] / [BindWidgetOptional],
// Java @BindWidget, BindWidget("StartButton", "Button") çağrısı. Eksik zorunlu öğe Play'i durdurur (Unreal'deki derleme hatası gibi);
// panel eksikleri işaretler ve tek tıkla oluşturur. UI Editor açılınca Hierarchy'nin yanında açılır.
namespace Conqueror::Editor
{
    struct WidgetBinding
    {
        std::string Name, Type, Script;
        bool Optional = false;
        int Status = 0;             // 0 bağlı, 1 eksik, 2 tür yanlış
        std::string FoundType;
    };

    class BindWidgetsPanel
    {
    public:
        static std::vector<WidgetBinding> Scan(Entity canvas);
        // Eksik zorunlu bağlar (Play öncesi denetim): ileti listesi
        static std::vector<std::string> MissingRequired(Entity canvas);
        void OnImGuiRender(bool* open, Entity canvas);
    };
}
