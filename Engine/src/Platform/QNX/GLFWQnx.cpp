// QNX'te GLFW yok: motorun kullandığı GLFW işlevleri burada QNX Screen + EGL ile uygulanır (SDP 7.1 / 8.0).
// Pencere = Screen penceresi üzerinde EGL yüzeyi (OpenGL ES 3.2, olmazsa 3.1 / 3.0). Otomotiv HMI'lerinde pencere
// varsayılan olarak ekranın tamamını kaplar (CQ_QNX_WINDOWED=1 ile istenen boyut); Screen'in pencere yöneticisi
// pencereyi SCREEN_PROPERTY_ID_STRING (CQ_APP_ID, varsayılan "conqueror") ile tanır.
// Girdi: klavye (sys/keycodes.h sembolleri), fare, çoklu dokunma (ilk parmak = sol fare düğmesi), oyun kumandası.
#ifdef __QNX__

#include <EGL/egl.h>
#include <screen/screen.h>
#include <sys/keycodes.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <dlfcn.h>
#include <time.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

struct GLFWwindow
{
    screen_window_t Window = nullptr;
    EGLSurface Surface = EGL_NO_SURFACE;
    EGLContext Context = EGL_NO_CONTEXT;
    int Width = 0, Height = 0;
    void* User = nullptr;
    bool ShouldClose = false;
    bool Focused = true;
    double CursorX = 0.0, CursorY = 0.0;
    int TouchId = -1;                       // fareyi süren parmak
    unsigned char Keys[GLFW_KEY_LAST + 1] = {};
    unsigned char Mouse[GLFW_MOUSE_BUTTON_LAST + 1] = {};
    GLFWkeyfun KeyCallback = nullptr;
    GLFWcharfun CharCallback = nullptr;
    GLFWmousebuttonfun MouseButtonCallback = nullptr;
    GLFWcursorposfun CursorPosCallback = nullptr;
    GLFWscrollfun ScrollCallback = nullptr;
    GLFWwindowsizefun SizeCallback = nullptr;
    GLFWwindowclosefun CloseCallback = nullptr;
    GLFWwindowfocusfun FocusCallback = nullptr;
    // Motor tuş / düğme durumunu kare başına okur: aynı karede basılıp bırakılan bırakma bir sonraki kareye ertelenir
    uint64_t KeyPressFrame[GLFW_KEY_LAST + 1] = {};
    uint64_t MousePressFrame[GLFW_MOUSE_BUTTON_LAST + 1] = {};
    std::vector<std::pair<int, int>> DeferredKeyUps;
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
        screen_context_t Context = nullptr;
        screen_event_t Event = nullptr;
        EGLDisplay Display = EGL_NO_DISPLAY;
        EGLConfig Config = nullptr;
        GLFWwindow* Window = nullptr;
        GLFWwindow* Current = nullptr;
        GLFWerrorfun ErrorCallback = nullptr;
        GLFWmonitor Monitor;
        GLFWvidmode Mode{};
        int MinorHint = 2;
        std::string Clipboard;
        bool Gamepad = false;
        uint64_t Frame = 1;
        unsigned char Buttons[GLFW_GAMEPAD_BUTTON_LAST + 1] = {};
        float Axes[GLFW_GAMEPAD_AXIS_LAST + 1] = { 0, 0, 0, 0, -1, -1 };
        timespec Start{};
    };
    State s;

    void ReportError(int code, const char* text)
    {
        if (s.ErrorCallback)
            s.ErrorCallback(code, text);
        std::fprintf(stderr, "[GLFW/QNX] %s\n", text);
    }

    // Birincil ekranın boyutu
    void QueryDisplaySize()
    {
        int count = 0;
        if (screen_get_context_property_iv(s.Context, SCREEN_PROPERTY_DISPLAY_COUNT, &count) != 0 || count <= 0)
            return;
        screen_display_t displays[8] = {};
        if (screen_get_context_property_pv(s.Context, SCREEN_PROPERTY_DISPLAYS, (void**)displays) != 0 || !displays[0])
            return;
        int size[2] = { 0, 0 };
        if (screen_get_display_property_iv(displays[0], SCREEN_PROPERTY_SIZE, size) == 0 && size[0] > 0 && size[1] > 0)
        {
            s.Mode.width = size[0];
            s.Mode.height = size[1];
        }
    }

    int MapKey(int sym)
    {
        if (sym >= 'a' && sym <= 'z') return GLFW_KEY_A + (sym - 'a');
        if (sym >= 'A' && sym <= 'Z') return GLFW_KEY_A + (sym - 'A');
        if (sym >= '0' && sym <= '9') return GLFW_KEY_0 + (sym - '0');
        if (sym >= KEYCODE_F1 && sym <= KEYCODE_F12) return GLFW_KEY_F1 + (sym - KEYCODE_F1);
        switch (sym)
        {
            case KEYCODE_SPACE: return GLFW_KEY_SPACE;
            case KEYCODE_RETURN: case KEYCODE_KP_ENTER: return GLFW_KEY_ENTER;
            case KEYCODE_BACKSPACE: return GLFW_KEY_BACKSPACE;
            case KEYCODE_DELETE: return GLFW_KEY_DELETE;
            case KEYCODE_TAB: return GLFW_KEY_TAB;
            case KEYCODE_ESCAPE: return GLFW_KEY_ESCAPE;
            case KEYCODE_UP: return GLFW_KEY_UP;
            case KEYCODE_DOWN: return GLFW_KEY_DOWN;
            case KEYCODE_LEFT: return GLFW_KEY_LEFT;
            case KEYCODE_RIGHT: return GLFW_KEY_RIGHT;
            case KEYCODE_LEFT_SHIFT: return GLFW_KEY_LEFT_SHIFT;
            case KEYCODE_RIGHT_SHIFT: return GLFW_KEY_RIGHT_SHIFT;
            case KEYCODE_LEFT_CTRL: return GLFW_KEY_LEFT_CONTROL;
            case KEYCODE_RIGHT_CTRL: return GLFW_KEY_RIGHT_CONTROL;
            case KEYCODE_LEFT_ALT: return GLFW_KEY_LEFT_ALT;
            case KEYCODE_RIGHT_ALT: return GLFW_KEY_RIGHT_ALT;
            case KEYCODE_HOME: return GLFW_KEY_HOME;
            case KEYCODE_END: return GLFW_KEY_END;
            case KEYCODE_PG_UP: return GLFW_KEY_PAGE_UP;
            case KEYCODE_PG_DOWN: return GLFW_KEY_PAGE_DOWN;
            case KEYCODE_INSERT: return GLFW_KEY_INSERT;
            case '-': return GLFW_KEY_MINUS;
            case '=': return GLFW_KEY_EQUAL;
            case '[': return GLFW_KEY_LEFT_BRACKET;
            case ']': return GLFW_KEY_RIGHT_BRACKET;
            case '\\': return GLFW_KEY_BACKSLASH;
            case ';': return GLFW_KEY_SEMICOLON;
            case '\'': return GLFW_KEY_APOSTROPHE;
            case ',': return GLFW_KEY_COMMA;
            case '.': return GLFW_KEY_PERIOD;
            case '/': return GLFW_KEY_SLASH;
            case '`': return GLFW_KEY_GRAVE_ACCENT;
            default: return GLFW_KEY_UNKNOWN;
        }
    }

    int Mods(int modifiers)
    {
        int m = 0;
        if (modifiers & KEYMOD_SHIFT) m |= GLFW_MOD_SHIFT;
        if (modifiers & KEYMOD_CTRL) m |= GLFW_MOD_CONTROL;
        if (modifiers & KEYMOD_ALT) m |= GLFW_MOD_ALT;
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

    void SetCursor(GLFWwindow* w, double x, double y)
    {
        if (!w) return;
        w->CursorX = x;
        w->CursorY = y;
        if (w->CursorPosCallback) w->CursorPosCallback(w, x, y);
    }

    void UpdateWindowSize(GLFWwindow* w)
    {
        int size[2] = { 0, 0 };
        if (!w || screen_get_window_property_iv(w->Window, SCREEN_PROPERTY_SIZE, size) != 0)
            return;
        if (size[0] == w->Width && size[1] == w->Height)
            return;
        w->Width = size[0];
        w->Height = size[1];
        if (w->SizeCallback) w->SizeCallback(w, w->Width, w->Height);
    }

    void HandleKeyboard(GLFWwindow* w)
    {
        int flags = 0, sym = 0, cap = 0, modifiers = 0;
        screen_get_event_property_iv(s.Event, SCREEN_PROPERTY_FLAGS, &flags);
        screen_get_event_property_iv(s.Event, SCREEN_PROPERTY_SYM, &sym);
        screen_get_event_property_iv(s.Event, SCREEN_PROPERTY_KEY_CAP, &cap);
        screen_get_event_property_iv(s.Event, SCREEN_PROPERTY_MODIFIERS, &modifiers);
        const bool down = (flags & KEY_DOWN) != 0;
        const bool repeat = (flags & KEY_REPEAT) != 0;
        // Tuş kimliği düzenden bağımsız tuş başlığından (cap), karakter sembolden
        const int key = MapKey((flags & KEY_CAP_VALID) ? cap : sym);
        if (key != GLFW_KEY_UNKNOWN)
        {
            if (down)
            {
                if (w->Keys[key] != GLFW_PRESS)
                    w->KeyPressFrame[key] = s.Frame;
                w->Keys[key] = GLFW_PRESS;
                if (w->KeyCallback)
                    w->KeyCallback(w, key, cap, repeat ? GLFW_REPEAT : GLFW_PRESS, Mods(modifiers));
            }
            else if (w->KeyPressFrame[key] == s.Frame)
                w->DeferredKeyUps.push_back({ key, cap });
            else
            {
                w->Keys[key] = GLFW_RELEASE;
                if (w->KeyCallback)
                    w->KeyCallback(w, key, cap, GLFW_RELEASE, Mods(modifiers));
            }
        }
        if (down && (flags & KEY_SYM_VALID) && sym >= 0x20 && sym < 0xF000 && w->CharCallback)
            w->CharCallback(w, (unsigned int)sym);
    }

    void HandlePointer(GLFWwindow* w)
    {
        int pos[2] = { 0, 0 }, buttons = 0, wheel = 0;
        screen_get_event_property_iv(s.Event, SCREEN_PROPERTY_SOURCE_POSITION, pos);
        screen_get_event_property_iv(s.Event, SCREEN_PROPERTY_BUTTONS, &buttons);
        screen_get_event_property_iv(s.Event, SCREEN_PROPERTY_MOUSE_WHEEL, &wheel);
        SetCursor(w, pos[0], pos[1]);
        SetMouse(w, GLFW_MOUSE_BUTTON_LEFT, (buttons & SCREEN_LEFT_MOUSE_BUTTON) != 0);
        SetMouse(w, GLFW_MOUSE_BUTTON_RIGHT, (buttons & SCREEN_RIGHT_MOUSE_BUTTON) != 0);
        SetMouse(w, GLFW_MOUSE_BUTTON_MIDDLE, (buttons & SCREEN_MIDDLE_MOUSE_BUTTON) != 0);
        if (wheel != 0 && w->ScrollCallback)
            w->ScrollCallback(w, 0.0, wheel > 0 ? -1.0 : 1.0);   // Screen: aşağı pozitif
    }

    void HandleTouch(GLFWwindow* w, int type)
    {
        int id = 0, pos[2] = { 0, 0 };
        screen_get_event_property_iv(s.Event, SCREEN_PROPERTY_TOUCH_ID, &id);
        screen_get_event_property_iv(s.Event, SCREEN_PROPERTY_SOURCE_POSITION, pos);
        if (type == SCREEN_EVENT_MTOUCH_TOUCH && w->TouchId < 0)
        {
            w->TouchId = id;
            SetCursor(w, pos[0], pos[1]);
            SetMouse(w, GLFW_MOUSE_BUTTON_LEFT, true);
        }
        else if (id == w->TouchId)
        {
            SetCursor(w, pos[0], pos[1]);
            if (type == SCREEN_EVENT_MTOUCH_RELEASE)
            {
                SetMouse(w, GLFW_MOUSE_BUTTON_LEFT, false);
                w->TouchId = -1;
            }
        }
    }

    float AxisValue(int raw) { return std::clamp((float)raw / 128.0f, -1.0f, 1.0f); }

    void HandleGamepad()
    {
        s.Gamepad = true;
        int buttons = 0;
        screen_get_event_property_iv(s.Event, SCREEN_PROPERTY_BUTTONS, &buttons);
        auto set = [&](int glfwButton, int screenMask) { s.Buttons[glfwButton] = (buttons & screenMask) ? GLFW_PRESS : GLFW_RELEASE; };
        set(GLFW_GAMEPAD_BUTTON_A, SCREEN_A_GAME_BUTTON);
        set(GLFW_GAMEPAD_BUTTON_B, SCREEN_B_GAME_BUTTON);
        set(GLFW_GAMEPAD_BUTTON_X, SCREEN_X_GAME_BUTTON);
        set(GLFW_GAMEPAD_BUTTON_Y, SCREEN_Y_GAME_BUTTON);
        set(GLFW_GAMEPAD_BUTTON_LEFT_BUMPER, SCREEN_L1_GAME_BUTTON);
        set(GLFW_GAMEPAD_BUTTON_RIGHT_BUMPER, SCREEN_R1_GAME_BUTTON);
        set(GLFW_GAMEPAD_BUTTON_LEFT_THUMB, SCREEN_L3_GAME_BUTTON);
        set(GLFW_GAMEPAD_BUTTON_RIGHT_THUMB, SCREEN_R3_GAME_BUTTON);
        set(GLFW_GAMEPAD_BUTTON_BACK, SCREEN_MENU1_GAME_BUTTON);
        set(GLFW_GAMEPAD_BUTTON_START, SCREEN_MENU2_GAME_BUTTON);
        set(GLFW_GAMEPAD_BUTTON_GUIDE, SCREEN_MENU3_GAME_BUTTON);
        set(GLFW_GAMEPAD_BUTTON_DPAD_UP, SCREEN_DPAD_UP_GAME_BUTTON);
        set(GLFW_GAMEPAD_BUTTON_DPAD_DOWN, SCREEN_DPAD_DOWN_GAME_BUTTON);
        set(GLFW_GAMEPAD_BUTTON_DPAD_LEFT, SCREEN_DPAD_LEFT_GAME_BUTTON);
        set(GLFW_GAMEPAD_BUTTON_DPAD_RIGHT, SCREEN_DPAD_RIGHT_GAME_BUTTON);
        int left[3] = { 0, 0, 0 }, right[3] = { 0, 0, 0 };
        screen_get_event_property_iv(s.Event, SCREEN_PROPERTY_ANALOG0, left);
        screen_get_event_property_iv(s.Event, SCREEN_PROPERTY_ANALOG1, right);
        s.Axes[GLFW_GAMEPAD_AXIS_LEFT_X] = AxisValue(left[0]);
        s.Axes[GLFW_GAMEPAD_AXIS_LEFT_Y] = AxisValue(left[1]);
        s.Axes[GLFW_GAMEPAD_AXIS_RIGHT_X] = AxisValue(right[0]);
        s.Axes[GLFW_GAMEPAD_AXIS_RIGHT_Y] = AxisValue(right[1]);
        // Tetikler: analog değer yoksa L2 / R2 düğmeleri
        s.Axes[GLFW_GAMEPAD_AXIS_LEFT_TRIGGER] = left[2] ? AxisValue(left[2]) : ((buttons & SCREEN_L2_GAME_BUTTON) ? 1.0f : -1.0f);
        s.Axes[GLFW_GAMEPAD_AXIS_RIGHT_TRIGGER] = right[2] ? AxisValue(right[2]) : ((buttons & SCREEN_R2_GAME_BUTTON) ? 1.0f : -1.0f);
    }

    // timeout: 0 = bekleme, ~0 = olay gelene kadar (nanosaniye)
    void PumpEvents(uint64_t timeout)
    {
        if (!s.Context || !s.Event)
            return;
        while (screen_get_event(s.Context, s.Event, timeout) == 0)
        {
            timeout = 0;
            int type = SCREEN_EVENT_NONE;
            screen_get_event_property_iv(s.Event, SCREEN_PROPERTY_TYPE, &type);
            if (type == SCREEN_EVENT_NONE)
                break;
            GLFWwindow* w = s.Window;
            if (!w)
                continue;
            switch (type)
            {
                case SCREEN_EVENT_CLOSE:
                    w->ShouldClose = true;
                    if (w->CloseCallback) w->CloseCallback(w);
                    break;
                case SCREEN_EVENT_PROPERTY:
                {
                    int name = 0;
                    screen_get_event_property_iv(s.Event, SCREEN_PROPERTY_NAME, &name);
                    if (name == SCREEN_PROPERTY_SIZE || name == SCREEN_PROPERTY_BUFFER_SIZE)
                        UpdateWindowSize(w);
                    else if (name == SCREEN_PROPERTY_FOCUS)
                    {
                        int focus = 1;
                        screen_get_window_property_iv(w->Window, SCREEN_PROPERTY_FOCUS, &focus);
                        w->Focused = focus != 0;
                        if (w->FocusCallback) w->FocusCallback(w, w->Focused ? GLFW_TRUE : GLFW_FALSE);
                    }
                    break;
                }
                case SCREEN_EVENT_KEYBOARD: HandleKeyboard(w); break;
                case SCREEN_EVENT_POINTER: HandlePointer(w); break;
                case SCREEN_EVENT_MTOUCH_TOUCH:
                case SCREEN_EVENT_MTOUCH_MOVE:
                case SCREEN_EVENT_MTOUCH_RELEASE:
                    HandleTouch(w, type);
                    break;
                case SCREEN_EVENT_GAMEPAD:
                case SCREEN_EVENT_JOYSTICK:
                    HandleGamepad();
                    break;
                default:
                    break;
            }
        }
    }

    bool InitDisplay()
    {
        if (s.Display != EGL_NO_DISPLAY) return true;
        s.Display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        if (s.Display == EGL_NO_DISPLAY || !eglInitialize(s.Display, nullptr, nullptr)) return false;
        const EGLint attribs[] = {
            EGL_RENDERABLE_TYPE, 0x40 /* EGL_OPENGL_ES3_BIT */, EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
            EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_DEPTH_SIZE, 24, EGL_STENCIL_SIZE, 8, EGL_NONE,
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

// ══════════════════════════════════════════ GLFW API
extern "C"
{
    int glfwInit(void)
    {
        clock_gettime(CLOCK_MONOTONIC, &s.Start);
        if (!s.Context && screen_create_context(&s.Context, SCREEN_APPLICATION_CONTEXT) != 0)
        {
            s.Context = nullptr;
            ReportError(GLFW_PLATFORM_UNAVAILABLE, "screen_create_context failed (is the Screen service running?)");
            return GLFW_FALSE;
        }
        if (!s.Event && screen_create_event(&s.Event) != 0)
            s.Event = nullptr;
        s.Mode.width = 1280;
        s.Mode.height = 720;
        s.Mode.refreshRate = 60;
        s.Mode.redBits = s.Mode.greenBits = s.Mode.blueBits = 8;
        QueryDisplaySize();
        return GLFW_TRUE;
    }

    void glfwTerminate(void)
    {
        if (s.Window) glfwDestroyWindow(s.Window);
        if (s.Display != EGL_NO_DISPLAY) eglTerminate(s.Display);
        s.Display = EGL_NO_DISPLAY;
        if (s.Event) screen_destroy_event(s.Event);
        s.Event = nullptr;
        if (s.Context) screen_destroy_context(s.Context);
        s.Context = nullptr;
    }

    GLFWerrorfun glfwSetErrorCallback(GLFWerrorfun callback)
    {
        GLFWerrorfun prev = s.ErrorCallback;
        s.ErrorCallback = callback;
        return prev;
    }

    int glfwGetPlatform(void) { return GLFW_PLATFORM_NULL; }
    void glfwDefaultWindowHints(void) { s.MinorHint = 2; }
    void glfwWindowHint(int hint, int value)
    {
        if (hint == GLFW_CONTEXT_VERSION_MINOR) s.MinorHint = value;
    }
    void glfwWindowHintString(int, const char*) {}

    GLFWwindow* glfwCreateWindow(int width, int height, const char* title, GLFWmonitor*, GLFWwindow*)
    {
        if (s.Window || !s.Context) return nullptr;   // tek pencere
        if (!InitDisplay())
        {
            ReportError(GLFW_API_UNAVAILABLE, "EGL display initialization failed");
            return nullptr;
        }
        auto* w = new GLFWwindow();
        if (screen_create_window(&w->Window, s.Context) != 0)
        {
            delete w;
            return nullptr;
        }
        int usage = SCREEN_USAGE_OPENGL_ES3;
        screen_set_window_property_iv(w->Window, SCREEN_PROPERTY_USAGE, &usage);
        int format = SCREEN_FORMAT_RGBX8888;
        screen_set_window_property_iv(w->Window, SCREEN_PROPERTY_FORMAT, &format);
        const char* windowed = std::getenv("CQ_QNX_WINDOWED");
        int size[2] = { s.Mode.width, s.Mode.height };
        if (windowed && *windowed == '1' && width > 0 && height > 0)
        {
            size[0] = width;
            size[1] = height;
        }
        screen_set_window_property_iv(w->Window, SCREEN_PROPERTY_SIZE, size);
        screen_set_window_property_iv(w->Window, SCREEN_PROPERTY_BUFFER_SIZE, size);
        const char* appId = std::getenv("CQ_APP_ID");
        const std::string id = appId && *appId ? appId : "conqueror";
        screen_set_window_property_cv(w->Window, SCREEN_PROPERTY_ID_STRING, (int)id.size(), id.c_str());
        (void)title;
        if (screen_create_window_buffers(w->Window, 2) != 0)
        {
            screen_destroy_window(w->Window);
            delete w;
            return nullptr;
        }
        for (int minor : { std::max(s.MinorHint, 2), 1, 0 })
        {
            const EGLint ctx[] = { 0x3098 /* EGL_CONTEXT_MAJOR_VERSION */, 3, 0x30FB /* EGL_CONTEXT_MINOR_VERSION */, minor, EGL_NONE };
            w->Context = eglCreateContext(s.Display, s.Config, EGL_NO_CONTEXT, ctx);
            if (w->Context != EGL_NO_CONTEXT) break;
        }
        if (w->Context != EGL_NO_CONTEXT)
            w->Surface = eglCreateWindowSurface(s.Display, s.Config, (EGLNativeWindowType)w->Window, nullptr);
        if (w->Context == EGL_NO_CONTEXT || w->Surface == EGL_NO_SURFACE)
        {
            ReportError(GLFW_VERSION_UNAVAILABLE, "OpenGL ES 3 context / window surface could not be created");
            if (w->Context != EGL_NO_CONTEXT) eglDestroyContext(s.Display, w->Context);
            screen_destroy_window(w->Window);
            delete w;
            return nullptr;
        }
        w->Width = size[0];
        w->Height = size[1];
        s.Window = w;
        return w;
    }

    void glfwDestroyWindow(GLFWwindow* w)
    {
        if (!w) return;
        eglMakeCurrent(s.Display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (w->Surface != EGL_NO_SURFACE) eglDestroySurface(s.Display, w->Surface);
        if (w->Context != EGL_NO_CONTEXT) eglDestroyContext(s.Display, w->Context);
        if (w->Window) screen_destroy_window(w->Window);
        if (s.Window == w) s.Window = nullptr;
        if (s.Current == w) s.Current = nullptr;
        delete w;
    }

    int glfwWindowShouldClose(GLFWwindow* w) { return w && w->ShouldClose ? GLFW_TRUE : GLFW_FALSE; }
    void glfwSetWindowShouldClose(GLFWwindow* w, int value) { if (w) w->ShouldClose = value != 0; }

    void glfwMakeContextCurrent(GLFWwindow* w)
    {
        s.Current = w;
        if (w) eglMakeCurrent(s.Display, w->Surface, w->Surface, w->Context);
        else eglMakeCurrent(s.Display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    }

    GLFWwindow* glfwGetCurrentContext(void) { return s.Current; }
    void glfwSwapBuffers(GLFWwindow* w) { if (w && w->Surface != EGL_NO_SURFACE) eglSwapBuffers(s.Display, w->Surface); }
    void glfwSwapInterval(int interval) { if (s.Display != EGL_NO_DISPLAY) eglSwapInterval(s.Display, interval); }

    // Çekirdek GLES işlevleri libGLESv2'den (QNX'te ES 3.x de bu kitaplıkta), uzantılar eglGetProcAddress ile
    GLFWglproc glfwGetProcAddress(const char* name)
    {
        static void* gles = dlopen("libGLESv2.so", RTLD_NOW | RTLD_LOCAL);
        if (gles)
            if (void* p = dlsym(gles, name))
                return (GLFWglproc)p;
        return (GLFWglproc)eglGetProcAddress(name);
    }

    void glfwPollEvents(void)
    {
        BeginFrame();
        PumpEvents(0);
    }
    void glfwWaitEvents(void) { PumpEvents(~0ull); }

    void glfwSetWindowUserPointer(GLFWwindow* w, void* p) { if (w) w->User = p; }
    void* glfwGetWindowUserPointer(GLFWwindow* w) { return w ? w->User : nullptr; }
    void glfwGetWindowSize(GLFWwindow* w, int* width, int* height)
    {
        if (width) *width = w ? w->Width : 0;
        if (height) *height = w ? w->Height : 0;
    }
    void glfwGetFramebufferSize(GLFWwindow* w, int* width, int* height) { glfwGetWindowSize(w, width, height); }
    void glfwGetWindowPos(GLFWwindow*, int* x, int* y) { if (x) *x = 0; if (y) *y = 0; }
    void glfwSetWindowPos(GLFWwindow* w, int x, int y)
    {
        int pos[2] = { x, y };
        if (w) screen_set_window_property_iv(w->Window, SCREEN_PROPERTY_POSITION, pos);
    }
    void glfwSetWindowSize(GLFWwindow* w, int width, int height)
    {
        if (!w || width <= 0 || height <= 0) return;
        int size[2] = { width, height };
        screen_set_window_property_iv(w->Window, SCREEN_PROPERTY_SIZE, size);
        UpdateWindowSize(w);
    }
    void glfwSetWindowTitle(GLFWwindow*, const char*) {}
    void glfwSetWindowIcon(GLFWwindow*, int, const GLFWimage*) {}
    void glfwSetWindowAttrib(GLFWwindow*, int, int) {}
    void glfwSetWindowMonitor(GLFWwindow*, GLFWmonitor*, int, int, int, int, int) {}
    void glfwShowWindow(GLFWwindow*) {}
    void glfwFocusWindow(GLFWwindow*) {}
    void glfwSetWindowOpacity(GLFWwindow*, float) {}
    GLFWmonitor* glfwGetPrimaryMonitor(void) { return &s.Monitor; }
    GLFWmonitor** glfwGetMonitors(int* count)
    {
        static GLFWmonitor* monitors[1] = { &s.Monitor };
        if (count) *count = 1;
        return monitors;
    }
    void glfwGetMonitorPos(GLFWmonitor*, int* x, int* y) { if (x) *x = 0; if (y) *y = 0; }
    void glfwGetMonitorWorkarea(GLFWmonitor*, int* x, int* y, int* width, int* height)
    {
        if (x) *x = 0;
        if (y) *y = 0;
        if (width) *width = s.Mode.width;
        if (height) *height = s.Mode.height;
    }
    void glfwGetMonitorContentScale(GLFWmonitor*, float* xscale, float* yscale) { if (xscale) *xscale = 1.0f; if (yscale) *yscale = 1.0f; }
    void glfwGetWindowContentScale(GLFWwindow*, float* xscale, float* yscale) { glfwGetMonitorContentScale(nullptr, xscale, yscale); }
    const GLFWvidmode* glfwGetVideoMode(GLFWmonitor*) { return &s.Mode; }
    int glfwGetWindowAttrib(GLFWwindow* w, int attrib) { return attrib == GLFW_FOCUSED ? (w && w->Focused ? GLFW_TRUE : GLFW_FALSE) : 0; }

    GLFWkeyfun glfwSetKeyCallback(GLFWwindow* w, GLFWkeyfun cb) { GLFWkeyfun p = w->KeyCallback; w->KeyCallback = cb; return p; }
    GLFWcharfun glfwSetCharCallback(GLFWwindow* w, GLFWcharfun cb) { GLFWcharfun p = w->CharCallback; w->CharCallback = cb; return p; }
    GLFWmousebuttonfun glfwSetMouseButtonCallback(GLFWwindow* w, GLFWmousebuttonfun cb) { GLFWmousebuttonfun p = w->MouseButtonCallback; w->MouseButtonCallback = cb; return p; }
    GLFWcursorposfun glfwSetCursorPosCallback(GLFWwindow* w, GLFWcursorposfun cb) { GLFWcursorposfun p = w->CursorPosCallback; w->CursorPosCallback = cb; return p; }
    GLFWscrollfun glfwSetScrollCallback(GLFWwindow* w, GLFWscrollfun cb) { GLFWscrollfun p = w->ScrollCallback; w->ScrollCallback = cb; return p; }
    GLFWwindowsizefun glfwSetWindowSizeCallback(GLFWwindow* w, GLFWwindowsizefun cb) { GLFWwindowsizefun p = w->SizeCallback; w->SizeCallback = cb; return p; }
    GLFWdropfun glfwSetDropCallback(GLFWwindow*, GLFWdropfun) { return nullptr; }
    GLFWwindowclosefun glfwSetWindowCloseCallback(GLFWwindow* w, GLFWwindowclosefun cb) { GLFWwindowclosefun p = w->CloseCallback; w->CloseCallback = cb; return p; }
    GLFWwindowfocusfun glfwSetWindowFocusCallback(GLFWwindow* w, GLFWwindowfocusfun cb) { GLFWwindowfocusfun p = w->FocusCallback; w->FocusCallback = cb; return p; }
    GLFWcursorenterfun glfwSetCursorEnterCallback(GLFWwindow*, GLFWcursorenterfun) { return nullptr; }
    GLFWwindowposfun glfwSetWindowPosCallback(GLFWwindow*, GLFWwindowposfun) { return nullptr; }
    GLFWmonitorfun glfwSetMonitorCallback(GLFWmonitorfun) { return nullptr; }

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
    const char* glfwGetKeyName(int, int) { return nullptr; }
    GLFWcursor* glfwCreateCursor(const GLFWimage*, int, int) { return nullptr; }
    GLFWcursor* glfwCreateStandardCursor(int) { return nullptr; }
    void glfwDestroyCursor(GLFWcursor*) {}
    void glfwSetCursor(GLFWwindow*, GLFWcursor*) {}
    const char* glfwGetClipboardString(GLFWwindow*) { return s.Clipboard.c_str(); }
    void glfwSetClipboardString(GLFWwindow*, const char* text) { s.Clipboard = text ? text : ""; }
    int glfwGetError(const char** description)
    {
        if (description) *description = nullptr;
        return GLFW_NO_ERROR;
    }
    const char* glfwGetVersionString(void) { return "3.4.0 QNX (Conqueror Screen/EGL)"; }

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
    const char* glfwGetJoystickName(int jid) { return glfwJoystickPresent(jid) ? "QNX Gamepad" : nullptr; }
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
    int glfwVulkanSupported(void) { return GLFW_FALSE; }
    int glfwCreateWindowSurface(void*, GLFWwindow*, const void*, void*) { return -7; }   // VK_ERROR_EXTENSION_NOT_PRESENT
}

#endif
