#include "VisualGraph.h"
#include "Core/Utils/JSON.h"
#include "Scripting/NativeDocs.h"
#include "Scripting/NativeRegistry.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <fstream>
#include <functional>
#include <set>
#include <sstream>
#include <unordered_set>

namespace Conqueror::Visual
{
    // ════════════════════════════════════════
    //  Pin types
    // ════════════════════════════════════════
    const char* PinTypeName(PinType t)
    {
        switch (t)
        {
            case PinType::Exec: return "exec";
            case PinType::Any: return "any";
            case PinType::Number: return "number";
            case PinType::Bool: return "bool";
            case PinType::String: return "string";
            case PinType::Vector: return "vector";
            case PinType::List: return "list";
        }
        return "any";
    }

    PinType PinTypeFromName(const std::string& n)
    {
        if (n == "exec") return PinType::Exec;
        if (n == "number" || n == "float" || n == "int") return PinType::Number;
        if (n == "bool") return PinType::Bool;
        if (n == "string") return PinType::String;
        if (n == "vector") return PinType::Vector;
        if (n == "list") return PinType::List;
        return PinType::Any;
    }

    bool PinsCompatible(PinType from, PinType to)
    {
        if (from == PinType::Exec || to == PinType::Exec)
            return from == to;
        if (from == to || from == PinType::Any || to == PinType::Any)
            return true;
        // Sayı ve mantık birbirine, her şey metne dönüşebilir
        if (to == PinType::String) return true;
        if ((from == PinType::Number && to == PinType::Bool) || (from == PinType::Bool && to == PinType::Number)) return true;
        return false;
    }

    std::string SanitizeIdentifier(const std::string& name)
    {
        std::string out;
        for (char c : name)
            out += (std::isalnum((unsigned char)c) || c == '_') ? c : '_';
        if (out.empty() || std::isdigit((unsigned char)out[0]))
            out = "_" + out;
        return out;
    }

    // ════════════════════════════════════════
    //  Node library
    // ════════════════════════════════════════
    namespace
    {
        PinDef In(const std::string& n, PinType t, const std::string& def = "") { return { n, t, def }; }
        PinDef Out(const std::string& n, PinType t, const std::string& expr = "") { return { n, t, expr }; }
        PinDef ExecIn() { return { "In", PinType::Exec, "" }; }
        PinDef ExecOut(const std::string& n = "Then") { return { n, PinType::Exec, "" }; }

        NodeDef Make(const std::string& id, const std::string& title, const std::string& category, NodeKind kind,
                     std::vector<PinDef> inputs, std::vector<PinDef> outputs, const std::string& expr = "", const std::string& desc = "")
        {
            NodeDef d;
            d.Id = id;
            d.Title = title;
            d.Category = category;
            d.Kind = kind;
            d.Inputs = std::move(inputs);
            d.Outputs = std::move(outputs);
            d.Expression = expr;
            d.Description = desc;
            return d;
        }

        bool StartsWithAny(const std::string& s, std::initializer_list<const char*> prefixes)
        {
            for (const char* p : prefixes)
                if (s.rfind(p, 0) == 0) return true;
            return false;
        }

