# Testing and automation

The editor and the player can be driven by a plain-text script. This is how the engine's own features are tested and
it is available for game projects too (smoke tests, screenshots for store pages, performance captures).

## Running a script

```sh
cat > /tmp/smoke.txt <<'EOF'
wait 30
open_scene Assets/Scenes/Main.cqscene
focus Game
play
wait 120
game_pixel_world 0 1 0 r >100
screenshot /tmp/smoke.png
stop
quit
EOF
CQ_AUTOMATION=/tmp/smoke.txt ./build/bin/ConquerorEngine /path/to/MyGame
cat /tmp/smoke.txt.result.txt
```

Each line is one command; `#` starts a comment. The result file lists every command with `OK` or `FAIL <reason>` and
ends with `DONE failures=<n>`. The process exit code is 0 when the script ran to the end.

Built games accept the same variable (`CQ_AUTOMATION=... ./MyGame`) with the runtime subset of commands.

## Common commands

| Command | Purpose |
|---------|---------|
| `wait <frames>`, `wait_until <condition>` | let frames pass / wait for a condition |
| `new_scene`, `open_scene <path>`, `save_scene` | scene management |
| `create_primitive <shape> <name> x y z`, `entity create/move/rotate/scale` | build test scenes |
| `comp add/set/expect <entity> <component> [field value]` | add components, set and check fields |
| `script add <entity> <file>`, `script expect <entity> <field> <cond>` | attach scripts, check script state |
| `play`, `stop`, `pause` | play mode |
| `key`, `click`, `move`, `gamepad ...` | simulated input |
| `game_pixel_world x y z <channel> <cond>` | colour of a world point in the Game view |
| `game_region u0 v0 u1 v1 <channel> <min/max/mean> <cond>` | statistics over a Game view region |
| `screenshot <png>` | capture the window |
| `profiler record on`, `profiler export <json>`, `profiler report` | performance capture (Chrome trace format) |
| `cvar <name> <value>` | set a console variable |
| `docs_dump <dir>` | write the registries used to generate the reference docs |

The complete list with descriptions is in [Automation commands](Reference/Automation.md).

## Graphics backends

Set `CQ_RENDERER=vulkan|d3d11|d3d12|metal|opengl` to run the same script on another backend; the translation layers
are expected to produce the same pixels (within a few values) as OpenGL. The Windows build can be tested on Linux
under Wine (use a Wine virtual desktop, `wine explorer /desktop=name,1920x1080 ConquerorEngine.exe ...`, so the window
does not start minimized).

## Headless runs

On a machine without a display, run inside a headless compositor, e.g.
`dbus-run-session -- mutter --headless --virtual-monitor 1920x1080 -- sh -c 'CQ_AUTOMATION=... ./ConquerorEngine ...'`.

## Regression tests

`Tools/Tests/regression.py` runs the engine's regression suite through the editor's automation:

```sh
python3 Tools/Tests/regression.py --list             # every case
python3 Tools/Tests/regression.py                    # run all (about 30 minutes)
python3 Tools/Tests/regression.py template:vr vr_rig # run some
```

- `template:<id>`: one case per hub template. The hub creates a fresh project, the editor presses Play for 240 frames.
  Passes with 0 script errors.
- `Tools/Tests/cases/<name>.txt`: automation scripts for specific features (AR plane detection, VR snap turn and
  two-eye view, Feature Test joint chain, `EntityGetBounds` with children, building the game for this PC). The
  `# template: <id>` header names the project the case runs on. Check scripts in `cases/checks/` create an entity
  named `RegressionCheckOK` when their condition holds.

Every case must also leave the editor with exit code 0: a crash while shutting down (for example a GPU resource freed
after the GL context) fails the case. On a headless machine set `CQ_RUNNER` (editor) and `CQ_HUB_RUNNER` (hub) to
wrappers that start them inside a compositor; set `XDG_DATA_HOME` to keep test saves away from your own.
