# Conqueror's Engine documentation

## Guides

| Page | Contents |
|------|----------|
| [Getting started](GettingStarted.md) | Building the engine, the launcher, creating a project, project layout, first scene |
| [Editor](Editor.md) | Panels, ribbon, viewport navigation, keyboard shortcuts, play mode, prefabs, undo |
| [Scripting](Scripting.md) | The ten script languages, script lifecycle, Inspector fields, hot reload, calling the engine |
| [Rendering](Rendering.md) | Graphics backends, render pipeline, lighting, shadows, post-processing, debugging |
| [Platforms and building games](Platforms.md) | Build Settings, target platforms, what is tested where |
| [Testing and automation](Testing.md) | Driving the editor from scripts, screenshots, pixel checks, the regression workflow |
| [LuaQ](LuaQ.md) | The LuaQ language (Lua 5.4 + Luau superset with classes, match, async/await ...) |
| [Native scripting](NativeScripting.md) | C++, C, Rust and Zig scripts compiled to native libraries |
| [Automotive](Automotive.md) | Android Automotive, Automotive Grade Linux, QNX, OpenGL / GL ES profiles |

## Reference (generated from the engine)

These pages are produced by `Tools/Docs/gen_docs.py` from the live registries (see the script header), so they list
exactly what the engine contains:

| Page | Contents |
|------|----------|
| [Components](Reference/Components.md) | Every component with its fields, types and default values |
| [Scripting API](Reference/ScriptingAPI.md) | Every native function callable from scripts, by category |
| [Console variables](Reference/ConsoleVariables.md) | `r.*`, `p.*`, `au.*` ... variables and console commands |
| [Automation commands](Reference/Automation.md) | Commands for automated tests and tooling |

To regenerate after changing components, natives or console variables:

```sh
printf 'wait 30\ndocs_dump /tmp/cqdocs\nquit\n' > /tmp/docs.txt
CQ_AUTOMATION=/tmp/docs.txt ./build/bin/ConquerorEngine <any project>
python3 Tools/Docs/gen_docs.py /tmp/cqdocs
```
