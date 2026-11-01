# LuaQ language reference

LuaQ (`.luaq`) is Conqueror's own Lua dialect. Every Lua 5.1–5.4 program and every Luau program is valid LuaQ;
on top of that LuaQ adds classes, enums, pattern matching, optional chaining and more. LuaQ source is compiled to
Luau (line numbers are preserved, so error messages point at your `.luaq` lines) and runs on the embedded Luau VM,
with native code generation where the platform supports it.

## Script files in the engine

A LuaQ script is a module whose class becomes the component. If the file has no top-level `return`, the class with the
same name as the file (or the only class in the file) is returned automatically.

```lua
-- Spinner.luaq
class Spinner
    speed = 90.0          -- fields are shown in the Inspector (names starting with "_" are hidden)
    label = "spin"

    function Start()
        print(`started on {GetEntityName()}`)
    end

    function Update(dt: number)
        local r = GetRotation()
        SetRotation(r.x, r.y + self.speed * dt, r.z)
    end
end
```

All `nativebridge` functions are globals (also available as `nativebridge.Name`). Methods get `self` implicitly.
Engine callbacks are the same as in every language: `Awake`, `Start`, `Update(dt)`, `LateUpdate(dt)`, `FixedUpdate(dt)`,
collision / trigger callbacks, UI events and so on.

## From Lua 5.4

| Feature | Example | Notes |
|---|---|---|
| Bitwise operators | `a & b`, `a \| b`, `a ~ b`, `~a`, `a << 2`, `a >> 2` | 32-bit unsigned (Luau `bit32`) |
| Floor division | `7 // 2` | |
| `goto` and labels | `goto continue` … `::continue::` | all legal Lua gotos: forward, backward, out of nested loops |
| Constant locals | `local x <const> = 10` | assigning to it is a compile error |
| To-be-closed locals | `local f <close> = open()` | `__close` runs when the block is left (end, `return`, `break`, `continue`, `goto`) |
| Hex floats | `0x1p4`, `0xA.8p0` | |
| Library | `math.type`, `math.tointeger`, `math.ult`, `math.maxinteger`, `warn`, `utf8`, `string.pack`, `table.move` | |

Numbers are 64-bit floats (Luau); integers up to 2^53 are exact. `_ENV` and `load` of source strings are not available.

## From Luau

Type annotations (`local x: number`, `function f(a: T): R`, `type T = …`, `export type`, generics, `expr :: T` casts)
are accepted and erased. Also `continue`, compound assignment (`+= -= *= /= //= %= ^= ..=`), if-expressions
(`if a then b else c`), string interpolation (`` `hello {name}` ``), generalized iteration (`for k, v in t do`) and
function attributes such as `@native`.

## LuaQ additions

### Classes

```lua
class Animal
    name = "animal"               -- field default (also the Inspector value for engine scripts)
    static count = 0

    function init(name)           -- constructor, called by Animal("cat")
        self.name = name
        Animal.count += 1
    end

    function speak() return self.name .. " makes a sound" end
    function __tostring() return "Animal(" .. self.name .. ")" end   -- metamethods work and are inherited
    static function create(n) return Animal(n) end
end

class Dog extends Animal
    function init(name, breed)
        super.init(self, name)    -- or super(name)
        self.breed = breed
    end
    function speak() return super:speak() .. " (woof)" end
end

print(isinstance(Dog("rex", "lab"), Animal))   -- true
```

`Class.__super` is the base class and `Class.__name` the class name.

### Enums and constants

```lua
enum Color { Red, Green = 5, Blue }   -- Red = 0, Green = 5, Blue = 6 (a frozen table)
const LIMIT = 100
```

### Pattern matching

```lua
match value
    case 1, 2 then print("small")
    case "x" then print("letter")
    case _ if type(value) == "number" and value > 100 then print("huge")
    else print("other")
end
```

Patterns are compared with `==`; `_` matches anything; `if` adds a guard.

### Functions

```lua
local double = x => x * 2
local add = (a, b) => a + b
local sum = (n) => do local s = 0 for i = 1, n do s += i end return s end

function greet(name = "world", punct = "!")   -- default parameters
    return "hi " .. name .. punct
end

call(10, ...list)                    -- spread a table as the last argument
```

### Nil-safe access

```lua
local w = config?.window?.size?.w     -- nil if any step is nil
target?:Damage(10)                     -- method call only if target is not nil
local name = player.name ?? "guest"    -- default only when the left side is nil (false is kept)
settings.volume ??= 0.8                -- assign only if nil
```

### Destructuring and spread

```lua
local { name, age } = person          -- by field name
local [x, y] = point                  -- by index
local all = { ...defaults, ...overrides, extra = true }
```

### defer

```lua
function save()
    local file = open()
    defer file:close()                -- runs when the scope is left, in reverse order
    defer do print("saved") end
    ...
end
```

### async / await

```lua
async function blink(times)
    for i = 1, times do
        SetVisible(false)
        await 0.2                      -- wait 0.2 s (the engine resumes the task every frame)
        SetVisible(true)
        await 0.2
    end
    return "done"
end

async function run()
    local result = await blink(3)      -- wait for another task and take its results
end

spawn(function() wait(1) print("one second later") end)
```

An `async function` returns a task immediately; `await` accepts seconds, a task, or a function (run as a task).

### Operator aliases

`!=` (`~=`), `!x` (`not x`), `&&` (`and`), `||` (`or`).

## Compatibility notes

LuaQ keywords are contextual, so existing Lua code that uses `class`, `match`, `enum`, `const`, `defer` or `async` as
names keeps working. Bitwise results are unsigned 32-bit (`~0` is `4294967295`). `defer` and `<close>` do not run when an
error unwinds the scope; use `pcall` for that.
