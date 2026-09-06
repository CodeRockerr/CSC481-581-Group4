# CSC 481/581 Game Engine - Fall 2026

Fall 2026 Game Engine Construction project for CSC 481/581 at NC State. Milestone 1: shared SDL3 engine plus each teammate's individual game.

## Team

- Adit Jigneshbhai Shah
- Yeva Mheryan
- Sai Sumedh Kaveti

## Project

Reusable engine in `Engine/`, built with C++17, CMake, and vendored SDL3 (Windows and macOS use the same copy). Individual games live under `Games/` and link against that library.

## Milestone 1 engine map

| Task | Location |
|---|---|
| 1. Window, renderer, blue clear, game loop | `Engine/src/Window.cpp`, `Renderer.cpp`, `Engine.cpp` |
| 2. Generic entities | `Entity.h`, `EntityManager.cpp` |
| 3. Configurable gravity | `Physics.h` (`setGravity`), `Entity::affectedByGravity` |
| 4. Keyboard polling | `Input.h` (`SDL_GetKeyboardState`, `isKeyPressed`) |
| 5. AABB overlap | `Collision.h` (`checkCollision`) |
| 6. Pixel vs percentage draw scale | `EntityManager` `ScaleMode` + `toggleScaleMode()` |

Default window size is 1920x1080 and resizable. Pass `0, 0` to auto-size to 80% of the display.

## Repository structure

```text
Engine/include/   Public headers
Engine/src/       Engine implementations
Games/saisumedh/  Sai's individual game
Games/ymherya/    Yeva's individual game
Games/ashah/      Adit's individual game (Lost Under the Sea)
Docs/             Team design notes (required for the team hand-in)
vendored/SDL/     SDL3 source
```

## Building

### Prerequisites

- C++17 compiler (Xcode Command Line Tools on Mac, MSVC on Windows)
- CMake 3.16+
- Do not install a separate SDL3 — it is vendored in `vendored/SDL`

### Build

From the repository root:

```bash
cmake -S . -B build
cmake --build build
```

### Run

Run from the repository root so asset paths resolve:

```bash
./build/saisumedh_game
./build/ymherya_game
./build/ashah_game
```

On Windows the executables are under `build/Debug/` or `build/Release/`.

## Lost Under the Sea (`ashah_game`)

Deep-sea platformer using the shared engine.

| Control | Action |
|---|---|
| A / D or arrows | Walk |
| W, Up, or Space | Jump |
| Tab or T | Toggle Pixel vs Percentage scaling (window shrinks/grows) |

- **Static:** vent shelf  
- **Player:** diver (gravity on)  
- **Patrol:** anglerfish (no gravity)  
- Land on the shelf, walk off either end to fall, hit the fish to drop in from the top  

## Development

Team engine design documentation belongs in `Docs/` before the team submission.
