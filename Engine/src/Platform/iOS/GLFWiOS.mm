// iOS / iPadOS'ta GLFW yok: motorun kullandığı GLFW işlevleri burada UIKit ile uygulanır (Android'deki GLFWAndroid.cpp gibi).
// Pencere = tam ekran UIWindow + katmanı CAMetalLayer olan görünüm (yalnız Metal; GL bağlamı yok). Boyutlar ve imleç piksel
// cinsinden (Android ile aynı; içerik ölçeği UIScreen.scale), ilk parmak birincil fare düğmesi, donanım klavyesi GLFW tuşları,
// Game Controller kumandaları GLFW kumanda durumu olur. Kareler CADisplayLink ile (RunFrameLoop), olaylar UIKit ana döngüsünden.
#include "Core/Base/Base.h"

#if defined(CQ_PLATFORM_IOS)

#include "iOSPlatform.h"
#include "Core/Application.h"

#import <UIKit/UIKit.h>
#import <QuartzCore/CAMetalLayer.h>
#import <GameController/GameController.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

struct GLFWwindow
{
    int Width = 0, Height = 0;   // piksel
    void* User = nullptr;
    bool ShouldClose = false;
    double CursorX = 0.0, CursorY = 0.0;
    unsigned char Keys[GLFW_KEY_LAST + 1] = {};
    unsigned char Mouse[GLFW_MOUSE_BUTTON_LAST + 1] = {};
    GLFWkeyfun KeyCallback = nullptr;
    GLFWcharfun CharCallback = nullptr;
    GLFWmousebuttonfun MouseButtonCallback = nullptr;
    GLFWcursorposfun CursorPosCallback = nullptr;
    GLFWscrollfun ScrollCallback = nullptr;
    GLFWwindowsizefun SizeCallback = nullptr;
    GLFWwindowclosefun CloseCallback = nullptr;
    GLFWframebuffersizefun FramebufferSizeCallback = nullptr;
    GLFWwindowfocusfun FocusCallback = nullptr;
    // Motor tuş / düğme durumunu kare başına okur: aynı karede basılıp bırakılan (kısa dokunuş) bırakma sonraki kareye ertelenir
    uint64_t KeyPressFrame[GLFW_KEY_LAST + 1] = {};
    uint64_t MousePressFrame[GLFW_MOUSE_BUTTON_LAST + 1] = {};
    std::vector<std::pair<int, int>> DeferredKeyUps;
    std::vector<int> DeferredMouseUps;
};

struct GLFWmonitor
{
    int Dummy = 0;
};

@interface CQMetalView : UIView
@end

@interface CQViewController : UIViewController
@end

namespace
{
    struct State
    {
        GLFWwindow* Window = nullptr;
        GLFWwindow* Current = nullptr;
        GLFWerrorfun ErrorCallback = nullptr;
        GLFWmonitor Monitor;
        GLFWvidmode Mode{};
        bool Focused = true;
        uint64_t Frame = 1;
        std::string Clipboard;
        UIWindow* UIWin = nil;
        CQMetalView* View = nil;
        UITouch* PrimaryTouch = nil;   // fare olan parmak
        CFTimeInterval Start = 0.0;
        unsigned char PadButtons[GLFW_GAMEPAD_BUTTON_LAST + 1] = {};
        float PadAxes[GLFW_GAMEPAD_AXIS_LAST + 1] = { 0, 0, 0, 0, -1, -1 };
    };
    State s;

    float Scale()
    {
        if (s.View)
            return (float)s.View.contentScaleFactor;
        return (float)UIScreen.mainScreen.nativeScale;
    }

    void SetMouse(GLFWwindow* w, int button, bool down)
    {
        if (!w || button < 0 || button > GLFW_MOUSE_BUTTON_LAST) return;
        const unsigned char state = down ? GLFW_PRESS : GLFW_RELEASE;
        if (w->Mouse[button] == state) return;
        if (down)
            w->MousePressFrame[button] = s.Frame;
        else if (w->MousePressFrame[button] == s.Frame)
        {
            w->DeferredMouseUps.push_back(button);
            return;
        }
        w->Mouse[button] = state;
        if (w->MouseButtonCallback) w->MouseButtonCallback(w, button, state, 0);
    }

    void SetKey(GLFWwindow* w, int key, int scancode, bool down, int mods)
    {
        if (!w || key < 0 || key > GLFW_KEY_LAST) return;
        if (down)
        {
            const bool repeat = w->Keys[key] == GLFW_PRESS;
            w->Keys[key] = GLFW_PRESS;
            if (!repeat)
                w->KeyPressFrame[key] = s.Frame;
            if (w->KeyCallback) w->KeyCallback(w, key, scancode, repeat ? GLFW_REPEAT : GLFW_PRESS, mods);
            return;
        }
        if (w->KeyPressFrame[key] == s.Frame)
        {
            w->DeferredKeyUps.push_back({ key, scancode });
            return;
        }
        w->Keys[key] = GLFW_RELEASE;
        if (w->KeyCallback) w->KeyCallback(w, key, scancode, GLFW_RELEASE, mods);
    }

