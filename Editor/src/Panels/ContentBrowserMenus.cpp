// Content Browser menüleri (Unity Project penceresi sağ tık menüsü + Create menüsü):
//  - En üstte arama kutusu (bütün menü girişlerinde, Create alt menüleri dahil) ve Asset Options ikon satırı
//    (Kes, Kopyala, Yapıştır, Sil, Yeniden Adlandır, Özellikler).
//  - "Other Options related to asset type": seçili varlığın türüne özel işlemler.
//  - Project bölümü: Create > (kategorili), Open Containing Folder, Open, Open Scene Additive, Delete, Rename, Copy Path,
//    View in Addon Manager, Create Addon Package, Import Package > Custom Package, Export Package, Export As Addon Package,
//    Import New Asset, Extract Materials, Find References In Scene / Project, Select Dependencies, Refresh, Reimport,
//    Reimport All, Open Script Project, View in Import Activity Window, Properties.
#include "ContentBrowserPanel.h"
#include "Panels/NodeAssetPanel.h"
#include "Core/Assets/SimpleAsset.h"

#include "AssetActions.h"
#include "EditorState.h"
#include "Panels/AssetInspector.h"
#include "UI/EditorUI.h"

#include "Addons/AddonManager.h"
#include "Core/Logging/Log.h"
#include "Core/Project/Project.h"
#include "Core/Serialization/PrefabSerializer.h"
#include "Core/Serialization/SceneSerializer.h"
#include "Core/Utils/Archive.h"
#include "Core/Utils/ImportActivity.h"
#include "Renderer/RHI/Texture.h"
#include "Renderer/SpriteAtlas.h"
#include "Renderer/Utilities/Renderer3D/Material.h"
#include "Renderer/Utilities/Renderer3D/MaterialAsset.h"
#include "Renderer/Utilities/Renderer3D/ModelLoader.h"
#include "Scripting/DataAssets.h"
#include "Scripting/ScriptRuntime.h"

#include <IconsFontAwesome6.h>
#include <imgui.h>
#include <nfd.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <set>
#include <sstream>

namespace Conqueror::Editor
{
    namespace fs = std::filesystem;

    namespace
    {
        std::string Lower(std::string s)
        {
            for (char& c : s)
                c = (char)std::tolower((unsigned char)c);
            return s;
        }

        bool ContainsNoCase(const std::string& haystack, const std::string& needle)
        {
            return Lower(haystack).find(Lower(needle)) != std::string::npos;
        }

        bool IsModelFile(const fs::path& p)
        {
            static const char* exts[] = { ".fbx", ".obj", ".gltf", ".glb", ".dae", ".3ds", ".blend", ".ply", ".stl" };
            const std::string e = Lower(p.extension().string());
            return std::any_of(std::begin(exts), std::end(exts), [&](const char* x) { return e == x; });
        }

        bool IsImageFile(const fs::path& p)
        {
            static const char* exts[] = { ".png", ".jpg", ".jpeg", ".tga", ".bmp", ".psd", ".gif", ".hdr", ".exr" };
            const std::string e = Lower(p.extension().string());
            return std::any_of(std::begin(exts), std::end(exts), [&](const char* x) { return e == x; });
        }

        bool IsAudioFile(const fs::path& p)
        {
            static const char* exts[] = { ".wav", ".mp3", ".ogg", ".flac" };
            const std::string e = Lower(p.extension().string());
            return std::any_of(std::begin(exts), std::end(exts), [&](const char* x) { return e == x; });
        }

        // Başka varlıklara yol ile başvurabilen metin dosyaları (sahne, prefab, malzeme, betik ...)
        bool IsTextAsset(const fs::path& p)
        {
            const std::string e = Lower(p.extension().string());
            if (e.rfind(".cq", 0) == 0 || e == ".yaml" || e == ".json" || e == ".txt" || e == ".md" || e == ".uss" || e == ".uxml")
                return true;
            return Scripting::ScriptRuntimes::IsScriptFile(p.string()) || DataAssets::IsCppHeader(p.string());
        }

        std::string ReadText(const fs::path& p, size_t limit = 32u << 20)
        {
            std::error_code ec;
            if (fs::file_size(p, ec) > limit)
                return {};
            std::ifstream in(p, std::ios::binary);
            std::stringstream ss;
            ss << in.rdbuf();
            return ss.str();
        }

        std::vector<uint8_t> ReadBytes(const fs::path& p)
        {
            std::ifstream in(p, std::ios::binary);
            return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        }

        fs::path ProjectDir()
        {
            return Project::GetActive() ? Project::GetActiveProjectDirectory() : fs::path();
        }

        std::string ProjectRelative(const fs::path& p)
        {
            std::error_code ec;
            const fs::path rel = fs::relative(p, ProjectDir(), ec);
            return ec ? std::string() : rel.generic_string();
        }

        // Dosyaya eşlik eden yan dosyalar (içe aktarma ayarı, sprite kesimi, atlas dokusu)
        std::vector<fs::path> Sidecars(const fs::path& p)
        {
            std::vector<fs::path> out;
            std::error_code ec;
            for (const char* ext : { ".meta", ".cqsprite" })
                if (fs::exists(p.string() + ext, ec))
                    out.push_back(p.string() + ext);
            if (Lower(p.extension().string()) == SpriteAtlases::Extension)
                for (const char* ext : { ".png", ".png.meta" })
                    if (fs::exists(p.string() + ext, ec))
                        out.push_back(p.string() + ext);
            return out;
        }

        // Metindeki proje yolları ("Assets/..." / "Packages/..."), var olan dosyalar
        void FindPathsInText(const std::string& text, const fs::path& projectDir, std::vector<fs::path>& out)
        {
            for (const char* root : { "Assets/", "Packages/" })
            {
                const size_t rootLen = std::strlen(root);
                for (size_t pos = text.find(root); pos != std::string::npos; pos = text.find(root, pos + rootLen))
                {
                    if (pos > 0)
                    {
                        const char b = text[pos - 1];
                        if (!(b == ' ' || b == '"' || b == '\'' || b == ':' || b == '[' || b == '{' || b == ',' || b == '(' || b == '\n' || b == '\t'))
                            continue;
                    }
                    size_t end = pos;
                    while (end < text.size() && text[end] != '\n' && text[end] != '\r' && text[end] != '"' && text[end] != '\'')
                        ++end;
                    std::string candidate = text.substr(pos, end - pos);
                    // Uzundan kısaya: sondaki boşluk / ayraçlar atılır, akış YAML'ında ',' ']' '}' ')' üzerinde kesilir
                    std::vector<size_t> cuts{ candidate.size() };
                    for (size_t i = 0; i < candidate.size(); ++i)
                        if (candidate[i] == ',' || candidate[i] == ']' || candidate[i] == '}' || candidate[i] == ')')
                            cuts.push_back(i);
                    std::sort(cuts.rbegin(), cuts.rend());
                    for (size_t cut : cuts)
                    {
                        std::string c = candidate.substr(0, cut);
                        while (!c.empty() && (c.back() == ' ' || c.back() == '\t'))
                            c.pop_back();
                        if (c.size() <= rootLen)
                            continue;
                        std::error_code ec;
                        const fs::path full = projectDir / c;
                        if (fs::is_regular_file(full, ec))
                        {
                            out.push_back(full);
                            break;
                        }
                    }
                }
            }
        }

        const AddonManifest* AddonContaining(const fs::path& path)
        {
            const std::string p = fs::weakly_canonical(path).generic_string();
            for (const auto& a : AddonManager::GetInstalled())
            {
                const std::string dir = fs::weakly_canonical(a.Directory).generic_string();
                if (!dir.empty() && (p == dir || p.rfind(dir + "/", 0) == 0))
                    return &a;
            }
            return nullptr;
        }
    }

    // ------------------------------------------------------------------ Create menüsü

