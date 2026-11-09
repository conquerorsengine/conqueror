#include "SimpleAsset.h"

#include <IconsFontAwesome6.h>

// Tabloyla tanımlanan varlık türleri (bkz. SimpleAsset.h). Çalışma zamanı kullanımları ilgili sistemlerde.
namespace Conqueror::SimpleAssets
{
    namespace
    {
        constexpr uint32_t Rgb(uint32_t r, uint32_t g, uint32_t b) { return 0xff000000u | (b << 16) | (g << 8) | r; }

        Field F(const char* name, float def, const char* tip = "", float mn = 0.0f, float mx = 0.0f)
        {
            Field f;
            f.Name = name;
            f.Type = FieldType::Float;
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%g", def);
            f.Default = buf;
            f.Tooltip = tip;
            f.Min = mn;
            f.Max = mx;
            return f;
        }

        Field I(const char* name, int def, const char* tip = "", int mn = 0, int mx = 0)
        {
            Field f = F(name, (float)def, tip, (float)mn, (float)mx);
            f.Type = FieldType::Int;
            return f;
        }

        Field B(const char* name, bool def, const char* tip = "")
        {
            Field f;
            f.Name = name;
            f.Type = FieldType::Bool;
            f.Default = def ? "true" : "false";
            f.Tooltip = tip;
            return f;
        }

        Field T(const char* name, const char* def = "", const char* tip = "")
        {
            Field f;
            f.Name = name;
            f.Type = FieldType::Text;
            f.Default = def;
            f.Tooltip = tip;
            return f;
        }

        Field Long(const char* name, const char* def = "", const char* tip = "")
        {
            Field f = T(name, def, tip);
            f.Type = FieldType::Multiline;
            return f;
        }

        Field C(const char* name, const char* def = "1 1 1 1", const char* tip = "")
        {
            Field f = T(name, def, tip);
            f.Type = FieldType::Color;
            return f;
        }

        Field V(const char* name, const char* def = "0 0 0", const char* tip = "")
        {
            Field f = T(name, def, tip);
            f.Type = FieldType::Vec3;
            return f;
        }

        Field E(const char* name, std::vector<std::string> options, const char* tip = "", int selected = 0)
        {
            Field f;
            f.Name = name;
            f.Type = FieldType::Choice;
            f.Default = options[(size_t)selected];
            f.Options = std::move(options);
            f.Tooltip = tip;
            return f;
        }

        Field A(const char* name, std::vector<std::string> exts, const char* tip = "")
        {
            Field f;
            f.Name = name;
            f.Type = FieldType::Asset;
            f.Options = std::move(exts);
            f.Tooltip = tip;
            return f;
        }

        Field Tab(const char* name, std::vector<Column> columns, const char* tip = "")
        {
            Field f;
            f.Name = name;
            f.Type = FieldType::Table;
            f.Columns = std::move(columns);
            f.Tooltip = tip;
            return f;
        }

        Column Col(const char* name, const char* def = "") { return { name, FieldType::Text, def, {} }; }
        Column ColF(const char* name, const char* def = "0") { return { name, FieldType::Float, def, {} }; }
        Column ColB(const char* name, const char* def = "false") { return { name, FieldType::Bool, def, {} }; }
        Column ColE(const char* name, std::vector<std::string> options) { std::string d = options.front(); return { name, FieldType::Choice, d, std::move(options) }; }
        Column ColA(const char* name, std::vector<std::string> exts) { return { name, FieldType::Asset, "", std::move(exts) }; }

        const std::vector<std::string> kPlatforms = { "Windows", "Linux", "macOS", "Android", "iOS", "Web" };
    }

