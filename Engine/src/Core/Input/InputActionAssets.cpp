#include "InputActionAssets.h"

#include "Core/KeyCodes.h"
#include "Core/Logging/Log.h"
#include "Core/Serialization/SceneSerializer.h"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>

namespace Conqueror
{
    namespace fs = std::filesystem;

    bool InputBindingDef::InScheme(const std::string& scheme) const
    {
        if (scheme.empty() || Groups.empty())
            return true;
        return std::find(Groups.begin(), Groups.end(), scheme) != Groups.end();
    }

    const InputActionDef* InputActionMapDef::Find(const std::string& name) const
    {
        for (const auto& a : Actions)
            if (a.Name == name)
                return &a;
        return nullptr;
    }

    const InputActionMapDef* InputActionAssetData::FindMap(const std::string& name) const
    {
        for (const auto& m : Maps)
            if (m.Name == name)
                return &m;
        return nullptr;
    }

    const InputActionMapDef* InputActionAssetData::FindMapOrFirst(const std::string& name) const
    {
        if (const auto* m = FindMap(name))
            return m;
        return Maps.empty() ? nullptr : &Maps.front();
    }

    namespace InputActionAssets
    {
        namespace
        {
            // ---------------------------------------------------------------- kontrol kataloğu
            std::vector<InputControlInfo> BuildControls()
            {
                std::vector<InputControlInfo> c;
                auto key = [&](int code, const std::string& name, const std::string& display, const char* category) {
                    c.push_back({ "<Keyboard>/" + name, "Keyboard", display, category, InputControlSource::Key, code, 0, false });
                };
                for (int i = 0; i < 26; ++i)
                {
                    const char lower = (char)('a' + i), upper = (char)('A' + i);
                    key(Key::A + i, std::string(1, lower), std::string(1, upper), "Letters");
                }
                for (int i = 0; i < 10; ++i)
                    key(Key::D0 + i, "digit" + std::to_string(i), std::to_string(i), "Digits");
                key(Key::Up, "upArrow", "Up Arrow", "Arrows");
                key(Key::Down, "downArrow", "Down Arrow", "Arrows");
                key(Key::Left, "leftArrow", "Left Arrow", "Arrows");
                key(Key::Right, "rightArrow", "Right Arrow", "Arrows");
                key(Key::Space, "space", "Space", "Common");
                key(Key::Enter, "enter", "Enter", "Common");
                key(Key::Escape, "escape", "Escape", "Common");
                key(Key::Tab, "tab", "Tab", "Common");
                key(Key::Backspace, "backspace", "Backspace", "Common");
                key(Key::LeftShift, "leftShift", "Left Shift", "Modifiers");
                key(Key::RightShift, "rightShift", "Right Shift", "Modifiers");
                key(Key::LeftControl, "leftCtrl", "Left Ctrl", "Modifiers");
                key(Key::RightControl, "rightCtrl", "Right Ctrl", "Modifiers");
                key(Key::LeftAlt, "leftAlt", "Left Alt", "Modifiers");
                key(Key::RightAlt, "rightAlt", "Right Alt", "Modifiers");
                key(Key::LeftSuper, "leftMeta", "Left System", "Modifiers");
                key(Key::RightSuper, "rightMeta", "Right System", "Modifiers");
                for (int i = 0; i < 12; ++i)
                    key(Key::F1 + i, "f" + std::to_string(i + 1), "F" + std::to_string(i + 1), "Function Keys");
                key(Key::Insert, "insert", "Insert", "Navigation");
                key(Key::Delete, "delete", "Delete", "Navigation");
                key(Key::Home, "home", "Home", "Navigation");
                key(Key::End, "end", "End", "Navigation");
                key(Key::PageUp, "pageUp", "Page Up", "Navigation");
                key(Key::PageDown, "pageDown", "Page Down", "Navigation");
                key(Key::Apostrophe, "quote", "'", "Symbols");
                key(Key::Comma, "comma", ",", "Symbols");
                key(Key::Minus, "minus", "-", "Symbols");
                key(Key::Period, "period", ".", "Symbols");
                key(Key::Slash, "slash", "/", "Symbols");
                key(Key::Semicolon, "semicolon", ";", "Symbols");
                key(Key::Equal, "equals", "=", "Symbols");
                key(Key::LeftBracket, "leftBracket", "[", "Symbols");
                key(Key::Backslash, "backslash", "\\", "Symbols");
                key(Key::RightBracket, "rightBracket", "]", "Symbols");
                key(Key::GraveAccent, "backquote", "`", "Symbols");
                for (int i = 0; i < 10; ++i)
                    key(Key::KP0 + i, "numpad" + std::to_string(i), "Numpad " + std::to_string(i), "Numpad");
                key(Key::KPDecimal, "numpadPeriod", "Numpad .", "Numpad");
                key(Key::KPDivide, "numpadDivide", "Numpad /", "Numpad");
                key(Key::KPMultiply, "numpadMultiply", "Numpad *", "Numpad");
                key(Key::KPSubtract, "numpadMinus", "Numpad -", "Numpad");
                key(Key::KPAdd, "numpadPlus", "Numpad +", "Numpad");
                key(Key::KPEnter, "numpadEnter", "Numpad Enter", "Numpad");
                key(Key::KPEqual, "numpadEquals", "Numpad =", "Numpad");
                key(Key::CapsLock, "capsLock", "Caps Lock", "Other");
                key(Key::ScrollLock, "scrollLock", "Scroll Lock", "Other");
                key(Key::NumLock, "numLock", "Num Lock", "Other");
                key(Key::PrintScreen, "printScreen", "Print Screen", "Other");
                key(Key::Pause, "pause", "Pause", "Other");
                key(Key::Menu, "contextMenu", "Context Menu", "Other");

                auto mouse = [&](const std::string& name, const std::string& display, InputControlSource src, int code, bool is2D) {
                    c.push_back({ "<Mouse>/" + name, "Mouse", display, "Mouse", src, code, 0, is2D });
                };
                mouse("leftButton", "Left Button", InputControlSource::MouseButton, 0, false);
                mouse("rightButton", "Right Button", InputControlSource::MouseButton, 1, false);
                mouse("middleButton", "Middle Button", InputControlSource::MouseButton, 2, false);
                mouse("backButton", "Back Button", InputControlSource::MouseButton, 3, false);
                mouse("forwardButton", "Forward Button", InputControlSource::MouseButton, 4, false);
                mouse("delta", "Delta", InputControlSource::MouseDelta, 0, true);
                mouse("scroll", "Scroll", InputControlSource::MouseScroll, 0, true);
                mouse("scroll/y", "Scroll Y", InputControlSource::MouseScrollY, 0, false);
                mouse("position", "Position", InputControlSource::MousePosition, 0, true);

                auto pad = [&](const std::string& name, const std::string& display, const char* category, InputControlSource src, int code, int extra, bool is2D) {
                    c.push_back({ "<Gamepad>/" + name, "Gamepad", display, category, src, code, extra, is2D });
                };
                pad("buttonSouth", "Button South (A / Cross)", "Buttons", InputControlSource::PadButton, 0, 0, false);
                pad("buttonEast", "Button East (B / Circle)", "Buttons", InputControlSource::PadButton, 1, 0, false);
                pad("buttonWest", "Button West (X / Square)", "Buttons", InputControlSource::PadButton, 2, 0, false);
                pad("buttonNorth", "Button North (Y / Triangle)", "Buttons", InputControlSource::PadButton, 3, 0, false);
                pad("leftShoulder", "Left Shoulder", "Buttons", InputControlSource::PadButton, 4, 0, false);
                pad("rightShoulder", "Right Shoulder", "Buttons", InputControlSource::PadButton, 5, 0, false);
                pad("select", "Select", "Buttons", InputControlSource::PadButton, 6, 0, false);
                pad("start", "Start", "Buttons", InputControlSource::PadButton, 7, 0, false);
                pad("leftStickPress", "Left Stick Press", "Buttons", InputControlSource::PadButton, 9, 0, false);
                pad("rightStickPress", "Right Stick Press", "Buttons", InputControlSource::PadButton, 10, 0, false);
                pad("leftTrigger", "Left Trigger", "Triggers", InputControlSource::PadTrigger, 0, 0, false);
                pad("rightTrigger", "Right Trigger", "Triggers", InputControlSource::PadTrigger, 1, 0, false);
                pad("dpad", "D-Pad", "D-Pad", InputControlSource::PadDpad, 0, 0, true);
                pad("dpad/up", "D-Pad Up", "D-Pad", InputControlSource::PadButton, 11, 0, false);
                pad("dpad/right", "D-Pad Right", "D-Pad", InputControlSource::PadButton, 12, 0, false);
                pad("dpad/down", "D-Pad Down", "D-Pad", InputControlSource::PadButton, 13, 0, false);
                pad("dpad/left", "D-Pad Left", "D-Pad", InputControlSource::PadButton, 14, 0, false);
                const char* sticks[2] = { "leftStick", "rightStick" };
                const char* stickNames[2] = { "Left Stick", "Right Stick" };
                for (int s = 0; s < 2; ++s)
                {
                    const std::string n = sticks[s], d = stickNames[s];
                    pad(n, d, "Sticks", InputControlSource::PadStick, s, 0, true);
                    pad(n + "/x", d + " X", "Sticks", InputControlSource::PadStickAxis, s, 0, false);
                    pad(n + "/y", d + " Y", "Sticks", InputControlSource::PadStickAxis, s, 1, false);
                    pad(n + "/up", d + " Up", "Sticks", InputControlSource::PadStickDir, s, 0, false);
                    pad(n + "/down", d + " Down", "Sticks", InputControlSource::PadStickDir, s, 1, false);
                    pad(n + "/left", d + " Left", "Sticks", InputControlSource::PadStickDir, s, 2, false);
                    pad(n + "/right", d + " Right", "Sticks", InputControlSource::PadStickDir, s, 3, false);
                }
                return c;
            }