    void SetCursor(GLFWwindow* w, CGPoint p)
    {
        if (!w) return;
        const float scale = Scale();
        w->CursorX = p.x * scale;
        w->CursorY = p.y * scale;
        if (w->CursorPosCallback) w->CursorPosCallback(w, w->CursorX, w->CursorY);
    }

    void SyncSize()
    {
        GLFWwindow* w = s.Window;
        if (!w || !s.View) return;
        const float scale = Scale();
        const int width = std::max(1, (int)(s.View.bounds.size.width * scale + 0.5f));
        const int height = std::max(1, (int)(s.View.bounds.size.height * scale + 0.5f));
        s.Mode.width = width;
        s.Mode.height = height;
        if (width == w->Width && height == w->Height) return;
        w->Width = width;
        w->Height = height;
        if (w->SizeCallback) w->SizeCallback(w, width, height);
        if (w->FramebufferSizeCallback) w->FramebufferSizeCallback(w, width, height);
    }

    // UIKeyboardHIDUsage (USB HID kullanım kodları) -> GLFW tuşu
    int MapKey(long hid)
    {
        if (hid >= 4 && hid <= 29) return GLFW_KEY_A + (int)(hid - 4);
        if (hid >= 30 && hid <= 38) return GLFW_KEY_1 + (int)(hid - 30);
        if (hid == 39) return GLFW_KEY_0;
        if (hid >= 58 && hid <= 69) return GLFW_KEY_F1 + (int)(hid - 58);
        switch (hid)
        {
            case 40: case 88: return GLFW_KEY_ENTER;
            case 41: return GLFW_KEY_ESCAPE;
            case 42: return GLFW_KEY_BACKSPACE;
            case 43: return GLFW_KEY_TAB;
            case 44: return GLFW_KEY_SPACE;
            case 45: return GLFW_KEY_MINUS;
            case 46: return GLFW_KEY_EQUAL;
            case 47: return GLFW_KEY_LEFT_BRACKET;
            case 48: return GLFW_KEY_RIGHT_BRACKET;
            case 49: return GLFW_KEY_BACKSLASH;
            case 51: return GLFW_KEY_SEMICOLON;
            case 52: return GLFW_KEY_APOSTROPHE;
            case 53: return GLFW_KEY_GRAVE_ACCENT;
            case 54: return GLFW_KEY_COMMA;
            case 55: return GLFW_KEY_PERIOD;
            case 56: return GLFW_KEY_SLASH;
            case 57: return GLFW_KEY_CAPS_LOCK;
            case 73: return GLFW_KEY_INSERT;
            case 74: return GLFW_KEY_HOME;
            case 75: return GLFW_KEY_PAGE_UP;
            case 76: return GLFW_KEY_DELETE;
            case 77: return GLFW_KEY_END;
            case 78: return GLFW_KEY_PAGE_DOWN;
            case 79: return GLFW_KEY_RIGHT;
            case 80: return GLFW_KEY_LEFT;
            case 81: return GLFW_KEY_DOWN;
            case 82: return GLFW_KEY_UP;
            case 224: return GLFW_KEY_LEFT_CONTROL;
            case 225: return GLFW_KEY_LEFT_SHIFT;
            case 226: return GLFW_KEY_LEFT_ALT;
            case 227: return GLFW_KEY_LEFT_SUPER;
            case 228: return GLFW_KEY_RIGHT_CONTROL;
            case 229: return GLFW_KEY_RIGHT_SHIFT;
            case 230: return GLFW_KEY_RIGHT_ALT;
            case 231: return GLFW_KEY_RIGHT_SUPER;
            default: return GLFW_KEY_UNKNOWN;
        }
    }

    int ModsOf(UIKeyModifierFlags flags)
    {
        int m = 0;
        if (flags & UIKeyModifierShift) m |= GLFW_MOD_SHIFT;
        if (flags & UIKeyModifierControl) m |= GLFW_MOD_CONTROL;
        if (flags & UIKeyModifierAlternate) m |= GLFW_MOD_ALT;
        if (flags & UIKeyModifierCommand) m |= GLFW_MOD_SUPER;
        return m;
    }

