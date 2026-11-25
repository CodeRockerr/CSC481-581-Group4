# Hello Kitty Adventure - Milestone 4

Yeva Mheryan's CSC 481 game (Group 4), built on the team's shared C++/SDL3 engine. This README covers
Milestone 4: events, a networked interaction, and the individual game built on them.

Hello Kitty hops across a long trail of pink clouds, jumps over pits, and avoids Kuromi, who guards the
start of the level. You have three hearts. When a friend falls, you can rescue them: one player's action
changes what happens to another player, and it all travels through the event system.

## Build and run

Run everything from the **repository root**, not from `Games/ymherya`. The game loads its images with paths
like `Games/ymherya/assets/background.png`, so it will not find them from anywhere else.
CMake, Git, and a C++17 compiler are required. SDL3 is included in the repository; CMake downloads ZeroMQ
during the first configure, which needs an internet connection.

```sh
git clone https://github.com/CodeRockerr/CSC481-581-Group4.git
cd CSC481-581-Group4
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build --config Release --parallel 4
./build/Release/ymherya_game
```

Build **everything** as above (not just one target) and wait for it to finish; the first build takes a while.

**Windows only:** if a game or test stops with `libzmq-v145-mt-4_3_5.dll was not found` (or a test exits with code
`0xc0000135`), the ZeroMQ DLL was built but is not next to the executables. Copy it once and run again:

```sh
cp build/_deps/libzmq-build/bin/Release/libzmq-v145-mt-4_3_5.dll build/Release/
```

(For a Debug build the file is `libzmq-v145-mt-gd-4_3_5.dll` in `bin/Debug` and `build/Debug/`.)

## Controls

| Key | Action |
| --- | --- |
| A / D or Left / Right | Walk left and right |
| Space / W / Up | Jump (only while standing on the ground or a cloud) |
| R | Rescue a fallen friend (client-server mode) |
| P | Pause or resume your own timeline |
| 1 / 2 / 3 | Half, normal, and double speed for your own player |
| Tab | Toggle percentage / fixed scale mode |

P and 1/2/3 come from the team's timeline code. They only change your player's timeline, so Kuromi and the
clouds keep running.

## How to play

1. Hello Kitty starts on the ground near the first screen. Kuromi patrols the ground near the start.
2. Jump onto the clouds to stay away from Kuromi. The level is about three screens wide. Nine clouds are
   fixed and three move: one goes up and down, one goes side to side, and one drifts in a circle. Standing on
   a moving cloud carries you with it.
3. Walk toward the right edge of the screen and the camera scrolls with you. Walk back toward the left edge
   and it scrolls back.
4. In the second half of the level the ground has two gaps (pits). Cross them from cloud to cloud. Falling
   in costs a heart and sends you to that pit's checkpoint.
5. Watch your hearts in the top left corner. You lose one when Kuromi touches you, when you fall into a pit,
   or when you fall out of the world. After a hit you get one second of protection, and two seconds when the
   game starts.
6. When you lose your last heart the hearts refill and you start over at the first spawn point.
7. In client-server mode, if another player falls you can rescue them (see below).

The window title and the console print a short story line for each event, with your hearts left.

## Client-server rescue demonstration

Open three terminals in the repository root. Start the server in the first:

```sh
./build/Release/ymherya_server 5555
```

Then start a client in each of two (or more, up to four) other terminals:

```sh
./build/Release/ymherya_game --client 127.0.0.1 5555
./build/Release/ymherya_game --client 127.0.0.1 5555
```

Make one client's Kitty fall (walk into Kuromi or jump into a pit). In the other window:

- the title shows `Kitty N fell! Press R to rescue her.`
- a pink bar appears at the top of the screen and shrinks over six seconds
- the fallen Kitty is tinted red

Press **R** before the bar empties. The fallen player respawns next to the rescuer, flashes yellow, and gets
one heart back (never above three). Every other client's bar disappears, and a second rescuer sees
`Nobody needs rescuing right now.`

