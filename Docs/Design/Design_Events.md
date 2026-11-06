# Milestone 4: Events and Replay

Owner: Adit. This change provides the shared event API, event manager, engine dispatch
phases, replay controller, and tests. Yeva will add shared gameplay event producers
and handling. Sai will add network transport. Each game owner integrates the shared
features into their individual game, including replay for all three games.

## Scope

Implemented here:

- Value-based event records and the four gameplay payload contracts.
- Registration, unsubscribe, thread-safe enqueueing, ordered dispatch, and dispatch limits.
- Three main-thread dispatch phases integrated into the existing engine loop.
- Frame timing shared by callbacks and physics workers.
- In-memory local replay with game-provided state capture and restore hooks.
- Always-on tests for the manager, replay, and the actual Engine loop.

Not implemented here:

- Keyboard, collision, death-zone, and spawn producers in the existing gameplay systems.
- Migration of individual game logic to event handlers or replay controls.
- Event packets, server fan-out, acknowledgments, deduplication, or clock mapping.
- Network-session replay, disk persistence, rewind, or automatic serialization of game state.

This is Adit's engine foundation, not a claim that all Milestone 4 submissions are complete.

## Core design decisions

| Decision | Choice | Reason and tradeoff |
| --- | --- | --- |
| Representation | Value records in `Event.h` | Queued events own their strings and payload values. They contain no GameObject pointers or handler functions. |
| Event types | Stable numeric types; engine reserves 1–7 and game types start at 100 | Arbitrary game types use the same manager without adding manager switch cases. |
| Payloads | Named gameplay structs plus `EventFields` for custom data | Shared gameplay fields are discoverable in the header. Custom fields remain serializable values; producers and codecs must agree on their schema. |
| Registration | Subscribe by type, or observe all events; return a numeric token | Explicit unsubscribe keeps ownership visible. A game must unsubscribe before destroying captured listener state. |
| Raising | Enqueue only, from any producer thread | Producers do not execute handlers or hold the queue lock during callbacks. |
| Handling | One non-recursive consumer; Engine uses the main thread | Mutating objects remains outside the engine worker phase. Server integration can use its own single simulation consumer. |
| Ordering | Earliest due timestamp, higher priority, then insertion order | A future high-priority event cannot bypass its due time. Equal-time/equal-priority events are FIFO. |
| Event time | Integer microseconds derived from Timeline | Timestamp units stay fixed even if Timeline tic size changes. Engine dispatch uses one frame-stable game clock. |
| Cascading events | Due child events can run in the same drain | Death can enqueue spawn without a recursive callback. A per-drain budget bounds accidental cycles. |
| Replay | Record input, state, timing, and dispatch phase; regenerate consequences | Collision/death/spawn handlers execute again from replayed decisions, rather than also replaying recorded consequences. |
| State ownership | Game-provided value snapshot in `std::any` | The engine does not hardcode a diver's lives, pearls, inventory, or camera rules. There is no automatic save-file format. |
| Compatibility | Existing `Engine::run` signatures and games remain available | Individual game migration is a separate step; this change does not edit teammate games. |

## Event contract

`Event` contains `type`, `timestamp`, `priority`, `id`, `source`, `target`, `payload`,
`origin`, and `shareable`.

- A negative timestamp asks `raise` to stamp the event with `now()`.
- Priorities are Low, Normal, High, and Critical. Normal is the default.
- `EventId` is `(ownerId, sequence)`. A zero sequence requests a new local ID.
  `setOwnerId` selects the producer's owner before network integration. Incoming
  nonzero IDs are preserved. The manager itself does not deduplicate deliveries.
- `EventObject` is `(ownerId, netId)`. For offline use, owner zero with a local object
  ID is sufficient. Network receivers must resolve identities through replication,
  not assume that remote netIds equal local GameObject IDs.
- Origins are Local, Network, and Replay. A transport must mark incoming events
  Network and only publish appropriate Local, shareable events.