        std::vector<NodeDef> BuildLibrary()
        {
            std::vector<NodeDef> lib;
            auto add = [&](NodeDef d) { lib.push_back(std::move(d)); };
            const NodeKind E = NodeKind::Event, F = NodeKind::Flow, P = NodeKind::Pure, A = NodeKind::Action;
            const PinType N = PinType::Number, B = PinType::Bool, S = PinType::String, V = PinType::Vector, X = PinType::Any, L = PinType::List;

            // ── Olaylar ──
            add(Make("event.start", "On Start", "Events", E, {}, { ExecOut() }, "", "Called once before the first frame."));
            add(Make("event.awake", "On Awake", "Events", E, {}, { ExecOut() }, "", "Called when the script instance is created."));
            add(Make("event.update", "On Update", "Events", E, {}, { ExecOut(), Out("Delta Time", N) }, "", "Called every frame."));
            add(Make("event.lateupdate", "On Late Update", "Events", E, {}, { ExecOut(), Out("Delta Time", N) }, "", "Called every frame after all updates."));
            add(Make("event.destroy", "On Destroy", "Events", E, {}, { ExecOut() }, "", "Called when play stops or the entity is destroyed."));
            add(Make("event.enterstate", "On Enter State", "Events", E, {}, { ExecOut() }, "", "State Graph: called each time this graph's state becomes active."));
            add(Make("event.exitstate", "On Exit State", "Events", E, {}, { ExecOut() }, "", "State Graph: called each time this graph's state is left."));
            {
                NodeDef d = Make("event.custom", "Custom Event", "Events", E, {}, { ExecOut(), Out("Value", X), Out("Kind", S) }, "",
                                 "Callable by name: UI buttons (OnClick), other scripts.");
                d.Properties = { { "Name", "OnClick" } };
                add(d);
            }
            {
                NodeDef d = Make("event.keydown", "On Key Pressed", "Events", E, {}, { ExecOut() }, "", "Fires on the frame a key goes down.");
                d.Properties = { { "Key", "Space" } };
                add(d);
            }
            {
                NodeDef d = Make("event.keyheld", "While Key Held", "Events", E, {}, { ExecOut(), Out("Delta Time", N) }, "", "Fires every frame while the key is held.");
                d.Properties = { { "Key", "W" } };
                add(d);
            }
            {
                NodeDef d = Make("event.mouse", "On Mouse Click", "Events", E, {}, { ExecOut() }, "", "Fires when a mouse button goes down (0 left, 1 right, 2 middle).");
                d.Properties = { { "Button", "0" } };
                add(d);
            }
            {
                NodeDef d = Make("event.timer", "Every N Seconds", "Events", E, {}, { ExecOut() }, "", "Fires repeatedly at a fixed interval.");
                d.Properties = { { "Interval", "1.0" } };
                add(d);
            }
            {
                NodeDef d = Make("event.collision", "On Collision", "Events", E, {}, { ExecOut(), Out("Other", X), Out("Other Name", S), Out("Normal", V), Out("Impulse", N) }, "",
                                 "A physics contact with another collider (Unreal Event Hit). Phase: Enter, Stay or Exit; Physics: 3D or 2D.");
                d.Properties = { { "Phase", "Enter" }, { "Physics", "3D" } };
                add(d);
            }
            {
                NodeDef d = Make("event.trigger", "On Trigger", "Events", E, {}, { ExecOut(), Out("Other", X), Out("Other Name", S) }, "",
                                 "Another collider entered, stayed in or left this trigger (Unreal Begin / End Overlap).");
                d.Properties = { { "Phase", "Enter" }, { "Physics", "3D" } };
                add(d);
            }
            {
                NodeDef d = Make("event.inputaction", "On Input Action", "Events", E, {}, { ExecOut(), Out("Value", X) }, "",
                                 "Input action from Project Settings > Input (Unreal Enhanced Input event): Started, Performed (Triggered) or Canceled (Completed).");
                d.Properties = { { "Action", "Jump" }, { "Phase", "Performed" } };
                add(d);
            }

            // ── Fonksiyonlar ──
            {
                NodeDef d = Make("event.function", "Function", "Functions", E, {}, { ExecOut() }, "",
                                 "Entry of a function with inputs (comma separated) and an optional Return Value; call it with Call Function, or from other scripts with InvokeMethod.");
                d.Properties = { { "Name", "MyFunction" }, { "Inputs", "A, B" } };
                add(d);
            }
            add(Make("flow.returnvalue", "Return Value", "Functions", F, { ExecIn(), In("Value", X) }, {}, "", "Ends the function and returns the value to the caller."));

            // ── Makrolar (Unreal Blueprint Macro): gövde her kullanımda satır içi açılır ──
            {
                NodeDef d = Make("macro.inputs", "Inputs", "Macros", F, {}, {}, "", "Macro entry: its pins (Pins, e.g. \"In:exec, Speed:number\") are the macro node's inputs.");
                d.Properties = { { "Pins", "In:exec" } };
                add(d);
            }
            {
                NodeDef d = Make("macro.outputs", "Outputs", "Macros", F, {}, {}, "", "Macro exit: its pins (Pins, e.g. \"Out:exec, Result:number\") are the macro node's outputs.");
                d.Properties = { { "Pins", "Out:exec" } };
                add(d);
            }
            {
                NodeDef d = Make("macro.instance", "Macro", "Macros", F, {}, {}, "", "Runs a macro of this graph: its body is placed here inline (several exec inputs and outputs).");
                d.Properties = { { "Macro", "" } };
                add(d);
            }
            {
                NodeDef d = Make("action.callfunction", "Call Function", "Functions", A, { ExecIn() }, { ExecOut(), Out("Result", X) }, "", "Calls a function of this graph and gives its return value.");
                d.Properties = { { "Function", "MyFunction" } };
                add(d);
            }

            // ── Akış ──
            add(Make("flow.branch", "Branch", "Flow", F, { ExecIn(), In("Condition", B, "true") }, { ExecOut("True"), ExecOut("False") }, "", "If / else."));
            add(Make("flow.sequence", "Sequence", "Flow", F, { ExecIn() }, { ExecOut("Then 0"), ExecOut("Then 1"), ExecOut("Then 2"), ExecOut("Then 3") }, "", "Runs outputs in order."));
            add(Make("flow.for", "For Loop", "Flow", F, { ExecIn(), In("First", N, "0"), In("Last", N, "9") }, { ExecOut("Body"), Out("Index", N), ExecOut("Completed") }, "", "Index goes from First to Last (inclusive)."));
            add(Make("flow.foreach", "For Each", "Flow", F, { ExecIn(), In("List", L) }, { ExecOut("Body"), Out("Element", X), Out("Index", N), ExecOut("Completed") }, "", "Loops over a list (or map keys, string characters, range)."));
            add(Make("flow.while", "While Loop", "Flow", F, { ExecIn(), In("Condition", B, "false") }, { ExecOut("Body"), ExecOut("Completed") }, "", "Repeats while the condition is true."));
            add(Make("flow.doonce", "Do Once", "Flow", F, { ExecIn(), { "Reset", PinType::Exec, "" } }, { ExecOut() }, "", "Passes through only the first time (until Reset)."));
            add(Make("flow.flipflop", "Flip Flop", "Flow", F, { ExecIn() }, { ExecOut("A"), ExecOut("B"), Out("Is A", B) }, "", "Alternates between A and B."));
            add(Make("flow.return", "Return", "Flow", F, { ExecIn() }, {}, "", "Stops the current event."));
            add(Make("flow.select", "Select", "Flow", P, { In("Condition", B, "true"), In("A", X), In("B", X) }, { Out("Result", X, "({0} ? {1} : {2})") }, "", "Picks A if the condition is true, otherwise B."));
            add(Make("flow.delay", "Delay", "Flow", F, { ExecIn(), In("Duration", N, "1") }, { ExecOut("Completed") }, "",
                     "Latent: continues after Duration seconds; calls while it is counting down are ignored."));
            add(Make("flow.retriggerabledelay", "Retriggerable Delay", "Flow", F, { ExecIn(), In("Duration", N, "1") }, { ExecOut("Completed") }, "",
                     "Latent: every call restarts the countdown; continues Duration seconds after the last call."));
            {
                NodeDef d = Make("flow.gate", "Gate", "Flow", F, { { "Enter", PinType::Exec, "" }, { "Open", PinType::Exec, "" }, { "Close", PinType::Exec, "" }, { "Toggle", PinType::Exec, "" } },
                                 { ExecOut("Exit") }, "", "Enter passes to Exit only while the gate is open.");
                d.Properties = { { "Start Closed", "false" } };
                add(d);
            }
            {
                NodeDef d = Make("flow.multigate", "MultiGate", "Flow", F, { ExecIn(), { "Reset", PinType::Exec, "" } },
                                 { ExecOut("Out 0"), ExecOut("Out 1"), ExecOut("Out 2"), ExecOut("Out 3") }, "",
                                 "Each call fires the next connected output (or a random unused one); with Loop it starts over after the last.");
                d.Properties = { { "Random", "false" }, { "Loop", "false" } };
                add(d);
            }
            add(Make("flow.don", "Do N", "Flow", F, { { "Enter", PinType::Exec, "" }, In("N", N, "1"), { "Reset", PinType::Exec, "" } }, { ExecOut("Exit"), Out("Counter", N) }, "",
                     "Passes through N times (until Reset)."));
            {
                NodeDef d = Make("flow.switchint", "Switch on Int", "Flow", F, { ExecIn(), In("Selection", N, "0") }, {}, "", "Runs the output whose case equals the value, otherwise Default.");
                d.Properties = { { "Cases", "0, 1, 2" } };
                add(d);
            }
            {
                NodeDef d = Make("flow.switchstring", "Switch on String", "Flow", F, { ExecIn(), In("Selection", S, "") }, {}, "", "Runs the output whose case equals the text, otherwise Default.");
                d.Properties = { { "Cases", "A, B, C" } };
                add(d);
            }
            {
                NodeDef d = Make("flow.timeline", "Timeline", "Flow", F,
                                 { { "Play", PinType::Exec, "" }, { "Play from Start", PinType::Exec, "" }, { "Stop", PinType::Exec, "" }, { "Reverse", PinType::Exec, "" }, { "Reverse from End", PinType::Exec, "" } },
                                 { ExecOut("Update"), ExecOut("Finished"), Out("Value", N), Out("Time", N) }, "",
                                 "Plays a float track over time (Unreal Timeline): Update fires every frame while playing, Finished at the end. Keys: time:value pairs.");
                d.Properties = { { "Length", "1" }, { "Keys", "0:0, 1:1" }, { "Loop", "false" } };
                add(d);
            }

            // ── Değişken / sabit ──
            {
                NodeDef d = Make("var.get", "Get", "Variables", NodeKind::Variable, {}, { Out("Value", X) });
                d.Properties = { { "Variable", "" } };
                add(d);
            }
            {
                NodeDef d = Make("var.set", "Set", "Variables", A, { ExecIn(), In("Value", X) }, { ExecOut(), Out("Value", X) });
                d.Properties = { { "Variable", "" } };
                add(d);
            }
            {
                NodeDef d = Make("literal.number", "Number", "Literals", NodeKind::Literal, {}, { Out("Value", N) });
                d.Properties = { { "Value", "0" } };
                add(d);
            }
            {
                NodeDef d = Make("literal.string", "Text", "Literals", NodeKind::Literal, {}, { Out("Value", S) });
                d.Properties = { { "Value", "" } };
                add(d);
            }
            {
                NodeDef d = Make("literal.bool", "Boolean", "Literals", NodeKind::Literal, {}, { Out("Value", B) });
                d.Properties = { { "Value", "true" } };
                add(d);
            }
            {
                NodeDef d = Make("literal.key", "Key Code", "Literals", NodeKind::Literal, {}, { Out("Value", N) });
                d.Properties = { { "Key", "Space" } };
                add(d);
            }

            // ── Eylemler ──
            add(Make("action.print", "Print", "Debug", A, { ExecIn(), In("Message", X, "Hello") }, { ExecOut() }, "print({0})", "Writes to the Log panel."));
            add(Make("action.listadd", "Add To List", "Collections", A, { ExecIn(), In("List", L), In("Item", X) }, { ExecOut() }, "{0}.Add({1})"));
            add(Make("action.listclear", "Clear List", "Collections", A, { ExecIn(), In("List", L) }, { ExecOut() }, "{0}.Clear()"));

            // ── Matematik ──
            auto bin = [&](const char* id, const char* title, const char* op) {
                add(Make(std::string("math.") + id, title, "Math", P, { In("A", N, "0"), In("B", N, "0") }, { Out("Result", N, std::string("({0} ") + op + " {1})") }));
            };
            bin("add", "Add", "+");
            bin("sub", "Subtract", "-");
            bin("mul", "Multiply", "*");
            bin("div", "Divide", "/");
            bin("mod", "Modulo", "%");
            add(Make("math.negate", "Negate", "Math", P, { In("Value", N, "0") }, { Out("Result", N, "(-{0})") }));
            add(Make("math.abs", "Abs", "Math", P, { In("Value", N, "0") }, { Out("Result", N, "MathAbs({0})") }));
            add(Make("math.min", "Min", "Math", P, { In("A", N, "0"), In("B", N, "0") }, { Out("Result", N, "MathMin({0}, {1})") }));
            add(Make("math.max", "Max", "Math", P, { In("A", N, "0"), In("B", N, "0") }, { Out("Result", N, "MathMax({0}, {1})") }));
            add(Make("math.clamp", "Clamp", "Math", P, { In("Value", N, "0"), In("Min", N, "0"), In("Max", N, "1") }, { Out("Result", N, "MathClamp({0}, {1}, {2})") }));
            add(Make("math.lerp", "Lerp", "Math", P, { In("A", N, "0"), In("B", N, "1"), In("T", N, "0.5") }, { Out("Result", N, "MathLerp({0}, {1}, {2})") }));
            add(Make("math.sin", "Sin", "Math", P, { In("Radians", N, "0") }, { Out("Result", N, "MathSin({0})") }));
            add(Make("math.cos", "Cos", "Math", P, { In("Radians", N, "0") }, { Out("Result", N, "MathCos({0})") }));
            add(Make("math.sqrt", "Square Root", "Math", P, { In("Value", N, "0") }, { Out("Result", N, "MathSqrt({0})") }));
            add(Make("math.pow", "Power", "Math", P, { In("Base", N, "2"), In("Exponent", N, "2") }, { Out("Result", N, "MathPow({0}, {1})") }));
            add(Make("math.floor", "Floor", "Math", P, { In("Value", N, "0") }, { Out("Result", N, "MathFloor({0})") }));
            add(Make("math.round", "Round", "Math", P, { In("Value", N, "0") }, { Out("Result", N, "MathRound({0})") }));
            add(Make("math.random", "Random Range", "Math", P, { In("Min", N, "0"), In("Max", N, "1") }, { Out("Result", N, "random(float({0}), float({1}))") }));
            add(Make("math.randomint", "Random Integer", "Math", P, { In("Min", N, "0"), In("Max", N, "10") }, { Out("Result", N, "random(int({0}), int({1}))") }, "", "Integer in [Min, Max)."));
            add(Make("math.toint", "To Integer", "Math", P, { In("Value", X, "0") }, { Out("Result", N, "int({0})") }));

            // ── Karşılaştırma / mantık ──
            auto cmp = [&](const char* id, const char* title, const char* op) {
                add(Make(std::string("compare.") + id, title, "Logic", P, { In("A", X, "0"), In("B", X, "0") }, { Out("Result", B, std::string("({0} ") + op + " {1})") }));
            };
            cmp("eq", "Equal", "==");
            cmp("ne", "Not Equal", "!=");
            cmp("lt", "Less", "<");
            cmp("le", "Less or Equal", "<=");
            cmp("gt", "Greater", ">");
            cmp("ge", "Greater or Equal", ">=");
            add(Make("logic.and", "And", "Logic", P, { In("A", B, "true"), In("B", B, "true") }, { Out("Result", B, "({0} && {1})") }));
            add(Make("logic.or", "Or", "Logic", P, { In("A", B, "false"), In("B", B, "false") }, { Out("Result", B, "({0} || {1})") }));
            add(Make("logic.not", "Not", "Logic", P, { In("Value", B, "false") }, { Out("Result", B, "(!{0})") }));
            add(Make("logic.isnull", "Is Null", "Logic", P, { In("Value", X) }, { Out("Result", B, "({0} == null)") }));

            // ── Vektör ──
            add(Make("vector.make", "Make Vector", "Vector", P, { In("X", N, "0"), In("Y", N, "0"), In("Z", N, "0") }, { Out("Vector", V, "Vector3({0}, {1}, {2})") }));
            add(Make("vector.break", "Break Vector", "Vector", P, { In("Vector", V) }, { Out("X", N, "({0}).x"), Out("Y", N, "({0}).y"), Out("Z", N, "({0}).z") }));
            add(Make("vector.add", "Vector Add", "Vector", P, { In("A", V), In("B", V) }, { Out("Result", V, "({0} + {1})") }));
            add(Make("vector.sub", "Vector Subtract", "Vector", P, { In("A", V), In("B", V) }, { Out("Result", V, "({0} - {1})") }));
            add(Make("vector.scale", "Vector Scale", "Vector", P, { In("Vector", V), In("Scale", N, "1") }, { Out("Result", V, "({0} * {1})") }));
            add(Make("vector.length", "Vector Length", "Vector", P, { In("Vector", V) }, { Out("Length", N, "Vec3Length({0})") }));
            add(Make("vector.distance", "Distance", "Vector", P, { In("A", V), In("B", V) }, { Out("Distance", N, "Vec3Distance({0}, {1})") }));
            add(Make("vector.normalize", "Normalize", "Vector", P, { In("Vector", V) }, { Out("Result", V, "Vec3Normalize({0})") }));

            // ── Metin ──
            add(Make("string.concat", "Append", "Text", P, { In("A", X, ""), In("B", X, "") }, { Out("Result", S, "(str({0}) + str({1}))") }));
            add(Make("string.tostring", "To Text", "Text", P, { In("Value", X) }, { Out("Result", S, "str({0})") }));
            add(Make("string.length", "Length", "Text", P, { In("Value", X) }, { Out("Length", N, "len({0})") }));
            add(Make("string.contains", "Contains", "Text", P, { In("Text", S), In("Part", S) }, { Out("Result", B, "({0}).Contains({1})") }));

            // ── Koleksiyon ──
            add(Make("list.make", "Make List", "Collections", P, { In("A", X), In("B", X), In("C", X) }, { Out("List", L, "[{0}, {1}, {2}]") }));
            add(Make("list.empty", "Empty List", "Collections", P, {}, { Out("List", L, "[]") }));
            add(Make("list.get", "Get Item", "Collections", P, { In("List", L), In("Index", N, "0") }, { Out("Item", X, "({0})[int({1})]") }));
            add(Make("list.count", "Count", "Collections", P, { In("List", L) }, { Out("Count", N, "len({0})") }));
            add(Make("list.contains", "List Contains", "Collections", P, { In("List", L), In("Item", X) }, { Out("Result", B, "({0}).Contains({1})") }));

            // ── nativebridge: tüm motor fonksiyonları ──
            std::unordered_set<std::string> covered = { "print", "LogInfo", "VSTrace" };
            const auto& docs = Scripting::GetNativeDocs();
            for (const auto& f : Scripting::NativeRegistry::Get().GetFunctions())
            {
                if (covered.count(f.Name)) continue;
                bool valid = !f.Name.empty() && (std::isalpha((unsigned char)f.Name[0]) || f.Name[0] == '_');
                for (char c : f.Name) if (!std::isalnum((unsigned char)c) && c != '_') valid = false;
                if (!valid) continue;
                auto doc = docs.find(f.Name);
                std::string signature = doc != docs.end() ? doc->second.Signature : "";
                std::string category = doc != docs.end() ? doc->second.Category : "";
                if (category.empty())
                {
                    static const std::pair<const char*, const char*> prefixes[] = {
                        { "Math", "Math" }, { "Vec", "Vector" }, { "Mat4", "Math" }, { "String", "Text" }, { "List", "Collections" },
                        { "Map", "Collections" }, { "Date", "Time" }, { "Path", "Files" }, { "File", "Files" }, { "Log", "Log" },
                        { "Debug", "Debug Draw" }, { "Net", "Network" }, { "Audio", "Audio" }, { "Anim", "Animation" }, { "Nav", "AI" },
                        { "Input", "Input" }, { "Is", "Input" }, { "Get", "Entity" }, { "Set", "Entity" }, { "Color", "Color" },
                        { "Geom", "Math" }, { "Stat", "Math" }, { "Rand", "Math" }, { "Bit", "Math" }, { "Regex", "Text" },
                    };
                    category = "Engine";
                    for (auto& [p, c] : prefixes)
                        if (f.Name.rfind(p, 0) == 0) { category = c; break; }
                }
                // Parametre adları imzadan: "SetPosition(x, y, z)"
                std::vector<std::string> names;
                size_t lp = signature.find('('), rp = signature.find(')');
                if (lp != std::string::npos && rp != std::string::npos && rp > lp + 1)
                {
                    std::string inside = signature.substr(lp + 1, rp - lp - 1);
                    std::stringstream ss(inside);
                    std::string part;
                    while (std::getline(ss, part, ','))
                    {
                        size_t a = part.find_first_not_of(" ."), b = part.find_last_not_of(' ');
                        std::string n = a == std::string::npos ? "" : part.substr(a, b - a + 1);
                        size_t eq = n.find('=');
                        if (eq != std::string::npos) n = n.substr(0, n.find_last_not_of(' ', eq - 1) + 1);
                        if (!n.empty()) names.push_back(n);
                    }
                }
                int arity = f.Arity < 0 ? std::max<int>(1, (int)names.size()) : f.Arity;
                std::vector<PinDef> inputs;
                std::string args;
                for (int i = 0; i < arity; ++i)
                {
                    std::string pin = i < (int)names.size() ? names[(size_t)i] : "Arg " + std::to_string(i + 1);
                    if (!pin.empty()) pin[0] = (char)std::toupper((unsigned char)pin[0]);
                    PinType t = X;
                    if (pin == "X" || pin == "Y" || pin == "Z" || pin == "W" || pin == "Volume" || pin == "Pitch" || pin == "Speed" || pin == "Scale") t = N;
                    inputs.push_back(In(pin, t, t == N ? "0" : ""));
                    args += (i ? ", " : "") + std::string("{") + std::to_string(i) + "}";
                }
                // Dönüş türü imzadan
                PinType ret = X;
                size_t arrow = signature.find("->");
                bool returns = arrow != std::string::npos;
                if (returns)
                {
                    std::string r = signature.substr(arrow + 2);
                    if (r.find("bool") != std::string::npos) ret = B;
                    else if (r.find("float") != std::string::npos || r.find("int") != std::string::npos) ret = N;
                    else if (r.find("{x") != std::string::npos) ret = V;
                    else if (r.find("string") != std::string::npos) ret = S;
                }
                bool pure = StartsWithAny(f.Name, { "Get", "Is", "Has", "Math", "Vec3", "Vec2", "Mat4", "String", "Date", "Path", "Color",
                                                    "Geom", "Stat", "Bit", "Regex", "Env", "TypeOf", "ToString", "ToNumber", "Parse",
                                                    "Base64", "Hash", "URL", "Make" })
                            && !StartsWithAny(f.Name, { "MathRunThreadSafetyTest" });
                if (!returns && pure)
                    returns = true; // saf fonksiyonlar değer döndürür
                std::string expr = f.Name + "(" + args + ")";
                NodeDef d;
                d.Id = "native." + f.Name;
                d.Title = f.Name;
                d.Category = "Engine/" + category;
                d.Description = doc != docs.end() ? std::string(doc->second.Signature) + " - " + doc->second.Description : "nativebridge." + f.Name;
                d.Expression = expr;
                if (pure)
                {
                    d.Kind = P;
                    d.Inputs = inputs;
                    d.Outputs = { Out("Result", ret, expr) };
                }
                else
                {
                    d.Kind = A;
                    d.Inputs = { ExecIn() };
                    d.Inputs.insert(d.Inputs.end(), inputs.begin(), inputs.end());
                    d.Outputs = { ExecOut() };
                    if (returns) d.Outputs.push_back(Out("Result", ret));
                }
                lib.push_back(std::move(d));
            }

            lib.push_back(Make("comment", "Comment", "Utility", NodeKind::Comment, {}, {}, "", "A resizable comment box."));
            lib.back().Properties = { { "Text", "Comment" } };
            return lib;
        }

