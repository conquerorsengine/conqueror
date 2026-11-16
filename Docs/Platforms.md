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

## Installs and modules (Hub)

Like Unity Hub, the hub's **Installs** page lists the editor versions on this computer (tabs All / Official releases /
Pre-releases, Search, **Locate** for an editor you built yourself, **Install Editor**). Versions come from the engine's
GitHub Releases: normal releases are *Official*, GitHub pre-releases are *Pre-release*, and the *ConquerorX* channel is
rebuilt by CI from every commit on `main` — when it is installed, the hub updates it to the newest commit each time it
starts. Versions older than the first published release are not downloadable; **Install Editor > Archive** shows how to
build them from source. A project opens with the editor version that last saved it (`EditorVersion` in the `.cqproj`);
**Open with** picks another one.

Modules are chosen when installing (or later with **Manage > Add modules**):

* **Platforms** — a prebuilt player for the platform, so building for it needs no toolchain (Linux x86_64 / ARM64 /
  other CPUs, Windows, macOS, Web, Android (also TV, Automotive, ChromeOS), Meta Quest, Android XR, FreeBSD / OpenBSD /
  NetBSD, GNU/Hurd, Embedded Linux ARM64). A dedicated server is the normal build started with `--server`. iOS, QNX and
  HarmonyOS packagers come with the editor but need the vendor's SDK (Xcode on a Mac, the QNX SDP, the OpenHarmony SDK):
  their licenses do not allow shipping them.
* **Languages** — every scripting language is its own module (LuaQ, ConquerorScript and Visual Scripting are always
  included; C++, C, Rust, Zig, C#, Java, Python). Each module lists what it needs on this computer (JDK 17+, .NET 8,
  a C/C++ compiler, Rust, Python), shows what was found and how to install the rest; tools whose license allows it come
  with the module (Zig, Eclipse Temurin JDK, .NET SDK, llvm-mingw on Windows).
* **Documentation** — an offline copy of the manual.

