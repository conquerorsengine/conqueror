#include "NodeAssetPanel.h"

#include "Scripting/Visual/StateGraph.h"
#include "Core/Audio/SoundCue.h"
#include "Scene/Foliage.h"
#include "AISystem/BehaviorTree/BehaviorTreeAsset.h"
#include "Particles/VisualEffect.h"

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

    namespace
    {
        void RegisterSoundCue()
        {
            NodeAssetSchema s;
            s.Kind = SoundCues::Kind;
            s.Extension = SoundCues::Extension;
            s.Title = "Sound Cue";
            s.Icon = ICON_FA_MUSIC;
            s.Hint = "Wave Players flow through Random, Mixer, Modulator and Delay nodes into Output. Assign the cue to an Audio Source Clip.";
            s.MenuCategory = "Audio";
            s.MenuTooltip = "A node graph that builds a sound each time it plays. Assign it to an Audio Source's Clip slot.";
            s.Color = IM_COL32(255, 150, 80, 255);
            const ImU32 source = IM_COL32(150, 92, 30, 255), logic = IM_COL32(85, 88, 98, 255), output = IM_COL32(165, 45, 45, 255);
            const std::vector<std::string> audio = { ".wav", ".mp3", ".ogg", ".flac" };

            NodeAssetType out;
            out.Id = "output"; out.Title = "Output"; out.Category = "Output"; out.Color = output;
            out.Inputs = { "In" }; out.Single = true; out.SingleInput = true;
            out.Props = { Number("Volume", "1"), Number("Pitch", "1") };

            NodeAssetType wave;
            wave.Id = "wave"; wave.Title = "Wave Player"; wave.Category = "Sources"; wave.Color = source;
            wave.Outputs = { "Out" }; wave.TitleProp = "Clip";
            wave.Props = { Asset("Clip", audio), Flag("Looping") };

            NodeAssetType random;
            random.Id = "random"; random.Title = "Random"; random.Category = "Logic"; random.Color = logic;
            random.Inputs = { "A", "B", "C", "D" }; random.Outputs = { "Out" };
            random.Description = "Picks one connected input each time, by weight.";
            random.Props = { Number("Weight A", "1"), Number("Weight B", "1"), Number("Weight C", "1"), Number("Weight D", "1"),
                             Flag("No Repeats", "false", "Never picks the same input twice in a row.") };

            NodeAssetType sequence;
            sequence.Id = "sequence"; sequence.Title = "Sequence"; sequence.Category = "Logic"; sequence.Color = logic;
            sequence.Inputs = { "A", "B", "C", "D" }; sequence.Outputs = { "Out" };
            sequence.Description = "Each play takes the next connected input.";

            NodeAssetType mixer;
            mixer.Id = "mixer"; mixer.Title = "Mixer"; mixer.Category = "Logic"; mixer.Color = logic;
            mixer.Inputs = { "A", "B", "C", "D" }; mixer.Outputs = { "Out" };
            mixer.Description = "Plays all connected inputs together.";
            mixer.Props = { Number("Volume A", "1"), Number("Volume B", "1"), Number("Volume C", "1"), Number("Volume D", "1") };

            NodeAssetType modulator;
            modulator.Id = "modulator"; modulator.Title = "Modulator"; modulator.Category = "Modify"; modulator.Color = logic;
            modulator.Inputs = { "In" }; modulator.Outputs = { "Out" }; modulator.SingleInput = true;
            modulator.Description = "Random volume and pitch on each play.";
            modulator.Props = { Number("Volume Min", "1"), Number("Volume Max", "1"), Number("Pitch Min", "1"), Number("Pitch Max", "1") };

            NodeAssetType delay;
            delay.Id = "delay"; delay.Title = "Delay"; delay.Category = "Modify"; delay.Color = logic;
            delay.Inputs = { "In" }; delay.Outputs = { "Out" }; delay.SingleInput = true;
            delay.Props = { Number("Min", "0.1"), Number("Max", "0.1") };

            NodeAssetType loop;
            loop.Id = "loop"; loop.Title = "Looping"; loop.Category = "Modify"; loop.Color = logic;
            loop.Inputs = { "In" }; loop.Outputs = { "Out" }; loop.SingleInput = true;

            s.Types = { out, wave, random, sequence, mixer, modulator, delay, loop };
            s.Populate = [](NodeAssets::File& f) {
                int o = f.Add("output", 500.0f, 80.0f);
                int w = f.Add("wave", 60.0f, 80.0f);
                f.Connect(w, 0, o, 0);
            };
            s.Validate = [](const NodeAssets::File& f) -> std::string {
                int outputs = 0;
                for (const auto& n : f.Nodes)
                    outputs += n.Type == "output" ? 1 : 0;
                return outputs == 0 ? "Add an Output node." : "";
            };
            NodeAssetPanel::Register(std::move(s));
        }
    }

    namespace
    {
        void RegisterBehaviorTree()
        {
            NodeAssetSchema s;
            s.Kind = BehaviorTrees::Kind;
            s.Extension = BehaviorTrees::Extension;
            s.Title = "Behavior Tree";
            s.Icon = ICON_FA_SITEMAP;
            s.Hint = "Root flows down through composites and decorators to tasks; children run left to right. Add a Behavior Tree component to use it.";
            s.MenuCategory = "AI";
            s.MenuTooltip = "AI logic as a tree of composites, decorators and tasks. Add a Behavior Tree component to run it.";
            s.Color = IM_COL32(120, 180, 255, 255);
            const ImU32 composite = IM_COL32(85, 88, 98, 255), decorator = IM_COL32(118, 62, 140, 255), task = IM_COL32(45, 82, 158, 255);
            auto make = [](const char* id, const char* title, const char* category, ImU32 color, std::vector<NodeAssetProp> props, bool hasChildren, const char* description = "") {
                NodeAssetType t;
                t.Id = id; t.Title = title; t.Category = category; t.Color = color; t.Description = description;
                t.Inputs = { "In" }; t.SingleInput = true;
                if (hasChildren) t.Outputs = { "Children" };
                t.Props = std::move(props);
                return t;
            };
            NodeAssetType root;
            root.Id = "root"; root.Title = "Root"; root.Category = "Tree"; root.Color = IM_COL32(165, 45, 45, 255);
            root.Outputs = { "Out" }; root.Single = true;
            root.Props = { Asset("Blackboard", { ".cqblackboard" }, "The keys this tree can read and write.") };

            const std::vector<std::string> operators = { "Is Set", "Is Not Set", "Equals", "Not Equals", "Greater", "Less" };
            s.Types = {
                root,
                make("selector", "Selector", "Composites", composite, {}, true, "Runs children until one succeeds."),
                make("sequence", "Sequence", "Composites", composite, {}, true, "Runs children until one fails."),
                make("parallel", "Parallel", "Composites", composite, { Choice("Finish When", { "All Children Finish", "Any Child Finishes" }) }, true, "Runs all children at once."),
                make("condition", "Blackboard Condition", "Decorators", decorator, { Text("Key"), Choice("Operator", operators), Text("Value") }, true, "Runs its child only while the key test passes."),
                make("invert", "Invert", "Decorators", decorator, {}, true),
                make("forcesuccess", "Force Success", "Decorators", decorator, {}, true),
                make("cooldown", "Cooldown", "Decorators", decorator, { Number("Seconds", "1") }, true, "Fails while cooling down after the child finishes."),
                make("loop", "Loop", "Decorators", decorator, { Number("Count", "0", "0 loops forever.") }, true),
                make("timelimit", "Time Limit", "Decorators", decorator, { Number("Seconds", "5") }, true, "Fails if the child takes longer."),
                make("wait", "Wait", "Tasks", task, { Number("Seconds", "1"), Number("Random Deviation", "0") }, false),
                make("moveto", "Move To", "Tasks", task, { Text("Target Key", "", "A Vector key."), Number("Acceptance", "0.5"), Number("Speed", "3", "Used without a NavMesh Agent.") }, false),
                make("setkey", "Set Blackboard Key", "Tasks", task, { Text("Key"), Text("Value") }, false),
                make("eqs", "Run EQS Query", "Tasks", task, { Asset("Query", { ".cqeqs" }), Text("Result Key", "", "A Vector key receiving the best point.") }, false),
                make("script", "Run Script Method", "Tasks", task, { Text("Method", "", "Called on this object's scripts with the delta time. Return false to fail, \"running\" to continue.") }, false),
                make("log", "Log", "Tasks", task, { Text("Text") }, false),
            };
            s.Populate = [](NodeAssets::File& f) {
                int r = f.Add("root", 300.0f, 40.0f);
                int seq = f.Add("sequence", 300.0f, 220.0f);
                int wait = f.Add("wait", 120.0f, 400.0f);
                int log = f.Add("log", 480.0f, 400.0f);
                f.Find(log)->Props["Text"] = "Hello from the Behavior Tree";
                f.Connect(r, 0, seq, 0);
                f.Connect(seq, 0, wait, 0);
                f.Connect(seq, 0, log, 0);
            };
            s.Validate = [](const NodeAssets::File& f) -> std::string {
                for (const auto& n : f.Nodes)
                    if (n.Type == "root")
                        return f.Out(n.Id).empty() ? "Connect the Root to a node." : "";
                return "Add a Root node.";
            };
            s.ActiveNodes = [](const std::string& path) { return BehaviorTrees::ActiveNodes(path); };
            NodeAssetPanel::Register(std::move(s));
        }
    }

    namespace
    {
        void RegisterPcg()
        {
            NodeAssetSchema s;
            s.Kind = Foliage::GraphKind;
            s.Extension = Foliage::GraphExtension;
            s.Title = "PCG Graph";
            s.Icon = ICON_FA_DIAGRAM_PROJECT;
            s.Hint = "Points flow from a Surface Sampler through filters into Spawn nodes. Assign the graph to a Foliage Volume and press Generate.";
            s.MenuCategory = "Foliage";
            s.MenuTooltip = "Procedural placement as a node graph (Unreal PCG). Runs inside a Foliage Volume.";
            s.Color = IM_COL32(170, 200, 110, 255);
            const ImU32 source = IM_COL32(45, 118, 72, 255), filter = IM_COL32(85, 88, 98, 255), spawn = IM_COL32(45, 82, 158, 255);
            auto make = [](const char* id, const char* title, const char* category, ImU32 color, std::vector<NodeAssetProp> props, bool input, bool output, const char* description = "") {
                NodeAssetType t;
                t.Id = id; t.Title = title; t.Category = category; t.Color = color; t.Description = description;
                if (input) t.Inputs = { "In" };
                if (output) t.Outputs = { "Out" };
                t.Props = std::move(props);
                return t;
            };
            const std::vector<std::string> models = { ".glb", ".gltf", ".fbx", ".obj", ".dae", ".blend", ".3ds", ".cqmesh" };
            s.Types = {
                make("sampler", "Surface Sampler", "Sources", source, { Number("Spacing", "2", "Meters between points."), Number("Jitter", "0.5", "Random offset, as a fraction of Spacing."), Number("Slope Max", "60", "Skip steeper ground.") }, false, true, "Points over the Foliage Volume, resting on the Terrain."),
                make("noise", "Density Noise", "Filters", filter, { Number("Scale", "10", "Meters per noise cell."), Number("Seed", "0") }, true, true, "Gives each point a smooth 0 to 1 density."),
                make("filter", "Density Filter", "Filters", filter, { Number("Min", "0.5"), Number("Max", "1") }, true, true, "Keeps the points whose density is in range."),
                make("random", "Random Choice", "Filters", filter, { Number("Keep", "0.5", "Fraction of the points to keep.") }, true, true),
                make("transform", "Transform Points", "Filters", filter, { Number("Scale Min", "1"), Number("Scale Max", "1"), Number("Height Offset", "0"), Flag("Random Yaw", "true") }, true, true),
                make("mesh", "Spawn Mesh", "Output", spawn, { Asset("Mesh", models) }, true, false, "One instanced mesh at every point."),
                make("prefab", "Spawn Prefab", "Output", spawn, { Asset("Prefab", { ".cqprefab" }) }, true, false, "A prefab copy at every point (at most 2000)."),
            };
            s.Populate = [](NodeAssets::File& f) {
                int a = f.Add("sampler", 40.0f, 100.0f);
                int b = f.Add("transform", 360.0f, 100.0f);
                int c = f.Add("mesh", 680.0f, 100.0f);
                f.Connect(a, 0, b, 0);
                f.Connect(b, 0, c, 0);
            };
            NodeAssetPanel::Register(std::move(s));
        }
    }

    namespace
    {
        // Unity VFX Graph: bağlamlar (Spawn -> Initialize Particle -> Update Particle -> Output Particle); bloklar bağlam özellikleri
        void RegisterVFXGraph()
        {
            NodeAssetSchema s;
            s.Kind = VisualEffects::Kind;
            s.Extension = VisualEffects::Extension;
            s.Title = "VFX Graph";
            s.Icon = ICON_FA_WAND_SPARKLES;
            s.Hint = "Spawn > Initialize Particle > Update Particle > Output Particle. Each Initialize starts a particle system; add the asset to a Visual Effect component.";
            s.MenuCategory = "Visual Effects";
            s.MenuTooltip = "A GPU particle effect graph (Unity VFX Graph). Play it with a Visual Effect component.";
            s.Color = IM_COL32(255, 160, 80, 255);

            NodeAssetType spawn;
            spawn.Id = "spawn";
            spawn.Title = "Spawn";
            spawn.Category = "Contexts";
            spawn.Description = "How many particles are born: a constant rate and bursts.";
            spawn.Color = IM_COL32(150, 90, 40, 255);
            spawn.Outputs = { "Spawn" };
            spawn.Props = { Number("Rate", "50", "Particles per second."), Number("Burst Count", "0", "Particles born at once."),
                            Number("Burst Delay", "0", "Seconds before the first burst."), Number("Burst Repeat", "0", "Seconds between bursts (0 = once).") };

            NodeAssetType init;
            init.Id = "initialize";
            init.Title = "Initialize Particle";
            init.Category = "Contexts";
            init.Description = "Where particles start and with what speed, life, size and color.";
            init.Color = IM_COL32(70, 110, 160, 255);
            init.Inputs = { "Spawn" };
            init.Outputs = { "Particles" };
            init.Props = { Number("Capacity", "2000", "Maximum living particles (GPU memory)."),
                           Number("Lifetime Min", "1"), Number("Lifetime Max", "2"),
                           Choice("Shape", { "Point", "Sphere", "Box", "Cone", "Circle" }),
                           Text("Shape Size", "1 1 1", "Sphere / Cone / Circle: radius (first value). Box: size."),
                           Number("Cone Angle", "25"),
                           Choice("Velocity", { "Direction", "From Shape", "Random" }),
                           Text("Direction", "0 1 0"), Number("Speed Min", "1"), Number("Speed Max", "3"),
                           Number("Size Min", "0.1"), Number("Size Max", "0.2"),
                           Text("Color", "1 0.6 0.2 1", "R G B A (above 1 glows)."), Number("Color Variation", "0"),
                           Number("Bounds Size", "10", "Box around the effect, used by temporal anti-aliasing.") };

            NodeAssetType update;
            update.Id = "update";
            update.Title = "Update Particle";
            update.Category = "Contexts";
            update.Description = "Forces every frame: gravity, drag, turbulence, ground collision.";
            update.Color = IM_COL32(70, 140, 90, 255);
            update.Inputs = { "Particles" };
            update.Outputs = { "Particles" };
            update.Props = { Text("Gravity", "0 -9.81 0"), Number("Drag", "0"), Number("Turbulence", "0"), Number("Turbulence Frequency", "1"),
                             Flag("Ground Collision", "false"), Number("Ground Height", "0"), Number("Bounce", "0.3") };

            NodeAssetType output;
            output.Id = "output";
            output.Title = "Output Particle Quad";
            output.Category = "Contexts";
            output.Description = "How particles are drawn: blending, texture, fading.";
            output.Color = IM_COL32(140, 70, 120, 255);
            output.Inputs = { "Particles" };
            output.Props = { Choice("Blend", { "Additive", "Alpha", "Premultiplied" }), Asset("Texture", { ".png", ".jpg", ".jpeg", ".tga" }),
                             Choice("Render", { "Billboard", "Stretched Billboard" }), Number("Stretch", "0.1", "Stretched: length per m/s."),
                             Text("Color At End", "1 1 1 0", "Color multiplier at the end of life (fades with alpha 0)."),
                             Number("Size At End", "1", "Size multiplier at the end of life."), Number("Emissive", "1") };

            s.Types = { spawn, init, update, output };
            s.Populate = [](NodeAssets::File& f) {
                int sp = f.Add("spawn", 40.0f, 80.0f);
                int in = f.Add("initialize", 320.0f, 80.0f);
                int up = f.Add("update", 640.0f, 80.0f);
                int out = f.Add("output", 960.0f, 80.0f);
                f.Connect(sp, 0, in, 0);
                f.Connect(in, 0, up, 0);
                f.Connect(up, 0, out, 0);
            };
            s.Validate = [](const NodeAssets::File& f) -> std::string {
                for (const auto& n : f.Nodes)
                    if (n.Type == "initialize")
                        return f.Out(n.Id).empty() ? "Connect Initialize Particle to Update Particle or an Output." : "";
                return "Add an Initialize Particle context.";
            };
            NodeAssetPanel::Register(std::move(s));
        }
    }

    void RegisterBuiltinNodeAssetSchemas()
    {
        RegisterVFXGraph();
        if (!NodeAssetPanel::Schemas().empty())
            return;
        RegisterStateGraph();
        RegisterSoundCue();
        RegisterBehaviorTree();
        RegisterPcg();
    }
}
