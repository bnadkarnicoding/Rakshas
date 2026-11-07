# Rakshas

A story-and-exploration game set in a pristine, mythic world stretching from India to Japan, built on our own engine.

- **Design rules:** `CorePrinciples.md`, kept in the Rakshas Studio project.
- **Engine design:** [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md)

## Current state: rung 1

A window shows a gray-box scene under a natural sky, lit by a sun that moves through a full day, with a moonlit night. You can orbit the view with the keyboard or a gamepad.

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

1. Install **Visual Studio 2022 Community** with the **"Desktop development with C++"** workload. This includes CMake.
2. Install **Git for Windows**.
3. Install the **Vulkan SDK** from LunarG (vulkan.lunarg.com). The defaults are fine. It provides the shader compiler and validation layers.

**Build and run:**

Open **"x64 Native Tools Command Prompt for VS 2022"** and run:

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

  The exit code is non-zero if any Vulkan validation error or engine error occurs.
- GitHub Actions (`.github/workflows/ci.yml`) builds on Windows and Linux. On Linux it also runs the tests and renders screenshots for every change.
