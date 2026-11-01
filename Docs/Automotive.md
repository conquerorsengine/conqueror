# Android, Android Automotive, AGL and QNX

The game player runs on mobile and in-vehicle systems through **OpenGL ES 3.x**. The engine itself is written against
the OpenGL 4.5 API; on ES (and on older desktop GL) the missing parts are emulated by `GLProfile`
(`Engine/src/Renderer/GraphicsAPIs/OpenGL/GLProfile.cpp`) and the shaders are cross-compiled to GLSL ES
(`#version 300 es` / `310 es` / `320 es`). The editor stays a desktop application; these targets only get the player.

| Target | Window / input layer | Graphics | Status |
|---|---|---|---|
| Android 8.0+ (arm64-v8a, x86_64) | `Platform/Android` (NativeActivity, no Java) | OpenGL ES 3.2 → 3.1 → 3.0 | tested on the Android 15 emulator (host GPU) |
| Android Automotive OS | same APK with the automotive feature | same | tested on the Android 14 Automotive emulator |
| Automotive Grade Linux / Yocto | GLFW on Wayland (no X11) | OpenGL ES 3.2 (`CQ_DEFAULT_GL_PROFILE`) | the same configuration is built and tested on desktop Linux; the Yocto recipes were not run in a real AGL build |
| QNX Neutrino 7.1 / 8.0 | `Platform/QNX` (Screen + EGL) | OpenGL ES 3.x | **not built**: needs the QNX SDP. The QNX layer was only compile-checked against stand-in headers |

## OpenGL profiles

Any build can be forced onto a profile to check that a game works on weaker hardware:

```bash
./ConquerorRuntime MyGame --gl=es3.0      # also: 4.6 4.5 4.1 3.3 es3.2 es3.1 webgl2, or CQ_GL_PROFILE=es3.0
```

What changes on OpenGL ES:

- No multisampled textures on ES 3.0 (the scene is rendered without MSAA; use FXAA/SMAA/TAA).
- Depth textures and 32-bit float textures cannot be filtered linearly. `GLProfile` gives the driver `NEAREST` for them
  automatically, and the screen-space effects read depth with `SampleDepth()` (`Shaders/Include/Common.glsl`), which does
  the bilinear filtering by hand when the shader is compiled for ES (`CQ_GLES` is defined).
- `glDrawBuffers` lists are remapped to the ES rule (entry *i* is attachment *i* or `GL_NONE`).
- Occlusion queries count visible parts of a 4×4 grid (`GL_ANY_SAMPLES_PASSED`) instead of samples.
- No wireframe (`glPolygonMode`), no timer queries unless `GL_EXT_disjoint_timer_query` exists.

All render tests of the engine pass on `es3.0` and on desktop `3.3`.

## Android and Android Automotive

### Requirements

- Android SDK with `build-tools` (aapt2, zipalign, apksigner), `platforms;android-36` and the NDK
  (`ndk;29.0.14206865` was used). The SDK is found through `ANDROID_HOME`, `ANDROID_SDK_ROOT` or `~/Android/Sdk`.
- A JDK (`keytool` and `apksigner` need Java).
- Python 3 and CMake 3.20+ (Ninja is used when installed).

No Gradle and no Java code: the APK contains `libconqueror.so` (the player built with the NDK, `NativeActivity`) and the
game files.

### From the editor

**Build → Build Settings → Platform → Android** (or **Android Automotive**) → **Build**. The output folder gets
`<Product>.apk`; the first build compiles the player for arm64-v8a and x86_64 with the NDK (this takes a while, later
builds reuse it from `build-android/`, or from `CQ_ANDROID_BUILD_ROOT` when that variable is set). **Build And Run**
installs the APK with `adb` and starts it on the connected device or emulator.

### From the command line

```bash
# 1. build the game for Linux / Windows in the editor (or use a project folder)
# 2. package it
python3 Tools/Android/package_apk.py Builds/Linux --out MyGame.apk --build            # phone / tablet
python3 Tools/Android/package_apk.py Builds/Linux --out MyGameCar.apk --build --automotive
python3 Tools/Android/package_apk.py Builds/Linux --out MyGame.apk --install --run    # adb install + start
```

Useful options: `--abi arm64-v8a` (repeatable), `--orientation landscape|portrait|sensorLandscape ...`,
`--package com.company.game`, `--version-code N`, `--release` (not debuggable), `--keystore`, `--ks-pass`, `--key-alias`
(release signing; the default is `~/.android/debug.keystore`, created when missing).

### How it works on the device

- On first start (and whenever the APK content changes) the game files are extracted from the APK to the app's data
  folder; the player then runs exactly like on the desktop.
