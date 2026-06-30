# Conqueror's Engine

A 2D/3D game engine built with C++ and OpenGL, with experimental Vulkan support.

**Version:** 0.110

## Features

- 2D and 3D rendering with OpenGL 4.6
- Entity Component System (ECS) using EnTT
- 2D physics with Box2D
- 3D physics with Jolt Physics
- ConquerorScript - custom scripting language
- Scene serialization with YAML
- ImGui-based editor
- Content browser with drag and drop
- Audio system with miniaudio
- Text rendering with FreeType
- Model loading with Assimp
- Network support with ENet
- Cross-platform (Linux, Windows)

## Requirements

- CMake 3.20 or higher
- C++17 compatible compiler (GCC 10+, Clang 12+, MSVC 2019+)
- OpenGL 4.6 compatible GPU
- Linux: X11 or Wayland development libraries

## Building from Source

### Linux

```bash
# Install dependencies (Fedora)
sudo dnf install cmake gcc-c++ mesa-libGL-devel libXrandr-devel libXinerama-devel libXcursor-devel libXi-devel wayland-devel libxkbcommon-devel

# Clone the repository
git clone https://github.com/conquerorsengine/conqueror.git
cd conqueror

# Build
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++ -DCMAKE_C_COMPILER=gcc
cmake --build build -j$(nproc)
```

### Windows

```bash
# Clone the repository
git clone https://github.com/conquerorsengine/conqueror.git
cd conqueror

# Build
cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++ -DCMAKE_C_COMPILER=gcc
cmake --build build -j%NUMBER_OF_PROCESSORS%
```

## Running

After building, the editor executable will be in the `build/bin/` directory:

```bash
cd build/bin
./ConquerorEngine
```

## Creating a Project

1. Launch the Launcher
2. Click "New Project"
3. Choose a template (2D, 3D, or Empty)
4. Select a location and name your project
5. Click "Create"

```bash
cd build/bin
./ConquerorLauncher
```

## Project Structure

```
YourProject/
  Assets/
    Scenes/       - Scene files (.cqscene)
    Scripts/      - ConquerorScript files (.cqs)
    Textures/     - Texture files
    Materials/    - Material files
    Models/       - 3D model files
    Audio/        - Audio files
  Packages/       - Package files
  ProjectName.cqproj - Project configuration
```

## ConquerorScript

ConquerorScript is a simple scripting language for game logic:

```
using Conqueror;

script PlayerController : ConquerorScript
{
    var moveSpeed = 5.0;

    func OnCreate()
    {
        LogInfo("Player created!");
    }

    func OnUpdate(ts)
    {
        if (IsKeyPressed(Key.W))
        {
            // Move forward
        }
    }
}
```

## Third-Party Libraries

This project uses the following open-source libraries:

| Library | License |
|---------|---------|
| Dear ImGui | MIT |
| GLFW | zlib/libpng |
| Box2D | MIT |
| Jolt Physics | MIT |
| EnTT | MIT |
| spdlog | MIT |
| yaml-cpp | MIT |
| Assimp | BSD-3-Clause |
| miniaudio | Public Domain / MIT |
| mbedTLS | Apache-2.0 OR GPL-2.0-or-later |
| FreeType | FTL or GPLv2 |
| stb | MIT/Public Domain |
| ImGuizmo | MIT |
| ImGuiColorTextEdit | MIT |
| imnodes | MIT |
| ENet | MIT |
| nativefiledialog-extended | zlib |
| miniupnpc | BSD |
| zlib | zlib |
| zstd | BSD |
| GLM | MIT |

## License

Conqueror's Engine is licensed under the [Mozilla Public License 2.0](LICENSE) with an additional clause: **Any modifications made to the game engine source code must be disclosed publicly.**

Third-party libraries in the `External/` directory retain their original licenses.

## Contact

- LinkedIn: [Fazli Ozlemis](https://www.linkedin.com/in/fazl%C4%B1-%C3%B6zlemi%C5%9F-3929673b0/)
- Email: fazli@vertexishere.com