            // ---------------------------------------------------------------- önbellek
            struct CacheEntry
            {
                fs::file_time_type Time{};
                uint64_t Stamp = 0;
                InputActionAssetData Data;
            };
            std::unordered_map<std::string, CacheEntry> s_Cache;
            uint64_t s_NextStamp = 1;

            std::string Resolve(const std::string& path)
            {
                return path.empty() ? path : SceneSerializer::ResolveAssetPath(path);
            }

            std::string Trim(std::string s)
            {
                while (!s.empty() && std::isspace((unsigned char)s.front()))
                    s.erase(s.begin());
                while (!s.empty() && std::isspace((unsigned char)s.back()))
                    s.pop_back();
                return s;
            }

            std::string FormatFloat(float v)
            {
                char buf[32];
                std::snprintf(buf, sizeof(buf), "%g", v);
                return buf;
            }

            InputActionKind KindFrom(const std::string& s)
            {
                if (s == "Value") return InputActionKind::Value;
                if (s == "PassThrough" || s == "Pass Through") return InputActionKind::PassThrough;
                return InputActionKind::Button;
            }

            InputControlKind ControlFrom(const std::string& s)
            {
                if (s == "Button") return InputControlKind::Button;
                if (s == "Axis") return InputControlKind::Axis;
                if (s == "Vector2") return InputControlKind::Vector2;
                return InputControlKind::Any;
            }

