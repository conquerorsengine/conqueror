#include "Automation.h"
#include "Core/Logging/Log.h"
#include "Core/Debug/CrashHandler.h"
#include "Core/Input/Input.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <CQImGuiAutomation.h>
#include <glad/glad.h>
#include "stb_image_write.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

namespace Conqueror
{
    namespace
    {
        struct ScriptLine
        {
            int Number = 0;
            std::vector<std::string> Tokens;
        };

        enum class InputStepType
        {
            MousePos,
            MouseButton,
            MouseWheel,
            Key,
            Text,
            Idle
        };

        struct InputStep
        {
            InputStepType Type = InputStepType::Idle;
            float X = 0.0f, Y = 0.0f;
            int Button = 0;
            bool Down = false;
            ImGuiKey Key = ImGuiKey_None;
            std::string Text;
        };

        struct AutomationState
        {
            bool Active = false;
            bool QuitRequested = false;
            std::string ScriptPath;
            std::vector<ScriptLine> Lines;
            size_t Cursor = 0;
            int WaitFrames = 0;
            std::chrono::steady_clock::time_point SleepUntil{};
            std::deque<InputStep> InputQueue;
            ImVec2 MousePos = { 0.0f, 0.0f };
            bool HasMousePos = false;
            std::string PendingScreenshot;
            std::ofstream ResultLog;
            int Failures = 0;
            std::chrono::steady_clock::time_point StartTime{};
            double TimeoutSeconds = 300.0;
            std::map<std::string, Automation::CommandHandler> Commands;
            uint64_t Frame = 0;
            std::vector<std::pair<int, uint64_t>> PadReleases;   // gamepad tap: (düğme, bırakılacağı kare)

            // wait_until <ms> <komut...>: iç komut başarılı olana ya da süre dolana kadar her karede yeniden denenir
            bool RetryActive = false;
            bool Retrying = false;            // wait_until denemesi sürerken ara raporlar yutulur
            std::string RetryLastReport;      // son denemenin raporu (sonuçla birlikte yazılır)
            ScriptLine RetryLine;
            int RetryOuterNumber = 0;
            std::chrono::steady_clock::time_point RetryDeadline{};
            std::string RetryLastError;
        };

        AutomationState& State()
        {
            static AutomationState s;
            return s;
        }

        std::vector<std::string> Tokenize(const std::string& line)
        {
            std::vector<std::string> tokens;
            std::string cur;
            bool inQuotes = false;
            bool hasToken = false;
            for (size_t i = 0; i < line.size(); ++i)
            {
                char c = line[i];
                if (inQuotes)
                {
                    if (c == '\\' && i + 1 < line.size() && (line[i + 1] == '"' || line[i + 1] == '\\'))
                        cur += line[++i];
                    else if (c == '"')
                        inQuotes = false;
                    else
                        cur += c;
                }
                else if (c == '"')
                {
                    inQuotes = true;
                    hasToken = true;
                }
                else if (c == '#' && !hasToken && cur.empty())
                {
                    break;
                }
                else if (c == ' ' || c == '\t' || c == '\r')
                {
                    if (hasToken || !cur.empty())
                        tokens.push_back(cur);
                    cur.clear();
                    hasToken = false;
                }
                else
                {
                    cur += c;
                    hasToken = true;
                }
            }
            if (hasToken || !cur.empty())
                tokens.push_back(cur);
            return tokens;
        }

        std::string ToLower(std::string s)
        {
            std::transform(s.begin(), s.end(), s.begin(), [](char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; });
            return s;
        }

