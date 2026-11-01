#!/usr/bin/env python3
"""Ember Trail: builds the six levels and the menu.

usage: gen_levels.py

1. Assembles every level from Tools/ember_levels.py and checks it with the solver (Tools/ember_solve.py):
   a level that cannot be finished stops the script.
2. Writes Tools/Levels/<Scene>.cqs (scene builders) + Tools/Levels/Menu.cqs + Tools/Levels/build_levels.txt.
   The recorded solution goes into the scene (EmberGame.demo) for the autopilot and the tests.
3. Build the scenes in the editor:  CQ_AUTOMATION=Tools/Levels/build_levels.txt ConquerorEngine Samples/EmberTrail
"""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import ember_levels as L      # noqa: E402
import ember_sim as sim       # noqa: E402
import ember_solve as solve   # noqa: E402

ROOT = os.path.dirname(HERE)
OUT = os.path.join(HERE, "Levels")
CACHE = os.path.join(HERE, "solutions.json")
CODES = {"#": "1", "X": "2", "=": "3", "^": "4"}


def f(v):
    s = f"{v:.4f}".rstrip("0").rstrip(".")
    return "0" if s in ("-0", "") else s


COMMON = r'''using nativebridge;

var FONT = "Assets/Fonts/RussoOne.ttf";
var TITLEFONT = "Assets/Fonts/Orbitron.ttf";

func Sprite(name, tex, x, y, z, rect, pivotY)
{
    var e = CreateEntity(name);
    EntityAddComponent(e, "SpriteRenderer");
    EntitySetComponentField(e, "SpriteRenderer", "Texture", tex);
    EntitySetComponentField(e, "SpriteRenderer", "PixelsPerUnit", 16);
    if (rect != null) { EntitySetComponentField(e, "SpriteRenderer", "Rect", rect); }
    EntitySetComponentField(e, "SpriteRenderer", "Pivot", {x: 0.5, y: pivotY});
    EntitySetPosition(e, x, y, z);
    return e;
}

func Frame(k, w, h) { return {x: k * w, y: 0, z: w, w: h}; }

func Particles(name, props)
{
    var fx = CreateEntity(name);
    EntityAddComponent(fx, "ParticleSystem");
    foreach (var k, v in props) { EntitySetComponentField(fx, "ParticleSystem", k, v); }
    return fx;
}

func Txt(name, parent, text, x, y, w, h, size, color, align, font)
{
    var t = UICreateElement("Text", parent, name);
    UISetText(t, text);
    UISetPosition(t, x, y);
    UISetSize(t, w, h);
    EntitySetComponentField(t, "UIText", "Font", font);
    EntitySetComponentField(t, "UIText", "FontSize", size);
    EntitySetComponentField(t, "UIText", "AlignH", align);
    EntitySetComponentField(t, "UIText", "AlignV", "Middle");
    EntitySetComponentField(t, "UIText", "WordWrap", false);
    EntitySetComponentField(t, "UIText", "Shadow", true);
    EntitySetComponentField(t, "UIText", "ShadowColor", {r: 0.1, g: 0.05, b: 0.15, a: 0.85});
    EntitySetComponentField(t, "UIText", "ShadowOffset", {x: 3, y: -3});
    UISetTextColor(t, color);
    return t;
}

func Icon(name, parent, tex, x, y, size)
{
    var i = UICreateElement("Image", parent, name);
    UISetImage(i, tex);
    UISetPosition(i, x, y);
    UISetSize(i, size, size * 1.0);
    EntitySetComponentField(i, "UIImage", "RaycastTarget", false);
    return i;
}

func Layer(name, parent, alpha)
{
    var p = UICreateElement("Panel", parent, name);
    EntitySetComponentField(p, "RectTransform", "AnchorMin", {x: 0, y: 0});
    EntitySetComponentField(p, "RectTransform", "AnchorMax", {x: 1, y: 1});
    EntitySetComponentField(p, "RectTransform", "SizeDelta", {x: 0, y: 0});
    EntitySetComponentField(p, "RectTransform", "AnchoredPosition", {x: 0, y: 0});
    EntitySetComponentField(p, "UIImage", "CornerRadius", 0);
    EntitySetColor(p, 0.06, 0.03, 0.10, alpha);
    EntitySetComponentField(p, "UIImage", "RaycastTarget", alpha > 0.01);
    return p;
}

func Btn(name, parent, label, x, y, w)
{
    var b = UICreateElement("Button", parent, name);
    UISetPosition(b, x, y);
    UISetSize(b, w, 70);
    EntitySetColor(b, 0.16, 0.12, 0.22, 0.92);
    EntitySetComponentField(b, "UIImage", "CornerRadius", 18);
    UISetText(b, label);
    var t = EntityGetChildren(b)[0];
    EntitySetComponentField(t, "UIText", "Font", FONT);
    EntitySetComponentField(t, "UIText", "FontSize", 30);
    UISetTextColor(t, 1.0, 0.96, 0.88, 1);
    return b;
}

func Anchor(t, ax, ay)
{
    EntitySetComponentField(t, "RectTransform", "AnchorMin", {x: ax, y: ay});
    EntitySetComponentField(t, "RectTransform", "AnchorMax", {x: ax, y: ay});
    return t;
}

func Fade(canvas)
{
    var fade = UICreateElement("Image", canvas, "Fade");
    EntitySetComponentField(fade, "RectTransform", "AnchorMin", {x: 0, y: 0});
    EntitySetComponentField(fade, "RectTransform", "AnchorMax", {x: 1, y: 1});
    EntitySetComponentField(fade, "RectTransform", "SizeDelta", {x: 0, y: 0});
    EntitySetComponentField(fade, "UIImage", "CornerRadius", 0);
    EntitySetColor(fade, 0, 0, 0, 1);
    EntitySetComponentField(fade, "UIImage", "RaycastTarget", false);
    return fade;
}

func Camera2D(name, x, y, bg)
{
    var cam = CreateEntity(name);
    EntityAddComponent(cam, "Camera");
    EntitySetComponentField(cam, "Camera", "Projection", "Orthographic");
    EntitySetComponentField(cam, "Camera", "OrthographicSize", 15);   // görünen yükseklik (karo)
    RenderSettingSet("Environment.BackgroundColor", {r: bg.r, g: bg.g, b: bg.b});
    EntitySetComponentField(cam, "Camera", "Primary", true);
    EntitySetComponentField(cam, "Camera", "Background", 2);
    EntitySetComponentField(cam, "Camera", "BackgroundColor", bg);
    EntitySetPosition(cam, x, y, 10);
    return cam;
}

func Backdrop(far, near, w, tint)
{
    var bf = Sprite("BgFar", far, w / 2.0, 6, -2, null, 0.5);
    EntitySetComponentField(bf, "SpriteRenderer", "DrawMode", "Tiled");
    EntitySetComponentField(bf, "SpriteRenderer", "Size", {x: w + 80, y: 10});
    EntitySetComponentField(bf, "SpriteRenderer", "Color", tint);
    var bn = Sprite("BgNear", near, w / 2.0, 5, -1, null, 0.5);
    EntitySetComponentField(bn, "SpriteRenderer", "DrawMode", "Tiled");
    EntitySetComponentField(bn, "SpriteRenderer", "Size", {x: w + 80, y: 10});
}

RenderSettingSet("Environment.Sky", "SolidColor");
RenderSettingSet("Post.Bloom", true);
RenderSettingSet("Post.BloomThreshold", 1.1);
RenderSettingSet("Post.BloomIntensity", 0.35);
RenderSettingSet("Post.AmbientOcclusion", false);
RenderSettingSet("Post.Tonemap", "None");
RenderSettingSet("Post.Vignette", true);
RenderSettingSet("Post.VignetteIntensity", 0.18);
'''

