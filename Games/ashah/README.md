# Individual Game
CSC 581 Game Engine Foundations (Group-4)
Adit Jigneshbhai Shah
(Student ID: 200643709)

# Lost Under the Sea: Abyss Run

A 2D underwater exploration game built with the team's shared C++/SDL3 engine for
CSC581. Explore three regions, recover all nine pearls, reach the seabed, and
return through the surface buoy to win.

## Build and run

Requirements: CMake, Git, and a C++17 compiler. Python 3 is needed only for the
multiplayer test script. SDL3 is included in the repository; CMake downloads
ZeroMQ during the first configuration, which requires an internet connection.

Run from the **repository root**, not from `Games/ashah`:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build --config Release --target ashah_game ashah_server ashah_gameplay_test --parallel 4
./build/Release/ashah_game
```

Press **Enter or Space** on the title screen. Keep `Games/ashah/assets` with the
source tree so the game can find its artwork.

The game requests a 1920×1080 window. The shared engine fits it to smaller displays.
The default proportional mode scales the 1280×720 game view to the window.

## How to play

1. Jump from the starting dock into the sea.
2. Explore the **Coral Gardens**, **Shipwreck**, and **Abyss**. Collect all **nine
   pearls**, including the pearl above the rotating kelp shelf.
3. Reach the recovery bed at the bottom. Reaching the bottom and collecting every
   pearl unlocks the return objective.
4. Swim back to the surface, align with the buoy's opening, and swim **up through
   its center**. Passing downward through the opening does not win.
5. The scene freezes at the buoy and displays the victory message. Press **R** to
   start another dive.

Swimming uses inertia, water drag, buoyancy, and currents. The diver stands and
walks when its feet touch a platform, including underwater and moving shelves.
After landing, release and press the jump control again to leave the platform.
Holding the same swim control keeps the diver standing after landing.

### Lives, checkpoints, and Overdrive

- **Lives:** Start with three lives, shown as bubble icons. These are lives, not a
  continuously draining oxygen meter. An unprotected fish or sea-urchin hit costs
  one life and returns you to the current checkpoint, keeping collected pearls.
  Brief protection prevents repeated damage immediately after respawning.
- **Checkpoints:** The dock, Shipwreck, and Abyss provide spawn locations.
  Checkpoints and elapsed time never restore lives. At zero lives, the run ends
  until you press R.
- **Overdrive:** Golden cores grant three seconds of the yellow alternate suit,
  increased speed, and immunity to fish, anglerfish, and sea urchins. Pausing stops
  the timer; speed keys do not extend the boost.
- **Abyss boundary:** Falling into the death zone costs a life, even during Overdrive.

## Controls

| Control | Action |
| --- | --- |
| Enter or Space | Start from the title screen |
| A / D or Left / Right | Walk or steer left and right |
| Space, W, or Up | Jump from a platform; hold to swim upward |
| S or Down | Descend faster; drop through underwater shelves, but not the dock |
| P | Pause or resume the local game world |
| F | Toggle pixel and proportional scaling |
| 1 | Half player speed and client loop/send rate |
| 2 | Normal player speed and client loop/send rate |
| 3 | Double player speed and client loop/send rate |
| R | Restart with three lives and reset pearls and power pickups |
| Escape | Quit |

**Scaling:** Resize the window after pressing F. Pixel mode keeps world sprites at
fixed pixel sizes; proportional mode scales the scene with the window. The HUD
fits the window in both modes.

**Pause and speed:** P freezes the local diver, fish, shelves, effects, camera, and
gameplay timers. In multiplayer, the server and other clients continue; resuming
updates your view to the shared scene. Keys 1/2/3 affect your player and client
frequency, while server-controlled platforms and other clients keep their own clocks.

## Multiplayer

### Client-server

Start the headless server in one terminal:

```sh
./build/Release/ashah_server
```

Start each client in a **separate terminal**. The default port is 5555. Both formats
work with the same server:

```sh
./build/Release/ashah_game --client 127.0.0.1 5555 binary
```

```sh
./build/Release/ashah_game --client 127.0.0.1 5555 text
```

For different computers, replace `127.0.0.1` with the server's address. Append
`--speed 0.5` or `--speed 2` to start a client at a different time scale.

The server synchronizes eight fish hazards and two moving shelves. Clients share
player position, facing, walking/swimming pose, and powered appearance. Pearls,
powers, lives, checkpoints, and victory belong to each player. Network clients
enter the game directly without the title screen.

### Peer-to-peer

No server is required. For two local players, run these in separate terminals.

Terminal 1:

```sh
./build/Release/ashah_game --p2p 6001 127.0.0.1:6002
```

Terminal 2:

```sh
./build/Release/ashah_game --p2p 6002 127.0.0.1:6001
```

Peers exchange player state directly and calculate platform positions from a
shared clock anchor.

## Design decisions

| Decision | Reason |
| --- | --- |
| A vertical dive followed by a return to the surface | Fits the underwater setting and gives the mission a clear sequence: descend, recover, and escape. Returning through the buoy requires navigating the environment in both directions. |
| Three visually distinct regions | Coral, wreck, and abyss backgrounds provide depth cues and landmarks as the camera moves through one continuous world. |
| Shared engine with game-specific components and rules | Reuses the team's graphics, physics, input, timelines, camera, workers, and networking. Level data, gameplay, and visuals are separated into three headers so each can be maintained independently. |
| Different movement on land and in water | Water drag, inertia, buoyancy, and currents make swimming feel different from walking. Feet contact selects walking; a fresh jump releases the diver from a platform. A short dock jump and stronger surface breach serve different purposes. |
| Separate sprite drawing and collision bounds | Walking and swimming artwork share a consistent drawing size and feet anchor without changing the collision body. Pose changes therefore keep platform contact and hazard detection predictable. |
| Patterned platforms and readable hazards | Vertical and circular shelves introduce timing challenges. A pearl follows the rotating shelf, while bright sea urchins make dangerous areas visible against underwater backgrounds. |
| Three lives, checkpoints, and a short power window | Checkpoints preserve pearl progress while deaths still cost a life. Three-second Overdrive creates a limited opportunity to cross hazards; speed keys do not extend its duration. |
| Timelines and joined worker phases | Elapsed-time updates support pause and speed controls. Diver physics runs on the shared worker, with completion awaited before scene mutation or rendering. This keeps ownership simple, at the cost of less parallel overlap. |
| Shared movement with individual mission progress | The server controls dynamic obstacles, or peers use a common clock anchor. Player appearance and movement replicate, while each player keeps their own pearls, lives, and victory state. Multiplayer shares the scene rather than a cooperative inventory. |
| Proportional presentation with a pixel-mode option | The default view keeps the scene composition consistent across window sizes. Pixel mode demonstrates fixed-size rendering, while the HUD remains fitted to the window in either mode. |

## CSC581 milestone coverage

### Milestone 1: Engine foundations

| Requirement | Implementation |
| --- | --- |
| Graphics and game loop | Shared Engine, SDL3 window, renderer, and loop; requested 1920×1080 window. |
| Entity system | Shared GameObjects represent the player, static platforms, and automatically moving fish and shelves. |
| Configurable physics | Shared `Physics::setGravity`, gravity updates, and position integration; game rules add buoyancy and steering. |
| Keyboard input | Shared Input and PlayerInput use keyboard-state polling. |
| Collision responses | Shared Collision detects fish, pickup, and death-zone overlaps; game code handles damage, collection, respawn, and platform landing. |
| Scaling (581) | F toggles the engine's Pixel/Percentage modes; resizing demonstrates each mode. |

### Milestone 2: Time and networking

| Requirement | Implementation |
| --- | --- |
| Timeline representation | Shared Timeline supports elapsed time, adjustable tic sizes, anchoring, pause, and scaling. P and 1/2/3 expose pause and .5/1/2 scales. |
| Client-server | Shared headless Server and windowed NetworkClient; multiplayer verification runs four separate clients and includes a later join. |
| Multithreaded loop | Main thread updates input, world paths, and scene state; a shared FrameWorker updates diver motion and physics. Joined phases protect scene access. |
| Asynchronous clients | Dedicated server threads use per-client synchronous REQ/REP exchanges. Speed keys select approximately 33/17/8 ms send intervals independently. The server controls moving platforms. |
| Peer-to-peer (581) | Shared PeerSession exchanges player state directly and negotiates a clock anchor for moving platforms. |

### Milestone 3: Object model and networked scenes

| Requirement | Implementation |
| --- | --- |
| Extensible object model | Shared component-based GameObjects on clients and server; game-specific components extend the model without a monolithic inheritance hierarchy. |
| Player character | Keyboard-controlled walking, jumping, and swimming, with replicated pose and appearance. |
| Static platforms | Ten platforms with different positions, sizes, and textures. |
| Movement patterns | Vertical and circular moving shelves; fish also follow horizontal, vertical, and circular paths. |
| Spawn points | Three hidden SpawnPoint objects: dock, Shipwreck, and Abyss. |
| Death zone | Hidden abyss boundary detects collision and teleports the diver to the designated spawn point. |
| Scrolling | Shared camera follows descent and ascent. |
| Networked scene | Four clients render replicated players and ten dynamic movers; shared networking handles joins and disconnects. |
| Performance comparison (581) | Team benchmarks compare binary/text formats across three experiments, 18 conditions, and five repeats per condition, with means, variance, and figures. |

The performance framework is in [`Tools/net_experiments.sh`](../../Tools/net_experiments.sh).
Existing measurements and graphs are in [`Docs/Results`](../../Docs/Results/README.md).
Each run measures 5,000 network snapshot iterations after warm-up; these results
measure networking performance, not graphical frame rate.

Individual papers, screenshot appendices, performance analysis, source attribution,
and actual submissions remain separate deliverables.

## Shared engine and game files

[`CMakeLists.txt`](CMakeLists.txt) links the game, server, and gameplay tests to the
team's `engine` target. The game also uses its camera, timelines, movement paths,
spawn/death systems, and networking.

| File | Purpose |
| --- | --- |
| `main.cpp` | Scene setup, controls, networking, and engine integration |
| `SeaLevel.h` | Level definitions and game-specific components |
| `SeaGameplay.h` | Swimming, platform contact, mission rules, and shared physics/collision calls |
| `SeaVisuals.h` | Game artwork helpers and HUD |
| `server.cpp` | Game scene configuration for the shared Server |
| `assets/` | Sprites, backgrounds, platforms, buoy, and title logo |
| `GameplayTest.cpp` | Gameplay, physics, and timeline checks |

**`Sea*.h` files.** They organize game-specific code and use the shared
engine; they do not define a separate engine. Diver physics is explicitly substepped
through engine APIs on the shared worker. Its manual timeline group prevents
integration from running twice.


## Verification

After building, run the four game checks:

```sh
ctest --test-dir build -C Release -R 'ashah_.*_test' --output-on-failure
```

They cover gameplay, physics, timelines, the full nine-pearl route, pause/resume,
scaling, resizing, and worker-thread integration.


## Reference

[primeemre, Update vs FixedUpdate in Unity: Where Movement and Timers Go](https://dev.to/primeemre/update-vs-fixedupdate-in-unity-where-movement-and-timers-go-3icl)
was consulted for input/physics update separation. This game uses the team's C++/SDL
implementation.
