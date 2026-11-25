# Pooled GameObject storage (Milestone 5)

Owner: Sai Sumedh Kaveti. Builds on Adit's `PoolAllocator<T>` (`Engine/include/PoolAllocator.h`).

## What it does

`GameObjectManager` can now keep all of a game's `GameObject`s in one fixed block reserved
up front, instead of one heap allocation per object. Games opt in with one call:

```cpp
GameObjectManager &objects = engine.getObjects();
objects.usePool(192);            // designer picks the capacity; call before the first create()

GameObject &enemy = objects.create("enemy");       // throws std::bad_alloc if the pool is full
GameObject *bullet  = objects.tryCreate("bullet"); // returns nullptr if the pool is full
```

Games that never call `usePool()` behave exactly as before (heap allocation, no limit).
Cave Ninja, Lost Under the Sea and Hello Kitty Adventure are unchanged.

## API added to GameObjectManager

| Call | Meaning |
| --- | --- |
| `usePool(capacity)` | Reserve storage for `capacity` objects. Throws `std::logic_error` if objects already exist (live objects cannot move between storages) and `std::invalid_argument` for 0. |
| `tryCreate(name)` | Like `create()`, but returns `nullptr` when the pool is full. Never null in heap mode. |
| `usesPool()`, `poolCapacity()`, `poolInUse()`, `poolAvailable()`, `poolPeakUsage()` | Read-only usage numbers, used by the in-game pool meter and the tests. |

`create()` / `createWithId()` keep their reference-returning signatures. With a pool they
throw `std::bad_alloc` when full. A failed create changes nothing: no id is used and no slot
is taken.

## Design decisions

| Decision | Why | Cost |
| --- | --- | --- |
| Ownership through `std::unique_ptr<GameObject, GameObjectDeleter>`; the deleter holds the pool pointer (null for heap objects) | Each pointer knows where its object came from, so `objects.erase(...)`, `clear()` and the manager's destructor all return memory to the right place. No other manager code had to change. | `getAll()` now returns `std::vector<GameObjectPtr>`. Nothing in the repo used the old exact type. |
| Copy and move disabled (`= delete`), default constructor kept | A defaulted move assignment assigns `pool` first, freeing the old block before the old objects are released, so their deleters would touch freed memory (found in review of PR #32). Engine owns one manager and never moves it, so forbidding moves is safer than a hand-written move. | A manager can't be stored in a container by value or returned by value. Nothing needs that today. |
| `pool` declared before `objects` | Members are destroyed in reverse order, so every live object goes back to its slot before the pool frees its block. | Order-dependent; there is a comment in the header so nobody reorders it. |
| Opt-in, chosen while the manager is empty | Old games keep working; a game decides its own capacity. | A game must set the pool before building its level. |
| Destruction stays deferred (`destroy()` then `flushDestroyed()` at the end of the frame) | Slots are reused only after the worker threads have finished the frame, so a worker never sees a slot change under it. | A destroyed object holds its slot until the end of the frame. |
| `clear()` keeps the block | Restarting a level reuses the same memory; the tests check the addresses repeat. | Peak usage is not reset by `clear()` (it is the allocator's lifetime peak). |
| Two creation calls (`create` throws, `tryCreate` returns null) | Level objects must exist, so failing loudly is right. Bullets, balls and debris are optional, so games just skip them. | Games must pick the right call. |

## What it does not do

Pooling covers the `GameObject` itself. Its components (`std::unique_ptr` in an
`unordered_map`), its name string and textures still use the heap. So a pooled game makes
far fewer object allocations, not zero allocations. The value is predictable object memory
and a hard cap the game must design for, not raw speed. I did not benchmark speed.

Ids are never reused within a level, but slots are. Code must hold ids, not raw pointers,
across frames.

## Tests

`Tests/PooledObjectManagerTest.cpp` (`pooled_object_manager_test`), 10 checks:

1. Heap mode is unchanged (500 objects, no cap).
2. The pool must be chosen while the manager is empty; capacity 0 is rejected.
3. Exhaustion changes nothing (`create` throws, `tryCreate` returns null, ids are not burned).
4. Destroy is deferred and freed slots are reused.
5. `clear()` returns everything and keeps the same block.
6. A duplicate id does not leak a slot.
7. Engine systems (`forEach`, `updateTimeline`, snapshots) see pooled objects.
8. Heavy create/destroy churn stays within capacity and leaks no components.
9. Destroying the manager releases every live object.
10. The manager can't be copied or moved, and its default constructor still works in both modes
    (`static_assert`s make re-enabling a move a build error).

## Regression check

Full suite run one test at a time (`ctest -j1`) on macOS and Linux: everything passes.
`net_events_test` is timing-sensitive and occasionally fails on a busy machine. Alternating 12 runs each, the commit before
this change failed it 4 times and this change failed it once, always on the same check
("server event resolves client-owned actor and replica"). So the occasional failure predates this work.
Running `ctest -j4` also makes the two sonar tests fail because they share network ports; they
pass when run alone.
