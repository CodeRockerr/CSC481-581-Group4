#pragma once
#include "Component.h"

// What the player wants to do this frame. Written ONLY by the Input event handler
// (GameplayEvents), read by PlayerInput's helper functions.
// Replay state: games must call PlayerIntent::reset() in their replay restore hook.
struct PlayerIntent : Component
{
    float moveX = 0.0f;         // -1, 0, +1
    bool jumpHeld = false;
    bool jumpPressed = false;   // key-down edge for THIS frame
    bool downHeld = false;

    void reset() { *this = PlayerIntent{}; }
};