        ImGuiKey KeyFromName(const std::string& rawName)
        {
            std::string name = ToLower(rawName);
            static const std::map<std::string, ImGuiKey> aliases = {
                { "ctrl", ImGuiKey_LeftCtrl }, { "control", ImGuiKey_LeftCtrl },
                { "shift", ImGuiKey_LeftShift }, { "alt", ImGuiKey_LeftAlt },
                { "super", ImGuiKey_LeftSuper }, { "esc", ImGuiKey_Escape },
                { "del", ImGuiKey_Delete }, { "return", ImGuiKey_Enter },
                { "pgup", ImGuiKey_PageUp }, { "pgdn", ImGuiKey_PageDown },
            };
            if (auto it = aliases.find(name); it != aliases.end())
                return it->second;
            for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END; ++k)
            {
                const char* keyName = ImGui::GetKeyName((ImGuiKey)k);
                if (keyName && ToLower(keyName) == name)
                    return (ImGuiKey)k;
            }
            return ImGuiKey_None;
        }

        ImGuiKey ModKeyFor(ImGuiKey key)
        {
            switch (key)
            {
                case ImGuiKey_LeftCtrl: case ImGuiKey_RightCtrl: return ImGuiMod_Ctrl;
                case ImGuiKey_LeftShift: case ImGuiKey_RightShift: return ImGuiMod_Shift;
                case ImGuiKey_LeftAlt: case ImGuiKey_RightAlt: return ImGuiMod_Alt;
                case ImGuiKey_LeftSuper: case ImGuiKey_RightSuper: return ImGuiMod_Super;
                default: return ImGuiKey_None;
            }
        }

        ImVec2 MainViewportOrigin()
        {
            if (ImGui::GetCurrentContext())
                return ImGui::GetMainViewport()->Pos;
            return ImVec2(0, 0);
        }

        int ParseButton(const std::string& s)
        {
            std::string b = ToLower(s);
            if (b == "right" || b == "r" || b == "1") return 1;
            if (b == "middle" || b == "m" || b == "2") return 2;
            return 0;
        }

        void QueueMove(float x, float y)
        {
            InputStep s; s.Type = InputStepType::MousePos; s.X = x; s.Y = y;
            State().InputQueue.push_back(s);
        }

        void QueueButton(int button, bool down)
        {
            InputStep s; s.Type = InputStepType::MouseButton; s.Button = button; s.Down = down;
            State().InputQueue.push_back(s);
        }

        void QueueIdle(int frames = 1)
        {
            for (int i = 0; i < frames; ++i)
                State().InputQueue.push_back(InputStep{});
        }

        void QueueClick(float x, float y, int button, int clicks)
        {
            QueueMove(x, y);
            QueueIdle();
            for (int i = 0; i < clicks; ++i)
            {
                QueueButton(button, true);
                QueueButton(button, false);
            }
            QueueIdle();
        }

        void QueueDrag(float x0, float y0, float x1, float y1, int steps, int button)
        {
            QueueMove(x0, y0);
            QueueIdle();
            QueueButton(button, true);
            steps = std::max(1, steps);
            for (int i = 1; i <= steps; ++i)
            {
                float t = (float)i / (float)steps;
                QueueMove(x0 + (x1 - x0) * t, y0 + (y1 - y0) * t);
            }
            QueueIdle();
            QueueButton(button, false);
            QueueIdle();
        }

        bool QueueKeyCombo(const std::string& combo, std::string& error)
        {
            std::vector<ImGuiKey> keys;
            std::stringstream ss(combo);
            std::string part;
            while (std::getline(ss, part, '+'))
            {
                if (part.empty())
                    continue;
                ImGuiKey k = KeyFromName(part);
                if (k == ImGuiKey_None)
                {
                    error = "unknown key '" + part + "'";
                    return false;
                }
                keys.push_back(k);
            }
            if (keys.empty())
            {
                error = "empty key combo";
                return false;
            }
            for (ImGuiKey k : keys)
            {
                InputStep s; s.Type = InputStepType::Key; s.Key = k; s.Down = true;
                State().InputQueue.push_back(s);
            }
            QueueIdle();
            for (auto it = keys.rbegin(); it != keys.rend(); ++it)
            {
                InputStep s; s.Type = InputStepType::Key; s.Key = *it; s.Down = false;
                State().InputQueue.push_back(s);
            }
            QueueIdle();
            return true;
        }

