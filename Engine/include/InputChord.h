#pragma once

#include <SDL3/SDL.h>

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

// Detects key chords (all keys held together) and, optionally, ordered key sequences.
// It only reports action names; InputSystem turns them into events.
//
// Chord rule: it activates ONCE when the last key goes down. At least one of its keys must be
// released before it can activate again.
class InputChord {
public:
    using KeyCheck = std::function<bool(SDL_Scancode)>;
    using KeyPressedCheck = std::function<bool(SDL_Scancode)>;

    // A sequence is abandoned after this many frames without its next key (about one second at 60 fps).
    static constexpr int SequenceTimeoutFrames = 60;

    // Both throw std::invalid_argument for an empty name, no keys, or a name already in use.
    void addChord(const std::string& name, const std::vector<SDL_Scancode>& keys);
    void addSequence(const std::string& name, const std::vector<SDL_Scancode>& keys);

    void remove(const std::string& name);
    void clear();
    void reset(); // forget activation state; bindings stay registered

    bool has(const std::string& name) const;
    std::size_t size() const { return bindings.size(); }

    // Call once per frame. Returns the actions that activated this frame, in registration order
    // (so the result is the same on every run).
    // When enabled is false, bindings still track key state but emit nothing, so a chord held
    // through a disabled period does not fire the moment input is re-enabled.
    std::vector<std::string> update(
        const KeyCheck& isDown,
        const KeyPressedCheck& wasJustPressed,
        bool enabled = true
    );

private:
    struct Binding {
        std::string name;
        std::vector<SDL_Scancode> keys;
        bool sequence = false;
        bool wasTriggered = false;
        std::size_t sequenceIndex = 0;
        int idleFrames = 0;
    };

    Binding* find(const std::string& name);
    void add(const std::string& name, const std::vector<SDL_Scancode>& keys, bool sequence);

    std::vector<Binding> bindings; // a vector, not a hash map: stable order matters for replay
};