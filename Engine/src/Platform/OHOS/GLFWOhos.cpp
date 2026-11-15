// HarmonyOS'ta GLFW yok: motorun ve editörün kullandığı GLFW işlevleri burada ArkTS XComponent + OHNativeWindow + EGL ile
// uygulanır (HarmonyOS 5+ PC "2in1", tablet; OpenHarmony NDK API 12+).
//
// İş parçacıkları: XComponent geri çağrıları ArkTS ana (UI) iş parçacığında gelir, motor kendi iş parçacığında döner
// (OHOSModule.cpp). Geri çağrılar olayları kilitli bir kuyruğa yazar; glfwPollEvents onları motor iş parçacığında GLFW geri
// çağrılarına çevirir. Kareleri XComponent'in kare saati (OnFrame, ekran tazelemesi) sürer: glfwPollEvents bir sonraki
// tazelemeyi bekler, böylece Application::RunFrame ekranla eşli çalışır.
// Pencere = XComponent yüzeyinin OHNativeWindow'u üzerinde EGL yüzeyi (OpenGL ES 3.2, olmazsa 3.1 / 3.0). Yüzey yok edilirken
// (uygulama arka plana geçti, pencere kapanıyor) UI iş parçacığı motor EGL yüzeyini bırakana kadar bekler; yüzey dönene kadar
// glfwPollEvents bekler (oyun döngüsü duraklar).
// Girdi: fare (düğmeler, hareket, tekerlek: ArkUI eksen olayı), klavye (XComponent tuş kodları GLFW tuşlarına; ASCII karakterler
// tuştan, diğer metin giriş yönteminden InsertText ile), dokunma (ilk parmak = sol fare düğmesi). İmleç biçimi ve pencere
// başlığı ArkTS'e istek olarak gider; pano OH_Pasteboard (API 13+, çalışma anında açılır) ya da süreç içi.
#if defined(__OHOS__)

#include "OHOSInternal.h"
#include "OHOSPlatform.h"

#include <EGL/egl.h>
#include <ace/xcomponent/native_interface_xcomponent.h>
#include <arkui/ui_input_event.h>
#include <hilog/log.h>
#include <native_window/external_window.h>
#include <window_manager/oh_display_manager.h>

#ifndef GLFW_INCLUDE_NONE
    #define GLFW_INCLUDE_NONE
#endif
#include <GLFW/glfw3.h>

#include <dlfcn.h>
#include <time.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

struct GLFWwindow
{
    EGLSurface Surface = EGL_NO_SURFACE;
    EGLContext Context = EGL_NO_CONTEXT;
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
    GLFWcursorenterfun CursorEnterCallback = nullptr;
    GLFWscrollfun ScrollCallback = nullptr;
    GLFWwindowsizefun SizeCallback = nullptr;
    GLFWframebuffersizefun FramebufferSizeCallback = nullptr;
    GLFWwindowclosefun CloseCallback = nullptr;
    GLFWwindowfocusfun FocusCallback = nullptr;
    GLFWwindowcontentscalefun ContentScaleCallback = nullptr;
    // Motor tuş / düğme durumunu kare başına okur: aynı karede basılıp bırakılan (dokunma, hızlı tık) bırakma bir sonraki
    // kareye ertelenir, yoksa tıklama hiç görülmez
    uint64_t KeyPressFrame[GLFW_KEY_LAST + 1] = {};
    uint64_t MousePressFrame[GLFW_MOUSE_BUTTON_LAST + 1] = {};
    std::vector<std::pair<int, int>> DeferredKeyUps;   // (tuş, tarama kodu)
    std::vector<int> DeferredMouseUps;
    int CursorMode = GLFW_CURSOR_NORMAL;
};

struct GLFWmonitor
{
    int Dummy = 0;
};

struct GLFWcursor
{
    int Shape = GLFW_ARROW_CURSOR;
};

namespace
{
    constexpr unsigned int kLogDomain = 0x3C51;

    // UI iş parçacığından motora giden olay
    struct Event
    {
        enum class Type
        {
            SurfaceCreated,
            SurfaceChanged,
            SurfaceDestroyed,
            MouseMove,
            MouseButton,
            Scroll,
            Key,
            Char,
            Enter,
            Focus,
            Close,
        };
        Type Kind;
        double X = 0.0, Y = 0.0;   // imleç konumu / kaydırma / yüzey boyutu
        int A = 0;                 // düğme, tuş, karakter, açık / kapalı
        int B = 0;                 // basılı mı (1 / 0)
        int C = 0;                 // tarama kodu (XComponent tuş kodu)
        int Mods = 0;              // GLFW_MOD_* değiştiricileri
    };

    struct State
    {
        // ── UI iş parçacığı ile paylaşılan (Lock)
        std::mutex Lock;
        std::condition_variable Wake;
        std::vector<Event> Queue;
        OH_NativeXComponent* Component = nullptr;
        OHNativeWindow* NativeWindow = nullptr;   // XComponent yüzeyi (yoksa null)
        int SurfaceWidth = 0, SurfaceHeight = 0;
        uint64_t VSyncCount = 0;                  // OnFrame sayacı
        bool SurfaceInUse = false;                // motor bu yerel pencereden EGL yüzeyi kurdu
        bool DestroyAcked = false;                // motor EGL yüzeyini bıraktı
        bool Destroy = false;                     // uygulama sonlanıyor
        bool EngineRunning = false;
        bool Focused = true;
        // Değiştirme tuşları (UI iş parçacığı yazar)
        bool LShift = false, RShift = false, LCtrl = false, RCtrl = false, LAlt = false, RAlt = false, LMeta = false, RMeta = false;
        bool CapsLock = false;
        int32_t TouchId = -1;                     // fareyi süren parmak

        // ── Yalnız motor iş parçacığı
        EGLDisplay Display = EGL_NO_DISPLAY;
        EGLConfig Config = nullptr;
        GLFWwindow* Window = nullptr;
        GLFWwindow* Current = nullptr;
        GLFWerrorfun ErrorCallback = nullptr;
        GLFWmonitor Monitor;
        GLFWvidmode Mode{};
        int MinorHint = 2;
        uint64_t ConsumedVSync = 0;
        uint64_t Frame = 1;                       // glfwPollEvents sayacı
        std::string Clipboard;
        std::string ClipboardRead;                // glfwGetClipboardString dönüşü (bir sonraki çağrıya kadar geçerli)
        int CursorShape = 0;                      // ArkTS'e son bildirilen
        bool CursorHidden = false;
        float Scale = 0.0f;                       // ekran yoğunluğu (0 = okunmadı)
        timespec Start{};
    };
    State s;

    void LogInfo(const char* msg)
    {
        OH_LOG_Print(LOG_APP, LOG_INFO, kLogDomain, "Conqueror", "%{public}s", msg);
    }

    void LogError(const char* msg)
    {
        OH_LOG_Print(LOG_APP, LOG_ERROR, kLogDomain, "Conqueror", "%{public}s", msg);
    }

    // ── UI iş parçacığı: olay kuyruğu
    void Push(const Event& e)
    {
        std::lock_guard<std::mutex> lock(s.Lock);
        s.Queue.push_back(e);
    }

