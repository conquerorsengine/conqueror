#pragma once

#include "Core/Base/Base.h"
#include "Scene/Entity.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Conqueror
{
    class Scene;
    namespace Scripting { class NativeRegistry; }

    // UI Document (Unity UI Toolkit karşılığı): .cquxml belgesi + .cquss stilleri oyun başında Canvas öğelerine kurulur;
    // yerleşim flexbox ile hesaplanır, öğeler betikten UIQuery ile bulunur.
    struct UIDocumentComponent
    {
        std::string Source;             // .cquxml
        std::string PanelSettings;      // .cqpanelsettings
        int SortOrder = 0;

        // Çalışma zamanı
        struct Item
        {
            uint32_t Entity = 0;
            std::string Type, Name;
            std::vector<std::string> Classes;
        };
        bool Built = false;
        std::vector<Item> Items;
    };

    // GUI Skin bileşeni: Canvas'a eklenir, altındaki UI öğelerine oyun başında uygulanır
    struct UISkinComponent
    {
        std::string Skin;               // .cqguiskin
        bool Applied = false;
    };

    namespace UIToolkit
    {
        constexpr const char* DocumentExtension = ".cquxml";
        constexpr const char* StyleExtension = ".cquss";
        CQ_API void Update(Scene& scene);
        // "#ad", ".sinif" ya da "Tür": eşleşen ilk öğe
        CQ_API Entity Query(Scene& scene, Entity document, const std::string& selector);
        CQ_API std::string NewDocumentTemplate();
        CQ_API std::string NewStyleTemplate();
        void RegisterComponents();
        void RegisterNatives(Scripting::NativeRegistry* vm);
    }
}
