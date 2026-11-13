// Android'de GLFW yok: motorun kullandığı GLFW işlevleri burada NativeActivity + EGL ile uygulanır.
// Pencere = ANativeWindow üzerinde EGL yüzeyi (OpenGL ES 3.2, olmazsa 3.1 / 3.0); dokunma birincil fare düğmesi
// ve imleç olur, donanım klavyesi / oyun kumandası GLFW tuş ve kumanda durumuna çevrilir. Uygulama arka plana
// geçince yüzey bırakılır ve glfwPollEvents pencere geri gelene kadar bekler (oyun döngüsü duraklar).
#ifdef __ANDROID__

#include "AndroidPlatform.h"

#include <android/asset_manager.h>
#include <android/configuration.h>
#include <android/input.h>
#include <android/keycodes.h>
#include <android/log.h>
#include <android/native_activity.h>
#include <android/native_window.h>
#include <android/window.h>
#include <android_native_app_glue.h>
#include <EGL/egl.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <dlfcn.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

struct GLFWwindow
{
    EGLSurface Surface = EGL_NO_SURFACE;
    EGLContext Context = EGL_NO_CONTEXT;
    int Width = 0, Height = 0;                 // EGL yüzeyinin tamamı
    // Sistem arayüzünün örtmediği içerik alanı (üst-sol köken). Motor pencereyi bu boyutta görür; Android
    // Automotive'de araç çubukları yüzeyin üstünde kalır, oyun aradaki alana çizilir
    int ContentX = 0, ContentY = 0, ContentW = 0, ContentH = 0;
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
    // Motor tuş / düğme durumunu kare başına okur: aynı karede basılıp bırakılan (dokunma, adb input) bırakma bir
    // sonraki kareye ertelenir, yoksa tıklama hiç görülmez
    uint64_t KeyPressFrame[GLFW_KEY_LAST + 1] = {};
    uint64_t MousePressFrame[GLFW_MOUSE_BUTTON_LAST + 1] = {};
    std::vector<std::pair<int, int>> DeferredKeyUps;   // (tuş, tarama kodu)
    std::vector<int> DeferredMouseUps;
};

struct GLFWmonitor
{
    int Dummy = 0;
};

namespace
{
    struct State
    {
        android_app* App = nullptr;
        EGLDisplay Display = EGL_NO_DISPLAY;
        EGLConfig Config = nullptr;
        GLFWwindow* Window = nullptr;
        GLFWwindow* Current = nullptr;
        GLFWerrorfun ErrorCallback = nullptr;
        GLFWmonitor Monitor;
        GLFWvidmode Mode{};
        int ClientApi = GLFW_OPENGL_ES_API;
        int MinorHint = 2;
        bool Focused = true;
        bool Destroy = false;
        int32_t (*Forwarder)(const AInputEvent*) = nullptr;
        uint64_t Frame = 1;   // glfwPollEvents sayacı
        uint64_t PadPressFrame[GLFW_GAMEPAD_BUTTON_LAST + 1] = {};
        std::vector<int> DeferredPadUps;
        std::string Clipboard;
        // Oyun kumandası (GLFW düzeni)
        bool Gamepad = false;
        unsigned char Buttons[GLFW_GAMEPAD_BUTTON_LAST + 1] = {};
        float Axes[GLFW_GAMEPAD_AXIS_LAST + 1] = { 0, 0, 0, 0, -1, -1 };
        timespec Start{};
    };
    State s;

    float DensityScale()
    {
        if (!s.App || !s.App->config) return 1.0f;
        const int32_t density = AConfiguration_getDensity(s.App->config);
        return density > 0 && density != ACONFIGURATION_DENSITY_NONE && density != ACONFIGURATION_DENSITY_ANY ? (float)density / 160.0f : 1.0f;
    }

    void LogInfo(const char* fmt, const char* a = "")
    {
        __android_log_print(ANDROID_LOG_INFO, "Conqueror", fmt, a);
    }

