# Gameplay Events: Usage Guide

This covers the input, collision, death and spawn producers and their common handlers.
It builds on Adit's `EventManager` and `ReplayController`.

## 1. The event chain

| Producer (only calls `raise()`) | Event | Handler (runs inside `dispatch()`) | Result |
|---|---|---|---|
| `InputSystem::poll` | Input (`move_x`, `jump_held`, `jump_pressed`, `down_held`) | `GameplayEvents::onInput` | writes `PlayerIntent` |
| `GameplayEvents::produceCollisions` | Collision (enter only) | `onCollision` | death zones raise Death, scroll triggers move the camera, then `Rules::onCollision` |
| `onCollision` (death zone), `produceOutOfBounds`, game hazard code | Death | `onDeath` | `Rules::onDeath` decides lives/game over and returns a spawn point name |
| `onDeath` | Spawn | `onSpawn` | position set to spawn point, velocity zeroed, then `Rules::onSpawned` |

Producers never call handlers. Handlers may raise child events. Those run in the same drain when they are already due, bounded by Adit's budget.

### Event field conventions
- **Collision:** `source` = player, `target` = touched object, `normal` points from the other object toward the player, `trigger` = `Collider::isTrigger`.
- **Death:** `source` = cause (death zone, hazard, or none), `target` = victim, `reason` = text.
- **Spawn:** `source` = spawn point object (if found), `target` = subject, `spawnPoint` = name.
- **Input:** `target` = player, `action` and `value` as in `InputActions`.
- Use `EventObjects::of(obj)` to create object references: `{ownerId, netId}` when networked, otherwise `{EventObjects::OfflineOwner, localId}`. `OfflineOwner` is reserved as `0xFFFFFFFFu`; owner `0` identifies the server. `EventObjects::resolve` uses local IDs only for offline references and matches both owner ID and network ID for network references. It returns `nullptr` when no matching object exists, and handlers ignore unresolved targets.

## 2. Setup in a game

```cpp
GameplayEvents gameplay(engine.getEvents(), engine.getObjects(), engine.getCamera());
player.add<PlayerIntent>();            // once, next to PlayerInput

GameplayEvents::Rules rules;
rules.onDeath = [&](GameObject& victim, const Event& death) -> std::optional<std::string> {
    // game-specific: lives, cooldown, game over...
    return gameplay.defaultSpawnFor(death);   // or "start" after game over, or nullopt for no respawn
};
rules.onSpawned = [&](GameObject&, const Event&) { /* camera snap, hearts, grounded = false */ };
gameplay.setRules(rules);

// in gameUpdate, every frame (also during playback):
gameplay.produceCollisions(playerId);
gameplay.produceOutOfBounds(playerId, WorldHeight + 200.0f, "fell out of the sky");
```

Game-specific hazards (for example Kuromi in Hello Kitty) use `Rules::onCollision`. Check for the hazard component on `other`, then call `gameplay.raiseDeath(player, &other, "reason")`.

## 3. Engine integration

These changes are already implemented in the shared engine.

**a) `Engine.cpp`: poll input inside the replay frame.** The engine calls this immediately before `replay->dispatch(ReplayPhase::Input);`:
```cpp
InputSystem::poll(*events, objects);
```
`InputSystem.h` is included in `Engine.cpp`. Polling comes after `replay->beginFrame()` so the recorder files the events under the Input phase. Games do not need to add another polling call.

**b) `Components.h`: `PlayerInput` reads intent when present.** The header includes `GameObject.h` and `PlayerIntent.h`, and its helpers select intent when available:
```cpp
float horizontalAxis() const
{
    if (!enabled) return 0.0f;
    if (const PlayerIntent* i = intent()) return i->moveX;
    float axis = 0.0f;
    if (anyPressed(left))  axis -= 1.0f;
    if (anyPressed(right)) axis += 1.0f;
    return axis;
}
bool jumpPressed() const     { if (!enabled) return false; if (auto* i = intent()) return i->jumpHeld;    return anyPressed(jump); }
bool jumpJustPressed() const { if (!enabled) return false; if (auto* i = intent()) return i->jumpPressed; return anyJustPressed(jump); }
bool downPressed() const     { if (!enabled) return false; if (auto* i = intent()) return i->downHeld;    return anyPressed(down); }
// in the private section:
const PlayerIntent* intent() const { return owner ? owner->get<PlayerIntent>() : nullptr; }
```
A player without `PlayerIntent` still polls keys directly, so existing games keep working.

**c) CMake.** `Engine/CMakeLists.txt` includes these sources in the `engine` library:
```
    src/InputSystem.cpp
    src/GamePlayEvents.cpp
```
The root `CMakeLists.txt` registers the test when `BUILD_TESTING` is enabled:
```
add_executable(event_chain_test Tests/EventChainTest.cpp)
target_link_libraries(event_chain_test PRIVATE engine)
add_test(NAME event_chain_test COMMAND event_chain_test)
```
Use the exact filename capitalization shown above on all platforms.

`GameplaySystems::updateDeathZones` and `updateSideScrollTriggers` are left unchanged, so `GamePlaySystemsTest` keeps passing. New games should use `GameplayEvents` instead and not call both.

## 4. Replay checklist (state each game's capture/restore hook must handle)
The replay records Input events and the pending events at record start. Collision, Death and Spawn events are regenerated by re-running the same simulation. The game's capture hook must save the following state in its snapshot, and its restore hook must put back the captured values:
- player transform, velocity, and `PlayerIntent` action values
- `gameplay.captureTransientState()`, which captures touching objects, the out-of-bounds latch, and every `DeathZone::armed` flag, including inactive zones; restore this value with `gameplay.restoreTransientState(saved.transient)`
- `SideScrollTrigger::activated`, camera position, and the full `MovementPath` state, including elapsed time, initialization, and start/center coordinates
- the game's own state (lives, deaths, cooldowns, grounded/riding flags)

Store the returned `GameplayEvents::TransientState` as a value in the game's replay snapshot. `saved.transient` above refers to that snapshot field. Restore object identities before restoring producer state. `Tests/EventChainTest.cpp` includes a complete capture/restore example in `testRecordingInsideTriggeredZone`.

Use `resetTransientState()` only for a fresh game or level. Calling it during replay restore forgets existing contacts and rearms death zones, which can introduce an extra collision or death when recording began inside an already-triggered zone.

Producers run every frame during playback. While playing, `raise()` drops live Input and tags local events as Replay origin, so the producers need no special replay code.

## 5. Known limits
- Collision events fire on enter only (no "stay" or "exit"). The producer tests the player against all other colliders, which is O(n) per frame.
- Solid-contact resolution (landing on ground) still lives in each game. Physics does not resolve collisions.
- If `Rules::onDeath` suppresses a death (for example a cooldown) and returns nullopt, the player is not teleported and the zone stays disarmed until the player leaves it.
- Death and Spawn are local by default. `setShareLifecycleEvents(true)` marks them shareable for Sai's transport, and spawn-vs-snapshot ownership of remote objects is Sai's contract.