        std::vector<NodeDef>& LibraryStorage()
        {
            static std::vector<NodeDef> s_Lib = BuildLibrary();
            return s_Lib;
        }

        std::unordered_map<std::string, size_t>& LibraryIndex()
        {
            static std::unordered_map<std::string, size_t> s_Index = [] {
                std::unordered_map<std::string, size_t> idx;
                auto& lib = LibraryStorage();
                for (size_t i = 0; i < lib.size(); ++i) idx[lib[i].Id] = i;
                return idx;
            }();
            return s_Index;
        }
    }

    const std::vector<NodeDef>& NodeLibrary::All() { return LibraryStorage(); }

    const NodeDef* NodeLibrary::Find(const std::string& id)
    {
        auto& idx = LibraryIndex();
        auto it = idx.find(id);
        return it == idx.end() ? nullptr : &LibraryStorage()[it->second];
    }

    // ════════════════════════════════════════
    //  Graph editing
    // ════════════════════════════════════════
    Node* Graph::FindNode(int id)
    {
        for (auto& n : Nodes) if (n.Id == id) return &n;
        return nullptr;
    }

    const Node* Graph::FindNode(int id) const
    {
        for (auto& n : Nodes) if (n.Id == id) return &n;
        return nullptr;
    }

    Variable* Graph::FindVariable(const std::string& name)
    {
        for (auto& v : Variables) if (v.Name == name) return &v;
        return nullptr;
    }

    int Graph::AddNode(const std::string& type, float x, float y)
    {
        const NodeDef* def = NodeLibrary::Find(type);
        if (!def) return 0;
        Node n;
        n.Id = NextId++;
        n.Type = type;
        n.X = x;
        n.Y = y;
        for (auto& [k, v] : def->Properties) n.Properties[k] = v;
        if (def->Kind == NodeKind::Comment) { n.Width = 320.0f; n.Height = 180.0f; }
        Nodes.push_back(n);
        return n.Id;
    }

    void Graph::RemoveNode(int id)
    {
        Nodes.erase(std::remove_if(Nodes.begin(), Nodes.end(), [&](const Node& n) { return n.Id == id; }), Nodes.end());
        Links.erase(std::remove_if(Links.begin(), Links.end(), [&](const Link& l) { return l.FromNode == id || l.ToNode == id; }), Links.end());
    }

    namespace
    {
        // "A, B, C" -> {"A", "B", "C"} (boşluklar kırpılır, boş öğeler atlanır)
        std::vector<std::string> SplitList(const std::string& s)
        {
            std::vector<std::string> out;
            std::stringstream ss(s);
            std::string part;
            while (std::getline(ss, part, ','))
            {
                const size_t a = part.find_first_not_of(" \t"), b = part.find_last_not_of(" \t");
                if (a != std::string::npos)
                    out.push_back(part.substr(a, b - a + 1));
            }
            return out;
        }
    }

    NodeDef Graph::ResolveDef(const Node& node) const
    {
        const NodeDef* base = NodeLibrary::Find(node.Type);
        if (!base)
        {
            NodeDef missing;
            missing.Id = node.Type;
            missing.Title = "Unknown: " + node.Type;
            missing.Kind = NodeKind::Pure;
            return missing;
        }
        NodeDef d = *base;
        if (node.Type == "var.get" || node.Type == "var.set")
        {
            auto it = node.Properties.find("Variable");
            std::string name = it != node.Properties.end() ? it->second : "";
            PinType t = PinType::Any;
            for (auto& v : Variables) if (v.Name == name) t = v.Type;
            d.Title = (node.Type == "var.get" ? "Get " : "Set ") + (name.empty() ? std::string("(variable)") : name);
            for (auto& p : d.Outputs) if (p.Name == "Value") p.Type = t;
            for (auto& p : d.Inputs) if (p.Name == "Value") p.Type = t;
        }
        else if (node.Type == "event.custom")
        {
            auto it = node.Properties.find("Name");
            d.Title = "Event: " + (it != node.Properties.end() ? it->second : std::string("?"));
        }
        else if (node.Type == "event.keydown" || node.Type == "event.keyheld")
        {
            auto it = node.Properties.find("Key");
            d.Title = std::string(node.Type == "event.keydown" ? "On Key Pressed: " : "While Key Held: ") + (it != node.Properties.end() ? it->second : "");
        }
        else if (node.Type == "event.timer")
        {
            auto it = node.Properties.find("Interval");
            d.Title = "Every " + (it != node.Properties.end() ? it->second : std::string("1")) + " s";
        }
        else
        {
            auto prop = [&](const char* key, const std::string& def) {
                auto it = node.Properties.find(key);
                return it != node.Properties.end() ? it->second : def;
            };
            if (node.Type == "event.collision" || node.Type == "event.trigger")
                d.Title = std::string(node.Type == "event.collision" ? "On Collision " : "On Trigger ") + prop("Phase", "Enter") + (prop("Physics", "3D") == "2D" ? " 2D" : "");
            else if (node.Type == "event.inputaction")
                d.Title = "Input Action: " + prop("Action", "?") + " (" + prop("Phase", "Performed") + ")";
            else if (node.Type == "event.function")
            {
                d.Title = "Function: " + prop("Name", "?");
                for (auto& p : SplitList(prop("Inputs", "")))
                    d.Outputs.push_back({ p, PinType::Any, "" });
            }
            else if (node.Type == "action.callfunction")
            {
                const std::string fn = prop("Function", "");
                d.Title = "Call " + (fn.empty() ? std::string("(function)") : fn);
                std::string args;
                int idx = 0;
                for (auto& n : Nodes)
                {
                    if (n.Type != "event.function") continue;
                    auto name = n.Properties.find("Name");
                    if (name == n.Properties.end() || name->second != fn) continue;
                    auto inputs = n.Properties.find("Inputs");
                    for (auto& p : SplitList(inputs != n.Properties.end() ? inputs->second : ""))
                    {
                        d.Inputs.push_back({ p, PinType::Any, "" });
                        args += (idx ? ", {" : "{") + std::to_string(idx) + "}";
                        ++idx;
                    }
                    break;
                }
                d.Expression = SanitizeIdentifier(fn) + "(" + args + ")";
            }
            else if (node.Type == "macro.inputs" || node.Type == "macro.outputs")
            {
                std::vector<PinDef> pins;
                for (auto& item : SplitList(prop("Pins", "")))
                {
                    const size_t colon = item.find(':');
                    std::string name = colon == std::string::npos ? item : item.substr(0, colon);
                    std::string type = colon == std::string::npos ? "any" : item.substr(colon + 1);
                    const size_t a = name.find_last_not_of(" \t"), b = type.find_first_not_of(" \t");
                    name = a == std::string::npos ? std::string() : name.substr(0, a + 1);
                    type = b == std::string::npos ? std::string("any") : type.substr(b);
                    if (!name.empty())
                        pins.push_back({ name, PinTypeFromName(type), "" });
                }
                if (node.Type == "macro.inputs")
                    d.Outputs = pins;
                else
                    d.Inputs = pins;
            }
            else if (node.Type == "macro.instance")
            {
                const std::string name = prop("Macro", "");
                d.Title = name.empty() ? std::string("Macro (none)") : name;
                if (const MacroDef* m = FindMacro(name); m && m->Body)
                    MacroPins(*m->Body, d.Inputs, d.Outputs);
            }
            else if (node.Type == "flow.switchint" || node.Type == "flow.switchstring")
            {
                for (auto& c : SplitList(prop("Cases", "")))
                    d.Outputs.push_back({ c, PinType::Exec, "" });
                d.Outputs.push_back({ "Default", PinType::Exec, "" });
            }
            else if (node.Type == "flow.timeline")
                d.Title = "Timeline (" + prop("Length", "1") + " s" + (prop("Loop", "false") == "true" ? ", loop)" : ")");
        }
        return d;
    }

    const Link* Graph::FindInputLink(int node, const std::string& pin) const
    {
        for (auto& l : Links) if (l.ToNode == node && l.ToPin == pin) return &l;
        return nullptr;
    }

    std::vector<const Link*> Graph::FindOutputLinks(int node, const std::string& pin) const
    {
        std::vector<const Link*> out;
        for (auto& l : Links) if (l.FromNode == node && l.FromPin == pin) out.push_back(&l);
        return out;
    }

