#pragma once

#include "EventManager.h"
#include "GameObjectManager.h"
#include "InputChord.h"

#include <SDL3/SDL.h>

#include <functional>
#include <string>
#include <vector>

namespace InputActions
{
    inline constexpr const char *MoveX = "move_x";
    inline constexpr const char *JumpHeld = "jump_held";
    inline constexpr const char *JumpPressed = "jump_pressed";
    inline constexpr const char *DownHeld = "down_held";
    inline constexpr const char *Dash = "dash"; // example chord action name (A + Space)
}

// Input PRODUCER. Reads keys and raises Input events; never changes game state.
//
// Chords PRESERVE their keys' individual actions: with A + Space held, move_x and jump_held keep
// being raised as usual and the chord's own action is raised in addition, once per activation.
namespace InputSystem
{
    // Where key state comes from. poll(events, objects) uses the real keyboard (Input class);
    // the tests pass a fake one.
    struct KeySource
    {
        std::function<bool(SDL_Scancode)> down;     // held this frame
        std::function<bool(SDL_Scancode)> justDown; // went down this frame
    };

    // Register bindings before polling begins. The action name becomes the InputEvent action.
    // Throws std::invalid_argument for an empty name, no keys, or a duplicate name.
    void registerChord(const std::string &action, const std::vector<SDL_Scancode> &keys);
    void registerSequence(const std::string &action, const std::vector<SDL_Scancode> &keys);

    void removeChord(const std::string &action);
    void clearChords();
    void resetChords(); // call from a replay restore callback

    // Call once per frame after replay->beginFrame() and before replay->dispatch(ReplayPhase::Input).
    void poll(EventManager &events, GameObjectManager &objects);

    // Same, with explicit key state (used by the tests).
    void poll(EventManager &events, GameObjectManager &objects, const KeySource &keys);
}