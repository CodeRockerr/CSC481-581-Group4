# Project 2 engine design

## Time

Files: `Engine/include/Timeline.h`, `Engine/src/Timeline.cpp`, `Engine/src/Engine.cpp`.

One global timeline reads system time with `SDL_GetTicksNS`. It is never paused and its scale stays 1.0; `pause`, `unpause` and `setScale` do nothing on it.

Every other timeline is a local timeline anchored to the global timeline or to another local timeline. A local timeline has:

| Field | Meaning |
|---|---|
| anchor | The timeline it reads time from |
| tic size | Nanoseconds of timeline time per tic. `getTime()` returns tics. Default 1,000,000 (1 ms) |
| scale | How fast it runs relative to its anchor: 0.5, 1.0, 2.0 |
| paused | While paused, no time is added |

Each step, a local timeline reads its anchor and adds `(anchor now - anchor last) × scale` to its own time, unless it is paused. Pause and scale changes first add the time up to that moment, so a change never moves an entity backward or forward. Unpausing drops the time that passed during the pause, so the entity continues from the same position.

`step()` returns the seconds of timeline time since the previous step. It returns 0 while paused.

## Entities and the loop

`Entity` stores `timelineId`. The engine creates two local timelines, both anchored to the global timeline:

| Id | Name | Behavior |
|---|---|---|
| 0 | `Engine::WorldTime` | Scale 1.0, never paused by the player keys. Default for every entity |
| 1 | `Engine::PlayerTime` | Paused with `P`, scale `1` = 0.5, `2` = 1.0, `3` = 2.0 |

Each frame the engine steps every timeline once, then moves each entity with the delta of its own timeline. Pausing or scaling a timeline changes only the entities on it; the entities themselves are not edited. The game update callback receives the `WorldTime` delta.

The main loop is paced from `PlayerTime`'s scale, with vsync off. Scale 1.0 is 60 frames per second, 0.5 is 30, and 2.0 is 120. `engine_demo --client` sends one `PlayerState` per frame, so those keys halve or double that client's 0MQ rate without changing any other client. `P` pauses `PlayerTime` and does not change the pace, so the platform and the send loop keep going. `WorldTime` stays at scale 1.0, so a slower loop takes larger steps and the platform keeps the same speed.

The loop is single-threaded in this change. `Timeline` locks a mutex in every public call, and a child locks its anchor only after itself, so the frame workers can read timelines later without a deadlock.

## Demo

`engine_demo`: the red player is on `PlayerTime` (arrow keys or WASD). The grey platform is on `WorldTime`. `P` freezes only the player; the platform keeps moving. `1` / `3` change only the player's speed. The window title shows both timelines' tics.