In client mode the **server owns** the moving clouds and Kuromi so every window shows them in the same place.
Each client owns its own Hello Kitty and sees the others through object replication. If a client closes, its
Hello Kitty disappears from the other windows and everything else keeps running.

## Milestone 4 scope

I am in CSC 481, so this covers the required parts of Milestone 4: the four events (Input, Collision, Death,
Spawn) driving gameplay, and a networked interaction that relies on the event system. Replay is optional for
CSC 481 and is not implemented in this game.

## What changed in Milestone 4

- Moved movement, hits, deaths and respawns onto the shared event system.
- Added the **cloud rescue**: when a player falls, a teammate can press R to bring them back.
- Added a rescue countdown bar, a red tint on a fallen friend, and red and yellow flashes on Kitty.
- Added `RescueTracker.h` and a unit test for the rescue rules.

### How the four events drive the game

| Event | Producer (only raises the event) | Handler and result |
| --- | --- | --- |
| Input | `InputSystem::poll` raises move and jump events every frame. The game raises a `rescue` Input event when R is pressed. | A handler writes `PlayerIntent`, which `PlayerInput` reads for walking and jumping. The rescue handler sends a Rescue event. |
| Collision | `GameplayEvents::produceCollisions` raises an event the moment Kitty starts overlapping something. | `Rules::onCollision` raises Death when the object is Kuromi. Overlapping a pit's `DeathZone` raises Death inside the shared handler. |
| Death | Collision, `GameplayEvents::produceOutOfBounds` (fell out of the world), or Kuromi. | `Rules::onDeath` removes a heart unless Kitty is in her protection window, refills the hearts after the last one, picks the spawn point, and raises FriendFell. |
| Spawn | Raised by the Death handler with the spawn point name, and by the rescue. | The shared handler moves Kitty to the spawn point and zeroes her velocity. `Rules::onSpawned` resets her grounded state, the camera and the hearts. |

### The networked interaction

| Event | Sent when | Effect on the other clients |
| --- | --- | --- |
| `FriendFell` | A real death (not one during protection) | Others add the fall to their pending list for six seconds and show the bar |
| `Rescue` | A player presses R while a fall is pending | The fallen player decides whether to accept it |
| `RescueAccepted` | The fallen player accepts the first valid rescue | Others clear that fall. The winner sees "You rescued…"; any other rescuer sees "Nobody needs…". |

All three are shareable. `NetEventBridge` hands them to the client, the server orders them and delivers them
to every other client, and receivers enqueue them on the main thread. Snapshots still synchronize
positions. Events say what happened; snapshots say where things are.

Each fall has an id (`owner << 32 | counter`). The fallen player remembers only her open fall and when it
expires (the six-second window plus one second for network delay), and accepts only the first rescue that
matches. A rescue for an old fall, an already-rescued fall, or an expired fall is ignored and logged as
`Ignored a rescue for fall …`. That logic lives in `RescueTracker.h` so it can be tested without the engine.

## Design decisions

| Decision | Choice and reason |
| --- | --- |
| Producers only raise | `InputSystem` and `GameplayEvents` never change the game directly. The handlers do, so the same events can be recorded, replayed or sent over the network later. |
| Rules, not copies | The engine handles the common part of Death and Spawn (moving Kitty, zeroing velocity). The game supplies only its own rules (`onCollision`, `onDeath`, `onSpawned`), so another game can reuse the same chain. |
| Enter-only collisions | A Collision event fires when contact begins. This stops one touch from costing two hearts. If Kuromi is still touching Kitty when her protection ends, the game raises the hit then. |
| Protection after a hit | A short timer after each hit and at the start keeps Kitty from being hit again before she can move. |
| Rescue needs a decision from the fallen player | Only the fallen player's game knows whether she is still waiting. Rescuers can say they tried, but only her `RescueAccepted` says who won, so two rescuers can never both succeed. |
| Only fall and rescue are shared | Death and Spawn stay local. The other players only need to know that someone fell and who rescued whom. |
| Fall ids | A rescue for an old fall can never revive someone who fell again. |
| Long frames are not game time | Dragging or resizing the window freezes the loop on Windows. A frame over 0.25 seconds restores Kitty's last position, raises no events, and does not use up the protection timer or the rescue windows. |
| Feedback with shapes | The engine has no text rendering, so messages go to the window title and console, and the bar and colors show the rescue on screen. |
| Kuromi smoothing | A replicated Kuromi only moves when a snapshot arrives. Clients estimate her speed and ease toward the snapshot so she walks smoothly, and keep her animation frame locally. |
| Standing height | The invisible ground is raised by `KittyFeetLift` so Kitty's feet sit on the visible ground. |

