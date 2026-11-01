#include "InputGlyphs.h"
#include "Input.h"
#include "Core/Console/ConsoleVariables.h"
#include "Core/Utils/FileSystem.h"
#include "Core/Utils/Platform.h"
#include "Renderer/RHI/Texture.h"

#include <GLFW/glfw3.h>
#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <unordered_map>

namespace Conqueror::InputGlyphs
{
    namespace
    {
        bool s_UsingGamepad = false;
        bool s_HaveForced = false;
        Style s_Forced = Style::Keyboard;
        glm::vec2 s_LastMouse{ -1.0f };
        std::unordered_map<std::string, std::shared_ptr<Texture2D>> s_Textures;

        std::string Lower(std::string s)
        {
            for (char& c : s)
                c = (char)std::tolower((unsigned char)c);
            return s;
        }

        const char* StyleDir(Style style)
        {
            switch (style)
            {
                case Style::Xbox: return "xbox";
                case Style::PlayStation: return "playstation";
                case Style::Nintendo: return "nintendo";
                case Style::SteamDeck: return "deck";
                default: return "keyboard";
            }
        }

        // GLFW standart kumanda düğmesi -> simge adı
        const char* PadButtonName(int button)
        {
            static const char* kNames[] = { "a", "b", "x", "y", "lb", "rb", "back", "start", "guide", "ls", "rs",
                                            "dpad_up", "dpad_right", "dpad_down", "dpad_left" };
            return button >= 0 && button < (int)(sizeof(kNames) / sizeof(kNames[0])) ? kNames[button] : nullptr;
        }

        const char* PadAxisName(int axis)
        {
            switch (axis)
            {
                case 0: case 1: return "left_stick";
                case 2: case 3: return "right_stick";
                case 4: return "lt";
                case 5: return "rt";
                default: return nullptr;
            }
        }

        std::string KeyName(int key)
        {
            if (key >= 'A' && key <= 'Z') return std::string("key_") + (char)std::tolower(key);
            if (key >= '0' && key <= '9') return std::string("key_") + (char)key;
            if (key >= 290 && key <= 301) return "key_f" + std::to_string(key - 289);
            switch (key)
            {
                case 32: return "key_space";
                case 256: return "key_escape";
                case 257: case 335: return "key_enter";
                case 258: return "key_tab";
                case 259: return "key_backspace";
                case 261: return "key_delete";
                case 262: return "key_right";
                case 263: return "key_left";
                case 264: return "key_down";
                case 265: return "key_up";
                case 340: case 344: return "key_shift";
                case 341: case 345: return "key_ctrl";
                case 342: case 346: return "key_alt";
                default: return {};
            }
        }

        const char* MouseName(int button)
        {
            switch (button)
            {
                case 0: return "mouse_left";
                case 1: return "mouse_right";
                case 2: return "mouse_middle";
                default: return nullptr;
            }
        }

        bool IsKeyboardName(const std::string& n) { return n.rfind("key_", 0) == 0 || n.rfind("mouse", 0) == 0; }

        std::string GlyphFile(const char* dir, const std::string& name)
        {
            const std::string rel = std::string("Resources/Textures/Glyphs/") + dir + "/" + name + ".png";
            const std::string path = FileSystem::ResolveResourcePath(rel);
            std::error_code ec;
            return std::filesystem::exists(path, ec) ? path : std::string();
        }

        // Bir eylemin ilk kumanda / klavye bağı -> "pad|key" belirteci
        std::string FromAction(const std::string& action)
        {
            for (const std::string& ctxName : Input::GetContextNames())
            {
                const InputContext* ctx = Input::FindContext(ctxName);
                if (!ctx)
                    continue;
                auto it = ctx->Actions.find(action);
                if (it == ctx->Actions.end())
                    continue;
                const InputAction& a = it->second;
                std::string pad, key;
                if (!a.GamepadButtons.empty() && PadButtonName(a.GamepadButtons.front()))
                    pad = PadButtonName(a.GamepadButtons.front());
                else if (!a.GamepadAxes.empty() && PadAxisName(a.GamepadAxes.front()))
                    pad = PadAxisName(a.GamepadAxes.front());
                if (!a.Keys.empty())
                    key = KeyName((int)a.Keys.front());
                else if (!a.MouseButtons.empty() && MouseName((int)a.MouseButtons.front()))
                    key = MouseName((int)a.MouseButtons.front());
                if (!pad.empty() || !key.empty())
                    return pad + "|" + key;
            }
            return {};
        }
    }