    void HandlePresses(NSSet<UIPress*>* presses, bool down)
    {
        GLFWwindow* w = s.Window;
        if (!w) return;
        for (UIPress* press in presses)
        {
            UIKey* key = press.key;
            if (!key) continue;
            const int glfwKey = MapKey((long)key.keyCode);
            if (glfwKey != GLFW_KEY_UNKNOWN)
                SetKey(w, glfwKey, (int)key.keyCode, down, ModsOf(key.modifierFlags));
            if (down && w->CharCallback && key.characters.length > 0 && !(key.modifierFlags & (UIKeyModifierCommand | UIKeyModifierControl)))
            {
                NSString* chars = key.characters;
                for (NSUInteger i = 0; i < chars.length; ++i)
                {
                    const unichar c = [chars characterAtIndex:i];
                    if (c >= 32 && c != 127)
                        w->CharCallback(w, (unsigned int)c);
                }
            }
        }
    }

    // İlk bağlı oyun kumandası (Game Controller: MFi, Xbox, PlayStation, Switch Pro) GLFW kumanda düzenine
    bool PollGamepad()
    {
        GCController* pad = nil;
        for (GCController* c in GCController.controllers)
            if (c.extendedGamepad)
            {
                pad = c;
                break;
            }
        if (!pad)
            return false;
        GCExtendedGamepad* g = pad.extendedGamepad;
        auto b = [](GCControllerButtonInput* in) -> unsigned char { return in && in.isPressed ? GLFW_PRESS : GLFW_RELEASE; };
        s.PadButtons[GLFW_GAMEPAD_BUTTON_A] = b(g.buttonA);
        s.PadButtons[GLFW_GAMEPAD_BUTTON_B] = b(g.buttonB);
        s.PadButtons[GLFW_GAMEPAD_BUTTON_X] = b(g.buttonX);
        s.PadButtons[GLFW_GAMEPAD_BUTTON_Y] = b(g.buttonY);
        s.PadButtons[GLFW_GAMEPAD_BUTTON_LEFT_BUMPER] = b(g.leftShoulder);
        s.PadButtons[GLFW_GAMEPAD_BUTTON_RIGHT_BUMPER] = b(g.rightShoulder);
        s.PadButtons[GLFW_GAMEPAD_BUTTON_BACK] = b(g.buttonOptions);
        s.PadButtons[GLFW_GAMEPAD_BUTTON_START] = b(g.buttonMenu);
        s.PadButtons[GLFW_GAMEPAD_BUTTON_GUIDE] = b(g.buttonHome);
        s.PadButtons[GLFW_GAMEPAD_BUTTON_LEFT_THUMB] = b(g.leftThumbstickButton);
        s.PadButtons[GLFW_GAMEPAD_BUTTON_RIGHT_THUMB] = b(g.rightThumbstickButton);
        s.PadButtons[GLFW_GAMEPAD_BUTTON_DPAD_UP] = b(g.dpad.up);
        s.PadButtons[GLFW_GAMEPAD_BUTTON_DPAD_RIGHT] = b(g.dpad.right);
        s.PadButtons[GLFW_GAMEPAD_BUTTON_DPAD_DOWN] = b(g.dpad.down);
        s.PadButtons[GLFW_GAMEPAD_BUTTON_DPAD_LEFT] = b(g.dpad.left);
        // GLFW: y ekseni aşağı pozitif, tetikler -1 .. 1
        s.PadAxes[GLFW_GAMEPAD_AXIS_LEFT_X] = g.leftThumbstick.xAxis.value;
        s.PadAxes[GLFW_GAMEPAD_AXIS_LEFT_Y] = -g.leftThumbstick.yAxis.value;
        s.PadAxes[GLFW_GAMEPAD_AXIS_RIGHT_X] = g.rightThumbstick.xAxis.value;
        s.PadAxes[GLFW_GAMEPAD_AXIS_RIGHT_Y] = -g.rightThumbstick.yAxis.value;
        s.PadAxes[GLFW_GAMEPAD_AXIS_LEFT_TRIGGER] = g.leftTrigger.value * 2.0f - 1.0f;
        s.PadAxes[GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER] = g.rightTrigger.value * 2.0f - 1.0f;
        return true;
    }

    void BeginFrame()
    {
        ++s.Frame;
        GLFWwindow* w = s.Window;
        if (!w) return;
        SyncSize();
        for (auto [key, scancode] : w->DeferredKeyUps)
            if (w->Keys[key] == GLFW_PRESS)
            {
                w->Keys[key] = GLFW_RELEASE;
                if (w->KeyCallback) w->KeyCallback(w, key, scancode, GLFW_RELEASE, 0);
            }
        w->DeferredKeyUps.clear();
        for (int button : w->DeferredMouseUps)
            if (w->Mouse[button] == GLFW_PRESS)
            {
                w->Mouse[button] = GLFW_RELEASE;
                if (w->MouseButtonCallback) w->MouseButtonCallback(w, button, GLFW_RELEASE, 0);
            }
        w->DeferredMouseUps.clear();
    }