    bool Graph::Connect(int fromNode, const std::string& fromPin, int toNode, const std::string& toPin, std::string* error)
    {
        const Node* a = FindNode(fromNode);
        const Node* b = FindNode(toNode);
        if (!a || !b || fromNode == toNode)
        {
            if (error) *error = "invalid nodes";
            return false;
        }
        NodeDef da = ResolveDef(*a), db = ResolveDef(*b);
        const PinDef* out = nullptr;
        const PinDef* in = nullptr;
        for (auto& p : da.Outputs) if (p.Name == fromPin) out = &p;
        for (auto& p : db.Inputs) if (p.Name == toPin) in = &p;
        if (!out || !in)
        {
            if (error) *error = "unknown pin";
            return false;
        }
        if (!PinsCompatible(out->Type, in->Type))
        {
            if (error) *error = std::string("cannot connect ") + PinTypeName(out->Type) + " to " + PinTypeName(in->Type);
            return false;
        }
        if (out->Type == PinType::Exec)
        {
            // Exec çıkışı tek hedefe gider; exec girişi birden çok kaynaktan gelebilir
            Links.erase(std::remove_if(Links.begin(), Links.end(), [&](const Link& l) { return l.FromNode == fromNode && l.FromPin == fromPin; }), Links.end());
        }
        else
        {
            // Veri girişi tek kaynaklıdır
            Links.erase(std::remove_if(Links.begin(), Links.end(), [&](const Link& l) { return l.ToNode == toNode && l.ToPin == toPin; }), Links.end());
        }
        Link l;
        l.Id = NextId++;
        l.FromNode = fromNode;
        l.FromPin = fromPin;
        l.ToNode = toNode;
        l.ToPin = toPin;
        Links.push_back(l);
        return true;
    }

    void Graph::Disconnect(int linkId)
    {
        Links.erase(std::remove_if(Links.begin(), Links.end(), [&](const Link& l) { return l.Id == linkId; }), Links.end());
    }

    // ════════════════════════════════════════
    //  Serialization (JSON)
    // ════════════════════════════════════════
    std::string Graph::ToJson() const
    {
        using JSON::Value;
        std::unordered_map<std::string, Value> root;
        root["format"] = Value::MakeString("cqgraph");
        root["version"] = Value::MakeNumber(1);
        root["name"] = Value::MakeString(Name);
        root["nextId"] = Value::MakeNumber(NextId);
        std::vector<Value> vars;
        for (auto& v : Variables)
        {
            std::unordered_map<std::string, Value> m;
            m["name"] = Value::MakeString(v.Name);
            m["type"] = Value::MakeString(PinTypeName(v.Type));
            m["default"] = Value::MakeString(v.Default);
            m["public"] = Value::MakeBool(v.Public);
            m["tooltip"] = Value::MakeString(v.Tooltip);
            vars.push_back(Value::MakeMap(m));
        }
        root["variables"] = Value::MakeList(vars);
        std::vector<Value> nodes;
        for (auto& n : Nodes)
        {
            std::unordered_map<std::string, Value> m;
            m["id"] = Value::MakeNumber(n.Id);
            m["type"] = Value::MakeString(n.Type);
            m["x"] = Value::MakeNumber(std::round(n.X));
            m["y"] = Value::MakeNumber(std::round(n.Y));
            if (n.Width > 0) { m["w"] = Value::MakeNumber(n.Width); m["h"] = Value::MakeNumber(n.Height); }
            std::unordered_map<std::string, Value> props, defs;
            for (auto& [k, v] : n.Properties) props[k] = Value::MakeString(v);
            for (auto& [k, v] : n.InputDefaults) defs[k] = Value::MakeString(v);
            m["properties"] = Value::MakeMap(props);
            m["defaults"] = Value::MakeMap(defs);
            nodes.push_back(Value::MakeMap(m));
        }
        root["nodes"] = Value::MakeList(nodes);
        std::vector<Value> links;
        for (auto& l : Links)
        {
            std::unordered_map<std::string, Value> m;
            m["id"] = Value::MakeNumber(l.Id);
            m["from"] = Value::MakeNumber(l.FromNode);
            m["fromPin"] = Value::MakeString(l.FromPin);
            m["to"] = Value::MakeNumber(l.ToNode);
            m["toPin"] = Value::MakeString(l.ToPin);
            links.push_back(Value::MakeMap(m));
        }
        root["links"] = Value::MakeList(links);
        if (!Macros.empty())
        {
            std::vector<Value> macros;
            for (auto& m : Macros)
            {
                if (!m.Body) continue;
                std::unordered_map<std::string, Value> mm;
                mm["name"] = Value::MakeString(m.Name);
                mm["graph"] = JSON::Parse(m.Body->ToJson());
                macros.push_back(Value::MakeMap(mm));
            }
            root["macros"] = Value::MakeList(macros);
        }
        return JSON::Stringify(Value::MakeMap(root));
    }

    bool Graph::FromJson(const std::string& json, std::string* error)
    {
        JSON::Value root = JSON::Parse(json);
        if (root.Type != JSON::ValueType::Map)
        {
            if (error) *error = "invalid graph file";
            return false;
        }
        auto str = [](const JSON::Value& m, const char* k, const std::string& def = "") {
            auto it = m.MapVal.find(k);
            return it != m.MapVal.end() && it->second.Type == JSON::ValueType::String ? it->second.StringVal : def;
        };
        auto num = [](const JSON::Value& m, const char* k, double def = 0) {
            auto it = m.MapVal.find(k);
            return it != m.MapVal.end() && it->second.Type == JSON::ValueType::Number ? it->second.NumberVal : def;
        };
        Name = str(root, "name", Name);
        NextId = (int)num(root, "nextId", 1);
        Variables.clear();
        Nodes.clear();
        Links.clear();
        if (auto it = root.MapVal.find("variables"); it != root.MapVal.end())
            for (auto& v : it->second.ListVal)
            {
                Variable var;
                var.Name = str(v, "name");
                var.Type = PinTypeFromName(str(v, "type", "number"));
                var.Default = str(v, "default");
                auto pub = v.MapVal.find("public");
                var.Public = pub == v.MapVal.end() || pub->second.BoolVal;
                var.Tooltip = str(v, "tooltip");
                Variables.push_back(var);
            }
        int maxId = 0;
        if (auto it = root.MapVal.find("nodes"); it != root.MapVal.end())
            for (auto& v : it->second.ListVal)
            {
                Node n;
                n.Id = (int)num(v, "id");
                n.Type = str(v, "type");
                n.X = (float)num(v, "x");
                n.Y = (float)num(v, "y");
                n.Width = (float)num(v, "w");
                n.Height = (float)num(v, "h");
                if (auto p = v.MapVal.find("properties"); p != v.MapVal.end())
                    for (auto& [k, pv] : p->second.MapVal) n.Properties[k] = pv.StringVal;
                if (auto p = v.MapVal.find("defaults"); p != v.MapVal.end())
                    for (auto& [k, pv] : p->second.MapVal) n.InputDefaults[k] = pv.StringVal;
                maxId = std::max(maxId, n.Id);
                Nodes.push_back(n);
            }
        if (auto it = root.MapVal.find("links"); it != root.MapVal.end())
            for (auto& v : it->second.ListVal)
            {
                Link l;
                l.Id = (int)num(v, "id");
                l.FromNode = (int)num(v, "from");
                l.FromPin = str(v, "fromPin");
                l.ToNode = (int)num(v, "to");
                l.ToPin = str(v, "toPin");
                maxId = std::max(maxId, l.Id);
                Links.push_back(l);
            }
        NextId = std::max(NextId, maxId + 1);
        Macros.clear();
        if (auto it = root.MapVal.find("macros"); it != root.MapVal.end())
            for (auto& v : it->second.ListVal)
            {
                MacroDef m;
                m.Name = str(v, "name");
                m.Body = std::make_shared<Graph>();
                if (auto g = v.MapVal.find("graph"); g != v.MapVal.end())
                    m.Body->FromJson(JSON::Stringify(g->second), nullptr);
                m.Body->Name = m.Name;
                m.Body->MacroScope = this;
                Macros.push_back(std::move(m));
            }
        return true;
    }

    const MacroDef* Graph::FindMacro(const std::string& name) const
    {
        if (name.empty())
            return nullptr;
        for (auto& m : Macros)
            if (m.Name == name)
                return &m;
        return MacroScope && MacroScope != this ? MacroScope->FindMacro(name) : nullptr;
    }

    MacroDef& Graph::AddMacro(const std::string& name)
    {
        MacroDef m;
        m.Name = name;
        m.Body = std::make_shared<Graph>();
        m.Body->Name = name;
        m.Body->MacroScope = this;
        const int in = m.Body->AddNode("macro.inputs", 0.0f, 0.0f);
        const int out = m.Body->AddNode("macro.outputs", 420.0f, 0.0f);
        (void)in;
        (void)out;
        Macros.push_back(std::move(m));
        return Macros.back();
    }

    void Graph::MacroPins(const Graph& body, std::vector<PinDef>& inputs, std::vector<PinDef>& outputs)
    {
        inputs.clear();
        outputs.clear();
        for (auto& n : body.Nodes)
        {
            if (n.Type == "macro.inputs" && inputs.empty())
                inputs = body.ResolveDef(n).Outputs;
            else if (n.Type == "macro.outputs" && outputs.empty())
                outputs = body.ResolveDef(n).Inputs;
        }
    }

    Graph Graph::ExpandMacros(std::vector<CompileMessage>& messages, std::unordered_map<int, int>* expandedToInstance) const
    {
        Graph flat;
        flat.Name = Name;
        flat.Variables = Variables;
        flat.Nodes = Nodes;
        flat.Links = Links;
        flat.NextId = NextId;
        int nextLink = NextId;
        for (auto& l : flat.Links) nextLink = std::max(nextLink, l.Id + 1);
        for (auto& n : flat.Nodes) flat.NextId = std::max(flat.NextId, n.Id + 1);
        flat.NextId = std::max(flat.NextId, nextLink);
        // Düğüm -> açıldığı en dıştaki makro düğümü ve açılma derinliği (kendini çağıran makro sonsuza açılmasın)
        std::unordered_map<int, int> owner, depth;
        for (;;)
        {
            auto it = std::find_if(flat.Nodes.begin(), flat.Nodes.end(), [](const Node& n) { return n.Type == "macro.instance"; });
            if (it == flat.Nodes.end())
                break;
            const Node inst = *it;
            const int topInstance = owner.count(inst.Id) ? owner[inst.Id] : inst.Id;
            const int d = depth.count(inst.Id) ? depth[inst.Id] : 0;
            auto propIt = inst.Properties.find("Macro");
            const std::string macroName = propIt != inst.Properties.end() ? propIt->second : "";
            const MacroDef* macro = FindMacro(macroName);
            if (!macro || !macro->Body || d >= 16)
            {
                messages.push_back({ topInstance, !macro || !macro->Body ? "Unknown macro '" + macroName + "'." : "Macro '" + macroName + "' uses itself (recursive macro).", false });
                flat.RemoveNode(inst.Id);
                continue;
            }
            const Graph& body = *macro->Body;
            int inputsNode = 0, outputsNode = 0;
            for (auto& n : body.Nodes)
            {
                if (n.Type == "macro.inputs" && !inputsNode) inputsNode = n.Id;
                else if (n.Type == "macro.outputs" && !outputsNode) outputsNode = n.Id;
                else if (n.Type.rfind("event.", 0) == 0)
                    messages.push_back({ topInstance, "Macro '" + macroName + "' contains an event node; events belong in the graph, not in macros.", false });
            }
            // 1) Gövde düğümlerinin kopyaları (tüneller hariç), yeni kimliklerle
            std::unordered_map<int, int> map;
            for (auto& n : body.Nodes)
            {
                if (n.Id == inputsNode || n.Id == outputsNode || n.Type == "comment")
                    continue;
                Node c = n;
                c.Id = flat.NextId++;
                map[n.Id] = c.Id;
                owner[c.Id] = topInstance;
                depth[c.Id] = d + 1;
                if (expandedToInstance)
                    (*expandedToInstance)[c.Id] = topInstance;
                flat.Nodes.push_back(std::move(c));
            }
            // Örneğin dış bağlantıları
            std::vector<Link> into, outOf;
            for (auto& l : flat.Links)
            {
                if (l.ToNode == inst.Id) into.push_back(l);
                if (l.FromNode == inst.Id) outOf.push_back(l);
            }
            auto addLink = [&](int fromNode, const std::string& fromPin, int toNode, const std::string& toPin) {
                Link l;
                l.Id = flat.NextId++;
                l.FromNode = fromNode;
                l.FromPin = fromPin;
                l.ToNode = toNode;
                l.ToPin = toPin;
                flat.Links.push_back(l);
            };
            // Örnekteki bir giriş pininin kaynağı (bağlantı ya da sabit)
            auto inputDefault = [&](const std::string& pin) -> std::string {
                auto dit = inst.InputDefaults.find(pin);
                return dit != inst.InputDefaults.end() ? dit->second : std::string();
            };
            // 2) Gövde bağlantıları
            for (auto& l : body.Links)
            {
                const bool fromIn = l.FromNode == inputsNode, toOut = l.ToNode == outputsNode;
                if (fromIn && toOut)
                {
                    // Doğrudan geçiş: dış kaynak -> dış hedef
                    for (auto& src : into)
                        if (src.ToPin == l.FromPin)
                            for (auto& dst : outOf)
                                if (dst.FromPin == l.ToPin)
                                    addLink(src.FromNode, src.FromPin, dst.ToNode, dst.ToPin);
                    continue;
                }
                if (fromIn)
                {
                    auto target = map.find(l.ToNode);
                    if (target == map.end()) continue;
                    bool linked = false;
                    for (auto& src : into)
                        if (src.ToPin == l.FromPin)
                        {
                            addLink(src.FromNode, src.FromPin, target->second, l.ToPin);
                            linked = true;
                        }
                    // Bağlı olmayan veri girişi: örnekteki sabit iç düğümün girişine
                    if (!linked)
                    {
                        const std::string def = inputDefault(l.FromPin);
                        if (!def.empty())
                            for (auto& n : flat.Nodes)
                                if (n.Id == target->second) n.InputDefaults[l.ToPin] = def;
                    }
                    continue;
                }
                if (toOut)
                {
                    auto source = map.find(l.FromNode);
                    if (source == map.end()) continue;
                    for (auto& dst : outOf)
                        if (dst.FromPin == l.ToPin)
                            addLink(source->second, l.FromPin, dst.ToNode, dst.ToPin);
                    continue;
                }
                auto a = map.find(l.FromNode), b = map.find(l.ToNode);
                if (a != map.end() && b != map.end())
                    addLink(a->second, l.FromPin, b->second, l.ToPin);
            }
            // 3) Örnek düğümü ve bağlantıları kalkar
            flat.RemoveNode(inst.Id);
        }
        return flat;
    }