    int MapKey(OH_NativeXComponent_KeyCode key)
    {
        const int k = (int)key;
        if (k >= KEY_A && k <= KEY_Z) return GLFW_KEY_A + (k - KEY_A);
        if (k >= KEY_0 && k <= KEY_9) return GLFW_KEY_0 + (k - KEY_0);
        if (k >= KEY_F1 && k <= KEY_F12) return GLFW_KEY_F1 + (k - KEY_F1);
        if (k >= KEY_NUMPAD_0 && k <= KEY_NUMPAD_9) return GLFW_KEY_KP_0 + (k - KEY_NUMPAD_0);
        switch (key)
        {
            case KEY_SPACE: return GLFW_KEY_SPACE;
            case KEY_ENTER: return GLFW_KEY_ENTER;
            case KEY_NUMPAD_ENTER: return GLFW_KEY_KP_ENTER;
            case KEY_DEL: return GLFW_KEY_BACKSPACE;
            case KEY_FORWARD_DEL: return GLFW_KEY_DELETE;
            case KEY_TAB: return GLFW_KEY_TAB;
            case KEY_ESCAPE: case KEY_BACK: return GLFW_KEY_ESCAPE;
            case KEY_DPAD_UP: return GLFW_KEY_UP;
            case KEY_DPAD_DOWN: return GLFW_KEY_DOWN;
            case KEY_DPAD_LEFT: return GLFW_KEY_LEFT;
            case KEY_DPAD_RIGHT: return GLFW_KEY_RIGHT;
            case KEY_DPAD_CENTER: return GLFW_KEY_ENTER;
            case KEY_SHIFT_LEFT: return GLFW_KEY_LEFT_SHIFT;
            case KEY_SHIFT_RIGHT: return GLFW_KEY_RIGHT_SHIFT;
            case KEY_CTRL_LEFT: return GLFW_KEY_LEFT_CONTROL;
            case KEY_CTRL_RIGHT: return GLFW_KEY_RIGHT_CONTROL;
            case KEY_ALT_LEFT: return GLFW_KEY_LEFT_ALT;
            case KEY_ALT_RIGHT: return GLFW_KEY_RIGHT_ALT;
            case KEY_META_LEFT: return GLFW_KEY_LEFT_SUPER;
            case KEY_META_RIGHT: return GLFW_KEY_RIGHT_SUPER;
            case KEY_CAPS_LOCK: return GLFW_KEY_CAPS_LOCK;
            case KEY_SCROLL_LOCK: return GLFW_KEY_SCROLL_LOCK;
            case KEY_NUM_LOCK: return GLFW_KEY_NUM_LOCK;
            case KEY_SYSRQ: return GLFW_KEY_PRINT_SCREEN;
            case KEY_BREAK: return GLFW_KEY_PAUSE;
            case KEY_MENU: return GLFW_KEY_MENU;
            case KEY_MINUS: return GLFW_KEY_MINUS;
            case KEY_EQUALS: return GLFW_KEY_EQUAL;
            case KEY_LEFT_BRACKET: return GLFW_KEY_LEFT_BRACKET;
            case KEY_RIGHT_BRACKET: return GLFW_KEY_RIGHT_BRACKET;
            case KEY_BACKSLASH: return GLFW_KEY_BACKSLASH;
            case KEY_SEMICOLON: return GLFW_KEY_SEMICOLON;
            case KEY_APOSTROPHE: return GLFW_KEY_APOSTROPHE;
            case KEY_COMMA: return GLFW_KEY_COMMA;
            case KEY_PERIOD: return GLFW_KEY_PERIOD;
            case KEY_SLASH: return GLFW_KEY_SLASH;
            case KEY_GRAVE: return GLFW_KEY_GRAVE_ACCENT;
            case KEY_PAGE_UP: return GLFW_KEY_PAGE_UP;
            case KEY_PAGE_DOWN: return GLFW_KEY_PAGE_DOWN;
            case KEY_MOVE_HOME: return GLFW_KEY_HOME;
            case KEY_MOVE_END: return GLFW_KEY_END;
            case KEY_INSERT: return GLFW_KEY_INSERT;
            case KEY_NUMPAD_DIVIDE: return GLFW_KEY_KP_DIVIDE;
            case KEY_NUMPAD_MULTIPLY: return GLFW_KEY_KP_MULTIPLY;
            case KEY_NUMPAD_SUBTRACT: return GLFW_KEY_KP_SUBTRACT;
            case KEY_NUMPAD_ADD: return GLFW_KEY_KP_ADD;
            case KEY_NUMPAD_DOT: case KEY_NUMPAD_COMMA: return GLFW_KEY_KP_DECIMAL;
            case KEY_NUMPAD_EQUALS: return GLFW_KEY_KP_EQUAL;
            case KEY_102ND: return GLFW_KEY_WORLD_1;
            default: return GLFW_KEY_UNKNOWN;
        }
    }

    // Donanım klavyesinden ASCII karakter (ABD düzeni). Diğer diller / IME metni ArkTS giriş yönteminden InsertText ile gelir
    unsigned int CharFor(int glfwKey, bool shift, bool caps)
    {
        if (glfwKey >= GLFW_KEY_A && glfwKey <= GLFW_KEY_Z)
            return (unsigned int)(((shift != caps) ? 'A' : 'a') + (glfwKey - GLFW_KEY_A));
        if (glfwKey >= GLFW_KEY_0 && glfwKey <= GLFW_KEY_9)
            return (unsigned int)(shift ? ")!@#$%^&*("[glfwKey - GLFW_KEY_0] : '0' + (glfwKey - GLFW_KEY_0));
        if (glfwKey >= GLFW_KEY_KP_0 && glfwKey <= GLFW_KEY_KP_9)
            return (unsigned int)('0' + (glfwKey - GLFW_KEY_KP_0));
        switch (glfwKey)
        {
            case GLFW_KEY_SPACE: return ' ';
            case GLFW_KEY_MINUS: return shift ? '_' : '-';
            case GLFW_KEY_EQUAL: return shift ? '+' : '=';
            case GLFW_KEY_LEFT_BRACKET: return shift ? '{' : '[';
            case GLFW_KEY_RIGHT_BRACKET: return shift ? '}' : ']';
            case GLFW_KEY_BACKSLASH: return shift ? '|' : '\\';
            case GLFW_KEY_SEMICOLON: return shift ? ':' : ';';
            case GLFW_KEY_APOSTROPHE: return shift ? '"' : '\'';
            case GLFW_KEY_COMMA: return shift ? '<' : ',';
            case GLFW_KEY_PERIOD: return shift ? '>' : '.';
            case GLFW_KEY_SLASH: return shift ? '?' : '/';
            case GLFW_KEY_GRAVE_ACCENT: return shift ? '~' : '`';
            case GLFW_KEY_KP_DIVIDE: return '/';
            case GLFW_KEY_KP_MULTIPLY: return '*';
            case GLFW_KEY_KP_SUBTRACT: return '-';
            case GLFW_KEY_KP_ADD: return '+';
            case GLFW_KEY_KP_DECIMAL: return '.';
            default: return 0;
        }
    }

    // s.Lock tutulurken
    int ModsLocked()
    {
        int m = 0;
        if (s.LShift || s.RShift) m |= GLFW_MOD_SHIFT;
        if (s.LCtrl || s.RCtrl) m |= GLFW_MOD_CONTROL;
        if (s.LAlt || s.RAlt) m |= GLFW_MOD_ALT;
        if (s.LMeta || s.RMeta) m |= GLFW_MOD_SUPER;
        if (s.CapsLock) m |= GLFW_MOD_CAPS_LOCK;
        return m;
    }

    // ── XComponent geri çağrıları (UI iş parçacığı)
    void UpdateSurfaceSize(OH_NativeXComponent* component, void* window)
    {
        uint64_t width = 0, height = 0;
        if (OH_NativeXComponent_GetXComponentSize(component, window, &width, &height) == OH_NATIVEXCOMPONENT_RESULT_SUCCESS)
        {
            s.SurfaceWidth = (int)width;
            s.SurfaceHeight = (int)height;
        }
    }

    void OnSurfaceCreated(OH_NativeXComponent* component, void* window)
    {
        {
            std::lock_guard<std::mutex> lock(s.Lock);
            s.Component = component;
            s.NativeWindow = static_cast<OHNativeWindow*>(window);
            UpdateSurfaceSize(component, window);
            s.Queue.push_back({ Event::Type::SurfaceCreated, (double)s.SurfaceWidth, (double)s.SurfaceHeight });
        }
        s.Wake.notify_all();
        LogInfo("XComponent surface created");
    }

