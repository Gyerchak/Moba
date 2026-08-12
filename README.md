# Moba

A minimal League-of-Legends-style game engine sandbox written in **C++20** with
**Vulkan** for rendering and **GLFW** for the window/input. Built as a clean,
small foundation — deliberately "basic" to stay unbuggy and easy to extend.

> Status: **early foundation**. Currently a top-down instanced renderer with a
> simple unit simulation (minions marching down a lane, towers, a player
> champion you can follow). No networking or full MOBA/Mmorpg gameplay yet —
> those are planned next steps.

## Features (current)

- Vulkan 1.2 renderer: swapchain, render pass, graphics pipeline, instanced
  drawing of colored quads on an XZ ground plane.
- GLFW window + input (pan camera with WASD, zoom with scroll wheel).
- Minimal top-down camera (orthographic, follows the selected champion).
- Simple game simulation:
  - Static towers + nexuses for blue and red teams.
  - Minion waves spawn periodically and march down a lane (waypoint path).
  - A player champion (green) you can select with `Space` and follow.
- Project keeps its own skills, agents, commands and chat history when launched
  from the SONIC drive sandbox.

## Build

Requirements: CMake >= 3.20, a C++20 compiler, the Vulkan SDK + `glslc`, GLFW3,
and GLM (headers).

```bash
./run.sh          # build (if needed) and run from build/
```

or manually:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
cd build && ./moba
```

Shaders are compiled by `glslc` at build time into `shaders/*.spv`.

## Controls

| Key        | Action                        |
|------------|-------------------------------|
| `W` `A` `S` `D` | Pan the camera          |
| Mouse wheel | Zoom in/out                  |
| `Space`    | Select / follow the champion |
| `R`        | Reset the match              |
| `Esc`      | Quit                         |

## Project layout

```
Moba/
  CMakeLists.txt      Build config (+ shader compilation)
  shaders/            GLSL sources and compiled .spv
  src/
    main.cpp          Window, game loop, camera, input, per-frame render
    vulkan_context.*  Vulkan setup + instanced draw pipeline
    game.*            Unit simulation (minions, towers, champion, lanes)
    shader.*          SPIR-V file loading helper
  assets/             (reserved for future textures/models)
  run.sh              Build + run launcher
```

## Roadmap (next, still basic-first)

- Textured / flat-shaded map rendering (map quad, river, lane markings).
- Attack/health + deaths so minions and towers actually fight.
- Click-to-move + ability keys for the champion.
- Simple bot opponent.
- Then: networking for a 2-player / mmorpg-style shared world.

## Notes

- The engine renders top-down in world space XZ (Y-up), using one shared quad
  instanced per unit with a per-instance color and scale.
- No assets are bundled yet; rendering uses flat colors.
- Extend `Game` in `game.cpp` for gameplay; keep `VulkanContext` as a thin
  reusable renderer.
