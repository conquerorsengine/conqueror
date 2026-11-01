#include "HubLearn.h"
#include "Scripting/NativeDocs.h"

#include <imgui.h>
#include <IconsFontAwesome6.h>

#include <algorithm>
#include <cctype>
#include <map>
#include <sstream>

namespace Conqueror::Launcher
{
    const std::vector<LearnGuide>& GetGuides()
    {
        static const std::vector<LearnGuide> s_Guides = {
            { "Getting Started", ICON_FA_FLAG_CHECKERED, "Editor layout, creating objects, Play mode and saving.",
              R"(# The editor at a glance
The Hierarchy (left) lists the objects of the open scene. The Scene view (center) is where you build the level; the Game view shows what the player sees. The Inspector (right) edits the selected object's components. The Content Browser (bottom) holds your project's files.

# Create and arrange objects
- Right-click the Hierarchy or use GameObject to add cubes, sprites, lights, tilemaps and more.
- Drag a prefab, model, sprite or sound from the Content Browser into the Scene view or Hierarchy.
- Move (W), rotate (E) and scale (R) with the gizmo. F frames the selection. Ctrl+D duplicates.
- Every Inspector change can be undone with Ctrl+Z.

# Play your game
Press Play (Ctrl+P). The Game view receives keyboard and mouse input while it is focused - click it first. Changes made while playing are thrown away when you stop, just like in Unity.

# Save
Ctrl+S saves the scene. The project file (.cqproj) stores the start scene and project-wide settings such as physics and layers.)" },
            { "Scripting", ICON_FA_CODE, "Write gameplay code in ConquerorScript, Luau, Python, C#, Java or C++.",
              R"(# Your first script
Create a script from the Content Browser (Create > Script) and drag it onto an object. Public fields appear in the Inspector.
```
using nativebridge;

script Spinner : ConquerorScript
{
    public float speed = 90.0;

    func Update(dt)
    {
        var r = GetRotation();
        SetRotation(r.x, r.y + speed * dt, r.z);
    }
}
```
# Lifecycle
- Awake / Start: once, when the object starts playing.
- Update(dt) every frame, LateUpdate(dt) after all updates, FixedUpdate(dt) every physics step.
- OnCollisionEnter / Stay / Exit and OnTriggerEnter / Exit (2D: ...2D) receive (other, name).

# Input
IsKeyPressed("Space") is true while held, IsKeyDown("Space") only on the frame it was pressed. GetMousePosition(), IsMouseButtonDown(0) and ScreenPointToRay(x, y) let you pick objects.

# Other languages
The same engine API ("nativebridge") is available in Luau (.luau), Python (.py), C# (.cs), Java (.java) and C/C++ (.cpp). Visual Scripts (.cqgraph) compile to ConquerorScript.)" },
            { "Physics", ICON_FA_WEIGHT_HANGING, "Rigidbodies, colliders, joints, queries and the MuJoCo backend.",
              R"(# Bodies and colliders
Add a Rigidbody and a collider (Box, Sphere, Capsule, Mesh) to make an object fall and collide. A collider without a Rigidbody is static scenery. Colliders on children combine into their parent's body.

# 2D
2D games use Rigidbody 2D with Box / Circle / Capsule / Polygon / Edge colliders, joints and effectors (one-way platforms, buoyancy, area forces). Tilemap Collider 2D turns solid tiles into collision.

# Queries
```
var hit = Raycast(p.x, p.y, p.z, 0, -1, 0, 10);
if (hit != null) { print(hit.name, hit.distance); }
```
# Physics engines
Project Settings > Physics selects Jolt (default, fastest) or MuJoCo (research-grade articulated dynamics). Both use the same components.)" },
            { "Prefabs & Addons", ICON_FA_CUBES_STACKED, "Reusable objects and editor extensions.",
              R"(# Prefabs
Drag an object from the Hierarchy into the Content Browser to save it as a .cqprefab. Drag the prefab back into the scene to create instances. Edit an instance and press Apply in its Prefab Instance component to update the asset, or Revert to discard changes. Scripts can spawn prefabs:
```
var enemy = Instantiate("Assets/Prefabs/Enemy.cqprefab", 0, 1, 5);
```
# Addons
Addons extend the editor with menus, ADDONS-tab tools, panels, script components and prefabs. Open Addons > Addon Manager to install built-in addons (Quick Tools, Scene Stats, Hex Board Kit, Day / Night Cycle) or press Create to start your own. An addon is a folder with addon.yaml and an editor script:
```
script MyTool : ConquerorScript
{
    func OnLoad() { EditorAddToolButton("Hello", "star", "SayHello"); }
    func SayHello() { EditorNotify("Hello from my addon!"); }
}
```)" },
            { "2D, Tilemaps & Grids", ICON_FA_TABLE_CELLS, "Sprites, tile painting, hex grids and pathfinding.",
              R"(# Tilemaps
GameObject > 2D Object > Tilemap creates a Grid with a Tilemap. Open Window > Tile Palette to paint (B), box fill (U), flood fill (G), erase (D) and pick (I). Grids can be rectangular, isometric or hexagonal, flat in 2D or on the XZ plane as a 3D board.

# Pathfinding
```
var path = GridFindPath(map, 0, 0, 5, 3);
for (var cell in path) { print(cell.x, cell.y); }
```
GridNeighbors, GridDistance (true hex distance), GridCellToWorld and GridScreenToCell (mouse picking) make board games easy.)" },
            { "Keyboard Shortcuts", ICON_FA_KEYBOARD, "The most useful editor shortcuts.",
              R"(# Editing
- Ctrl+Z / Ctrl+Y: undo / redo
- Ctrl+C / Ctrl+V / Ctrl+X / Ctrl+D: copy, paste, cut, duplicate
- Delete: delete selection
- F: frame selection, Q/W/E/R: select, move, rotate, scale

# Scene and play
- Ctrl+S: save scene, Ctrl+N: new scene, Ctrl+O: open scene
- Ctrl+P: play / stop

# Tile Palette
- B brush, U box fill, G flood fill, D erase, I picker, [ and ] brush size, 1-9 select tile)" },
        };
        return s_Guides;
    }

    void DrawMarkup(const char* text)
    {
        std::istringstream in(text);
        std::string line;
        bool code = false;
        std::string codeBlock;
        auto flushCode = [&]() {
            if (codeBlock.empty())
                return;
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.09f, 0.1f, 0.12f, 1.0f));
            const float lines = (float)std::count(codeBlock.begin(), codeBlock.end(), '\n') + 1.0f;
            ImGui::BeginChild(ImGui::GetID(codeBlock.c_str()), ImVec2(-1, lines * ImGui::GetTextLineHeightWithSpacing() + 16.0f), ImGuiChildFlags_Borders);
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.72f, 0.86f, 1.0f, 1.0f));
            ImGui::TextUnformatted(codeBlock.c_str());
            ImGui::PopStyleColor();
            ImGui::EndChild();
            ImGui::PopStyleColor();
            codeBlock.clear();
        };
        while (std::getline(in, line))
        {
            if (line.rfind("```", 0) == 0)
            {
                if (code)
                    flushCode();
                code = !code;
                continue;
            }
            if (code)
            {
                codeBlock += (codeBlock.empty() ? "" : "\n") + line;
                continue;
            }
            if (line.rfind("# ", 0) == 0)
            {
                ImGui::Spacing();
                ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.25f);
                ImGui::TextColored(ImVec4(0.55f, 0.75f, 1.0f, 1.0f), "%s", line.c_str() + 2);
                ImGui::PopFont();
                continue;
            }
            if (line.rfind("- ", 0) == 0)
            {
                ImGui::Bullet();
                ImGui::SameLine();
                ImGui::TextWrapped("%s", line.c_str() + 2);
                continue;
            }
            if (line.empty())
            {
                ImGui::Spacing();
                continue;
            }
            ImGui::TextWrapped("%s", line.c_str());
        }
        flushCode();
    }

    void DrawApiReference(std::string& search)
    {
        char buf[128];
        std::snprintf(buf, sizeof(buf), "%s", search.c_str());
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputTextWithHint("##apisearch", ICON_FA_MAGNIFYING_GLASS "  Search functions (e.g. Raycast, Tilemap, Audio)", buf, sizeof(buf)))
            search = buf;
        std::string q = search;
        std::transform(q.begin(), q.end(), q.begin(), [](unsigned char c) { return (char)std::tolower(c); });

        // Kategoriye göre grupla (sıralı)
        std::map<std::string, std::vector<std::pair<std::string, const Scripting::NativeDoc*>>> byCategory;
        for (const auto& [name, doc] : Scripting::GetNativeDocs())
        {
            if (!q.empty())
            {
                std::string hay = name + " " + doc.Description + " " + doc.Category;
                std::transform(hay.begin(), hay.end(), hay.begin(), [](unsigned char c) { return (char)std::tolower(c); });
                if (hay.find(q) == std::string::npos)
                    continue;
            }
            byCategory[doc.Category].push_back({ name, &doc });
        }
        size_t total = 0;
        for (auto& [cat, list] : byCategory)
            total += list.size();
        ImGui::TextDisabled("%zu functions", total);
        ImGui::BeginChild("##apilist", ImVec2(0, 0), ImGuiChildFlags_None);
        for (auto& [cat, list] : byCategory)
        {
            std::sort(list.begin(), list.end(), [](auto& a, auto& b) { return a.first < b.first; });
            if (!ImGui::CollapsingHeader((cat + " (" + std::to_string(list.size()) + ")").c_str(), q.empty() ? 0 : ImGuiTreeNodeFlags_DefaultOpen))
                continue;
            for (auto& [name, doc] : list)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.72f, 0.86f, 1.0f, 1.0f));
                ImGui::TextWrapped("%s", doc->Signature);
                ImGui::PopStyleColor();
                ImGui::Indent(14.0f);
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.72f, 0.76f, 1.0f));
                ImGui::TextWrapped("%s", doc->Description);
                ImGui::PopStyleColor();
                ImGui::Unindent(14.0f);
                ImGui::Spacing();
            }
        }
        ImGui::EndChild();
    }
}