    void OnSurfaceChanged(OH_NativeXComponent* component, void* window)
    {
        {
            std::lock_guard<std::mutex> lock(s.Lock);
            UpdateSurfaceSize(component, window);
            if (window && window == s.NativeWindow && s.SurfaceWidth > 0 && s.SurfaceHeight > 0)
                OH_NativeWindow_NativeWindowHandleOpt(s.NativeWindow, SET_BUFFER_GEOMETRY, s.SurfaceWidth, s.SurfaceHeight);
            s.Queue.push_back({ Event::Type::SurfaceChanged, (double)s.SurfaceWidth, (double)s.SurfaceHeight });
        }
        s.Wake.notify_all();
    }

    void OnSurfaceDestroyed(OH_NativeXComponent*, void* window)
    {
        std::unique_lock<std::mutex> lock(s.Lock);
        if (window != s.NativeWindow)
            return;
        if (s.SurfaceInUse && s.EngineRunning && !s.Destroy)
        {
            // Yerel pencere bu dönüşten sonra geçersizdir: motor EGL yüzeyini bırakana kadar bekle (en çok 3 sn; motor uzun bir
            // iş yapıyorsa UI iş parçacığı donmasın)
            s.DestroyAcked = false;
            s.Queue.push_back({ Event::Type::SurfaceDestroyed });
            s.Wake.notify_all();
            s.Wake.wait_for(lock, std::chrono::seconds(3), [] { return s.DestroyAcked || !s.EngineRunning; });
        }
        s.NativeWindow = nullptr;
        s.SurfaceInUse = false;
        LogInfo("XComponent surface destroyed");
    }

    void OnTouch(OH_NativeXComponent* component, void* window)
    {
        OH_NativeXComponent_TouchEvent touch{};
        if (OH_NativeXComponent_GetTouchEvent(component, window, &touch) != OH_NATIVEXCOMPONENT_RESULT_SUCCESS)
            return;
        // Fare ve dokunmatik yüzey de dokunma olayı üretir; onlar fare geri çağrısından gelir
        OH_NativeXComponent_EventSourceType source = OH_NATIVEXCOMPONENT_SOURCE_TYPE_UNKNOWN;
        if (OH_NativeXComponent_GetTouchEventSourceType(component, touch.id, &source) == OH_NATIVEXCOMPONENT_RESULT_SUCCESS &&
            (source == OH_NATIVEXCOMPONENT_SOURCE_TYPE_MOUSE || source == OH_NATIVEXCOMPONENT_SOURCE_TYPE_TOUCHPAD))
            return;
        std::lock_guard<std::mutex> lock(s.Lock);
        switch (touch.type)
        {
            case OH_NATIVEXCOMPONENT_DOWN:
                if (s.TouchId >= 0)
                    return;   // ilk parmak fareyi sürer
                s.TouchId = touch.id;
                s.Queue.push_back({ Event::Type::MouseMove, touch.x, touch.y });
                s.Queue.push_back({ Event::Type::MouseButton, touch.x, touch.y, GLFW_MOUSE_BUTTON_LEFT, 1 });
                break;
            case OH_NATIVEXCOMPONENT_MOVE:
                if (touch.id == s.TouchId)
                    s.Queue.push_back({ Event::Type::MouseMove, touch.x, touch.y });
                break;
            case OH_NATIVEXCOMPONENT_UP:
            case OH_NATIVEXCOMPONENT_CANCEL:
                if (touch.id != s.TouchId)
                    return;
                s.TouchId = -1;
                s.Queue.push_back({ Event::Type::MouseMove, touch.x, touch.y });
                s.Queue.push_back({ Event::Type::MouseButton, touch.x, touch.y, GLFW_MOUSE_BUTTON_LEFT, 0 });
                break;
            default:
                break;
        }
    }

    void OnMouse(OH_NativeXComponent* component, void* window)
    {
        OH_NativeXComponent_MouseEvent mouse{};
        if (OH_NativeXComponent_GetMouseEvent(component, window, &mouse) != OH_NATIVEXCOMPONENT_RESULT_SUCCESS)
            return;
        int button = -1;
        switch (mouse.button)
        {
            case OH_NATIVEXCOMPONENT_LEFT_BUTTON: button = GLFW_MOUSE_BUTTON_LEFT; break;
            case OH_NATIVEXCOMPONENT_RIGHT_BUTTON: button = GLFW_MOUSE_BUTTON_RIGHT; break;
            case OH_NATIVEXCOMPONENT_MIDDLE_BUTTON: button = GLFW_MOUSE_BUTTON_MIDDLE; break;
            case OH_NATIVEXCOMPONENT_BACK_BUTTON: button = GLFW_MOUSE_BUTTON_4; break;
            case OH_NATIVEXCOMPONENT_FORWARD_BUTTON: button = GLFW_MOUSE_BUTTON_5; break;
            default: break;
        }
        std::lock_guard<std::mutex> lock(s.Lock);
        s.Queue.push_back({ Event::Type::MouseMove, mouse.x, mouse.y });
        if (button >= 0 && (mouse.action == OH_NATIVEXCOMPONENT_MOUSE_PRESS || mouse.action == OH_NATIVEXCOMPONENT_MOUSE_RELEASE))
            s.Queue.push_back({ Event::Type::MouseButton, mouse.x, mouse.y, button, mouse.action == OH_NATIVEXCOMPONENT_MOUSE_PRESS ? 1 : 0,
                                0, ModsLocked() });
    }

    void OnHover(OH_NativeXComponent*, bool hover)
    {
        Push({ Event::Type::Enter, 0.0, 0.0, hover ? 1 : 0 });
    }

    void OnKey(OH_NativeXComponent* component, void*)
    {
        OH_NativeXComponent_KeyEvent* key = nullptr;
        if (OH_NativeXComponent_GetKeyEvent(component, &key) != OH_NATIVEXCOMPONENT_RESULT_SUCCESS || !key)
            return;
        OH_NativeXComponent_KeyAction action = OH_NATIVEXCOMPONENT_KEY_ACTION_UNKNOWN;
        OH_NativeXComponent_KeyCode code = KEY_UNKNOWN;
        OH_NativeXComponent_GetKeyEventAction(key, &action);
        OH_NativeXComponent_GetKeyEventCode(key, &code);
        if (action != OH_NATIVEXCOMPONENT_KEY_ACTION_DOWN && action != OH_NATIVEXCOMPONENT_KEY_ACTION_UP)
            return;
        const bool down = action == OH_NATIVEXCOMPONENT_KEY_ACTION_DOWN;
        const int glfwKey = MapKey(code);
        std::lock_guard<std::mutex> lock(s.Lock);
        switch (code)
        {
            case KEY_SHIFT_LEFT: s.LShift = down; break;
            case KEY_SHIFT_RIGHT: s.RShift = down; break;
            case KEY_CTRL_LEFT: s.LCtrl = down; break;
            case KEY_CTRL_RIGHT: s.RCtrl = down; break;
            case KEY_ALT_LEFT: s.LAlt = down; break;
            case KEY_ALT_RIGHT: s.RAlt = down; break;
            case KEY_META_LEFT: s.LMeta = down; break;
            case KEY_META_RIGHT: s.RMeta = down; break;
            case KEY_CAPS_LOCK: if (down) s.CapsLock = !s.CapsLock; break;
            default: break;
        }
        if (glfwKey == GLFW_KEY_UNKNOWN)
            return;
        const int mods = ModsLocked();
        s.Queue.push_back({ Event::Type::Key, 0.0, 0.0, glfwKey, down ? 1 : 0, (int)code, mods });
        // Kısayol (Ctrl / Alt / Meta) değilse karakter
        if (down && !(mods & (GLFW_MOD_CONTROL | GLFW_MOD_ALT | GLFW_MOD_SUPER)))
            if (const unsigned int c = CharFor(glfwKey, (mods & GLFW_MOD_SHIFT) != 0, s.CapsLock))
                s.Queue.push_back({ Event::Type::Char, 0.0, 0.0, (int)c });
    }

