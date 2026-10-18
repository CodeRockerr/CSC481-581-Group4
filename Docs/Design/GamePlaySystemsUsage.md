
# Shared Gameplay Systems Usage

These reusable components are part of the shared Engine. They use
GameObject, Transform, Collider, PhysicsBody, and Camera directly.

## Creating gameplay objects

```cpp
// A hidden checkpoint.
GameObject& checkpoint = objects.create("checkpoint");
checkpoint.add<Transform>(100.0f, 200.0f, 10.0f, 10.0f);
checkpoint.add<SpawnPoint>("checkpoint");

// A horizontally moving platform.
GameObject& platform = objects.create("moving-platform");
platform.add<Transform>(200.0f, 300.0f, 80.0f, 10.0f);
platform.add<MovementPath>(
    MovementPathType::Horizontal, 1.0f, 80.0f);

// A hazard that sends the player to the checkpoint.
GameObject& hazard = objects.create("hazard");
hazard.add<Transform>(400.0f, 500.0f, 50.0f, 50.0f);
hazard.add<Collider>(0.0f, 0.0f, 0.0f, 0.0f, true);
hazard.add<DeathZone>("checkpoint");

// A trigger that moves the camera.
GameObject& cameraTrigger = objects.create("camera-trigger");
cameraTrigger.add<Transform>(600.0f, 300.0f, 20.0f, 100.0f);
cameraTrigger.add<Collider>(0.0f, 0.0f, 0.0f, 0.0f, true);
cameraTrigger.add<SideScrollTrigger>(640.0f, 0.0f);
```

## Updating gameplay

Call these from the existing game update loop, using the real player ID
and frame delta time.

```cpp
GameplaySystems::updateMovementPaths(objects, deltaTime);
GameplaySystems::updateDeathZones(objects, playerId);
GameplaySystems::updateSideScrollTriggers(
    objects, camera, playerId);
```

The player needs a Transform and Collider for death-zone and camera-trigger
collision checks. If the player has a PhysicsBody, its velocity is reset
when it respawns.

## Spawn-point lookup

Spawn points can be found by object ID, name, or zero-based index.

```cpp
GameObject* byId =
    GameplaySystems::findSpawnPointById(objects, checkpoint.getId());

GameObject* byName =
    GameplaySystems::findSpawnPointByName(objects, "checkpoint");

GameObject* byIndex =
    GameplaySystems::findSpawnPointByIndex(objects, 0);
```

A SpawnPoint should have a Transform and SpawnPoint component. It does not
need a Sprite, so it can remain invisible during gameplay.
