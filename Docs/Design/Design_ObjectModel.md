# Milestone 3 game object model

Owner: Adit. Reviewers: Yeva (gameplay components), Sai (networking).

## Decision

The engine uses a **component-based** runtime object model.

A `GameObject` is a stable numeric id, a name, an `active` flag, and a set of components. A component is a small piece of data, and sometimes a small piece of behavior, for one concern: where the object is, how it is drawn, how it collides, how it moves. An object's "type" is never a class. It is whatever set of components it has. A platform is a `Transform`, a `Sprite`, and a `Collider`. A moving platform is the same object plus a movement path. A death zone is a `Transform` and a trigger `Collider` with no `Sprite`, so it is never drawn.

Engine systems (physics, movement, collision, rendering, camera, networking) loop over the objects that have the components they need and ignore everything else.

## Why this replaces `Entity`

The Milestone 1 and 2 `Entity` class stored position, size, velocity, color, gravity, timeline, texture, sprite frame, and flip on every object. That causes three problems:

1. **Every object pays for every field.** The background and the hearts carry velocity and gravity they never use. A hidden death zone would carry a texture pointer and a color it must never draw.
2. **New kinds of object mean editing the core class.** Spawn points, death zones, side-scroll boundaries, movement paths, and network ownership would each add more fields to `Entity`, and every game would recompile against a larger, more fragile class.
3. **The games already work around it.** Lost Under the Sea and Cave Ninja each wrote their own hitbox padding helper (`bodyHitbox`, `paddedHitbox`) because `Entity` has one rectangle for both drawing and collision. That is a missing collider concept.

Milestone 3 also forbids a monolithic inheritance hierarchy, so a `Player : Entity`, `Platform : Entity` tree is not an option.

## Alternatives considered

| Option | Why not |
|---|---|
| Inheritance hierarchy (`GameObject` then `Character`, `Platform`, `MovingPlatform`, and so on) | Disallowed by the milestone. It also forces combinations into the tree: a moving platform that hurts the player needs a class that is both. Diamond and "blob base class" problems follow. |
| Keep `Entity` and add flags | This is the monolith again, and it grows with every feature. |
| Property-centric model (one table per property, keyed by object id) | Good cache behavior for large counts, and easy to serialize. But each property is a free-floating table, behavior lives far from data, and the three games mostly reason about one object at a time ("the diver touched the fish"). For our object counts (tens to a few hundred) the cache benefit is small, and the API is harder for each teammate to use correctly. |
| Full ECS with archetype storage | Fastest at very large counts, but a lot of engine code for a course project with hundreds of objects. Not needed for the experiments (10, 100, 500 objects). |

Component-based composition gives the main benefit of the property model (objects are defined by data, not class) while keeping one object's data together and readable.

## Tradeoffs and limitations

- Component lookup goes through a `std::type_index` map, so `get<T>()` is a hash lookup instead of a direct field access. Systems that loop every frame should fetch the components once per object per pass.
- Components live in separate heap allocations, so memory access is less cache friendly than a property table. Acceptable at our scale, and something the performance experiments can expose.
- One component of each type per object. Two colliders on one object would need a second object or a collider that holds several shapes.
- Behavior that depends on two objects (the diver touching the fish) still lives in game code or in a system, not inside one component.

## How the three games map onto the model

| Game object | Components |
|---|---|
| Background (all games) | `Transform`, `Sprite` |
| Static platform, shelf, ledge | `Transform`, `Sprite`, `Collider` (solid) |
| Moving platform (Cave Ninja right platform) | `Transform`, `Sprite`, `Collider`, movement path (Yeva) |
| Local player: diver, ninja, Hello Kitty | `Transform`, `Sprite`, `Collider` (with padding), `PhysicsBody` (gravity, `PlayerTime`), `PlayerInput` |
| Remote player | `Transform`, `Sprite`, `PhysicsBody` (`WorldTime`, no gravity), network identity (Sai) |
| Fish, shadow wisp, Kuromi | `Transform`, `Sprite`, `Collider` (trigger), movement path on `WorldTime` |
| Hearts, bubbles (UI and effects) | `Transform`, `Sprite` |
| Spawn point | `Transform`, spawn point marker (Yeva). No `Sprite`, so hidden |
| Death zone | `Transform`, `Collider` (trigger), death zone marker (Yeva). Hidden |
| Side-scroll boundary | `Transform`, `Collider` (trigger), side-scroll marker that moves the `Camera` (Yeva). Hidden |

Nothing in that table needs a new class in the core. New requirements become new components.

## Core API

Headers live in `Engine/include/`.

### Identity