        const CQAutomation::ItemRecord* FindItemArg(const Automation::CommandArgs& args, size_t labelIndex, std::string& error)
        {
            if (args.size() <= labelIndex)
            {
                error = "missing label";
                return nullptr;
            }
            const char* window = args.size() > labelIndex + 1 ? args[labelIndex + 1].c_str() : nullptr;
            int occurrence = args.size() > labelIndex + 2 ? std::atoi(args[labelIndex + 2].c_str()) : 0;
            const auto* item = CQAutomation::FindItem(args[labelIndex].c_str(), window, occurrence);
            if (!item)
                error = "item '" + args[labelIndex] + "' not found" + (window ? std::string(" in window '") + window + "'" : std::string());
            return item;
        }

        void DumpItems(const std::string& path)
        {
            std::ofstream out(path);
            for (int i = 0; i < CQAutomation::GetItemCount(); ++i)
            {
                const auto* r = CQAutomation::GetItem(i);
                out << "[" << r->Window << "] \"" << r->Label << "\" (" << r->Min.x << "," << r->Min.y << ")-(" << r->Max.x << "," << r->Max.y << ")\n";
            }
        }

        bool RunBuiltin(const ScriptLine& line, bool& handled, std::string& error)
        {
            auto& st = State();
            const auto& t = line.Tokens;
            const std::string cmd = ToLower(t[0]);
            handled = true;

            auto argf = [&](size_t i, float def = 0.0f) { return t.size() > i ? (float)std::atof(t[i].c_str()) : def; };
            ImVec2 origin = MainViewportOrigin();

            if (cmd == "wait")
            {
                st.WaitFrames = std::max(1, (int)argf(1, 1));
                return true;
            }
            if (cmd == "wait_until")
            {
                if (t.size() < 3)
                {
                    error = "wait_until <timeoutMs> <command ...>";
                    return false;
                }
                st.RetryActive = true;
                st.RetryLine.Number = line.Number;
                st.RetryLine.Tokens.assign(t.begin() + 2, t.end());
                st.RetryOuterNumber = line.Number;
                st.RetryDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds((int)argf(1, 1000));
                st.RetryLastError.clear();
                handled = true;
                // Sonuç (OK/FAIL) koşul çözülünce yazılır
                return true;
            }
            if (cmd == "sleep")
            {
                st.SleepUntil = std::chrono::steady_clock::now() + std::chrono::milliseconds((int)argf(1, 100));
                return true;
            }
            if (cmd == "screenshot")
            {
                if (t.size() < 2) { error = "screenshot needs a path"; return false; }
                st.PendingScreenshot = t[1];
                return true;
            }
            if (cmd == "quit" || cmd == "exit")
            {
                st.QuitRequested = true;
                return true;
            }
            if (cmd == "crash_test")
            {
                // crash_test <segv|abort|fpe|throw>: çökme raporlayıcısını doğrula (süreç sonlanır)
                Automation::Report("CRASH_TEST " + (t.size() > 1 ? t[1] : std::string("segv")));
                CrashHandler::TriggerTestCrash(t.size() > 1 ? t[1] : std::string("segv"));
            }
            if (cmd == "log")
            {
                std::string msg;
                for (size_t i = 1; i < t.size(); ++i) msg += (i > 1 ? " " : "") + t[i];
                Automation::Report("LOG " + msg);
                return true;
            }
            if (cmd == "move")
            {
                QueueMove(origin.x + argf(1), origin.y + argf(2));
                QueueIdle();
                return true;
            }
            if (cmd == "click" || cmd == "dclick" || cmd == "rclick")
            {
                int button = cmd == "rclick" ? 1 : (t.size() > 3 ? ParseButton(t[3]) : 0);
                QueueClick(origin.x + argf(1), origin.y + argf(2), button, cmd == "dclick" ? 2 : 1);
                return true;
            }
            if (cmd == "drag")
            {
                QueueDrag(origin.x + argf(1), origin.y + argf(2), origin.x + argf(3), origin.y + argf(4), (int)argf(5, 12), 0);
                return true;
            }
            if (cmd == "rdrag" || cmd == "mdrag")
            {
                QueueDrag(origin.x + argf(1), origin.y + argf(2), origin.x + argf(3), origin.y + argf(4), (int)argf(5, 12), cmd == "rdrag" ? 1 : 2);
                return true;
            }
            if (cmd == "scroll")
            {
                QueueMove(origin.x + argf(1), origin.y + argf(2));
                QueueIdle();
                InputStep s; s.Type = InputStepType::MouseWheel; s.Y = argf(3, 1);
                st.InputQueue.push_back(s);
                QueueIdle();
                return true;
            }
            if (cmd == "click_item" || cmd == "dclick_item" || cmd == "rclick_item" || cmd == "hover_item")
            {
                const auto* item = FindItemArg(t, 1, error);
                if (!item) return false;
                float cx = (item->Min.x + item->Max.x) * 0.5f;
                float cy = (item->Min.y + item->Max.y) * 0.5f;
                if (cmd == "hover_item")
                {
                    QueueMove(cx, cy);
                    QueueIdle(2);
                }
                else
                {
                    QueueClick(cx, cy, cmd == "rclick_item" ? 1 : 0, cmd == "dclick_item" ? 2 : 1);
                }
                return true;
            }
            if (cmd == "click_item_offset")
            {
                // click_item_offset <label> <dx> <dy> [window] [kaçıncı eşleşme]: öğenin ortasından (dx, dy) uzağa tıkla
                if (t.size() < 4) { error = "click_item_offset <label> <dx> <dy> [window] [occurrence]"; return false; }
                const char* window = t.size() > 4 && t[4] != "-" ? t[4].c_str() : nullptr;
                const int occurrence = t.size() > 5 ? std::atoi(t[5].c_str()) : 0;
                const auto* item = CQAutomation::FindItem(t[1].c_str(), window, occurrence);
                if (!item) { error = "item '" + t[1] + "' not found"; return false; }
                QueueClick((item->Min.x + item->Max.x) * 0.5f + argf(2), (item->Min.y + item->Max.y) * 0.5f + argf(3), 0, 1);
                return true;
            }
            if (cmd == "drag_item")
            {
                // drag_item <label> <dx> <dy> [window] [kaçıncı eşleşme]
                if (t.size() < 4) { error = "drag_item <label> <dx> <dy> [window] [occurrence]"; return false; }
                const char* window = t.size() > 4 && t[4] != "-" ? t[4].c_str() : nullptr;
                const int occurrence = t.size() > 5 ? std::atoi(t[5].c_str()) : 0;
                const auto* item = CQAutomation::FindItem(t[1].c_str(), window, occurrence);
                if (!item) { error = "item '" + t[1] + "' not found"; return false; }
                float cx = (item->Min.x + item->Max.x) * 0.5f;
                float cy = (item->Min.y + item->Max.y) * 0.5f;
                QueueDrag(cx, cy, cx + argf(2), cy + argf(3), 16, 0);
                return true;
            }
            if (cmd == "expect_item")
            {
                return FindItemArg(t, 1, error) != nullptr;
            }
            if (cmd == "expect_item_size")
            {
                // expect_item_size <label> <min genişlik> <min yükseklik> [window]: pencere / öğe en az bu boyutta
                if (t.size() < 4) { error = "expect_item_size <label> <minW> <minH> [window]"; return false; }
                const auto* item = CQAutomation::FindItem(t[1].c_str(), t.size() > 4 ? t[4].c_str() : nullptr, 0);
                if (!item) { error = "item '" + t[1] + "' not found"; return false; }
                const float w = item->Max.x - item->Min.x, h = item->Max.y - item->Min.y;
                Automation::Report("ITEM_SIZE " + t[1] + " " + std::to_string((int)w) + "x" + std::to_string((int)h));
                if (w < argf(2) || h < argf(3))
                {
                    error = "item '" + t[1] + "' is " + std::to_string((int)w) + "x" + std::to_string((int)h);
                    return false;
                }
                return true;
            }
            if (cmd == "expect_no_item")
            {
                std::string dummy;
                if (FindItemArg(t, 1, dummy))
                {
                    error = "item '" + t[1] + "' unexpectedly present";
                    return false;
                }
                return true;
            }
            if (cmd == "key_down" || cmd == "key_up")
            {
                // Tuşu basılı tut / bırak (oyun girdisi testleri: yürüme, koşma)
                if (t.size() < 2) { error = cmd + " needs a key name"; return false; }
                ImGuiKey k = KeyFromName(t[1]);
                if (k == ImGuiKey_None) { error = "unknown key '" + t[1] + "'"; return false; }
                InputStep s; s.Type = InputStepType::Key; s.Key = k; s.Down = cmd == "key_down";
                st.InputQueue.push_back(s);
                QueueIdle();
                return true;
            }
            if (cmd == "key")
            {
                if (t.size() < 2) { error = "key needs a key name"; return false; }
                std::string combo = t[1];
                for (size_t i = 2; i < t.size(); ++i) combo = t[i] + "+" + combo; // "key s ctrl" == "key ctrl+s"
                return QueueKeyCombo(combo, error);
            }
            if (cmd == "type")
            {
                std::string text;
                for (size_t i = 1; i < t.size(); ++i) text += (i > 1 ? " " : "") + t[i];
                InputStep s; s.Type = InputStepType::Text; s.Text = text;
                st.InputQueue.push_back(s);
                QueueIdle();
                return true;
            }
            if (cmd == "gamepad")
            {
                // gamepad connect [ad...] | disconnect | down <düğme> | up <düğme> | tap <düğme> | axis <eksen> <değer>
                // düğme: a b x y lb rb back start guide ls rs up right down left (ya da 0..14); eksen: lx ly rx ry lt rt (ya da 0..5)
                if (t.size() < 2) { error = "gamepad connect|disconnect|down|up|tap|axis ..."; return false; }
                auto button = [](const std::string& n) {
                    static const char* kNames[] = { "a", "b", "x", "y", "lb", "rb", "back", "start", "guide", "ls", "rs", "up", "right", "down", "left" };
                    for (int i = 0; i < 15; ++i)
                        if (n == kNames[i]) return i;
                    return std::isdigit((unsigned char)n[0]) ? std::atoi(n.c_str()) : -1;
                };
                const std::string& sub = t[1];
                if (sub == "connect")
                {
                    std::string name;
                    for (size_t i = 2; i < t.size(); ++i) name += (i > 2 ? " " : "") + t[i];
                    Input::SetVirtualGamepad(true, name.empty() ? std::string("Xbox Controller") : name);
                    return true;
                }
                if (sub == "disconnect") { Input::SetVirtualGamepad(false); return true; }
                if ((sub == "down" || sub == "up" || sub == "tap") && t.size() > 2)
                {
                    const int b = button(t[2]);
                    if (b < 0 || b > 14) { error = "unknown gamepad button '" + t[2] + "'"; return false; }
                    Input::SetVirtualGamepadButton(b, sub != "up");
                    if (sub == "tap")
                        st.PadReleases.push_back({ b, st.Frame + 3 });
                    return true;
                }
                if (sub == "axis" && t.size() > 3)
                {
                    static const char* kAxes[] = { "lx", "ly", "rx", "ry", "lt", "rt" };
                    int a = -1;
                    for (int i = 0; i < 6; ++i)
                        if (t[2] == kAxes[i]) a = i;
                    if (a < 0 && std::isdigit((unsigned char)t[2][0])) a = std::atoi(t[2].c_str());
                    if (a < 0 || a > 5) { error = "unknown gamepad axis '" + t[2] + "'"; return false; }
                    Input::SetVirtualGamepadAxis(a, (float)std::atof(t[3].c_str()));
                    return true;
                }
                error = "unknown gamepad command " + sub;
                return false;
            }
            if (cmd == "dump_items")
            {
                DumpItems(t.size() > 1 ? t[1] : std::string("automation_items.txt"));
                return true;
            }
            if (cmd == "timeout")
            {
                st.TimeoutSeconds = argf(1, 300.0f);
                return true;
            }

            handled = false;
            return false;
        }

