# Parkour — Conqueror's Engine sample

A first-person parkour time trial with 24 levels in six themed worlds, built only with engine features:
ConquerorScript, the Character Controller (Jolt), kinematic bodies, physics ray casts, the PBR renderer
with fog / bloom / SSAO / shadows, particles, the UGUI canvas, the audio mixer and PlayerPrefs.

Open `Parkour.cqproj` in the editor and press **Play**, or build it from **Build → Build Settings**.

## Movement

- Run and sprint with acceleration-based ground movement and air control that keeps momentum
- Variable jump height, coyote time, jump buffering and a double jump
- **Wall-run** along the orange walls (jump towards the wall and keep holding forward),
  then **wall-jump** off it
- **Slide** (with a speed boost) and crouch under low ceilings; you only stand up when there is room
- **Ledge mantle**: grab and climb edges you barely miss
- **Jump pads** and **moving platforms** (the player rides along with them)
- Head bob, landing dip, speed-based field of view, camera roll while wall-running, dust on landings,
  slides and wall steps, footsteps / wind / landing sounds

## Levels

The game starts on a level-select menu: six categories with four levels each.

| Category      | Theme                                   | Focus                         |
|---------------|-----------------------------------------|-------------------------------|
| Skyline       | Golden-hour city, concrete rooftops     | Basics; level 4 is the original course |
| Neon Night    | Night city, glossy panels, neon signs   | Wall-runs                     |
| Desert Ruins  | Sandstone mesas and obelisks            | Long jumps and drops          |
| Arctic Peaks  | Icebergs, falling snow                  | Narrow beams, moving floes    |
| Foundry       | Rusty steel decks, smoking chimneys     | Hurdles, slides, jump pads    |
| Jungle Temple | Mossy stone, giant trees, fireflies     | Everything, hardest courses   |

The first level of every category is open; the others unlock when you finish the previous one.
Each level keeps its own best time, splits and medal, and the finish screen offers **Next level**.
The menu shows your medals per level and in total.

## The original course (Skyline 4)

Three checkpoints split the course into four legs: warm-up jumps, the first wall-run, timed moving platforms,
hurdles and a slide tunnel, a jump pad, a second wall-run, a double-jump gap and the finish gate.
Falling below the course fades to black and puts you back at the last checkpoint (the timer keeps
running). Checkpoints show split times compared with your best run.

Medals: **gold** ≤ 0:26, **silver** ≤ 0:34, **bronze** ≤ 0:48 (editable on the `Game` object).
Your best time, splits, mouse sensitivity and field of view are saved between sessions.
Tutorial hints appear until you finish the course once.

## Controls

| Action                         | Keyboard / mouse        | Gamepad                    |
|--------------------------------|-------------------------|----------------------------|
| Move                           | W A S D / arrow keys    | Left stick                 |
| Look                           | Mouse                   | Right stick                |
| Sprint                         | Shift                   | Push the stick fully / L3  |
| Jump / double jump / wall-jump | Space                   | A                          |
| Slide / crouch                 | Ctrl / C                | B / R3                     |
| Pause                          | Esc / P                 | Start                      |
| Menus                          | Mouse, ↑ ↓ + Enter      | D-pad + A                  |

## Project layout

| Path                               | Contents                                                     |
|------------------------------------|--------------------------------------------------------------|
| `Assets/Scenes/Menu.cqscene`       | Level select (start scene)                                   |
| `Assets/Scenes/Main.cqscene`       | Skyline 4, the original course: level, lighting, player, cameras and UI |
| `Assets/Scenes/<World><N>.cqscene` | The other 23 levels                                          |
| `Assets/Scripts/Player.cqs`        | First-person controller (all movement mechanics, camera, sounds) |
| `Assets/Scripts/ParkourGame.cqs`   | Game flow: title, countdown, timer, checkpoints, respawn, pause, finish, medals |
| `Assets/Scripts/ParkourMenu.cqs`   | Level select: categories, unlocks, best times and medals     |
| `Assets/Scripts/MovingPlatform.cqs`| Kinematic sine-wave platform that publishes its motion to riders |
| `Tools/BuildScene.cqs`             | Script that generated `Main.cqscene` (course layout in metres) |
| `Tools/gen_levels.py`              | Generates the other levels and the menu from course pieces (`Tools/Levels/`) |
| `Tools/gen_assets.py`              | Generator for the textures, sound effects and music          |

Every movement value (speeds, gravity, jump heights, wall-run and slide timings, camera feel)
is a field on the `Player` script and can be tuned in the Inspector while playing.
`ParkourGame` also contains an autopilot that runs the whole course; the automated tests use it
to prove every level can be completed.

To change the levels, edit the designs in `Tools/gen_levels.py` (each level is a short list of pieces:
jumps, wall-runs, moving platforms, the runway with hurdles / slide tunnel / jump pad, beams, double-jump gaps
and checkpoints), run it, then build the scenes in the editor with
`CQ_AUTOMATION=Tools/Levels/build_levels.txt`. Every piece writes its own autopilot route and tutorial hints.

## Credits

- **Fonts:** [Orbitron](https://fonts.google.com/specimen/Orbitron) by The Orbitron Project Authors and
  [Russo One](https://fonts.google.com/specimen/Russo+One) by Jovanny Lemonad, both under the
  SIL Open Font License 1.1 (license texts next to the font files in `Assets/Fonts`).
- **Music, sound effects and textures:** original, generated procedurally by `Tools/gen_assets.py`.