- Every object has a `uint32_t` id that never changes and is never reused while the program runs.
- `GameObjectManager::create()` hands out the next local id.
- `GameObjectManager::createWithId(id)` creates an object with an id chosen elsewhere. The network code uses it so that a replica on a client has the same id as the object on the server.

### Lifecycle

1. `create()` or `createWithId(id)` returns a `GameObject&`.
2. `add<T>(args...)` attaches a component and returns it. Adding a type that is already present replaces it.
3. `get<T>()` returns a pointer or `nullptr`. `has<T>()` returns a bool. `remove<T>()` detaches it.
4. `destroy(id)` marks the object for removal. It stays valid until `flushDestroyed()` runs at the end of the frame, so a system that is iterating never sees a freed object.
5. Setting `active = false` hides and freezes an object without destroying it.

### Update order each frame

1. Events and input.
2. Game update callback (game logic, reading `PlayerInput`, network messages, creating and destroying objects).
3. Frame workers: player-timeline worker and world-timeline worker run physics (gravity) then movement (velocity into position) for their timeline.
4. World update callback (runs inside the world worker).
5. Camera update (follow target).
6. Render snapshot of the previous finished frame is drawn while the workers build the next one.
7. `flushDestroyed()`.

### Thread safety

- Objects are only created and destroyed on the main thread, outside the worker phase. The frame workers never change the object list.
- Each `GameObject` keeps the existing per-object mutex. Workers lock an object while they change its `Transform` or `PhysicsBody`, and the snapshot locks it while copying.
- The two workers touch disjoint objects because each object belongs to exactly one timeline.

### Core components

| Component | Data | Used by |
|---|---|---|
| `Transform` | `x`, `y`, `width`, `height` in world units | everything |
| `Sprite` | texture, tint color, frame, frame count, horizontal flip, `visible`, `layer` | render system |
| `Collider` | padding on each side (fraction of the transform), `isTrigger`, `enabled` | collision helpers |
| `PhysicsBody` | `velocityX`, `velocityY`, `affectedByGravity`, `timelineId` | physics and movement |
| `PlayerInput` | left, right, jump, and down key bindings, move speed, jump speed, `enabled` | game logic |

`Collider` padding replaces the per-game hitbox helpers. An object with no `Sprite`, or with `visible = false`, is never drawn.

### Extension points

- Teammates add components by deriving from `Component` in their own headers. The core never needs to know about them.
- Yeva: movement paths (horizontal, vertical, circular), spawn point, death zone, side-scroll boundary.
- Sai: network identity (owner client id, whether this endpoint is authoritative), plus whatever the message format needs to read and write components.

### Usage sample

```cpp
GameObjectManager &objects = engine.getObjects();

GameObject &player = objects.create("player");
player.add<Transform>(600.0f, 300.0f, 50.0f, 50.0f);
player.add<Sprite>(SDL_Color{230, 60, 60, 255});
player.add<Collider>(0.1f, 0.1f, 0.05f, 0.0f);
player.add<PhysicsBody>(Engine::PlayerTime, true);
player.add<PlayerInput>(300.0f, 500.0f);

GameObject &platform = objects.create("platform");
platform.add<Transform>(100.0f, 520.0f, 240.0f, 30.0f);
platform.add<Sprite>(SDL_Color{160, 160, 160, 255});
platform.add<Collider>();
platform.add<PhysicsBody>(Engine::WorldTime, false).velocityX = 120.0f;

objects.forEach<Transform, PhysicsBody>([](GameObject &object, Transform &t, PhysicsBody &body) {
    // runs only for objects that have both components
});

// A custom component in a teammate's own header:
struct DeathZone : Component { uint32_t respawnAt = 0; };
```

## Camera

The camera separates world space from screen space.

- State: world position of the top-left corner, viewport size in world units, zoom, optional world bounds.
- `worldToScreen` and `screenToWorld` convert points and rectangles.
- `follow(objectId)` keeps a target inside a dead zone in the middle of the view. Updated once per frame before drawing.
- `setPosition` and `translate` move the camera directly. Yeva's side-scroll boundary calls these.
- The render system draws every visible object through the camera, then applies the existing pixel or percentage scale mode. That keeps Milestone 1 scaling working.

## Migration and compatibility

- `Engine::getObjects()` returns the `GameObjectManager`. All engine systems use it.
- `Entity` and `EntityManager` stay for now as a deprecated layer so the three games still build. Each owner moves their own game to the new API as part of their individual work. The layer is removed once all three are moved.

## Status

Filled in as each step lands.

- Step 1: design note written (this file).
- Step 2: API headers added (`Component.h`, `GameObject.h`, `GameObjectManager.h`, `Components.h`).
- Step 3: `GameObjectManager.cpp` implemented; `object_model_test` covers create, createWithId, find, forEach, deferred destroy, and clear.