    int MapKey(int32_t key)
    {
        if (key >= AKEYCODE_A && key <= AKEYCODE_Z) return GLFW_KEY_A + (key - AKEYCODE_A);
        if (key >= AKEYCODE_0 && key <= AKEYCODE_9) return GLFW_KEY_0 + (key - AKEYCODE_0);
        if (key >= AKEYCODE_F1 && key <= AKEYCODE_F12) return GLFW_KEY_F1 + (key - AKEYCODE_F1);
        switch (key)
        {
            case AKEYCODE_SPACE: return GLFW_KEY_SPACE;
            case AKEYCODE_ENTER: case AKEYCODE_NUMPAD_ENTER: return GLFW_KEY_ENTER;
            case AKEYCODE_DEL: return GLFW_KEY_BACKSPACE;
            case AKEYCODE_FORWARD_DEL: return GLFW_KEY_DELETE;
            case AKEYCODE_TAB: return GLFW_KEY_TAB;
            case AKEYCODE_ESCAPE: case AKEYCODE_BACK: return GLFW_KEY_ESCAPE;
            // Android TV / Google TV kumandası: orta (OK) tuşu onaylar (UI Submit)
            case AKEYCODE_DPAD_CENTER: return GLFW_KEY_ENTER;
            case AKEYCODE_DPAD_UP: return GLFW_KEY_UP;
            case AKEYCODE_DPAD_DOWN: return GLFW_KEY_DOWN;
            case AKEYCODE_DPAD_LEFT: return GLFW_KEY_LEFT;
            case AKEYCODE_DPAD_RIGHT: return GLFW_KEY_RIGHT;
            case AKEYCODE_SHIFT_LEFT: return GLFW_KEY_LEFT_SHIFT;
            case AKEYCODE_SHIFT_RIGHT: return GLFW_KEY_RIGHT_SHIFT;
            case AKEYCODE_CTRL_LEFT: return GLFW_KEY_LEFT_CONTROL;
            case AKEYCODE_CTRL_RIGHT: return GLFW_KEY_RIGHT_CONTROL;
            case AKEYCODE_ALT_LEFT: return GLFW_KEY_LEFT_ALT;
            case AKEYCODE_ALT_RIGHT: return GLFW_KEY_RIGHT_ALT;
            case AKEYCODE_MINUS: return GLFW_KEY_MINUS;
            case AKEYCODE_EQUALS: return GLFW_KEY_EQUAL;
            case AKEYCODE_LEFT_BRACKET: return GLFW_KEY_LEFT_BRACKET;
            case AKEYCODE_RIGHT_BRACKET: return GLFW_KEY_RIGHT_BRACKET;
            case AKEYCODE_BACKSLASH: return GLFW_KEY_BACKSLASH;
            case AKEYCODE_SEMICOLON: return GLFW_KEY_SEMICOLON;
            case AKEYCODE_APOSTROPHE: return GLFW_KEY_APOSTROPHE;
            case AKEYCODE_COMMA: return GLFW_KEY_COMMA;
            case AKEYCODE_PERIOD: return GLFW_KEY_PERIOD;
            case AKEYCODE_SLASH: return GLFW_KEY_SLASH;
            case AKEYCODE_GRAVE: return GLFW_KEY_GRAVE_ACCENT;
            case AKEYCODE_PAGE_UP: return GLFW_KEY_PAGE_UP;
            case AKEYCODE_PAGE_DOWN: return GLFW_KEY_PAGE_DOWN;
            case AKEYCODE_MOVE_HOME: return GLFW_KEY_HOME;
            case AKEYCODE_MOVE_END: return GLFW_KEY_END;
            case AKEYCODE_INSERT: return GLFW_KEY_INSERT;
            default: return GLFW_KEY_UNKNOWN;
        }
    }

    int MapGamepadButton(int32_t key)
    {
        switch (key)
        {
            case AKEYCODE_BUTTON_A: return GLFW_GAMEPAD_BUTTON_A;
            case AKEYCODE_BUTTON_B: return GLFW_GAMEPAD_BUTTON_B;
            case AKEYCODE_BUTTON_X: return GLFW_GAMEPAD_BUTTON_X;
            case AKEYCODE_BUTTON_Y: return GLFW_GAMEPAD_BUTTON_Y;
            case AKEYCODE_BUTTON_L1: return GLFW_GAMEPAD_BUTTON_LEFT_BUMPER;
            case AKEYCODE_BUTTON_R1: return GLFW_GAMEPAD_BUTTON_RIGHT_BUMPER;
            case AKEYCODE_BUTTON_SELECT: return GLFW_GAMEPAD_BUTTON_BACK;
            case AKEYCODE_BUTTON_START: return GLFW_GAMEPAD_BUTTON_START;
            case AKEYCODE_BUTTON_MODE: return GLFW_GAMEPAD_BUTTON_GUIDE;
            case AKEYCODE_BUTTON_THUMBL: return GLFW_GAMEPAD_BUTTON_LEFT_THUMB;
            case AKEYCODE_BUTTON_THUMBR: return GLFW_GAMEPAD_BUTTON_RIGHT_THUMB;
            default: return -1;
        }
    }

    // Donanım klavyesinden basit ASCII karakter (yazılım klavyesi Java tarafında; oyunlar çoğunlukla tuş durumuna bakar)
    unsigned int CharFor(int32_t key, int32_t meta)
    {
        const bool shift = (meta & AMETA_SHIFT_ON) != 0;
        if (key >= AKEYCODE_A && key <= AKEYCODE_Z) return (unsigned int)((shift ? 'A' : 'a') + (key - AKEYCODE_A));
        if (key >= AKEYCODE_0 && key <= AKEYCODE_9 && !shift) return (unsigned int)('0' + (key - AKEYCODE_0));
        switch (key)
        {
            case AKEYCODE_SPACE: return ' ';
            case AKEYCODE_MINUS: return shift ? '_' : '-';
            case AKEYCODE_EQUALS: return shift ? '+' : '=';
            case AKEYCODE_COMMA: return shift ? '<' : ',';
            case AKEYCODE_PERIOD: return shift ? '>' : '.';
            case AKEYCODE_SLASH: return shift ? '?' : '/';
            case AKEYCODE_SEMICOLON: return shift ? ':' : ';';
            case AKEYCODE_APOSTROPHE: return shift ? '"' : '\'';
            default: return 0;
        }
    }

