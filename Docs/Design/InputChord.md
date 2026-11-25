# Input Chords: Usage Guide

A chord is a group of keys that have to be held at the same time. When the last key goes down, the
shared input system raises one named `Input` event through the `EventManager`. The game's own event
handler decides what that action does. `InputSystem` only detects and raises, it never changes game state.

Example used throughout: **A** moves left, **Space** jumps, and **A + Space** is a separate `dash` action.

Files:

| File | Job |
|---|---|
| `Engine/include/InputChord.h`, `Engine/src/InputChord.cpp` | Detects chords (and optional sequences). Knows nothing about events. |
| `Engine/include/InputSystem.h`, `Engine/src/InputSystem.cpp` | Reads keys each frame, raises the Input events, including chord actions. |
| `Tests/InputChordTest.cpp` | Detector tests. |
| `Tests/InputChordIntegrationTest.cpp` | Tests through `InputSystem::poll`, `EventManager` and replay. |

## 1. Registering a chord

Register chords once at startup, before the main loop begins.

```cpp
#include "InputSystem.h"

InputSystem::registerChord("dash", {SDL_SCANCODE_A, SDL_SCANCODE_SPACE});
```

* The first argument is the action name. It becomes the `action` of the `InputEvent`.
  (`InputActions::Dash` is a ready-made constant for `"dash"`.)
* The second argument is the keys that must all be held.
* It throws `std::invalid_argument` if the name is empty, the key list is empty, or the name is
  already registered.
* Each key should already do something on its own. Here A already moves the player left and Space already jumps.

Other calls:

| Call | What it does |
|---|---|
| `InputSystem::removeChord("dash")` | Removes one binding. |
| `InputSystem::clearChords()` | Removes all bindings (use when a level or game restarts). |
| `InputSystem::resetChords()` | Keeps the bindings but forgets which chords are currently held. |
| `InputSystem::registerSequence("name", {UP, UP, DOWN})` | Optional: keys pressed in order. Not needed for the chord requirement. |

## 2. Handling the chord in the game

The chord arrives as a normal `Input` event. Put the behaviour in the game's handler:

```cpp
events.subscribe(EventTypes::Input, [&](const Event &e) {
    const auto *in = std::get_if<InputEvent>(&e.payload);
    if (!in || in->action != InputActions::Dash)
        return;

    GameObject *player = EventObjects::resolve(objects, e.target);
    if (player)
        startDash(*player);        // game-specific: faster move, cooldown, sound...
});
```

The event's `target` is the first player whose input is enabled, so with two players you still get
exactly one event per activation.

## 3. Design decisions

**Does the chord consume its keys? No, individual actions are preserved.**
While A + Space are held, `move_x` and `jump_held` keep being raised every frame as before, and
`dash` is raised in addition. Existing controls behave exactly as they did without chords. If a game
does not want the player to also jump during a dash, its handler can decide that (for example by ignoring
jump while dashing). The input layer does not hide keys from the game.

**Once per activation.** The chord fires when the last key goes down. Holding the keys does not repeat it.
At least one key has to be released before it can fire again. Key order does not matter, and extra keys
held at the same time do not stop it.

**Order inside a frame.** The chord event is raised after that frame's `move_x`, `jump_held`,
`jump_pressed` and `down_held` events, so the handler sees the current movement input.

**Disabled input.** If the player's `PlayerInput::enabled` is false, no chord event is raised and the normal
actions are neutral (as before). The detector still tracks the keys, so holding the chord through a
disabled stretch does not make it fire the moment input comes back. A fresh activation is needed.

**Several chords.** Chords are checked in registration order, so if two fire in the same frame they come
out in the same order every run. That matters for replay.

**Replay.** A chord is an ordinary `Input` event, so the replay recorder stores it and plays it back with
no special code. During playback `EventManager` drops live `Input` events and `poll` raises no chord events,
so playback cannot produce a duplicate. In the replay restore callback call `InputSystem::resetChords()`.

```cpp
replay.setStateCallbacks(capture, [&](const std::any &state) {
    /* restore the game state ... */
    InputSystem::resetChords();
});
```

## 4. Where to call it in the frame

`InputSystem::poll(events, objects)` is already called by `Engine::run` after `replay->beginFrame()` and
before `replay->dispatch(ReplayPhase::Input)`. A game does not call it itself. A game only registers its
chords and subscribes its handler.

## 5. Testing

From the repository root:

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel 4
ctest --test-dir build -C Release -R "input_chord" --output-on-failure
```

That should show 2 of 2 passed (`input_chord_test`, `input_chord_integration_test`). Then the whole suite:

```
ctest --test-dir build -C Release --output-on-failure
```

On Windows, if a test will not start with error `0xc0000135`, copy the zmq DLL next to the executables:

```
cp build/_deps/libzmq-build/bin/Release/libzmq-v145-mt-4_3_5.dll build/Release/
```

What the tests cover:

| Requirement | Test |
|---|---|
| Individual keys still work | `testIndividualKeys` |
| Chord activates, raised through the EventManager | `testChordActivatesOnceAndKeepsIndividualActions` |
| Component actions preserved with the chord | same test (checks `move_x` and `jump_held` in the chord frame) |
| Held keys do not repeat | `testHeldDoesNotRepeatAndReleaseRetriggers` and detector test 3 |
| Release then retrigger, either key order | `testHeldDoesNotRepeatAndReleaseRetriggers` and detector tests 4, 4b |
| Disabled input, including re-enabling while held | `testDisabledInput` and detector test 5 |
| Only one event with two players | `testTwoPlayersStillOneChordEvent` |
| Removing a binding | `testBindingsCanBeRemoved` |
| Replay: no duplicate chord | `testPlaybackModeDropsLiveChords`, `testReplayDoesNotDuplicateChord` |
| Bad bindings, duplicates, remove, clear, reset | detector tests 7 to 10 |
| Optional sequences | detector test 6 |

## 6. Common problems

* **Chord never fires:** one of the keys is already used by something else on the OS, or the name was
  registered after `clearChords()`. Check with `InputSystem::registerChord` that it did not throw.
* **Chord fires on every press of one key:** you registered a one-key chord. A chord needs two or more keys
  to be meaningful (a single key is accepted but is just a key press).
* **`std::invalid_argument` at startup:** the same action name was registered twice. Call `clearChords()` first
  when restarting a level.