            std::string ReadText(const std::string& path)
            {
                std::ifstream in(path, std::ios::binary);
                if (!in)
                    return {};
                std::stringstream ss;
                ss << in.rdbuf();
                return ss.str();
            }
        }

        const char* KindName(InputActionKind kind)
        {
            switch (kind)
            {
            case InputActionKind::Value: return "Value";
            case InputActionKind::PassThrough: return "PassThrough";
            default: return "Button";
            }
        }

        const char* ControlName(InputControlKind kind)
        {
            switch (kind)
            {
            case InputControlKind::Button: return "Button";
            case InputControlKind::Axis: return "Axis";
            case InputControlKind::Vector2: return "Vector2";
            default: return "Any";
            }
        }

        // ---------------------------------------------------------------- dosya
        std::string ToString(const InputActionAssetData& data)
        {
            YAML::Emitter out;
            out << YAML::BeginMap;
            out << YAML::Key << "InputActions" << YAML::Value << YAML::BeginMap;
            out << YAML::Key << "Version" << YAML::Value << 1;
            out << YAML::Key << "ControlSchemes" << YAML::Value << YAML::BeginSeq;
            for (const auto& s : data.Schemes)
            {
                out << YAML::BeginMap;
                out << YAML::Key << "Name" << YAML::Value << s.Name;
                out << YAML::Key << "Devices" << YAML::Value << YAML::BeginSeq;
                for (size_t i = 0; i < s.Devices.size(); ++i)
                {
                    out << YAML::BeginMap;
                    out << YAML::Key << "Device" << YAML::Value << s.Devices[i];
                    out << YAML::Key << "Optional" << YAML::Value << (i < s.Optional.size() && s.Optional[i]);
                    out << YAML::EndMap;
                }
                out << YAML::EndSeq;
                out << YAML::EndMap;
            }
            out << YAML::EndSeq;
            out << YAML::Key << "Maps" << YAML::Value << YAML::BeginSeq;
            for (const auto& m : data.Maps)
            {
                out << YAML::BeginMap;
                out << YAML::Key << "Name" << YAML::Value << m.Name;
                out << YAML::Key << "Actions" << YAML::Value << YAML::BeginSeq;
                for (const auto& a : m.Actions)
                {
                    out << YAML::BeginMap;
                    out << YAML::Key << "Name" << YAML::Value << a.Name;
                    out << YAML::Key << "Type" << YAML::Value << KindName(a.Kind);
                    out << YAML::Key << "Control" << YAML::Value << ControlName(a.Control);
                    if (!a.Interactions.empty()) out << YAML::Key << "Interactions" << YAML::Value << a.Interactions;
                    if (!a.Processors.empty()) out << YAML::Key << "Processors" << YAML::Value << a.Processors;
                    if (a.InitialStateCheck) out << YAML::Key << "InitialStateCheck" << YAML::Value << true;
                    out << YAML::Key << "Bindings" << YAML::Value << YAML::BeginSeq;
                    for (const auto& b : a.Bindings)
                    {
                        out << YAML::BeginMap;
                        if (!b.Name.empty()) out << YAML::Key << "Name" << YAML::Value << b.Name;
                        if (!b.Composite.empty()) out << YAML::Key << "Composite" << YAML::Value << b.Composite;
                        if (!b.CompositeParams.empty()) out << YAML::Key << "Params" << YAML::Value << b.CompositeParams;
                        if (!b.Part.empty()) out << YAML::Key << "Part" << YAML::Value << b.Part;
                        if (!b.Composite.empty() && b.Path.empty()) {}
                        else out << YAML::Key << "Path" << YAML::Value << b.Path;
                        if (!b.Interactions.empty()) out << YAML::Key << "Interactions" << YAML::Value << b.Interactions;
                        if (!b.Processors.empty()) out << YAML::Key << "Processors" << YAML::Value << b.Processors;
                        if (!b.Groups.empty())
                        {
                            out << YAML::Key << "Groups" << YAML::Value << YAML::Flow << YAML::BeginSeq;
                            for (const auto& g : b.Groups)
                                out << g;
                            out << YAML::EndSeq;
                        }
                        out << YAML::EndMap;
                    }
                    out << YAML::EndSeq;
                    out << YAML::EndMap;
                }
                out << YAML::EndSeq;
                out << YAML::EndMap;
            }
            out << YAML::EndSeq;
            out << YAML::EndMap;
            out << YAML::EndMap;
            return std::string("# Conqueror's Engine - Input Actions\n") + out.c_str() + "\n";
        }

