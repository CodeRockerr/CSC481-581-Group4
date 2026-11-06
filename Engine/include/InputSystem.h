#pragma once
#include "EventManager.h"
#include "GameObjectManager.h"

namespace InputActions
{
    inline constexpr const char *MoveX = "move_x";           // value -1 / 0 / +1
    inline constexpr const char *JumpHeld = "jump_held";     // value 0 / 1
    inline constexpr const char *JumpPressed = "jump_pressed"; // value 0 / 1 (edge, this frame)
    inline constexpr const char *DownHeld = "down_held";     // value 0 / 1
}

// Input PRODUCER. Reads polled keys and raises Input events; never touches game state.
// Call once per frame in Engine::run after replay->beginFrame() and before
// replay->dispatch(ReplayPhase::Input) so the recorder files the events in the Input phase.
// During playback EventManager::raise() drops live Input events, so no special casing is needed.
namespace InputSystem
{
    void poll(EventManager &events, GameObjectManager &objects);
}