FX = r'''
Particles("FX_Spark", {PlayOnAwake: false, Looping: false, EmissionEnabled: false, RateOverTime: 0,
    StartLifetime: {x: 0.35, y: 0.8}, StartSpeed: {x: 2, y: 6}, StartSize: {x: 0.08, y: 0.18},
    StartColorA: {r: 1, g: 0.85, b: 0.35, a: 1}, StartColorB: {r: 1, g: 0.5, b: 0.2, a: 1},
    Shape: "Sphere", Radius: 0.2, GravityModifier: 0.6, SimulationSpace: "World", Drag: 1.5,
    Blend: "Additive", Texture: "Assets/Textures/glow.png", EmissiveIntensity: 2, MaxParticles: 600, ColorOverLifetimeEnabled: true});
Particles("FX_Dust", {PlayOnAwake: false, Looping: false, EmissionEnabled: false, RateOverTime: 0,
    StartLifetime: {x: 0.25, y: 0.5}, StartSpeed: {x: 0.6, y: 1.8}, StartSize: {x: 0.15, y: 0.3},
    StartColorA: {r: 1, g: 1, b: 1, a: 0.7}, StartColorB: {r: 0.9, g: 0.85, b: 0.8, a: 0.5},
    Shape: "Circle", Radius: 0.2, GravityModifier: -0.05, Drag: 4, SimulationSpace: "World",
    Blend: "Alpha", Texture: "Assets/Textures/glow.png", MaxParticles: 200, ColorOverLifetimeEnabled: true});
Particles("FX_Dash", {PlayOnAwake: false, Looping: false, EmissionEnabled: false, RateOverTime: 0,
    StartLifetime: {x: 0.2, y: 0.45}, StartSpeed: {x: 0.5, y: 2.5}, StartSize: {x: 0.2, y: 0.4},
    StartColorA: {r: 1, g: 0.6, b: 0.2, a: 0.9}, StartColorB: {r: 1, g: 0.9, b: 0.4, a: 0.8},
    Shape: "Sphere", Radius: 0.3, GravityModifier: 0, Drag: 3, SimulationSpace: "World",
    Blend: "Additive", Texture: "Assets/Textures/glow.png", EmissiveIntensity: 2, MaxParticles: 200, ColorOverLifetimeEnabled: true});
'''

