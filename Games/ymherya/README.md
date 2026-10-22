# Individual Game
CSC 481 Game Engine Foundations (Group-4)
Yeva Mheryan

# Hello Kitty Adventure

A 2D side-scrolling platformer built with the team's shared C++/SDL3 engine. Hello Kitty hops across a long trail of pink clouds, jumps over pits, and avoids Kuromi, who guards the start of the level. You have three hearts, and a few hidden spawn points decide where you reappear.

Milestone 3 uses the team’s component `GameObject` model and object networking (`NetworkIdentity`, `publish` / `sync`).

## Build and run

Requirements: CMake, Git, and a C++17 compiler. SDL3 is included in the repository. CMake downloads ZeroMQ the first time it configures, so the first build needs an internet connection.

Run everything from the **repository root**, not from `Games/ymherya`. The game loads its images with paths like `Games/ymherya/assets/background.png`, so it will not find them from anywhere else.

```sh
git clone https://github.com/CodeRockerr/CSC481-581-Group4.git
cd CSC481-581-Group4
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target ymherya_game ymherya_server -j8
```

On some setups the executables end up in `build/Debug/` (Windows with Visual Studio) or directly in `build/`. Use whichever folder contains `ymherya_game`.

**Offline (single player):**

```sh
./build/Release/ymherya_game
```

**Client–server (recommended for multiplayer):**

```sh
# Terminal 1
./build/Release/ymherya_server 5555

# Terminal 2 (and more clients as needed, up to four)
./build/Release/ymherya_game --client 127.0.0.1 5555
```

Optional wire format (default is binary):

```sh
./build/Release/ymherya_game --client 127.0.0.1 5555 binary
./build/Release/ymherya_game --client 127.0.0.1 5555 text
```

**Peer-to-peer (optional for CSC 481; no server):**

```sh
./build/Release/ymherya_game --p2p 6001 127.0.0.1:6002
./build/Release/ymherya_game --p2p 6002 127.0.0.1:6001
```

To connect from another computer, replace `127.0.0.1` with the server’s address. The game view is 1280 × 800 in game units and scales with the window when you resize it.

## How to play

1. Hello Kitty starts on the ground near the middle of the first screen. Kuromi patrols the ground near the start.
2. Jump onto the clouds to stay away from Kuromi. The level is about three screens wide. Nine clouds are fixed and three move: one goes up and down, one goes side to side, and one drifts in a circle. Standing on a moving cloud carries you with it.
3. Walk toward the right edge of the screen and the camera scrolls with you. Walk back toward the left edge and it scrolls back, so you can return to earlier parts.
4. In the second half of the level the ground has two gaps (pits). Cross them by jumping from cloud to cloud. If you fall in, you lose a heart and you are sent to a spawn point.
5. Watch your hearts in the top left corner. You lose one when Kuromi touches you or when you fall into a pit. You get one second of protection after you respawn, and two seconds when the game starts.
6. When you lose your last heart the hearts refill and you start over at the first spawn point.

The window title and the console print a short story line for each event (landing on a cloud, getting caught, falling into a pit), together with how many hearts you have left.

## Controls

| Control | Action |
| --- | --- |
| A / D or Left / Right | Walk left and right |
| Space, W, or Up | Jump (only while standing on the ground or a cloud) |
| P | Pause or resume your own timeline |
| 1 / 2 / 3 | Half speed, normal speed, double speed for your own player |
| Tab | Toggle percentage / fixed scale mode |
| Escape | Quit |

P and 1/2/3 come from the team's timeline code from Milestone 2. They only change your player's timeline, so Kuromi and the clouds keep running.

## Multiplayer

In client mode the **server owns** the moving clouds and Kuromi so every window shows them in the same place. Each client owns its own Hello Kitty and sees the others through object replication. If a client closes, its Hello Kitty disappears from the other windows and everything else keeps running.

## Milestone coverage

I am in CSC 481, so this covers the required parts of each milestone. The optional parts (peer-to-peer, binary/text comparison) are available but not required.

### Milestone 1–2 (summary)

Hello Kitty uses gravity and keyboard input on `PlayerTime`. Clouds, Kuromi, and remote players run on world time. Client–server multiplayer uses the team network stack. Pause (P) and 1/2/3 only affect the local player timeline.

### Milestone 3: Object model and networked scenes

| Requirement | Implementation |
| --- | --- |
| Player character | Hello Kitty walks and jumps with the keyboard; other clients appear as replicas. |
| Static platforms | Nine fixed clouds that differ in position and size (big and small textures). |
| Moving platforms | Three moving clouds with `MovementPath`: vertical, horizontal, and circular. |
| Spawn points | Hidden `SpawnPoint` objects: `start`, `middle`, `upper`. |
| Death zones | Hidden `DeathZone` pits (and Kuromi as a hazard); `GameplaySystems::updateDeathZones` teleports to the named spawn. |
| Side scrolling | Camera + left/right scroll boundary objects. |
| Object model | All gameplay objects are `GameObject`s with components (`Transform`, `Sprite`, `Collider`, `PhysicsBody`, `PlayerInput`, `MovementPath`, `SpawnPoint`, `DeathZone`, `NetworkIdentity`). |
| Networking | `NetworkIdentity` + `publish` / `sync`. Server creates movers and Kuromi; clients replicate them. |

## Files

| File | Purpose |
| --- | --- |
| `main.cpp` | Game: scene setup, controls, hearts, death zones, camera scroll, networking |
| `KittyLevel.h` | Level data: clouds, movers, spawns, pits, net kinds |
| `server.cpp` | Headless server; owns moving clouds and Kuromi |
| `assets/` | Hello Kitty, Kuromi, background, cloud, and heart images |

The shared engine, object model, and networking code are in the team's `Engine/` folder.
