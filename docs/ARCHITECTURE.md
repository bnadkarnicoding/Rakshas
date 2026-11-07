# Rakshas Engine — Architecture

**Version:** 0.1 · **Date:** 6 November 2026 · **Owner:** Avi · **Status:** draft for review

This document describes how the engine is organised and the rules that keep it healthy. It serves the game described in `CorePrinciples.md` and the Release 1 plan. If anything here conflicts with the Core Principles, the Core Principles win.

---

## 1. Decisions this architecture rests on

| Area | Decision |
|---|---|
| Language | C++20 |
| Graphics | Vulkan 1.3, behind our own render translation layer (the RHI). MoltenVK is used for development on Mac. |
| Libraries | **Own core, borrow plumbing.** The approved libraries are SDL3, miniaudio, Dear ImGui, Tracy, cgltf and stb. Anything else needs Avi's approval. |
| Platform | Steam on Windows first. Steam Deck Verified is a goal. Linux and Mac come later. Consoles need new backends. |
| Hardware | 60 fps on Steam Deck (a 16.6 ms frame). Around 30 fps on older budget laptops (a 33 ms frame) using a low preset. |
| Build | CMake, with Ninja or Visual Studio. |

---

## 2. The layers

Each layer may only use the layers below it. Nothing reaches upward.

```
┌──────────────────────────────────────────────────────────────┐
│ 6. Tools        in-game editor, debug views, asset converter  │
├──────────────────────────────────────────────────────────────┤
│ 5. Game         Rakshas content: legends, teachers, region    │
│                 (mostly DATA files, little code)              │
├──────────────────────────────────────────────────────────────┤
│ 4. Framework    world & entities, camera director, story      │
│                 triggers, dialogue, save/load, weather        │
├──────────────────────────────────────────────────────────────┤
│ 3. Systems      renderer · animation · collision · audio ·    │
│                 input mapping                                 │
├──────────────────────────────────────────────────────────────┤
│ 2. Core         math, memory, logging, files, time, jobs      │
├──────────────────────────────────────────────────────────────┤
│ 1. Platform     window, raw input, OS services (SDL3)         │
│    RHI backend  Vulkan today; PlayStation / Xbox later        │
└──────────────────────────────────────────────────────────────┘
```

| Layer | Folder | Responsibility | Platform-specific? |
|---|---|---|---|
| Platform | `engine/platform/` | Window, events, raw keyboard, mouse and gamepad, clocks | Yes, the only place besides `render/vulkan/` |
| Core | `engine/core/` | Math, logging, asserts, memory, file I/O, job system | No |
| Systems | `engine/render/`, `engine/anim/`, `engine/collision/`, `engine/audio/`, `engine/input/` | The engine's machinery | No, except the RHI backends |
| Framework | `engine/world/`, `engine/camera/`, `engine/story/`, `engine/save/` | Rules shared by all game content | No |
| Game | `game/` + `content/` | The actual Rakshas game | No |
| Tools | `tools/` + in-engine editor | Building content | Developer-only |

---

## 3. The render translation layer (RHI)

The game and the renderer never call Vulkan directly. They talk to our own small interface in `engine/render/rhi.h`:

- create a buffer,
- create a pipeline from shaders,
- begin a frame,
- draw,
- end the frame,
- read back a screenshot.

A **backend** implements that interface for each platform:

| Backend | Platforms | Status |
|---|---|---|
| `render/vulkan/` | Windows, Steam Deck/Linux, Mac (MoltenVK) | Building now |
| `render/ps5/` | PlayStation 5 | Later, needs Sony approval and NDA |
| `render/d3d12/` | Xbox | Later |

The interface stays **small and boring on purpose**. A new backend should take months, not a rewrite. When we need a new rendering feature, we add the smallest possible operation to the RHI and implement it in every live backend.

### Rendering rules from the Core Principles