    bool Graph::Save(const std::string& path) const
    {
        std::ofstream f(path, std::ios::binary);
        if (!f) return false;
        f << ToJson();
        return (bool)f;
    }

    bool Graph::Load(const std::string& path, std::string* error)
    {
        std::ifstream f(path, std::ios::binary);
        if (!f)
        {
            if (error) *error = "cannot open " + path;
            return false;
        }
        std::stringstream ss;
        ss << f.rdbuf();
        return FromJson(ss.str(), error);
    }

    // ════════════════════════════════════════
    //  Transpiler
    // ════════════════════════════════════════
    namespace
    {
        std::string QuoteString(const std::string& s)
        {
            std::string out = "\"";
            for (char c : s)
            {
                switch (c)
                {
                    case '"': out += "\\\""; break;
                    case '\\': out += "\\\\"; break;
                    case '\n': out += "\\n"; break;
                    case '\t': out += "\\t"; break;
                    case '{': out += "\\{"; break;
                    case '}': out += "\\}"; break;
                    default: out += c; break;
                }
            }
            return out + "\"";
        }

        bool IsNumber(const std::string& s, double* out = nullptr)
        {
            if (s.empty()) return false;
            char* end = nullptr;
            double d = std::strtod(s.c_str(), &end);
            if (end == s.c_str() || *end != '\0') return false;
            if (out) *out = d;
            return true;
        }

        std::string FloatLiteral(const std::string& s)
        {
            double d = 0;
            if (!IsNumber(s, &d)) return "0.0";
            std::string r = s;
            if (r.find_first_of(".eE") == std::string::npos) r += ".0";
            return r;
        }

        // Pin türüne göre bir sabiti CQS ifadesine çevir
        std::string LiteralFor(PinType type, const std::string& raw)
        {
            switch (type)
            {
                case PinType::Number: return FloatLiteral(raw.empty() ? "0" : raw);
                case PinType::Bool: return (raw == "true" || raw == "1" || raw == "True") ? "true" : "false";
                case PinType::String: return QuoteString(raw);
                case PinType::Vector:
                {
                    double v[3] = { 0, 0, 0 };
                    std::stringstream ss(raw);
                    std::string part;
                    for (int i = 0; i < 3 && std::getline(ss, part, ','); ++i) IsNumber(part.substr(part.find_first_not_of(' ') == std::string::npos ? 0 : part.find_first_not_of(' ')), &v[i]);
                    char buf[128];
                    std::snprintf(buf, sizeof(buf), "Vector3(%g, %g, %g)", v[0], v[1], v[2]);
                    return buf;
                }
                case PinType::List: return "[]";
                default:
                    if (raw.empty()) return "null";
                    if (raw == "true" || raw == "false") return raw;
                    if (IsNumber(raw)) return FloatLiteral(raw);
                    return QuoteString(raw);
            }
        }

        std::string FormatExpr(const std::string& tmpl, const std::vector<std::string>& args)
        {
            std::string out;
            for (size_t i = 0; i < tmpl.size(); ++i)
            {
                if (tmpl[i] == '{' && i + 1 < tmpl.size() && std::isdigit((unsigned char)tmpl[i + 1]))
                {
                    size_t close = tmpl.find('}', i);
                    int idx = std::atoi(tmpl.c_str() + i + 1);
                    out += idx < (int)args.size() ? args[(size_t)idx] : "null";
                    i = close;
                }
                else
                    out += tmpl[i];
            }
            return out;
        }

        class Transpiler
        {
        public:
            Transpiler(const Graph& g, std::vector<CompileMessage>& msgs, bool trace) : m_Graph(g), m_Messages(msgs), m_Trace(trace) {}

            std::string Run(const std::string& className, std::vector<int>* lineToNode)
            {
                m_ClassName = className;
                m_LineToNode = lineToNode;
                if (m_LineToNode) m_LineToNode->clear();
                Line("// Generated from visual script '" + m_Graph.Name + "' - do not edit, changes are overwritten.", 0);
                Line("using nativebridge;", 0);
                Line("", 0);
                Line("script " + className + " : ConquerorScript", 0);
                Line("{", 0);
                // Değişkenler
                std::set<std::string> names;
                for (auto& v : m_Graph.Variables)
                {
                    std::string id = SanitizeIdentifier(v.Name);
                    if (!names.insert(id).second)
                    {
                        Error(0, "Duplicate variable name '" + v.Name + "'.");
                        continue;
                    }
                    std::string decl = std::string("    ") + (v.Public ? "public " : "private ");
                    if (!v.Tooltip.empty()) Line("    [Tooltip(" + QuoteString(v.Tooltip) + ")]", 0);
                    switch (v.Type)
                    {
                        case PinType::Number: decl += "float " + id + " = " + LiteralFor(v.Type, v.Default) + ";"; break;
                        case PinType::Bool: decl += "bool " + id + " = " + LiteralFor(v.Type, v.Default) + ";"; break;
                        case PinType::String: decl += "string " + id + " = " + LiteralFor(v.Type, v.Default) + ";"; break;
                        default: decl += "var " + id + " = " + LiteralFor(v.Type, v.Default) + ";"; break;
                    }
                    Line(decl, 0);
                }
                // Gövdeleri önce üret (gizli alanlar bu sırada toplanır), sonra alanları yaz
                std::vector<std::pair<std::string, int>> body;
                std::swap(m_Out, body);
                GenerateEvents();
                std::swap(m_Out, body);
                for (auto& f : m_HiddenFields)
                    Line("    private var " + f + " = null;", 0);
                for (auto& f : m_HiddenNumberFields)
                    Line("    private var " + f + " = 0.0;", 0);
                for (auto& [text, node] : body)
                    Line(text, node);
                Line("}", 0);

                std::string out;
                for (auto& [text, node] : m_Out)
                {
                    out += text;
                    out += '\n';
                    if (m_LineToNode) m_LineToNode->push_back(node);
                }
                return out;
            }

        private:
            const Graph& m_Graph;
            std::vector<CompileMessage>& m_Messages;
            bool m_Trace = false;
            std::string m_ClassName;
            std::vector<int>* m_LineToNode = nullptr;
            std::vector<std::pair<std::string, int>> m_Out;
            std::set<std::string> m_HiddenFields;
            std::set<std::string> m_HiddenNumberFields;
            std::set<std::pair<int, std::string>> m_ExecStack;   // (düğüm, giriş pini): Gate Exit -> Close gibi bağlantılar döngü değildir
            std::set<int> m_DataStack;

            void Line(const std::string& text, int node) { m_Out.emplace_back(text, node); }
            void Error(int node, const std::string& msg) { m_Messages.push_back({ node, msg, false }); }
            void Warning(int node, const std::string& msg) { m_Messages.push_back({ node, msg, true }); }

            std::string Prop(const Node& n, const char* key, const std::string& def = "") const
            {
                auto it = n.Properties.find(key);
                return it != n.Properties.end() ? it->second : def;
            }

            static std::string Field(const char* prefix, int node, const std::string& pin = "")
            {
                return "_" + std::string(prefix) + std::to_string(node) + (pin.empty() ? "" : "_" + SanitizeIdentifier(pin));
            }

            // ── Veri ifadeleri ──
            std::string OutputExpr(const Node& n, const std::string& pin)
            {
                NodeDef def = m_Graph.ResolveDef(n);
                switch (def.Kind)
                {
                    case NodeKind::Event:
                    {
                        std::string f = Field("e", n.Id, pin);
                        m_HiddenFields.insert(f);
                        return f;
                    }
                    case NodeKind::Literal:
                    {
                        if (n.Type == "literal.key")
                        {
                            std::string key = SanitizeIdentifier(Prop(n, "Key", "Space"));
                            return "Key." + key;
                        }
                        PinType t = def.Outputs.empty() ? PinType::Any : def.Outputs[0].Type;
                        return LiteralFor(t, Prop(n, "Value"));
                    }
                    case NodeKind::Variable:
                    {
                        std::string var = Prop(n, "Variable");
                        if (var.empty()) { Error(n.Id, "Get node has no variable selected."); return "null"; }
                        return SanitizeIdentifier(var);
                    }
                    case NodeKind::Action:
                    case NodeKind::Flow:
                    {
                        if (n.Type == "var.set")
                            return SanitizeIdentifier(Prop(n, "Variable"));
                        if (n.Type == "flow.for" && pin == "Index") { m_HiddenNumberFields.insert(Field("i", n.Id)); return Field("i", n.Id); }
                        if (n.Type == "flow.foreach" && pin == "Index") { m_HiddenNumberFields.insert(Field("i", n.Id)); return Field("i", n.Id); }
                        if (n.Type == "flow.foreach" && pin == "Element") { m_HiddenFields.insert(Field("el", n.Id)); return Field("el", n.Id); }
                        if (n.Type == "flow.flipflop" && pin == "Is A") { m_HiddenFields.insert(Field("ff", n.Id)); return "(" + Field("ff", n.Id) + " != true)"; }
                        if (n.Type == "flow.don" && pin == "Counter") { m_HiddenNumberFields.insert(Field("dn", n.Id)); return Field("dn", n.Id); }
                        if (n.Type == "flow.timeline" && pin == "Value") { m_HiddenNumberFields.insert(Field("tlv", n.Id)); return Field("tlv", n.Id); }
                        if (n.Type == "flow.timeline" && pin == "Time") { m_HiddenNumberFields.insert(Field("tlt", n.Id)); return Field("tlt", n.Id); }
                        if (def.Kind == NodeKind::Flow)
                        {
                            // Pure akış düğümü (Select)
                            for (auto& o : def.Outputs)
                                if (o.Name == pin && !o.Default.empty())
                                    return FormatExpr(o.Default, InputExprs(n, def));
                        }
                        std::string f = Field("o", n.Id, pin);
                        m_HiddenFields.insert(f);
                        return f;
                    }
                    case NodeKind::Pure:
                    {
                        if (!m_DataStack.insert(n.Id).second)
                        {
                            Error(n.Id, "Data loop: this node's output feeds back into its own input.");
                            return "null";
                        }
                        std::string tmpl = def.Expression;
                        for (auto& o : def.Outputs)
                            if (o.Name == pin && !o.Default.empty()) tmpl = o.Default;
                        std::string r = FormatExpr(tmpl, InputExprs(n, def));
                        m_DataStack.erase(n.Id);
                        return r;
                    }
                    default:
                        return "null";
                }
            }