UI = r'''
var canvas = UICreateCanvas("HUD");
EntitySetComponentField(canvas, "CanvasScaler", "ReferenceResolution", {x: 1920, y: 1080});
EntitySetComponentField(canvas, "CanvasScaler", "MatchWidthOrHeight", 0.5);
var cream = {r: 1.0, g: 0.96, b: 0.86, a: 1};
var amber = {r: 1.0, g: 0.74, b: 0.3, a: 1};
var dim = {r: 0.85, g: 0.78, b: 0.92, a: 1};

var hud = Layer("HudRoot", canvas, 0);
Anchor(Icon("EmberIcon", hud, "Assets/Textures/icon_ember.png", 70, -60, 64), 0, 1);
Anchor(Txt("EmberText", hud, "0", 210, -60, 180, 64, 52, cream, "Left", FONT), 0, 1);
for (var k = 0; k < 3; k++) { Anchor(Icon("ShardIcon" + k, hud, "Assets/Textures/icon_shard.png", 350 + k * 70, -60, 60), 0, 1); }
Anchor(Txt("TimeText", hud, "0:00.00", -180, -60, 320, 64, 48, cream, "Right", FONT), 1, 1);
Anchor(Icon("DeathIcon", hud, "Assets/Textures/icon_death.png", -70, -140, 52), 1, 1);
Anchor(Txt("DeathText", hud, "0", -190, -140, 160, 50, 38, dim, "Right", FONT), 1, 1);

var card = Layer("CardUI", canvas, 0.55);
Txt("CardWorld", card, "WORLD", 0, 140, 1400, 70, 46, amber, "Center", FONT);
Txt("CardName", card, "LEVEL", 0, 40, 1600, 130, 104, cream, "Center", TITLEFONT);
Txt("CardHint", card, "ARROWS / WASD  MOVE     SPACE  JUMP     X / SHIFT  DASH", 0, -110, 1600, 50, 28, dim, "Center", FONT);

var pause = Layer("PauseUI", canvas, 0.7);
Txt("PauseTitle", pause, "PAUSED", 0, 250, 900, 120, 96, cream, "Center", TITLEFONT);
Btn("ResumeBtn", pause, "RESUME", 0, 80, 420);
Btn("RestartBtn", pause, "RESTART LEVEL", 0, -10, 420);
Btn("QuitBtn", pause, "LEVEL SELECT", 0, -100, 420);

var res = Layer("ResultUI", canvas, 0.72);
Txt("ResultTitle", res, "LANTERN LIT!", 0, 330, 1400, 120, 96, amber, "Center", TITLEFONT);
Txt("ResultMedal", res, "", 0, 225, 900, 70, 52, cream, "Center", FONT);
Txt("ResultTimeL", res, "TIME", -200, 130, 360, 54, 36, dim, "Right", FONT);
Txt("ResultTime", res, "", 210, 130, 360, 54, 44, cream, "Left", FONT);
Txt("ResultEmbersL", res, "EMBERS", -200, 70, 360, 54, 36, dim, "Right", FONT);
Txt("ResultEmbers", res, "", 210, 70, 360, 54, 44, cream, "Left", FONT);
Txt("ResultShardsL", res, "SUN SHARDS", -200, 10, 360, 54, 36, dim, "Right", FONT);
Txt("ResultShards", res, "", 210, 10, 360, 54, 44, cream, "Left", FONT);
Txt("ResultDeathsL", res, "FALLS", -200, -50, 360, 54, 36, dim, "Right", FONT);
Txt("ResultDeaths", res, "", 210, -50, 360, 54, 44, cream, "Left", FONT);
Txt("ResultBest", res, "", 0, -115, 1200, 50, 32, amber, "Center", FONT);
Btn("NextBtn", res, "NEXT LEVEL", 0, -200, 420);
Btn("RetryBtn", res, "PLAY AGAIN", 0, -285, 420);
Btn("MenuBtn", res, "LEVEL SELECT", 0, -370, 420);
Fade(canvas);
'''