    bool CreateUIWindow()
    {
        if (s.UIWin)
            return true;
        UIWindow* window = nil;
        // Sahne tabanlı yaşam döngüsü (iOS 13+): etkin sahnenin penceresi; yoksa eski tek pencereli yol
        for (UIScene* scene in UIApplication.sharedApplication.connectedScenes)
            if ([scene isKindOfClass:[UIWindowScene class]])
            {
                window = [[UIWindow alloc] initWithWindowScene:(UIWindowScene*)scene];
                break;
            }
        if (!window)
            window = [[UIWindow alloc] initWithFrame:UIScreen.mainScreen.bounds];
        CQViewController* controller = [[CQViewController alloc] init];
        window.rootViewController = controller;
        [window makeKeyAndVisible];
        s.UIWin = window;
        s.View = (CQMetalView*)controller.view;
        [s.View becomeFirstResponder];
        return s.View != nil;
    }
}

@implementation CQMetalView

+ (Class)layerClass
{
    return [CAMetalLayer class];
}

- (instancetype)initWithFrame:(CGRect)frame
{
    if ((self = [super initWithFrame:frame]))
    {
        self.multipleTouchEnabled = YES;
        self.contentScaleFactor = UIScreen.mainScreen.nativeScale;
        self.layer.contentsScale = self.contentScaleFactor;
    }
    return self;
}

- (BOOL)canBecomeFirstResponder
{
    return YES;
}

- (void)layoutSubviews
{
    [super layoutSubviews];
    SyncSize();
}

- (void)touchesBegan:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event
{
    GLFWwindow* w = s.Window;
    for (UITouch* t in touches)
        if (!s.PrimaryTouch)
        {
            s.PrimaryTouch = t;
            SetCursor(w, [t locationInView:self]);
            SetMouse(w, GLFW_MOUSE_BUTTON_LEFT, true);
        }
}

- (void)touchesMoved:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event
{
    if (s.PrimaryTouch && [touches containsObject:s.PrimaryTouch])
        SetCursor(s.Window, [s.PrimaryTouch locationInView:self]);
}

- (void)touchesEnded:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event
{
    if (s.PrimaryTouch && [touches containsObject:s.PrimaryTouch])
    {
        SetCursor(s.Window, [s.PrimaryTouch locationInView:self]);
        SetMouse(s.Window, GLFW_MOUSE_BUTTON_LEFT, false);
        s.PrimaryTouch = nil;
    }
}

- (void)touchesCancelled:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event
{
    [self touchesEnded:touches withEvent:event];
}

- (void)pressesBegan:(NSSet<UIPress*>*)presses withEvent:(UIPressesEvent*)event
{
    HandlePresses(presses, true);
}

- (void)pressesEnded:(NSSet<UIPress*>*)presses withEvent:(UIPressesEvent*)event
{
    HandlePresses(presses, false);
}

- (void)pressesCancelled:(NSSet<UIPress*>*)presses withEvent:(UIPressesEvent*)event
{
    HandlePresses(presses, false);
}

@end

@implementation CQViewController

- (void)loadView
{
    self.view = [[CQMetalView alloc] initWithFrame:UIScreen.mainScreen.bounds];
}

// Oyun: tam ekran, durum çubuğu ve ana ekran çizgisi gizli, sistem kenar hareketleri ilk dokunuşta oyuna gider
- (BOOL)prefersStatusBarHidden
{
    return YES;
}

- (BOOL)prefersHomeIndicatorAutoHidden
{
    return YES;
}

- (UIRectEdge)preferredScreenEdgesDeferringSystemGestures
{
    return UIRectEdgeAll;
}

@end

// Kare döngüsü: CADisplayLink hedefi
@interface CQFrameDriver : NSObject
@property(nonatomic, assign) Conqueror::Application* App;
@property(nonatomic, strong) CADisplayLink* Link;
- (void)step:(CADisplayLink*)link;
@end

@implementation CQFrameDriver

- (void)step:(CADisplayLink*)link
{
    Conqueror::Application* app = self.App;
    if (!app)
        return;
    if (!app->IsRunning())
    {
        // Oyun kendini kapattı (Application.Quit): Android'deki gibi süreç biter
        [self.Link invalidate];
        self.Link = nil;
        std::exit(0);
    }
    @autoreleasepool
    {
        app->RunFrame();
    }
}

@end

namespace Conqueror::iOS
{
    namespace
    {
        CQFrameDriver* s_Driver = nil;
    }

    std::string LibraryDirectory()
    {
        NSArray<NSString*>* dirs = NSSearchPathForDirectoriesInDomains(NSLibraryDirectory, NSUserDomainMask, YES);
        return dirs.count ? std::string(dirs.firstObject.UTF8String) : std::string(NSHomeDirectory().UTF8String) + "/Library";
    }

    std::string CachesDirectory()
    {
        NSArray<NSString*>* dirs = NSSearchPathForDirectoriesInDomains(NSCachesDirectory, NSUserDomainMask, YES);
        return dirs.count ? std::string(dirs.firstObject.UTF8String) : LibraryDirectory() + "/Caches";
    }

