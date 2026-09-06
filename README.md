# CSC 481/581 Game Engine - Fall 2026

Fall 2026 Game Engine Construction project for CSC 481/581 at NC State.

## Team

- Adit Jigneshbhai Shah
- Yeva Mheryan
- Sai Sumedh Kaveti

## Project

Reusable game engine built with C++17, CMake, and vendored SDL3. The shared engine lives in `Engine/`. Each teammate builds a separate game under `Games/` against that library.

## Repository Structure

```text
Engine/                 Shared game engine
  include/              Public headers (Window, Renderer, Entity, Physics, Input, Collision, ...)
  src/                  Engine implementations
Games/                  Individual games (one folder per teammate)
  saisumedh/
  ymherya/
Assets/                 Optional shared assets
Docs/                   Engine design and milestone notes
Tests/                  Reserved for tests
vendored/SDL/           SDL3 source (do not install a separate copy)
```

## Building

### Prerequisites

- C++17 compiler (Xcode Command Line Tools on Mac, MSVC on Windows)
- CMake 3.16+
- SDL3 is vendored in `vendored/SDL` — do not install a system copy

### Build

From the repository root:

```bash
cmake -S . -B build
cmake --build build
```

### Run

Run from the repository root so game asset paths resolve correctly:

```bash
./build/saisumedh_game
./build/ymherya_game
```

On Windows the executables are under `build/Debug/` or `build/Release/`.

Default window size is 1920x1080. The window is resizable. In Sai's game, **Tab** toggles pixel vs percentage scaling (Task 6).

## Development

See `Docs/` for architecture and team documentation.
