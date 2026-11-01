# Native scripting: C, C++, Rust and Zig

Scripts written in C (`.c`), C++ (`.cpp`, `.cc`, `.cxx`), Rust (`.rs`) and Zig (`.zig`) are compiled by the engine with
the language's own compiler into a shared library and loaded automatically when you press Play. You never build a DLL
by hand: edit a file, press Play (or keep playing — the library is rebuilt in the background and running instances are
migrated with their field values).

All four languages talk to the engine through the same small C ABI (`cq_plugin_describe`, `cq_create`, `cq_invoke`,
`cq_get_field` / `cq_set_field` ...). Every `nativebridge` function (the same 930+ functions the other script languages
use) is generated for each language in `Library/ScriptAssemblies/<language>/`.

| Language | Compiler that is looked for | SDK |
|---|---|---|
| C++ | `$CXX`, `c++`, `clang++`, `g++` (Windows: `clang-cl`, `cl`, `clang++`, `g++`) | `Resources/Scripting/Cpp/include/Conqueror/Script.h` |
| C (real C23, not C++) | `$CC`, `cc`, `clang`, `gcc` (Windows: `clang-cl`, `cl`, `clang`, `gcc`) | `Resources/Scripting/C/include/conqueror.h` |
| Rust | `$RUSTC`, `rustc`, `~/.cargo/bin/rustc` (no cargo, no external crates) | `Resources/Scripting/Rust/conqueror.rs` |
| Zig (0.14 – 0.17) | `$ZIG`, `zig`, or the `ziglang` Python package (`pip install ziglang`) | `Resources/Scripting/Zig/conqueror.zig` |

If a compiler is missing, the language is shown as unavailable with a hint; everything else keeps working.
The code editor checks C, C++, Rust and Zig files in the background and underlines errors as you type.

Use **Content Browser → Create → Script** to start from a template. Each template compiles as is.

## C

```c
#include <conqueror.h>
#include "nativebridge.h"            // every engine function as a macro

typedef struct Spinner {
    CQ_OBJECT;                       // must be the first member (holds the entity id)
    float speed;
    int ticks;
    char label[64];                  // string fields are fixed-size char arrays
} Spinner;

static void Spinner_Update(Spinner* self, const cq_value* args, int argc)
{
    float dt = cq_arg_float(args, argc, 0);
    cq_value r = GetRotation();                   // C numbers and strings convert automatically
    SetRotation(r.x, r.y + self->speed * dt, r.z);
    self->ticks++;
}

static void Spinner_PlayBtn(Spinner* self, const cq_value* args, int argc)
{
    cq_log("clicked: %s", cq_arg_str(args, argc, 1));
}

CQ_FIELDS(Spinner,
    CQ_RANGE(Spinner, speed, 90, 0, 360),         // slider in the Inspector
    CQ_INT(Spinner, ticks, 0),
    CQ_STRING(Spinner, label, "spin"))
CQ_METHODS(Spinner, CQ_ON(Spinner, Update), CQ_ON(Spinner, PlayBtn))
CQ_CLASS(Spinner)
```

Methods receive the arguments the engine passes (`dt` for `Update`, the value and kind for UI events, the other entity
for collisions ...). Helpers: `cq_arg_float/int/str`, `cq_to_num/int/bool/str`, `cq_num`, `cq_int`, `cq_str`,
`cq_vec3`, `cq_call("Name", argc, args)`, `cq_log/cq_warn/cq_error` (printf style). Field macros: `CQ_FLOAT`, `CQ_DOUBLE`,
`CQ_INT` (8/16/32/64-bit), `CQ_BOOL_FIELD`, `CQ_STRING`, `CQ_RANGE`.

## C++

```cpp
#include <Conqueror/Script.h>
#include "NativeBridge.g.h"

class Rotator : public cq::Script
{
    CQ_SCRIPT_CLASS(Rotator)
public:
    CQ_FIELD_RANGE(float, speed, 90.0f, 0, 360)
    void Update(float dt) override
    {
        cq::Value r = cq::GetRotation();
        cq::SetRotation(r.x(), r.y() + speed * dt, r.z());
    }
    void PlayBtn(float value, const char* kind) { cq::Log(kind); }
    CQ_METHOD(PlayBtn)
};
CQ_SCRIPT(Rotator)
```