- **Natural light only** (Tier 3 #8). Sun, moon, sky and fire are the only light sources. No glowing or emissive "magic" materials (Rule 1.3).
- **No camera shake** (decision log). The renderer has no shake facility.
- **Light on the GPU** (Tenet 2.0). Every feature must fit the hardware budget, with a low preset for budget laptops.

---

## 4. Simulation is separate from rendering

- Game logic runs on a **fixed timestep** (60 updates per second), independent of frame rate.
- **Game state never touches the GPU.** The renderer reads a snapshot of the world each frame and draws it.
- This gives us:
  - the same behaviour on fast and slow machines,
  - automated tests of game logic without a screen,
  - a path to a headless server if multiplayer is ever revisited (Tenet 2.9).

---

## 5. Content is data, not code

Whatever Avi or a designer will tune lives in **data files** that the engine loads, never hard-coded in C++:

- the region layout,
- camera zones,
- story triggers,
- dialogue,
- teachers' lessons and prices,
- weather patterns.

Changing a riddle or moving a camera zone should never need a programmer.

- **Format (decided):** our own simple, human-readable text format and reader. It supports comments and multi-line text, uses no quotes or braces, and gives plain-language error messages. JSON may be added later for machine-generated files, with approval.
- **Assets:** artists work in Blender and export **glTF**. Our converter (`tools/assetc`) turns glTF and images into fast binary files. The game only loads the binary files.

---

## 6. Quality rules (the C++ safety net)

C++ allows memory bugs, so every build is checked automatically:

1. **Warnings are errors** on all compilers.
2. **Vulkan validation layers** run in development and test builds. Any validation error fails the test.
3. **Sanitizers** (AddressSanitizer and UndefinedBehaviorSanitizer) run in the Linux test build.
4. **Headless render tests** on every change:
   - The engine renders scenes with no window, using a software GPU (Mesa lavapipe) in the cloud.
   - It saves screenshots, so every change produces pictures you can look at.
5. **Unit tests** for core code (math first).
6. **Continuous integration** on GitHub Actions builds on Windows and Linux for every change and attaches the screenshots.

---

## 7. Coding conventions

- Namespace `rk::`. Files are `snake_case`. Types are `PascalCase`. Functions and variables are `snake_case`.
- No exceptions in runtime code. Errors are return values. Asserts check programmer mistakes.
- No hidden global state. Systems are created and owned explicitly by the application.
- Platform `#ifdef`s only inside `engine/platform/` and RHI backends.
- Every module has a short header comment saying what it is for.

---

## 8. The build ladder

| Rung | Visible result | Main modules |
|---|---|---|
| **1** ✓ | A window opens. A shape lit by a sun appears under a sky. Keyboard and gamepad move the view. | platform, core/math, render (RHI + Vulkan) |
| **2** ✓ | A gray-box landscape from a content file. Sun and moon shadows. A moon and stars. | core/data (content reader), region, render (shadow pass) |
| **3** ✓ | A capsule walks and runs, follows the ground and collides with objects. The camera follows. | game/character (fixed 60 Hz step), input |
| **4** ✓ | The hybrid camera: Explore, Vista, Duel and Conversation framings from camera zones in the region file. It glides with no cuts and no shake, and turns at most 60°/s. Nudge-and-return or fully directed (C). | game/camera_director |
| **4b** ✓ | Detail preview: textures with mipmaps, glTF models, instanced trees and grass with wind, all placeholder art | render (textures, instancing), game/model (cgltf, stb_image), game/foliage |
| 5 | An animated mannequin | anim, asset converter |
| 6 | Footsteps, wind and silence | audio |
| 7 | Editor overlay: place objects and camera zones | tools (Dear ImGui) |
| 8 | Rain, mud, heat. Saving and loading. | weather, save |

**Gate (Release 1 plan, phase 2):** a character crosses a gray-box landscape at the hardware target.

---

## 9. Folder layout

```
rakshas/
  CMakeLists.txt
  docs/ARCHITECTURE.md      this document
  engine/
    core/                   math, log, assert
    platform/               SDL3 window + input
    render/                 rhi.h (translation layer), renderer
      vulkan/               Vulkan backend
  game/                     the Rakshas application
  shaders/                  GLSL, compiled to SPIR-V at build time
  content/                  data files (from rung 2)
  tests/                    unit tests
  third_party/              approved single-file libraries (stb)
  .github/workflows/        CI
```

---

## 10. Open items for Avi

- **Engine name:** "Rakshas Engine" is a working title.
