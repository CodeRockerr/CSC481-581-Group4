# CSC 481/581 Game Engine - Fall 2026

Fall 2026 Game Engine Construction project for CSC 481/581 at NC State. The shared SDL3 engine lives in `Engine/`. Each teammate's game lives under `Games/` and links against that library.

## Team

- Adit Jigneshbhai Shah
- Yeva Mheryan
- Sai Sumedh Kaveti

## Project

Reusable engine in `Engine/`, built with C++17, CMake, and vendored SDL3 (Windows and macOS use the same copy). Milestone 2 adds timelines, a multithreaded frame loop, a headless client-server mode, and peer-to-peer play with no elected server.

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

## Milestone 2

| Piece | Where it lives |
|---|---|
| Player time and world time, pause, and 0.5 / 1 / 2 speed | `Engine/include/Timeline.h`, `Engine/src/Engine.cpp` |
| Two worker threads for the next frame | `Engine/src/Engine.cpp`, `Engine/src/FrameWorker.cpp` |
| Headless server, one thread per client, request-reply sockets | `Engine/src/Server.cpp` |
| Peer mode with a shared start time and no central server | `Engine/src/PeerSession.cpp`, `Engine/include/PeerClock.h` |

`P` pauses player time. `1`, `2`, and `3` set that timeline to 0.5, 1.0, and 2.0. The main loop follows that scale (30, 60, or 120 frames per second), so a faster window sends updates more often. Another window keeps its own speed. World time stays at 1.0, so the shared moving object does not slow down with the player.

Design notes are in `Docs/Design/`.

## Repository structure

```text
Engine/include/   Public headers
Engine/src/       Engine implementations
Games/skaveti/    Sai's individual game (Cave Ninja)
Games/ymherya/    Yeva's individual game (Hello Kitty Adventure)
Games/ashah/      Adit's individual game (Lost Under the Sea)
Demo/             Engine demo for timelines and networking
Docs/             Team design notes
vendored/SDL/     SDL3 source
```

## Building

### Prerequisites

- C++17 compiler (Xcode Command Line Tools on Mac, MSVC on Windows)
- CMake 3.16+
- Do not install a separate SDL3. It is vendored in `vendored/SDL`
- libzmq 4.3.5 is fetched by CMake on the first configure

### Build

From the repository root:

```bash
cmake -S . -B build
cmake --build build
```

Run every command below from the repository root so asset paths resolve. On macOS the binaries are under `build/Release/`. On Windows they are under `build/Debug/` or `build/Release/` and end in `.exe`.

## Lost Under the Sea (`Games/ashah`)

Offline, one window. The diver follows player time. The fish follows world time.

```bash
./build/Release/ashah_game
```

| Key | Action |
|---|---|
| A / D or Left / Right | Swim |
| W, Up, or Space | Jump |
| P | Pause or unpause the diver. The fish keeps moving |
| 1 / 2 / 3 | Diver speed 0.5 / 1 / 2 |
| Tab or T | Toggle pixel and percentage scale |

Climb the ledges. Falling off the bottom or touching the fish costs a heart. Health is local to that window.

### Client-server

Start the headless server first and leave it open. It has no window. Then open one game window per player. Each window controls one diver and draws the others. Clients 2, 4, and 6 wear the yellow suit. Closing a window removes that diver from the others.

```bash
./build/Release/ashah_server
./build/Release/ashah_game --client 127.0.0.1 5555
./build/Release/ashah_game --client 127.0.0.1 5555
```

The server listens on port 5555 unless you pass another port: `./build/Release/ashah_server 5566`.

### Peer-to-peer

No server. Each window lists the other peers. The lower port connects to the higher port. All three windows show the same fish, taken from the earliest shared start time. An even port (6002) wears the yellow suit.

```bash
./build/Release/ashah_game --p2p 6001 127.0.0.1:6002 127.0.0.1:6003
./build/Release/ashah_game --p2p 6002 127.0.0.1:6001 127.0.0.1:6003
./build/Release/ashah_game --p2p 6003 127.0.0.1:6001 127.0.0.1:6002
```

## Other games

Cave Ninja uses the same `--client` and `--p2p` pattern:

```bash
./build/Release/skaveti_server
./build/Release/skaveti_game --client 127.0.0.1 5555
./build/Release/skaveti_game --p2p 6001 127.0.0.1:6002 127.0.0.1:6003
```

Hello Kitty Adventure is client-server only:

```bash
./build/Release/ymherya_server
./build/Release/ymherya_game --client 127.0.0.1 5555
```

The engine demo is `engine_demo` and `engine_server`, with the same flags as Lost Under the Sea.

Do not run two servers on the same port. A client and a peer session are different launches of the same game binary.