        bool FromString(const std::string& text, InputActionAssetData& data)
        {
            data = {};
            YAML::Node root;
            try
            {
                root = YAML::Load(text);
            }
            catch (const std::exception& e)
            {
                CQ_CORE_ERROR("Input Actions: parse error: {0}", e.what());
                return false;
            }
            YAML::Node ia = root["InputActions"];
            if (!ia)
                return false;
            auto str = [](const YAML::Node& n, const char* key) { return n[key] ? n[key].as<std::string>() : std::string(); };
            if (auto schemes = ia["ControlSchemes"])
                for (const auto& s : schemes)
                {
                    InputControlSchemeDef def;
                    def.Name = str(s, "Name");
                    if (auto devices = s["Devices"])
                        for (const auto& d : devices)
                        {
                            def.Devices.push_back(str(d, "Device"));
                            def.Optional.push_back(d["Optional"] && d["Optional"].as<bool>());
                        }
                    data.Schemes.push_back(std::move(def));
                }
            if (auto maps = ia["Maps"])
                for (const auto& m : maps)
                {
                    InputActionMapDef map;
                    map.Name = str(m, "Name");
                    if (auto actions = m["Actions"])
                        for (const auto& a : actions)
                        {
                            InputActionDef act;
                            act.Name = str(a, "Name");
                            act.Kind = KindFrom(str(a, "Type"));
                            act.Control = ControlFrom(str(a, "Control"));
                            act.Interactions = str(a, "Interactions");
                            act.Processors = str(a, "Processors");
                            act.InitialStateCheck = a["InitialStateCheck"] && a["InitialStateCheck"].as<bool>();
                            if (auto bindings = a["Bindings"])
                                for (const auto& b : bindings)
                                {
                                    InputBindingDef bd;
                                    bd.Name = str(b, "Name");
                                    bd.Path = str(b, "Path");
                                    bd.Composite = str(b, "Composite");
                                    bd.CompositeParams = str(b, "Params");
                                    bd.Part = str(b, "Part");
                                    bd.Interactions = str(b, "Interactions");
                                    bd.Processors = str(b, "Processors");
                                    if (auto groups = b["Groups"])
                                        for (const auto& g : groups)
                                            bd.Groups.push_back(g.as<std::string>());
                                    act.Bindings.push_back(std::move(bd));
                                }
                            map.Actions.push_back(std::move(act));
                        }
                    data.Maps.push_back(std::move(map));
                }
            return true;
        }