    std::vector<ContentBrowserPanel::CreateItem> ContentBrowserPanel::CollectCreateItems()
    {
        std::vector<CreateItem> v;
        auto add = [&](const std::string& category, const std::string& sub, const std::string& label, const std::string& icon,
                       const std::string& kind, const std::string& tip = std::string(), bool enabled = true, bool separator = false) {
            CreateItem it;
            it.Category = category;
            it.SubMenu = sub;
            it.Label = label;
            it.Icon = icon;
            it.Kind = kind;
            it.Tooltip = tip;
            it.Enabled = enabled;
            it.SeparatorBefore = separator;
            v.push_back(std::move(it));
        };

        add("", "", "Folder", ICON_FA_FOLDER, "folder");

        // Scene
        add("Scene", "", "Scene", ICON_FA_CLAPPERBOARD, "scene");
        add("Scene", "", "Scene Template", ICON_FA_CLAPPERBOARD, "scenetemplate",
            "A starting point for new scenes: File > New Scene lists it next to the built-in templates.\n"
            "With a scene selected, the template uses that scene.");
        add("Scene", "", "Prefab", ICON_FA_CUBES_STACKED, "prefab", "", true, true);
        {
            const bool canVariant = m_SelectedPath.extension() == PrefabSerializer::Extension;
            add("Scene", "", "Prefab Variant", ICON_FA_CUBES_STACKED, "prefabvariant",
                canVariant ? "A prefab based on the selected prefab: it stores only what you change, everything else follows the base."
                           : "Select a prefab first: the variant is based on it.",
                canVariant);
        }

        // 2D
        {
            static const char* shapes[][2] = { { "Triangle", "Triangle" }, { "Square", "Square" }, { "Circle", "Circle" },
                                               { "Capsule", "Capsule" }, { "Isometric Diamond", "IsometricDiamond" },
                                               { "Hexagon Flat-Top", "HexagonFlat" }, { "Hexagon Pointed-Top", "HexagonPoint" },
                                               { "9-Sliced", "NineSliced" } };
            for (const auto& sh : shapes)
                add("2D", "Sprites", sh[0], "", std::string("sprite:") + sh[1] + "|" + sh[0]);
            add("2D", "Tile Palette", "Rectangular", "", "tilepalette:0");
            add("2D", "Tile Palette", "Hexagonal Pointed-Top", "", "tilepalette:1");
            add("2D", "Tile Palette", "Hexagonal Flat-Top", "", "tilepalette:2");
            add("2D", "Tile Palette", "Isometric", "", "tilepalette:3");
            add("2D", "Tiles", "Tile", "", "tile");
            add("2D", "Tiles", "Rule Tile", "", "ruletile", "Picks its sprite from rules about which neighbours are the same tile (paths, walls, platforms).");
            add("2D", "Tiles", "Animated Tile", "", "animatedtile");
            add("2D", "", "Sprite Atlas", ICON_FA_OBJECT_GROUP, "spriteatlas",
                "Packs sprites (or whole folders) into one texture so they draw with fewer texture switches.\n"
                "With images selected, they are added to the atlas.");
            add("2D", "", "Physics Material 2D", ICON_FA_BASKETBALL, "physicsmaterial2d", "Friction and bounciness for 2D colliders and Rigidbody 2D (Material slot).");
        }

        // Rendering
        add("Rendering", "", "Material", ICON_FA_CIRCLE_HALF_STROKE, "material");
        {
            const bool canVariant = MaterialAsset::IsMaterialFile(m_SelectedPath.string());
            add("Rendering", "", "Material Variant", ICON_FA_CIRCLE_HALF_STROKE, "materialvariant",
                canVariant ? "A material that inherits everything from the selected material and stores only what you change\n"
                             "(Unity Material Variant / Unreal Material Instance)."
                           : "Select a material first: the variant inherits from it.",
                canVariant);
        }
        add("Rendering", "", "Render Texture", ICON_FA_TV, "rendertexture",
            "A texture a camera renders into (Render Target component > Target Texture). Use it in any texture slot\n"
            "for monitors, minimaps, mirrors and portals.", true, true);
        add("Rendering", "", "Custom Render Texture", ICON_FA_TV, "customrendertexture",
            "A texture a shader updates (every frame, once, or on demand), with the previous frame as input.");
        add("Rendering", "", "Cubemap", ICON_FA_CUBE, "cubemap",
            "Six face images or one panorama as a cube texture (Unity Legacy Cubemap): use it as a Skybox.\n"
            "With an image selected, it becomes the panorama.");
        add("Rendering", "", "Lighting Settings", ICON_FA_LIGHTBULB, "lightingsettings",
            "Bake settings (lightmapper, samples, bounces, resolution) that scenes can share (Lighting window > Scene).", true, true);
        add("Rendering", "", "Lightmap Parameters", ICON_FA_SLIDERS, "lightmapparams",
            "Per-object bake quality: anti-aliasing, backface tolerance, pushoff (Mesh Renderer > Lighting).");
        add("Rendering", "", "Volume Profile", ICON_FA_CUBES_STACKED, "volumeprofile",
            "Post-processing overrides (bloom, exposure, color, vignette, depth of field...) that Post Process Volumes share.", true, true);
        add("Rendering", "", "Particle System", ICON_FA_FIRE, "particleasset", "Particle settings in a file (Unreal Niagara System). Assign it to a Particle System component's Asset slot.");
        add("Rendering", "", "Lens Flare", ICON_FA_STAR, "lensflare", "Lens flare elements (glow, ring, ghosts, streak) for the Lens Flare component.");

        // Shader
        add("Shader", "", "Material Function", ICON_FA_DIAGRAM_PROJECT, "materialfunction", "A Shader Graph used as one node in other graphs (Function Input / Output nodes).");
        add("Shader", "", "Shader Graph", ICON_FA_DIAGRAM_PROJECT, "shadergraph", "A material driven by a node graph; opens in the Shader Graph editor.");
        add("Shader", "", "Lit Shader", "", "shader:lit",
            "Built-in PBR lighting with your own surface function (Standard Surface Shader). Material > Shader File.", true, true);
        add("Shader", "", "Unlit Shader", "", "shader:unlit");
        add("Shader", "", "UI Shader", "", "shader:ui",
            "Draws an Image or Raw Image with your own code (material > Shader, Image > Material); reads Position As UV1.");
        add("Shader", "", "Image Effect Shader", "", "shader:imageeffect", "A full-screen effect for the Full Screen Pass component.");
        add("Shader", "", "Custom Render Texture Shader", "", "shader:crt");
        add("Shader", "", "Compute Shader", "", "shader:compute", "GPU computation run from scripts (ComputeSetTexture / ComputeSetFloat / ComputeDispatch).");
        add("Shader", "", "Ray Tracing Shader", "", "shader:raytracing",
            "Traces rays against the scene's meshes on the GPU (shadows, reflections, AO). Run with RayTracingDispatch.");
        add("Shader", "", "Shader Variant Collection", "", "shadervariants",
            "Shaders to compile when the game starts (Project Settings > Graphics > Preloaded Shaders).", true, true);

        // Physics
        add("Physics", "", "Physics Material", ICON_FA_BASKETBALL, "physicsmaterial", "Friction and bounciness for colliders (Collider > Material).");

        // Terrain
        add("Terrain", "", "Terrain Layer", ICON_FA_LAYER_GROUP, "terrainlayer",
            "A terrain texture layer (texture, tint, tiling, roughness) that terrains share. With an image selected, it uses it.");
        add("Terrain", "", "Brush", ICON_FA_PAINTBRUSH, "terrainbrush", "A terrain brush shape from a grayscale mask. With an image selected, it becomes the mask.");

        // Audio
        add("Audio", "", "Audio Mixer", ICON_FA_SLIDERS, "audiomixer", "A mixer with its own groups, effects and snapshots; Audio Sources output to its groups.");
        add("Audio", "", "Audio Random Container", ICON_FA_SHUFFLE, "audiorandom",
            "Plays a random, shuffled or sequential clip from a list with volume / pitch variation;\n"
            "Audio Sources and AudioPlay use it like a clip.");

        // Animation
        add("Animation", "", "Animation Clip", ICON_FA_CLAPPERBOARD, "animclip",
            "Keyframes for positions, rotations, scales and component fields (lights, materials, any number or color) plus Animation Events.\n"
            "Edit it in Window > Animation; the Animation component or AnimationPlay plays it.");
        add("Animation", "", "Animator Controller", ICON_FA_PERSON_RUNNING, "controller");
        add("Animation", "", "Animator Override Controller", ICON_FA_PERSON_RUNNING, "overridecontroller",
            "Reuses an Animator Controller with other clips (same states, transitions and parameters). Put it in the Animation "
            "component's Controller slot.");
        add("Animation", "", "Blend Space", ICON_FA_BRAILLE, "blendspace2d",
            "Clips placed on a 2D grid (e.g. speed x direction); a Blend Tree state's Blend Space slot plays them blended (Unreal Blend Space).");
        add("Animation", "", "Blend Space 1D", ICON_FA_BRAILLE, "blendspace1d",
            "Clips placed on one axis (e.g. idle, walk, run by speed) (Unreal Blend Space 1D).");
        add("Animation", "", "Anim Montage", ICON_FA_PERSON_RUNNING, "montage");
        add("Animation", "", "Avatar Mask", ICON_FA_PERSON, "avatarmask",
            "Which body parts or bones an Animator layer animates (Animator > layer gear > Mask), e.g. upper body only.");

        // Timeline
        add("Timeline", "", "Timeline", ICON_FA_FILM, "timeline");
        add("Timeline", "Playables", "Playable Asset", ICON_FA_FILE_CODE, "playableasset", "A Timeline clip for a Playable track: names a Playable Behaviour script and keeps its field values.");
        add("Timeline", "Playables", "Playable Behaviour Script", ICON_FA_FILE_CODE, "playablescript", "A script with OnClipStart, OnClipUpdate(time, progress) and OnClipStop, played by Playable track clips.");
        add("Timeline", "", "Signal", ICON_FA_TOWER_BROADCAST, "signal", "A Timeline signal: Signal track markers emit it, Signal Receiver components react to it.");

        // Scripting
        for (auto* runtime : Scripting::ScriptRuntimes::All())
        {
            if (std::string(runtime->GetLanguageId()) == "stategraph")
                continue;
            const bool available = runtime->IsAvailable();
            add("Scripting", "Script", std::string(runtime->GetName()) + " (" + runtime->GetExtensions().front() + ")", "",
                std::string("script:") + runtime->GetLanguageId(), available ? std::string() : runtime->GetUnavailableReason(), available);
        }
        add("Scripting", "Script", "C++ Native Behaviour (Native Script component)", "", "cpp",
            "A ScriptableEntity class using the engine's C++ API. Drag it onto a Native Script component; the engine compiles it when you press Play.",
            true, true);
        add("Scripting", "", "Visual Script Graph", ICON_FA_DIAGRAM_PROJECT, "visualscript");
        add("Scripting", "Script", "Interface", "", "scripttpl:interface", "Method signatures other scripts can implement. Call them with SendMessage().");
        add("Scripting", "Script", "Library", "", "scripttpl:library", "Shared functions. Other scripts load it with: using FileName;");
        add("Scripting", "Script", "Enum", "", "scripttpl:enum", "A named list of values shared by scripts with: using FileName;");
        add("Scripting", "Script", "Struct", "", "scripttpl:struct", "A data type shared by scripts with: using FileName;");
        add("Scripting", "", "Editor Utility", ICON_FA_WAND_MAGIC_SPARKLES, "editorutility", "A script that runs inside the editor (select it and press Run in the Inspector).");
        add("Scripting", "Testing", "Test Script", ICON_FA_FLASK_VIAL, "testscript", "Test* methods run from Window > Test Runner in Edit Mode, PlayTest* methods in Play Mode.");
        RegisterBuiltinNodeAssetSchemas();
        for (const auto& schema : NodeAssetPanel::Schemas())
            add(schema.MenuCategory, schema.MenuSubmenu, schema.Title, schema.Icon, "nodeasset:" + schema.Kind, schema.MenuTooltip);
        for (auto* runtime : Scripting::ScriptRuntimes::All())
        {
            if (!DataAssets::SupportsDataClasses(runtime->GetLanguageId()))
                continue;
            const bool available = runtime->IsAvailable();
            const std::string ext = std::string(runtime->GetLanguageId()) == "cpp" ? std::string(".h") : runtime->GetExtensions().front();
            add("Scripting", "Data Asset Script", std::string(runtime->GetName()) + " (" + ext + ")", "",
                std::string("dataassetscript:") + runtime->GetLanguageId(),
                available ? std::string("A class deriving from DataAsset: its public fields are the data template.") : runtime->GetUnavailableReason(), available);
        }

        // Data
        {
            static double s_LastScan = -10.0;
            static std::vector<std::pair<std::string, std::string>> s_Classes;
            if (ImGui::GetTime() - s_LastScan > 1.0 && Project::GetActive())
            {
                s_Classes = DataAssets::FindDataClasses(Project::GetActiveProjectDirectory().string());
                s_LastScan = ImGui::GetTime();
            }
            if (s_Classes.empty())
                add("Data", "Data Asset", "No Data Asset classes yet: create a Data Asset Script, then pick its class here.", "", "");
            for (const auto& [script, cls] : s_Classes)
                add("Data", "Data Asset", cls, "", "dataasset:" + script + "|" + cls,
                    "A " + cls + " asset (class in " + script + "): values edited in the Inspector, read in game with LoadDataAsset.");
        }

        // Curve (Unreal Curve Float / Vector / Linear Color): CurveEvaluate ile okunur
        add("Data", "", "Data Table", ICON_FA_TABLE, "datatable", "Rows of game data edited as a grid. Scripts read it with DataTableGetValue(path, row, column).");
        add("Data", "Curve", "Curve Table", ICON_FA_CHART_LINE, "curvetable", "Named curves in a grid. Scripts read it with CurveTableEvaluate(path, row, time).");
        add("Data", "Curve", "Float Curve", ICON_FA_CHART_LINE, "curve:float", "One value over time (Unreal Curve Float). Read with CurveEvaluate(path, t).");
        add("Data", "Curve", "Vector Curve", ICON_FA_CHART_LINE, "curve:vector", "X, Y and Z over time (Unreal Curve Vector).");
        add("Data", "Curve", "Color Curve", ICON_FA_CHART_LINE, "curve:color", "R, G, B and A over time (Unreal Curve Linear Color), with a gradient preview.");

        // Input
        add("Input", "", "Input Actions", ICON_FA_GAMEPAD, "inputactions",
            "Action maps, actions and their keyboard / mouse / gamepad bindings (Unity Input Actions, Unreal Input Action\n"
            "+ Mapping Context). Double-click to edit; assign it to a Player Input component's Actions slot.");

        add("AI", "", "Environment Query", ICON_FA_LOCATION_CROSSHAIRS, "eqs", "Finds the best spots or objects around an AI. Run it from a Behavior Tree task or EQSRunAsset().");
        add("UI", "UI Toolkit", "UI Document", ICON_FA_WINDOW_MAXIMIZE, "uidocument", "UI layout in a text file (.cquxml). Show it with a UI Document component.");
        add("UI", "UI Toolkit", "Style Sheet", ICON_FA_FILE_LINES, "uistyle", "Selectors and properties (.cquss) for UI Documents.");

        // Tabloyla tanımlı varlıklar
        for (const auto& def : SimpleAssets::All())
            add(def.Category, def.Submenu, def.Label, def.Icon, "simple:" + def.Kind, def.Tooltip);

        add("~", "", "Text File", ICON_FA_FILE_LINES, "text");
        return v;
    }

