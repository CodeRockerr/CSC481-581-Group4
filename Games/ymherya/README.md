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
cmake -S . -B build
cmake --build build
```

Then start the game offline:

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
On some setups the executables end up in `build/Debug/` (Windows with Visual Studio) or directly in `build/`. Use whichever folder contains `ymherya_game`.

The game view is 1280 x 800 in game units and scales with the window when you resize it. Hello Kitty and Kuromi stay inside the visible area at any window size.

## How to play

1. Hello Kitty starts on the ground near the middle of the first screen. Kuromi starts at the far left and patrols the width of the screen.
2. Jump onto the clouds to stay away from Kuromi. The level is about three screens wide. Nine clouds are fixed and three move: one goes up and down, one goes side to side, and one drifts in a circle. Standing on a moving cloud carries you with it.
3. Walk toward the right edge of the screen and the level scrolls with you. Walk back toward the left edge and it scrolls back, so you can return to earlier parts.
4. In the second half of the level the ground has two gaps (pits). Cross them by jumping from cloud to cloud. If you fall in, you lose a heart and you are sent to a spawn point, and the level scrolls so that spot is on screen.
5. Watch your hearts in the top left corner. You lose one when Kuromi touches you or when you fall into a pit. You get one second of protection after you respawn, and two seconds when the game starts.
6. When you lose your last heart the hearts refill and you start over at the first spawn point.

The window title and the console print a short story line for each event (landing on a new cloud, getting caught, falling into a pit), together with how many hearts you have left.

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
Start the server in one terminal:

```sh
./build/Release/ymherya_server 5555
```

Start each client in its own terminal (up to four):

```sh
./build/Release/ymherya_game --client 127.0.0.1 5555
```

To connect from another computer, replace `127.0.0.1` with the server's address. Every client draws the other players, and positions are sent in world coordinates so clients that have scrolled to different parts of the level still agree on where everyone is. If a client closes, its Hello Kitty disappears from the other windows and everything else keeps running.

In client mode the server controls Kuromi's position, so every window shows her in the same place, and the client maps the server's value onto the width of the first screen. The clouds move from each client's own world timeline, so they are not sent over the network. The peer-to-peer mode from the team engine is not used by this game, which only supports `--client`.

## Milestone coverage

I am in CSC 481, so this covers the required parts of each milestone. The optional parts (scaling toggle, peer-to-peer, and the performance comparison) are noted where they apply.

### Milestone 1: Engine foundations

| Task | How the game uses it |
| --- | --- |
| Task 1: Core graphics | `main` creates the shared `Engine` and calls `engine.run`. The team engine opens the window and renderer and runs the game loop. |
| Task 2: Entities | Static objects: the clouds, the background, and the hearts. Controllable object: Hello Kitty. Auto-moving objects: Kuromi and the moving clouds. |
| Task 3: Physics | Hello Kitty has gravity turned on. The clouds, Kuromi, the hearts, and the background have it turned off. The strength of gravity is the engine's setting. The game only sets the jump speed. |
| Task 4: Input | The game uses the engine's `Input::isKeyPressed` for walking and `Input::isKeyJustPressed` for jumping. The game code does not read SDL events. |
| Task 5: Collision | Hello Kitty and Kuromi are checked with the engine's `Collision::checkCollision` (using smaller boxes than the sprites). A hit costs a heart and respawns Hello Kitty. Landing on a cloud stops her fall, and falling into a pit respawns her at a spawn point. |
| Task 6: Scaling (optional for 481) | The game uses a 1280 x 800 reference size in proportional (percentage) mode, so the scene scales when the window is resized. I did not add a key to switch to pixel mode. |

### Milestone 2: Time and networking

| Section | How the game uses it |
| --- | --- |
| Section 1: Timelines | Hello Kitty moves on `PlayerTime`. Kuromi, the clouds, the background, the hearts, and the remote players run on `WorldTime`. Movement uses the elapsed time of each timeline. P pauses and resumes my timeline, and 1/2/3 switch it to 0.5x, 1x, and 2x. Kuromi and the clouds keep moving when I pause or slow down. |
| Section 2: Client-server | `ymherya_server` runs headless, and every client is its own process with its own window and one Hello Kitty. Each client sends its player state and draws the other players it receives. |
| Section 3: Multithreaded loop | The game gives the engine a game update and a world update function. The `NetworkClient` runs on its own thread, so the game locks an entity's mutex before it applies network data, and a mutex-protected queue holds new remote players until the main thread can create them. |
| Section 4: Asynchronicity | Kuromi is controlled by the server, so she is in the same place for every client. Hello Kitty runs on the local player timeline, so slowing down or pausing one client does not change the others. Old messages (by timeline tic) are ignored so a late packet cannot move a player backwards. |
| Section 5: Peer-to-peer (optional for 481) | Not implemented in this game. |

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
| Player character | Hello Kitty walks and jumps with the keyboard, and other clients' players are drawn as remote copies. |
| Static platforms | Nine fixed clouds that differ in position and size, using the big and small cloud textures. |
| Moving platforms | Three moving clouds, each with a `MovementPath` component: vertical, horizontal, and circular. Kuromi also uses a horizontal `MovementPath` when the game runs offline. |
| Spawn points | Three hidden `SpawnPoint` objects named `start`, `middle`, and `upper`. They have no texture and are never drawn. `start` is the first screen, `middle` is on the ground in the second part of the level, and `upper` is on top of a cloud in the third part. |
| Death zones | Three hidden `DeathZone` objects. One follows Kuromi and sends you to `start`. The other two are the pits: the first sends you to `middle` and the second sends you to `upper`. The floor does not exist over the pits, so Hello Kitty falls into the zone. The shared `GameplaySystems::updateDeathZones` does the teleport. |
| Side scrolling | Two hidden boundary objects, one covering the left quarter of the screen and one covering the right 35%. When Hello Kitty overlaps one, the whole scene is translated sideways, so clouds, pits, spawn points, and the background move into and out of view. The level is 4000 units wide. |
| Object model | Spawn points, death zones, scroll boundaries, and movement paths are `GameObject`s built from components (`Transform`, `Collider`, `SpawnPoint`, `DeathZone`, `MovementPath`, `SideScrollTrigger`). |

The visible parts of the game (Hello Kitty, Kuromi, the clouds, the background, and the hearts) are still the older `Entity` objects from Milestone 1. Hello Kitty has an invisible `GameObject` that follows her, so the shared death zone and scroll systems can see her, and the game translates the visible entities itself when the level scrolls.

## Design decisions

| Decision | Reason |
| --- | --- |
| Scrolling in both directions | The first version scrolled once. Two boundaries let the level be longer than one screen and let the player go back, and the background is two tiles that wrap around so it never runs out. |
| Pits as gaps in the floor | The ground is solid except over the two pits, so the death zone is something the player actually falls into instead of an invisible wall. The clouds above the pits give a way across. |
| Respawn scrolls the level | A spawn point can be far from where Hello Kitty died. Respawning scrolls the level so that the spawn point is on screen, and the hidden objects shift with it. |
| Smaller collision boxes for Kuromi | The sprite images have transparent space around them. With full-size boxes, standing on a cloud while Kuromi walked underneath counted as a hit. |
| One function that removes a life | Every hit goes through the same place. This stops one touch from costing two hearts and makes the hearts refill correctly after a game over. |
| Protection after a respawn | A short timer after each respawn, and at the start, keeps Hello Kitty from being hit again before she can move. |
| Landing checks where Kitty's feet are | The cloud landing test compares her feet with each cloud's top, including where they were last frame, so she still lands after one long frame such as while resizing the window. |
| Moving clouds from a `MovementPath` | The same system that moves Kuromi moves the clouds. The game applies each cloud's movement each frame and carries Hello Kitty if she stands on it, so scrolling and the path never fight each other. |
| Kuromi's range follows the window width | Her position is scaled every frame to the visible width so she never leaves the window when it is resized. She lives in the world, so she scrolls away as you move right. |

## Files

| File | Purpose |
| --- | --- |
| `main.cpp` | Game: scene setup, controls, hearts, death zones, camera scroll, networking |
| `KittyLevel.h` | Level data: clouds, movers, spawns, pits, net kinds |
| `server.cpp` | Headless server; owns moving clouds and Kuromi |
| `ymherya_game.cpp` | The game: scene setup, controls, hearts, death zones, scrolling, and networking |
| `ymherya_server` | The server for this game, which owns Kuromi's movement |
| `assets/` | Hello Kitty, Kuromi, background, cloud, and heart images |

The shared engine, object model, and networking code are in the team's `Engine/` folder.