    int Mods(int32_t meta)
    {
        int m = 0;
        if (meta & AMETA_SHIFT_ON) m |= GLFW_MOD_SHIFT;
        if (meta & AMETA_CTRL_ON) m |= GLFW_MOD_CONTROL;
        if (meta & AMETA_ALT_ON) m |= GLFW_MOD_ALT;
        return m;
    }

    void SetMouse(GLFWwindow* w, int button, bool down, int mods = 0)
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

    void SetPadButton(int button, bool down)
    {
        if (down)
        {
            if (s.Buttons[button] != GLFW_PRESS)
                s.PadPressFrame[button] = s.Frame;
            s.Buttons[button] = GLFW_PRESS;
        }
        else if (s.PadPressFrame[button] == s.Frame)
            s.DeferredPadUps.push_back(button);
        else
            s.Buttons[button] = GLFW_RELEASE;
    }

    // Yeni kare: önceki karede ertelenen bırakmalar uygulanır
    void SyncWindowSize(GLFWwindow* w);

    void BeginFrame()
    {
        ++s.Frame;
        for (int b : s.DeferredPadUps)
            s.Buttons[b] = GLFW_RELEASE;
        s.DeferredPadUps.clear();
        GLFWwindow* w = s.Window;
        if (!w)
            return;
        // Sürüklenerek boyutlandırılan pencere her karede (olay gelmese de) yeni boyutuna uyar
        if (w->Surface != EGL_NO_SURFACE)
            SyncWindowSize(w);
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

    void SetCursor(GLFWwindow* w, double x, double y)
    {
        if (!w) return;
        x -= w->ContentX;
        y -= w->ContentY;
        w->CursorX = x;
        w->CursorY = y;
        if (w->CursorPosCallback) w->CursorPosCallback(w, x, y);
    }

    float Axis(const AInputEvent* e, int32_t axis, size_t pointer = 0)
    {
        return AMotionEvent_getAxisValue(e, axis, pointer);
    }

    int32_t HandleInput(android_app*, AInputEvent* event)
    {
        if (s.Forwarder)
            s.Forwarder(event);
        GLFWwindow* w = s.Window;
        const int32_t type = AInputEvent_getType(event);
        const int32_t source = AInputEvent_getSource(event);
        if (type == AINPUT_EVENT_TYPE_MOTION)
        {
            const int32_t action = AMotionEvent_getAction(event) & AMOTION_EVENT_ACTION_MASK;
            if ((source & AINPUT_SOURCE_JOYSTICK) == AINPUT_SOURCE_JOYSTICK)
            {
                s.Gamepad = true;
                s.Axes[GLFW_GAMEPAD_AXIS_LEFT_X] = Axis(event, AMOTION_EVENT_AXIS_X);
                s.Axes[GLFW_GAMEPAD_AXIS_LEFT_Y] = Axis(event, AMOTION_EVENT_AXIS_Y);
                s.Axes[GLFW_GAMEPAD_AXIS_RIGHT_X] = Axis(event, AMOTION_EVENT_AXIS_Z);
                s.Axes[GLFW_GAMEPAD_AXIS_RIGHT_Y] = Axis(event, AMOTION_EVENT_AXIS_RZ);
                const float lt = std::max(Axis(event, AMOTION_EVENT_AXIS_LTRIGGER), Axis(event, AMOTION_EVENT_AXIS_BRAKE));
                const float rt = std::max(Axis(event, AMOTION_EVENT_AXIS_RTRIGGER), Axis(event, AMOTION_EVENT_AXIS_GAS));
                s.Axes[GLFW_GAMEPAD_AXIS_LEFT_TRIGGER] = lt * 2.0f - 1.0f;
                s.Axes[GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER] = rt * 2.0f - 1.0f;
                const float hx = Axis(event, AMOTION_EVENT_AXIS_HAT_X), hy = Axis(event, AMOTION_EVENT_AXIS_HAT_Y);
                s.Buttons[GLFW_GAMEPAD_BUTTON_DPAD_LEFT] = hx < -0.5f;
                s.Buttons[GLFW_GAMEPAD_BUTTON_DPAD_RIGHT] = hx > 0.5f;
                s.Buttons[GLFW_GAMEPAD_BUTTON_DPAD_UP] = hy < -0.5f;
                s.Buttons[GLFW_GAMEPAD_BUTTON_DPAD_DOWN] = hy > 0.5f;
                return 1;
            }
            if (!w) return 0;
            if (action == AMOTION_EVENT_ACTION_SCROLL)
            {
                if (w->ScrollCallback)
                    w->ScrollCallback(w, Axis(event, AMOTION_EVENT_AXIS_HSCROLL), Axis(event, AMOTION_EVENT_AXIS_VSCROLL));
                return 1;
            }
            const double x = AMotionEvent_getX(event, 0), y = AMotionEvent_getY(event, 0);
            if ((source & AINPUT_SOURCE_MOUSE) == AINPUT_SOURCE_MOUSE)
            {
                SetCursor(w, x, y);
                const int32_t buttons = AMotionEvent_getButtonState(event);
                // Düğme durumu olmadan gelen basış (bazı dokunmatik yüzeyler, enjekte edilen tıklama): sol tık, bırakılana kadar
                static bool s_ImplicitPrimary = false;
                if (action == AMOTION_EVENT_ACTION_DOWN && buttons == 0)
                    s_ImplicitPrimary = true;
                else if (action == AMOTION_EVENT_ACTION_UP || action == AMOTION_EVENT_ACTION_CANCEL)
                    s_ImplicitPrimary = false;
                SetMouse(w, GLFW_MOUSE_BUTTON_LEFT, (buttons & AMOTION_EVENT_BUTTON_PRIMARY) != 0 || s_ImplicitPrimary);
                SetMouse(w, GLFW_MOUSE_BUTTON_RIGHT, (buttons & AMOTION_EVENT_BUTTON_SECONDARY) != 0);
                SetMouse(w, GLFW_MOUSE_BUTTON_MIDDLE, (buttons & AMOTION_EVENT_BUTTON_TERTIARY) != 0);
                return 1;
            }
            // Dokunma: ilk parmak = sol fare düğmesi
            switch (action)
            {
                case AMOTION_EVENT_ACTION_DOWN:
                    SetCursor(w, x, y);
                    SetMouse(w, GLFW_MOUSE_BUTTON_LEFT, true);
                    break;
                case AMOTION_EVENT_ACTION_MOVE:
                    SetCursor(w, x, y);
                    break;
                case AMOTION_EVENT_ACTION_UP:
                case AMOTION_EVENT_ACTION_CANCEL:
                    SetCursor(w, x, y);
                    SetMouse(w, GLFW_MOUSE_BUTTON_LEFT, false);
                    break;
                default:
                    break;
            }
            return 1;
        }
        if (type == AINPUT_EVENT_TYPE_KEY)
        {
            const int32_t code = AKeyEvent_getKeyCode(event);
            const int32_t action = AKeyEvent_getAction(event);
            const int32_t meta = AKeyEvent_getMetaState(event);
            const bool down = action == AKEY_EVENT_ACTION_DOWN;
            const int button = MapGamepadButton(code);
            if (button >= 0)
            {
                s.Gamepad = true;
                SetPadButton(button, down);
                return 1;
            }
            if ((source & AINPUT_SOURCE_GAMEPAD) == AINPUT_SOURCE_GAMEPAD && code >= AKEYCODE_DPAD_UP && code <= AKEYCODE_DPAD_RIGHT)
            {
                s.Gamepad = true;
                const int dpad = code == AKEYCODE_DPAD_UP ? GLFW_GAMEPAD_BUTTON_DPAD_UP : code == AKEYCODE_DPAD_DOWN ? GLFW_GAMEPAD_BUTTON_DPAD_DOWN
                               : code == AKEYCODE_DPAD_LEFT ? GLFW_GAMEPAD_BUTTON_DPAD_LEFT : GLFW_GAMEPAD_BUTTON_DPAD_RIGHT;
                SetPadButton(dpad, down);
                return 1;
            }
            const int key = MapKey(code);
            if (!w || key == GLFW_KEY_UNKNOWN) return code == AKEYCODE_BACK ? 1 : 0;
            SetKey(w, key, code, down, Mods(meta));
            if (down && w->CharCallback)
                if (unsigned int c = CharFor(code, meta))
                    w->CharCallback(w, c);
            return 1;
        }
        return 0;
    }

    void UpdateContentRect(GLFWwindow* w, bool notify)
    {
        if (!w) return;
        int cx = 0, cy = 0, cw = w->Width, ch = w->Height;
        if (s.App)
        {
            const ARect r = s.App->contentRect;
            if (r.right > r.left && r.bottom > r.top)
            {
                cx = std::clamp(r.left, 0, w->Width);
                cy = std::clamp(r.top, 0, w->Height);
                cw = std::clamp(r.right, 0, w->Width) - cx;
                ch = std::clamp(r.bottom, 0, w->Height) - cy;
                if (cw <= 0 || ch <= 0)
                {
                    cx = cy = 0;
                    cw = w->Width;
                    ch = w->Height;
                }
            }
        }
        const bool changed = cw != w->ContentW || ch != w->ContentH;
        w->ContentX = cx;
        w->ContentY = cy;
        w->ContentW = cw;
        w->ContentH = ch;
        if (changed && notify && w->SizeCallback)
            w->SizeCallback(w, cw, ch);
    }

    // Pencere boyutu: EGL yüzeyinin boyutu ancak sonraki takastan sonra değişir; serbest pencerede (ChromeOS, masaüstü kipi,
    // çoklu pencere) yeniden boyutlandırma anında doğrusu ANativeWindow'dan okunur
    void SyncWindowSize(GLFWwindow* w)
    {
        if (!w || !s.App || !s.App->window)
            return;
        const int width = ANativeWindow_getWidth(s.App->window), height = ANativeWindow_getHeight(s.App->window);
        if (width > 0 && height > 0 && (width != w->Width || height != w->Height))
        {
            w->Width = width;
            w->Height = height;
            s.Mode.width = width;
            s.Mode.height = height;
        }
        UpdateContentRect(w, true);
    }

    bool CreateSurface(GLFWwindow* w)
    {
        if (!s.App || !s.App->window || s.Display == EGL_NO_DISPLAY) return false;
        w->Surface = eglCreateWindowSurface(s.Display, s.Config, s.App->window, nullptr);
        if (w->Surface == EGL_NO_SURFACE) return false;
        eglQuerySurface(s.Display, w->Surface, EGL_WIDTH, &w->Width);
        eglQuerySurface(s.Display, w->Surface, EGL_HEIGHT, &w->Height);
        s.Mode.width = w->Width;
        s.Mode.height = w->Height;
        UpdateContentRect(w, false);
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
    }

    void HandleCommand(android_app* app, int32_t cmd)
    {
        switch (cmd)
        {
            case APP_CMD_INIT_WINDOW:
                if (s.Window && s.Window->Surface == EGL_NO_SURFACE)
                {
                    CreateSurface(s.Window);
                    if (s.Window->SizeCallback) s.Window->SizeCallback(s.Window, s.Window->ContentW, s.Window->ContentH);
                }
                break;
            case APP_CMD_TERM_WINDOW:
                DestroySurface(s.Window);
                break;
            case APP_CMD_WINDOW_RESIZED:
            case APP_CMD_CONFIG_CHANGED:
                if (s.Window && s.Window->Surface != EGL_NO_SURFACE)
                    SyncWindowSize(s.Window);
                break;
            case APP_CMD_CONTENT_RECT_CHANGED:
                UpdateContentRect(s.Window, true);
                break;
            case APP_CMD_GAINED_FOCUS: s.Focused = true; break;
            case APP_CMD_LOST_FOCUS: s.Focused = false; break;
            case APP_CMD_DESTROY:
                s.Destroy = true;
                if (s.Window)
                {
                    s.Window->ShouldClose = true;
                    if (s.Window->CloseCallback) s.Window->CloseCallback(s.Window);
                }
                break;
            default:
                break;
        }
        (void)app;
    }

    // timeoutMs: 0 = bekleme, -1 = olay gelene kadar
    void PumpEvents(int timeoutMs)
    {
        if (!s.App) return;
        int events = 0;
        android_poll_source* source = nullptr;
        while (ALooper_pollOnce(timeoutMs, nullptr, &events, (void**)&source) >= 0)
        {
            if (source) source->process(s.App, source);
            if (s.App->destroyRequested) { s.Destroy = true; break; }
            timeoutMs = 0;
        }
    }

    bool InitDisplay()
    {
        if (s.Display != EGL_NO_DISPLAY) return true;
        s.Display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        if (s.Display == EGL_NO_DISPLAY || !eglInitialize(s.Display, nullptr, nullptr)) return false;
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
}

namespace Conqueror::Android
{
    void SetApp(android_app* app)
    {
        s.App = app;
        app->onAppCmd = &HandleCommand;
        app->onInputEvent = &HandleInput;
        clock_gettime(CLOCK_MONOTONIC, &s.Start);
        // Oyun: tam ekran, oynarken ekran kararmasın. Araçta (Android Automotive) sistem çubukları gizlenemez: pencere
        // çubukların arasındaki alana yerleşsin (yoksa oyunun altı çubukların arkasında kalır)
        const bool car = app->config && AConfiguration_getUiModeType(app->config) == ACONFIGURATION_UI_MODE_TYPE_CAR;
        if (app->activity)
            ANativeActivity_setWindowFlags(app->activity, AWINDOW_FLAG_KEEP_SCREEN_ON | (car ? 0 : AWINDOW_FLAG_FULLSCREEN), 0);
    }

    android_app* GetApp() { return s.App; }

    std::string FilesDirectory()
    {
        return s.App && s.App->activity && s.App->activity->internalDataPath ? s.App->activity->internalDataPath : "/data/local/tmp";
    }

    void SetInputForwarder(int32_t (*forwarder)(const AInputEvent* event)) { s.Forwarder = forwarder; }

    bool DestroyRequested() { return s.Destroy; }

    bool OutputViewport(int& x, int& y, int& width, int& height)
    {
        const GLFWwindow* w = s.Window;
        if (!w || (w->ContentW == w->Width && w->ContentH == w->Height))
            return false;
        x = w->ContentX;
        y = w->Height - (w->ContentY + w->ContentH);
        width = w->ContentW;
        height = w->ContentH;
        return true;
    }

    bool ExtractAssets(const std::string& destination)
    {
        if (!s.App || !s.App->activity) return false;
        AAssetManager* mgr = s.App->activity->assetManager;
        AAsset* list = AAssetManager_open(mgr, "cq_files.txt", AASSET_MODE_BUFFER);
        if (!list) { LogInfo("cq_files.txt missing in APK assets%s"); return false; }
        std::string manifest((const char*)AAsset_getBuffer(list), (size_t)AAsset_getLength(list));
        AAsset_close(list);
        const std::string version = manifest.substr(0, manifest.find('\n'));
        namespace fs = std::filesystem;
        std::error_code ec;
        fs::create_directories(destination, ec);
        {
            std::ifstream stamp(destination + "/.cq_assets_version");
            std::string have;
            std::getline(stamp, have);
            if (have == version) return true;
        }
        size_t pos = manifest.find('\n');
        int extracted = 0;
        while (pos != std::string::npos && pos + 1 < manifest.size())
        {
            const size_t end = manifest.find('\n', pos + 1);
            const std::string rel = manifest.substr(pos + 1, (end == std::string::npos ? manifest.size() : end) - pos - 1);
            pos = end;
            if (rel.empty()) continue;
            AAsset* a = AAssetManager_open(mgr, rel.c_str(), AASSET_MODE_STREAMING);
            if (!a) continue;
            const fs::path out = fs::path(destination) / rel;
            fs::create_directories(out.parent_path(), ec);
            std::ofstream f(out, std::ios::binary);
            char buf[64 * 1024];
            int n;
            while ((n = AAsset_read(a, buf, sizeof(buf))) > 0)
                f.write(buf, n);
            AAsset_close(a);
            ++extracted;
        }
        std::ofstream(destination + "/.cq_assets_version") << version << "\n";
        __android_log_print(ANDROID_LOG_INFO, "Conqueror", "Extracted %d asset files to %s", extracted, destination.c_str());
        return true;
    }
}

// ══════════════════════════════════════════ GLFW API
extern "C"
{
    int glfwInit(void)
    {
        if (s.Start.tv_sec == 0 && s.Start.tv_nsec == 0)
            clock_gettime(CLOCK_MONOTONIC, &s.Start);
        s.Mode.refreshRate = 60;
        s.Mode.redBits = s.Mode.greenBits = s.Mode.blueBits = 8;
        return GLFW_TRUE;
    }

    void glfwTerminate(void)
    {
        if (s.Window) glfwDestroyWindow(s.Window);
        if (s.Display != EGL_NO_DISPLAY) eglTerminate(s.Display);
        s.Display = EGL_NO_DISPLAY;
    }

    GLFWerrorfun glfwSetErrorCallback(GLFWerrorfun callback)
    {
        GLFWerrorfun prev = s.ErrorCallback;
        s.ErrorCallback = callback;
        return prev;
    }

    int glfwGetPlatform(void) { return GLFW_PLATFORM_NULL; }

    void glfwDefaultWindowHints(void)
    {
        s.ClientApi = GLFW_OPENGL_ES_API;
        s.MinorHint = 2;
    }

    void glfwWindowHint(int hint, int value)
    {
        if (hint == GLFW_CONTEXT_VERSION_MINOR && s.ClientApi == GLFW_OPENGL_ES_API) s.MinorHint = value;
        if (hint == GLFW_CLIENT_API && value == GLFW_OPENGL_ES_API) s.ClientApi = value;
    }

    void glfwWindowHintString(int, const char*) {}

    GLFWwindow* glfwCreateWindow(int, int, const char*, GLFWmonitor*, GLFWwindow*)
    {
        if (s.Window) return nullptr;   // tek pencere
        // Etkinlik penceresini bekle
        while (s.App && !s.App->window && !s.Destroy)
            PumpEvents(-1);
        if (!InitDisplay()) return nullptr;
        auto* w = new GLFWwindow();
        for (int minor : { std::max(s.MinorHint, 2), 1, 0 })
        {
            const EGLint ctx[] = { 0x3098 /* EGL_CONTEXT_MAJOR_VERSION */, 3, 0x30FB /* EGL_CONTEXT_MINOR_VERSION */, minor, EGL_NONE };
            w->Context = eglCreateContext(s.Display, s.Config, EGL_NO_CONTEXT, ctx);
            if (w->Context != EGL_NO_CONTEXT) break;
        }
        if (w->Context == EGL_NO_CONTEXT || !CreateSurface(w))
        {
            delete w;
            return nullptr;
        }
        s.Window = w;
        return w;
    }

    void glfwDestroyWindow(GLFWwindow* w)
    {
        if (!w) return;
        DestroySurface(w);
        if (w->Context != EGL_NO_CONTEXT) eglDestroyContext(s.Display, w->Context);
        if (s.Window == w) s.Window = nullptr;
        if (s.Current == w) s.Current = nullptr;
        delete w;
    }

    int glfwWindowShouldClose(GLFWwindow* w) { return w && (w->ShouldClose || s.Destroy) ? GLFW_TRUE : GLFW_FALSE; }
    void glfwSetWindowShouldClose(GLFWwindow* w, int value) { if (w) w->ShouldClose = value != 0; }

    void glfwMakeContextCurrent(GLFWwindow* w)
    {
        s.Current = w;
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

    void glfwPollEvents(void)
    {
        BeginFrame();
        PumpEvents(0);
        // Arka planda (yüzey yok): pencere dönene ya da kapanış istenene kadar bekle
        while (s.Window && s.Window->Surface == EGL_NO_SURFACE && !s.Destroy)
            PumpEvents(-1);
    }

    void glfwWaitEvents(void) { PumpEvents(-1); }

    void glfwSetWindowUserPointer(GLFWwindow* w, void* p) { if (w) w->User = p; }
    void* glfwGetWindowUserPointer(GLFWwindow* w) { return w ? w->User : nullptr; }

    void glfwGetWindowSize(GLFWwindow* w, int* width, int* height)
    {
        if (width) *width = w ? w->ContentW : 0;
        if (height) *height = w ? w->ContentH : 0;
    }
    void glfwGetFramebufferSize(GLFWwindow* w, int* width, int* height) { glfwGetWindowSize(w, width, height); }
    void glfwGetWindowPos(GLFWwindow*, int* x, int* y) { if (x) *x = 0; if (y) *y = 0; }
    void glfwSetWindowPos(GLFWwindow*, int, int) {}
    void glfwSetWindowSize(GLFWwindow*, int, int) {}
    void glfwSetWindowTitle(GLFWwindow*, const char*) {}
    void glfwSetWindowIcon(GLFWwindow*, int, const GLFWimage*) {}
    void glfwSetWindowAttrib(GLFWwindow*, int, int) {}
    void glfwSetWindowMonitor(GLFWwindow*, GLFWmonitor*, int, int, int, int, int) {}
    GLFWmonitor* glfwGetPrimaryMonitor(void) { return &s.Monitor; }
    void glfwGetMonitorPos(GLFWmonitor*, int* x, int* y) { if (x) *x = 0; if (y) *y = 0; }
    const GLFWvidmode* glfwGetVideoMode(GLFWmonitor*) { return &s.Mode; }
    int glfwGetWindowAttrib(GLFWwindow*, int attrib) { return attrib == GLFW_FOCUSED ? (s.Focused ? GLFW_TRUE : GLFW_FALSE) : 0; }

    GLFWkeyfun glfwSetKeyCallback(GLFWwindow* w, GLFWkeyfun cb) { GLFWkeyfun p = w->KeyCallback; w->KeyCallback = cb; return p; }
    GLFWcharfun glfwSetCharCallback(GLFWwindow* w, GLFWcharfun cb) { GLFWcharfun p = w->CharCallback; w->CharCallback = cb; return p; }
    GLFWmousebuttonfun glfwSetMouseButtonCallback(GLFWwindow* w, GLFWmousebuttonfun cb) { GLFWmousebuttonfun p = w->MouseButtonCallback; w->MouseButtonCallback = cb; return p; }
    GLFWcursorposfun glfwSetCursorPosCallback(GLFWwindow* w, GLFWcursorposfun cb) { GLFWcursorposfun p = w->CursorPosCallback; w->CursorPosCallback = cb; return p; }
    GLFWscrollfun glfwSetScrollCallback(GLFWwindow* w, GLFWscrollfun cb) { GLFWscrollfun p = w->ScrollCallback; w->ScrollCallback = cb; return p; }
    GLFWwindowsizefun glfwSetWindowSizeCallback(GLFWwindow* w, GLFWwindowsizefun cb) { GLFWwindowsizefun p = w->SizeCallback; w->SizeCallback = cb; return p; }
    GLFWwindowclosefun glfwSetWindowCloseCallback(GLFWwindow* w, GLFWwindowclosefun cb) { GLFWwindowclosefun p = w->CloseCallback; w->CloseCallback = cb; return p; }

    int glfwGetKey(GLFWwindow* w, int key) { return w && key >= 0 && key <= GLFW_KEY_LAST ? w->Keys[key] : GLFW_RELEASE; }
    int glfwGetMouseButton(GLFWwindow* w, int button) { return w && button >= 0 && button <= GLFW_MOUSE_BUTTON_LAST ? w->Mouse[button] : GLFW_RELEASE; }
    void glfwGetCursorPos(GLFWwindow* w, double* x, double* y)
    {
        if (x) *x = w ? w->CursorX : 0.0;
        if (y) *y = w ? w->CursorY : 0.0;
    }
    void glfwSetCursorPos(GLFWwindow* w, double x, double y) { if (w) { w->CursorX = x; w->CursorY = y; } }
    void glfwSetInputMode(GLFWwindow*, int, int) {}
    int glfwRawMouseMotionSupported(void) { return GLFW_FALSE; }

    double glfwGetTime(void)
    {
        timespec now{};
        clock_gettime(CLOCK_MONOTONIC, &now);
        return (double)(now.tv_sec - s.Start.tv_sec) + (double)(now.tv_nsec - s.Start.tv_nsec) * 1e-9;
    }
    uint64_t glfwGetTimerValue(void)
    {
        timespec now{};
        clock_gettime(CLOCK_MONOTONIC, &now);
        return (uint64_t)now.tv_sec * 1000000000ull + (uint64_t)now.tv_nsec;
    }
    uint64_t glfwGetTimerFrequency(void) { return 1000000000ull; }

    int glfwJoystickPresent(int jid) { return jid == GLFW_JOYSTICK_1 && s.Gamepad ? GLFW_TRUE : GLFW_FALSE; }
    int glfwJoystickIsGamepad(int jid) { return glfwJoystickPresent(jid); }
    const char* glfwGetJoystickName(int jid) { return glfwJoystickPresent(jid) ? "Android Gamepad" : nullptr; }
    const char* glfwGetGamepadName(int jid) { return glfwGetJoystickName(jid); }
    const unsigned char* glfwGetJoystickButtons(int jid, int* count)
    {
        if (!glfwJoystickPresent(jid)) { if (count) *count = 0; return nullptr; }
        if (count) *count = GLFW_GAMEPAD_BUTTON_LAST + 1;
        return s.Buttons;
    }
    const float* glfwGetJoystickAxes(int jid, int* count)
    {
        if (!glfwJoystickPresent(jid)) { if (count) *count = 0; return nullptr; }
        if (count) *count = GLFW_GAMEPAD_AXIS_LAST + 1;
        return s.Axes;
    }
    int glfwGetGamepadState(int jid, GLFWgamepadstate* state)
    {
        if (!glfwJoystickPresent(jid) || !state) return GLFW_FALSE;
        std::memcpy(state->buttons, s.Buttons, sizeof(state->buttons));
        std::memcpy(state->axes, s.Axes, sizeof(state->axes));
        return GLFW_TRUE;
    }

    // ── ImGui GLFW arka ucunun kullandığı geri kalan işlevler (Android'de imleç, pano, çoklu monitör yok)
    int glfwGetError(const char** description)
    {
        if (description) *description = nullptr;
        return GLFW_NO_ERROR;
    }
    const char* glfwGetVersionString(void) { return "3.4.0 Android (Conqueror NativeActivity/EGL)"; }
    GLFWcursor* glfwCreateCursor(const GLFWimage*, int, int) { return nullptr; }
    GLFWcursor* glfwCreateStandardCursor(int) { return nullptr; }
    void glfwDestroyCursor(GLFWcursor*) {}
    void glfwSetCursor(GLFWwindow*, GLFWcursor*) {}
    int glfwGetInputMode(GLFWwindow*, int mode) { return mode == GLFW_CURSOR ? GLFW_CURSOR_NORMAL : GLFW_FALSE; }
    const char* glfwGetKeyName(int, int) { return nullptr; }
    const char* glfwGetClipboardString(GLFWwindow*) { return s.Clipboard.c_str(); }
    void glfwSetClipboardString(GLFWwindow*, const char* text) { s.Clipboard = text ? text : ""; }
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
    // Ekran yoğunluğu (160 dpi = 1.0): ImGui ve arayüz ölçeği
    void glfwGetMonitorContentScale(GLFWmonitor*, float* xscale, float* yscale)
    {
        const float scale = DensityScale();
        if (xscale) *xscale = scale;
        if (yscale) *yscale = scale;
    }
    void glfwGetWindowContentScale(GLFWwindow*, float* xscale, float* yscale) { glfwGetMonitorContentScale(nullptr, xscale, yscale); }
    GLFWdropfun glfwSetDropCallback(GLFWwindow*, GLFWdropfun) { return nullptr; }
    GLFWcursorenterfun glfwSetCursorEnterCallback(GLFWwindow*, GLFWcursorenterfun) { return nullptr; }
    GLFWwindowfocusfun glfwSetWindowFocusCallback(GLFWwindow*, GLFWwindowfocusfun) { return nullptr; }
    GLFWwindowposfun glfwSetWindowPosCallback(GLFWwindow*, GLFWwindowposfun) { return nullptr; }
    GLFWmonitorfun glfwSetMonitorCallback(GLFWmonitorfun) { return nullptr; }
    int glfwVulkanSupported(void) { return GLFW_FALSE; }
    // Vulkan yüzeyi yok (Android'de OpenGL ES): VK_ERROR_EXTENSION_NOT_PRESENT
    int glfwCreateWindowSurface(void*, GLFWwindow*, const void*, void*) { return -7; }
}

#endif