            std::vector<std::string> InputExprs(const Node& n, const NodeDef& def)
            {
                std::vector<std::string> args;
                for (auto& in : def.Inputs)
                {
                    if (in.Type == PinType::Exec) continue;
                    args.push_back(InputExpr(n, in));
                }
                return args;
            }

            std::string InputExpr(const Node& n, const PinDef& in)
            {
                if (const Link* l = m_Graph.FindInputLink(n.Id, in.Name))
                {
                    const Node* src = m_Graph.FindNode(l->FromNode);
                    if (!src)
                    {
                        Error(n.Id, "Input '" + in.Name + "' is connected to a missing node.");
                        return "null";
                    }
                    std::string expr = OutputExpr(*src, l->FromPin);
                    // Örtük dönüşümler (PinsCompatible ile izin verilenler)
                    PinType from = PinType::Any;
                    for (auto& o : m_Graph.ResolveDef(*src).Outputs)
                        if (o.Name == l->FromPin) from = o.Type;
                    if (from != in.Type && from != PinType::Any)
                    {
                        if (in.Type == PinType::String) return "str(" + expr + ")";
                        if (from == PinType::Number && in.Type == PinType::Bool) return "(" + expr + " != 0)";
                        if (from == PinType::Bool && in.Type == PinType::Number) return "(" + expr + " ? 1.0 : 0.0)";
                    }
                    return expr;
                }
                auto it = n.InputDefaults.find(in.Name);
                std::string raw = it != n.InputDefaults.end() ? it->second : in.Default;
                return LiteralFor(in.Type, raw);
            }

            // ── Akış ──
            // Blueprint hata ayıklayıcısı: düğüm yürütüldü
            void TraceNode(const Node& n, int indent)
            {
                if (m_Trace)
                    Line(Indent(indent) + "VSTrace(" + QuoteString(m_ClassName) + ", " + std::to_string(n.Id) + ");", n.Id);
            }

            void FollowExec(const Node& n, const std::string& pin, int indent)
            {
                for (const Link* l : m_Graph.FindOutputLinks(n.Id, pin))
                    if (const Node* target = m_Graph.FindNode(l->ToNode))
                        Exec(*target, indent, l->ToPin);
            }

            static std::string Num(double v)
            {
                char b[64];
                std::snprintf(b, sizeof(b), "%.9g", v);
                return FloatLiteral(b);
            }

            double TimelineLength(const Node& n) const { return std::max(std::atof(Prop(n, "Length", "1").c_str()), 0.001); }

            // Timeline anahtarları (zaman:değer) arasında doğrusal ara değer ifadesi
            std::string TimelineValueExpr(const Node& n, const std::string& t)
            {
                std::vector<std::pair<double, double>> keys;
                auto trim = [](const std::string& x) {
                    const size_t a = x.find_first_not_of(" \t"), b = x.find_last_not_of(" \t");
                    return a == std::string::npos ? std::string() : x.substr(a, b - a + 1);
                };
                for (auto& k : SplitList(Prop(n, "Keys", "")))
                {
                    const size_t c = k.find(':');
                    double kt = 0, kv = 0;
                    if (c == std::string::npos || !IsNumber(trim(k.substr(0, c)), &kt) || !IsNumber(trim(k.substr(c + 1)), &kv))
                    {
                        Warning(n.Id, "Timeline key '" + k + "' is not time:value.");
                        continue;
                    }
                    keys.push_back({ kt, kv });
                }
                if (keys.empty())
                    return "0.0";
                std::sort(keys.begin(), keys.end());
                std::string expr = Num(keys.back().second);
                for (size_t i = keys.size() - 1; i > 0; --i)
                {
                    const auto& [t0, v0] = keys[i - 1];
                    const auto& [t1, v1] = keys[i];
                    const std::string seg = t1 - t0 > 1e-9 ? "(" + Num(v0) + " + " + Num(v1 - v0) + " * ((" + t + " - " + Num(t0) + ") / " + Num(t1 - t0) + "))" : Num(v1);
                    expr = "(" + t + " <= " + Num(t1) + " ? " + seg + " : " + expr + ")";
                }
                return "(" + t + " <= " + Num(keys.front().first) + " ? " + Num(keys.front().second) + " : " + expr + ")";
            }

            // Delay / Timeline: Update başında geri sayım ve devam akışı
            void EmitLatentTick(const Node& n)
            {
                if (n.Type == "flow.timeline")
                {
                    const std::string p = Field("tlp", n.Id), dir = Field("tld", n.Id), t = Field("tlt", n.Id), v = Field("tlv", n.Id), fin = Field("tlf", n.Id);
                    m_HiddenFields.insert(p);
                    m_HiddenFields.insert(fin);
                    m_HiddenNumberFields.insert(dir);
                    m_HiddenNumberFields.insert(t);
                    m_HiddenNumberFields.insert(v);
                    const std::string L = Num(TimelineLength(n));
                    const bool loop = Prop(n, "Loop", "false") == "true";
                    Line("        if (" + p + " == true)", n.Id);
                    Line("        {", n.Id);
                    Line("            " + fin + " = false;", n.Id);
                    Line("            " + t + " += dt * " + dir + ";", n.Id);
                    Line("            if (" + dir + " > 0.0 && " + t + " >= " + L + ")", n.Id);
                    Line("            {", n.Id);
                    if (loop)
                        Line("                " + t + " -= " + L + ";", n.Id);
                    else
                    {
                        Line("                " + t + " = " + L + ";", n.Id);
                        Line("                " + p + " = false;", n.Id);
                        Line("                " + fin + " = true;", n.Id);
                    }
                    Line("            }", n.Id);
                    Line("            if (" + dir + " < 0.0 && " + t + " <= 0.0)", n.Id);
                    Line("            {", n.Id);
                    if (loop)
                        Line("                " + t + " += " + L + ";", n.Id);
                    else
                    {
                        Line("                " + t + " = 0.0;", n.Id);
                        Line("                " + p + " = false;", n.Id);
                        Line("                " + fin + " = true;", n.Id);
                    }
                    Line("            }", n.Id);
                    Line("            " + v + " = " + TimelineValueExpr(n, t) + ";", n.Id);
                    TraceNode(n, 3);
                    FollowExec(n, "Update", 3);
                    Line("            if (" + fin + " == true)", n.Id);
                    Line("            {", n.Id);
                    FollowExec(n, "Finished", 4);
                    Line("            }", n.Id);
                    Line("        }", n.Id);
                    return;
                }
                const std::string on = Field("dly", n.Id), t = Field("dlt", n.Id);
                m_HiddenFields.insert(on);
                m_HiddenNumberFields.insert(t);
                Line("        if (" + on + " == true)", n.Id);
                Line("        {", n.Id);
                Line("            " + t + " -= dt;", n.Id);
                Line("            if (" + t + " <= 0.0)", n.Id);
                Line("            {", n.Id);
                Line("                " + on + " = false;", n.Id);
                TraceNode(n, 4);
                FollowExec(n, "Completed", 4);
                Line("            }", n.Id);
                Line("        }", n.Id);
            }

            static std::string Indent(int n) { return std::string((size_t)n * 4, ' '); }