        bool Load(const std::string& pathIn, InputActionAssetData& out)
        {
            const std::string path = Resolve(pathIn);
            std::error_code ec;
            if (path.empty() || !fs::exists(path, ec))
                return false;
            return FromString(ReadText(path), out);
        }

        bool Save(const std::string& pathIn, const InputActionAssetData& data)
        {
            const std::string path = Resolve(pathIn);
            std::ofstream out(path, std::ios::binary | std::ios::trunc);
            if (!out)
            {
                CQ_CORE_ERROR("Input Actions: cannot write {0}", path);
                return false;
            }
            out << ToString(data);
            out.close();
            Invalidate(path);
            return true;
        }

        const InputActionAssetData* Get(const std::string& pathIn)
        {
            const std::string path = Resolve(pathIn);
            if (path.empty())
                return nullptr;
            std::error_code ec;
            const auto time = fs::last_write_time(path, ec);
            if (ec)
                return nullptr;
            auto it = s_Cache.find(path);
            if (it == s_Cache.end() || it->second.Time != time)
            {
                CacheEntry e;
                e.Time = time;
                e.Stamp = s_NextStamp++;
                if (!FromString(ReadText(path), e.Data))
                    CQ_CORE_WARN("Input Actions: {0} could not be read", path);
                it = s_Cache.insert_or_assign(path, std::move(e)).first;
            }
            return &it->second.Data;
        }

        uint64_t Stamp(const std::string& pathIn)
        {
            if (!Get(pathIn))
                return 0;
            auto it = s_Cache.find(Resolve(pathIn));
            return it != s_Cache.end() ? it->second.Stamp : 0;
        }

        void Invalidate(const std::string& path)
        {
            s_Cache.erase(Resolve(path));
        }