def level_builder(scene, title, world, lv, demo, demo_time, next_scene, index):
    wd = L.WORLDS[world]
    out = [f"// Generated by Tools/gen_levels.py - {title}. Edit the generator, not this file.\n", COMMON]
    bg = wd["sky"]
    out.append(f'var cam = Camera2D("MainCamera", 13, 7.5, {{r: {bg[0]}, g: {bg[1]}, b: {bg[2]}, a: 1}});\n')
    tint = "{r: 0.75, g: 0.75, b: 0.85, a: 1}" if world != "cave" else "{r: 0.6, g: 0.6, b: 0.75, a: 1}"
    out.append(f'Backdrop("Assets/Textures/{wd["far"]}", "Assets/Textures/{wd["near"]}", {lv.w}, {tint});\n')
    # Arazi: karo haritası (16'lık otomatik döşeme)
    out.append('var terrain = CreateEntity("Terrain");\nEntityAddComponent(terrain, "Grid");\nEntityAddComponent(terrain, "Tilemap");\n'
               'EntitySetPosition(terrain, 0.5, 0.5, 0);\nEntitySetComponentField(terrain, "Tilemap", "Outline", false);\n'
               'var ground = TilemapAddTileType(terrain, "Ground", 1, 1, 1, 1);\n'
               f'TilemapSetTileType(terrain, ground, {{Texture: "Assets/Textures/{wd["tiles"]}", Autotile: 1, Solid: true}});\n')
    cells = []
    for j in range(lv.h):
        for i in range(lv.w):
            if lv.tile(i, j) == "#":
                cells.append(f"{i},{j}")
    out.append('foreach (var c in StringSplit("' + ";".join(cells) + '", ";")) { var p = StringSplit(c, ","); '
               'TilemapSetTile(terrain, ToInt(p[0]), ToInt(p[1]), ground); }\n')
    items = "Assets/Textures/items.png"
    deco = []
    for j in range(lv.h):
        for i in range(lv.w):
            t = lv.tile(i, j)
            if t == "=":
                deco.append(f'Sprite("Plank", "{items}", {i + 0.5}, {j + 0.5}, 0.05, Frame(10, 16, 16), 0.5);')
            elif t == "^":
                deco.append(f'Sprite("Spikes", "{items}", {i + 0.5}, {j + 0.5}, 0.05, Frame(9, 16, 16), 0.5);')
    out.append("\n".join(deco) + "\n")
    for k, (i, j) in enumerate(lv.crumbles):
        out.append(f'Sprite("Crumble{k}", "{items}", {i + 0.5}, {j + 0.5}, 0.05, Frame(11, 16, 16), 0.5);\n')
    for k, (i, j) in enumerate(lv.springs):
        out.append(f'Sprite("Spring{k}", "{items}", {i + 0.5}, {j}, 0.06, Frame(5, 16, 16), 0);\n')
    for k, (i, j, kind) in enumerate(lv.plats):
        out.append(f'var pl{k} = Sprite("Plat{k}", "Assets/Textures/platform.png", {i + 1.5}, {j + 1.0}, 0.1, null, 0.5);\nEntitySetTag(pl{k}, "{kind}");\n')
    for k, (i, j) in enumerate(lv.checkpoints):
        out.append(f'var cpe{k} = Sprite("Checkpoint{k}", "{items}", {i + 0.5}, {j}, 0.04, Frame(7, 16, 16), 0);\nEntitySetScale(cpe{k}, 1.5, 1.5, 1);\n')
    gx, gy = lv.goal
    out.append(f'Sprite("Goal", "Assets/Textures/goal.png", {gx + 1.0}, {gy}, 0.04, Frame(0, 32, 48), 0);\n')
    k = 0
    for (x, y, kind) in lv.slugs:
        out.append(f'var en{k} = Sprite("Enemy{k}", "Assets/Textures/enemies.png", {x}, {y}, 0.15, Frame({0 if kind == "S" else 4}, 16, 16), 0);\nEntitySetTag(en{k}, "{kind}");\n')
        k += 1
    for (x, y) in lv.moths:
        out.append(f'var en{k} = Sprite("Enemy{k}", "Assets/Textures/enemies.png", {x}, {y}, 0.15, Frame(2, 16, 16), 0);\nEntitySetTag(en{k}, "M");\n')
        k += 1
    for n, (i, j) in enumerate(lv.embers):
        out.append(f'Sprite("Ember{n}", "{items}", {i + 0.5}, {j + 0.5}, 0.08, Frame(0, 16, 16), 0.5);\n')
    for n, (i, j) in enumerate(lv.shards):
        out.append(f'var sh{n} = Sprite("Shard{n}", "{items}", {i + 0.5}, {j + 0.5}, 0.08, Frame(4, 16, 16), 0.5);\nEntitySetScale(sh{n}, 1.25, 1.25, 1);\n')
    sx, sy = lv.spawn
    out.append(f'var spawn = CreateEntity("Spawn");\nEntitySetPosition(spawn, {sx}, {int(round(sy - sim.HH))}, 0);\n')
    out.append(f'var player = Sprite("Player", "Assets/Textures/wick.png", {sx}, {sy - sim.HH}, 0.2, Frame(0, 16, 20), 0);\n')
    if wd["dark"]:
        out.append('var gl = CreateEntity("GlobalLight");\nEntityAddComponent(gl, "Light2D");\n'
                   'EntitySetComponentField(gl, "Light2D", "Type", "Global");\nEntitySetComponentField(gl, "Light2D", "Intensity", 0.32);\n'
                   'EntitySetComponentField(gl, "Light2D", "Color", {r: 0.55, g: 0.6, b: 1.0});\n'
                   'EntityAddComponent(player, "Light2D");\nEntitySetComponentField(player, "Light2D", "Type", "Point");\n'
                   'EntitySetComponentField(player, "Light2D", "OuterRadius", 7);\nEntitySetComponentField(player, "Light2D", "InnerRadius", 1.5);\n'
                   'EntitySetComponentField(player, "Light2D", "Intensity", 1.25);\nEntitySetComponentField(player, "Light2D", "Color", {r: 1.0, g: 0.78, b: 0.5});\n')
        # Kontrol noktaları ve hedef de ışık verir
        for k2 in range(len(lv.checkpoints)):
            out.append(f'EntityAddComponent(cpe{k2}, "Light2D");\nEntitySetComponentField(cpe{k2}, "Light2D", "Type", "Point");\n'
                       f'EntitySetComponentField(cpe{k2}, "Light2D", "OuterRadius", 4);\nEntitySetComponentField(cpe{k2}, "Light2D", "Color", {{r: 0.5, g: 0.9, b: 1.0}});\n')
    out.append(FX)
    out.append('var game = CreateEntity("Game");\nEntityAddComponent(game, "AudioSource");\n'
               f'EntitySetComponentField(game, "AudioSource", "Clip", "Assets/Music/{wd["music"]}");\n'
               'EntitySetComponentField(game, "AudioSource", "Loop", true);\nEntitySetComponentField(game, "AudioSource", "PlayOnAwake", true);\n'
               'EntitySetComponentField(game, "AudioSource", "Is3D", false);\nEntitySetComponentField(game, "AudioSource", "Volume", 0.55);\n'
               'EntitySetComponentField(game, "AudioSource", "OutputGroup", "Music");\nAddScript(game, "Assets/Scripts/EmberGame.cqs");\n')
    rows = []
    for j in range(lv.h - 1, -1, -1):
        rows.append("".join(CODES.get(lv.tile(i, j), "0") for i in range(lv.w)))
    fields = dict(levelId=scene.lower(), levelName=title, worldName=wd["title"] + f"  {index // 2 + 1}-{index % 2 + 1}",
                  nextLevel=next_scene, mapData="/".join(rows), demo=demo)
    for key, v in fields.items():
        out.append(f'SetScriptField(game, "{key}", "{v}");\n')
    out.append(f'SetScriptField(game, "demoTime", {f(demo_time)});\n')
    out.append(UI)
    out.append('UISetVisible("PauseUI", false);\nUISetVisible("ResultUI", false);\nUISetVisible("HudRoot", false);\nCreateEntity("BuildSceneDone");\n')
    return "".join(out)