            void Exec(const Node& n, int indent, const std::string& inPin = "In")
            {
                if (!m_ExecStack.insert({ n.Id, inPin }).second)
                {
                    Error(n.Id, "Execution loop: the flow returns to this node. Use a While/For loop node instead.");
                    return;
                }
                NodeDef def = m_Graph.ResolveDef(n);
                const std::string ind = Indent(indent);
                TraceNode(n, indent);
                if (n.Type == "flow.doonce" && inPin == "Reset")
                {
                    Line(ind + Field("once", n.Id) + " = false;", n.Id);
                    m_HiddenFields.insert(Field("once", n.Id));
                }
                else if (n.Type == "flow.delay" || n.Type == "flow.retriggerabledelay")
                {
                    const std::string on = Field("dly", n.Id), t = Field("dlt", n.Id);
                    m_HiddenFields.insert(on);
                    m_HiddenNumberFields.insert(t);
                    if (n.Type == "flow.delay")
                    {
                        Line(ind + "if (" + on + " != true)", n.Id);
                        Line(ind + "{", n.Id);
                        Line(ind + "    " + on + " = true;", n.Id);
                        Line(ind + "    " + t + " = " + InputExpr(n, def.Inputs[1]) + ";", n.Id);
                        Line(ind + "}", n.Id);
                    }
                    else
                    {
                        Line(ind + on + " = true;", n.Id);
                        Line(ind + t + " = " + InputExpr(n, def.Inputs[1]) + ";", n.Id);
                    }
                }
                else if (n.Type == "flow.gate")
                {
                    const std::string f = Field("gate", n.Id);
                    m_HiddenFields.insert(f);
                    const std::string isOpen = Prop(n, "Start Closed", "false") == "true" ? "(" + f + " == true)" : "(" + f + " != false)";
                    if (inPin == "Open") Line(ind + f + " = true;", n.Id);
                    else if (inPin == "Close") Line(ind + f + " = false;", n.Id);
                    else if (inPin == "Toggle") Line(ind + f + " = " + isOpen + " ? false : true;", n.Id);
                    else
                    {
                        Line(ind + "if " + isOpen, n.Id);
                        Line(ind + "{", n.Id);
                        FollowExec(n, "Exit", indent + 1);
                        Line(ind + "}", n.Id);
                    }
                }
                else if (n.Type == "flow.multigate")
                {
                    int count = 0;   // bağlı en yüksek çıkışa kadar
                    for (int k = 0; k < 4; ++k)
                        if (!m_Graph.FindOutputLinks(n.Id, "Out " + std::to_string(k)).empty()) count = k + 1;
                    const bool random = Prop(n, "Random", "false") == "true", loop = Prop(n, "Loop", "false") == "true";
                    const std::string idx = Field("mg", n.Id), cnt = std::to_string(count) + ".0";
                    m_HiddenNumberFields.insert(idx);
                    std::vector<std::string> used;
                    for (int k = 0; k < count; ++k)
                    {
                        used.push_back(Field("mgu", n.Id, std::to_string(k)));
                        if (random) m_HiddenFields.insert(used.back());
                    }
                    if (inPin == "Reset")
                    {
                        Line(ind + idx + " = 0.0;", n.Id);
                        if (random)
                            for (auto& u : used) Line(ind + u + " = false;", n.Id);
                    }
                    else if (count > 0 && !random)
                    {
                        if (loop)
                        {
                            Line(ind + "if (" + idx + " >= " + cnt + ")", n.Id);
                            Line(ind + "{", n.Id);
                            Line(ind + "    " + idx + " = 0.0;", n.Id);
                            Line(ind + "}", n.Id);
                        }
                        Line(ind + "if (" + idx + " < " + cnt + ")", n.Id);
                        Line(ind + "{", n.Id);
                        Line(ind + "    " + idx + " += 1.0;", n.Id);
                        for (int k = 0; k < count; ++k)
                        {
                            Line(ind + (k ? "    else if (" : "    if (") + idx + " == " + std::to_string(k + 1) + ".0)", n.Id);
                            Line(ind + "    {", n.Id);
                            FollowExec(n, "Out " + std::to_string(k), indent + 2);
                            Line(ind + "    }", n.Id);
                        }
                        Line(ind + "}", n.Id);
                    }
                    else if (count > 0)
                    {
                        // Rastgele: henüz çalışmamış çıkışlardan biri
                        const std::string free = Field("mgk", n.Id), pick = Field("mgr", n.Id);
                        m_HiddenNumberFields.insert(free);
                        m_HiddenNumberFields.insert(pick);
                        Line(ind + free + " = 0.0;", n.Id);
                        for (auto& u : used)
                        {
                            Line(ind + "if (" + u + " != true)", n.Id);
                            Line(ind + "{", n.Id);
                            Line(ind + "    " + free + " += 1.0;", n.Id);
                            Line(ind + "}", n.Id);
                        }
                        if (loop)
                        {
                            Line(ind + "if (" + free + " == 0.0)", n.Id);
                            Line(ind + "{", n.Id);
                            for (auto& u : used) Line(ind + "    " + u + " = false;", n.Id);
                            Line(ind + "    " + free + " = " + cnt + ";", n.Id);
                            Line(ind + "}", n.Id);
                        }
                        Line(ind + "if (" + free + " > 0.0)", n.Id);
                        Line(ind + "{", n.Id);
                        Line(ind + "    " + pick + " = float(random(int(0), int(" + free + ")));", n.Id);
                        for (int k = 0; k < count; ++k)
                        {
                            Line(ind + "    if (" + used[(size_t)k] + " != true)", n.Id);
                            Line(ind + "    {", n.Id);
                            Line(ind + "        if (" + pick + " == 0.0)", n.Id);
                            Line(ind + "        {", n.Id);
                            Line(ind + "            " + used[(size_t)k] + " = true;", n.Id);
                            Line(ind + "            " + pick + " = -1.0;", n.Id);
                            FollowExec(n, "Out " + std::to_string(k), indent + 3);
                            Line(ind + "        }", n.Id);
                            Line(ind + "        else", n.Id);
                            Line(ind + "        {", n.Id);
                            Line(ind + "            " + pick + " -= 1.0;", n.Id);
                            Line(ind + "        }", n.Id);
                            Line(ind + "    }", n.Id);
                        }
                        Line(ind + "}", n.Id);
                    }
                }
                else if (n.Type == "flow.don")
                {
                    const std::string c = Field("dn", n.Id);
                    m_HiddenNumberFields.insert(c);
                    if (inPin == "Reset")
                        Line(ind + c + " = 0.0;", n.Id);
                    else
                    {
                        Line(ind + "if (" + c + " < " + InputExpr(n, def.Inputs[1]) + ")", n.Id);
                        Line(ind + "{", n.Id);
                        Line(ind + "    " + c + " += 1.0;", n.Id);
                        FollowExec(n, "Exit", indent + 1);
                        Line(ind + "}", n.Id);
                    }
                }
                else if (n.Type == "flow.switchint" || n.Type == "flow.switchstring")
                {
                    const bool isInt = n.Type == "flow.switchint";
                    const std::string f = Field("sw", n.Id);
                    m_HiddenFields.insert(f);
                    Line(ind + f + " = " + (isInt ? "float(int(" + InputExpr(n, def.Inputs[1]) + "))" : "str(" + InputExpr(n, def.Inputs[1]) + ")") + ";", n.Id);
                    bool first = true;
                    for (auto& o : def.Outputs)
                    {
                        if (o.Type != PinType::Exec || o.Name == "Default") continue;
                        if (isInt && !IsNumber(o.Name))
                        {
                            Warning(n.Id, "Case '" + o.Name + "' is not a number.");
                            continue;
                        }
                        Line(ind + (first ? "if (" : "else if (") + f + " == " + (isInt ? FloatLiteral(o.Name) : QuoteString(o.Name)) + ")", n.Id);
                        Line(ind + "{", n.Id);
                        FollowExec(n, o.Name, indent + 1);
                        Line(ind + "}", n.Id);
                        first = false;
                    }
                    if (!m_Graph.FindOutputLinks(n.Id, "Default").empty())
                    {
                        if (first)
                            FollowExec(n, "Default", indent);
                        else
                        {
                            Line(ind + "else", n.Id);
                            Line(ind + "{", n.Id);
                            FollowExec(n, "Default", indent + 1);
                            Line(ind + "}", n.Id);
                        }
                    }
                }
                else if (n.Type == "flow.timeline")
                {
                    const std::string p = Field("tlp", n.Id), dir = Field("tld", n.Id), t = Field("tlt", n.Id);
                    m_HiddenFields.insert(p);
                    m_HiddenNumberFields.insert(dir);
                    m_HiddenNumberFields.insert(t);
                    if (inPin == "Stop")
                        Line(ind + p + " = false;", n.Id);
                    else
                    {
                        const bool reverse = inPin == "Reverse" || inPin == "Reverse from End";
                        if (inPin == "Play from Start") Line(ind + t + " = 0.0;", n.Id);
                        if (inPin == "Reverse from End") Line(ind + t + " = " + Num(TimelineLength(n)) + ";", n.Id);
                        Line(ind + p + " = true;", n.Id);
                        Line(ind + dir + " = " + (reverse ? "-1.0" : "1.0") + ";", n.Id);
                    }
                }
                else if (n.Type == "flow.returnvalue")
                {
                    Line(ind + "return " + InputExpr(n, def.Inputs[1]) + ";", n.Id);
                }
                else if (n.Type == "action.callfunction" && Prop(n, "Function").empty())
                {
                    Error(n.Id, "Call Function has no function selected.");
                    FollowExec(n, "Then", indent);
                }
                else if (n.Type == "flow.branch")
                {
                    Line(ind + "if (" + InputExpr(n, def.Inputs[1]) + ")", n.Id);
                    Line(ind + "{", n.Id);
                    FollowExec(n, "True", indent + 1);
                    Line(ind + "}", n.Id);
                    if (!m_Graph.FindOutputLinks(n.Id, "False").empty())
                    {
                        Line(ind + "else", n.Id);
                        Line(ind + "{", n.Id);
                        FollowExec(n, "False", indent + 1);
                        Line(ind + "}", n.Id);
                    }
                }
                else if (n.Type == "flow.sequence")
                {
                    for (auto& o : def.Outputs)
                        if (o.Type == PinType::Exec) FollowExec(n, o.Name, indent);
                }
                else if (n.Type == "flow.for")
                {
                    std::string i = Field("i", n.Id);
                    m_HiddenNumberFields.insert(i);
                    Line(ind + "for (" + i + " = " + InputExpr(n, def.Inputs[1]) + "; " + i + " <= " + InputExpr(n, def.Inputs[2]) + "; " + i + "++)", n.Id);
                    Line(ind + "{", n.Id);
                    FollowExec(n, "Body", indent + 1);
                    Line(ind + "}", n.Id);
                    FollowExec(n, "Completed", indent);
                }
                else if (n.Type == "flow.foreach")
                {
                    std::string el = Field("el", n.Id), i = Field("i", n.Id), v = "_v" + std::to_string(n.Id);
                    m_HiddenFields.insert(el);
                    m_HiddenNumberFields.insert(i);
                    Line(ind + i + " = -1;", n.Id);
                    Line(ind + "foreach (var " + v + " in " + InputExpr(n, def.Inputs[1]) + ")", n.Id);
                    Line(ind + "{", n.Id);
                    Line(ind + "    " + el + " = " + v + ";", n.Id);
                    Line(ind + "    " + i + "++;", n.Id);
                    FollowExec(n, "Body", indent + 1);
                    Line(ind + "}", n.Id);
                    FollowExec(n, "Completed", indent);
                }
                else if (n.Type == "flow.while")
                {
                    Line(ind + "while (" + InputExpr(n, def.Inputs[1]) + ")", n.Id);
                    Line(ind + "{", n.Id);
                    FollowExec(n, "Body", indent + 1);
                    Line(ind + "}", n.Id);
                    FollowExec(n, "Completed", indent);
                }
                else if (n.Type == "flow.doonce")
                {
                    std::string f = Field("once", n.Id);
                    m_HiddenFields.insert(f);
                    Line(ind + "if (" + f + " != true)", n.Id);
                    Line(ind + "{", n.Id);
                    Line(ind + "    " + f + " = true;", n.Id);
                    FollowExec(n, "Then", indent + 1);
                    Line(ind + "}", n.Id);
                }
                else if (n.Type == "flow.flipflop")
                {
                    std::string f = Field("ff", n.Id);
                    m_HiddenFields.insert(f);
                    Line(ind + f + " = (" + f + " != true);", n.Id);
                    Line(ind + "if (" + f + ")", n.Id);
                    Line(ind + "{", n.Id);
                    FollowExec(n, "A", indent + 1);
                    Line(ind + "}", n.Id);
                    Line(ind + "else", n.Id);
                    Line(ind + "{", n.Id);
                    FollowExec(n, "B", indent + 1);
                    Line(ind + "}", n.Id);
                }
                else if (n.Type == "flow.return")
                {
                    Line(ind + "return;", n.Id);
                }
                else if (n.Type == "var.set")
                {
                    std::string var = Prop(n, "Variable");
                    if (var.empty())
                        Error(n.Id, "Set node has no variable selected.");
                    else
                        Line(ind + SanitizeIdentifier(var) + " = " + InputExpr(n, def.Inputs[1]) + ";", n.Id);
                    FollowExec(n, "Then", indent);
                }
                else if (def.Kind == NodeKind::Action)
                {
                    std::string call = FormatExpr(def.Expression, InputExprs(n, def));
                    bool hasResult = false;
                    for (auto& o : def.Outputs) if (o.Type != PinType::Exec) hasResult = true;
                    if (hasResult)
                    {
                        std::string f = Field("o", n.Id, "Result");
                        m_HiddenFields.insert(f);
                        Line(ind + f + " = " + call + ";", n.Id);
                    }
                    else
                        Line(ind + call + ";", n.Id);
                    FollowExec(n, "Then", indent);
                }
                else
                    Warning(n.Id, "This node cannot be executed (it has no execution input).");
                m_ExecStack.erase({ n.Id, inPin });
            }

            void EmitEventPrologue(const Node& n, const std::vector<std::pair<std::string, std::string>>& params, int indent)
            {
                // Olay parametrelerini alanlara kopyala (başka akışlardan okunabilsin)
                for (auto& [pin, param] : params)
                {
                    std::string f = Field("e", n.Id, pin);
                    m_HiddenFields.insert(f);
                    Line(Indent(indent) + f + " = " + param + ";", n.Id);
                }
            }

