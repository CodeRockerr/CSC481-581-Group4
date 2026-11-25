// Unit tests for InputChord (detector only, no engine objects, no window).
#include "InputChord.h"

#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    int failures = 0;

    void check(bool condition, const char *description)
    {
        if (!condition) {
            std::cerr << "FAIL: " << description << '\n';
            ++failures;
        }
    }
}

int main()
{
    InputChord chords;

    std::set<SDL_Scancode> heldKeys;
    std::set<SDL_Scancode> pressedKeys;

    const auto isDown = [&](SDL_Scancode key) { return heldKeys.count(key) != 0; };
    const auto wasJustPressed = [&](SDL_Scancode key) { return pressedKeys.count(key) != 0; };
    const auto update = [&](bool enabled = true) { return chords.update(isDown, wasJustPressed, enabled); };
    const auto press = [&](SDL_Scancode key) { heldKeys.insert(key); pressedKeys.insert(key); };
    const auto release = [&](SDL_Scancode key) { heldKeys.erase(key); pressedKeys.erase(key); };
    const auto finishFrame = [&]() { pressedKeys.clear(); };
    const auto wipe = [&]() { chords.clear(); heldKeys.clear(); pressedKeys.clear(); };

    // 1. One component key is not a chord.
    chords.addChord("dash", {SDL_SCANCODE_A, SDL_SCANCODE_SPACE});
    press(SDL_SCANCODE_A);
    check(update().empty(), "One component key must not activate the chord");
    finishFrame();

    // 2. The second key activates it once.
    press(SDL_SCANCODE_SPACE);
    {
        const auto actions = update();
        check(actions.size() == 1 && actions[0] == "dash", "Both component keys activate the named chord");
    }
    finishFrame();

    // 3. Holding does not retrigger.
    check(update().empty(), "Holding an active chord does not retrigger");
    finishFrame();
    check(update().empty(), "A held chord stays quiet on later frames");
    finishFrame();

    // 4. Release one key, press it again: new activation.
    release(SDL_SCANCODE_A);
    check(update().empty(), "Releasing a component does not itself trigger the chord");
    finishFrame();
    press(SDL_SCANCODE_A);
    {
        const auto actions = update();
        check(actions.size() == 1 && actions[0] == "dash", "Re-pressing the released key retriggers the chord");
    }
    finishFrame();

    // 4b. Key order does not matter, and extra keys do not block a chord.
    wipe();
    chords.addChord("dash", {SDL_SCANCODE_A, SDL_SCANCODE_SPACE});
    press(SDL_SCANCODE_SPACE);
    check(update().empty(), "Space alone is not the chord");
    finishFrame();
    press(SDL_SCANCODE_LSHIFT);
    press(SDL_SCANCODE_A);
    check(update().size() == 1, "Pressing the keys in the other order, with an extra key held, still activates");
    finishFrame();

    // 5. Disabled input emits nothing, and re-enabling while held does not fire.
    wipe();
    chords.addChord("disabled_test", {SDL_SCANCODE_A, SDL_SCANCODE_SPACE});
    press(SDL_SCANCODE_A);
    press(SDL_SCANCODE_SPACE);
    check(update(false).empty(), "Disabled input suppresses chord actions");
    finishFrame();
    check(update(false).empty(), "Disabled input keeps suppressing a held chord");
    check(update(true).empty(), "Re-enabling input while a chord is held does not trigger it");
    release(SDL_SCANCODE_A);
    finishFrame();
    check(update(true).empty(), "Releasing a chord key does not emit an action");
    press(SDL_SCANCODE_A);
    {
        const auto actions = update(true);
        check(actions.size() == 1 && actions[0] == "disabled_test",
              "A fresh activation works after input is re-enabled");
    }
    finishFrame();

    // 5b. Several chords, shared key: results come back in registration order.
    wipe();
    chords.addChord("first", {SDL_SCANCODE_A, SDL_SCANCODE_SPACE});
    chords.addChord("second", {SDL_SCANCODE_S, SDL_SCANCODE_SPACE});
    press(SDL_SCANCODE_A);
    press(SDL_SCANCODE_S);
    press(SDL_SCANCODE_SPACE);
    {
        const auto actions = update();
        check(actions.size() == 2 && actions[0] == "first" && actions[1] == "second",
              "Two chords sharing a key both fire, in registration order");
    }
    finishFrame();

    // 6. Sequences (optional feature): Up, Up, Down, with quiet frames in between.
    wipe();
    chords.addSequence("sequence", {SDL_SCANCODE_UP, SDL_SCANCODE_UP, SDL_SCANCODE_DOWN});

    press(SDL_SCANCODE_UP);
    check(update().empty(), "An incomplete sequence does not trigger");
    finishFrame();
    release(SDL_SCANCODE_UP);
    for (int i = 0; i < 5; ++i) {
        check(update().empty(), "Quiet frames between sequence keys do not trigger");
        finishFrame();
    }
    press(SDL_SCANCODE_UP);
    check(update().empty(), "The second key does not finish the sequence");
    finishFrame();
    release(SDL_SCANCODE_UP);
    finishFrame();
    press(SDL_SCANCODE_DOWN);
    {
        const auto actions = update();
        check(actions.size() == 1 && actions[0] == "sequence", "The full sequence triggers, once");
    }
    finishFrame();
    release(SDL_SCANCODE_DOWN);
    check(update().empty(), "A finished sequence starts over");
    finishFrame();

    // 6b. Wrong order does not trigger.
    press(SDL_SCANCODE_UP);
    update();
    finishFrame();
    release(SDL_SCANCODE_UP);
    press(SDL_SCANCODE_DOWN); // expected Up
    check(update().empty(), "A wrong key mid-sequence does not trigger");
    finishFrame();
    release(SDL_SCANCODE_DOWN);
    press(SDL_SCANCODE_UP);
    update();
    finishFrame();
    release(SDL_SCANCODE_UP);
    press(SDL_SCANCODE_DOWN);
    check(update().empty(), "Up, Down is not Up, Up, Down");
    finishFrame();
    release(SDL_SCANCODE_DOWN);

    // 6c. Timeout.
    wipe();
    chords.addSequence("slow", {SDL_SCANCODE_UP, SDL_SCANCODE_DOWN});
    press(SDL_SCANCODE_UP);
    update();
    finishFrame();
    release(SDL_SCANCODE_UP);
    for (int i = 0; i < InputChord::SequenceTimeoutFrames + 5; ++i) {
        update();
        finishFrame();
    }
    press(SDL_SCANCODE_DOWN);
    check(update().empty(), "A sequence is abandoned after the timeout");
    finishFrame();
    release(SDL_SCANCODE_DOWN);

    // 6d. Disabled input resets a sequence.
    press(SDL_SCANCODE_UP);
    update();
    finishFrame();
    release(SDL_SCANCODE_UP);
    update(false);
    press(SDL_SCANCODE_DOWN);
    check(update().empty(), "Disabling input resets a half-finished sequence");
    finishFrame();
    release(SDL_SCANCODE_DOWN);

    // 7. Duplicate names rejected.
    wipe();
    chords.addSequence("sequence", {SDL_SCANCODE_UP});
    bool duplicateRejected = false;
    try { chords.addChord("sequence", {SDL_SCANCODE_A}); } catch (const std::invalid_argument &) { duplicateRejected = true; }
    check(duplicateRejected, "Duplicate binding names are rejected");

    // 8. Empty names and key lists rejected.
    bool emptyNameRejected = false;
    try { chords.addChord("", {SDL_SCANCODE_A}); } catch (const std::invalid_argument &) { emptyNameRejected = true; }
    check(emptyNameRejected, "Empty binding names are rejected");
    bool emptyKeysRejected = false;
    try { chords.addChord("empty_keys", {}); } catch (const std::invalid_argument &) { emptyKeysRejected = true; }
    check(emptyKeysRejected, "Empty key collections are rejected");
    check(chords.size() == 1, "Rejected bindings are not registered");

    // 9. Remove and clear.
    chords.remove("sequence");
    check(!chords.has("sequence"), "remove() unregisters a binding");
    bool again = false;
    try { chords.addChord("sequence", {SDL_SCANCODE_A}); } catch (const std::invalid_argument &) { again = true; }
    check(!again, "A removed binding name can be registered again");
    chords.clear();
    check(chords.size() == 0, "clear() removes everything");

    // 10. reset() clears activation state.
    wipe();
    chords.addChord("reset_test", {SDL_SCANCODE_A});
    press(SDL_SCANCODE_A);
    check(update().size() == 1, "A single-key chord activates");
    finishFrame();
    check(update().empty(), "...and not again while held");
    chords.reset();
    check(update().size() == 1, "Explicit reset clears the previous activation state");

    if (failures != 0) {
        std::cerr << failures << " input chord checks failed.\n";
        return 1;
    }
    std::cout << "All input chord checks passed.\n";
    return 0;
}
