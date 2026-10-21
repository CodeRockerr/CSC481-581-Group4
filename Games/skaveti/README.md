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

## Milestone 2 (graded version)
The Milestone 2 game is tagged `milestone-2` (commit `b22da95`, the last commit before the deadline).
`main` holds later milestone work (object model), so please use the tag.

Requirements: CMake, Ninja, a C++17 compiler, and internet access on the first build (CMake downloads libzmq 4.3.5). SDL3 is vendored, no install needed.

    git clone https://github.com/CodeRockerr/CSC481-581-Group4.git
    cd CSC481-581-Group4
    git checkout milestone-2
    cmake -S . -B build -G Ninja
    cmake --build build

From Milestone 2 on, the executables are in `build/Release/`. Run every command from the repository root.

Offline:

    ./build/Release/skaveti_game

Client-server (start the server, then up to three clients, each in its own terminal):

    ./build/Release/skaveti_server
    ./build/Release/skaveti_game --client localhost 5555

The server listens on port 5555 unless you pass another port: `./build/Release/skaveti_server 5566`.

Peer-to-peer (no server, one peer per terminal):

    ./build/Release/skaveti_game --p2p 6001 127.0.0.1:6002 127.0.0.1:6003
    ./build/Release/skaveti_game --p2p 6002 127.0.0.1:6001 127.0.0.1:6003
    ./build/Release/skaveti_game --p2p 6003 127.0.0.1:6001 127.0.0.1:6002

What it shows:
- The ninja runs on `PlayerTime`; the moving platform and the shadow wisp run on `WorldTime`
- Pausing or slowing the ninja does not change the platform or the wisp
- In client mode the server owns the platform; in peer-to-peer mode every peer computes it from a shared start time, with no server
- Every window shows every connected ninja
- Clients at 0.5x, 1x and 2x run their loops at about 30, 60 and 120 Hz and still see the platform in the same place
- The ninja rides the moving platform

## Controls
- Move: A/D or Left/Right
- Jump: W, Up or Space
- Toggle scaling (pixel vs percentage): Tab
- Touching the shadow wisp or falling off the platform respawns the player

Milestone 2 time controls (only the local ninja changes):
- P: pause / unpause
- 1: 0.5x speed
- 2: 1x speed
- 3: 2x speed

## Files
- `Games/skaveti/main.cpp`: the game
- `Games/skaveti/server.cpp`: the Cave Ninja server (Milestone 2)
- `Games/skaveti/assets/`: images
- `Engine/`: the shared team engine