            void GenerateEvents()
            {
                // Olayları türlerine göre grupla (aynı türden birden çok düğüm tek fonksiyonda birleşir)
                std::vector<const Node*> start, awake, update, late, destroy, perFrame, enterState, exitState, functions, latent;
                std::map<std::string, std::vector<const Node*>> custom, physics;
                for (auto& n : m_Graph.Nodes)
                {
                    if (n.Type == "flow.delay" || n.Type == "flow.retriggerabledelay" || n.Type == "flow.timeline")
                        latent.push_back(&n);
                    if (n.Type == "event.collision" || n.Type == "event.trigger")
                    {
                        std::string phase = Prop(n, "Phase", "Enter");
                        if (phase != "Enter" && phase != "Stay" && phase != "Exit")
                        {
                            Warning(n.Id, "Unknown phase '" + phase + "', using Enter.");
                            phase = "Enter";
                        }
                        physics[std::string(n.Type == "event.collision" ? "OnCollision" : "OnTrigger") + phase + (Prop(n, "Physics", "3D") == "2D" ? "2D" : "")].push_back(&n);
                    }
                    else if (n.Type == "event.inputaction") perFrame.push_back(&n);
                    else if (n.Type == "event.function") functions.push_back(&n);
                    else if (n.Type == "event.start") start.push_back(&n);
                    else if (n.Type == "event.awake") awake.push_back(&n);
                    else if (n.Type == "event.update") update.push_back(&n);
                    else if (n.Type == "event.lateupdate") late.push_back(&n);
                    else if (n.Type == "event.destroy") destroy.push_back(&n);
                    else if (n.Type == "event.enterstate") enterState.push_back(&n);
                    else if (n.Type == "event.exitstate") exitState.push_back(&n);
                    else if (n.Type == "event.keydown" || n.Type == "event.keyheld" || n.Type == "event.mouse" || n.Type == "event.timer") perFrame.push_back(&n);
                    else if (n.Type == "event.custom")
                    {
                        std::string name = SanitizeIdentifier(Prop(n, "Name", "OnClick"));
                        custom[name].push_back(&n);
                    }
                    else if (!NodeLibrary::Find(n.Type))
                        Error(n.Id, "Unknown node type '" + n.Type + "'.");
                }
                auto simple = [&](const char* fn, const std::vector<const Node*>& nodes) {
                    if (nodes.empty()) return;
                    Line("", 0);
                    Line(std::string("    func ") + fn + "()", nodes.front()->Id);
                    Line("    {", nodes.front()->Id);
                    for (const Node* n : nodes)
                    {
                        TraceNode(*n, 2);
                        FollowExec(*n, "Then", 2);
                    }
                    Line("    }", 0);
                };
                simple("Awake", awake);
                simple("Start", start);
                if (!update.empty() || !perFrame.empty() || !latent.empty())
                {
                    Line("", 0);
                    Line("    func Update(dt)", !update.empty() ? update.front()->Id : !perFrame.empty() ? perFrame.front()->Id : latent.front()->Id);
                    Line("    {", 0);
                    // Gecikmeler / Timeline'lar önce: bu karede başlatılan bir geri sayım bir sonraki karede ilerler (Unreal gibi)
                    for (const Node* n : latent)
                        EmitLatentTick(*n);
                    for (const Node* n : update)
                    {
                        EmitEventPrologue(*n, { { "Delta Time", "dt" } }, 2);
                        TraceNode(*n, 2);
                        FollowExec(*n, "Then", 2);
                    }
                    for (const Node* n : perFrame)
                    {
                        if (n->Type == "event.inputaction")
                        {
                            const std::string action = QuoteString(Prop(*n, "Action", "Jump"));
                            const std::string phase = Prop(*n, "Phase", "Performed");
                            const char* fn = phase == "Started" ? "InputActionStarted"
                                           : (phase == "Canceled" || phase == "Completed") ? "InputActionCanceled" : "InputActionPerformed";
                            Line(std::string("        if (") + fn + "(" + action + "))", n->Id);
                            Line("        {", n->Id);
                            EmitEventPrologue(*n, { { "Value", "InputActionVector(" + action + ")" } }, 3);
                            TraceNode(*n, 3);
                            FollowExec(*n, "Then", 3);
                            Line("        }", n->Id);
                            continue;
                        }
                        if (n->Type == "event.timer")
                        {
                            std::string t = Field("t", n->Id);
                            m_HiddenNumberFields.insert(t);
                            std::string interval = FloatLiteral(Prop(*n, "Interval", "1.0"));
                            Line("        " + t + " += dt;", n->Id);
                            Line("        if (" + t + " >= " + interval + ")", n->Id);
                            Line("        {", n->Id);
                            Line("            " + t + " -= " + interval + ";", n->Id);
                            TraceNode(*n, 3);
                            FollowExec(*n, "Then", 3);
                            Line("        }", n->Id);
                            continue;
                        }
                        std::string cond;
                        if (n->Type == "event.mouse")
                            cond = "IsMouseButtonDown(" + std::to_string(std::atoi(Prop(*n, "Button", "0").c_str())) + ")";
                        else
                            cond = std::string(n->Type == "event.keydown" ? "IsKeyDown" : "IsKeyPressed") + "(Key." + SanitizeIdentifier(Prop(*n, "Key", "Space")) + ")";
                        Line("        if (" + cond + ")", n->Id);
                        Line("        {", n->Id);
                        if (n->Type == "event.keyheld")
                            EmitEventPrologue(*n, { { "Delta Time", "dt" } }, 3);
                        TraceNode(*n, 3);
                        FollowExec(*n, "Then", 3);
                        Line("        }", n->Id);
                    }
                    Line("    }", 0);
                }
                if (!late.empty())
                {
                    Line("", 0);
                    Line("    func LateUpdate(dt)", late.front()->Id);
                    Line("    {", 0);
                    for (const Node* n : late)
                    {
                        EmitEventPrologue(*n, { { "Delta Time", "dt" } }, 2);
                        TraceNode(*n, 2);
                        FollowExec(*n, "Then", 2);
                    }
                    Line("    }", 0);
                }
                simple("OnDestroy", destroy);
                simple("OnEnterState", enterState);
                simple("OnExitState", exitState);
                static const std::set<std::string> reserved = { "Start", "Awake", "Update", "LateUpdate", "OnDestroy", "OnUpdate", "OnCreate", "OnEnterState", "OnExitState", "init" };
                // Fizik olayları (Unity adları; Unreal Event Hit / Begin Overlap / End Overlap)
                for (auto& [method, nodes] : physics)
                {
                    Line("", 0);
                    Line("    func " + method + "(other, otherName)", nodes.front()->Id);
                    Line("    {", 0);
                    for (const Node* n : nodes)
                    {
                        std::vector<std::pair<std::string, std::string>> params = { { "Other", "other" }, { "Other Name", "otherName" } };
                        if (n->Type == "event.collision")
                        {
                            params.push_back({ "Normal", "GetContactNormal()" });
                            params.push_back({ "Impulse", "GetContactImpulse()" });
                        }
                        EmitEventPrologue(*n, params, 2);
                        TraceNode(*n, 2);
                        FollowExec(*n, "Then", 2);
                    }
                    Line("    }", 0);
                }
                // Fonksiyonlar: girişler _a0, _a1 ... parametreleri; giriş düğümünün çıkışları bu değerleri verir
                std::set<std::string> functionNames;
                for (const Node* n : functions)
                {
                    const std::string name = SanitizeIdentifier(Prop(*n, "Name", "MyFunction"));
                    if (reserved.count(name) || custom.count(name) || physics.count(name) || !functionNames.insert(name).second)
                    {
                        Error(n->Id, "Function name '" + name + "' is already used by another event or function.");
                        continue;
                    }
                    const auto inputs = SplitList(Prop(*n, "Inputs", ""));
                    std::string signature;
                    std::vector<std::pair<std::string, std::string>> params;
                    for (size_t i = 0; i < inputs.size(); ++i)
                    {
                        const std::string arg = "_a" + std::to_string(i);
                        signature += (i ? ", " : "") + arg;
                        params.push_back({ inputs[i], arg });
                    }
                    Line("", 0);
                    Line("    func " + name + "(" + signature + ")", n->Id);
                    Line("    {", 0);
                    EmitEventPrologue(*n, params, 2);
                    TraceNode(*n, 2);
                    FollowExec(*n, "Then", 2);
                    Line("    }", 0);
                }
                for (auto& [name, nodes] : custom)
                {
                    if (reserved.count(name) || physics.count(name))
                    {
                        Error(nodes.front()->Id, "Custom event name '" + name + "' is reserved; use the matching event node instead.");
                        continue;
                    }
                    Line("", 0);
                    Line("    func " + name + "(value = null, kind = \"\")", nodes.front()->Id);
                    Line("    {", 0);
                    for (const Node* n : nodes)
                    {
                        EmitEventPrologue(*n, { { "Value", "value" }, { "Kind", "kind" } }, 2);
                        TraceNode(*n, 2);
                        FollowExec(*n, "Then", 2);
                    }
                    Line("    }", 0);
                }
                if (start.empty() && awake.empty() && update.empty() && late.empty() && destroy.empty() && perFrame.empty() && custom.empty() && enterState.empty() && exitState.empty()
                    && physics.empty() && functions.empty())
                    Warning(0, "The graph has no event nodes; nothing will run. Add 'On Start' or 'On Update'.");
            }
        };
    }

    std::string Graph::Transpile(const std::string& className, std::vector<CompileMessage>& messages, std::vector<int>* lineToNode, bool trace) const
    {
        if (Macros.empty())
        {
            Transpiler t(*this, messages, trace);
            return t.Run(SanitizeIdentifier(className), lineToNode);
        }
        // Blueprint Macro: örnekler gövdeleriyle açılır (Unreal'daki gibi derlemede satır içi); açılan düğümlerin hataları ve satır
        // eşlemesi makro düğümüne bağlanır
        std::unordered_map<int, int> expanded;
        Graph flat = ExpandMacros(messages, &expanded);
        const size_t firstMessage = messages.size();
        Transpiler t(flat, messages, trace);
        std::string code = t.Run(SanitizeIdentifier(className), lineToNode);
        for (size_t i = firstMessage; i < messages.size(); ++i)
            if (auto it = expanded.find(messages[i].NodeId); it != expanded.end())
                messages[i].NodeId = it->second;
        if (lineToNode)
            for (int& node : *lineToNode)
                if (auto it = expanded.find(node); it != expanded.end())
                    node = it->second;
        return code;
    }
}

namespace Conqueror::Visual
{
    namespace
    {
        struct DebugState
        {
            bool Enabled = false;
            std::unordered_map<std::string, std::string> Paths;
            std::unordered_map<std::string, std::unordered_map<int, std::pair<double, uint64_t>>> Hits;   // zaman, sayı
            std::unordered_map<std::string, std::unordered_set<int>> Breakpoints;
            bool BreakPending = false;
            std::string BreakGraph;
            int BreakNode = 0;
        };
        DebugState& D() { static auto& d = *new DebugState(); return d; }
    }

    void Debugger::SetEnabled(bool enabled) { D().Enabled = enabled; }
    bool Debugger::IsEnabled() { return D().Enabled; }
    void Debugger::RegisterGraph(const std::string& graph, const std::string& path) { D().Paths[graph] = path; }
    std::string Debugger::GetGraphPath(const std::string& graph)
    {
        auto it = D().Paths.find(graph);
        return it != D().Paths.end() ? it->second : std::string();
    }

    double Debugger::Now()
    {
        using namespace std::chrono;
        static const auto start = steady_clock::now();
        return duration<double>(steady_clock::now() - start).count();
    }

    void Debugger::Trace(const std::string& graph, int node)
    {
        DebugState& d = D();
        if (!d.Enabled)
            return;
        auto& hit = d.Hits[graph][node];
        hit.first = Now();
        ++hit.second;
        if (!d.BreakPending)
        {
            auto it = d.Breakpoints.find(graph);
            if (it != d.Breakpoints.end() && it->second.count(node))
            {
                d.BreakPending = true;
                d.BreakGraph = graph;
                d.BreakNode = node;
            }
        }
    }

    double Debugger::LastExecuted(const std::string& graph, int node)
    {
        auto g = D().Hits.find(graph);
        if (g == D().Hits.end()) return -1.0;
        auto n = g->second.find(node);
        return n == g->second.end() ? -1.0 : n->second.first;
    }

    uint64_t Debugger::HitCount(const std::string& graph, int node)
    {
        auto g = D().Hits.find(graph);
        if (g == D().Hits.end()) return 0;
        auto n = g->second.find(node);
        return n == g->second.end() ? 0 : n->second.second;
    }

    void Debugger::ClearHits()
    {
        D().Hits.clear();
        D().BreakPending = false;
    }

    void Debugger::SetBreakpoint(const std::string& graph, int node, bool enabled)
    {
        if (enabled) D().Breakpoints[graph].insert(node);
        else D().Breakpoints[graph].erase(node);
    }

    bool Debugger::HasBreakpoint(const std::string& graph, int node)
    {
        auto it = D().Breakpoints.find(graph);
        return it != D().Breakpoints.end() && it->second.count(node) != 0;
    }

    bool Debugger::ConsumeBreak(std::string& graph, int& node)
    {
        DebugState& d = D();
        if (!d.BreakPending)
            return false;
        d.BreakPending = false;
        graph = d.BreakGraph;
        node = d.BreakNode;
        return true;
    }
}