        // ---------------------------------------------------------------- varsayılan varlık
        InputActionAssetData MakeDefault()
        {
            InputActionAssetData d;
            d.Schemes.push_back({ "Keyboard&Mouse", { "Keyboard", "Mouse" }, { false, false } });
            d.Schemes.push_back({ "Gamepad", { "Gamepad" }, { false } });
            const std::vector<std::string> km = { "Keyboard&Mouse" }, gp = { "Gamepad" };
            auto bind = [](const std::string& path, const std::vector<std::string>& groups, const std::string& processors = std::string()) {
                InputBindingDef b;
                b.Path = path;
                b.Groups = groups;
                b.Processors = processors;
                return b;
            };
            auto dpadComposite = [&](std::vector<InputBindingDef>& out, const std::string& name, const char* up, const char* down, const char* left,
                                     const char* right) {
                InputBindingDef root;
                root.Name = name;
                root.Composite = "2DVector";
                root.CompositeParams = "mode=DigitalNormalized";
                out.push_back(root);
                const char* parts[4][2] = { { "up", up }, { "down", down }, { "left", left }, { "right", right } };
                for (auto& p : parts)
                {
                    InputBindingDef b = bind(std::string("<Keyboard>/") + p[1], km);
                    b.Part = p[0];
                    out.push_back(b);
                }
            };
            auto action = [](const std::string& name, InputActionKind kind, InputControlKind control) {
                InputActionDef a;
                a.Name = name;
                a.Kind = kind;
                a.Control = control;
                a.InitialStateCheck = kind != InputActionKind::Button;
                return a;
            };

            InputActionMapDef player;
            player.Name = "Player";
            {
                auto move = action("Move", InputActionKind::Value, InputControlKind::Vector2);
                move.Bindings.push_back(bind("<Gamepad>/leftStick", gp));
                dpadComposite(move.Bindings, "WASD", "w", "s", "a", "d");
                dpadComposite(move.Bindings, "Arrows", "upArrow", "downArrow", "leftArrow", "rightArrow");
                player.Actions.push_back(move);
                auto look = action("Look", InputActionKind::Value, InputControlKind::Vector2);
                look.Bindings.push_back(bind("<Mouse>/delta", km));
                look.Bindings.push_back(bind("<Gamepad>/rightStick", gp, "scaleVector2(x=10,y=10)"));
                player.Actions.push_back(look);
                auto attack = action("Attack", InputActionKind::Button, InputControlKind::Button);
                attack.Bindings = { bind("<Mouse>/leftButton", km), bind("<Gamepad>/buttonWest", gp), bind("<Keyboard>/enter", km) };
                player.Actions.push_back(attack);
                auto interact = action("Interact", InputActionKind::Button, InputControlKind::Button);
                interact.Interactions = "hold(duration=0.4,pressPoint=0.5)";
                interact.Bindings = { bind("<Keyboard>/e", km), bind("<Gamepad>/buttonNorth", gp) };
                player.Actions.push_back(interact);
                auto crouch = action("Crouch", InputActionKind::Button, InputControlKind::Button);
                crouch.Bindings = { bind("<Keyboard>/c", km), bind("<Gamepad>/buttonEast", gp) };
                player.Actions.push_back(crouch);
                auto jump = action("Jump", InputActionKind::Button, InputControlKind::Button);
                jump.Bindings = { bind("<Keyboard>/space", km), bind("<Gamepad>/buttonSouth", gp) };
                player.Actions.push_back(jump);
                auto prev = action("Previous", InputActionKind::Button, InputControlKind::Button);
                prev.Bindings = { bind("<Keyboard>/digit1", km), bind("<Gamepad>/dpad/left", gp) };
                player.Actions.push_back(prev);
                auto next = action("Next", InputActionKind::Button, InputControlKind::Button);
                next.Bindings = { bind("<Keyboard>/digit2", km), bind("<Gamepad>/dpad/right", gp) };
                player.Actions.push_back(next);
                auto sprint = action("Sprint", InputActionKind::Button, InputControlKind::Button);
                sprint.Bindings = { bind("<Keyboard>/leftShift", km), bind("<Gamepad>/leftStickPress", gp) };
                player.Actions.push_back(sprint);
            }
            d.Maps.push_back(player);

            InputActionMapDef ui;
            ui.Name = "UI";
            {
                auto navigate = action("Navigate", InputActionKind::PassThrough, InputControlKind::Vector2);
                navigate.Bindings.push_back(bind("<Gamepad>/leftStick", gp));
                navigate.Bindings.push_back(bind("<Gamepad>/dpad", gp));
                dpadComposite(navigate.Bindings, "WASD", "w", "s", "a", "d");
                dpadComposite(navigate.Bindings, "Arrows", "upArrow", "downArrow", "leftArrow", "rightArrow");
                ui.Actions.push_back(navigate);
                auto submit = action("Submit", InputActionKind::Button, InputControlKind::Button);
                submit.Bindings = { bind("<Keyboard>/enter", km), bind("<Gamepad>/buttonSouth", gp) };
                ui.Actions.push_back(submit);
                auto cancel = action("Cancel", InputActionKind::Button, InputControlKind::Button);
                cancel.Bindings = { bind("<Keyboard>/escape", km), bind("<Gamepad>/buttonEast", gp) };
                ui.Actions.push_back(cancel);
                auto point = action("Point", InputActionKind::PassThrough, InputControlKind::Vector2);
                point.Bindings = { bind("<Mouse>/position", km) };
                ui.Actions.push_back(point);
                auto click = action("Click", InputActionKind::PassThrough, InputControlKind::Button);
                click.Bindings = { bind("<Mouse>/leftButton", km) };
                ui.Actions.push_back(click);
                auto right = action("RightClick", InputActionKind::PassThrough, InputControlKind::Button);
                right.Bindings = { bind("<Mouse>/rightButton", km) };
                ui.Actions.push_back(right);
                auto middle = action("MiddleClick", InputActionKind::PassThrough, InputControlKind::Button);
                middle.Bindings = { bind("<Mouse>/middleButton", km) };
                ui.Actions.push_back(middle);
                auto scroll = action("ScrollWheel", InputActionKind::PassThrough, InputControlKind::Vector2);
                scroll.Bindings = { bind("<Mouse>/scroll", km) };
                ui.Actions.push_back(scroll);
            }
            d.Maps.push_back(ui);
            return d;
        }

        bool CreateDefault(const std::string& path)
        {
            return Save(path, MakeDefault());
        }

        // ---------------------------------------------------------------- katalog / görünen adlar
        const std::vector<InputControlInfo>& Controls()
        {
            static const std::vector<InputControlInfo> s_Controls = BuildControls();
            return s_Controls;
        }

