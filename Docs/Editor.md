# Editor

## Layout

| Panel | Purpose |
|-------|---------|
| Ribbon (top) | HOME / CREATE / PLAY / VIEW / ADDONS tabs: clipboard, transform tools, snapping, insert shapes and lights, play controls, recording |
| Hierarchy | Entities of the open scene; drag to re-parent, right-click for create / duplicate / delete, search with `t:Type` filters |
| Viewport | The scene camera: Shaded / Wireframe modes, 2D / 3D switch, gizmos, grid, camera settings, Stats overlay |
| Game | What the primary camera sees in play mode; aspect presets, scale, Maximize On Play, mute |
| Inspector | Components of the selected entity (see [Components](Reference/Components.md)) |
| Content Browser | Project files; create assets, import by drag and drop, double-click to open |
| Console / Log | Engine log, and the console for [console variables](Reference/ConsoleVariables.md) and commands |
| Lighting | Environment, sky, fog, baked lighting |
| Project Settings | Player, Quality, Graphics, Audio, Input, Physics, Navigation, Tags and Layers, Script Execution Order |

Other windows (Window menu): Animator, Timeline, Shader Graph, Visual Script editor, Code editor, Audio Mixer, Sprite
Editor, Tile Palette, Terrain tools, Profiler (Ctrl+7), AI Assistant, Multiplayer, Build Settings (Ctrl+Shift+B).

## Viewport navigation

| Input | Action |
|-------|--------|
| Right mouse drag | Look around (3D) / pan (2D) |
| WASD + Q / E | Move forward, left, back, right, down / up (Shift: faster); disabled in play mode |
| Alt + left mouse | Orbit (3D) |
| Alt + middle mouse | Pan |
| Alt + right mouse | Zoom |
| Mouse wheel | Zoom |
| F | Frame the selected entity |
| Click | Select (in the Hierarchy: Ctrl+click toggles, Shift+click selects a range) |

## Keyboard shortcuts

| Shortcut | Action |
|----------|--------|
| Q / W / E / R / Y | No tool / Move / Rotate / Scale / Universal gizmo (when not flying) |
| Ctrl+Z / Ctrl+Y | Undo / Redo |
| Ctrl+C / Ctrl+X / Ctrl+V | Copy / Cut / Paste entities |
| Ctrl+D | Duplicate |
| Ctrl+A | Select all |
| F2 | Rename |
| Delete | Delete selection |
| Ctrl+N / Ctrl+O | New scene / Open scene |
| Ctrl+S / Ctrl+Shift+S | Save / Save As |
| Ctrl+Shift+B | Build Settings |
| Ctrl+7 | Profiler |

When the Content Browser has focus, copy / paste / rename / delete apply to files instead of entities.

## Play mode

**Play** copies the edit scene and runs it: scripts start, physics simulates, audio plays. **Pause** and **Step** stop
the simulation or advance one frame. **Stop** discards the runtime copy. The Realtime toggle in the Viewport animates
effects (particles, water, sky) in edit mode as well.

## Prefabs

Drag an entity from the Hierarchy into the Content Browser to create a `.cqprefab`; drag the prefab into the scene (or
call `Instantiate` from a script) to create instances.

## Creating assets

Right-click in the Content Browser (on an asset or on an empty area) for the project menu, as in Unity's Project window:

- **Search** at the top finds any entry of the menu, including every Create entry (type "mat" to see Material, Material
  Variant, Physics Material ...).
- **Asset Options**: Cut, Copy, Paste, Delete, Rename and Properties as icons (Ctrl+X / Ctrl+C / Ctrl+V, Del, F2, Alt+P).
  Copies keep their import settings (`.meta`) and sprite slicing.
- **Other Options related to asset type**: for images Open in Sprite Editor, Create Material / Tile / Terrain Layer /
  Brush / Sprite Atlas; Create Material Variant, Create Prefab Variant, Open Scene Additive, Create Scene Template From
  Scene, Extract Materials (models), Create *Class* Asset (Data Asset scripts), Edit Asset.
- **Create** holds every asset type in groups: Scene, 2D, Rendering, Shader, Physics, Terrain, Audio, Animation, Timeline,
  Scripting, Data and Input (Folder at the top, Text File at the bottom). The **+▾** button in the Content Browser
  toolbar opens the same Create menu.
- **Open Containing Folder**, **Open**, **Open Scene Additive** (adds the scene's objects under a `[Level]` root; they
  are not saved into the open scene, **Save Level** in that root's Inspector writes them back to their own file and
  **Unload Level** removes them), **Delete**, **Rename**, **Copy Path** (Ctrl+Alt+C).
- **Packages**: View in Addon Manager, Create Addon Package, **Import Package > Custom Package** and **Export Package**
  (`.cqpackage`, a tar.gz of project files: the export window lists the asset with its dependencies, the import window
  marks each file New or Overwrite), Export As Addon Package (addon folders), Import New Asset.
- **Extract Materials** (models): writes the model's materials and embedded textures as `.cqmat` files into a
  `Materials` folder next to the model and remaps the model to them; the model's Inspector lists the remapped materials
  and can switch any of them to another `.cqmat` or back to the embedded one.
- **Find References In Scene** selects the objects that use the asset, **Find References In Project** and
  **Select Dependencies** show the matching files in the Content Browser (Clear returns to the folder).
- **Refresh** (Ctrl+R), **Reimport**, **Reimport All**, **Open Script Project** (External Script Editor or the Code
  Editor), **View in Import Activity Window** (Window > Import Activity lists every texture, model, material and audio
  import with its duration and result) and **Properties...** (a floating Inspector for the asset).

A new asset is selected and ready to rename; its settings appear in the Inspector.

A Physics Material (`.cqphysmat`) sets friction and bounciness. Drop it on a collider's Material slot (or onto the
object in the Scene view); it replaces the Rigidbody's own friction and restitution.

An Animator Override Controller (`.cqoverride`, Create > Animation) reuses an Animator Controller with other clips, like
Unity's: pick the base **Controller**, set **Clips From** to a model to list its clips, and choose an **Override** for any
clip of the controller (None keeps the original). Put the file in the Animation component's Controller slot; states,
transitions and parameters come from the base controller, which the Animator window opens for editing.

The **Packages** folder is read-only, as in Unity: nothing can be created, pasted, moved, renamed or deleted there (the
Create menu and **+▾** show "Packages are read-only"). Built-in meshes (Packages/Core/Meshes), materials, sprites and
textures are picked from there with the target buttons.

## Undo

Inspector edits, gizmo drags, create / delete and re-parent operations are recorded (Ctrl+Z / Ctrl+Y).