    void ContentBrowserPanel::RunCreate(const std::string& kind)
    {
        const fs::path created = CreateAsset(kind);
        if (created.empty())
            return;
        m_Dirty = true;
        m_SelectedPath = created;
        EditorState::Get().SelectAsset(created.string());   // Unity: yeni varlık Inspector'da açılır
        BeginRename(created);
    }

    namespace
    {
        struct CategoryInfo
        {
            const char* Name;
            const char* Icon;
        };
        const CategoryInfo kCategories[] = {
            { "Scene", ICON_FA_CLAPPERBOARD },      { "2D", ICON_FA_SHAPES },           { "Rendering", ICON_FA_CIRCLE_HALF_STROKE },
            { "Shader", ICON_FA_WAND_MAGIC_SPARKLES }, { "Physics", ICON_FA_BASKETBALL }, { "Terrain", ICON_FA_MOUNTAIN_SUN },
            { "Audio", ICON_FA_VOLUME_HIGH },       { "Animation", ICON_FA_PERSON_RUNNING }, { "Timeline", ICON_FA_FILM },
            { "Scripting", ICON_FA_FILE_CODE },     { "Data", ICON_FA_DATABASE },       { "Input", ICON_FA_GAMEPAD },         { "UI", ICON_FA_WINDOW_MAXIMIZE },          { "AI", ICON_FA_SITEMAP },          { "Media", ICON_FA_FILM },         { "Foliage", ICON_FA_TREE },         { "Miscellaneous", ICON_FA_SHAPES },
        };
    }

