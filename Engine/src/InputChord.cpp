#include "InputChord.h"

#include <algorithm>
#include <stdexcept>

InputChord::Binding* InputChord::find(const std::string& name) {
    for (Binding& binding : bindings) {
        if (binding.name == name) {
            return &binding;
        }
    }
    return nullptr;
}

bool InputChord::has(const std::string& name) const {
    return std::any_of(bindings.begin(), bindings.end(),
                       [&](const Binding& b) { return b.name == name; });
}

void InputChord::add(const std::string& name, const std::vector<SDL_Scancode>& keys, bool sequence) {
    if (name.empty() || keys.empty()) {
        throw std::invalid_argument("Input binding name and keys cannot be empty.");
    }
    if (find(name) != nullptr) {
        throw std::invalid_argument("An input binding with this name already exists.");
    }

    Binding binding;
    binding.name = name;
    binding.keys = keys;
    binding.sequence = sequence;
    bindings.push_back(std::move(binding));
}

void InputChord::addChord(const std::string& name, const std::vector<SDL_Scancode>& keys) {
    add(name, keys, false);
}

void InputChord::addSequence(const std::string& name, const std::vector<SDL_Scancode>& keys) {
    add(name, keys, true);
}

void InputChord::remove(const std::string& name) {
    bindings.erase(std::remove_if(bindings.begin(), bindings.end(),
                                  [&](const Binding& b) { return b.name == name; }),
                   bindings.end());
}

void InputChord::clear() {
    bindings.clear();
}

void InputChord::reset() {
    for (Binding& binding : bindings) {
        binding.wasTriggered = false;
        binding.sequenceIndex = 0;
        binding.idleFrames = 0;
    }
}

std::vector<std::string> InputChord::update(
    const KeyCheck& isDown,
    const KeyPressedCheck& wasJustPressed,
    bool enabled
) {
    std::vector<std::string> triggered;

    for (Binding& binding : bindings) {
        if (binding.sequence) {
            if (!enabled) {
                binding.sequenceIndex = 0;
                binding.idleFrames = 0;
                continue;
            }

            const SDL_Scancode expected = binding.keys[binding.sequenceIndex];

            if (wasJustPressed(expected)) {
                ++binding.sequenceIndex;
                binding.idleFrames = 0;
                if (binding.sequenceIndex >= binding.keys.size()) {
                    triggered.push_back(binding.name);
                    binding.sequenceIndex = 0;
                }
                continue;
            }

            if (binding.sequenceIndex == 0) {
                continue;
            }

            // Part-way through: a different key of the sequence pressed out of order starts over
            // (it counts as a first key if it is one). Quiet frames in between are fine, until
            // the timeout runs out.
            bool wrongKey = false;
            for (SDL_Scancode key : binding.keys) {
                if (key != expected && wasJustPressed(key)) {
                    wrongKey = true;
                    break;
                }
            }

            if (wrongKey) {
                binding.sequenceIndex = wasJustPressed(binding.keys[0]) ? 1 : 0;
                binding.idleFrames = 0;
            } else if (++binding.idleFrames > SequenceTimeoutFrames) {
                binding.sequenceIndex = 0;
                binding.idleFrames = 0;
            }
            continue;
        }

        const bool allDown = std::all_of(binding.keys.begin(), binding.keys.end(),
                                         [&](SDL_Scancode key) { return isDown(key); });

        if (!allDown) {
            binding.wasTriggered = false;
            continue;
        }

        // Track activation even while disabled, so holding a chord through a disabled
        // period cannot trigger it when input is re-enabled.
        if (!binding.wasTriggered) {
            if (enabled) {
                triggered.push_back(binding.name);
            }
            binding.wasTriggered = true;
        }
    }

    return triggered;
}