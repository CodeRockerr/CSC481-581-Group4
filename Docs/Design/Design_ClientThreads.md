# Project 2 engine design

## Client frame threads

Files: `Engine/include/FrameWorker.h`, `Engine/src/FrameWorker.cpp`, `Engine/src/Engine.cpp`.

The client builds the next frame on two worker threads while the main thread draws the last finished frame. Both workers are created once in `Engine::run` and reused every frame.

| Thread | Work each frame |
|---|---|
| Main | Poll events, `Input::update`, the `P` / `1` / `2` / `3` keys, the game callback (keyboard velocity, sending the local player, creating new remote players), then draw the last finished frame and present it |
| Player worker | Step `PlayerTime`, apply gravity and movement to entities on `PlayerTime` |
| World worker | Step `WorldTime` (and any other local timeline), apply gravity and movement to those entities, then run the world callback. The demo applies the latest `NetworkClient` snapshot to the platform and remote players there |

## Frame order

1. Main thread handles input and runs the game callback. The workers are idle, so the main thread can create entities here.
2. Main thread starts both workers.
3. While they run, the main thread clears the screen and draws `finishedFrame`, a copy of every entity taken at the end of the previous frame.
4. Main thread waits for both workers, presents, then copies the entities into `finishedFrame` for the next frame.

No SDL call runs on a worker. Rendering reads only the copy, never the live entities.

## Shared data

- Each `Entity` has its own mutex (`stateMutex`) that guards position and velocity. Workers lock it while integrating, the world callback locks it while applying the snapshot, and the copy for drawing locks it while reading. `EntityMutex` wraps `std::mutex` so `Entity` can still be copied.
- `Timeline` already locks itself.
- Workers never add or remove entities, because the other worker may be looping over the list. A remote player seen for the first time is queued by the world worker and created by the main thread on the next frame.

## Demo

`engine_demo` with no arguments runs the offline timeline demo. `engine_demo --client <host> <port>` connects to `engine_server`. The window title shows how many frames each worker has built; the two numbers match.