def menu_builder(levels):
    data = ";".join(f"{scene}|{scene.lower()}|{title}|{L.WORLDS[world]['title']}" for (scene, title, world, _) in levels)
    meadow = L.WORLDS["meadow"]
    out = ["// Generated by Tools/gen_levels.py - title + level select. Edit the generator, not this file.\n", COMMON]
    bg = meadow["sky"]
    out.append(f'var cam = Camera2D("MainCamera", 0, 4, {{r: {bg[0]}, g: {bg[1]}, b: {bg[2]}, a: 1}});\n')
    out.append('Backdrop("Assets/Textures/bg_meadow_far.png", "Assets/Textures/bg_meadow_near.png", 120, {r: 0.75, g: 0.75, b: 0.85, a: 1});\n'
               'EntitySetPosition("BgFar", 0, 5.2, -2);\nEntitySetPosition("BgNear", 0, 4.2, -1);\n')
    out.append('var terrain = CreateEntity("Terrain");\nEntityAddComponent(terrain, "Grid");\nEntityAddComponent(terrain, "Tilemap");\n'
               'EntitySetPosition(terrain, 0.5, 0.5, 0);\nEntitySetComponentField(terrain, "Tilemap", "Outline", false);\n'
               'var ground = TilemapAddTileType(terrain, "Ground", 1, 1, 1, 1);\n'
               'TilemapSetTileType(terrain, ground, {Texture: "Assets/Textures/tiles_meadow.png", Autotile: 1, Solid: true});\n'
               'for (var i = -20; i < 20; i++) { for (var j = -4; j < 0; j++) { TilemapSetTile(terrain, i, j, ground); } }\n'
               'var wick = Sprite("MenuWick", "Assets/Textures/wick.png", -10, 0, 0.2, Frame(2, 16, 20), 0);\nEntitySetScale(wick, 2, 2, 1);\n')
    out.append('var menu = CreateEntity("Menu");\nEntityAddComponent(menu, "AudioSource");\n'
               'EntitySetComponentField(menu, "AudioSource", "Clip", "Assets/Music/title.mp3");\n'
               'EntitySetComponentField(menu, "AudioSource", "Loop", true);\nEntitySetComponentField(menu, "AudioSource", "PlayOnAwake", true);\n'
               'EntitySetComponentField(menu, "AudioSource", "Is3D", false);\nEntitySetComponentField(menu, "AudioSource", "Volume", 0.55);\n'
               'EntitySetComponentField(menu, "AudioSource", "OutputGroup", "Music");\nAddScript(menu, "Assets/Scripts/EmberMenu.cqs");\n'
               f'SetScriptField(menu, "levelData", "{data}");\n')
    out.append(r'''
var canvas = UICreateCanvas("MenuCanvas");
EntitySetComponentField(canvas, "CanvasScaler", "ReferenceResolution", {x: 1920, y: 1080});
EntitySetComponentField(canvas, "CanvasScaler", "MatchWidthOrHeight", 0.5);
var cream = {r: 1.0, g: 0.96, b: 0.86, a: 1};
var amber = {r: 1.0, g: 0.74, b: 0.3, a: 1};
var dim = {r: 0.85, g: 0.78, b: 0.92, a: 1};
var root = Layer("MenuRoot", canvas, 0.0);
Anchor(Txt("MenuTitle", root, "EMBER TRAIL", 0, -150, 1700, 190, 150, amber, "Center", TITLEFONT), 0.5, 1);
Anchor(Txt("MenuSub", root, "LIGHT THE LANTERNS, WICK!", 0, -255, 1400, 60, 38, cream, "Center", FONT), 0.5, 1);
for (var w = 0; w < 3; w++)
{
    Txt("WorldLabel" + w, root, "", -650, 170 - w * 150, 520, 60, 34, amber, "Right", FONT);
    for (var l = 0; l < 2; l++)
    {
        var b = Btn("LvlBtn" + (w * 2 + l), root, "", -110 + l * 560, 170 - w * 150, 520);
        UISetSize(b, 520, 110);
        EntitySetComponentField(EntityGetChildren(b)[0], "UIText", "AlignV", "Top");
        Txt("LvlInfo" + (w * 2 + l), root, "", -110 + l * 560, 140 - w * 150, 500, 40, 26, dim, "Center", FONT);
    }
}
Anchor(Txt("MenuTotals", root, "", 0, 120, 1600, 50, 32, cream, "Center", FONT), 0.5, 0);
Anchor(Txt("MenuHint", root, "ARROWS + ENTER  OR  MOUSE        ESC  QUIT", 0, 60, 1600, 40, 24, dim, "Center", FONT), 0.5, 0);
Fade(canvas);
CreateEntity("BuildSceneDone");
''')
    return "".join(out)