    std::string PrepareGameFolder()
    {
        namespace fs = std::filesystem;
        std::error_code ec;
        const fs::path source = fs::path(NSBundle.mainBundle.resourcePath.UTF8String) / "game";
        const fs::path destination = fs::path(LibraryDirectory()) / "Application Support" / "game";
        if (!fs::is_directory(source, ec))
            return {};
        // Paketleyicinin yazdığı sürüm damgası (oyun her derlendiğinde değişir): aynıysa kopya güncel
        std::string bundleVersion, haveVersion;
        {
            std::ifstream in(source / ".cq_game_version");
            std::getline(in, bundleVersion);
        }
        {
            std::ifstream in(destination / ".cq_game_version");
            std::getline(in, haveVersion);
        }
        if (!bundleVersion.empty() && bundleVersion == haveVersion && fs::is_directory(destination, ec))
            return destination.string();
        if (fs::exists(destination, ec))
            fs::remove_all(destination, ec);
        fs::create_directories(destination, ec);
        fs::copy(source, destination, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
        if (ec)
        {
            NSLog(@"[Conqueror] Copying the game files failed: %s", ec.message().c_str());
            return {};
        }
        // Yedeklenmesin (iCloud): kurulumda yeniden üretilebilir veri
        NSURL* url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:destination.c_str()]];
        [url setResourceValue:@YES forKey:NSURLIsExcludedFromBackupKey error:nil];
        return destination.string();
    }

    void RunFrameLoop(Application* app)
    {
        if (!s_Driver)
            s_Driver = [[CQFrameDriver alloc] init];
        s_Driver.App = app;
        CADisplayLink* link = [CADisplayLink displayLinkWithTarget:s_Driver selector:@selector(step:)];
        // ProMotion ekranlarda 120 Hz'e kadar
        link.preferredFrameRateRange = CAFrameRateRangeMake(30.0f, 120.0f, 60.0f);
        [link addToRunLoop:NSRunLoop.mainRunLoop forMode:NSRunLoopCommonModes];
        s_Driver.Link = link;
        // Arka planda GPU işi yasak: uygulama etkin değilken kare çizilmez, odak bilgisi oyuna gider
        NSNotificationCenter* nc = NSNotificationCenter.defaultCenter;
        [nc addObserverForName:UIApplicationWillResignActiveNotification object:nil queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification*) {
            s_Driver.Link.paused = YES;
            s.Focused = false;
            if (s.Window && s.Window->FocusCallback) s.Window->FocusCallback(s.Window, GLFW_FALSE);
        }];
        [nc addObserverForName:UIApplicationDidBecomeActiveNotification object:nil queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification*) {
            s_Driver.Link.paused = NO;
            s.Focused = true;
            if (s.Window && s.Window->FocusCallback) s.Window->FocusCallback(s.Window, GLFW_TRUE);
        }];
        [nc addObserverForName:UIApplicationWillTerminateNotification object:nil queue:NSOperationQueue.mainQueue usingBlock:^(NSNotification*) {
            if (s.Window)
            {
                s.Window->ShouldClose = true;
                if (s.Window->CloseCallback) s.Window->CloseCallback(s.Window);
            }
        }];
    }

    void* MetalLayer()
    {
        return s.View ? (__bridge void*)s.View.layer : nullptr;
    }

    float ContentScale() { return Scale(); }

    void OpenURL(const std::string& url)
    {
        NSURL* u = [NSURL URLWithString:[NSString stringWithUTF8String:url.c_str()]];
        if (u)
            [UIApplication.sharedApplication openURL:u options:@{} completionHandler:nil];
    }
}