    std::vector<Def> BuildDefs()
    {
        std::vector<Def> d;
        auto add = [&](const char* kind, const char* ext, const char* label, const char* category, const char* submenu, const char* icon, uint32_t color,
                       const char* tooltip, std::vector<Field> fields) {
            Def def;
            def.Kind = kind;
            def.Extension = ext;
            def.Label = label;
            def.Category = category;
            def.Submenu = submenu;
            def.Icon = icon;
            def.Color = color;
            def.Tooltip = tooltip;
            def.Fields = std::move(fields);
            d.push_back(std::move(def));
        };

        add("NativeScriptModule", ".cqmodule", "Native Script Module", "Scripting", "", ICON_FA_CUBES, Rgb(110, 175, 255),
            "Groups the C and C++ scripts of its folder (Unity Assembly Definition): platforms, defines and references apply to those scripts only.",
            { B("Enabled", true, "A disabled module's scripts are left out of the build."),
              Tab("Defines", { Col("Name"), Col("Value", "1") }, "Preprocessor definitions for this module's scripts only."),
              Tab("References", { Col("Module") }, "Other modules this one needs. If one is disabled or missing, this module is left out too."),
              Tab("Include Directories", { Col("Folder") }, "Extra include folders, relative to the project."),
              Tab("Exclude Platforms", { ColE("Platform", kPlatforms) }, "The module's scripts are not compiled on these platforms."),
              Tab("Define Constraints", { Col("Symbol") }, "Compiled only when every symbol here is defined in Project Settings > Script Execution Order > Scripting Define Symbols.") });

        add("SavedSearch", ".cqsearch", "Search", "~", "", ICON_FA_MAGNIFYING_GLASS, Rgb(170, 190, 220),
            "A saved Content Browser search: double-click it to run it again.",
            { T("Query", "", "The text typed in the Content Browser search box.") });

        // Animasyon (Unreal)
        add("Skeleton", ".cqskeleton", "Skeleton", "Animation", "", ICON_FA_BONE, Rgb(255, 150, 190),
            "A character's bone hierarchy, taken from a model. IK Rigs and Retargeters refer to it.",
            { A("Model", { ".glb", ".gltf", ".fbx", ".dae" }, "The skinned model.") });
        add("PoseAsset", ".cqpose", "Pose Asset", "Animation", "", ICON_FA_PERSON, Rgb(255, 150, 190),
            "Named poses captured from a clip. Add a Pose Asset Player to blend one over the animation.",
            { A("Model", { ".glb", ".gltf", ".fbx", ".dae" }, "Used by Capture Pose."), T("Clip", "", "The clip to capture from."), F("Time", 0.0f, "Seconds into the clip."), T("Pose Name", "Pose"),
              Tab("Bones", { Col("Pose"), Col("Bone"), ColF("TX"), ColF("TY"), ColF("TZ"), ColF("QX"), ColF("QY"), ColF("QZ"), ColF("QW", "1") }, "Filled by Capture Pose.") });
        add("AimOffset", ".cqaimoffset", "Aim Offset", "Animation", "", ICON_FA_CROSSHAIRS, Rgb(255, 150, 190),
            "Additive poses arranged by yaw and pitch, layered over the animation by an Aim Offset Player.",
            { A("Model", { ".glb", ".gltf", ".fbx", ".dae" }, "The skinned model these clips belong to."), T("Base Clip", "", "Poses are taken relative to the first frame of this clip."),
              Tab("Samples", { Col("Clip"), ColF("Yaw"), ColF("Pitch") }) });
        add("IKRig", ".cqikrig", "IK Rig", "Animation", "", ICON_FA_DIAGRAM_NEXT, Rgb(255, 150, 190),
            "Bone chains that reach for target objects. Add an IK Rig component to the model.",
            { A("Skeleton", { ".cqskeleton" }, "The skeleton these bones belong to."),
              Tab("Chains", { Col("Name"), Col("Root Bone"), Col("Tip Bone"), Col("Target Object"), ColF("Weight", "1") }, "Target Object is found by name when the game starts.") });
        add("IKRetargeter", ".cqretarget", "IK Retargeter", "Animation", "", ICON_FA_PERSON_RUNNING, Rgb(255, 150, 190),
            "Plays one skeleton's animation on another. Add a Retarget Player to the target model.",
            { A("Source Skeleton", { ".cqskeleton", ".glb", ".gltf", ".fbx", ".dae" }, "The skeleton that has the clip."), T("Clip", "", "The clip to play."),
              B("Copy Root Translation", false), F("Translation Scale", 1.0f, "Scales the root movement for different body sizes."),
              Tab("Bone Map", { Col("Source Bone"), Col("Target Bone") }, "Leave empty to match bones with the same names.") });

        // Doku ve malzeme (Unreal)
        add("TextureArray", ".cqtexarray", "Texture 2D Array", "Rendering", "Texture", ICON_FA_LAYER_GROUP, Rgb(130, 200, 250),
            "Several textures in one, chosen by index in a Shader Graph (Texture Array Sample node).",
            { I("Size", 512, "Every slice is resized to this.", 4, 4096), Tab("Slices", { ColA("Texture", { ".png", ".jpg", ".jpeg", ".tga", ".bmp" }) }) });
        add("VolumeTexture", ".cqvoltex", "Volume Texture", "Rendering", "Texture", ICON_FA_CUBE, Rgb(130, 200, 250),
            "A 3D texture from a sheet of slices. Sample it with a Volume Texture Sample node in a Shader Graph.",
            { A("Source", { ".png", ".jpg", ".jpeg", ".tga", ".bmp" }, "A grid of slices, left to right, top to bottom."), I("Columns", 4, "", 1, 64), I("Rows", 4, "", 1, 64) });
        add("CubeRenderTarget", ".cqcubert", "Cube Render Target", "Rendering", "Texture", ICON_FA_CUBE, Rgb(130, 200, 250),
            "A cubemap a Scene Capture Cube component renders into; read it with a Cube Render Target Sample node.",
            { I("Size", 256, "Pixels per face.", 16, 2048) });
        add("CanvasRenderTarget", ".cqcanvasrt", "Canvas Render Target", "Rendering", "Texture", ICON_FA_PAINTBRUSH, Rgb(130, 200, 250),
            "A texture scripts draw on: CanvasBegin, CanvasClear, CanvasDrawRect, CanvasDrawText, CanvasDrawTexture, CanvasEnd. Use it like a Render Texture.",
            { I("Width", 512, "", 1, 4096), I("Height", 512, "", 1, 4096) });
        add("MaterialParameterCollection", ".cqmpc", "Material Parameter Collection", "Shader", "", ICON_FA_SLIDERS, Rgb(190, 120, 255),
            "Global values shared by Shader Graphs (Collection Parameter node). Scripts change them with MPCSetScalar and MPCSetVector.",
            { Tab("Scalars", { Col("Name"), ColF("Value", "0") }), Tab("Vectors", { Col("Name"), ColF("R", "1"), ColF("G", "1"), ColF("B", "1"), ColF("A", "1") }) });
        add("MaterialLayer", ".cqmatlayer", "Material Layer", "Shader", "", ICON_FA_LAYER_GROUP, Rgb(190, 120, 255),
            "One layer of a layered material. Add it in a Material's Layers list.",
            { C("Tint"), A("Albedo", { ".png", ".jpg", ".jpeg", ".tga", ".bmp" }), F("Metallic", 0.0f, "", 0.0f, 1.0f), F("Roughness", 0.5f, "", 0.0f, 1.0f) });
        add("MaterialLayerBlend", ".cqmatblend", "Material Layer Blend", "Shader", "", ICON_FA_LAYER_GROUP, Rgb(190, 120, 255),
            "How a Material Layer mixes over the layers below it.",
            { E("Mode", { "Mask Texture", "Vertex Color", "Slope" }), A("Mask", { ".png", ".jpg", ".jpeg", ".tga", ".bmp" }, "Mask Texture mode: white shows this layer."),
              F("Threshold", 0.5f, "", 0.0f, 1.0f), F("Softness", 0.1f, "", 0.0f, 1.0f) });

        // Görüntü (Unreal)
        add("SubUVAnimation", ".cqsubuv", "Sub UV Animation", "Rendering", "", ICON_FA_TABLE_CELLS, Rgb(255, 180, 120),
            "A sprite sheet for particles. Assign it to a Particle System's Sub UV slot.",
            { A("Texture", { ".png", ".jpg", ".jpeg", ".tga", ".bmp" }), I("Columns", 4, "", 1, 64), I("Rows", 4, "", 1, 64), F("Cycles", 1.0f, "How many times the sheet plays over a particle's life.") });
        add("LightProfile", ".cqlightprofile", "Light Profile", "Rendering", "", ICON_FA_LIGHTBULB, Rgb(255, 210, 110),
            "The angular falloff of a light. Add a Light Profile component to a Spot or Point Light.",
            { A("Source", { ".ies" }, "An IES photometric file. Leave empty to use Points."), F("Brightness", 1.0f, "Multiplies the profile."),
              Tab("Points", { ColF("Angle"), ColF("Intensity", "1") }, "Degrees from the light axis and relative intensity, in increasing angle order.") });

        add("PhysicsAsset", ".cqphysicsasset", "Physics Asset", "Physics", "", ICON_FA_PERSON_FALLING, Rgb(120, 200, 120),
            "Which bones of a skeleton get physics bodies, and how. Assign it to a Ragdoll component.",
            { A("Preview Model", { ".glb", ".gltf", ".fbx", ".dae" }, "Used by Generate Bodies."),
              Tab("Bodies", { Col("Bone"), ColF("Radius", "0"), ColF("Mass", "0"), ColF("Swing Limit", "0"), ColF("Twist Limit", "0") },
                  "0 uses the Ragdoll component's automatic value.") });

        // Bitki örtüsü (Unreal Foliage)
        add("FoliageType", ".cqfoliage", "Foliage Type", "Foliage", "", ICON_FA_TREE, Rgb(150, 200, 110),
            "One kind of plant or rock: mesh, density and placement rules. Used by a Procedural Foliage Spawner.",
            { A("Mesh", { ".glb", ".gltf", ".fbx", ".obj", ".dae", ".blend", ".3ds", ".cqmesh" }), F("Density", 10.0f, "Instances per 100 square meters."), F("Scale Min", 0.8f), F("Scale Max", 1.2f), B("Random Yaw", true),
              F("Slope Min", 0.0f, "Degrees.", 0.0f, 90.0f), F("Slope Max", 45.0f, "Degrees.", 0.0f, 90.0f) });
        add("GrassType", ".cqgrasstype", "Landscape Grass Type", "Foliage", "", ICON_FA_SEEDLING, Rgb(150, 200, 110),
            "Grass that grows where a Terrain layer is painted. Assign it to a Foliage Volume.",
            { I("Terrain Layer", 0, "The painted Terrain layer it grows on.", 0, 3),
              Tab("Varieties", { ColA("Mesh", { ".glb", ".gltf", ".fbx", ".obj", ".dae", ".blend", ".3ds", ".cqmesh" }), ColF("Density", "100"), ColF("Scale Min", "0.8"), ColF("Scale Max", "1.2") }) });
        add("FoliageSpawner", ".cqfoliagespawner", "Procedural Foliage Spawner", "Foliage", "", ICON_FA_WAND_MAGIC_SPARKLES, Rgb(150, 200, 110),
            "A set of Foliage Types scattered together. Assign it to a Foliage Volume and press Generate.",
            { I("Random Seed", 1), Tab("Foliage Types", { ColA("Foliage Type", { ".cqfoliage" }), ColF("Density Scale", "1") }) });

        // Medya
        add("MediaSource", ".cqmediasource", "Media Source", "Media", "", ICON_FA_FILM, Rgb(200, 120, 255),
            "A video to play. Assign it to a Video Player's Source, or list it in a Media Playlist.",
            { A("File", { ".avi", ".gif", ".mp4", ".mkv", ".mov", ".webm" }, "The video file.") });
        add("MediaPlaylist", ".cqmediaplaylist", "Media Playlist", "Media", "", ICON_FA_LIST, Rgb(200, 120, 255),
            "Videos played one after another.",
            { Tab("Items", { ColA("Source", { ".cqmediasource", ".avi", ".gif", ".mp4", ".mkv", ".mov", ".webm" }) }) });
        add("MediaPlayer", ".cqmediaplayer", "Media Player", "Media", "", ICON_FA_CIRCLE_PLAY, Rgb(200, 120, 255),
            "A playlist with playback options. Assign it to a Video Player's Source.",
            { A("Playlist", { ".cqmediaplaylist" }), B("Loop", false, "Start the playlist again after the last item."), B("Shuffle", false, "Random order."),
              F("Rate", 1.0f, "Playback speed.", 0.1f, 4.0f) });

        // Oyun (Unreal Misc)
        add("CameraShake", ".cqcamerashake", "Camera Shake", "Miscellaneous", "", ICON_FA_CAMERA, Rgb(170, 190, 220),
            "A reusable camera shake. Scripts call CameraShakePlay(path, scale).",
            { F("Amplitude", 1.0f, "Meters of positional shake."), F("Frequency", 12.0f, "Hz."), F("Duration", 0.4f, "Seconds."), F("Radius", 1000.0f, "Cameras farther away feel nothing.") });
        add("ForceFeedback", ".cqforcefeedback", "Force Feedback Effect", "Miscellaneous", "", ICON_FA_GAMEPAD, Rgb(170, 190, 220),
            "Gamepad vibration over time. Scripts call ForceFeedbackPlay(path).",
            { B("Looping", false), Tab("Keys", { ColF("Time"), ColF("Left", "0.5"), ColF("Right", "0.5") }, "Motor strengths at each time; values between keys are interpolated.") });
        add("TouchInterface", ".cqtouchinterface", "Touch Interface Setup", "Miscellaneous", "", ICON_FA_HAND_POINTER, Rgb(170, 190, 220),
            "On-screen touch controls that drive the gamepad input. Scripts call TouchInterfaceActivate(path).",
            { Tab("Controls", { Col("Name", "Move"), ColE("Type", { "Joystick", "Button" }), ColF("X", "0.15"), ColF("Y", "0.2"), ColF("Radius", "0.12"),
                                ColE("Control", { "Left Stick", "Right Stick", "A", "B", "X", "Y", "Left Bumper", "Right Bumper", "Back", "Start" }) },
                  "X and Y are fractions of the screen, measured from the lower left; Radius is a fraction of the screen height.") });

        // Paper2D
        add("Flipbook", ".cqflipbook", "Flipbook", "2D", "", ICON_FA_FILM, Rgb(120, 200, 255),
            "A sprite animation. Add a Flipbook Player next to a Sprite Renderer to play it.",
            { F("Frames Per Second", 12.0f), B("Loop", true), Tab("Frames", { ColA("Sprite", { ".png", ".jpg", ".jpeg", ".tga", ".bmp" }), ColF("Hold", "1") }, "Hold is how many frames this sprite stays.") });

        // Yapay zekâ
        add("Blackboard", ".cqblackboard", "Blackboard", "AI", "", ICON_FA_DATABASE, Rgb(120, 180, 255),
            "The memory of a Behavior Tree: named values shared by its tasks and decorators. Scripts use BlackboardSet / BlackboardGet.",
            { Tab("Keys", { Col("Name"), ColE("Type", { "Number", "Bool", "String", "Vector" }), Col("Default") }) });

        // Ses (Unreal)
        add("SoundAttenuation", ".cqattenuation", "Sound Attenuation", "Audio", "", ICON_FA_VOLUME_LOW, Rgb(255, 170, 90),
            "How a sound fades with distance. Assign it in a Sound Settings component.",
            { E("Model", { "Inverse", "Linear", "Exponential", "None" }), F("Min Distance", 1.0f, "Full volume inside this distance."),
              F("Max Distance", 50.0f, "Silent or minimum volume beyond this distance."), F("Rolloff", 1.0f), B("Spatialize", true, "Position the sound in 3D.") });
        add("SoundClass", ".cqsoundclass", "Sound Class", "Audio", "", ICON_FA_LAYER_GROUP, Rgb(255, 170, 90),
            "A shared volume and pitch: all Audio Sources of a class follow it, through its parent chain.",
            { F("Volume", 1.0f, "", 0.0f, 2.0f), F("Pitch", 1.0f, "", 0.1f, 3.0f), A("Parent", { ".cqsoundclass" }, "Volume and pitch multiply through the parents.") });
        add("SoundMix", ".cqsoundmix", "Sound Mix", "Audio", "", ICON_FA_SLIDERS, Rgb(255, 170, 90),
            "Temporarily changes Sound Classes. Scripts call SoundMixPush(path) and SoundMixPop(path).",
            { F("Fade In", 0.2f), F("Fade Out", 0.2f), F("Duration", 0.0f, "Pops itself after this many seconds; 0 keeps it until popped."),
              Tab("Adjusters", { ColA("Sound Class", { ".cqsoundclass" }), ColF("Volume", "1"), ColF("Pitch", "1") }) });
        add("SoundConcurrency", ".cqconcurrency", "Sound Concurrency", "Audio", "", ICON_FA_LAYER_GROUP, Rgb(255, 170, 90),
            "Limits how many sounds using this asset play together.",
            { I("Max Count", 4, "", 1, 64), E("Resolution Rule", { "Stop Oldest", "Stop Farthest", "Stop Quietest", "Prevent New" }) });
        add("ReverbEffect", ".cqreverb", "Reverb Effect", "Audio", "", ICON_FA_WAVE_SQUARE, Rgb(255, 170, 90),
            "Reverb settings for an Audio Source. Assign it in a Sound Settings component.",
            { F("Decay Time", 1.49f, "Seconds.", 0.1f, 20.0f), F("Decay HF Ratio", 0.83f, "", 0.1f, 2.0f), F("Density", 100.0f, "%", 0.0f, 100.0f),
              F("Diffusion", 100.0f, "%", 0.0f, 100.0f), F("Dry Level", 0.0f, "dB"), F("Room", -1000.0f, "dB"), F("Reverb Level", 200.0f, "dB") });
        add("DialogueWave", ".cqdialogue", "Dialogue Wave", "Audio", "", ICON_FA_COMMENT, Rgb(255, 170, 90),
            "A voice line with its subtitle. Scripts call DialoguePlay(path).",
            { T("Speaker", ""), Long("Spoken Text", ""), A("Voice", { ".wav", ".mp3", ".ogg", ".flac" }, "The recorded line."), F("Duration", 0.0f, "Subtitle seconds; 0 estimates from the text.") });

        // Metin (TextMeshPro karşılıkları)
        add("FontAsset", ".cqfontasset", "Font Asset", "UI", "Text", ICON_FA_FONT, Rgb(235, 170, 90),
            "A signed-distance font: stays sharp at any size. Use it in a Text component's Font slot.",
            { A("Source Font", { ".ttf", ".otf" }, "The font file its glyphs come from."),
              E("Atlas Population", { "Dynamic", "Static" }, "Dynamic adds characters as text needs them. Static keeps only the Characters list."),
              Long("Characters", "", "Characters prepared when the font loads."),
              Tab("Fallback Fonts", { ColA("Font", { ".ttf", ".otf" }) }, "Searched in order for characters the source font lacks."),
              F("Line Spacing", 1.0f, "Multiplies the font's line height.", 0.5f, 3.0f) });
        add("SpriteAsset", ".cqspriteasset", "Sprite Asset", "UI", "Text", ICON_FA_FACE_SMILE, Rgb(235, 170, 90),
            "Inline images for rich text: write <sprite name=coin> in a Text component with Rich Text on.",
            { Tab("Sprites", { Col("Name"), ColA("Texture", { ".png", ".jpg", ".jpeg", ".tga", ".bmp" }), ColF("Scale", "1") }) });
        add("TextColorGradient", ".cqcolorgradient", "Color Gradient", "UI", "Text", ICON_FA_PALETTE, Rgb(235, 170, 90),
            "A four-corner color preset for each character. Assign it to a Text component or use <gradient=Name> in rich text.",
            { E("Mode", { "Four Corners", "Horizontal", "Vertical", "Single" }), C("Top Left"), C("Top Right"), C("Bottom Left"), C("Bottom Right") });
        add("TextStyleSheet", ".cqtextstyle", "Text Style Sheet", "UI", "Text", ICON_FA_FILE_LINES, Rgb(235, 170, 90),
            "Named sets of rich text tags. <style=Heading>Text</style> expands to the tags of the Heading row.",
            { Tab("Styles", { Col("Name"), Col("Opening Tags"), Col("Closing Tags") }) });

        // Eski (Legacy)
        add("CustomFont", ".cqcustomfont", "Custom Font", "UI", "Legacy", ICON_FA_FONT, Rgb(200, 160, 110),
            "A bitmap font from a texture grid. Use it in a Text component's Font slot.",
            { A("Texture", { ".png", ".jpg", ".jpeg", ".tga", ".bmp" }, "Cells run left to right, top to bottom."),
              I("Columns", 16, "", 1, 64), I("Rows", 16, "", 1, 64),
              Long("Characters", "", "The characters in the cells, in order."),
              F("Spacing", 0.0f, "Extra space between characters, as a fraction of the font size."),
              F("Line Spacing", 1.0f, "", 0.5f, 3.0f),
              Tab("Character Widths", { Col("Character"), ColF("Width", "1") }, "Character width as a fraction of the cell width.") });
        add("GUISkin", ".cqguiskin", "GUI Skin", "UI", "Legacy", ICON_FA_PALETTE, Rgb(200, 160, 110),
            "One look for a whole UI: add a UI Skin component to a Canvas and every widget under it uses these colors when the game plays.",
            { A("Font", { ".ttf", ".otf", ".cqfontasset", ".cqcustomfont" }), C("Text Color", "0.1 0.1 0.12 1"),
              C("Panel Color", "1 1 1 0.55"), A("Panel Background", { ".png", ".jpg", ".jpeg", ".tga", ".bmp" }),
              C("Button Color", "1 1 1 1"), A("Button Background", { ".png", ".jpg", ".jpeg", ".tga", ".bmp" }),
              C("Normal", "1 1 1 1"), C("Highlighted", "0.92 0.92 0.92 1"), C("Pressed", "0.75 0.75 0.75 1"), C("Disabled", "0.78 0.78 0.78 0.5"),
              C("Slider Background", "0.8 0.8 0.82 1"), C("Slider Fill", "0.26 0.59 0.98 1"), C("Slider Handle", "1 1 1 1"),
              C("Input Background", "1 1 1 1") });

        // UI Toolkit
        add("PanelSettings", ".cqpanelsettings", "Panel Settings", "UI", "UI Toolkit", ICON_FA_SLIDERS, Rgb(120, 200, 255),
            "How UI Documents are scaled and themed. Assign it to a UI Document component.",
            { E("Scale Mode", { "Scale With Screen Size", "Constant Pixel Size" }), V("Reference Resolution", "1920 1080 0", "Width and height (the third value is unused)."),
              F("Match", 0.5f, "0 follows the width, 1 the height.", 0.0f, 1.0f), I("Sort Order", 0, "Higher panels draw on top."),
              A("Theme", { ".cqtheme" }, "Style sheets applied to every document on this panel.") });
        add("UITheme", ".cqtheme", "Theme Style Sheet", "UI", "UI Toolkit", ICON_FA_SWATCHBOOK, Rgb(120, 200, 255),
            "Base style sheets and variables shared by every UI Document on a panel.",
            { A("Inherits", { ".cqtheme" }, "Another theme applied first."),
              Tab("Style Sheets", { ColA("Style Sheet", { ".cquss" }) }), Tab("Variables", { Col("Name"), Col("Value") }, "Used in style sheets as var(--name).") });

        return d;
    }
}
