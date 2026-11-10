# CSC 481/581 Game Engine — Group 4

Game Engine Construction project at NC State. A shared C++17/SDL3 engine supports
three individual games and the graphics, timing, object-model, and networking
features developed across Milestones 1–4. Milestone 4 adds queued gameplay events,
networked interactions, and offline recording/playback through the shared engine.

| Team member | Individual game | Guide |
| --- | --- | --- |
| Adit Jigneshbhai Shah | Lost Under the Sea: Abyss Run | [Game guide](Games/ashah/README.md) |
| Sai Sumedh Kaveti | Cave Ninja | [Game guide](Games/skaveti/README.md) |
| Yeva Mheryan | Hello Kitty Adventure | [Game guide](Games/ymherya/README.md) |

## Build and run

### Requirements

- A C++17 compiler: Xcode Command Line Tools on macOS, GCC/Clang on Linux, or MSVC on Windows.
- CMake 3.16 or newer and Git.
- Internet access for the first configuration, which fetches ZeroMQ 4.3.5.
- Python 3 for performance analysis, with `matplotlib` for graphs. The experiment runner requires Bash.

SDL3 is included in `vendored/SDL`; a separate SDL installation is unnecessary.

From the **repository root**, configure and build all games, servers, and tests:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build --config Release --parallel 4
```

The examples below use macOS/Linux shell syntax and Release binaries in
`build/Release/`. On Windows, executable names end in `.exe`; a Visual Studio
build uses the configuration selected by `--config`.

### Windows runtime DLLs

With Visual Studio, build and run the same configuration. From PowerShell at the
repository root, after a successful Release build, copy the ZeroMQ runtime DLL:

```powershell
Copy-Item .\build\_deps\libzmq-build\bin\Release\libzmq*.dll .\build\Release\ -Force
ctest --test-dir build -C Release --output-on-failure
```

For Debug, build with `--config Debug`, use `bin\Debug` and `build\Debug` in the
copy command, and run CTest with `-C Debug`. Use the actual build directory if it
has another name, such as `build-netcheck`. Executables run as
`.\build\Release\ashah_game.exe`, for example.

If CTest reports **Not Run / executable not found**, build the targets first.
If Windows reports a missing `libzmq*.dll` or exit code `0xc0000135`, repeat the
DLL copy after building. Close running tests or stop debugging before rebuilding
an executable that Windows has locked. These steps address build/runtime setup;
a test that starts and reports failed checks needs separate investigation.

Keep the game assets in the repository and launch from its root. The shared window
class fits requested window dimensions to smaller displays.

## Individual games

### Lost Under the Sea: Abyss Run

```sh
./build/Release/ashah_game
```

Press **Enter or Space** to begin. Jump from the dock, explore the Coral Gardens,
Shipwreck, and Abyss, collect all **nine pearls**, and reach the bottom recovery bed.
One pearl follows the rotating kelp shelf. Then return to the surface and swim
**up through the buoy opening** to win.

The diver walks when its feet touch platforms and swims between them. After landing,
release and press the jump control again to leave the platform. Fish, anglerfish,
and bright sea-urchin clusters are hazards. Damage consumes one of three lives and
returns the diver to the current checkpoint while preserving collected pearls.
Checkpoints do not replenish lives; zero lives ends the run.

Golden cores grant **three seconds of Overdrive**: a yellow suit, increased speed,
and immunity to fish and sea urchins. The abyss death zone still costs a life.

| Control | Action |
| --- | --- |
| Enter or Space | Start from the title screen |
| A / D or Left / Right | Walk or steer |
| Space, W, or Up | Jump; hold to swim upward |
| S or Down | Descend faster or drop through an underwater shelf |
| P | Pause/resume the entire local scene and gameplay timers |
| F | Toggle fixed pixel sizes and proportional scaling |
| 1 / 2 / 3 | Half / normal / double player speed and client loop/send rate |
| R | Restart the dive with lives, pearls, and power pickups reset |
| E | Send a Sonar pulse; client-server teammates nearby receive brief protection |
| F5 / F6 / F7 | Start recording / stop / play the recording, offline only |
| H | Show/hide the six most recent gameplay outcomes |
| Escape | Quit |

Proportional scaling is the default; resize the window after pressing F to compare
modes. The HUD fits the window in both modes. In multiplayer, pausing freezes the
local view while other endpoints continue. Resuming reconciles the shared scene.
Pearls, lives, checkpoints, and victory are individual progress. Yellow suits are
earned through Overdrive, rather than assigned by client ID or peer port.

Milestone 4 routes input, pickups, damage, and respawning through shared events.
Sonar has a four-second cooldown and a 380-unit range; a receiving teammate gains
up to 1.2 seconds of player-time protection. The sender receives no protection
from its own pulse. H shows outcomes such as pearl collection, death, spawn, and
received Sonar; the INPUT counter also counts neutral input updates each frame.
On some keyboards, use Fn with the replay function keys.

### Cave Ninja

```sh
./build/Release/skaveti_game
```

Explore platforms with horizontal, vertical, and circular movement, avoid the
shadow wisp, and reach checkpoints. Hidden death zones return the ninja to the
latest checkpoint; scroll boundaries move the camera through the world.

| Control | Action |
| --- | --- |
| A / D or Left / Right | Move |
| Space, W, or Up | Jump |
| P | Pause/resume player time |
| 1 / 2 / 3 | Half / normal / double player-time scale |
| Tab | Toggle pixel/proportional scaling |
| H | Show/hide spawn points, death zones, and scroll boundaries |
| R / T / Y | Start recording / stop / play the recording, offline only |

Milestone 4 routes input, hazard/checkpoint collisions, death, and spawn through
the shared EventManager. In client-server mode, a death event makes that player's
replica flash red on other clients and increments their `remote deaths` count.
The title shows `REC`, `REPLAY`, or `live` for offline recording and playback.

See the [Cave Ninja guide](Games/skaveti/README.md) for further details.

### Hello Kitty Adventure

```sh
./build/Release/ymherya_game
```

Move and jump between cloud platforms, avoid Kuromi, and use the game's checkpoints
and scrolling environment. A/D or Left/Right move; Space, W, or Up jump. The shared
P and 1/2/3 controls pause and scale player time.

Milestone 4 uses the shared Input, Collision, Death, and Spawn handlers. In
client-server mode, a player's death starts a six-second rescue prompt on the
other clients. Press **R** in another window before its pink bar expires. The
fallen player moves to the rescuer, gains one heart up to the three-heart maximum,
and flashes yellow. A fall identifier allows only one accepted rescue per fall;
all clients clear the prompt after acceptance. Replay is not implemented in this
CSC481 game, where the assignment makes it optional.

See the [Hello Kitty guide](Games/ymherya/README.md) for rescue rules and tests.

## Multiplayer

Run a matching game server and its clients. Each command below starts a separate
process and should run in its **own terminal**. Start the server first. A server has
no SDL window; each client has its own window and locally controlled player.

### Client-server

For Lost Under the Sea, start:

```sh
./build/Release/ashah_server
```

Then start clients, choosing binary or text encoding independently:

```sh
./build/Release/ashah_game --client 127.0.0.1 5555 binary
```

```sh
./build/Release/ashah_game --client 127.0.0.1 5555 text
```

For different computers, replace `127.0.0.1` with the server's address. A custom
server port is a positional argument, for example `ashah_server 5566`; use that
same port in each client command. Ashah clients also accept `--speed 0.5` or
`--speed 2` to select their initial time scale.

Use these matching pairs for the other games:

| Game | Server command | Client command |
| --- | --- | --- |
| Cave Ninja | `./build/Release/skaveti_server` | `./build/Release/skaveti_game --client 127.0.0.1 5555 binary` |
| Hello Kitty Adventure | `./build/Release/ymherya_server` | `./build/Release/ymherya_game --client 127.0.0.1 5555` |

All three clients accept `binary` or `text` as the final format argument; binary
is the default. Run one matching server per session;
servers using the same port cannot run simultaneously.

### Peer-to-peer

All three games accept direct peer sessions without a game server.
For two local Ashah players, run these in separate terminals:

```sh
./build/Release/ashah_game --p2p 6001 127.0.0.1:6002
```

```sh
./build/Release/ashah_game --p2p 6002 127.0.0.1:6001
```

Replace `ashah_game` with `skaveti_game` or `ymherya_game` for the other games.
Each peer binds its own port
and lists the other endpoints. Player state is exchanged directly, while shared
moving objects derive their phase from a negotiated clock anchor. The lower port
initiates each connection.

Use **client-server mode** for the Milestone 4 network-event demonstrations.
The current PeerSession path replicates object state; Sonar, remote-death events,
and rescue messages use NetworkClient and NetEventBridge.

## Shared engine architecture

The component model represents objects as combinations of `Transform`, `Sprite`,
`Collider`, `PhysicsBody`, `PlayerInput`, and optional gameplay/network components.
Objects without a Sprite remain hidden. Game-specific components and rules live
under `Games/`; their headers extend the shared model rather than defining another
engine. The legacy Entity and NetMessage interfaces remain for compatibility.

The engine uses separate player/world timelines and frame workers. Timelines
support anchoring, adjustable tic sizes, pause, and scaling. Rendering uses completed
snapshots; game code coordinates live-object updates with worker completion.

Client-server networking uses ZeroMQ REQ/REP sockets, dedicated client workers,
and a server simulation thread. Server-owned objects provide common obstacle
movement; clients publish their owned player state. Network identity is the pair
`(ownerId, netId)`, mapped to local object IDs at each endpoint. Full snapshots drive
replica creation, updates, and removal. Graceful leaves and silent-client timeouts
remove departed owners. The default server timeout is three seconds.

Binary and text packets carry the same object fields. Binary uses a packed 22-byte
header and 36-byte object records; text uses readable lines with decimal values.
The first byte identifies the encoding, allowing mixed-format clients on one server.
PeerSession provides direct replication and shared clock-anchor negotiation.

Milestone 4 adds an EventManager and ReplayController per Engine. Producers enqueue
value records; main-thread dispatch runs before the game callback, after it, and
after both frame workers finish. Events are ordered by due timestamp, priority,
then insertion order. Frame timing is cached so callbacks and physics can use the
recorded deltas during replay.

InputSystem converts polled actions into Input events and PlayerIntent.
GameplayEvents provides enter-only Collision events, Death rules, named Spawn
handling, and replay capture/restore for contact history. Games supply their own
lives, inventory, checkpoints, and camera rules.

Network events use server-centric routing alongside the existing snapshots.
NetEventBridge forwards Local, shareable events and enqueues received events for
main-thread handling. Per-link sequence numbers, acknowledgments, and retries
suppress retransmitted deliveries. Recipients stamp events with their local event
clock. Movement remains client-simulated; games provide authority checks through
server filters and handlers. Offline references and replay events stay local.

Replay stores a game-provided starting state, pending gameplay events, input, and
frame timing/dispatch phases. Playback regenerates consequences through the same
handlers and blocks live input/network gameplay events. It is local and in memory;
it requires complete state hooks and does not provide disk saves or network-session
replay.

## Milestone implementation map

### Milestone 1 — Engine foundations

| Requirement | Shared implementation |
| --- | --- |
| SDL initialization, window, renderer, and loop | [Engine.cpp](Engine/src/Engine.cpp), [Window.cpp](Engine/src/Window.cpp), [Renderer.cpp](Engine/src/Renderer.cpp) |
| Generic objects and rendering | [EntityManager](Engine/include/EntityManager.h); component-based [GameObjectManager](Engine/include/GameObjectManager.h) added in Milestone 3 |
| Configurable gravity | [Physics](Engine/include/Physics.h), with gravity-enabled PhysicsBody components |
| Keyboard-state input | [Input](Engine/src/Input.cpp), using `SDL_GetKeyboardState` |
| Collision detection | [Collision](Engine/src/Collision.cpp), with component collider bounds |
| Pixel/proportional scaling (581) | [ScaleMode](Engine/include/ScaleMode.h), supported by both object managers |

### Milestone 2 — Time and networking

| Requirement | Shared implementation |
| --- | --- |
| Anchored timelines, tic sizes, pause, and scale | [Timeline](Engine/include/Timeline.h) and [Engine time controls](Engine/src/Engine.cpp) |
| Multithreaded frame loop | [FrameWorker](Engine/src/FrameWorker.cpp) and Engine |
| Headless, asynchronous client-server model | [Server](Engine/src/Server.cpp) and [NetworkClient](Engine/src/NetworkClient.cpp) |
| Independent client speeds and server-owned movement | Separate timelines, server simulation, and configurable client send intervals; Ashah connects speed controls to its send interval |
| Peer-to-peer networking (581) | [PeerSession](Engine/src/PeerSession.cpp) and [PeerClock](Engine/include/PeerClock.h) |

The engine's P key toggles player time; each game decides whether additional world
updates also pause. Keys 1/2/3 select .5/1/2 player-time scales and target loop rates
of approximately 30/60/120 Hz. Actual frame rate depends on workload. Background
network send frequency is separately configurable.

### Milestone 3 — Object model and networked scenes

| Requirement | Implementation and evidence |
| --- | --- |
| Extensible component-based object model | [GameObject](Engine/include/GameObject.h), [Components](Engine/include/Components.h), and GameObjectManager |
| Patterned movement, hidden spawns, death zones, and scroll triggers | [GameplaySystems](Engine/src/GamePlaySystems.cpp), MovementPath, SpawnPoint, DeathZone, and SideScrollTrigger |
| Camera alternative for scrolling | [Camera](Engine/src/Camera.cpp), with bounds, following, and world/screen conversion |
| Runtime model on network endpoints | [NetworkIdentity](Engine/include/NetworkIdentity.h) and [NetReplicator](Engine/src/NetReplicator.cpp) |
| Four-client scene, player propagation, and disconnect handling | Server/NetworkClient; [NetSyncTest](Tests/NetSyncTest.cpp) covers four clients, both formats, latency, leaves, timeouts, and peers |
| Distinct data formats (581) | Binary/text encoders and decoders in [NetProtocol](Engine/src/NetProtocol.cpp) |
| Three performance experiments and repeated measurements (581) | [Experiment runner](Tools/net_experiments.sh), [benchmark client](Tests/NetBenchClient.cpp), and [analysis script](Tools/net_analyze.py) |

The individual-game checklist requires a player, at least three distinct static
platforms, at least two movement patterns, multiple hidden spawn points, a hidden
death zone, and scrolling through a boundary or camera. The shared systems support
these features; individual integration and evidence must be checked per game.

### Milestone 4 — Events, networked gameplay, and replay

| Requirement | Shared implementation |
| --- | --- |
| Event representation | [Event.h](Engine/include/Event.h): typed value payloads, IDs, object references, timestamps, priorities, origins, and custom EventFields |
| Event registration | [EventManager](Engine/include/EventManager.h): type-specific/all-event subscriptions and explicit unsubscribe tokens |
| Event raising | [EventManager.cpp](Engine/src/EventManager.cpp): thread-safe enqueueing; raising does not invoke handlers |
| Event handling | Timestamp/priority/FIFO queue, one non-recursive consumer, and a default 1,024-event budget per drain |
| Timeline timestamps and engine integration | [Engine.cpp](Engine/src/Engine.cpp): frame-stable simulation time and Input/Update/Workers dispatch phases |
| Input, Collision, Death, and Spawn | [InputSystem](Engine/src/InputSystem.cpp), [PlayerIntent](Engine/include/PlayerIntent.h), and [GameplayEvents](Engine/src/GamePlayEvents.cpp) |
| Network event sharing | [NetEventBridge](Engine/src/NetEventBridge.cpp), [EventCodec](Engine/src/EventCodec.cpp), NetworkClient, Server, and the event trailer in NetProtocol |
| Object identity and lifecycle ownership | [EventObjects](Engine/include/EventObjects.h) resolves offline/network references; snapshots create replicas and actor owners decide Death-to-Spawn consequences |
| Recording and playback (required for CSC581) | [ReplayController](Engine/src/ReplayController.cpp): ReplayStart/ReplayStop/ReplayPlayback events, state hooks, recorded timing/input phases, and playback isolation |

The four event decisions favor explicit ownership and safe update boundaries.
Value records avoid queued object pointers; subscription tokens require lifetime
management; enqueue-only raising delays effects until dispatch; main-thread
handling simplifies object access but slow handlers can delay a frame. The server
centralizes event routing at the cost of a server hop and a single routing point.
Delivery acknowledgments cover transport receipt, not durable completion of a game
action. Replay hooks keep the engine general but each game must save all state
that can affect playback.

| Individual game | Four required events | Networked interaction | Replay |
| --- | --- | --- | --- |
| Lost Under the Sea (CSC581) | Movement intent, pickups/hazards, life loss, checkpoint respawn | Sonar pulse protects a nearby teammate | F5 / F6 / F7, offline |
| Cave Ninja (CSC581) | Movement intent, hazard/checkpoint contacts, death, checkpoint respawn | Death notification flashes the remote ninja and updates the count | R / T / Y, offline |
| Hello Kitty Adventure (CSC481) | Movement intent, Kuromi/pit contacts, heart loss, named respawn | FriendFell, Rescue, and RescueAccepted coordinate a single rescue per fall | Not implemented; optional for CSC481 in the assignment |

Team design documents:

- [Object model](Docs/Design/Design_ObjectModel.md)
- [Gameplay systems](Docs/Design/Design_GamePlaySystems.md) and [usage guide](Docs/Design/GamePlaySystemsUsage.md)
- [Milestone 3 networking](Docs/Design/Design_Networking_M3.md)
- [Timeline](Docs/Design/Design_Timeline.md), [client threads](Docs/Design/Design_ClientThreads.md), and [peer networking](Docs/Design/Design_P2P.md)
- [Milestone 4 events and replay](Docs/Design/Design_Events.md), [gameplay events](Docs/Design/GamePlayEvents.md), and [networked events](Docs/Design/Design_NetEvents.md)

## Manual Milestone 4 verification

1. **Local events:** In each game, move/jump, touch a hazard, and observe death and
   checkpoint respawn. Ashah's H panel shows outcomes and its final life ends the
   dive. Kitty loses a heart, then refills and restarts after its last heart.
2. **Ashah network event:** Start `ashah_server` and two matching clients. Keep the
   divers within 380 world units and press E in one window. The other shows a
   received Sonar event/SONAR RX increase and brief protection against fish/urchins.
3. **Cave Ninja network event:** Start `skaveti_server` and two matching clients.
   Let one ninja die. In the other window, check `remote deaths` in the title and
   the red flash on the replica when it is visible.
4. **Kitty network event:** Start `ymherya_server` and two matching clients. Let A
   lose a heart, then press R on B while its rescue bar is visible. A should spawn
   beside B and gain one heart. With a third client, simultaneous rescues must
   still restore only one heart. Old/expired falls must not accept a rescue.
5. **CSC581 replay:** Run Ashah offline, start a dive, press F5, play, press F6,
   wait for LIVE, then press F7. For Cave Ninja offline, use R, T, and Y. Replay
   should reproduce recorded movement/consequences and ignore live movement keys.
   Repeat playback to check consistency. Neither game enables recording in a
   network session.

Close one multiplayer session before starting another game server. Each process
needs its own terminal. The default dedicated client ports are shared between
servers, so changing only the join port does not isolate simultaneous sessions.

## Tests

Use the in-repository build folders shown here. The asset-based CTest checks
assume that layout; an external build needs those tests' working directory set to
the repository root. After the Release build above:

```sh
ctest --test-dir build -C Release --output-on-failure
```

To run the five shared Milestone 4 checks:

```sh
ctest --test-dir build -C Release -R "^(event_manager|event_chain|engine_events|replay|net_events)_test$" --output-on-failure
```

To run the individual-game event/replay/network regressions:

```sh
ctest --test-dir build -C Release -R "^(ashah_(m4|sonar_binary|sonar_text)|ymherya_rescue)_test$" --output-on-failure
```

To run all seven Ashah checks, including gameplay, the complete route, pause,
foundations, events/replay, and both Sonar formats:

```sh
ctest --test-dir build -C Release -R 'ashah_.*_test' --output-on-failure
```

The current suite registers 18 tests. `event_manager_test`, `event_chain_test`,
`engine_events_test`, `replay_test`, and `net_events_test` cover the shared M4
implementation. `ymherya_rescue_test` checks first-rescue-wins, old/expired falls,
and winner feedback; it is a rule test, not an end-to-end multiplayer test.
Cave Ninja currently has no dedicated game regression target, so use its manual
event/network/replay checks in addition to the shared tests.

The shared object-model, object-system, camera, and gameplay-system tests use C++
`assert`. Release builds disable these assertions, so also run them in Debug:

```sh
cmake -S . -B build-debug -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build-debug --config Debug --parallel 4
ctest --test-dir build-debug -C Debug -R "^(object_model|object_systems|camera|gameplay_systems)_test$" --output-on-failure
```

On Windows, copy the Debug ZeroMQ DLL into the selected Debug binary directory
before running these tests, as described above. Run network tests serially and
close existing game servers first; the tests start their own loopback servers.
If a test fails, rerun it with `ctest --test-dir build -C Release -R "test_name" -V`.

## Current limitations and submission checklist

- Cave Ninja's animation reads `playerTime.getLastDelta()` in
  [main.cpp](Games/skaveti/main.cpp). During playback at a different frame rate,
  its animation timing can differ even when position and death count reproduce.
  It should use `engine.getFrameDelta(Engine::PlayerTime)` for recorded timing.
- Replay is offline, in memory, and intended for the same build/process. Live
  network-session replay, persistent replay files, and cross-version determinism
  are outside the current implementation.
- Windows builds currently need the ZeroMQ runtime DLL copy described above.
- Use Release for the complete gameplay suite. The full Ashah route can exceed
  its 90-second CTest limit with an unoptimized Debug/software-rendering build;
  the Debug command above checks the older assertion-based shared tests.
- Hello Kitty does not implement replay; it is optional for CSC481 in the assignment.

The source code and this guide are only part of the submission. Each individual
write-up must include engine design/rationale, the game's event and network
mechanics, critical reflection and limitations, and an appendix of screenshots
referenced in the text. Provide attribution for consulted sources and assets.
Confirm the team engine source, each individual game, and the individual reports
are submitted to the correct assignment entries; repository presence alone does
not submit them.

## CSC581 performance analysis

The supplied benchmark compares binary and text object snapshots with the same
client-server architecture. It measures headless network-loop elapsed time and
snapshot sizes. Results describe networking throughput rather than graphical FPS.

| Experiment | Variable | Fixed conditions |
| --- | --- | --- |
| E1 | 10 / 100 / 500 server-owned moving objects | One client |
| E2 | 1 / 2 / 4 connected clients | 100 server-owned moving objects |
| E3 | 1 / 100 / 500 owned objects per client | Two clients and ten server objects |

Each format/condition has five repetitions, 200 warm-up iterations, and 5,000
measured iterations per client. The runner starts a fresh server for each case and
alternates format order between repetitions: 18 conditions and 90 runs in total.

### Collect new measurements

Close running game servers first; the runner manages its own server and clients.
The Release build above includes the required binaries. To build only those targets:

```sh
cmake --build build --config Release --target engine_server net_bench_client --parallel 4
```

Write a separate run folder to preserve the existing measurements:

```sh
bash Tools/net_experiments.sh build/Release Docs/Results/new_run/net_results.csv 5000 5
python3 Tools/net_analyze.py Docs/Results/new_run/net_results.csv
```

Arguments are binary directory, output CSV, measured iteration count, and repetitions.
The default port is 5555; `PORT=5566 bash Tools/net_experiments.sh ...` selects another
join port. The runner still uses the shared server's dedicated per-client ports.
The full experiment takes several minutes.

### Interpret the output

| Output | Purpose |
| --- | --- |
| `net_results.csv` | One raw measurement row per client per run |
| `summary.csv` / `summary.md` | Mean elapsed time, sample variance, standard deviation, and average bytes per snapshot |
| `figure1_E1.png` | Moving-object comparison |
| `figure2_E2.png` | Connected-client comparison |
| `figure3_E3.png` | Client-owned-object comparison |

For multi-client cases, client times are averaged within each run before calculating
statistics across five runs. Graph error bars show one sample standard deviation.
A lower mean time indicates faster completion of the same iteration count; variance
describes repeat-to-repeat variation. Text mean divided by binary mean gives the
text/binary time ratio.

Existing [results and methodology](Docs/Results/README.md), [summary tables](Docs/Results/summary.md),
and [raw measurements](Docs/Results/net_results.csv) are included. Binary had lower
mean elapsed time in all recorded conditions, though it did not always have lower
variance. Measurements used a local Apple Silicon MacBook Air, macOS, Release
binaries, and loopback TCP. They do not establish WAN or rendering performance.

For the individual writeup, explain the methodology, compare the measured means and
variances, discuss limitations, and reference the three performance figures by number
in the appendix. Individual papers, screenshot appendices, asset/source credits,
and actual submissions remain separate deliverables.

## Repository layout

```text
Engine/include/   Shared engine interfaces and components
Engine/src/       Graphics, physics, timing, objects, events, replay, and networking
Games/ashah/      Lost Under the Sea: Abyss Run
Games/skaveti/    Cave Ninja
Games/ymherya/    Hello Kitty Adventure
Tests/            Shared tests and the headless networking benchmark client
Tools/            Performance experiment runner and analysis
Docs/Design/      Architecture and gameplay design documents
Docs/Results/     Benchmark data, summaries, and figures
Docs/Milestones/  Existing team engine documents
Docs/Team/        Existing individual submission documents
ServerMain.cpp    Generic engine_server entry point
vendored/SDL/     Included SDL3 source
```

The generic server runs the legacy single-platform scene without options. For a
benchmark scene, specify its moving-object count:

```sh
./build/Release/engine_server --port 5555 --objects 100
```
