# Cave Ninja (Sai Sumedh Kaveti)

## Milestone 1 (graded version)
The Milestone 1 game is tagged `milestone-1` (commit `dd877de`).
`main` holds later milestone work (networking), so please use the tag.

Requirements: CMake, Ninja, a C++17 compiler. SDL3 is vendored, no install needed.

    git clone https://github.com/CodeRockerr/CSC481-581-Group4.git
    cd CSC481-581-Group4
    git checkout milestone-1
    cmake -S . -B build -G Ninja
    cmake --build build
    ./build/skaveti_game

Run from the repository root, not from inside `build/`. Image paths are relative to the root.

## Controls
- Move: A/D or Left/Right
- Jump: W, Up or Space
- Toggle scaling (pixel vs percentage): Tab
- Touching the shadow wisp or falling off the platform respawns the player

## Files
- `Games/skaveti/main.cpp`: the game
- `Games/skaveti/assets/`: images
- `Engine/`: the shared team engine
