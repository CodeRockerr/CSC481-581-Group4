#include "InputSystem.h"

#include "Components.h"
#include "EventObjects.h"
#include "Input.h"
#include "PlayerIntent.h"

namespace
{
    template <std::size_t N>
    bool anyPressed(const SDL_Scancode (&keys)[N])
    {
        for (SDL_Scancode key : keys)
            if (Input::isKeyPressed(key))
                return true;
        return false;
    }

    template <std::size_t N>
    bool anyJustPressed(const SDL_Scancode (&keys)[N])
    {
        for (SDL_Scancode key : keys)
            if (Input::isKeyJustPressed(key))
                return true;
        return false;
    }

    void send(EventManager &events, const EventObject &target, const char *action, float value)
    {
        Event event;
        event.type = EventTypes::Input;
        event.priority = EventPriority::High;
        event.target = target;
        event.payload = InputEvent{action, value};
        events.raise(std::move(event));
    }
}

void InputSystem::poll(EventManager &events, GameObjectManager &objects)
{
    objects.forEach<PlayerInput, PlayerIntent>(
        [&](GameObject &object, PlayerInput &input, PlayerIntent &)
        {
            const EventObject target = EventObjects::of(object);

            // Raw key reads (NOT PlayerInput's helpers, which now read PlayerIntent).
            float axis = 0.0f;
            bool jumpHeld = false, jumpPressed = false, downHeld = false;
            if (input.enabled)
            {
                if (anyPressed(input.left))
                    axis -= 1.0f;
                if (anyPressed(input.right))
                    axis += 1.0f;
                jumpHeld = anyPressed(input.jump);
                jumpPressed = anyJustPressed(input.jump);
                downHeld = anyPressed(input.down);
            }

            // Every action is sent every frame so a replay never depends on prior held state.
            send(events, target, InputActions::MoveX, axis);
            send(events, target, InputActions::JumpHeld, jumpHeld ? 1.0f : 0.0f);
            send(events, target, InputActions::JumpPressed, jumpPressed ? 1.0f : 0.0f);
            send(events, target, InputActions::DownHeld, downHeld ? 1.0f : 0.0f);
        });
}