    void ContentBrowserPanel::DrawCreateMenu()
    {
        if (IsReadOnly(m_CurrentDirectory))
        {
            ImGui::TextDisabled(ICON_FA_LOCK "  Packages are read-only");
            return;
        }
        const auto items = CollectCreateItems();
        std::string run;
        auto drawItem = [&](const CreateItem& it) {
            if (it.SeparatorBefore)
                ImGui::Separator();
            if (it.Kind.empty())
            {
                ImGui::TextDisabled("%s", it.Label.c_str());
                return;
            }
            const std::string label = (it.Icon.empty() ? std::string() : it.Icon + "  ") + it.Label;
            if (ImGui::MenuItem(label.c_str(), nullptr, false, it.Enabled))
                run = it.Kind;
            if (!it.Tooltip.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayShort))
                ImGui::SetTooltip("%s", it.Tooltip.c_str());
        };
        // Bir kategorinin girişleri: alt menüler ilk göründükleri yerde açılır
        auto drawCategory = [&](const std::string& category) {
            std::vector<std::string> doneSubs;
            for (const auto& it : items)
            {
                if (it.Category != category)
                    continue;
                if (it.SubMenu.empty())
                {
                    drawItem(it);
                    continue;
                }
                if (std::find(doneSubs.begin(), doneSubs.end(), it.SubMenu) != doneSubs.end())
                    continue;
                doneSubs.push_back(it.SubMenu);
                if (it.SeparatorBefore)
                    ImGui::Separator();
                if (ImGui::BeginMenu(it.SubMenu.c_str()))
                {
                    bool first = true;
                    for (const auto& sub : items)
                        if (sub.Category == category && sub.SubMenu == it.SubMenu)
                        {
                            CreateItem copy = sub;
                            if (first)
                                copy.SeparatorBefore = false;
                            first = false;
                            drawItem(copy);
                        }
                    ImGui::EndMenu();
                }
            }
        };
        drawCategory("");
        ImGui::Separator();
        for (const auto& c : kCategories)
            if (ImGui::BeginMenu((std::string(c.Icon) + "  " + c.Name).c_str()))
            {
                drawCategory(c.Name);
                ImGui::EndMenu();
            }
        ImGui::Separator();
        drawCategory("~");
        if (!run.empty())
            RunCreate(run);
    }

    // ------------------------------------------------------------------ Türe özel ve Project işlemleri

    std::vector<ContentBrowserPanel::MenuAction> ContentBrowserPanel::TypeActions(const fs::path& target, bool isDirectory)
    {
        std::vector<MenuAction> v;
        if (target.empty() || isDirectory)
            return v;
        const std::string ext = Lower(target.extension().string());
        const bool readOnly = IsReadOnly(target);
        auto add = [&](const std::string& label, const std::string& icon, std::function<void()> run, bool enabled = true, const std::string& tip = std::string()) {
            MenuAction a;
            a.Label = label;
            a.Icon = icon;
            a.Run = std::move(run);
            a.Enabled = enabled;
            a.Tooltip = tip;
            v.push_back(std::move(a));
        };
        auto createFrom = [this](const std::string& kind) {
            return [this, kind]() { RunCreate(kind); };
        };
        if (IsImageFile(target))
        {
            if (m_SpriteEditorCallback)
                add("Open in Sprite Editor", ICON_FA_OBJECT_UNGROUP, [this, target]() { m_SpriteEditorCallback(target); });
            add("Create Material", ICON_FA_CIRCLE_HALF_STROKE, [this, target]() {
                // Unreal: dokuya sağ tık > Create Material (doku taban haritası olur)
                fs::path created = MakeUniquePath(WritableDirectory() / (target.stem().string() + ".cqmat"));
                Material m;
                m.AlbedoMap = Texture2D::Create(target.string());
                if (MaterialAsset::Create(created.string(), &m))
                {
                    m_Dirty = true;
                    m_SelectedPath = created;
                    EditorState::Get().SelectAsset(created.string());
                }
            });
            add("Create Tile", ICON_FA_SQUARE, createFrom("tile"));
            add("Create Terrain Layer", ICON_FA_LAYER_GROUP, createFrom("terrainlayer"));
            add("Create Terrain Brush", ICON_FA_PAINTBRUSH, createFrom("terrainbrush"));
            add("Create Sprite Atlas", ICON_FA_OBJECT_GROUP, createFrom("spriteatlas"));
        }
        if (ext == ".cqmat")
            add("Create Material Variant", ICON_FA_CIRCLE_HALF_STROKE, createFrom("materialvariant"));
        if (ext == PrefabSerializer::Extension)
            add("Create Prefab Variant", ICON_FA_CUBES_STACKED, createFrom("prefabvariant"));
        if (ext == ".cqscene")
        {
            add("Open Scene Additive", ICON_FA_LAYER_GROUP, [target]() {
                if (s_OpenSceneAdditive)
                    s_OpenSceneAdditive(target.string());
            }, (bool)s_OpenSceneAdditive && EditorState::Get().IsEditing());
            add("Create Scene Template From Scene", ICON_FA_CLAPPERBOARD, createFrom("scenetemplate"));
        }
        if (IsModelFile(target))
            add("Extract Materials", ICON_FA_FILE_EXPORT, [this, target]() { ExtractModelMaterials(target); }, !readOnly,
                "Writes the model's materials (and embedded textures) as .cqmat files next to it and remaps the model to them.");
        if (Scripting::ScriptRuntimes::IsScriptFile(target.string()) || DataAssets::IsCppHeader(target.string()))
        {
            add("Open in Code Editor", ICON_FA_FILE_CODE, [this, target]() { OpenFile(target); });
            for (const auto& cls : DataAssets::DataClassesOf(target.string()))
                add("Create " + cls + " Asset", ICON_FA_DATABASE, createFrom("dataasset:" + ProjectRelative(target) + "|" + cls));
        }
        if (ext == ".cqdata")
        {
            DataAssets::DataAssetFile data;
            if (DataAssets::Load(target.string(), data) && !data.Script.empty())
                add("Open Script", ICON_FA_FILE_CODE, [this, data]() { OpenFile(SceneSerializer::ResolveAssetPath(data.Script)); });
        }
        if (ext == ".cqinputactions")
            add("Edit Asset", ICON_FA_GAMEPAD, [this, target]() { OpenFile(target); });
        return v;
    }

    std::vector<ContentBrowserPanel::MenuAction> ContentBrowserPanel::ProjectActions(const fs::path& target, bool isDirectory)
    {
        std::vector<MenuAction> v;
        const bool hasTarget = !target.empty();
        const fs::path folder = hasTarget ? (isDirectory ? target : target.parent_path()) : m_CurrentDirectory;
        const bool readOnly = hasTarget ? IsReadOnly(target) : IsReadOnly(m_CurrentDirectory);
        const std::string ext = Lower(target.extension().string());
        auto add = [&](const std::string& label, const std::string& icon, const std::string& shortcut, std::function<void()> run, bool enabled = true,
                       bool separator = false, const std::string& sub = std::string(), const std::string& tip = std::string()) {
            MenuAction a;
            a.Label = label;
            a.Icon = icon;
            a.Shortcut = shortcut;
            a.Run = std::move(run);
            a.Enabled = enabled;
            a.SeparatorBefore = separator;
            a.SubMenu = sub;
            a.Tooltip = tip;
            v.push_back(std::move(a));
        };

        add("Open Containing Folder", ICON_FA_UP_RIGHT_FROM_SQUARE, "", [this, target, folder, hasTarget]() {
            RevealInFileManager(hasTarget ? target.parent_path() : folder);
        });
        add("Open", ICON_FA_FOLDER_OPEN, "Enter", [this, target, isDirectory]() {
            if (isDirectory)
                NavigateTo(target);
            else
                OpenFile(target);
        }, hasTarget);
        add("Open Scene Additive", ICON_FA_LAYER_GROUP, "", [target]() {
            if (s_OpenSceneAdditive)
                s_OpenSceneAdditive(target.string());
        }, hasTarget && ext == ".cqscene" && (bool)s_OpenSceneAdditive && EditorState::Get().IsEditing(), false, "",
            "Adds the scene's objects to the open scene under a level root (deleting that root removes them again).");
        add("Delete", ICON_FA_TRASH, "Del", [this, target]() {
            m_PendingDelete = target;
            m_OpenDeleteModal = true;
        }, hasTarget && !readOnly);
        add("Rename", ICON_FA_PEN, "F2", [this, target]() { BeginRename(target); }, hasTarget && !readOnly);
        add("Copy Path", ICON_FA_COPY, "Ctrl+Alt+C", [target]() { ImGui::SetClipboardText(ProjectRelative(target).c_str()); }, hasTarget);

        // Paketler (Unity Package Manager / UPM karşılığı: addon'lar Packages klasöründe)
        const AddonManifest* addon = hasTarget ? AddonContaining(target) : nullptr;
        add("View in Addon Manager", ICON_FA_PUZZLE_PIECE, "", [addon]() {
            if (addon && s_OpenAddonManager)
                s_OpenAddonManager(addon->Id);
        }, addon != nullptr && (bool)s_OpenAddonManager, true);
        add("Create Addon Package...", ICON_FA_PUZZLE_PIECE, "", [this]() {
            std::string id, error;
            if (AddonManager::CreateNew("New Addon", &id, &error))
            {
                m_Dirty = true;
                if (s_OpenAddonManager)
                    s_OpenAddonManager(id);
            }
            else
                CQ_CORE_ERROR("Create addon: {0}", error);
        }, Project::GetActive() != nullptr);
        add("Custom Package...", ICON_FA_BOX_ARCHIVE, "", [this]() {
            nfdu8char_t* out = nullptr;
            nfdu8filteritem_t filter[1] = { { "Conqueror Package", "cqpackage" } };
            if (NFD_OpenDialogU8(&out, filter, 1, nullptr) == NFD_OKAY && out)
            {
                std::string error;
                if (!BeginImportPackage(out, &error))
                    CQ_CORE_ERROR("Import package: {0}", error);
                NFD_FreePathU8(out);
            }
        }, Project::GetActive() != nullptr, false, "Import Package");
        add("Export Package...", ICON_FA_FILE_EXPORT, "", [this, target]() { BeginExportPackage({ target }); }, hasTarget);
        const bool isAddonFolder = hasTarget && isDirectory && fs::exists(target / "addon.yaml");
        add("Export As Addon Package...", ICON_FA_FILE_EXPORT, "", [this, target]() {
            nfdu8char_t* out = nullptr;
            nfdu8filteritem_t filter[1] = { { "Conqueror Package", "cqpackage" } };
            if (NFD_SaveDialogU8(&out, filter, 1, nullptr, (target.filename().string() + ".cqpackage").c_str()) == NFD_OKAY && out)
            {
                std::vector<fs::path> files;
                std::error_code ec;
                for (fs::recursive_directory_iterator it(target, ec), end; !ec && it != end; it.increment(ec))
                    if (it->is_regular_file(ec))
                        files.push_back(it->path());
                std::string error;
                if (!ExportPackage(files, out, &error))
                    CQ_CORE_ERROR("Export addon package: {0}", error);
                NFD_FreePathU8(out);
            }
        }, isAddonFolder);
        add("Import New Asset...", ICON_FA_FILE_IMPORT, "", [this]() { ImportFile(); }, !readOnly);
        add("Extract Materials", ICON_FA_FILE_EXPORT, "", [this, target]() { ExtractModelMaterials(target); }, hasTarget && IsModelFile(target) && !readOnly);

        // Başvurular
        add("Find References In Scene", ICON_FA_MAGNIFYING_GLASS, "", [target]() {
            if (s_FindReferencesInScene)
            {
                const int n = s_FindReferencesInScene(target.string());
                CQ_CORE_INFO("Find References In Scene: {0} object(s) use {1}", n, target.filename().string());
            }
        }, hasTarget && (bool)s_FindReferencesInScene, true);
        add("Find References In Project", ICON_FA_MAGNIFYING_GLASS, "", [this, target]() {
            ShowResults("References to " + target.filename().string(), FindReferencesInProject(target));
        }, hasTarget);
        add("Select Dependencies", ICON_FA_SITEMAP, "", [this, target]() {
            ShowResults("Dependencies of " + target.filename().string(), CollectDependencies(target));
        }, hasTarget);

        // İçe aktarma
        add("Refresh", ICON_FA_ARROWS_ROTATE, "Ctrl+R", [this]() {
            m_Thumbnails.clear();
            m_Dirty = true;
        }, true, true);
        add("Reimport", ICON_FA_ROTATE, "", [this, target]() { Reimport(target); }, hasTarget);
        add("Reimport All", ICON_FA_ROTATE, "", [this]() { ReimportAll(); });

        add("Open Script Project", ICON_FA_CODE, "", []() {
            if (s_OpenScriptProject)
                s_OpenScriptProject();
        }, (bool)s_OpenScriptProject, true, "",
            "Opens the project's scripts in the External Script Editor (Project Settings > Editor), or in the Code Editor.");
        add("View in Import Activity Window", ICON_FA_CLOCK_ROTATE_LEFT, "", [target]() {
            if (s_OpenImportActivity)
                s_OpenImportActivity(ProjectRelative(target));
        }, hasTarget && (bool)s_OpenImportActivity);
        add("Properties...", ICON_FA_CUBE, "Alt+P", [this, target]() { OpenProperties(target); }, hasTarget && !isDirectory);
        return v;
    }

    // ------------------------------------------------------------------ Sağ tık menüsü

    void ContentBrowserPanel::DrawItemContextMenu(const Entry& entry)
    {
        if (!ImGui::BeginPopupContextItem("##ItemContext"))
            return;
        if (ImGui::IsWindowAppearing())
            m_SelectedPath = entry.Path;   // Unity: sağ tıklanan varlık seçilir
        DrawProjectMenu(entry.Path, entry.IsDirectory);
        ImGui::EndPopup();
    }

    void ContentBrowserPanel::DrawBackgroundContextMenu()
    {
        if (!ImGui::BeginPopupContextWindow("##ContentBackground", ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems))
            return;
        DrawProjectMenu({}, false);
        ImGui::EndPopup();
    }

    void ContentBrowserPanel::DrawProjectMenu(const fs::path& target, bool isDirectory)
    {
        const bool hasTarget = !target.empty();
        const bool readOnly = hasTarget ? IsReadOnly(target) : IsReadOnly(m_CurrentDirectory);
        const fs::path folder = hasTarget ? (isDirectory ? target : target.parent_path()) : m_CurrentDirectory;

        // ── Arama ──
        if (ImGui::IsWindowAppearing())
        {
            m_MenuSearch[0] = '\0';
            ImGui::SetKeyboardFocusHere();
        }
        ImGui::SetNextItemWidth(std::max(260.0f, ImGui::GetContentRegionAvail().x));
        ImGui::InputTextWithHint("##MenuSearch", ICON_FA_MAGNIFYING_GLASS "  Search", m_MenuSearch, sizeof(m_MenuSearch));

        auto typeActions = TypeActions(target, isDirectory);
        auto projectActions = ProjectActions(target, isDirectory);

        if (m_MenuSearch[0] != '\0')
        {
            // Düz sonuç listesi: Create girişleri (kategori yoluyla), türe özel ve Project işlemleri
            const std::string q = m_MenuSearch;
            int shown = 0;
            ImGui::Separator();
            if (!readOnly)
                for (const auto& it : CollectCreateItems())
                {
                    if (it.Kind.empty())
                        continue;
                    std::string path = "Create";
                    if (!it.Category.empty() && it.Category != "~")
                        path += " > " + it.Category;
                    if (!it.SubMenu.empty())
                        path += " > " + it.SubMenu;
                    if (!ContainsNoCase(it.Label, q) && !ContainsNoCase(path, q))
                        continue;
                    const std::string label = (it.Icon.empty() ? std::string(ICON_FA_PLUS) : it.Icon) + "  " + it.Label + "##s" + it.Kind;
                    if (ImGui::MenuItem(label.c_str(), path.c_str(), false, it.Enabled))
                        RunCreate(it.Kind);
                    ++shown;
                }
            auto showActions = [&](std::vector<MenuAction>& actions, const char* group) {
                for (auto& a : actions)
                {
                    if (!ContainsNoCase(a.Label, q) && !(a.SubMenu.size() && ContainsNoCase(a.SubMenu, q)))
                        continue;
                    const std::string label = a.Icon + "  " + (a.SubMenu.empty() ? a.Label : a.SubMenu + " > " + a.Label) + "##s" + group;
                    if (ImGui::MenuItem(label.c_str(), a.Shortcut.empty() ? group : a.Shortcut.c_str(), false, a.Enabled) && a.Run)
                        a.Run();
                    ++shown;
                }
            };
            showActions(typeActions, "Asset");
            showActions(projectActions, "Project");
            if (shown == 0)
                ImGui::TextDisabled("No menu entries match \"%s\"", m_MenuSearch);
            return;
        }

        // ── Asset Options: ikonlu satır ──
        ImGui::Spacing();
        ImGui::TextDisabled("Asset Options");
        {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(7.0f, 5.0f));
            auto iconButton = [](const char* icon, const char* id, const char* tip, bool enabled) {
                ImGui::BeginDisabled(!enabled);
                const bool pressed = ImGui::Button((std::string(icon) + id).c_str());
                ImGui::EndDisabled();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayShort))
                    ImGui::SetTooltip("%s", tip);
                return pressed;
            };
            if (iconButton(ICON_FA_SCISSORS, "##aoCut", "Cut (Ctrl+X)", hasTarget && !readOnly))
            {
                CopyAssets({ target }, true);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (iconButton(ICON_FA_COPY, "##aoCopy", "Copy (Ctrl+C)", hasTarget))
            {
                CopyAssets({ target }, false);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (iconButton(ICON_FA_PASTE, "##aoPaste", "Paste (Ctrl+V)", HasClipboard() && !IsReadOnly(folder)))
            {
                PasteInto(folder);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (iconButton(ICON_FA_TRASH, "##aoDelete", "Delete (Del)", hasTarget && !readOnly))
            {
                m_PendingDelete = target;
                m_OpenDeleteModal = true;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (iconButton(ICON_FA_PEN_TO_SQUARE, "##aoRename", "Rename (F2)", hasTarget && !readOnly))
            {
                BeginRename(target);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (iconButton(ICON_FA_CUBE, "##aoProperties", "Properties (Alt+P)", hasTarget && !isDirectory))
            {
                OpenProperties(target);
                ImGui::CloseCurrentPopup();
            }
            ImGui::PopStyleVar();
            ImGui::PopStyleColor();
        }

        // ── Türe özel seçenekler ──
        if (ImGui::BeginMenu(ICON_FA_LIST "  Other Options related to asset type", !typeActions.empty()))
        {
            for (auto& a : typeActions)
            {
                if (ImGui::MenuItem((a.Icon + "  " + a.Label).c_str(), nullptr, false, a.Enabled) && a.Run)
                    a.Run();
                if (!a.Tooltip.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayShort))
                    ImGui::SetTooltip("%s", a.Tooltip.c_str());
            }
            ImGui::EndMenu();
        }

        // ── Project ──
        ImGui::Separator();
        ImGui::TextDisabled("Project");
        if (ImGui::BeginMenu(ICON_FA_PLUS "  Create"))
        {
            DrawCreateMenu();
            ImGui::EndMenu();
        }
        std::vector<std::string> doneSubs;
        for (size_t i = 0; i < projectActions.size(); ++i)
        {
            auto& a = projectActions[i];
            if (a.SeparatorBefore)
                ImGui::Separator();
            if (!a.SubMenu.empty())
            {
                if (std::find(doneSubs.begin(), doneSubs.end(), a.SubMenu) != doneSubs.end())
                    continue;
                doneSubs.push_back(a.SubMenu);
                if (ImGui::BeginMenu((a.Icon + "  " + a.SubMenu).c_str()))
                {
                    for (auto& s : projectActions)
                        if (s.SubMenu == a.SubMenu && ImGui::MenuItem(s.Label.c_str(), s.Shortcut.empty() ? nullptr : s.Shortcut.c_str(), false, s.Enabled) && s.Run)
                            s.Run();
                    ImGui::EndMenu();
                }
                continue;
            }
            if (ImGui::MenuItem((a.Icon + "  " + a.Label).c_str(), a.Shortcut.empty() ? nullptr : a.Shortcut.c_str(), false, a.Enabled) && a.Run)
                a.Run();
            if (!a.Tooltip.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled | ImGuiHoveredFlags_DelayShort))
                ImGui::SetTooltip("%s", a.Tooltip.c_str());
        }
    }

    // ------------------------------------------------------------------ Pano (Kes / Kopyala / Yapıştır)

    bool ContentBrowserPanel::CopyWithSidecars(const fs::path& source, const fs::path& target)
    {
        std::error_code ec;
        fs::copy(source, target, fs::copy_options::recursive, ec);
        if (ec)
        {
            CQ_CORE_ERROR("Copy failed: {0} -> {1} ({2})", source.string(), target.string(), ec.message());
            return false;
        }
        // İçe aktarma ayarı, sprite kesimi ve atlas dokusu da kopyalanır
        for (const auto& side : Sidecars(source))
        {
            const std::string suffix = side.string().substr(source.string().size());
            fs::copy(side, target.string() + suffix, fs::copy_options::overwrite_existing, ec);
        }
        return true;
    }

    void ContentBrowserPanel::CopyAssets(const std::vector<fs::path>& paths, bool cut)
    {
        m_Clipboard.clear();
        for (const auto& p : paths)
            if (!p.empty() && (!cut || !IsReadOnly(p)))
                m_Clipboard.push_back(p);
        m_ClipboardCut = cut;
    }

    bool ContentBrowserPanel::PasteInto(const fs::path& directory)
    {
        std::error_code ec;
        if (m_Clipboard.empty() || IsReadOnly(directory) || !fs::is_directory(directory, ec))
            return false;
        fs::path last;
        for (const auto& source : m_Clipboard)
        {
            if (!fs::exists(source, ec))
                continue;
            if (m_ClipboardCut)
            {
                const fs::path target = MakeUniquePath(directory / source.filename());
                if (MoveInto(source, directory))
                    last = target;
            }
            else
            {
                // Aynı klasöre yapıştırma: "Ad 1" (Unity: "Ad 1")
                const fs::path target = MakeUniquePath(directory / source.filename());
                if (CopyWithSidecars(source, target))
                    last = target;
            }
        }
        if (m_ClipboardCut)
            m_Clipboard.clear();
        if (last.empty())
            return false;
        m_SelectedPath = last;
        m_Dirty = true;
        return true;
    }

    // ------------------------------------------------------------------ Başvurular / bağımlılıklar

    std::vector<fs::path> ContentBrowserPanel::FindReferencesInProject(const fs::path& asset) const
    {
        std::vector<fs::path> out;
        const fs::path projectDir = ProjectDir();
        const std::string rel = ProjectRelative(asset);
        if (projectDir.empty() || rel.empty() || rel.rfind("..", 0) == 0)
            return out;
        std::error_code ec;
        const std::string self = fs::weakly_canonical(asset, ec).string();
        for (const char* root : { "Assets", "Packages" })
            for (fs::recursive_directory_iterator it(projectDir / root, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end;
                 it.increment(ec))
            {
                if (!it->is_regular_file(ec) || !IsTextAsset(it->path()) || it->path().extension() == ".meta")
                    continue;
                if (fs::weakly_canonical(it->path(), ec).string() == self)
                    continue;
                const std::string text = ReadText(it->path());
                for (size_t pos = text.find(rel); pos != std::string::npos; pos = text.find(rel, pos + 1))
                {
                    const size_t end = pos + rel.size();
                    const bool startOk = pos == 0 || std::string(" \"':[{,(\n\t").find(text[pos - 1]) != std::string::npos;
                    const bool endOk = end >= text.size() || std::string("\n\r\"',]})").find(text[end]) != std::string::npos ||
                                       (text[end] == ' ' && (end + 1 >= text.size() || text[end + 1] == '#'));
                    // Klasör: altındaki her şeye başvuru da sayılır
                    const bool folderOk = fs::is_directory(asset, ec) && end < text.size() && text[end] == '/';
                    if (startOk && (endOk || folderOk))
                    {
                        out.push_back(it->path());
                        break;
                    }
                }
            }
        std::sort(out.begin(), out.end());
        return out;
    }

    std::vector<fs::path> ContentBrowserPanel::CollectDependencies(const fs::path& asset) const
    {
        std::vector<fs::path> out;
        const fs::path projectDir = ProjectDir();
        std::error_code ec;
        if (projectDir.empty() || !fs::exists(asset, ec))
            return out;
        std::set<std::string> seen;
        std::vector<fs::path> queue;
        auto push = [&](const fs::path& p) {
            const std::string key = fs::weakly_canonical(p, ec).string();
            if (seen.insert(key).second)
            {
                queue.push_back(p);
                out.push_back(p);
            }
        };
        if (fs::is_directory(asset, ec))
        {
            for (fs::recursive_directory_iterator it(asset, ec), end; !ec && it != end; it.increment(ec))
                if (it->is_regular_file(ec) && it->path().extension() != ".meta" && it->path().extension() != ".cqsprite")
                    push(it->path());
        }
        else
            push(asset);
        for (size_t i = 0; i < queue.size(); ++i)
        {
            const fs::path current = queue[i];
            if (!IsTextAsset(current))
                continue;
            std::vector<fs::path> found;
            FindPathsInText(ReadText(current), projectDir, found);
            // Data Asset betiği, malzeme grafiği vb. proje göreli yolla durur; model içe aktarma eşlemesi .meta'da
            if (IsModelFile(current))
                for (const auto& [name, file] : ModelLoader::ReadMaterialRemap(current.string()))
                    if (!file.empty())
                        found.push_back(projectDir / file);
            for (const auto& f : found)
                if (fs::is_regular_file(f, ec))
                    push(f);
        }
        // Modellerin malzeme eşlemesi (metin olmayan dosya)
        for (size_t i = 0; i < out.size(); ++i)
            if (IsModelFile(out[i]))
                for (const auto& [name, file] : ModelLoader::ReadMaterialRemap(out[i].string()))
                    if (!file.empty() && fs::is_regular_file(projectDir / file, ec))
                        push(projectDir / file);
        return out;
    }

    void ContentBrowserPanel::ShowResults(const std::string& title, std::vector<fs::path> results)
    {
        m_ResultsTitle = title;
        m_Results = std::move(results);
        if (!m_Results.empty())
            m_SelectedPath = m_Results.front();
    }

    ContentBrowserPanel::Entry ContentBrowserPanel::MakeEntry(const fs::path& path) const
    {
        Entry e;
        std::error_code ec;
        e.Path = path;
        e.IsDirectory = fs::is_directory(path, ec);
        e.Name = e.IsDirectory ? path.filename().string() : path.stem().string();
        e.Extension = Lower(path.extension().string());
        if (!e.IsDirectory)
            e.Size = fs::file_size(path, ec);
        e.WriteTime = fs::last_write_time(path, ec);
        return e;
    }

    void ContentBrowserPanel::SelectAndReveal(const fs::path& path)
    {
        std::error_code ec;
        if (!fs::exists(path, ec))
            return;
        if (path.parent_path() != m_CurrentDirectory)
            NavigateTo(path.parent_path());
        m_SelectedPath = path;
        m_ScrollToSelected = true;
        EditorState::Get().SelectAsset(path.string());
    }

    // ------------------------------------------------------------------ Paketler (.cqpackage)

    bool ContentBrowserPanel::ExportPackage(const std::vector<fs::path>& files, const fs::path& file, std::string* error)
    {
        const fs::path projectDir = ProjectDir();
        if (projectDir.empty())
        {
            if (error) *error = "no project";
            return false;
        }
        std::vector<Archive::Entry> entries;
        std::set<std::string> added;
        std::error_code ec;
        auto addFile = [&](const fs::path& p) {
            const std::string rel = ProjectRelative(p);
            if (rel.empty() || rel.rfind("..", 0) == 0 || !fs::is_regular_file(p, ec) || !added.insert(rel).second)
                return;
            Archive::Entry e;
            e.Path = rel;
            e.Data = ReadBytes(p);
            entries.push_back(std::move(e));
        };
        for (const auto& f : files)
        {
            addFile(f);
            for (const auto& side : Sidecars(f))
                addFile(side);
        }
        if (entries.empty())
        {
            if (error) *error = "nothing to export";
            return false;
        }
        if (!Archive::WriteTarGz(file.string(), entries, error))
            return false;
        CQ_CORE_INFO("Exported {0} file(s) to {1}", entries.size(), file.string());
        return true;
    }

    void ContentBrowserPanel::BeginExportPackage(const std::vector<fs::path>& roots)
    {
        m_ExportRoots = roots;
        m_ExportItems.clear();
        std::set<std::string> seen;
        std::error_code ec;
        auto addItem = [&](const fs::path& p) {
            const std::string rel = ProjectRelative(p);
            if (rel.empty() || rel.rfind("..", 0) == 0 || !seen.insert(rel).second)
                return;
            PackageItem it;
            it.Path = rel;
            it.Size = (size_t)fs::file_size(p, ec);
            m_ExportItems.push_back(it);
        };
        for (const auto& root : roots)
        {
            if (fs::is_directory(root, ec))
            {
                for (fs::recursive_directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec))
                    if (it->is_regular_file(ec) && it->path().extension() != ".meta" && it->path().extension() != ".cqsprite")
                        addItem(it->path());
            }
            else
                addItem(root);
            if (m_ExportDependencies)
                for (const auto& d : CollectDependencies(root))
                    addItem(d);
        }
        std::sort(m_ExportItems.begin(), m_ExportItems.end(), [](const PackageItem& a, const PackageItem& b) { return a.Path < b.Path; });
        m_ExportOpen = true;
    }

    bool ContentBrowserPanel::BeginImportPackage(const fs::path& file, std::string* error)
    {
        std::vector<Archive::Entry> entries;
        if (!Archive::ReadTarGz(file.string(), entries, error))
            return false;
        const fs::path projectDir = ProjectDir();
        m_ImportFile = file.string();
        m_ImportItems.clear();
        m_ImportData.clear();
        std::error_code ec;
        for (auto& e : entries)
        {
            // Güvenlik: yalnız Assets/ ve Packages/ altı, ".." ve mutlak yol yok
            const fs::path rel(e.Path);
            bool safe = !rel.is_absolute() && (e.Path.rfind("Assets/", 0) == 0 || e.Path.rfind("Packages/", 0) == 0);
            for (const auto& part : rel)
                safe &= part != "..";
            if (!safe)
            {
                CQ_CORE_WARN("Import package: skipped unsafe path {0}", e.Path);
                continue;
            }
            PackageItem it;
            it.Path = e.Path;
            it.Size = e.Data.size();
            it.Exists = fs::exists(projectDir / rel, ec);
            m_ImportItems.push_back(it);
            m_ImportData.push_back({ e.Path, std::move(e.Data) });
        }
        if (m_ImportItems.empty())
        {
            if (error) *error = "the package has no project files";
            return false;
        }
        m_ImportOpen = true;
        return true;
    }

    int ContentBrowserPanel::CommitImportPackage()
    {
        const fs::path projectDir = ProjectDir();
        int written = 0;
        bool addons = false;
        std::error_code ec;
        for (size_t i = 0; i < m_ImportItems.size() && i < m_ImportData.size(); ++i)
        {
            if (!m_ImportItems[i].Selected)
                continue;
            const fs::path target = projectDir / m_ImportData[i].first;
            fs::create_directories(target.parent_path(), ec);
            std::ofstream f(target, std::ios::binary | std::ios::trunc);
            if (!f)
                continue;
            f.write(reinterpret_cast<const char*>(m_ImportData[i].second.data()), (std::streamsize)m_ImportData[i].second.size());
            f.close();
            ++written;
            addons |= m_ImportData[i].first.rfind("Packages/", 0) == 0;
            if (IsImageFile(target))
                Texture2D::ReloadFromDisk(target.string());
        }
        if (addons)
            AddonManager::Refresh();
        CQ_CORE_INFO("Imported {0} file(s) from {1}", written, m_ImportFile);
        m_ImportOpen = false;
        m_ImportItems.clear();
        m_ImportData.clear();
        m_Thumbnails.clear();
        m_Dirty = true;
        return written;
    }

    void ContentBrowserPanel::DrawPackageWindows()
    {
        auto drawList = [](std::vector<PackageItem>& items, bool importing) {
            ImGui::BeginChild("##pkgList", ImVec2(560.0f, 300.0f), ImGuiChildFlags_Borders);
            for (size_t i = 0; i < items.size(); ++i)
            {
                auto& it = items[i];
                ImGui::PushID((int)i);
                ImGui::Checkbox("##sel", &it.Selected);
                ImGui::SameLine();
                ImGui::TextUnformatted(it.Path.c_str());
                if (importing)
                {
                    ImGui::SameLine();
                    if (it.Exists)
                        ImGui::TextColored(UI::Colors::Warning, ICON_FA_ROTATE " Overwrite");
                    else
                        ImGui::TextColored(UI::Colors::Success, ICON_FA_PLUS " New");
                }
                ImGui::PopID();
            }
            ImGui::EndChild();
            if (ImGui::SmallButton("All"))
                for (auto& it : items)
                    it.Selected = true;
            ImGui::SameLine();
            if (ImGui::SmallButton("None"))
                for (auto& it : items)
                    it.Selected = false;
        };

        if (m_ExportOpen && !ImGui::IsPopupOpen("Exporting Package"))
            ImGui::OpenPopup("Exporting Package");
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        if (ImGui::BeginPopupModal("Exporting Package", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextDisabled("Items to export (paths in the project):");
            drawList(m_ExportItems, false);
            ImGui::SameLine();
            if (ImGui::Checkbox("Include dependencies", &m_ExportDependencies))
            {
                auto roots = m_ExportRoots;
                BeginExportPackage(roots);
            }
            ImGui::Separator();
            if (ImGui::Button(ICON_FA_FILE_EXPORT "  Export...", ImVec2(140.0f, 0.0f)))
            {
                nfdu8char_t* out = nullptr;
                nfdu8filteritem_t filter[1] = { { "Conqueror Package", "cqpackage" } };
                const std::string name = (m_ExportRoots.empty() ? std::string("Package") : m_ExportRoots.front().stem().string()) + ".cqpackage";
                if (NFD_SaveDialogU8(&out, filter, 1, nullptr, name.c_str()) == NFD_OKAY && out)
                {
                    std::vector<fs::path> files;
                    for (const auto& it : m_ExportItems)
                        if (it.Selected)
                            files.push_back(ProjectDir() / it.Path);
                    std::string error;
                    if (!ExportPackage(files, out, &error))
                        CQ_CORE_ERROR("Export package: {0}", error);
                    NFD_FreePathU8(out);
                    m_ExportOpen = false;
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f)))
            {
                m_ExportOpen = false;
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        if (m_ImportOpen && !ImGui::IsPopupOpen("Import Package"))
            ImGui::OpenPopup("Import Package");
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        if (ImGui::BeginPopupModal("Import Package", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextColored(UI::Colors::Accent, ICON_FA_BOX_ARCHIVE);
            ImGui::SameLine();
            ImGui::TextUnformatted(fs::path(m_ImportFile).filename().string().c_str());
            drawList(m_ImportItems, true);
            ImGui::Separator();
            if (ImGui::Button(ICON_FA_FILE_IMPORT "  Import", ImVec2(140.0f, 0.0f)))
            {
                CommitImportPackage();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(100.0f, 0.0f)))
            {
                m_ImportOpen = false;
                m_ImportItems.clear();
                m_ImportData.clear();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }

    // ------------------------------------------------------------------ Reimport

    void ContentBrowserPanel::Reimport(const fs::path& path)
    {
        std::error_code ec;
        if (path.empty() || !fs::exists(path, ec))
            return;
        if (fs::is_directory(path, ec))
        {
            for (fs::recursive_directory_iterator it(path, ec), end; !ec && it != end; it.increment(ec))
                if (it->is_regular_file(ec) && it->path().extension() != ".meta")
                    Reimport(it->path());
            return;
        }
        const std::string ext = Lower(path.extension().string());
        // Sahne ve prefab açık sahneyi değiştirmesin: yalnız içe aktarılan türler yeniden okunur
        if (ext == ".cqscene" || ext == PrefabSerializer::Extension || ext == ".meta")
            return;
        // Dosya izleyicileri (doku, malzeme, betik, Data Asset ...) değişiklik görsün
        if (!IsReadOnly(path))
            fs::last_write_time(path, fs::file_time_type::clock::now(), ec);
        if (IsImageFile(path))
            Texture2D::ReloadFromDisk(path.string());
        else if (IsModelFile(path))
        {
            const auto t0 = std::chrono::steady_clock::now();
            ModelLoader::Invalidate(path.string());
            if (s_ReloadModelInScene)
                s_ReloadModelInScene(path.string());
            else
                ModelLoader::Load(path.string());
            (void)t0;
        }
        else
            ImportActivity::Add(path.string(), IsAudioFile(path) ? "Audio" : ext == ".cqmat" ? "Material" : "Asset", 0.0, true, "reimport requested", true);
        m_Thumbnails.erase(path.string());
        m_Dirty = true;
    }

    void ContentBrowserPanel::ReimportAll()
    {
        const fs::path projectDir = ProjectDir();
        if (projectDir.empty())
            return;
        Reimport(projectDir / "Assets");
        m_Thumbnails.clear();
        CQ_CORE_INFO("Reimported all assets");
    }

    // ------------------------------------------------------------------ Properties pencereleri / model malzemeleri

    void ContentBrowserPanel::OpenProperties(const fs::path& path)
    {
        if (path.empty())
            return;
        if (std::find(m_PropertyWindows.begin(), m_PropertyWindows.end(), path) == m_PropertyWindows.end())
            m_PropertyWindows.push_back(path);
        ImGui::SetWindowFocus((ICON_FA_CUBE "  " + path.filename().string() + "###Properties" + path.string()).c_str());
    }

    void ContentBrowserPanel::DrawPropertyWindows()
    {
        for (size_t i = 0; i < m_PropertyWindows.size();)
        {
            const fs::path p = m_PropertyWindows[i];
            bool open = true;
            ImGui::SetNextWindowSize(ImVec2(420.0f, 560.0f), ImGuiCond_FirstUseEver);
            const std::string title = std::string(ICON_FA_CUBE "  ") + p.filename().string() + "###Properties" + p.string();
            if (ImGui::Begin(title.c_str(), &open))
            {
                std::error_code ec;
                if (fs::exists(p, ec))
                    AssetInspector::Draw(p.string());
                else
                    ImGui::TextDisabled("The asset no longer exists.");
            }
            ImGui::End();
            if (!open)
                m_PropertyWindows.erase(m_PropertyWindows.begin() + (long)i);
            else
                ++i;
        }
    }

    bool ContentBrowserPanel::ExtractModelMaterials(const fs::path& model)
    {
        if (IsReadOnly(model))
            return false;
        std::vector<std::string> created;
        std::string error;
        const fs::path outDir = model.parent_path() / "Materials";
        if (!ModelLoader::ExtractMaterials(model.string(), outDir.string(), created, &error))
        {
            CQ_CORE_ERROR("Extract materials from {0}: {1}", model.filename().string(), error);
            return false;
        }
        if (s_ReloadModelInScene)
            s_ReloadModelInScene(model.string());
        std::vector<fs::path> files(created.begin(), created.end());
        ShowResults("Extracted from " + model.filename().string(), files);
        m_Dirty = true;
        CQ_CORE_INFO("Extracted {0} file(s) from {1}", created.size(), model.filename().string());
        return true;
    }
}