extern "C"
{
    int glfwInit(void)
    {
        if (s.Start == 0.0)
            s.Start = CACurrentMediaTime();
        s.Mode.refreshRate = (int)UIScreen.mainScreen.maximumFramesPerSecond;
        s.Mode.redBits = s.Mode.greenBits = s.Mode.blueBits = 8;
        const float scale = (float)UIScreen.mainScreen.nativeScale;
        s.Mode.width = (int)(UIScreen.mainScreen.bounds.size.width * scale);
        s.Mode.height = (int)(UIScreen.mainScreen.bounds.size.height * scale);
        return GLFW_TRUE;
    }

    void glfwTerminate(void)
    {
        if (s.Window) glfwDestroyWindow(s.Window);
    }

    GLFWerrorfun glfwSetErrorCallback(GLFWerrorfun callback)
    {
        GLFWerrorfun prev = s.ErrorCallback;
        s.ErrorCallback = callback;
        return prev;
    }

    int glfwGetPlatform(void) { return GLFW_PLATFORM_NULL; }
    void glfwDefaultWindowHints(void) {}
    void glfwWindowHint(int, int) {}
    void glfwWindowHintString(int, const char*) {}

    GLFWwindow* glfwCreateWindow(int, int, const char*, GLFWmonitor*, GLFWwindow*)
    {
        if (s.Window) return nullptr;   // tek tam ekran pencere
        if (!CreateUIWindow()) return nullptr;
        auto* w = new GLFWwindow();
        s.Window = w;
        SyncSize();
        return w;
    }

    void glfwDestroyWindow(GLFWwindow* w)
    {
        if (!w) return;
        if (s.Window == w) s.Window = nullptr;
        if (s.Current == w) s.Current = nullptr;
        delete w;
    }

    int glfwWindowShouldClose(GLFWwindow* w) { return w && w->ShouldClose ? GLFW_TRUE : GLFW_FALSE; }
    void glfwSetWindowShouldClose(GLFWwindow* w, int value) { if (w) w->ShouldClose = value != 0; }
    void glfwMakeContextCurrent(GLFWwindow* w) { s.Current = w; }
    GLFWwindow* glfwGetCurrentContext(void) { return s.Current; }
    void glfwSwapBuffers(GLFWwindow*) {}   // Metal arka ucu sunar
    void glfwSwapInterval(int) {}
    GLFWglproc glfwGetProcAddress(const char*) { return nullptr; }   // GL yok: glad göstericileri Metal katmanına bağlanır

    void glfwPollEvents(void)
    {
        BeginFrame();
    }
    void glfwWaitEvents(void) { BeginFrame(); }
    void glfwWaitEventsTimeout(double) { BeginFrame(); }
    void glfwPostEmptyEvent(void) {}

    void glfwSetWindowUserPointer(GLFWwindow* w, void* p) { if (w) w->User = p; }
    void* glfwGetWindowUserPointer(GLFWwindow* w) { return w ? w->User : nullptr; }
    void glfwGetWindowSize(GLFWwindow* w, int* width, int* height)
    {
        if (width) *width = w ? w->Width : 0;
        if (height) *height = w ? w->Height : 0;
    }
    void glfwGetFramebufferSize(GLFWwindow* w, int* width, int* height) { glfwGetWindowSize(w, width, height); }
    void glfwGetWindowFrameSize(GLFWwindow*, int* l, int* t, int* r, int* b)
    {
        if (l) *l = 0;
        if (t) *t = 0;
        if (r) *r = 0;
        if (b) *b = 0;
    }
    void glfwGetWindowPos(GLFWwindow*, int* x, int* y) { if (x) *x = 0; if (y) *y = 0; }
    void glfwSetWindowPos(GLFWwindow*, int, int) {}
    void glfwSetWindowSize(GLFWwindow*, int, int) {}
    void glfwSetWindowSizeLimits(GLFWwindow*, int, int, int, int) {}
    void glfwSetWindowTitle(GLFWwindow*, const char*) {}
    void glfwSetWindowIcon(GLFWwindow*, int, const GLFWimage*) {}
    void glfwSetWindowAttrib(GLFWwindow*, int, int) {}
    void glfwSetWindowMonitor(GLFWwindow*, GLFWmonitor*, int, int, int, int, int) {}
    void glfwIconifyWindow(GLFWwindow*) {}
    void glfwRestoreWindow(GLFWwindow*) {}
    void glfwMaximizeWindow(GLFWwindow*) {}
    void glfwRequestWindowAttention(GLFWwindow*) {}
    GLFWmonitor* glfwGetPrimaryMonitor(void) { return &s.Monitor; }
    GLFWmonitor* glfwGetWindowMonitor(GLFWwindow*) { return &s.Monitor; }
    void glfwGetMonitorPos(GLFWmonitor*, int* x, int* y) { if (x) *x = 0; if (y) *y = 0; }
    const char* glfwGetMonitorName(GLFWmonitor*) { return "iOS Screen"; }
    const GLFWvidmode* glfwGetVideoMode(GLFWmonitor*) { return &s.Mode; }
    const GLFWvidmode* glfwGetVideoModes(GLFWmonitor*, int* count)
    {
        if (count) *count = 1;
        return &s.Mode;
    }
    int glfwGetWindowAttrib(GLFWwindow*, int attrib)
    {
        if (attrib == GLFW_FOCUSED) return s.Focused ? GLFW_TRUE : GLFW_FALSE;
        if (attrib == GLFW_VISIBLE) return GLFW_TRUE;
        return 0;
    }
    float glfwGetWindowOpacity(GLFWwindow*) { return 1.0f; }

    GLFWkeyfun glfwSetKeyCallback(GLFWwindow* w, GLFWkeyfun cb) { GLFWkeyfun p = w->KeyCallback; w->KeyCallback = cb; return p; }
    GLFWcharfun glfwSetCharCallback(GLFWwindow* w, GLFWcharfun cb) { GLFWcharfun p = w->CharCallback; w->CharCallback = cb; return p; }
    GLFWmousebuttonfun glfwSetMouseButtonCallback(GLFWwindow* w, GLFWmousebuttonfun cb) { GLFWmousebuttonfun p = w->MouseButtonCallback; w->MouseButtonCallback = cb; return p; }
    GLFWcursorposfun glfwSetCursorPosCallback(GLFWwindow* w, GLFWcursorposfun cb) { GLFWcursorposfun p = w->CursorPosCallback; w->CursorPosCallback = cb; return p; }
    GLFWscrollfun glfwSetScrollCallback(GLFWwindow* w, GLFWscrollfun cb) { GLFWscrollfun p = w->ScrollCallback; w->ScrollCallback = cb; return p; }
    GLFWwindowsizefun glfwSetWindowSizeCallback(GLFWwindow* w, GLFWwindowsizefun cb) { GLFWwindowsizefun p = w->SizeCallback; w->SizeCallback = cb; return p; }
    GLFWframebuffersizefun glfwSetFramebufferSizeCallback(GLFWwindow* w, GLFWframebuffersizefun cb) { GLFWframebuffersizefun p = w->FramebufferSizeCallback; w->FramebufferSizeCallback = cb; return p; }
    GLFWwindowclosefun glfwSetWindowCloseCallback(GLFWwindow* w, GLFWwindowclosefun cb) { GLFWwindowclosefun p = w->CloseCallback; w->CloseCallback = cb; return p; }
    GLFWwindowfocusfun glfwSetWindowFocusCallback(GLFWwindow* w, GLFWwindowfocusfun cb) { GLFWwindowfocusfun p = w->FocusCallback; w->FocusCallback = cb; return p; }

    int glfwGetKey(GLFWwindow* w, int key) { return w && key >= 0 && key <= GLFW_KEY_LAST ? w->Keys[key] : GLFW_RELEASE; }
    int glfwGetMouseButton(GLFWwindow* w, int button) { return w && button >= 0 && button <= GLFW_MOUSE_BUTTON_LAST ? w->Mouse[button] : GLFW_RELEASE; }
    void glfwGetCursorPos(GLFWwindow* w, double* x, double* y)
    {
        if (x) *x = w ? w->CursorX : 0.0;
        if (y) *y = w ? w->CursorY : 0.0;
    }
    void glfwSetCursorPos(GLFWwindow* w, double x, double y) { if (w) { w->CursorX = x; w->CursorY = y; } }
    void glfwSetInputMode(GLFWwindow*, int, int) {}
    int glfwGetInputMode(GLFWwindow*, int mode) { return mode == GLFW_CURSOR ? GLFW_CURSOR_NORMAL : GLFW_FALSE; }
    int glfwRawMouseMotionSupported(void) { return GLFW_FALSE; }

    double glfwGetTime(void) { return CACurrentMediaTime() - s.Start; }
    void glfwSetTime(double time) { s.Start = CACurrentMediaTime() - time; }
    uint64_t glfwGetTimerValue(void) { return (uint64_t)(CACurrentMediaTime() * 1e9); }
    uint64_t glfwGetTimerFrequency(void) { return 1000000000ull; }

    int glfwJoystickPresent(int jid) { return jid == GLFW_JOYSTICK_1 && PollGamepad() ? GLFW_TRUE : GLFW_FALSE; }
    int glfwJoystickIsGamepad(int jid) { return glfwJoystickPresent(jid); }
    const char* glfwGetJoystickName(int jid)
    {
        static std::string name;
        if (!glfwJoystickPresent(jid)) return nullptr;
        GCController* c = GCController.controllers.firstObject;
        name = c && c.vendorName ? c.vendorName.UTF8String : "Game Controller";
        return name.c_str();
    }
    const char* glfwGetGamepadName(int jid) { return glfwGetJoystickName(jid); }
    const char* glfwGetJoystickGUID(int) { return nullptr; }
    const unsigned char* glfwGetJoystickButtons(int jid, int* count)
    {
        if (!glfwJoystickPresent(jid)) { if (count) *count = 0; return nullptr; }
        if (count) *count = GLFW_GAMEPAD_BUTTON_LAST + 1;
        return s.PadButtons;
    }
    const float* glfwGetJoystickAxes(int jid, int* count)
    {
        if (!glfwJoystickPresent(jid)) { if (count) *count = 0; return nullptr; }
        if (count) *count = GLFW_GAMEPAD_AXIS_LAST + 1;
        return s.PadAxes;
    }
    const unsigned char* glfwGetJoystickHats(int, int* count)
    {
        if (count) *count = 0;
        return nullptr;
    }
    int glfwGetGamepadState(int jid, GLFWgamepadstate* state)
    {
        if (!state || !glfwJoystickPresent(jid)) return GLFW_FALSE;
        std::memcpy(state->buttons, s.PadButtons, sizeof(state->buttons));
        std::memcpy(state->axes, s.PadAxes, sizeof(state->axes));
        return GLFW_TRUE;
    }
    int glfwUpdateGamepadMappings(const char*) { return GLFW_TRUE; }
    GLFWjoystickfun glfwSetJoystickCallback(GLFWjoystickfun) { return nullptr; }

    int glfwGetError(const char** description)
    {
        if (description) *description = nullptr;
        return GLFW_NO_ERROR;
    }
    const char* glfwGetVersionString(void) { return "3.4.0 iOS (Conqueror UIKit/Metal)"; }
    void glfwGetVersion(int* major, int* minor, int* rev)
    {
        if (major) *major = 3;
        if (minor) *minor = 4;
        if (rev) *rev = 0;
    }
    GLFWcursor* glfwCreateCursor(const GLFWimage*, int, int) { return nullptr; }
    GLFWcursor* glfwCreateStandardCursor(int) { return nullptr; }
    void glfwDestroyCursor(GLFWcursor*) {}
    void glfwSetCursor(GLFWwindow*, GLFWcursor*) {}
    const char* glfwGetKeyName(int, int) { return nullptr; }
    int glfwGetKeyScancode(int) { return -1; }
    const char* glfwGetClipboardString(GLFWwindow*)
    {
        NSString* text = UIPasteboard.generalPasteboard.string;
        s.Clipboard = text ? text.UTF8String : "";
        return s.Clipboard.c_str();
    }
    void glfwSetClipboardString(GLFWwindow*, const char* text)
    {
        UIPasteboard.generalPasteboard.string = [NSString stringWithUTF8String:text ? text : ""];
    }
    void glfwFocusWindow(GLFWwindow*) {}
    void glfwShowWindow(GLFWwindow*) {}
    void glfwHideWindow(GLFWwindow*) {}   // tek tam ekran pencere: ek pencereler (Window bileşeni) bu platformda açılmaz
    void glfwSetWindowOpacity(GLFWwindow*, float) {}
    GLFWmonitor** glfwGetMonitors(int* count)
    {
        static GLFWmonitor* monitors[1] = { &s.Monitor };
        if (count) *count = 1;
        return monitors;
    }
    void glfwGetMonitorWorkarea(GLFWmonitor*, int* x, int* y, int* width, int* height)
    {
        if (x) *x = 0;
        if (y) *y = 0;
        if (width) *width = s.Mode.width;
        if (height) *height = s.Mode.height;
    }
    void glfwGetMonitorPhysicalSize(GLFWmonitor*, int* widthMM, int* heightMM)
    {
        if (widthMM) *widthMM = 0;
        if (heightMM) *heightMM = 0;
    }
    void glfwGetMonitorContentScale(GLFWmonitor*, float* xscale, float* yscale)
    {
        const float scale = Scale();
        if (xscale) *xscale = scale;
        if (yscale) *yscale = scale;
    }
    void glfwGetWindowContentScale(GLFWwindow*, float* xscale, float* yscale) { glfwGetMonitorContentScale(nullptr, xscale, yscale); }
    GLFWdropfun glfwSetDropCallback(GLFWwindow*, GLFWdropfun) { return nullptr; }
    GLFWcursorenterfun glfwSetCursorEnterCallback(GLFWwindow*, GLFWcursorenterfun) { return nullptr; }
    GLFWwindowposfun glfwSetWindowPosCallback(GLFWwindow*, GLFWwindowposfun) { return nullptr; }
    GLFWwindowiconifyfun glfwSetWindowIconifyCallback(GLFWwindow*, GLFWwindowiconifyfun) { return nullptr; }
    GLFWwindowmaximizefun glfwSetWindowMaximizeCallback(GLFWwindow*, GLFWwindowmaximizefun) { return nullptr; }
    GLFWwindowrefreshfun glfwSetWindowRefreshCallback(GLFWwindow*, GLFWwindowrefreshfun) { return nullptr; }
    GLFWwindowcontentscalefun glfwSetWindowContentScaleCallback(GLFWwindow*, GLFWwindowcontentscalefun) { return nullptr; }
    GLFWcharmodsfun glfwSetCharModsCallback(GLFWwindow*, GLFWcharmodsfun) { return nullptr; }
    GLFWmonitorfun glfwSetMonitorCallback(GLFWmonitorfun) { return nullptr; }
    int glfwVulkanSupported(void) { return GLFW_FALSE; }
    int glfwCreateWindowSurface(void*, GLFWwindow*, const void*, void*) { return -7; }   // VK_ERROR_EXTENSION_NOT_PRESENT: Vulkan yok
    int glfwExtensionSupported(const char*) { return GLFW_FALSE; }
}

#endif
