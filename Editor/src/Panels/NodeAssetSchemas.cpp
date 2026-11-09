#include "NodeAssetPanel.h"

#include "Scripting/Visual/StateGraph.h"

#include <IconsFontAwesome6.h>

namespace Conqueror::Editor
{
    namespace
    {
        using Prop = NodeAssetProp;
        using K = NodeAssetProp::Kind;

        Prop Text(const char* name, const char* def = "", const char* tip = "") { return { name, K::Text, def, tip, {} }; }
        Prop Number(const char* name, const char* def = "0", const char* tip = "") { return { name, K::Number, def, tip, {} }; }
        Prop Flag(const char* name, const char* def = "false", const char* tip = "") { return { name, K::Bool, def, tip, {} }; }
        Prop Choice(const char* name, std::vector<std::string> options, const char* tip = "")
        {
            std::string def = options.empty() ? std::string() : options.front();
            return { name, K::Choice, def, tip, std::move(options) };
        }
        Prop Asset(const char* name, std::vector<std::string> exts, const char* tip = "") { return { name, K::Asset, "", tip, std::move(exts) }; }

        void RegisterStateGraph()
        {
            NodeAssetSchema s;
            s.Kind = Scripting::StateGraphs::Kind;
            s.Extension = Scripting::StateGraphs::Extension;
            s.Title = "State Graph";
            s.Icon = ICON_FA_DIAGRAM_NEXT;
            s.Hint = "Connect State > Transition > State. Each state runs its own script; add this asset to a Script component.";
            s.MenuCategory = "Scripting";
            s.MenuTooltip = "A state machine whose states each run a script (Unity State Graph / Unreal State Tree). Add it to a Script component.";
            s.Color = IM_COL32(120, 220, 200, 255);

            NodeAssetType state;
            state.Id = "state";
            state.Title = "State";
            state.Category = "States";
            state.Description = "Runs its script while active: On Enter State, On Update and On Exit State events fire only here.";
            state.Color = IM_COL32(45, 118, 110, 255);
            state.Inputs = { "In" };
            state.Outputs = { "Out" };
            state.TitleProp = "Name";
            state.Props = { Text("Name", "State"), Asset("Script", { "@scripts" }, "The script or Visual Script this state runs."),
                            Flag("Entry", "false", "The state the graph starts in. Without one, the first state is used.") };

            NodeAssetType any;
            any.Id = "anystate";
            any.Title = "Any State";
            any.Category = "States";
            any.Description = "Its transitions can fire from whichever state is active.";
            any.Color = IM_COL32(118, 62, 140, 255);
            any.Outputs = { "Out" };
            any.Single = true;

            NodeAssetType transition;
            transition.Id = "transition";
            transition.Title = "Transition";
            transition.Category = "Transitions";
            transition.Description = "Moves from the connected state to the next one when its condition holds.";
            transition.Color = IM_COL32(150, 92, 30, 255);
            transition.Inputs = { "From" };
            transition.Outputs = { "To" };
            transition.TitleProp = "When";
            transition.Props = {
                Choice("When", { "After Seconds", "Key Pressed", "Trigger", "Variable Is True", "Variable Is False" }),
                Text("Value", "1", "After Seconds: the number of seconds. Key Pressed: a key name such as Space. Trigger: the name passed to "
                                   "StateGraphTrigger(). Variable: the name set with StateGraphSetVariable() or in the Script component.")
            };
            s.Types = { state, any, transition };
            s.Populate = [](NodeAssets::File& f) {
                int idle = f.Add("state", 40.0f, 80.0f);
                f.Find(idle)->Props["Name"] = "Idle";
                f.Find(idle)->Props["Entry"] = "true";
                int t = f.Add("transition", 340.0f, 80.0f);
                int active = f.Add("state", 640.0f, 80.0f);
                f.Find(active)->Props["Name"] = "Active";
                f.Connect(idle, 0, t, 0);
                f.Connect(t, 0, active, 0);
            };
            s.Validate = [](const NodeAssets::File& f) -> std::string {
                int states = 0, entries = 0;
                for (const auto& n : f.Nodes)
                    if (n.Type == "state")
                    {
                        ++states;
                        entries += n.Bool("Entry") ? 1 : 0;
                    }
                if (states == 0)
                    return "Add at least one State.";
                if (entries > 1)
                    return "More than one state is marked Entry; the last one is used.";
                return "";
            };
            s.ActiveNodes = [](const std::string& path) { return Scripting::StateGraphs::ActiveNodes(path); };
            NodeAssetPanel::Register(std::move(s));
        }
    }

    void RegisterBuiltinNodeAssetSchemas()
    {
        if (!NodeAssetPanel::Schemas().empty())
            return;
        RegisterStateGraph();
    }
}