    void Update()
    {
        bool gamepad = false, keyboardMouse = false;
        if (Input::IsGamepadConnected(0))
        {
            for (int b = 0; b <= GLFW_GAMEPAD_BUTTON_LAST && !gamepad; ++b)
                gamepad = Input::IsGamepadButtonPressed(b, 0);
            gamepad = gamepad || glm::length(Input::GetGamepadLeftStick(0)) > 0.45f || glm::length(Input::GetGamepadRightStick(0)) > 0.45f ||
                      Input::GetGamepadLeftTrigger(0) > 0.45f || Input::GetGamepadRightTrigger(0) > 0.45f;
        }
        const glm::vec2 mouse = Input::GetMousePosition();
        if (s_LastMouse.x >= 0.0f && glm::distance(mouse, s_LastMouse) > 3.0f)
            keyboardMouse = true;
        s_LastMouse = mouse;
        for (int b = 0; b < 3 && !keyboardMouse; ++b)
            keyboardMouse = Input::IsMouseButtonPressed((MouseCode)b);
        for (int k = 32; k <= 348 && !keyboardMouse; ++k)
            keyboardMouse = Input::IsKeyPressed((KeyCode)k);
        // ImGui üzerinden gelen girdi (editörün oyun görünümü, otomasyon): fare hareketi / tıklama / klavye tuşu
        if (!keyboardMouse && ImGui::GetCurrentContext())
        {
            const ImGuiIO& io = ImGui::GetIO();
            static ImVec2 s_LastImGuiMouse(-1.0f, -1.0f);
            if (s_LastImGuiMouse.x >= 0.0f && (std::fabs(io.MousePos.x - s_LastImGuiMouse.x) > 3.0f || std::fabs(io.MousePos.y - s_LastImGuiMouse.y) > 3.0f))
                keyboardMouse = true;
            s_LastImGuiMouse = io.MousePos;
            for (int b = 0; b < 3 && !keyboardMouse; ++b)
                keyboardMouse = io.MouseDown[b];
            for (int k = ImGuiKey_Tab; k <= ImGuiKey_KeypadEqual && !keyboardMouse; ++k)
                keyboardMouse = ImGui::IsKeyDown((ImGuiKey)k);
        }
        if (gamepad)
            s_UsingGamepad = true;
        else if (keyboardMouse)
            s_UsingGamepad = false;
    }

    bool UsingGamepad()
    {
        if (s_HaveForced)
            return s_Forced != Style::Keyboard;
        return s_UsingGamepad;
    }

    Style GamepadStyle(int gamepadID)
    {
        if (s_HaveForced && s_Forced != Style::Keyboard)
            return s_Forced;
        const std::string name = Lower(Input::GetGamepadName(gamepadID));
        auto has = [&](const char* s) { return name.find(s) != std::string::npos; };
        if (has("steam deck") || (Platform::IsSteamDeck() && (name.empty() || has("steam") || has("valve"))))
            return Style::SteamDeck;
        if (has("ps3") || has("ps4") || has("ps5") || has("dualshock") || has("dualsense") || has("playstation") || has("sony"))
            return Style::PlayStation;
        if (has("nintendo") || has("switch") || has("joy-con") || has("joycon"))
            return Style::Nintendo;
        return Platform::IsSteamDeck() ? Style::SteamDeck : Style::Xbox;
    }

    Style ActiveStyle()
    {
        if (s_HaveForced)
            return s_Forced;
        return UsingGamepad() ? GamepadStyle(0) : Style::Keyboard;
    }

    const char* StyleName(Style style)
    {
        switch (style)
        {
            case Style::Xbox: return "Xbox";
            case Style::PlayStation: return "PlayStation";
            case Style::Nintendo: return "Nintendo";
            case Style::SteamDeck: return "SteamDeck";
            default: return "Keyboard";
        }
    }

    bool ParseStyle(const std::string& text, Style& out)
    {
        const std::string n = Lower(text);
        if (n == "keyboard" || n == "kbm" || n == "mouse") out = Style::Keyboard;
        else if (n == "xbox") out = Style::Xbox;
        else if (n == "playstation" || n == "ps" || n == "ps5" || n == "ps4") out = Style::PlayStation;
        else if (n == "nintendo" || n == "switch") out = Style::Nintendo;
        else if (n == "steamdeck" || n == "deck" || n == "steam_deck") out = Style::SteamDeck;
        else return false;
        return true;
    }

    void SetForcedStyle(const std::string& name)
    {
        Style style;
        if (ParseStyle(name, style))
        {
            s_HaveForced = true;
            s_Forced = style;
        }
        else
            s_HaveForced = false;
    }