        const InputControlInfo* FindControl(const std::string& path)
        {
            static std::unordered_map<std::string, const InputControlInfo*> s_Index = [] {
                std::unordered_map<std::string, const InputControlInfo*> index;
                for (const auto& c : Controls())
                    index[c.Path] = &c;
                return index;
            }();
            auto it = s_Index.find(path);
            return it != s_Index.end() ? it->second : nullptr;
        }

        std::string DeviceOf(const std::string& path)
        {
            if (path.size() > 2 && path[0] == '<')
            {
                const size_t end = path.find('>');
                if (end != std::string::npos)
                    return path.substr(1, end - 1);
            }
            return {};
        }

        std::string ControlDisplay(const std::string& path)
        {
            if (path.empty())
                return "<No Binding>";
            if (const auto* c = FindControl(path))
                return c->Display + " [" + c->Device + "]";
            return path;
        }

        std::string DisplayName(const InputBindingDef& b)
        {
            if (b.IsComposite())
                return b.Name.empty() ? CompositeDisplay(b.Composite) : b.Name;
            if (b.IsPart())
            {
                std::string part = b.Part;
                if (!part.empty())
                    part[0] = (char)std::toupper((unsigned char)part[0]);
                return part + ": " + ControlDisplay(b.Path);
            }
            return ControlDisplay(b.Path);
        }

        // ---------------------------------------------------------------- etkileşim / işlemci dizgeleri
        float Spec::Get(const std::string& key, float def) const
        {
            for (const auto& [k, v] : Params)
                if (k == key)
                    return v;
            return def;
        }

        void Spec::Set(const std::string& key, float value)
        {
            for (auto& [k, v] : Params)
                if (k == key)
                {
                    v = value;
                    return;
                }
            Params.push_back({ key, value });
        }

        std::vector<Spec> ParseSpecs(const std::string& text)
        {
            std::vector<Spec> specs;
            std::string current;
            int depth = 0;
            auto flush = [&] {
                std::string item = Trim(current);
                current.clear();
                if (item.empty())
                    return;
                Spec s;
                const size_t open = item.find('(');
                s.Name = Trim(item.substr(0, open));
                if (open != std::string::npos)
                {
                    const size_t close = item.rfind(')');
                    std::string params = item.substr(open + 1, (close == std::string::npos ? item.size() : close) - open - 1);
                    std::stringstream ss(params);
                    std::string kv;
                    while (std::getline(ss, kv, ','))
                    {
                        const size_t eq = kv.find('=');
                        if (eq == std::string::npos)
                            continue;
                        try
                        {
                            s.Params.push_back({ Trim(kv.substr(0, eq)), std::stof(Trim(kv.substr(eq + 1))) });
                        }
                        catch (...)
                        {
                        }
                    }
                }
                if (!s.Name.empty())
                    specs.push_back(std::move(s));
            };
            for (char ch : text)
            {
                if (ch == '(') ++depth;
                if (ch == ')') --depth;
                if (ch == ',' && depth == 0)
                {
                    flush();
                    continue;
                }
                current += ch;
            }
            flush();
            return specs;
        }

        std::string FormatSpecs(const std::vector<Spec>& specs)
        {
            std::string out;
            for (size_t i = 0; i < specs.size(); ++i)
            {
                if (i)
                    out += ",";
                out += specs[i].Name;
                if (!specs[i].Params.empty())
                {
                    out += "(";
                    for (size_t p = 0; p < specs[i].Params.size(); ++p)
                    {
                        if (p)
                            out += ",";
                        out += specs[i].Params[p].first + "=" + FormatFloat(specs[i].Params[p].second);
                    }
                    out += ")";
                }
            }
            return out;
        }

        std::string Param(const std::string& params, const std::string& key, const std::string& def)
        {
            std::stringstream ss(params);
            std::string kv;
            while (std::getline(ss, kv, ','))
            {
                const size_t eq = kv.find('=');
                if (eq != std::string::npos && Trim(kv.substr(0, eq)) == key)
                    return Trim(kv.substr(eq + 1));
            }
            return def;
        }

