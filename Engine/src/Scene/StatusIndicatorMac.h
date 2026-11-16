#pragma once

#include <string>
#include <vector>

// Status Indicator'ın macOS arka ucu (StatusIndicatorMac.mm, yalnız macOS; iOS'ta menü çubuğu yok): menü çubuğunda NSStatusItem.
// Yalnız AppKit yapıştırıcısı; bileşenle eşleme, sinyaller ve betik çağrıları StatusIndicator.cpp'de (Linux / Windows ile ortak).
// Bütün işlevler ana iş parçacığından çağrılır (AppKit kuralı); tıklamalar ana döngüde (glfwPollEvents -> NSApp) birikir.
namespace Conqueror::StatusIndicatorMac
{
    struct Item;

    struct Event
    {
        int Kind = 0;        // 0 tıklama, 1 menü öğesi
        int Button = 0;      // 1 sol, 2 sağ (Ctrl+sol da), 3 orta
        int X = 0, Y = 0;    // fare, masaüstü noktası (sol üst kök: menü çubuklu ekran)
        int Index = -1;      // menü öğesi (ayraçlar dahil sıra)
    };

    struct State
    {
        std::string Tooltip;
        bool Visible = false;
        bool InStatusBar = false;   // öğe görünür ve düğmesi menü çubuğunun ekrandaki penceresinde
        std::string Window;         // düğmenin penceresi: none / hidden / shown
        int IconPixels = 0;         // simgenin piksel genişliği
        int MenuItems = 0;          // ayraçlar dahil
    };

    bool IsMainThread();
    Item* Create();
    void Destroy(Item* item);
    void SetTooltip(Item* item, const std::string& tooltip);
    void SetVisible(Item* item, bool visible);
    void SetIcon(Item* item, const unsigned char* rgba, int width, int height);   // RGBA, üstten satır satır, ön çarpımsız
    void SetMenu(Item* item, const std::vector<std::string>& labels);              // "-" ayraç
    bool InStatusBar(Item* item);
    std::vector<Event> TakeEvents(Item* item);

    // Test (otomasyon): düğmenin hedef / eylemi (performClick) belirtilen fare düğmesiyle, menü öğesinin eylemi
    // (performActionForItemAtIndex); sağ tıkta kalıcı menü açılmaz
    bool Click(Item* item, int button);
    bool ChooseMenuItem(Item* item, int index);
    State Read(Item* item);
}