- Replay controls are never shareable. Type zero is reserved and rejected.

| Type | Payload | Object roles |
| --- | --- | --- |
| Collision | `CollisionEvent { normalX, normalY, trigger }` | `source` and `target` identify the colliding pair. |
| Death | `DeathEvent { reason }` | `source` identifies the cause when available; `target` identifies the victim. |
| Spawn | `SpawnEvent { spawnPoint }` | `target` identifies the actor; the game resolves the named SpawnPoint. |
| Input | `InputEvent { action, value }` | `target` identifies the controlled actor. Action names and held/edge semantics are agreed by the game. |
| ReplayStart | Empty | Request recording at the next frame boundary. |
| ReplayStop | Empty | Request stopping recording or playback at the next frame boundary. |
| ReplayPlayback | Empty | Request playback of the stored recording at the next frame boundary. |

`EventFields` maps names to signed integers, doubles, booleans, or strings. The manager
does not interpret custom payloads. Handlers and future codecs must validate the
payload expected for each event type. Raw copying of an Event is not a wire format:
strings, maps, and variants require explicit serialization.

## Registration and dispatch

`subscribe(type, handler)` and `subscribeAll(handler)` return tokens. Listeners run
in registration order for each event. Removing a listener during dispatch prevents
it from running later in that event. Registering a new listener affects later events.

Callbacks run without the queue mutex held. They may raise more events or unsubscribe.
Calling dispatch recursively, or from another simultaneous consumer, throws. The
default budget is 1,024 popped events per drain; remaining work stays queued. Handler
exceptions propagate to the caller and release the dispatch guard.

Subscription state must outlive its callback. Unsubscribe and object destruction
belong on the consumer thread or between dispatches; unregistering from another
thread does not wait for an already-running callback.
Stop producer threads before destroying their EventManager.

Engine frame order:

1. Poll window events and keyboard state; apply existing time controls outside playback.
2. Step Timelines once and begin the replay frame, applying pending replay commands.
3. Dispatch the Input phase before the game callback.
4. Run the game callback with the current WorldTime frame delta.
5. Dispatch the Update phase.
6. Run player/world workers using cached frame deltas and draw the completed snapshot.
7. Wait for both workers, then dispatch the Workers phase on the main thread.
8. Finish the replay frame, flush deferred destruction, and capture the next render snapshot.

The post-worker drain lets worker-raised events mutate objects before destruction
is flushed. Events raised while an input handler runs do not recursively call
another handler; they re-enter the queue under the same ordering policy.

Timeline stepping moved to the main frame boundary. Workers consume the same
cached deltas, instead of independently stepping their Timelines. `getFrameDelta(id)`
and `getFrameRealDelta()` expose the selected timing for gameplay and replay.

## Replay decisions

There is one ReplayController per engine/EventManager. It subscribes to the three
control types. Direct `startRecording`, `stop`, and `startPlayback` calls also queue
commands. Commands take effect at the next `beginFrame`, in request order, so state
is never restored halfway through physics or dispatch.
ReplayController methods and state hooks belong on the simulation consumer thread.
Other threads request replay changes by raising the control events.

Recording captures:

- A game-provided initial-state value.
- Existing pending non-input gameplay events, excluding replay controls.
- Per-frame unscaled delta and each Timeline's delta, scale, and paused state.
- Local input events, their relative timestamps, and the phase where they were handled.

The engine clock advances from the WorldTime delta and stays constant within a frame.
It starts when the controller is created, so asset-loading time before the first
frame does not delay already-queued events.
Playback restores the initial state and pending events, then selects the recorded
timing and injects input into the original dispatch phases. Collision, death, and
spawn consequences are generated by normal gameplay again.

During playback, live input and incoming network gameplay events are rejected.
Replayed input and locally regenerated consequences are marked Replay and are not
shareable. Replay origin also propagates through later child events. Replay controls
remain available. The recording is not extended during playback.