        std::string SetParam(const std::string& params, const std::string& key, const std::string& value)
        {
            std::vector<std::pair<std::string, std::string>> kvs;
            std::stringstream ss(params);
            std::string kv;
            bool found = false;
            while (std::getline(ss, kv, ','))
            {
                const size_t eq = kv.find('=');
                if (eq == std::string::npos)
                    continue;
                std::string k = Trim(kv.substr(0, eq)), v = Trim(kv.substr(eq + 1));
                if (k == key)
                {
                    v = value;
                    found = true;
                }
                kvs.push_back({ k, v });
            }
            if (!found)
                kvs.push_back({ key, value });
            std::string out;
            for (size_t i = 0; i < kvs.size(); ++i)
                out += (i ? "," : "") + kvs[i].first + "=" + kvs[i].second;
            return out;
        }

        const std::vector<std::string>& CompositeTypes()
        {
            static const std::vector<std::string> s_Types = { "2DVector", "1DAxis", "OneModifier", "TwoModifiers" };
            return s_Types;
        }

        std::vector<std::string> CompositeParts(const std::string& composite)
        {
            if (composite == "2DVector") return { "up", "down", "left", "right" };
            if (composite == "1DAxis") return { "negative", "positive" };
            if (composite == "OneModifier") return { "modifier", "binding" };
            if (composite == "TwoModifiers") return { "modifier1", "modifier2", "binding" };
            return {};
        }

        std::string CompositeDisplay(const std::string& composite)
        {
            if (composite == "2DVector") return "2D Vector";
            if (composite == "1DAxis") return "1D Axis";
            if (composite == "OneModifier") return "One Modifier";
            if (composite == "TwoModifiers") return "Two Modifiers";
            return composite;
        }

        const std::vector<SpecInfo>& InteractionCatalog()
        {
            static const std::vector<SpecInfo> s_Catalog = {
                { "hold", "Hold", { { "duration", 0.4f }, { "pressPoint", 0.5f } }, "Started when pressed, Performed after the control is held for Duration seconds, Canceled when released." },
                { "tap", "Tap", { { "duration", 0.2f }, { "pressPoint", 0.5f } }, "Performed when the control is pressed and released within Duration seconds; Canceled when held longer." },
                { "slowTap", "Slow Tap", { { "duration", 0.5f }, { "pressPoint", 0.5f } }, "Performed when the control is released after being held at least Duration seconds." },
                { "multiTap", "Multi Tap", { { "tapTime", 0.2f }, { "tapDelay", 0.75f }, { "tapCount", 2.0f }, { "pressPoint", 0.5f } },
                  "Performed after Tap Count quick taps (each shorter than Tap Time, at most Tap Delay apart), e.g. a double click." },
                { "press", "Press", { { "pressPoint", 0.5f }, { "behavior", 0.0f } },
                  "Behavior 0: Performed on press. 1: Performed on release. 2: Performed on press and again on release." },
            };
            return s_Catalog;
        }

        const std::vector<SpecInfo>& ProcessorCatalog()
        {
            static const std::vector<SpecInfo> s_Catalog = {
                { "invert", "Invert", {}, "Multiplies the value by -1." },
                { "invertVector2", "Invert Vector 2", { { "invertX", 1.0f }, { "invertY", 1.0f } }, "Inverts X and / or Y of a 2D value." },
                { "normalize", "Normalize", { { "min", 0.0f }, { "max", 1.0f }, { "zero", 0.0f } }, "Maps the range Min..Max to 0..1 (or -1..1 around Zero)." },
                { "normalizeVector2", "Normalize Vector 2", {}, "Makes a 2D value unit length (direction only)." },
                { "scale", "Scale", { { "factor", 1.0f } }, "Multiplies the value by Factor." },
                { "scaleVector2", "Scale Vector 2", { { "x", 1.0f }, { "y", 1.0f } }, "Multiplies X and Y of a 2D value." },
                { "clamp", "Clamp", { { "min", 0.0f }, { "max", 1.0f } }, "Keeps the value between Min and Max." },
                { "axisDeadzone", "Axis Deadzone", { { "min", 0.125f }, { "max", 0.925f } }, "Values below Min become 0, above Max become 1, rescaled in between." },
                { "stickDeadzone", "Stick Deadzone", { { "min", 0.125f }, { "max", 0.925f } }, "Like Axis Deadzone, applied to the length of a 2D stick value." },
            };
            return s_Catalog;
        }

        const SpecInfo* FindInteraction(const std::string& name)
        {
            for (const auto& s : InteractionCatalog())
                if (name == s.Name)
                    return &s;
            return nullptr;
        }

        const SpecInfo* FindProcessor(const std::string& name)
        {
            for (const auto& s : ProcessorCatalog())
                if (name == s.Name)
                    return &s;
            return nullptr;
        }
    }
}