- `HOME`, `XDG_DATA_HOME` and `TMPDIR` point at the app's private folders, so PlayerPrefs, save files, logs and the
  shader cache work without changes.
- Touch: the first finger is the left mouse button (UI buttons, `IsMouseButtonDown`, ImGui). Hardware keyboards map to
  the normal key codes, the Back button is `Escape`, gamepads use the standard layout (`IsGamepadButtonDown`).
- When the app goes to the background the GL surface is released and the game loop pauses until it comes back.
- Logs go to logcat: `adb logcat -s Conqueror`.

### Android Automotive notes

`--automotive` adds `android.hardware.type.automotive` to the manifest so the APK installs on Android Automotive OS. The
system bars of the car stay visible; the game gets the remaining area. Distraction-optimisation rules of the car maker
decide whether a game may run while driving (games are normally only allowed while parked).

## Automotive Grade Linux (and other Yocto images)

`Tools/AGL/meta-conqueror` is a Yocto layer with two recipes:

- `conqueror-player` builds only the player (`-DCQ_RUNTIME_ONLY=ON`), Wayland-only GLFW (`GLFW_BUILD_X11=OFF`),
  OpenGL ES 3.2 as the default profile (`-DCQ_DEFAULT_GL_PROFILE=es3.2`) and no Vulkan.
- `conqueror-game` installs a game folder built with the editor (Linux target), a launcher `/usr/bin/<app id>` and a
  desktop entry, so the AGL launcher can start it. The window's Wayland `app_id` is the app id (`CQ_APP_ID`), which is what
  the AGL compositor uses to place the application.

```
# bblayers.conf
BBLAYERS += "/path/to/Conqueror_Engine/Tools/AGL/meta-conqueror"
# local.conf
CONQUEROR_GAME_DIR = "/home/me/Builds/MyGame"
CONQUEROR_APP_ID = "mygame"
CONQUEROR_GAME_NAME = "My Game"
IMAGE_INSTALL:append = " conqueror-game"
```

The layer targets Yocto *scarthgap* and newer (GCC 13+ for C++23). The game folder is read-only on the target: logs,
saves and the shader cache are written to the user's data folder.

The same configuration can be built and tried on any Linux desktop with a Wayland session:

```bash
cmake -S . -B build-agl -G Ninja -DCMAKE_BUILD_TYPE=Release -DCQ_RUNTIME_ONLY=ON -DCQ_DEFAULT_GL_PROFILE=es3.2 \
      -DCQ_ENABLE_VULKAN=OFF -DCQ_LINUX_ENGINE_CORE_STATIC=ON -DGLFW_BUILD_X11=OFF -DGLFW_BUILD_WAYLAND=ON
cmake --build build-agl --target ConquerorRuntime
```

## QNX

`Tools/QNX/qnx.toolchain.cmake` builds the player with the QNX SDP (`qcc` / `q++`):

```bash
source ~/qnx800/qnxsdp-env.sh
cmake -S . -B build-qnx -G Ninja -DCMAKE_TOOLCHAIN_FILE=Tools/QNX/qnx.toolchain.cmake -DCQ_QNX_ARCH=aarch64le
cmake --build build-qnx --target ConquerorRuntime
```

- Window, input and the EGL context come from `Engine/src/Platform/QNX/GLFWQnx.cpp` (QNX Screen). The window fills the
  display (set `CQ_QNX_WINDOWED=1` to use the requested size); its Screen id string is `CQ_APP_ID` (default `conqueror`).
- Touch (first finger = left mouse button), keyboard (`sys/keycodes.h`), mouse and Screen gamepads are supported.
- 3D physics is off on QNX (`CQ_ENABLE_JOLT=OFF`): JoltPhysics does not recognise the platform yet. 2D physics (Box2D),
  scripting, UI, audio mixing (without an output device: miniaudio has no QNX backend) and networking (`libsocket`) are
  built.
- Status: this port has **not** been compiled with the real SDP. Expect small fixes on the first build.

## CMake options for these targets

| Option | Default | Meaning |
|---|---|---|
| `CQ_RUNTIME_ONLY` | OFF (ON for Android / QNX) | build only the engine and the player |
| `CQ_DEFAULT_GL_PROFILE` | empty | profile used when none is requested (`es3.2`, `es3.0`, `4.1`, ...) |
| `CQ_ENABLE_JOLT` | ON (OFF on QNX) | build the JoltPhysics 3D physics backend |
| `CQ_ENABLE_VULKAN` | ON (OFF on Android / QNX) | build the Vulkan backend |