    void OnFocus(OH_NativeXComponent*, void*)
    {
        std::lock_guard<std::mutex> lock(s.Lock);
        s.Focused = true;
        s.Queue.push_back({ Event::Type::Focus, 0.0, 0.0, 1 });
    }

    void OnBlur(OH_NativeXComponent*, void*)
    {
        {
            // Odak giderken basılı kalan değiştirme tuşları (Alt+Tab ...) takılı kalmasın
            std::lock_guard<std::mutex> lock(s.Lock);
            s.LShift = s.RShift = s.LCtrl = s.RCtrl = s.LAlt = s.RAlt = s.LMeta = s.RMeta = false;
            s.Focused = false;
        }
        Push({ Event::Type::Focus, 0.0, 0.0, 0 });
    }

    void OnFrame(OH_NativeXComponent*, uint64_t, uint64_t)
    {
        {
            std::lock_guard<std::mutex> lock(s.Lock);
            ++s.VSyncCount;
        }
        s.Wake.notify_all();
    }

    // Fare tekerleği / dokunmatik yüzey kaydırması (ArkUI eksen olayı). Bir tekerlek çentiği 15 birim sayılır (OpenHarmony çok
    // modlu girdi biçimi; işaret aygıtta doğrulanmadı): CQ_OHOS_WHEEL_SCALE ile ölçek / yön değiştirilebilir
    void OnAxis(OH_NativeXComponent*, ArkUI_UIInputEvent* event, ArkUI_UIInputEvent_Type type)
    {
        if (type != ARKUI_UIINPUTEVENT_TYPE_AXIS || !event)
            return;
        static const double scale = [] {
            const char* env = std::getenv("CQ_OHOS_WHEEL_SCALE");
            return env && *env ? std::atof(env) : -1.0 / 15.0;
        }();
        const double v = OH_ArkUI_AxisEvent_GetVerticalAxisValue(event);
        const double h = OH_ArkUI_AxisEvent_GetHorizontalAxisValue(event);
        if (v != 0.0 || h != 0.0)
            Push({ Event::Type::Scroll, h * scale, v * scale });
    }

    OH_NativeXComponent_Callback s_SurfaceCallbacks{ &OnSurfaceCreated, &OnSurfaceChanged, &OnSurfaceDestroyed, &OnTouch };
    OH_NativeXComponent_MouseEvent_Callback s_MouseCallbacks{ &OnMouse, &OnHover };

    // ── Motor iş parçacığı
    float DensityScale()
    {
        if (s.Scale <= 0.0f)
        {
            float density = 0.0f;
            s.Scale = OH_NativeDisplayManager_GetDefaultDisplayDensityPixels(&density) == DISPLAY_MANAGER_OK && density > 0.0f ? density : 1.0f;
        }
        return s.Scale;
    }

