# Lost Under the Sea: Abyss Run

Adit Shah's CSC581 game, built on the team's shared C++/SDL3 engine.

Jump from the dock, collect nine pearls across three underwater regions, reach the
recovery bed, and return up through the buoy opening. Walk on platforms, swim
between them, avoid fish and bright urchins, and recover at checkpoints after a hit.

## Build and run

Run from the repository root. CMake, Git, and a C++17 compiler are required.
SDL3 is included in the repository; CMake downloads ZeroMQ during the initial
configuration, which needs an internet connection.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build --config Release --target ashah_game ashah_server ashah_gameplay_test ashah_m4_test --parallel 4
./build/Release/ashah_game
```

Keep `Games/ashah/assets/` in place and run the game from the repository root.
Use `build/Release/ashah_game` after rebuilding so you launch the current version.
On keyboards with media keys, use Fn with F5/F6/F7 if needed.

## Controls

| Key | Action |
| --- | --- |
| Enter / Space | Start from the title screen |
| A / D or Left / Right | Walk or steer |
| Space / W / Up | Fresh press jumps; hold to swim upward |
| S / Down | Dive or drop through underwater shelves |
| E | Sonar pulse; four-second cooldown |
| H | Show or hide the recent event history |
| P | Pause/resume the local world |
| F | Toggle pixel/proportional view when replay is idle |
| 1 / 2 / 3 | Half / normal / double player time scale |
| R | Restart through Input and Spawn events |
| F5 / F6 / F7 | Record / stop / play an offline replay |
| Escape | Quit |

Start with three lives. Unprotected fish/urchin contact costs one life. If lives
remain, return to your checkpoint with collected pearls kept. Checkpoints never
restore lives. Golden cores grant three seconds of Overdrive while unpaused,
independent of the time-scale setting: increased speed and hazard immunity.
The abyss still costs a life. At zero lives, press R to start a new dive.
To win, collect all nine pearls, including the circular-shelf pearl, visit the
recovery bed at the bottom, and return through the buoy opening from below.
Victory freezes the scene.

Press H to see the six most recent outcomes, newest first. Each line names what
happened and its event type, such as a collected pearl, life lost, checkpoint spawn,
or received sonar. Movement key presses stay out of the list; Input events still
drive movement and appear in the counters. H also works while paused or
watching a replay; it changes only the display. INPUT counts action updates each
frame, so it also increases while idle.

## Client-server sonar demonstration

Open three terminals in the repository root. Start the server in the first:

```sh
./build/Release/ashah_server
```

Wait for `Server listening`, then launch one client in each of two other terminals.

```sh
./build/Release/ashah_game --client 127.0.0.1 5555 binary
```

```sh
./build/Release/ashah_game --client 127.0.0.1 5555 text
```

Network clients start directly on the dock; Enter is only needed for the offline
title screen. Keep the divers close together on the dock and press E in one client.
The receiving client should show `SONAR RX` increasing and a Sonar assist message.

Sonar has a range of 380 world units. Its Input handler raises a shareable Sonar
event. The server forwards it to the other client's event
queue. The receiving handler displays a pulse, highlights nearby pearls, increments
`SONAR RX`, and grants its local diver 1.2 player-time seconds of protection.
The sender receives no protection from its own pulse. Sonar does not restore lives.

Snapshots still synchronize players and dynamic obstacles. Each player keeps their
own inventory, lives, and mission. Sonar adds cooperation without duplicating pearls.

Close both clients with Escape and stop the server with Ctrl+C before trying P2P.
Run these commands in two terminals; no separate server is needed:

```sh
./build/Release/ashah_game --p2p 6001 127.0.0.1:6002
./build/Release/ashah_game --p2p 6002 127.0.0.1:6001
```

Both peers should show the other diver. P2P synchronizes movement; Sonar stays
local in this mode. Use client-server mode for the Milestone 4 network event test.

## Offline replay

Run without `--client` or `--p2p`. Start a dive, press F5, play, press F6, then F7.
The HUD shows LIVE, RECORDING, or PLAYBACK. Wait for LIVE after F6 before pressing
F7. Escape and F6 remain available during playback; live movement, restart, and
Sonar input are ignored.

The shared controller records input and frame timing. Game hooks capture and
restore progress, motion, object state, contact history, timers, camera, sonar,
and movement-path phase. Normal gameplay regenerates collisions, deaths, and
spawns. Playback finishes at the clip's final state and resumes live play.

Replay is local, in memory, and within the same build/process. The scene must keep
its object IDs. Texture handles reference resources owned by that process; this
is not a portable save-file format. Window resizing and controller-status text
are presentation controls, not recorded gameplay actions.

## Milestone coverage

| Milestone | Game implementation |
| --- | --- |
| 1 | Shared renderer, loop, objects, input, configurable physics, collisions, scaling |
| 2 | Timelines, pause/speed, joined motion worker, client-server snapshots, peer movement |
| 3 | Component model, static/moving platforms, circular paths, hidden spawn/death objects, scrolling, replication |
| 4 | Input drives movement intent; Collision collects pearls/powers and requests damage; Death applies life/checkpoint rules; Spawn moves the diver and resets velocity; networked Sonar assists teammates; F5/F6/F7 drive shared replay |

## Milestone 4 additions

- Connected movement, pickups, life loss, and respawning to the shared event system.
- Added networked Sonar: one player's event briefly protects a nearby teammate.
- Added offline recording and playback through the shared replay controller, as required for CSC581.
- Added H to display recent gameplay outcomes and counters to demonstrate event handling.

## Design decisions

| Decision | Choice and reason |
| --- | --- |
| Representation | Shared event records hold a type, Timeline timestamp, priority, object references, and payload. The same format supports engine and game events. |
| Registration | Subscribe by event type and unsubscribe when destroyed, so callbacks cannot outlive their game system. |
| Raising | Enqueue events instead of calling handlers directly, so producers do not change gameplay immediately. |
| Handling | Dispatch on the main thread to keep gameplay changes outside worker execution. Order due events by timestamp, priority, then insertion order. |
| Networking | Use server-centric routing to reuse the existing client-server connection and centralize event ordering. Snapshots move replicas; Sonar events trigger teammate protection. |
| Replay | Save the starting state and replay input with recorded timing. Regenerate collisions, deaths, and spawns through normal handlers to avoid applying effects twice. |
| Fish contact | Shared rectangular checks find possible contacts. Visible sprite pixels confirm fish damage, so transparent corners are safe. |
| Event history | Keep six gameplay outcomes without movement key messages. Replay restores the history; H stays a viewer preference. |

The game keeps its own swimming, pearl, life, and mission rules. The shared engine
provides the loop, objects, physics, event manager, networking, and replay controller.
The `.h` files organize game code and do not create a separate engine.

## Code guide

| File | Purpose |
| --- | --- |
| `main.cpp` | Assets, scene, frame loop, network/replay integration, HUD |
| `SeaEvents.h` | Game handlers, pickups, death rules, sonar, counters, event history |
| `SeaReplay.h` | Value snapshots and component restoration |
| `SeaLevel.h`, `SeaGameplay.h`, `SeaVisuals.h` | Existing world, movement, and artwork helpers |
| `server.cpp` | Existing scene for the shared server |
| `GameplayTest.cpp` | Existing movement/mission tests |
| `Milestone4Test.cpp` | Events, protection, repeated replay, network sonar tests |

## How to test Milestones 1-4

Build using the commands above and run each test from the repository root.
For manual checks, launch `./build/Release/ashah_game` and press Enter.

### Milestone 1: Engine foundations

1. Move with A/D and jump from the dock with Space. Movement and gravity should work smoothly.
2. Land on a platform, walk across it, and leave its edge. The diver should stand on the platform and swim when underwater.
3. Resize the window and press F. Pixel mode should keep sprite sizes; proportional mode should scale the scene.
4. Run the automated physics, movement, and display checks:

```sh
ctest --test-dir build -C Release -R '^ashah_(gameplay|foundations)_test$' --output-on-failure
```

### Milestone 2: Time and networking

1. Press P. The diver, fish, platforms, camera, and game timers should freeze. Press P again to resume.
2. Try 1, 2, and 3 while moving or swimming. Player time should change between half, normal, and double speed.
3. Follow the client-server commands above in separate terminals. Both clients should see the other diver and the shared moving obstacles.
4. Close both clients and stop the server, then try the P2P commands above. Each peer should see the other diver move.
5. Run the automated pause, time-scale, and motion-worker checks:

```sh
ctest --test-dir build -C Release -R '^ashah_(pause|foundations)_test$' --output-on-failure
```

In network mode, P pauses the local view; the server and other clients continue.

### Milestone 3: Game object model

1. Walk on static platforms and ride moving shelves, including the circular shelf. The platforms should support and carry the diver.
2. Collect pearls, including the circular-shelf pearl. Each collected pearl should disappear and update the total as the camera follows the dive.
3. Reach a checkpoint, then touch a fish or urchin without protection. Lose one life and respawn at the checkpoint with recovered pearls kept.
4. Collect a golden core. Fish and urchins should not cause damage during the three-second Overdrive; the abyss still costs a life.
5. Collect all nine pearls, reach the bottom, and swim back through the buoy opening. Victory should stop the scene.
6. Run the automated object, platform, respawn, and complete-route checks:

```sh
ctest --test-dir build -C Release -R '^ashah_(gameplay|route)_test$' --output-on-failure
```

### Milestone 4: Events, networked gameplay, and replay

1. Move or swim and watch the INPUT counter increase. Press H: history should show gameplay outcomes rather than movement key presses.
2. Collect a pearl or core, then touch a hazard after protection expires. History should show Collision, Death, and Spawn outcomes. A death costs one life; the final life ends the dive without another spawn.
3. Run two client-server games using the commands above. Keep the divers nearby and press E in one client. The other client's SONAR RX should increase, history should show the received event, and brief protection should prevent hazard damage.
4. In an offline dive, press F5, move and collect a pearl or lose a life, then press F6 and F7. Playback should repeat the recorded progress and ignore live movement keys. Play the clip twice to check repeatability.
5. Toggle H during pause and replay. It should change only the panel's visibility.
6. Run the automated event, history, replay, and binary/text Sonar checks:

```sh
ctest --test-dir build -C Release -R '^ashah_(m4|sonar_binary|sonar_text)_test$' --output-on-failure
```

To run all seven game checks together:

```sh
ctest --test-dir build -C Release -R '^ashah_.*_test$' --output-on-failure
```

All checks should pass. These tests verify the game features; the individual
write-up and screenshot appendix are separate submission deliverables.

## References and attribution

The game uses the team's shared engine and the artwork in `Games/ashah/assets/`.

- [A history the player can read: what an event log should record](https://dev.to/idlecultivation/a-history-the-player-can-read-what-an-event-log-should-record-37od): readable outcomes and fewer repeated entries.
- [Testing a deterministic browser game](https://dev.to/yulingg_zhang_dfbce7a345a/testing-a-deterministic-browser-game-seeds-replay-and-invalid-state-12ob): input history and repeatability checks.
- [Update vs FixedUpdate](https://dev.to/primeemre/update-vs-fixedupdate-in-unity-where-movement-and-timers-go-3icl): retained reference from the existing movement design.