def main():
    os.makedirs(OUT, exist_ok=True)
    cache = {}
    if os.path.exists(CACHE):
        with open(CACHE) as fp:
            cache = json.load(fp)
    levels = L.LEVELS
    auto = ["# Generated by Tools/gen_levels.py: builds every Ember Trail scene (CQ_AUTOMATION)", "wait 30"]
    summary = {}
    for n, (scene, title, world, design) in enumerate(levels):
        b = L.build(design, n)
        L.place_shards(b, b.rows())
        rows = b.rows()
        lv = sim.Level(rows)
        key = "\n".join(rows)
        if cache.get(scene, {}).get("map") == key and "-f" not in sys.argv:
            demo = cache[scene]["demo"]
        else:
            print(f"solving {scene} ({lv.w}x{lv.h}) ...", flush=True)
            sol = solve.solve(lv)
            if not sol:
                sys.exit(f"{scene}: no solution found - change the design")
            demo = solve.encode(sol)
            cache[scene] = {"map": key, "demo": demo}
        p, ticks, ev = solve.replay(lv, solve.decode(demo))
        assert p.won, scene
        summary[scene] = dict(ticks=ticks, x=p.x, y=p.y, time=ticks / 60.0, dashes=ev.count("dash"), stomps=ev.count("stomp"),
                              embers=len(lv.embers), shards=len(lv.shards))
        nxt = levels[n + 1][0] if n + 1 < len(levels) else ""
        path = os.path.join(OUT, scene + ".cqs")
        with open(path, "w", encoding="utf-8") as fp:
            fp.write(level_builder(scene, title, world, lv, demo, ticks / 60.0, nxt, n))
        with open(os.path.join(OUT, scene + ".txt"), "w") as fp:
            fp.write("\n".join(rows) + "\n")
        auto += ["new_scene", f'run_cqs_file "{path}"', "wait 3", "entity expect_active BuildSceneDone true", "entity delete BuildSceneDone",
                 f'save_scene_as "{os.path.join(ROOT, "Assets", "Scenes", scene + ".cqscene")}"']
        print(f"{scene:8s} {title:16s} {lv.w:3d} cols  solution {ticks / 60.0:5.2f}s  embers {len(lv.embers)}  shards {len(lv.shards)}")
    path = os.path.join(OUT, "Menu.cqs")
    with open(path, "w", encoding="utf-8") as fp:
        fp.write(menu_builder(levels))
    auto += ["new_scene", f'run_cqs_file "{path}"', "wait 3", "entity expect_active BuildSceneDone true", "entity delete BuildSceneDone",
             f'save_scene_as "{os.path.join(ROOT, "Assets", "Scenes", "Menu.cqscene")}"', "quit"]
    with open(os.path.join(OUT, "build_levels.txt"), "w") as fp:
        fp.write("\n".join(auto) + "\n")
    with open(CACHE, "w") as fp:
        json.dump(cache, fp, indent=0)
    # Testler için beklenen sonuçlar (otopilot aynı karede, aynı konumda bitirmeli)
    with open(os.path.join(OUT, "expected.json"), "w") as fp:
        json.dump(summary, fp, indent=1)


if __name__ == "__main__":
    main()