    void SetMouse(GLFWwindow* w, int button, bool down, int mods)
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
        if (w->MouseButtonCallback) w->MouseButtonCallback(w, button, state, mods);
    }

    void SetKey(GLFWwindow* w, int key, int scancode, bool down, int mods)
    {
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

    bool InitDisplay()
    {
        if (s.Display != EGL_NO_DISPLAY) return true;
        s.Display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        if (s.Display == EGL_NO_DISPLAY || !eglInitialize(s.Display, nullptr, nullptr))
        {
            s.Display = EGL_NO_DISPLAY;
            return false;
        }
        const EGLint attribs[] = {
            EGL_RENDERABLE_TYPE, 0x40 /* EGL_OPENGL_ES3_BIT */, EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
            EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_DEPTH_SIZE, 24, EGL_STENCIL_SIZE, 8, EGL_NONE,
        };
        EGLint count = 0;
        if (!eglChooseConfig(s.Display, attribs, &s.Config, 1, &count) || count == 0)
        {
            const EGLint fallback[] = { EGL_RENDERABLE_TYPE, 0x40, EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_DEPTH_SIZE, 16, EGL_NONE };
            if (!eglChooseConfig(s.Display, fallback, &s.Config, 1, &count) || count == 0) return false;
        }
        return true;
    }

    // Kilit tutulmadan çağrılır. Çağıran yerel pencereyi kilit altında alırken SurfaceInUse'u işaretler: UI iş parçacığı o andan
    // sonra gelen yok etmede motorun onayını bekler (yarım kurulan yüzey ölü pencereye bağlanmasın)
    bool CreateSurface(GLFWwindow* w, OHNativeWindow* native)
    {
        if (native && s.Display != EGL_NO_DISPLAY)
            w->Surface = eglCreateWindowSurface(s.Display, s.Config, (EGLNativeWindowType)native, nullptr);
        if (w->Surface == EGL_NO_SURFACE)
        {
            LogError("eglCreateWindowSurface failed");
            {
                std::lock_guard<std::mutex> lock(s.Lock);
                s.SurfaceInUse = false;
            }
            s.Wake.notify_all();
            return false;
        }
        EGLint width = 0, height = 0;
        eglQuerySurface(s.Display, w->Surface, EGL_WIDTH, &width);
        eglQuerySurface(s.Display, w->Surface, EGL_HEIGHT, &height);
        {
            std::lock_guard<std::mutex> lock(s.Lock);
            if (width <= 0 || height <= 0)
            {
                width = s.SurfaceWidth;
                height = s.SurfaceHeight;
            }
        }
        w->Width = width;
        w->Height = height;
        s.Mode.width = width;
        s.Mode.height = height;
        if (s.Current == w)
            eglMakeCurrent(s.Display, w->Surface, w->Surface, w->Context);
        return true;
    }

    void DestroySurface(GLFWwindow* w)
    {
        if (!w || w->Surface == EGL_NO_SURFACE) return;
        eglMakeCurrent(s.Display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroySurface(s.Display, w->Surface);
        w->Surface = EGL_NO_SURFACE;
        // Bağlam yüzeysiz geçerli kalsın (motor arka planda kaynak bırakabilir; EGL_KHR_surfaceless_context)
        if (s.Current == w)
            eglMakeCurrent(s.Display, EGL_NO_SURFACE, EGL_NO_SURFACE, w->Context);
    }

    void Resize(GLFWwindow* w, int width, int height)
    {
        if (!w || width <= 0 || height <= 0 || (width == w->Width && height == w->Height))
            return;
        w->Width = width;
        w->Height = height;
        s.Mode.width = width;
        s.Mode.height = height;
        if (w->SizeCallback) w->SizeCallback(w, width, height);
        if (w->FramebufferSizeCallback) w->FramebufferSizeCallback(w, width, height);
    }

    void Dispatch(const Event& e)
    {
        GLFWwindow* w = s.Window;
        switch (e.Kind)
        {
            case Event::Type::SurfaceCreated:
                if (w && w->Surface == EGL_NO_SURFACE)
                {
                    OHNativeWindow* native = nullptr;
                    {
                        std::lock_guard<std::mutex> lock(s.Lock);
                        native = s.NativeWindow;
                        s.SurfaceInUse = native != nullptr;
                    }
                    if (CreateSurface(w, native))
                    {
                        if (w->SizeCallback) w->SizeCallback(w, w->Width, w->Height);
                        if (w->FramebufferSizeCallback) w->FramebufferSizeCallback(w, w->Width, w->Height);
                    }
                }
                break;
            case Event::Type::SurfaceChanged:
                Resize(w, (int)e.X, (int)e.Y);
                break;
            case Event::Type::SurfaceDestroyed:
                DestroySurface(w);
                {
                    std::lock_guard<std::mutex> lock(s.Lock);
                    s.DestroyAcked = true;
                    s.SurfaceInUse = false;
                }
                s.Wake.notify_all();
                break;
            case Event::Type::MouseMove:
                if (!w) break;
                w->CursorX = e.X;
                w->CursorY = e.Y;
                if (w->CursorPosCallback) w->CursorPosCallback(w, e.X, e.Y);
                break;
            case Event::Type::MouseButton:
                SetMouse(w, e.A, e.B != 0, e.Mods);
                break;
            case Event::Type::Scroll:
                if (w && w->ScrollCallback) w->ScrollCallback(w, e.X, e.Y);
                break;
            case Event::Type::Key:
                if (w && e.A >= 0 && e.A <= GLFW_KEY_LAST)
                    SetKey(w, e.A, e.C, e.B != 0, e.Mods);
                break;
            case Event::Type::Char:
                if (w && w->CharCallback) w->CharCallback(w, (unsigned int)e.A);
                break;
            case Event::Type::Enter:
                if (w && w->CursorEnterCallback) w->CursorEnterCallback(w, e.A);
                break;
            case Event::Type::Focus:
                if (w && w->FocusCallback) w->FocusCallback(w, e.A);
                break;
            case Event::Type::Close:
                if (!w) break;
                w->ShouldClose = true;
                if (w->CloseCallback) w->CloseCallback(w);
                break;
        }
    }

    // Yeni kare: önceki karede ertelenen bırakmalar uygulanır
    void BeginFrame()
    {
        ++s.Frame;
        GLFWwindow* w = s.Window;
        if (!w)
            return;
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

    // Kuyruktaki olayları işler. waitVSync: bir sonraki ekran tazelemesini (OnFrame) bekle; kare saati gelmezse (yüzey gizli,
    // eski sistem) en çok 50 ms
    void PumpEvents(bool waitVSync, bool block)
    {
        std::vector<Event> events;
        {
            std::unique_lock<std::mutex> lock(s.Lock);
            if (block)
                s.Wake.wait(lock, [] { return !s.Queue.empty() || s.Destroy; });
            else if (waitVSync)
                s.Wake.wait_for(lock, std::chrono::milliseconds(50), [] {
                    return s.VSyncCount != s.ConsumedVSync || s.Destroy ||
                           std::any_of(s.Queue.begin(), s.Queue.end(), [](const Event& e) { return e.Kind == Event::Type::SurfaceDestroyed; });
                });
            s.ConsumedVSync = s.VSyncCount;
            events.swap(s.Queue);
        }
        for (const Event& e : events)
            Dispatch(e);
    }

    // Kare saati, girdi ve tekerlek geri çağrılarını kaydeder (ArkTS ana iş parçacığı)
    void RegisterCallbacks(OH_NativeXComponent* component)
    {
        OH_NativeXComponent_RegisterCallback(component, &s_SurfaceCallbacks);
        OH_NativeXComponent_RegisterMouseEventCallback(component, &s_MouseCallbacks);
        OH_NativeXComponent_RegisterKeyEventCallback(component, &OnKey);
        OH_NativeXComponent_RegisterFocusEventCallback(component, &OnFocus);
        OH_NativeXComponent_RegisterBlurEventCallback(component, &OnBlur);
        OH_NativeXComponent_RegisterOnFrameCallback(component, &OnFrame);
        OH_NativeXComponent_RegisterUIInputEventCallback(component, &OnAxis, ARKUI_UIINPUTEVENT_TYPE_AXIS);
    }

    // ── Pano: OH_Pasteboard (API 13+) ve UDMF düz metin kaydı, çalışma anında açılır (API 12 cihazlarda süreç içi pano)
    struct Pasteboard
    {
        using CreateFn = void* (*)();
        using DestroyFn = void (*)(void*);
        using GetDataFn = void* (*)(void*, int*);
        using SetDataFn = int (*)(void*, void*);
        using DataCreateFn = void* (*)();
        using DataDestroyFn = void (*)(void*);
        using DataAddRecordFn = int (*)(void*, void*);
        using DataGetRecordsFn = void** (*)(void*, unsigned int*);
        using RecordCreateFn = void* (*)();
        using RecordDestroyFn = void (*)(void*);
        using RecordAddPlainTextFn = int (*)(void*, void*);
        using RecordGetPlainTextFn = int (*)(void*, void*);
        using TextCreateFn = void* (*)();
        using TextDestroyFn = void (*)(void*);
        using TextGetFn = const char* (*)(void*);
        using TextSetFn = int (*)(void*, const char*);

        bool Ready = false;
        void* Board = nullptr;
        CreateFn Create = nullptr;
        GetDataFn GetData = nullptr;
        SetDataFn SetData = nullptr;
        DataCreateFn DataCreate = nullptr;
        DataDestroyFn DataDestroy = nullptr;
        DataAddRecordFn DataAddRecord = nullptr;
        DataGetRecordsFn DataGetRecords = nullptr;
        RecordCreateFn RecordCreate = nullptr;
        RecordDestroyFn RecordDestroy = nullptr;
        RecordAddPlainTextFn RecordAddPlainText = nullptr;
        RecordGetPlainTextFn RecordGetPlainText = nullptr;
        TextCreateFn TextCreate = nullptr;
        TextDestroyFn TextDestroy = nullptr;
        TextGetFn TextGet = nullptr;
        TextSetFn TextSet = nullptr;

        static Pasteboard& Get()
        {
            static Pasteboard p = [] {
                Pasteboard r;
                void* pb = dlopen("libpasteboard.so", RTLD_NOW | RTLD_LOCAL);
                void* udmf = dlopen("libudmf.so", RTLD_NOW | RTLD_LOCAL);
                if (!pb || !udmf)
                    return r;
                auto sym = [](void* lib, const char* name) { return dlsym(lib, name); };
                r.Create = (CreateFn)sym(pb, "OH_Pasteboard_Create");
                r.GetData = (GetDataFn)sym(pb, "OH_Pasteboard_GetData");
                r.SetData = (SetDataFn)sym(pb, "OH_Pasteboard_SetData");
                r.DataCreate = (DataCreateFn)sym(udmf, "OH_UdmfData_Create");
                r.DataDestroy = (DataDestroyFn)sym(udmf, "OH_UdmfData_Destroy");
                r.DataAddRecord = (DataAddRecordFn)sym(udmf, "OH_UdmfData_AddRecord");
                r.DataGetRecords = (DataGetRecordsFn)sym(udmf, "OH_UdmfData_GetRecords");
                r.RecordCreate = (RecordCreateFn)sym(udmf, "OH_UdmfRecord_Create");
                r.RecordDestroy = (RecordDestroyFn)sym(udmf, "OH_UdmfRecord_Destroy");
                r.RecordAddPlainText = (RecordAddPlainTextFn)sym(udmf, "OH_UdmfRecord_AddPlainText");
                r.RecordGetPlainText = (RecordGetPlainTextFn)sym(udmf, "OH_UdmfRecord_GetPlainText");
                r.TextCreate = (TextCreateFn)sym(udmf, "OH_UdsPlainText_Create");
                r.TextDestroy = (TextDestroyFn)sym(udmf, "OH_UdsPlainText_Destroy");
                r.TextGet = (TextGetFn)sym(udmf, "OH_UdsPlainText_GetContent");
                r.TextSet = (TextSetFn)sym(udmf, "OH_UdsPlainText_SetContent");
                r.Ready = r.Create && r.GetData && r.SetData && r.DataCreate && r.DataDestroy && r.DataAddRecord && r.DataGetRecords &&
                          r.RecordCreate && r.RecordDestroy && r.RecordAddPlainText && r.RecordGetPlainText && r.TextCreate &&
                          r.TextDestroy && r.TextGet && r.TextSet;
                if (r.Ready)
                    r.Board = r.Create();
                r.Ready = r.Ready && r.Board;
                return r;
            }();
            return p;
        }

        bool Write(const std::string& text)
        {
            if (!Ready) return false;
            void* data = DataCreate();
            void* record = RecordCreate();
            void* plain = TextCreate();
            bool ok = data && record && plain && TextSet(plain, text.c_str()) == 0 && RecordAddPlainText(record, plain) == 0 &&
                      DataAddRecord(data, record) == 0 && SetData(Board, data) == 0;
            if (plain) TextDestroy(plain);
            if (record) RecordDestroy(record);
            if (data) DataDestroy(data);
            return ok;
        }

        // Okuma izni (ohos.permission.READ_PASTEBOARD) yoksa sistem boş / hata döndürür: false
        bool Read(std::string& out)
        {
            if (!Ready) return false;
            int status = 0;
            void* data = GetData(Board, &status);
            if (!data) return false;
            bool ok = false;
            unsigned int count = 0;
            void** records = DataGetRecords(data, &count);
            for (unsigned int i = 0; records && i < count && !ok; ++i)
            {
                void* plain = TextCreate();
                if (plain && RecordGetPlainText(records[i], plain) == 0)
                    if (const char* text = TextGet(plain))
                    {
                        out = text;
                        ok = true;
                    }
                if (plain) TextDestroy(plain);
            }
            DataDestroy(data);
            return ok;
        }
    };

    const char* CursorName(int shape)
    {
        switch (shape)
        {
            case GLFW_IBEAM_CURSOR: return "ibeam";
            case GLFW_CROSSHAIR_CURSOR: return "crosshair";
            case GLFW_POINTING_HAND_CURSOR: return "hand";
            case GLFW_RESIZE_EW_CURSOR: return "resize_ew";
            case GLFW_RESIZE_NS_CURSOR: return "resize_ns";
            case GLFW_RESIZE_NWSE_CURSOR: return "resize_nwse";
            case GLFW_RESIZE_NESW_CURSOR: return "resize_nesw";
            case GLFW_RESIZE_ALL_CURSOR: return "resize_all";
            case GLFW_NOT_ALLOWED_CURSOR: return "not_allowed";
            default: return "arrow";
        }
    }

    void ApplyCursor(int shape, bool hidden)
    {
        if (shape == s.CursorShape && hidden == s.CursorHidden)
            return;
        s.CursorShape = shape;
        s.CursorHidden = hidden;
        Conqueror::OHOS::PostHostRequest("cursor", hidden ? "hidden" : CursorName(shape));
    }
}

namespace Conqueror::OHOS::Detail
{
    void AttachXComponent(OH_NativeXComponent* component)
    {
        if (!component)
            return;
        {
            std::lock_guard<std::mutex> lock(s.Lock);
            if (s.Component == component)
                return;
            s.Component = component;
        }
        RegisterCallbacks(component);
        LogInfo("XComponent attached");
    }

    void InsertText(const std::string& utf8)
    {
        // UTF-8 -> kod noktaları
        std::lock_guard<std::mutex> lock(s.Lock);
        for (size_t i = 0; i < utf8.size();)
        {
            const unsigned char c = (unsigned char)utf8[i];
            unsigned int cp = 0;
            size_t n = 1;
            if (c < 0x80) cp = c;
            else if ((c >> 5) == 0x6) { cp = c & 0x1F; n = 2; }
            else if ((c >> 4) == 0xE) { cp = c & 0x0F; n = 3; }
            else if ((c >> 3) == 0x1E) { cp = c & 0x07; n = 4; }
            else { ++i; continue; }
            if (i + n > utf8.size()) break;
            for (size_t k = 1; k < n; ++k)
                cp = (cp << 6) | ((unsigned char)utf8[i + k] & 0x3F);
            i += n;
            if (cp >= 0x20 && cp != 0x7F)
                s.Queue.push_back({ Event::Type::Char, 0.0, 0.0, (int)cp });
        }
    }

    void RequestClose()
    {
        Push({ Event::Type::Close });
        s.Wake.notify_all();
    }

    void RequestDestroy()
    {
        {
            std::lock_guard<std::mutex> lock(s.Lock);
            s.Destroy = true;
            s.Queue.push_back({ Event::Type::Close });
        }
        s.Wake.notify_all();
    }

    void EngineFinished()
    {
        {
            std::lock_guard<std::mutex> lock(s.Lock);
            s.EngineRunning = false;
        }
        s.Wake.notify_all();
    }
}

// ══════════════════════════════════════════ GLFW API (motor iş parçacığı)
extern "C"
{
    int glfwInit(void)
    {
        if (s.Start.tv_sec == 0 && s.Start.tv_nsec == 0)
            clock_gettime(CLOCK_MONOTONIC, &s.Start);
        s.Mode.refreshRate = 60;
        s.Mode.redBits = s.Mode.greenBits = s.Mode.blueBits = 8;
        std::lock_guard<std::mutex> lock(s.Lock);
        s.EngineRunning = true;
        return GLFW_TRUE;
    }

    void glfwTerminate(void)
    {
        if (s.Window) glfwDestroyWindow(s.Window);
        if (s.Display != EGL_NO_DISPLAY) eglTerminate(s.Display);
        s.Display = EGL_NO_DISPLAY;
    }

    void glfwInitHint(int, int) {}
    int glfwPlatformSupported(int platform) { return platform == GLFW_PLATFORM_NULL ? GLFW_TRUE : GLFW_FALSE; }
    int glfwGetPlatform(void) { return GLFW_PLATFORM_NULL; }

    GLFWerrorfun glfwSetErrorCallback(GLFWerrorfun callback)
    {
        GLFWerrorfun prev = s.ErrorCallback;
        s.ErrorCallback = callback;
        return prev;
    }

    void glfwDefaultWindowHints(void) { s.MinorHint = 2; }

    void glfwWindowHint(int hint, int value)
    {
        if (hint == GLFW_CONTEXT_VERSION_MINOR) s.MinorHint = value;
    }

    void glfwWindowHintString(int, const char*) {}

    GLFWwindow* glfwCreateWindow(int, int, const char* title, GLFWmonitor*, GLFWwindow*)
    {
        if (s.Window) return nullptr;   // tek pencere (XComponent yüzeyi); ek pencereler bu platformda açılmaz
        // XComponent yüzeyini bekle
        OHNativeWindow* native = nullptr;
        {
            std::unique_lock<std::mutex> lock(s.Lock);
            s.Wake.wait(lock, [] { return s.NativeWindow != nullptr || s.Destroy; });
            native = s.NativeWindow;
            s.SurfaceInUse = native != nullptr;
            // Yüzey olayları kuyrukta kalmasın (pencere şimdi kuruluyor)
            s.Queue.erase(std::remove_if(s.Queue.begin(), s.Queue.end(), [](const Event& e) {
                return e.Kind == Event::Type::SurfaceCreated || e.Kind == Event::Type::SurfaceChanged; }), s.Queue.end());
        }
        if (!native || !InitDisplay())
        {
            LogError("EGL display / XComponent surface unavailable");
            {
                std::lock_guard<std::mutex> lock(s.Lock);
                s.SurfaceInUse = false;
            }
            s.Wake.notify_all();
            return nullptr;
        }
        auto* w = new GLFWwindow();
        for (int minor : { std::max(s.MinorHint, 2), 1, 0 })
        {
            const EGLint ctx[] = { 0x3098 /* EGL_CONTEXT_MAJOR_VERSION */, 3, 0x30FB /* EGL_CONTEXT_MINOR_VERSION */, minor, EGL_NONE };
            w->Context = eglCreateContext(s.Display, s.Config, EGL_NO_CONTEXT, ctx);
            if (w->Context != EGL_NO_CONTEXT) break;
        }
        if (w->Context == EGL_NO_CONTEXT || !CreateSurface(w, native))
        {
            LogError("OpenGL ES 3 context creation failed");
            {
                std::lock_guard<std::mutex> lock(s.Lock);
                s.SurfaceInUse = false;
            }
            s.Wake.notify_all();
            if (w->Context != EGL_NO_CONTEXT) eglDestroyContext(s.Display, w->Context);
            delete w;
            return nullptr;
        }
        s.Window = w;
        if (title && *title)
            Conqueror::OHOS::PostHostRequest("title", title);
        return w;
    }

    void glfwDestroyWindow(GLFWwindow* w)
    {
        if (!w) return;
        DestroySurface(w);
        {
            std::lock_guard<std::mutex> lock(s.Lock);
            s.SurfaceInUse = false;
        }
        s.Wake.notify_all();
        if (w->Context != EGL_NO_CONTEXT) eglDestroyContext(s.Display, w->Context);
        if (s.Window == w) s.Window = nullptr;
        if (s.Current == w) s.Current = nullptr;
        delete w;
    }

    int glfwWindowShouldClose(GLFWwindow* w)
    {
        std::lock_guard<std::mutex> lock(s.Lock);
        return w && (w->ShouldClose || s.Destroy) ? GLFW_TRUE : GLFW_FALSE;
    }
    void glfwSetWindowShouldClose(GLFWwindow* w, int value) { if (w) w->ShouldClose = value != 0; }

    void glfwMakeContextCurrent(GLFWwindow* w)
    {
        s.Current = w;
        if (s.Display == EGL_NO_DISPLAY) return;
        if (w) eglMakeCurrent(s.Display, w->Surface, w->Surface, w->Context);
        else eglMakeCurrent(s.Display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    }

    GLFWwindow* glfwGetCurrentContext(void) { return s.Current; }

    void glfwSwapBuffers(GLFWwindow* w)
    {
        if (w && w->Surface != EGL_NO_SURFACE) eglSwapBuffers(s.Display, w->Surface);
    }

    void glfwSwapInterval(int interval)
    {
        if (s.Display != EGL_NO_DISPLAY) eglSwapInterval(s.Display, interval);
    }

    // EGL 1.4'te eglGetProcAddress çekirdek GLES işlevlerini döndürmek zorunda değil: önce libGLESv3'ten
    GLFWglproc glfwGetProcAddress(const char* name)
    {
        static void* gles = dlopen("libGLESv3.so", RTLD_NOW | RTLD_LOCAL);
        if (gles)
            if (void* p = dlsym(gles, name))
                return (GLFWglproc)p;
        return (GLFWglproc)eglGetProcAddress(name);
    }

    int glfwExtensionSupported(const char*) { return GLFW_FALSE; }

    void glfwPollEvents(void)
    {
        BeginFrame();
        PumpEvents(true, false);
        // Yüzey yok (arka plan, simge durumu): pencere dönene ya da kapanış istenene kadar bekle
        for (;;)
        {
            GLFWwindow* w = s.Window;
            bool destroy = false;
            {
                std::lock_guard<std::mutex> lock(s.Lock);
                destroy = s.Destroy;
            }
            if (!w || w->Surface != EGL_NO_SURFACE || destroy || w->ShouldClose)
                break;
            PumpEvents(false, true);
        }
    }

    void glfwWaitEvents(void) { PumpEvents(false, true); }
    void glfwWaitEventsTimeout(double) { PumpEvents(true, false); }
    void glfwPostEmptyEvent(void) { s.Wake.notify_all(); }

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
    void glfwSetWindowAspectRatio(GLFWwindow*, int, int) {}
    void glfwSetWindowTitle(GLFWwindow*, const char* title) { if (title) Conqueror::OHOS::PostHostRequest("title", title); }
    void glfwSetWindowIcon(GLFWwindow*, int, const GLFWimage*) {}
    void glfwSetWindowAttrib(GLFWwindow*, int, int) {}
    void glfwSetWindowMonitor(GLFWwindow*, GLFWmonitor*, int, int, int, int, int) {}
    GLFWmonitor* glfwGetWindowMonitor(GLFWwindow*) { return nullptr; }
    void glfwIconifyWindow(GLFWwindow*) {}
    void glfwMaximizeWindow(GLFWwindow*) {}
    void glfwRestoreWindow(GLFWwindow*) {}
    void glfwRequestWindowAttention(GLFWwindow*) {}
    GLFWmonitor* glfwGetPrimaryMonitor(void) { return &s.Monitor; }
    void glfwGetMonitorPos(GLFWmonitor*, int* x, int* y) { if (x) *x = 0; if (y) *y = 0; }
    const char* glfwGetMonitorName(GLFWmonitor*) { return "HarmonyOS Display"; }
    const GLFWvidmode* glfwGetVideoMode(GLFWmonitor*) { return &s.Mode; }
    const GLFWvidmode* glfwGetVideoModes(GLFWmonitor*, int* count)
    {
        if (count) *count = 1;
        return &s.Mode;
    }

    int glfwGetWindowAttrib(GLFWwindow* w, int attrib)
    {
        switch (attrib)
        {
            case GLFW_FOCUSED: { std::lock_guard<std::mutex> lock(s.Lock); return s.Focused ? GLFW_TRUE : GLFW_FALSE; }
            case GLFW_VISIBLE: return GLFW_TRUE;
            case GLFW_HOVERED: return GLFW_TRUE;
            case GLFW_ICONIFIED: return w && w->Surface == EGL_NO_SURFACE ? GLFW_TRUE : GLFW_FALSE;
            case GLFW_CLIENT_API: return GLFW_OPENGL_ES_API;
            case GLFW_CONTEXT_VERSION_MAJOR: return 3;
            default: return 0;
        }
    }

    GLFWkeyfun glfwSetKeyCallback(GLFWwindow* w, GLFWkeyfun cb) { GLFWkeyfun p = w->KeyCallback; w->KeyCallback = cb; return p; }
    GLFWcharfun glfwSetCharCallback(GLFWwindow* w, GLFWcharfun cb) { GLFWcharfun p = w->CharCallback; w->CharCallback = cb; return p; }
    GLFWmousebuttonfun glfwSetMouseButtonCallback(GLFWwindow* w, GLFWmousebuttonfun cb) { GLFWmousebuttonfun p = w->MouseButtonCallback; w->MouseButtonCallback = cb; return p; }
    GLFWcursorposfun glfwSetCursorPosCallback(GLFWwindow* w, GLFWcursorposfun cb) { GLFWcursorposfun p = w->CursorPosCallback; w->CursorPosCallback = cb; return p; }
    GLFWcursorenterfun glfwSetCursorEnterCallback(GLFWwindow* w, GLFWcursorenterfun cb) { GLFWcursorenterfun p = w->CursorEnterCallback; w->CursorEnterCallback = cb; return p; }
    GLFWscrollfun glfwSetScrollCallback(GLFWwindow* w, GLFWscrollfun cb) { GLFWscrollfun p = w->ScrollCallback; w->ScrollCallback = cb; return p; }
    GLFWwindowsizefun glfwSetWindowSizeCallback(GLFWwindow* w, GLFWwindowsizefun cb) { GLFWwindowsizefun p = w->SizeCallback; w->SizeCallback = cb; return p; }
    GLFWframebuffersizefun glfwSetFramebufferSizeCallback(GLFWwindow* w, GLFWframebuffersizefun cb) { GLFWframebuffersizefun p = w->FramebufferSizeCallback; w->FramebufferSizeCallback = cb; return p; }
    GLFWwindowclosefun glfwSetWindowCloseCallback(GLFWwindow* w, GLFWwindowclosefun cb) { GLFWwindowclosefun p = w->CloseCallback; w->CloseCallback = cb; return p; }
    GLFWwindowfocusfun glfwSetWindowFocusCallback(GLFWwindow* w, GLFWwindowfocusfun cb) { GLFWwindowfocusfun p = w->FocusCallback; w->FocusCallback = cb; return p; }
    GLFWwindowcontentscalefun glfwSetWindowContentScaleCallback(GLFWwindow* w, GLFWwindowcontentscalefun cb) { GLFWwindowcontentscalefun p = w->ContentScaleCallback; w->ContentScaleCallback = cb; return p; }
    GLFWwindowposfun glfwSetWindowPosCallback(GLFWwindow*, GLFWwindowposfun) { return nullptr; }
    GLFWwindowiconifyfun glfwSetWindowIconifyCallback(GLFWwindow*, GLFWwindowiconifyfun) { return nullptr; }
    GLFWwindowmaximizefun glfwSetWindowMaximizeCallback(GLFWwindow*, GLFWwindowmaximizefun) { return nullptr; }
    GLFWwindowrefreshfun glfwSetWindowRefreshCallback(GLFWwindow*, GLFWwindowrefreshfun) { return nullptr; }
    GLFWdropfun glfwSetDropCallback(GLFWwindow*, GLFWdropfun) { return nullptr; }
    GLFWmonitorfun glfwSetMonitorCallback(GLFWmonitorfun) { return nullptr; }
    GLFWjoystickfun glfwSetJoystickCallback(GLFWjoystickfun) { return nullptr; }

    int glfwGetKey(GLFWwindow* w, int key) { return w && key >= 0 && key <= GLFW_KEY_LAST ? w->Keys[key] : GLFW_RELEASE; }
    int glfwGetMouseButton(GLFWwindow* w, int button) { return w && button >= 0 && button <= GLFW_MOUSE_BUTTON_LAST ? w->Mouse[button] : GLFW_RELEASE; }
    void glfwGetCursorPos(GLFWwindow* w, double* x, double* y)
    {
        if (x) *x = w ? w->CursorX : 0.0;
        if (y) *y = w ? w->CursorY : 0.0;
    }
    // Uygulamalar sistem imlecini taşıyamaz: yalnız motorun gördüğü konum değişir
    void glfwSetCursorPos(GLFWwindow* w, double x, double y) { if (w) { w->CursorX = x; w->CursorY = y; } }

    void glfwSetInputMode(GLFWwindow* w, int mode, int value)
    {
        if (!w || mode != GLFW_CURSOR) return;
        w->CursorMode = value;
        ApplyCursor(s.CursorShape ? s.CursorShape : GLFW_ARROW_CURSOR, value != GLFW_CURSOR_NORMAL);
    }
    int glfwGetInputMode(GLFWwindow* w, int mode) { return mode == GLFW_CURSOR ? (w ? w->CursorMode : GLFW_CURSOR_NORMAL) : GLFW_FALSE; }
    int glfwRawMouseMotionSupported(void) { return GLFW_FALSE; }

    double glfwGetTime(void)
    {
        timespec now{};
        clock_gettime(CLOCK_MONOTONIC, &now);
        return (double)(now.tv_sec - s.Start.tv_sec) + (double)(now.tv_nsec - s.Start.tv_nsec) * 1e-9;
    }
    void glfwSetTime(double time)
    {
        timespec now{};
        clock_gettime(CLOCK_MONOTONIC, &now);
        const double start = (double)now.tv_sec + (double)now.tv_nsec * 1e-9 - time;
        s.Start.tv_sec = (time_t)start;
        s.Start.tv_nsec = (long)((start - (double)s.Start.tv_sec) * 1e9);
    }
    uint64_t glfwGetTimerValue(void)
    {
        timespec now{};
        clock_gettime(CLOCK_MONOTONIC, &now);
        return (uint64_t)now.tv_sec * 1000000000ull + (uint64_t)now.tv_nsec;
    }
    uint64_t glfwGetTimerFrequency(void) { return 1000000000ull; }

    // Oyun kumandası: OpenHarmony NDK'da kumanda API'si yok (HarmonyOS GameController Kit ayrı SDK); kumanda bağlı görünmez
    int glfwJoystickPresent(int) { return GLFW_FALSE; }
    int glfwJoystickIsGamepad(int) { return GLFW_FALSE; }
    const char* glfwGetJoystickName(int) { return nullptr; }
    const char* glfwGetGamepadName(int) { return nullptr; }
    const char* glfwGetJoystickGUID(int) { return nullptr; }
    const unsigned char* glfwGetJoystickButtons(int, int* count) { if (count) *count = 0; return nullptr; }
    const unsigned char* glfwGetJoystickHats(int, int* count) { if (count) *count = 0; return nullptr; }
    const float* glfwGetJoystickAxes(int, int* count) { if (count) *count = 0; return nullptr; }
    int glfwGetGamepadState(int, GLFWgamepadstate*) { return GLFW_FALSE; }
    int glfwUpdateGamepadMappings(const char*) { return GLFW_TRUE; }

    int glfwGetError(const char** description)
    {
        if (description) *description = nullptr;
        return GLFW_NO_ERROR;
    }
    const char* glfwGetVersionString(void) { return "3.4.0 HarmonyOS (Conqueror XComponent/EGL)"; }
    void glfwGetVersion(int* major, int* minor, int* rev)
    {
        if (major) *major = 3;
        if (minor) *minor = 4;
        if (rev) *rev = 0;
    }

    // ── İmleç: ArkTS pointer.setPointerStyle ile (Index.ets)
    GLFWcursor* glfwCreateCursor(const GLFWimage*, int, int) { return new GLFWcursor{ GLFW_ARROW_CURSOR }; }
    GLFWcursor* glfwCreateStandardCursor(int shape) { return new GLFWcursor{ shape }; }
    void glfwDestroyCursor(GLFWcursor* cursor) { delete cursor; }
    void glfwSetCursor(GLFWwindow* w, GLFWcursor* cursor)
    {
        ApplyCursor(cursor ? cursor->Shape : GLFW_ARROW_CURSOR, w && w->CursorMode != GLFW_CURSOR_NORMAL);
    }

    const char* glfwGetKeyName(int, int) { return nullptr; }
    int glfwGetKeyScancode(int) { return -1; }

    const char* glfwGetClipboardString(GLFWwindow*)
    {
        std::string text;
        s.ClipboardRead = Pasteboard::Get().Read(text) ? text : s.Clipboard;
        return s.ClipboardRead.c_str();
    }
    void glfwSetClipboardString(GLFWwindow*, const char* text)
    {
        s.Clipboard = text ? text : "";
        Pasteboard::Get().Write(s.Clipboard);
    }

    void glfwFocusWindow(GLFWwindow*) {}
    void glfwShowWindow(GLFWwindow*) {}
    void glfwHideWindow(GLFWwindow*) {}
    void glfwSetWindowOpacity(GLFWwindow*, float) {}
    float glfwGetWindowOpacity(GLFWwindow*) { return 1.0f; }
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
    // Ekran yoğunluğu (vp başına piksel, 160 dpi = 1.0): ImGui ve arayüz ölçeği
    void glfwGetMonitorContentScale(GLFWmonitor*, float* xscale, float* yscale)
    {
        const float scale = DensityScale();
        if (xscale) *xscale = scale;
        if (yscale) *yscale = scale;
    }
    void glfwGetWindowContentScale(GLFWwindow*, float* xscale, float* yscale) { glfwGetMonitorContentScale(nullptr, xscale, yscale); }

    // Vulkan yüzeyi yok (VK_OHOS_surface arka ucu henüz yazılmadı; OpenGL ES): VK_ERROR_EXTENSION_NOT_PRESENT
    int glfwVulkanSupported(void) { return GLFW_FALSE; }
    const char** glfwGetRequiredInstanceExtensions(uint32_t* count)
    {
        if (count) *count = 0;
        return nullptr;
    }
    int glfwCreateWindowSurface(void*, GLFWwindow*, const void*, void*) { return -7; }

    // glfw3native.h EGL erişimi (OpenXR / birlikte çalışma kodu zayıf bağla arar)
    EGLDisplay glfwGetEGLDisplay(void) { return s.Display; }
    EGLContext glfwGetEGLContext(GLFWwindow* w) { return w ? w->Context : EGL_NO_CONTEXT; }
    EGLSurface glfwGetEGLSurface(GLFWwindow* w) { return w ? w->Surface : EGL_NO_SURFACE; }
}

#endif
