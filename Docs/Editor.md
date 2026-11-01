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

Right-click an empty area of the Content Browser to create assets directly: Folder, Scene, Prefab, Material, Material
Variant (with a material selected), Shader Graph, Shader (Code), Render Texture, Physics Material, Animator Controller,
Anim Montage, Timeline, scripts in every installed language, Visual Script Graph and Text File. The **+ Create** button
shows the same list. A new asset is selected and ready to rename; its settings appear in the Inspector.

A Physics Material (`.cqphysmat`) sets friction and bounciness. Drop it on a collider's Material slot (or onto the
object in the Scene view); it replaces the Rigidbody's own friction and restitution.

## Undo

Inspector edits, gizmo drags, create / delete and re-parent operations are recorded (Ctrl+Z / Ctrl+Y).
