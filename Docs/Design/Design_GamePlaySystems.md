# Gameplay Systems Design

The gameplay systems provide reusable engine components for moving objects, defining spawn locations, respawning players, and triggering camera movement. They are implemented in the shared engine so that all three games can use them without duplicating game-specific logic.

## Patterned Movement

`MovementPath` is attached to a game object that has a `Transform`. It supports three movement patterns:

- **Horizontal:** moves an object back and forth along the X axis.
- **Vertical:** moves an object back and forth along the Y axis.
- **Circular:** moves an object around its initial position.

The component stores the movement type, speed, distance, and initial position. `speed` is an angular rate in **radians per second**; `distance` is the amplitude (horizontal/vertical) or radius (circular). Circular paths offset their center so the object does not jump on the first tick. `GameplaySystems::updateMovementPaths` advances the movement using delta time.

Example:

```cpp
auto& objects = engine.getObjects();

// Create a game object using the project's GameObjectManager API.
// Attach a Transform and a MovementPath component.
```

Movement should be updated using the world's delta time so that it is independent of any one game's implementation.

## Spawn Points

A `SpawnPoint` identifies a named location where a player can appear or reappear. The spawn object requires a `Transform` and a `SpawnPoint` component. Spawn points do not need a `Sprite`, so they can remain invisible during gameplay. Multiple named spawn points can exist in the same scene. The `DeathZone` component specifies the name of the spawn point to use after a collision.

## Death Zones and Respawning

A death-zone object uses:

- `Transform` to define its position.
- `Collider` configured as a trigger.
- `DeathZone` to identify the destination spawn point.

`GameplaySystems::updateDeathZones` checks collisions using `Collision::checkCollision`. When the player **enters** a death zone and the named spawn point exists, the player's transform is moved to the spawn location. If the player has a `PhysicsBody`, its horizontal and vertical velocities are reset to zero. Respawn is edge-triggered (`DeathZone::armed`) so overlapping the zone does not teleport every frame; the zone re-arms after the player leaves.

Death zones do not require a visible sprite. Spawn points should not overlap the death zone that teleports to them.

## Side-Scroll Triggers

A side-scroll trigger uses a `Transform`, a trigger `Collider`, and a `SideScrollTrigger` component. The component stores the camera offset and whether the trigger has already activated. When the player collides with an inactive trigger, `GameplaySystems::updateSideScrollTriggers` calls the camera's `translate` method and marks the trigger as activated. The engine does not hard-code level boundaries or camera offsets. Each game configures its own trigger objects and camera behavior.

## Integration and Testing

These systems are **not** wired into `Engine::run` automatically. The game or engine integration layer must call `GameplaySystems::updateMovementPaths`, `updateDeathZones`, and `updateSideScrollTriggers` each frame with the world delta time, object manager, player ID, and camera. See `Docs/Design/GamePlaySystemsUsage.md`.

Gameplay tests verify horizontal, vertical, and circular movement (including no circular first-frame jump); named-spawn respawning and velocity reset; death-zone edge triggering; and camera translation when a trigger activates. These systems must use the shared `GameObject`, `Transform`, `Collider`, `PhysicsBody`, and `Camera` APIs rather than relying on the legacy `Entity` system.