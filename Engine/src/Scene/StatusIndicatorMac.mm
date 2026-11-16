// Godot StatusIndicator, macOS: menü çubuğunda NSStatusItem (simge, ipucu, görünürlük, sağ tıkta NSMenu). Godot'un macOS
// uygulaması gibi düğme fare inişinde eylem gönderir. Yalnız macOS'ta derlenir (iOS'ta menü çubuğu yok); ARC açık (-fobjc-arc)
#include "Scene/StatusIndicatorMac.h"

#import <AppKit/AppKit.h>

#include <algorithm>
#include <cmath>

@class CQStatusIndicatorTarget;

namespace Conqueror::StatusIndicatorMac
{
    struct Item
    {
        NSStatusItem* Status = nil;
        NSMenu* Menu = nil;                      // yoksa sağ tık yalnız "pressed" verir
        CQStatusIndicatorTarget* Target = nil;   // düğmenin ve menü öğelerinin hedefi (onlar hedefi zayıf tutar)
        std::vector<Event> Events;
        int SyntheticButton = 0;                 // otomasyonun performClick'i: fare düğmesi olaydan değil buradan
    };
}

namespace SIM = Conqueror::StatusIndicatorMac;

namespace
{
    NSString* Text(const std::string& s)
    {
        NSString* text = [NSString stringWithUTF8String:s.c_str()];
        return text ? text : @"";
    }

    // Fare konumu: Cocoa'nın sol alt kökünden GLFW'nin masaüstü noktalarına (sol üst kök, menü çubuklu ekran)
    void MousePosition(SIM::Event& e)
    {
        const NSPoint p = [NSEvent mouseLocation];
        NSScreen* primary = [NSScreen screens].firstObject;
        const CGFloat top = primary ? NSMaxY(primary.frame) : 0.0;
        e.X = (int)std::lround(p.x);
        e.Y = (int)std::lround(top - p.y);
    }
}

@interface CQStatusIndicatorTarget : NSObject
@property(nonatomic, assign) SIM::Item* owner;
- (void)clicked:(id)sender;
- (void)menuItemChosen:(NSMenuItem*)sender;
@end

@implementation CQStatusIndicatorTarget

- (void)clicked:(id)sender
{
    (void)sender;
    SIM::Item* item = self.owner;
    if (!item)
        return;
    int button = item->SyntheticButton;
    if (!button)
    {
        button = 1;   // Godot gibi: olay fare olayı değilse (ör. erişilebilirlik ile basma) sol
        NSEvent* event = [NSApp currentEvent];
        if (event)
        {
            switch (event.type)
            {
                case NSEventTypeRightMouseDown:
                case NSEventTypeRightMouseUp:
                    button = 2;
                    break;
                case NSEventTypeOtherMouseDown:
                case NSEventTypeOtherMouseUp:
                    button = event.buttonNumber == 2 ? 3 : 0;   // yan düğmeler Linux / Windows tepsisinde de yok
                    break;
                case NSEventTypeLeftMouseDown:
                case NSEventTypeLeftMouseUp:
                    if (event.modifierFlags & NSEventModifierFlagControl)
                        button = 2;   // Ctrl+tık macOS'ta sağ tıktır
                    break;
                default:
                    break;
            }
        }
    }
    if (!button)
        return;
    SIM::Event e;
    e.Button = button;
    MousePosition(e);
    item->Events.push_back(e);
    // Sağ tık: menü düğmenin altında (Windows'taki gibi; seçim menuItemChosen ile gelir). Otomasyonun tıklaması kalıcı menü açmaz
    if (button == 2 && item->Menu && !item->SyntheticButton)
    {
        NSStatusBarButton* view = item->Status.button;
        [item->Menu popUpMenuPositioningItem:nil atLocation:NSMakePoint(0.0, NSHeight(view.bounds) + 4.0) inView:view];
    }
}

- (void)menuItemChosen:(NSMenuItem*)sender
{
    SIM::Item* item = self.owner;
    if (!item)
        return;
    SIM::Event e;
    e.Kind = 1;
    e.Index = (int)sender.tag;
    item->Events.push_back(e);
}

@end

namespace Conqueror::StatusIndicatorMac
{
    bool IsMainThread()
    {
        return [NSThread isMainThread];
    }

    Item* Create()
    {
        [NSApplication sharedApplication];   // GLFW zaten açar; menü çubuğu bağlantısı uygulama nesnesini ister
        NSStatusItem* status = [[NSStatusBar systemStatusBar] statusItemWithLength:NSSquareStatusItemLength];
        if (!status)
            return nullptr;
        auto* item = new Item();
        item->Status = status;
        item->Target = [[CQStatusIndicatorTarget alloc] init];
        item->Target.owner = item;
        NSStatusBarButton* button = status.button;
        button.imagePosition = NSImageOnly;
        button.imageScaling = NSImageScaleProportionallyUpOrDown;
        button.target = item->Target;
        button.action = @selector(clicked:);
        [button sendActionOn:NSEventMaskLeftMouseDown | NSEventMaskRightMouseDown | NSEventMaskOtherMouseDown];
        return item;
    }