        void ExecuteLine(const ScriptLine& line)
        {
            auto& st = State();
            std::string error;
            bool handled = false;
            bool ok = RunBuiltin(line, handled, error);
            if (!handled)
            {
                std::string name = ToLower(line.Tokens[0]);
                auto it = st.Commands.find(name);
                if (it == st.Commands.end())
                {
                    ok = false;
                    error = "unknown command '" + line.Tokens[0] + "'";
                }
                else
                {
                    try
                    {
                        ok = it->second(line.Tokens, error);
                    }
                    catch (const std::exception& e)
                    {
                        ok = false;
                        error = std::string("exception: ") + e.what();
                    }
                }
            }

            std::string joined;
            for (const auto& tok : line.Tokens) joined += (joined.empty() ? "" : " ") + tok;
            if (ok && st.RetryActive && ToLower(line.Tokens[0]) == "wait_until")
                return; // sonuç koşul çözülünce raporlanır
            if (ok)
            {
                Automation::Report("OK   " + std::to_string(line.Number) + ": " + joined);
            }
            else
            {
                ++st.Failures;
                Automation::Report("FAIL " + std::to_string(line.Number) + ": " + joined + " -> " + error);
            }
        }
    }

    void Automation::Init(int argc, char** argv)
    {
        auto& st = State();
        std::string script;
        if (const char* env = std::getenv("CQ_AUTOMATION"))
            script = env;
        for (int i = 1; i < argc && argv; ++i)
        {
            std::string a = argv[i] ? argv[i] : "";
            if (a.rfind("--automation=", 0) == 0)
                script = a.substr(13);
        }
        if (script.empty())
            return;

        std::ifstream in(script);
        if (!in)
        {
            CQ_CORE_ERROR("[Automation] Script not found: {0}", script);
            return;
        }

        st.ScriptPath = script;
        std::string raw;
        int number = 0;
        while (std::getline(in, raw))
        {
            ++number;
            auto tokens = Tokenize(raw);
            if (!tokens.empty())
                st.Lines.push_back({ number, std::move(tokens) });
        }

        std::string resultPath = script + ".result.txt";
        if (const char* env = std::getenv("CQ_AUTOMATION_LOG"))
            resultPath = env;
        st.ResultLog.open(resultPath, std::ios::trunc);

        st.Active = true;
        st.StartTime = std::chrono::steady_clock::now();
        CQAutomation::EnableItemHooks(true);
        CQ_CORE_INFO("[Automation] Running script {0} ({1} commands)", script, st.Lines.size());
    }

    void Automation::Shutdown()
    {
        auto& st = State();
        if (!st.Active)
            return;
        Report("DONE failures=" + std::to_string(st.Failures));
        st.ResultLog.close();
        st.Active = false;
    }

    bool Automation::IsActive() { return State().Active; }
    bool Automation::WantsQuit() { return State().QuitRequested; }

    void Automation::RegisterCommand(const std::string& name, CommandHandler handler)
    {
        State().Commands[ToLower(name)] = std::move(handler);
    }

    void Automation::UnregisterCommand(const std::string& name)
    {
        State().Commands.erase(ToLower(name));
    }

    bool Automation::ExecuteCommandLine(const std::string& line, std::string& error)
    {
        auto tokens = Tokenize(line);
        if (tokens.empty())
        {
            error = "empty command";
            return false;
        }
        // Önce yerleşik komutlar (click, key, type ...) — kayıtlı komutlar bunları birleştirebilsin
        {
            ScriptLine builtin;
            builtin.Tokens = tokens;
            bool handled = false;
            bool ok = RunBuiltin(builtin, handled, error);
            if (handled)
                return ok;
            error.clear();
        }
        auto& st = State();
        auto it = st.Commands.find(ToLower(tokens[0]));
        if (it == st.Commands.end())
        {
            error = "unknown command '" + tokens[0] + "'";
            return false;
        }
        try
        {
            return it->second(tokens, error);
        }
        catch (const std::exception& e)
        {
            error = e.what();
            return false;
        }
    }

    std::vector<std::string> Automation::GetCommandNames()
    {
        std::vector<std::string> names;
        for (const auto& [name, handler] : State().Commands)
            names.push_back(name);
        return names;
    }

    void Automation::WaitFrames(int frames)
    {
        State().WaitFrames = std::max(State().WaitFrames, frames);
    }

    void Automation::QueueMouseMove(float x, float y)
    {
        QueueMove(x, y);
        QueueIdle(2);
    }

    void Automation::QueueMouseClick(float x, float y, int button)
    {
        QueueClick(x, y, button, 1);
    }

    void Automation::QueueMouseDrag(float x0, float y0, float x1, float y1, int steps, int button)
    {
        QueueDrag(x0, y0, x1, y1, steps, button);
    }

    void Automation::Report(const std::string& line)
    {
        auto& st = State();
        if (st.Retrying)
        {
            // wait_until her karede komutu yeniden dener: yalnızca son denemenin raporu saklanır
            st.RetryLastReport = line;
            return;
        }
        if (st.ResultLog.is_open())
        {
            st.ResultLog << "[frame " << st.Frame << "] " << line << "\n";
            st.ResultLog.flush();
        }
        CQ_CORE_INFO("[Automation] {0}", line);
    }

    void Automation::OnFrameBegin()
    {
        auto& st = State();
        if (!st.Active)
            return;
        ++st.Frame;
        // gamepad tap: birkaç kare sonra bırak
        for (auto it = st.PadReleases.begin(); it != st.PadReleases.end();)
        {
            if (st.Frame >= it->second)
            {
                Input::SetVirtualGamepadButton(it->first, false);
                it = st.PadReleases.erase(it);
            }
            else
                ++it;
        }

        double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - st.StartTime).count();
        if (elapsed > st.TimeoutSeconds)
        {
            Report("FAIL timeout after " + std::to_string((int)elapsed) + "s");
            ++st.Failures;
            st.QuitRequested = true;
            return;
        }

        // A screenshot is taken at the end of this frame; input steps play one per frame.
        if (!st.PendingScreenshot.empty() || !st.InputQueue.empty())
            return;
        if (st.WaitFrames > 0)
        {
            --st.WaitFrames;
            return;
        }
        if (std::chrono::steady_clock::now() < st.SleepUntil)
            return;

        // wait_until: iç komutu sessizce dene
        if (st.RetryActive)
        {
            std::string error;
            bool ok = false;
            const std::string name = ToLower(st.RetryLine.Tokens[0]);
            auto it = st.Commands.find(name);
            if (it == st.Commands.end())
            {
                error = "unknown command '" + st.RetryLine.Tokens[0] + "'";
            }
            else
            {
                st.Retrying = true;
                try { ok = it->second(st.RetryLine.Tokens, error); }
                catch (const std::exception& e) { ok = false; error = std::string("exception: ") + e.what(); }
                st.Retrying = false;
            }
            std::string joined = "wait_until";
            for (const auto& tok : st.RetryLine.Tokens) joined += " " + tok;
            const bool timedOut = !ok && std::chrono::steady_clock::now() >= st.RetryDeadline;
            if ((ok || timedOut) && !st.RetryLastReport.empty())
            {
                Report(st.RetryLastReport);
                st.RetryLastReport.clear();
            }
            if (ok)
            {
                st.RetryActive = false;
                Report("OK   " + std::to_string(st.RetryOuterNumber) + ": " + joined);
            }
            else if (timedOut)
            {
                st.RetryActive = false;
                ++st.Failures;
                Report("FAIL " + std::to_string(st.RetryOuterNumber) + ": " + joined + " -> timed out: " + error);
            }
            // Komut kare beklemesi istemiş olabilir; her durumda bu kare bitti
            if (st.RetryActive || st.WaitFrames > 0 || !st.InputQueue.empty() || !st.PendingScreenshot.empty())
                return;
        }

        // Run commands until one needs frames to complete.
        while (st.Cursor < st.Lines.size() && !st.QuitRequested)
        {
            ExecuteLine(st.Lines[st.Cursor++]);
            if (st.WaitFrames > 0 || !st.InputQueue.empty() || !st.PendingScreenshot.empty()
                || std::chrono::steady_clock::now() < st.SleepUntil || st.RetryActive)
                break;
        }

        if (st.Cursor >= st.Lines.size() && st.InputQueue.empty() && st.PendingScreenshot.empty() && st.WaitFrames == 0 && !st.RetryActive)
            st.QuitRequested = true;
    }

    void Automation::BeforeImGuiNewFrame()
    {
        if (State().Active)
            CQAutomation::BeginFrame();
    }

    void Automation::InjectImGuiInput()
    {
        auto& st = State();
        if (!st.Active || !ImGui::GetCurrentContext())
            return;
        ImGuiIO& io = ImGui::GetIO();

        // Keep our virtual cursor authoritative (the platform backend may report
        // the real cursor position every frame).
        if (st.HasMousePos)
            io.AddMousePosEvent(st.MousePos.x, st.MousePos.y);

        if (st.InputQueue.empty())
            return;

        InputStep step = st.InputQueue.front();
        st.InputQueue.pop_front();
        switch (step.Type)
        {
            case InputStepType::MousePos:
                st.MousePos = ImVec2(step.X, step.Y);
                st.HasMousePos = true;
                io.AddMousePosEvent(step.X, step.Y);
                break;
            case InputStepType::MouseButton:
                io.AddMouseButtonEvent(step.Button, step.Down);
                break;
            case InputStepType::MouseWheel:
                io.AddMouseWheelEvent(0.0f, step.Y);
                Input::AddMouseScrollDelta(step.Y);   // gerçek tekerlek gibi motorun Input'una da (kamera yakınlaştırma)
                break;
            case InputStepType::Key:
                if (ImGuiKey mod = ModKeyFor(step.Key); mod != ImGuiKey_None)
                    io.AddKeyEvent(mod, step.Down);
                io.AddKeyEvent(step.Key, step.Down);
                break;
            case InputStepType::Text:
                io.AddInputCharactersUTF8(step.Text.c_str());
                break;
            case InputStepType::Idle:
                break;
        }
    }

    bool Automation::CaptureBackbuffer(const std::string& path, uint32_t width, uint32_t height)
    {
        if (width == 0 || height == 0)
            return false;
        std::vector<unsigned char> pixels((size_t)width * height * 4);
        GLint prevReadFbo = 0;
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevReadFbo);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
        glReadBuffer(GL_BACK);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, (GLsizei)width, (GLsizei)height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        glBindFramebuffer(GL_READ_FRAMEBUFFER, prevReadFbo);

        // OpenGL rows are bottom-up; force opaque alpha for viewers.
        std::vector<unsigned char> flipped(pixels.size());
        const size_t stride = (size_t)width * 4;
        for (uint32_t y = 0; y < height; ++y)
            memcpy(&flipped[(size_t)y * stride], &pixels[(size_t)(height - 1 - y) * stride], stride);
        for (size_t i = 3; i < flipped.size(); i += 4)
            flipped[i] = 255;

        std::error_code ec;
        std::filesystem::path p(path);
        if (p.has_parent_path())
            std::filesystem::create_directories(p.parent_path(), ec);
        return stbi_write_png(path.c_str(), (int)width, (int)height, 4, flipped.data(), (int)stride) != 0;
    }

    void Automation::OnFrameRendered(uint32_t framebufferWidth, uint32_t framebufferHeight)
    {
        auto& st = State();
        if (!st.Active || st.PendingScreenshot.empty())
            return;
        bool ok = CaptureBackbuffer(st.PendingScreenshot, framebufferWidth, framebufferHeight);
        Report(std::string(ok ? "SHOT " : "FAIL screenshot ") + st.PendingScreenshot);
        if (!ok)
            ++st.Failures;
        st.PendingScreenshot.clear();
    }
}
