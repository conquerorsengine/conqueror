# Scripting

## Languages

A script is attached to an entity with the Script component (or by dragging the file onto the entity). All languages see
the same engine API (see [Scripting API](Reference/ScriptingAPI.md)) and the same lifecycle; pick per script.

| Language | Extension | Runtime | Notes |
|----------|-----------|---------|-------|
| ConquerorScript | `.cqs` | own interpreter | C#-like syntax made for the engine |
| Luau | `.luau` | Luau VM (Roblox's typed Lua) | gradual typing, fast |
| LuaQ | `.luaq` | compiled to Luau | Lua 5.4 + Luau + classes, `match`, `?.`, `??`, `defer`, `async`/`await` ([details](LuaQ.md)) |
| Python | `.py` | embedded CPython 3 | method names in snake_case (`update`, `on_collision_enter`) |
| C# | `.cs` | .NET (hosted) | class derives from the API in `Resources/Scripting/CSharp` |
| Java | `.java` | JVM (JNI) | |
| C++ | `.cpp` | compiled to a native library | [Native scripting](NativeScripting.md) |
| C | `.c` | compiled as C23 | |
| Rust | `.rs` | `rustc` cdylib | |
| Zig | `.zig` | `zig build-lib` | |
| Visual Script | `.cqgraph` | node graph | the same functions as nodes |

## Lifecycle

| Callback | When |
|----------|------|
| `Awake` | once, when the instance is created (before any Start) |
| `Start` | once, before the first Update of this script |
| `Update(dt)` | every frame |
| `FixedUpdate(dt)` | every physics step (Project Settings > Time) |
| `LateUpdate(dt)` | every frame after all Updates (cameras, follow logic) |
| `OnDestroy` | when the entity or the script is removed, and at Stop |
| `OnCollisionEnter / Stay / Exit(other)` | physics contacts (2D and 3D) |
| `OnTriggerEnter / Stay / Exit(other)` | trigger volumes |
| `OnMouseEnter / Over / Exit / Down / Up / Drag / UpAsButton` | pointer over the entity's collider (Unity semantics) |

Script Execution Order (Project Settings) fixes the order between classes.

## Inspector fields

Public fields (class attributes in Python / Luau tables, `public` in ConquerorScript and C#) appear in the Inspector and
are saved with the scene. Names starting with `_` stay hidden.

```cqs
using nativebridge;

script Spinner : ConquerorScript
{
    public float speed = 90.0;
    float rotationY = 0.0;

    func Update(dt)
    {
        rotationY += speed * dt;
        SetRotation(0, rotationY, 0);
    }
}
```

```python
from nativebridge import *

class Spinner:
    speed = 90.0
    _angle = 0.0

    def update(self, dt):
        self._angle += self.speed * dt
        SetRotation(0, self._angle, 0)
```

```lua
--!strict
local Spinner = {}
Spinner.speed = 90.0
Spinner._angle = 0.0

function Spinner:Update(dt: number)
    self._angle += self.speed * dt
    SetRotation(0, self._angle, 0)
end

return Spinner
```

## Hot reload

Saving a script while the editor runs reloads it without restarting the editor. Compile errors appear in the Console
and in the code editor.

## Talking to other entities

`FindEntity("Name")`, `FindEntitiesWithTag`, `FindEntitiesWithScript`, `GetComponentField("Component", "Field")` /
`EntityGetComponentField(entity, ...)`, `SetComponentField(...)`, `InvokeMethod(...)`, Signals (`SignalConnect` / `SignalEmit`) and console access (`CVarGet` / `CVarSet` / `ConsoleCommand`)
cover most cases. See the [Scripting API](Reference/ScriptingAPI.md) for the full list.
