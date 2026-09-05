# CSC 481/581 Game Engine - Fall 2026

Fall 2026 - Game Engine Construction Project for CSC 481/581 at NC State.

## Team

- Adit Jigneshbhai Shah
- Yeva Mheryan
- Sai Sumedh Kaveti

## Project

Reusable game engine built using:

- C++
- SDL3
- CMake

## Repository Structure

- `Engine/` - Shared game engine
- `Games/` - Individual team member games
- `Assets/` - Game assets
- `Tests/` - Tests
- `Docs/` - Project documentation

## Building

### Prerequisites

- C++17 compiler (Xcode Command Line Tools on Mac, MSVC on Windows)
- CMake 3.16+
- SDL3 is vendored in `vendored/SDL` (do not install a separate copy)

### Build

```bash
cmake -S . -B build
cmake --build build

## Development

See `Docs/` for architecture and team development documentation.
