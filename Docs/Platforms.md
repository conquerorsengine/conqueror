# Platforms and building games

## Building a game

**Build > Build Settings** (Ctrl+Shift+B):

1. **Scenes in Build**: the scenes the game contains; the first one is loaded at start. **Add Open Scenes** adds the
   current scene.
2. **Target Platform**: where the game will run.
3. **Player settings** (also in Project Settings > Player): product name, company, version, window size and mode,
   resizable, VSync, Run In Background, Graphics API, icon. Settings for one platform are under **Project Settings >
   Player > Platform Settings** (see below).
4. **Build** writes the game to the output folder: the player executable (named after the product), the engine
   library, the project's assets and scenes, and the resources the player needs. **Build And Run** starts it afterwards.

The output folder must be empty or a previous build of the same project.

**Build All Ready Platforms** builds every platform whose module is ready on this computer, one after another, into
`Builds/<Platform>` (or `<output folder>/<Platform>`), and lists the result of each.

## Platform modules

Each platform needs its own toolchain on the computer that builds it (Unity Hub calls these modules). The hub's
**Settings > Platform Modules** table and the Build Settings window show for every platform whether it is ready, what
was found and what to install:

| Platform | Module | Built from |
|----------|--------|------------|
| Linux | this PC | Linux |
| Windows | this PC, or the MinGW-w64 cross compiler on Linux (`x86_64-w64-mingw32-g++`) | Windows, Linux |
| macOS | Xcode command line tools | a Mac |
| FreeBSD | this PC, or clang + a FreeBSD 14 sysroot (`CQ_FREEBSD_SYSROOT`) on Linux | FreeBSD, Linux |
| Android, Android Automotive | Android SDK (build-tools, a platform), NDK, JDK | any desktop |
| iOS, Web | not available yet | — |
| QNX | QNX SDP (`QNX_HOST`, `QNX_TARGET`); built from the command line | Linux, Windows |

Cross builds (Windows and FreeBSD from Linux) compile the player from the engine source the first time (several
minutes) into `build-windows` / `build-freebsd` next to the engine source (`CQ_WINDOWS_BUILD_ROOT` /
`CQ_FREEBSD_BUILD_ROOT` move them); later builds reuse it and only recompile what changed. A Windows game built on
Linux is started with Wine by **Build And Run** when Wine is installed.

## Targets

| Target | Built from | Status |
|--------|-----------|--------|
| Linux x86-64 | Linux | primary platform; full automated test suite; also runs inside the Steam Linux Runtime (Steam Deck) |
| Windows x64 | Windows (MSVC / MinGW) or Linux (MinGW cross toolchain, `Tools/Windows`; also from the editor's Build Settings) | cross build tested under Wine on OpenGL, Vulkan, Direct3D 11 and Direct3D 12 |
| macOS | macOS | Metal and Vulkan/MoltenVK backends written; not yet compiled on a Mac (CI) |
| Android (phone / tablet) | Linux with the Android NDK | APK built and signed without Gradle (`Tools/Android/package_apk.py`); tested on the emulator (GL ES 3) |
| Android Automotive | same | tested on the AAOS emulator ([Automotive](Automotive.md)) |
| Automotive Grade Linux | Yocto layer `Tools/AGL/meta-conqueror` | Wayland-only player build tested |
| FreeBSD | Linux (cross, `Tools/BSD`) or FreeBSD | cross build links; runtime not yet tested on BSD |
| QNX | QNX SDP (`Tools/QNX`) | compile-checked against stub headers only |
| iOS | — | not available yet: the iOS player is not part of this version (it needs a Mac with Xcode) |
| Web (WebGL 2 / WebGPU) | — | not available yet: the web player needs Emscripten and is not part of this version |
| Steam Deck | Linux build | 1280×800 borderless, Deck button glyphs, on-screen keyboard |

## Splash screen and icons

**Project Settings > Player > Splash Image**: when a built game starts, the logos fade in and out over a background
colour while the first scene loads behind them. *Splash Logo* (a project `.png`) is shown first, then the engine logo
(*Show Engine Logo*); *Duration Per Logo* sets the time of each. Automated runs (`CQ_AUTOMATION`) skip it unless
`CQ_SPLASH=1`.

The window and taskbar icon is the project's *Default Icon*, or the engine icon when it is empty. Windows executables
carry the engine icon as a resource, Linux and FreeBSD builds include `<Product>.png` next to the game (the window's
application id is the product name, so a desktop entry with that name shows the icon), and APKs get it as the launcher
icon. The engine logo and icons are vector based: `Resources/Logo/*.svg` (made by `Tools/Branding/make_logo_svg.py`)
and `Resources/Logo/AppIcon` (`Tools/Branding/make_icons.py`).

## Platform settings

**Project Settings > Player > Platform Settings** has one tab per platform (no consoles). The Build Settings window
uses the same platform list; platforms it cannot build from this PC are greyed out and their tab says how they are
built. The values are saved in the project file.

| Tab | Settings | Used by |
|-----|----------|---------|
| Windows | uses the shared identity, icon, window and Graphics API settings | the Windows player |
| macOS | bundle identifier, build number, minimum macOS, category, camera / microphone usage texts | `Info.plist` written into the build folder |
| Linux, FreeBSD | display server: automatic, Wayland or X11 | the player at start (if the chosen one is missing it falls back to automatic); the `CQ_DISPLAY_SERVER` environment variable (`wayland` / `x11`) overrides it |
| Android | package name, version code, orientation, target architectures (ARM64, x86-64), keystore and key alias | the APK packager (`Tools/Android/package_apk.py`); minimum API 26, target API 36 (Automotive 34) |
| iOS | bundle identifier, build number, devices, minimum iOS, orientation, team ID, camera usage, ARKit, status bar | saved for the iOS player (not available yet) |
| Web | WebGL 2 or WebGPU, page template, compression, initial memory, multithreading, data caching | saved for the web player (not available yet) |
| QNX | SDP version, architecture, fill display, Screen app id | architecture: `-DCQ_QNX_ARCH` of the toolchain (the tab shows the command); fill display and app id: the player at start (`CQ_QNX_WINDOWED`, `CQ_APP_ID` override them) |

The package / bundle identifier defaults to `com.<company>.<product>` (lower case; `conqueror` when the company is
empty). The Android keystore password is never saved in the project: set `CQ_ANDROID_KEYSTORE_PASS` before building
with a release key (empty keystore = the debug key in `~/.android/debug.keystore`).

## Platform features the engine adapts to

- **Input glyphs**: UI text can show the button of the device the player is using (`<glyph=a|key_space>` in rich text;
  Xbox, PlayStation, Nintendo, Steam Deck and keyboard sets). Console variable `input.GlyphStyle` forces a style.
- **On-screen keyboard**: text fields open a gamepad / touch keyboard on Steam Deck and touch devices
  (`ui.OnScreenKeyboard`).
- **GL ES profiles**: on Android, automotive and embedded targets the renderer uses OpenGL ES 3.x; shaders are
  cross-compiled automatically.
- **Graphics API fallback**: Direct3D 12 falls back to Direct3D 11, Metal to Vulkan, anything else to OpenGL.

## Engine portability notes

- The engine library has no hard dependency on GTK or Python: the Python runtime is a separate module
  (`libCQPython`) that is loaded only when a project uses Python scripts, so built games run on systems without
  Python (verified in the Steam Linux Runtime).
- `d3d12.dll` and the Vulkan loader are opened at runtime, so a Windows build starts on machines without them.
