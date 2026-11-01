# Tetris — Conqueror's Engine sample

A complete falling-block game that follows the modern guideline rules, built only with engine
features: ConquerorScript, the 2D sprite renderer, the UGUI canvas, particles, the audio mixer
and PlayerPrefs.

Open `Tetris.cqproj` in the editor and press **Play**, or build it from **Build → Build Settings**.

## Gameplay

- SRS rotation with the standard wall-kick tables (separate table for the I piece)
- 7-bag randomizer, hold piece, ghost piece, 5-piece preview
- Lock delay (0.5 s) with move/rotate resets (15 max), DAS/ARR auto-repeat
- T-Spin detection (3-corner rule), Back-to-Back bonus, combos, perfect clears
- Level progression with a guideline gravity curve
- Line-clear flash, particle bursts, screen shake, score pop-ups, countdown,
  game-over fill animation and statistics screen
- Best score is saved between sessions (PlayerPrefs key `Tetris.Best`)
- Pause menu with music / sound volume sliders (saved)

## Controls

| Action        | Keyboard                | Gamepad            |
|---------------|-------------------------|--------------------|
| Move          | ← → / A D               | D-pad left / right |
| Soft drop     | ↓ / S                   | D-pad down         |
| Hard drop     | Space                   | D-pad up           |
| Rotate right  | ↑ / X / W               | A                  |
| Rotate left   | Z / Left Ctrl           | B                  |
| Hold          | C / Left Shift          | LB / RB            |
| Pause         | Esc / P                 | Start              |
| Menus         | ↑ ↓ + Enter, mouse      | D-pad + A          |

## Project layout

| Path                           | Contents                                                    |
|--------------------------------|-------------------------------------------------------------|
| `Assets/Scenes/Main.cqscene`   | The game scene (camera, board, UI, effects, game object)    |
| `Assets/Scripts/TetrisGame.cqs`| All game logic (state machine, rules, scoring, UI, audio)   |
| `Assets/Textures`, `Audio`, `Music`, `Fonts` | Game assets                                   |
| `Tools/BuildScene.cqs`         | Script that generated `Main.cqscene`                        |
| `Tools/gen_assets.py`          | Generator for the textures, sound effects and music         |

The script exposes tuning fields in the Inspector (DAS, ARR, soft-drop factor, lock delay,
starting level, ghost on/off).

## Credits

- **Fonts:** [Orbitron](https://fonts.google.com/specimen/Orbitron) by The Orbitron Project Authors and
  [Russo One](https://fonts.google.com/specimen/Russo+One) by Jovanny Lemonad, both under the
  SIL Open Font License 1.1 (license texts next to the font files in `Assets/Fonts`).
- **Music:** original arrangement of *Korobeiniki*, a traditional Russian folk song in the public
  domain, synthesized by `Tools/gen_assets.py`.
- **Textures and sound effects:** generated procedurally by `Tools/gen_assets.py`.

Tetris is a trademark of The Tetris Company. This sample is an educational demonstration of the
engine and is not affiliated with or endorsed by The Tetris Company.