Playback finishes at the recorded final state and re-enables live input. Future
gameplay events generated during playback remain queued, so the game can continue
from that state. The simulation clock continues from the replayed time without
jumping back to the live wall-clock position.

State hooks must capture all game state affecting future simulation: positions,
velocities, object activity, movement-path phase, input intent, lives, collectibles,
timers, checkpoint/mission state, and camera state where applicable. The game must
restore relevant object identities and listener bindings. Do not capture references
to mutable live state as the snapshot.

Replay-aware code must use the frame timing getters and `getEvents().now()` for game
time. Reading raw wall time or Timeline elapsed time during playback bypasses the
recorded simulation. Live snapshot application and publication must be disabled
by the game/network integration during local playback. Network-session replay is
outside this first version.

Recording is in memory, with no frame limit or persistence. Start/stop/play does not
promise portable bitwise determinism across different binaries or platforms. Tests
verify repeated playback of the same simulation using the same build.

## Yeva's handoff

- Add keyboard-state event producers; do not replace the required gameplay events with SDL events.
- Add collision and death-zone producers using the payload contracts above.
- Route damage and respawn through handlers, removing equivalent direct side effects.
- Let each game choose checkpoint and game-over rules; death does not imply an unconditional spawn.
- Unsubscribe game/object listeners during cleanup.
- Keep SDL window/quit handling on the main thread.

A minimal offline input integration is:

```cpp
auto &events = engine.getEvents();
auto subscription = events.subscribe(EventTypes::Input, [&](const Event &event)
{
    if (event.target.netId != player.getId())
        return;
    const auto &input = std::get<InputEvent>(event.payload);
    if (input.action == "horizontal")
        player.get<PhysicsBody>()->velocityX = input.value * 280;
});

Event move;
move.type = EventTypes::Input;
move.target = {0, player.getId()};
move.payload = InputEvent{"horizontal", 1};
events.raise(move);
```

The engine drains the event; game code does not call dispatch while workers run.

## Sai's handoff

- Serialize the value fields and payload types explicitly, preserving both supported formats.
- Set the manager owner ID and preserve incoming event IDs.
- Mark received events Network and enqueue them; do not run handlers on network threads.
- Keep server-centric routing, authoritative ordering, per-recipient queues, acknowledgments,
  duplicate prevention, and server/local clock mapping in the transport integration.
- Deliver fan-out through the existing REQ/REP exchanges rather than unsolicited REP sends.
- Define one owner for object creation so a Spawn event and a snapshot do not create duplicates.
- Do not publish Replay-origin events, replay controls, or local playback state.

## Tests

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build --config Release --target event_manager_test replay_test engine_events_test --parallel 4
ctest --test-dir build -C Release -R '^(event_manager_test|replay_test|engine_events_test)$' --output-on-failure
```

The new tests use runtime checks that remain active in Release builds. They cover
due-time/priority/FIFO ordering, payloads, arbitrary event types, unsubscribe, chained
events, four concurrent producers, recursive-dispatch rejection, dispatch budgets,
exception recovery, replay isolation, state and scheduled-event restoration, event
phase reproduction, repeated playback, and native engine physics replay.

The engine integration test raises from the world worker and verifies that its
handler runs on the main thread after worker completion. Individual game migration
and actual networked events remain follow-up work for the assigned owners.

## Decision record

- Used existing C++17 facilities and engine APIs; added no dependency or SDL user-event subsystem.
- Kept EventManager responsible for routing and ReplayController responsible for recording/timing.
- Chose explicit state hooks rather than attempting to copy non-copyable GameObjects or arbitrary components.
- Preserved pending gameplay events when recording starts; input and control events are treated separately.
- Preserved frame phase as well as time because an input handled after physics must affect the next frame.
- Added a third dispatch drain after workers join to preserve the current engine's mutation boundaries.
- Left network protocol changes and gameplay producer migration out of this PR to avoid overlapping ownership.