## Rust

```rust
use crate::conqueror::*;
use crate::nativebridge::*;

script! {
    pub struct Spinner {
        #[range(0.0, 360.0)] speed: f32 = 90.0,   // Inspector fields with defaults
        ticks: i32 = 0,
        label: String = String::from("spin"),
    }
    methods [PlayBtn]                              // extra engine / UI events, handled in on_message
}

impl Script for Spinner {
    fn update(&mut self, dt: f32) {
        let r = cq!(GetRotation);                  // cq!(Function, args...) converts Rust values
        cq!(SetRotation, r.x(), r.y() + (self.speed * dt) as f64, r.z());
        self.ticks += 1;
    }
    fn on_message(&mut self, name: &str, args: &[Value]) -> bool {
        if name == "PlayBtn" { log(format!("clicked {}", args.len())); return true; }
        false
    }
}
```

Lifecycle methods: `awake`, `start`, `update(dt)`, `late_update(dt)`, `on_destroy`. Panics are caught and reported in the
console instead of crashing the editor. Field types: `f32`, `f64`, `i32`, `i64`, `u32`, `bool`, `String`.

## Zig

```zig
const std = @import("std");
const cq = @import("conqueror.zig");
const nb = @import("nativebridge.zig");

pub const Spinner = struct {
    speed: f32 = 90.0,                             // every public field with a default is an Inspector field
    ticks: i32 = 0,
    label: cq.String = cq.String.init("spin"),
    entity: u32 = 0,                               // optional: receives the entity id

    pub fn update(self: *Spinner, dt: f32) void {
        const r = nb.GetRotation(.{});
        _ = nb.SetRotation(.{ r.x(), r.y() + self.speed * dt, r.z() });
        self.ticks += 1;
    }

    pub fn PlayBtn(self: *Spinner, value: f32, kind: []const u8) void {
        cq.log("clicked {s} ({d})", .{ kind, value });
    }
};
```

Methods are found by name with comptime reflection (`update` and `Update` both match the engine's `Update`), and their
parameters are converted from the engine's arguments (`f32`/`f64`, integers, `bool`, `[]const u8`, `cq.Value`, or
`[]const cq.Value` for all of them). Fields starting with `_` are hidden. The first build of a project compiles parts
of Zig's standard library and takes a little longer; later builds are cached in `~/.cache/zig`.

## Native Script component (the engine's C++ API)

The **Native Script** component runs C++ classes that use the engine's own C++ API directly (`Entity`,
`GetComponent<T>()`, every component struct), unlike the C ABI scripts above. Write a class derived from
`Conqueror::ScriptableEntity` and register it with `CQ_NATIVE_SCRIPT`:

```cpp
#include "Scripting/ScriptableEntity.h"
#include "Scene/Components.h"

class Spinner : public Conqueror::ScriptableEntity
{
public:
    float Speed = 90.0f;   // degrees per second

    void OnUpdate(float deltaTime) override
    {
        auto& transform = GetComponent<Conqueror::TransformComponent>();
        transform.Rotation.y += glm::radians(Speed) * deltaTime;
    }
};

CQ_NATIVE_SCRIPT(Spinner)
```

**Create > Script > C++ Native Behaviour** in the Content Browser writes this template. Drag the `.cpp` onto a Native Script
component (or use its **C++ Source** button). When you press Play the engine compiles the file with the system C++
compiler (`$CXX`, `c++`, `clang++`, `g++`) against the engine headers into `Library/NativeScripts/lib<Name>.so` — only
when the file changed — loads it and creates the class. **Compile Now** in the Inspector compiles right away and shows
the compiler messages. Builds compile every file that contains `CQ_NATIVE_SCRIPT(` and ship the libraries in the
game's `NativeScripts/` folder, so players need no compiler.

The engine headers are found next to the editor (the engine source tree); set `CQ_ENGINE_SOURCE` if the editor runs
from elsewhere. Native Script libraries are built for the computer that builds the game: on Windows (where the engine
is linked statically) and in cross / Android builds use the C++ scripts of the Script component instead.