    void Destroy(Item* item)
    {
        if (!item)
            return;
        auto destroy = ^{
            item->Target.owner = nullptr;
            if (item->Status)
            {
                item->Status.button.target = nil;
                [[NSStatusBar systemStatusBar] removeStatusItem:item->Status];
            }
            delete item;   // ARC: öğe, menü ve hedef bırakılır
        };
        if ([NSThread isMainThread])
            destroy();
        else
            dispatch_async(dispatch_get_main_queue(), destroy);   // AppKit nesnelerine yalnız ana iş parçacığı dokunur
    }

    void SetTooltip(Item* item, const std::string& tooltip)
    {
        if (item)
            item->Status.button.toolTip = tooltip.empty() ? nil : Text(tooltip);
    }

    void SetVisible(Item* item, bool visible)
    {
        // Görünürlük her zaman açıkça verilir: AppKit gizlenmiş öğeyi kullanıcı ayarlarına yazar, sonraki açılışta da gizli gelirdi
        if (item)
            [item->Status setVisible:visible ? YES : NO];
    }

    void SetIcon(Item* item, const unsigned char* rgba, int width, int height)
    {
        if (!item)
            return;
        NSImage* image = nil;
        NSBitmapImageRep* rep = nil;
        if (rgba && width > 0 && height > 0)
            rep = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:nullptr pixelsWide:width pixelsHigh:height bitsPerSample:8
                                                     samplesPerPixel:4 hasAlpha:YES isPlanar:NO colorSpaceName:NSDeviceRGBColorSpace
                                                         bytesPerRow:width * 4 bitsPerPixel:32];
        if (rep)
        {
            // NSBitmapImageRep varsayılanı ön çarpımlı alfa (Godot'un macOS simgesi gibi)
            unsigned char* dst = rep.bitmapData;
            const NSInteger stride = rep.bytesPerRow;
            for (int y = 0; y < height; ++y)
                for (int x = 0; x < width; ++x)
                {
                    const unsigned char* s = rgba + ((size_t)y * width + x) * 4;
                    unsigned char* d = dst + y * stride + x * 4;
                    const unsigned a = s[3];
                    d[0] = (unsigned char)(s[0] * a / 255);
                    d[1] = (unsigned char)(s[1] * a / 255);
                    d[2] = (unsigned char)(s[2] * a / 255);
                    d[3] = (unsigned char)a;
                }
            // Menü çubuğu ~22 pt: simge 18 pt'ye sığar, pikseller Retina ekranda keskinlik için korunur
            const CGFloat side = 18.0, longest = (CGFloat)std::max(width, height);
            const NSSize size = NSMakeSize(side * width / longest, side * height / longest);
            rep.size = size;
            image = [[NSImage alloc] initWithSize:size];
            [image addRepresentation:rep];
        }
        item->Status.button.image = image;
    }

    void SetMenu(Item* item, const std::vector<std::string>& labels)
    {
        if (!item)
            return;
        if (labels.empty())
        {
            item->Menu = nil;
            return;
        }
        NSMenu* menu = [[NSMenu alloc] initWithTitle:@""];
        menu.autoenablesItems = NO;
        for (size_t i = 0; i < labels.size(); ++i)
        {
            if (labels[i] == "-")
            {
                [menu addItem:[NSMenuItem separatorItem]];
                continue;
            }
            NSMenuItem* entry = [[NSMenuItem alloc] initWithTitle:Text(labels[i]) action:@selector(menuItemChosen:) keyEquivalent:@""];
            entry.target = item->Target;
            entry.tag = (NSInteger)i;   // Linux / Windows gibi ayraçlar dahil sıra
            [menu addItem:entry];
        }
        item->Menu = menu;
    }

    bool InStatusBar(Item* item)
    {
        if (!item || !item->Status || ![item->Status isVisible])
            return false;
        NSWindow* window = item->Status.button.window;
        return window != nil && [window isVisible];
    }

    std::vector<Event> TakeEvents(Item* item)
    {
        std::vector<Event> events;
        if (item)
            events.swap(item->Events);
        return events;
    }

    bool Click(Item* item, int button)
    {
        if (!item || !item->Status.button)
            return false;
        item->SyntheticButton = button;
        [item->Status.button performClick:nil];
        item->SyntheticButton = 0;
        return true;
    }

    bool ChooseMenuItem(Item* item, int index)
    {
        if (!item || !item->Menu || index < 0 || index >= item->Menu.numberOfItems)
            return false;
        [item->Menu performActionForItemAtIndex:index];
        return true;
    }

    State Read(Item* item)
    {
        State s;
        if (!item || !item->Status)
            return s;
        NSStatusBarButton* button = item->Status.button;
        const char* tip = button.toolTip.UTF8String;
        s.Tooltip = tip ? tip : "";
        s.Visible = [item->Status isVisible];
        NSWindow* window = button.window;
        s.Window = !window ? "none" : [window isVisible] ? "shown" : "hidden";
        s.InStatusBar = InStatusBar(item);
        NSImageRep* rep = button.image.representations.firstObject;
        s.IconPixels = rep ? (int)rep.pixelsWide : 0;
        s.MenuItems = item->Menu ? (int)item->Menu.numberOfItems : 0;
        return s;
    }
}