    std::string GetForcedStyle() { return s_HaveForced ? StyleName(s_Forced) : "auto"; }

    std::string Resolve(const std::string& specIn)
    {
        std::string spec = Lower(specIn);
        // Bilinen simge adı değilse eylem adı olabilir
        if (spec.find('|') == std::string::npos && GlyphFile("xbox", spec).empty() && GlyphFile("keyboard", spec).empty() &&
            GlyphFile("deck", spec).empty())
        {
            const std::string fromAction = FromAction(specIn);
            if (!fromAction.empty())
                spec = fromAction;
        }
        std::string pad = spec, key;
        if (const size_t bar = spec.find('|'); bar != std::string::npos)
        {
            pad = spec.substr(0, bar);
            key = spec.substr(bar + 1);
        }
        if (IsKeyboardName(pad) && key.empty())
            std::swap(pad, key);

        const bool gamepad = UsingGamepad();
        if (!gamepad && !key.empty())
            if (std::string path = GlyphFile("keyboard", key); !path.empty())
                return path;
        if (!pad.empty())
        {
            const Style style = gamepad ? GamepadStyle(0) : (Platform::IsSteamDeck() ? Style::SteamDeck : Style::Xbox);
            if (std::string path = GlyphFile(StyleDir(style), pad); !path.empty())
                return path;
            // Bu kumandada olmayan düğme (ör. Xbox'ta L4): Deck simgesi
            if (std::string path = GlyphFile("deck", pad); !path.empty())
                return path;
        }
        if (!key.empty())
            return GlyphFile("keyboard", key);
        return {};
    }

    std::shared_ptr<Texture2D> Texture(const std::string& spec)
    {
        const std::string path = Resolve(spec);
        if (path.empty())
            return nullptr;
        auto it = s_Textures.find(path);
        if (it != s_Textures.end())
            return it->second;
        auto tex = Texture2D::Create(path);
        s_Textures[path] = tex;
        return tex;
    }

    std::string Label(const std::string& spec)
    {
        const std::string path = Resolve(spec);
        if (path.empty())
            return spec;
        std::string name = std::filesystem::path(path).stem().string();
        const std::string dir = std::filesystem::path(path).parent_path().filename().string();
        if (name.rfind("key_", 0) == 0)
        {
            name = name.substr(4);
            if (!name.empty())
                name[0] = (char)std::toupper((unsigned char)name[0]);
            return name;
        }
        if (dir == "playstation")
        {
            static const std::unordered_map<std::string, std::string> ps = { { "a", "Cross" }, { "b", "Circle" }, { "x", "Square" },
                                                                              { "y", "Triangle" }, { "lb", "L1" }, { "rb", "R1" },
                                                                              { "lt", "L2" }, { "rt", "R2" }, { "start", "Options" }, { "back", "Create" } };
            if (auto it = ps.find(name); it != ps.end()) return it->second;
        }
        if (dir == "nintendo")
        {
            static const std::unordered_map<std::string, std::string> nin = { { "a", "B" }, { "b", "A" }, { "x", "Y" }, { "y", "X" },
                                                                               { "lb", "L" }, { "rb", "R" }, { "lt", "ZL" }, { "rt", "ZR" },
                                                                               { "start", "+" }, { "back", "-" } };
            if (auto it = nin.find(name); it != nin.end()) return it->second;
        }
        if (dir == "deck")
        {
            static const std::unordered_map<std::string, std::string> deck = { { "lb", "L1" }, { "rb", "R1" }, { "lt", "L2" }, { "rt", "R2" },
                                                                               { "start", "Menu" }, { "back", "View" } };
            if (auto it = deck.find(name); it != deck.end()) return it->second;
        }
        for (char& c : name)
            c = (char)std::toupper((unsigned char)c);
        return name;
    }

    void Shutdown() { s_Textures.clear(); }

    void RegisterConsoleVariables()
    {
        ConsoleVariable v;
        v.Name = "input.GlyphStyle";
        v.Description = "Button glyph style: auto, keyboard, xbox, playstation, nintendo, deck";
        v.Type = CVarType::String;
        v.Get = [] { return GetForcedStyle(); };
        v.Set = [](const std::string& value) {
            Style s;
            if (Lower(value) != "auto" && !ParseStyle(value, s))
                return false;
            SetForcedStyle(value);
            return true;
        };
        v.Reset = [] { SetForcedStyle("auto"); };
        ConsoleSystem::RegisterVariable(std::move(v));
    }
}
