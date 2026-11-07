# Rakshas

A story-and-exploration game set in a pristine, mythic world stretching from India to Japan, built on our own engine.

- **Design rules:** `CorePrinciples.md`, kept in the Rakshas Studio project.
- **Engine design:** [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md)

## Current state: rung 2

A gray-box landscape with hills, a ridge and a flat village clearing, loaded from a content file. The sun and moon cast soft shadows, and the night sky has a moon and stars. The view orbits with the keyboard or a gamepad.

### Changing the land yourself

The landscape and its placeholder objects live in a plain text file:

```
content/regions/rung2_forest_lake.rk
```

Open it in any text editor (Notepad works). You can:

- change a number,
- add a `hill`,
- move a `prop`.

Save, rebuild (`cmake --build --preset windows-release`), and run. If something in the file is wrong, the game tells you the line and suggests a fix, for example "did you mean 'position'?".

| Control | Keyboard | Gamepad |
|---|---|---|
| Orbit the view | A / D or ← / → | Left stick ↔ |
| Closer / further | W / S or ↑ / ↓ | Left stick ↕ |
| Scrub time of day | Q / E | Triggers |
| Pause the sun | Space | A / Cross (South) |
| Screenshot | F12 | — |
| Quit | Esc | Back / Select |

## Building on Windows

**One-time setup:**

1. Install **Visual Studio Community** (2022 or newer) with the **"Desktop development with C++"** workload. This includes CMake and Ninja.
2. Install **Git for Windows**.
3. Install the **Vulkan SDK** from LunarG (vulkan.lunarg.com). The defaults are fine. It provides the shader compiler and validation layers.

**Build and run:**

From the Start menu, open **"x64 Native Tools Command Prompt"** (installed with Visual Studio) and run:

```
git clone <your repository URL> rakshas
cd rakshas
cmake --preset windows
cmake --build --preset windows-release
build\windows\bin\rakshas.exe
```

## Building on Linux / Steam Deck

```
sudo apt install cmake ninja-build g++ libvulkan-dev glslc vulkan-validationlayers \
                 libx11-dev libxext-dev libwayland-dev libxkbcommon-dev
cmake --preset linux-debug
cmake --build --preset linux-debug
./build/linux-debug/bin/rakshas
```

## Automated checks

- `rk_tests` runs the unit tests.
- Headless render, with no window:

  ```
  rakshas --headless --validate --time 0.3 --out shot.png
  ```

  Add `--low` to use the low graphics preset (smaller shadow map), or `--region <file>` for another region file:

  ```
  rakshas --low --region content/regions/rung2_forest_lake.rk
  ```

  The exit code is non-zero if any Vulkan validation error or engine error occurs.
- GitHub Actions (`.github/workflows/ci.yml`) builds on Windows and Linux. On Linux it also runs the tests and renders screenshots for every change.
