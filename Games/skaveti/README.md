# Cave Ninja (Sai Sumedh Kaveti)

## Milestone 3 (current version on `main`)
Milestone 3 moves Cave Ninja onto the component-based object model, the shared gameplay systems and the new object networking. It is on `main` (merged in PR #16) and will be tagged `milestone-3` after the deadline.

Requirements: CMake, a C++17 compiler, and internet access on the first build (CMake downloads libzmq 4.3.5). SDL3 is vendored, no install needed.

    git clone https://github.com/CodeRockerr/CSC481-581-Group4.git
    cd CSC481-581-Group4
    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build -j8

Run every command from the repository root. Executables are in `build/Release/`.

Offline:

    ./build/Release/skaveti_game

Client-server (start the server, then up to four clients, each in its own terminal; the last word picks the wire format, `binary` or `text`):

    ./build/Release/skaveti_server
    ./build/Release/skaveti_game --client 127.0.0.1 5555 binary
    ./build/Release/skaveti_game --client 127.0.0.1 5555 text

Peer-to-peer (no server):

    ./build/Release/skaveti_game --p2p 6001 127.0.0.1:6002
    ./build/Release/skaveti_game --p2p 6002 127.0.0.1:6001

Networking test (should print `ALL PASSED`):

    ./build/Release/net_sync_test

What it shows:
- Every object is a GameObject built from components (no Entity class)
- Four static platforms with different positions, sizes and colours
- Three moving platforms (horizontal, vertical, circular) using `MovementPath`; the server owns them in client mode
- Hidden spawn points (`SpawnPoint`) and death zones (`DeathZone`); any death returns the ninja to the last checkpoint reached (start, mid, late)
- Side scrolling: two hidden boundaries move the camera as the ninja walks; the camera re-centres after a respawn
- Every window shows every connected ninja; a player who quits or crashes is removed from all other windows

Milestone 3 keys (in addition to the controls below):
- H: show / hide hidden objects (green spawn points, red death zones, blue scroll boundaries)

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
- Touching the shadow wisp or falling into a pit respawns the player (at the last checkpoint from Milestone 3 on)

Milestone 2 time controls (only the local ninja changes):
- P: pause / unpause
- 1: 0.5x speed
- 2: 1x speed
- 3: 2x speed

## Files
- `Games/skaveti/main.cpp`: the game
- `Games/skaveti/server.cpp`: the Cave Ninja server (owns the moving platforms from Milestone 3 on)
- `Games/skaveti/CaveLevel.h`: level layout shared by the game and the server (Milestone 3)
- `Games/skaveti/assets/`: images
- `Engine/`: the shared team engine