## Code guide

| File | Purpose |
| --- | --- |
| `main.cpp` | Game: scene setup, controls, hearts, event rules, rescue, camera scroll, networking |
| `KittyLevel.h` | Level data: clouds, movers, spawns, pits, network kinds, and the custom event types |
| `RescueTracker.h` | Rescue bookkeeping: `FallTracker`, `PendingRescues`, `RescueAttempts` (no engine dependencies) |
| `RescueTrackerTest.cpp` | Unit test for the rescue rules, including two rescuers |
| `server.cpp` | Headless server; owns the moving clouds and Kuromi |
| `assets/` | Hello Kitty, Kuromi, background, cloud, and heart images |

## How to test Milestone 4

Build with the commands above and run each check from the repository root. For manual checks, launch
`./build/Release/ymherya_game`.

### Manual checks

1. **Input:** walk and jump. Press R with nobody to rescue; the title shows `Nobody needs rescuing right now.`
2. **Collision and Death:** walk into Kuromi. Kitty flashes red, one heart breaks, and the title shows the lives left. Touching Kuromi again during the protection second does nothing.
3. **Spawn:** fall into a pit. Kitty respawns at that pit's checkpoint with no leftover velocity. Lose all three hearts: they refill and you restart.
4. **Rescue:** run the server and two clients. Let client A die. On client B the title shows the fall, the pink bar shrinks, and A's Kitty is red. Press R on B before the bar empties: A respawns next to B, flashes yellow and gets a heart.
5. **Expired window:** let A die and wait seven seconds. Pressing R on B does nothing.
6. **Two rescuers:** add a third client. Let A die and press R on B and C together. A respawns once. One of them sees `You rescued…` and the other `Nobody needs…`.
7. Run the automated checks (see the next section).

### Automated tests

Run these from the repository root after building everything (see Build and run). On Windows, if a test exits with
code `0xc0000135`, copy the ZeroMQ DLL as described in Build and run, then run it again.

All tests (every test program in the project should pass; this branch has 18):

```sh
ctest --test-dir build -C Release --output-on-failure
```

Only the tests for my part of Milestone 4:

```sh
ctest --test-dir build -C Release -R "event_chain_test|ymherya_rescue_test" --output-on-failure
```

Milestone 4 tests only, including the team's event, replay and network event tests:

```sh
ctest --test-dir build -C Release -R "event_manager_test|replay_test|engine_events_test|net_events_test|event_chain_test|ymherya_rescue_test" --output-on-failure
```

Run one test directly (it prints its own result):

```sh
./build/Release/ymherya_rescue_test
./build/Release/event_chain_test
```

`ymherya_rescue_test` prints `rescue_tracker_test: all checks passed`. A successful `ctest` run ends with
`100% tests passed, 0 tests failed`.

With a Visual Studio Debug build, use `-C Debug` and `./build/Debug/...` instead.

What the tests cover:

| Test | What it checks |
| --- | --- |
| `event_chain_test` | Shared Input, Collision, Death and Spawn producers and handlers: movement intent, enter-only collisions, death then spawn, rule overrides, destroyed targets, object references |
| `ymherya_rescue_test` | Rescue rules: first rescue wins, expired and old falls are ignored, a second rescuer is told it was too late, ids never collide |
| `event_manager_test`, `engine_events_test`, `replay_test`, `net_events_test` | The team's event manager, engine dispatch, replay and networked events |
