# Scripting

## Languages

A script is attached to an entity with the Script component (or by dragging the file onto the entity). All languages see
the same engine API (see [Scripting API](Reference/ScriptingAPI.md)) and the same lifecycle; pick per script.

| Language | Extension | Runtime | Notes |
|----------|-----------|---------|-------|
| ConquerorScript | `.cqs` | own interpreter | C#-like syntax made for the engine |
| LuaQ | `.luaq` | compiled to Luau, runs on the embedded Luau VM | Lua 5.4 + Luau + classes, `match`, `?.`, `??`, `defer`, `async`/`await` ([details](LuaQ.md)) |
| C# | `.cs` | .NET (hosted) | class derives from the API in `Resources/Scripting/CSharp` |
| Python | `.py` | embedded CPython 3 | method names in snake_case (`update`, `on_collision_enter`) |
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

Public fields (class attributes in Python and LuaQ, `public` in ConquerorScript and C#) appear in the Inspector and
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

## Data Assets

A Data Asset (Unity ScriptableObject, Unreal Data Asset) is a file of values made from a script class that derives from
`DataAsset`. The class's public fields are the template; every `.cqdata` file made from it keeps its own values.

1. **Create > Data Asset Script** and pick a language: ConquerorScript, C#, Java, Python or C++. Rename the file right
   away; the class is renamed with it.
2. **Create > Data Asset > *YourClass*** makes a `.cqdata` file. Select it to edit its values in the Inspector. Edits are
   written to the file at once and can be undone with Ctrl+Z.
3. Read it in game:

```cqs
var sword = LoadDataAsset("Assets/Sword.cqdata");   // class defaults + the values set in the Inspector
var damage = sword.damage;
var range = DataAssetGet("Assets/Sword.cqdata", "range");
```

```csharp
var sword = DataAsset.Load<WeaponData>("Assets/Sword.cqdata");   // C#: public class WeaponData : DataAsset
```

```java
WeaponData sword = DataAsset.load(WeaponData.class, "Assets/Sword.cqdata");   // Java: extends DataAsset
```

```python
sword = WeaponData.load("Assets/Sword.cqdata")   # Python: class WeaponData(DataAsset)
```

```cpp
#include "WeaponData.h"   // C++: class WeaponData : public cq::DataAsset, in a header
auto sword = cq::LoadDataAsset<WeaponData>("Assets/Sword.cqdata");
```

A string field marked `[Asset(".cqdata")]` becomes a slot: drop a Data Asset on it or pick one with the circle button.

- A Data Asset class cannot be added to an object: the editor shows **Can't add script**, and Add Component does not
  list it.
- If the class is renamed or removed, the asset keeps its values and the Inspector asks you to fix the script or assign
  another one. Values of fields the class no longer has are listed with a **Remove Unused Values** button.
- C++ Data Asset classes live in a header so other scripts can `#include` them; the engine compiles them even when no
  script includes them.
- LuaQ, C, Rust, Zig and Visual Scripts read Data Assets with `LoadDataAsset` and `DataAssetGet`.

## Input Actions

**Create > Input Actions** makes a `.cqinputactions` asset (Unity Input System, Unreal Input Actions + Mapping Context).
A new asset already has a Player map (Move, Look, Attack, Interact, Crouch, Jump, Previous, Next, Sprint) and a UI map,
plus the Keyboard&Mouse and Gamepad control schemes.

Double-click the asset to open the **Input Actions** window:

- **Action Maps | Actions | Properties** panes. Rename with F2 or a double-click, reorder by dragging, Ctrl+C / Ctrl+V /
  Ctrl+D / Del, Ctrl+Z / Ctrl+Y, Save Asset (Ctrl+S) or Auto-Save.
- **Actions**: Action Type (Value, Button, Pass Through), Control Type, Initial State Check, Interactions (Hold, Tap,
  Slow Tap, Multi Tap, Press) and Processors (Invert, Normalize, Scale, Clamp, Dead Zone ...).
- **Bindings**: pick a control from the Keyboard / Mouse / Gamepad tree, or press **Listen** and press the key or button.
  Composites: 2D Vector (WASD), 1D Axis, One Modifier and Two Modifiers.
- **Control schemes**: the combo in the toolbar adds, edits, duplicates or deletes schemes. With a scheme selected, the
  list shows its bindings only and new bindings join it.

Use it from a **Player Input** component: set **Actions** to the asset, choose the **Default Map** and **Default Scheme**
and the **Behavior**. With Send Messages the scripts on the object receive `On<Action>(value)` (`OnMove(x, y)`,
`OnJump(v)`) when an action is performed, and a Value action sends its zero value when it is released. Broadcast Messages
also reaches the children. Scripts can poll the same state:

```cqs
var move = PlayerInputVector("Move");             // {x, y}
if (PlayerInputPerformed("Interact")) { Use(); }  // a Hold of 0.4 s completed
PlayerInputSwitchMap("UI");
```

**Project Settings > Input > Project-wide Actions** makes one asset active everywhere without a Player Input:
`InputActionVector("Player/Move")`, `InputActionPerformed("Jump")`, `InputActionPhase("Interact")`. The keyboard,
the mouse and every gamepad drive it.

## Hot reload

Saving a script while the editor runs reloads it without restarting the editor. Compile errors appear in the Console
and in the code editor.

## Talking to other entities

`FindEntity("Name")`, `FindEntitiesWithTag`, `FindEntitiesWithScript`, `GetComponentField("Component", "Field")` /
`EntityGetComponentField(entity, ...)`, `SetComponentField(...)`, `InvokeMethod(...)`, Signals (`SignalConnect` / `SignalEmit`) and console access (`CVarGet` / `CVarSet` / `ConsoleCommand`)
cover most cases. See the [Scripting API](Reference/ScriptingAPI.md) for the full list.