**Build Settings** offers **Install <module>** for the selected platform when its prebuilt player is missing.
Releases are made by `.github/workflows/release.yml` (`Tools/Release/package.py`, `make_manifest.py`,
`fetch_toolchain.py`): every archive is listed in `conqueror-release.json` with its size and SHA-256, which the hub checks.

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
| Web | Emscripten SDK (emsdk) + Python 3, or a prebuilt web player (`CQ_WEB_RUNTIME`) | any desktop |
| Linux ARM (32-bit), x86 (32-bit), RISC-V, PowerPC, LoongArch | clang + lld + a Debian sysroot (`Tools/Linux/make-linux-sysroot.sh <arch>`, `CQ_LINUX_<ARCH>_SYSROOT`) | Linux |
| iOS | Xcode with the iOS platform + CMake + Python 3 (`Tools/iOS/package_ios.py`) | a Mac |
| QNX | QNX SDP (`QNX_HOST`, `QNX_TARGET`); built from the command line | Linux, Windows |
| HarmonyOS (PC, tablet) | OpenHarmony native SDK for the libraries, DevEco Studio or hvigor for the `.hap`; built from the command line ([HarmonyOS](#harmonyos-pc-and-tablet)) | Linux (also Windows / macOS with their SDK) |

Cross builds (Windows and FreeBSD from Linux) compile the player from the engine source the first time (several
minutes) into `build-windows` / `build-freebsd` next to the engine source (`CQ_WINDOWS_BUILD_ROOT` /
`CQ_FREEBSD_BUILD_ROOT` move them); later builds reuse it and only recompile what changed. A Windows game built on
Linux is started with Wine by **Build And Run** when Wine is installed.

## Targets

| Target | Built from | Status |
|--------|-----------|--------|
| Linux x86-64 | Linux | primary platform; full automated test suite; also runs inside the Steam Linux Runtime (Steam Deck) |
| Windows x64 | Windows (MSVC / MinGW) or Linux (MinGW cross toolchain, `Tools/Windows`; also from the editor's Build Settings) | cross build tested under Wine on OpenGL, Vulkan, Direct3D 11 and Direct3D 12 |
| macOS | macOS | editor, player and hub built on Apple Silicon in CI; the CI renders a 3D template in the editor with the Metal backend (test metal_smoke) |
| Android (phone / tablet) | Linux with the Android NDK | APK built and signed without Gradle (`Tools/Android/package_apk.py`); tested on the emulator (GL ES 3) |
| Android Automotive | same | tested on the AAOS emulator ([Automotive](Automotive.md)) |
| Automotive Grade Linux | Yocto layer `Tools/AGL/meta-conqueror` | Wayland-only player build tested |
| FreeBSD | Linux (cross, `Tools/BSD`) or FreeBSD | cross build links; runtime not yet tested on BSD |
| QNX | QNX SDP (`Tools/QNX`) | compile-checked against stub headers only |
| HarmonyOS 5+ PC (2in1) / tablet | Linux with the OpenHarmony native SDK (`Tools/HarmonyOS`) | the **editor** and the player compile and link for arm64-v8a and x86_64 (OpenHarmony 6.0 SDK, CI); HAP project template and packager; not run on a device or emulator yet |
| iOS / iPadOS | a Mac with Xcode (`Tools/iOS/package_ios.py`, also from Build Settings) | UIKit + Metal player app; without a Team ID built for the iOS Simulator, with a Team ID signed for devices (.app + .ipa). Tested in CI: the editor builds the 3D template, the app is installed on the iOS Simulator, runs and renders (test ios_simulator, `Tools/iOS/simulator_test.py`); not tested on a device |
| Web (WebAssembly) | any desktop with emsdk (`Tools/Web/package_web.py`, also from the editor's Build Settings) | WebGL 2 and WebGPU players tested in headless Chromium (the FPS template); a static site: `index.html` + `Build/` (gzip / brotli, data cached in the browser, saves in IndexedDB); single-thread by default, multithreaded with SharedArrayBuffer (`coi-serviceworker.js` for hosts without COOP / COEP); WebGPU backend with a Dawn source tree (`CQ_DAWN_DIR`) |
| Linux ARM (32-bit), x86 (32-bit), RISC-V 64, PowerPC 64 LE, LoongArch 64 | Linux (cross, `Tools/Linux/linux-cross.toolchain.cmake`) | players cross-built; RISC-V player runs under qemu-user up to window creation |
| Steam Deck | Linux build | 1280×800 borderless, Deck button glyphs, on-screen keyboard |

## Portable game package (.cqspace)

**Build Settings > Portable Game (.cqspace)** writes one file, `Builds/<Game>.cqspace`, that the Conqueror player opens
on every platform it runs on (like LÖVE's `.love`, a Java `.jar` or Godot's `.pck`). The file holds the project file,
`Assets/`, `Packages/` and a manifest (game name and version, start scene, scripting languages, needed engine features,
the engine version that made it); engine resources come with the player. A package that needs a newer engine is refused
with a clear message. Scripts compiled to machine code (C++, C, Rust, Zig) cannot run from a portable package: the build
names the scripts that block it.

* Desktop: `ConquerorRuntime <Game>.cqspace`, or open the file directly. Without a game, the player shows
  *Open a .cqspace* with the recently opened games. File association: `Tools/Linux/register-cqspace.sh <bin folder>`
  (MIME type + `.desktop` entry), `Tools/Windows/register-cqspace.ps1 <bin folder>` (per user, no admin rights).
* Web: a player site without a game (`Tools/Web/package_web.py --player --out <folder>`) opens `?game=<url of the
  .cqspace>` or a file picker.
* Android: `Tools/Android/package_apk.py --player` makes the *Conqueror Player* app that opens `.cqspace` files from
  the file manager, downloads or sharing.
* iOS / iPadOS: `Tools/iOS/package_ios.py --player` declares the `.cqspace` document type; files arrive from Files,
  AirDrop or *Open in*. (iOS App Store rules limit apps that run downloaded games.)

Tested: the package is built in the editor and opened by the desktop player (test `build_portable_package`). The web,
Android and iOS openers are compiled in CI but were not run on those platforms yet.

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
| iOS | bundle identifier, build number, devices, minimum iOS, orientation, team ID, camera usage, ARKit, status bar | the iOS packager (`Tools/iOS/package_ios.py`): minimum iOS is at least 16 (Metal 3 APIs); Team ID selects device signing; the status bar is always hidden; ARKit is not available yet |
| Web | WebGL 2 or WebGPU, page template, compression, initial memory, multithreading, data caching | `Tools/Web/package_web.py` (page template, compression, threads player, caching) and the page loader (WebGPU device request with WebGL 2 fallback) |
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

## HarmonyOS PC and tablet

HarmonyOS 5 and later (HarmonyOS NEXT) run only HAP apps: an ArkTS page plus native libraries. The editor and the player
are built as native libraries (`libConquerorEditor.so`, `libconqueror.so`) that a small HAP project loads; the page is a
full-window `XComponent` and the engine draws into it with EGL and OpenGL ES 3.2 (3.1 / 3.0 fallback) on its own thread.
The device types are `2in1` (HarmonyOS PC) and `tablet`; the minimum is API 12 (HarmonyOS 5.0).

Build the libraries with the OpenHarmony native SDK (the CI job uses the 6.0 Release public SDK from
`repo.huaweicloud.com`, SHA-256 checked) and put them into a HAP project:

```sh
cmake -S . -B build/ohos-arm64 -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_TOOLCHAIN_FILE=<sdk>/native/build/cmake/ohos.toolchain.cmake -DOHOS_ARCH=arm64-v8a -DOHOS_STL=c++_static
cmake --build build/ohos-arm64 --target ConquerorEngine conqueror        # editor + player
python3 Tools/HarmonyOS/package_hap.py --out build/hap-editor --build-dir build/ohos-arm64 [--build-dir build/ohos-x86_64]
python3 Tools/HarmonyOS/package_hap.py --out build/hap-game --player <built game folder> --build-dir build/ohos-arm64
```

The packager copies `Tools/HarmonyOS/template` (AppScope, `entry` module with `EntryAbility.ets` and `pages/Index.ets`,
hvigor files), the libraries into `entry/libs/<abi>/` and the engine resources (editor) or the built game (player) into
`entry/src/main/resources/rawfile/` with a `cq_files.txt` list; the app extracts them into its files folder on the first
start (again only when they change). Open the folder in DevEco Studio, set up signing (Project Structure > Signing
Configs) and run, or build with `hvigorw assembleHap --mode module -p product=default` (`--assemble`). `--openharmony`
writes an OpenHarmony product instead of a HarmonyOS one (hvigor with the OpenHarmony public SDK).

What the platform layer (`Engine/src/Platform/OHOS`) provides:

* window and frames: the XComponent surface (created / resized / destroyed); frames follow the XComponent frame
  callback (display refresh); the engine pauses while the surface is gone (app in the background);
* input: mouse buttons, movement and wheel, keyboard (XComponent key codes mapped to engine keys, US-layout characters),
  text from an input method (`insertText`), touch (first finger acts as the left mouse button);
* the system file picker for every editor file dialog (open, open multiple, save, pick folder), cursor shapes,
  opening links, the window close button (the editor asks about unsaved changes first), clipboard (system pasteboard on
  API 13+, reading it needs the pasteboard permission, otherwise in-app), screen density for UI scaling, logs in hilog
  (`hdc shell hilog -T Conqueror`);
* there is no project hub (one app = one process): on the first start the editor copies the 3D example project to
  `Projects/My Project` in its files folder and opens it; other projects are opened with File > Open Project... (system file picker).

Not available on HarmonyOS: starting other programs (building games for other platforms, C# / C++ script compilers,
Git, external editors), Python and Java scripting, the Vulkan backend (OpenGL ES only), gamepads (the OpenHarmony NDK
has no gamepad API), extra OS windows (the Window component, ImGui panels outside the main window).

Status: both libraries compile and link with no undefined symbols for arm64-v8a and x86_64; nothing has been run on a
HarmonyOS device or emulator yet (the DevEco emulator needs Windows or macOS). Unverified details: the mouse wheel scale
and direction (`CQ_OHOS_WHEEL_SCALE` overrides it), whether paths returned by the file picker can be opened from native
code for every location, and the window close callback (`ohos.permission.PREPARE_APP_TERMINATE`).

## Web player

`Tools/Web/package_web.py <built game> --out <site> [--build]` turns a built game into a static web site; the editor's Build Settings > Web
does the same (the first build compiles the player with Emscripten). `Tools/Web/serve.py <site> --open` serves it locally with the right
MIME types and COOP / COEP headers (Build And Run starts it).

* Graphics: WebGL 2 (OpenGL ES 3.0 level). Desktop OpenGL functions WebGL lacks are stubbed (a one-time warning per function); compute
  shader effects (NIS, CMAA 2, FSR 2) and multisample-texture effects (SRAA) are not selectable on such GPUs (also macOS OpenGL 4.1).
* Threads: off by default (the player runs everything on the browser's main thread and works on any host); with Player Settings >
  Web > Threads the multithreaded player needs SharedArrayBuffer (served with COOP / COEP, or through `coi-serviceworker.js`).
* Not in browsers: UDP networking (ENet), UPnP, native C++ scripts, video recording, the AI assistant, JIT (ConquerorScript runs
  interpreted).
* WebGPU: `Tools/WebGPU/fetch-dawn.sh <folder>` and `-DCQ_DAWN_DIR=<folder>` build the GL -> WebGPU translation layer (shaders
  SPIR-V -> WGSL with Tint). The page requests a WebGPU device when Graphics API is WebGPU and falls back to WebGL 2 without it.
  Not yet in the WebGPU backend: compute shaders and multisample textures (NIS, CMAA 2, FSR 2, SRAA and MSAA are not selectable) and
  occlusion queries (objects count as visible). On desktop the backend needs a WebGPU library (Dawn or wgpu-native) at run time.
* 16 textures per shader stage (WebGPU default, WebGL 2, many mobile GPUs): shaders get `CQ_TEXTURE_BUDGET_16` (reflection probes use
  the sky reflection; APV, dynamic GI and planar reflections are off) and the 2D batch renderer uses 14 texture slots.
