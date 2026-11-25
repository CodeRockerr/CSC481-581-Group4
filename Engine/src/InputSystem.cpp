#include "InputSystem.h"

#include "Components.h"
#include "EventObjects.h"
#include "Input.h"
#include "PlayerIntent.h"

#include <cstddef>
#include <utility>

namespace
{
    InputChord inputChords;

    template <std::size_t N, typename Check>
    bool anyOf(const SDL_Scancode (&keys)[N], const Check &check)
    {
        for (SDL_Scancode key : keys)
        {
            if (check(key))
            {
                return true;
            }
        }
        return false;
    }

    void send(EventManager &events, const EventObject &target, const std::string &action, float value)
    {
        Event event;
        event.type = EventTypes::Input;
        event.priority = EventPriority::High;
        event.target = target;
        event.payload = InputEvent{action, value};
        events.raise(std::move(event));
    }
}

void InputSystem::registerChord(const std::string &action, const std::vector<SDL_Scancode> &keys)
{
    inputChords.addChord(action, keys);
}

void InputSystem::registerSequence(const std::string &action, const std::vector<SDL_Scancode> &keys)
{
    inputChords.addSequence(action, keys);
}

void InputSystem::removeChord(const std::string &action)
{
    inputChords.remove(action);
}

void InputSystem::clearChords()
{
    inputChords.clear();
}

void InputSystem::resetChords()
{
    inputChords.reset();
}

void InputSystem::poll(EventManager &events, GameObjectManager &objects)
{
    const KeySource live{
        [](SDL_Scancode key) { return Input::isKeyPressed(key); },
        [](SDL_Scancode key) { return Input::isKeyJustPressed(key); }};
    poll(events, objects, live);
}

void InputSystem::poll(EventManager &events, GameObjectManager &objects, const KeySource &keys)
{
    const bool playback = events.isPlaybackMode();

    // Chord events go to the first player whose input is enabled (one chord = one event,
    // however many players there are).
    bool haveChordTarget = false;
    EventObject chordTarget{};
    objects.forEach<PlayerInput, PlayerIntent>(
        [&](GameObject &object, PlayerInput &input, PlayerIntent &)
        {
            if (input.enabled && !haveChordTarget)
            {
                chordTarget = EventObjects::of(object);
                haveChordTarget = true;
            }
        });

    // Chord state is always updated, even when input is disabled or a replay is playing, so a
    // chord held through that period cannot fire the moment live input comes back. Actions are
    // only emitted from live, enabled input.
    const std::vector<std::string> triggered =
        inputChords.update(keys.down, keys.justDown, haveChordTarget && !playback);

    objects.forEach<PlayerInput, PlayerIntent>(
        [&](GameObject &object, PlayerInput &input, PlayerIntent &)
        {
            const EventObject target = EventObjects::of(object);

            // Raw key reads, NOT PlayerInput's helpers (those read PlayerIntent).
            float axis = 0.0f;
            bool jumpHeld = false;
            bool jumpPressed = false;
            bool downHeld = false;

            if (input.enabled)
            {
                if (anyOf(input.left, keys.down))
                    axis -= 1.0f;
                if (anyOf(input.right, keys.down))
                    axis += 1.0f;
                jumpHeld = anyOf(input.jump, keys.down);
                jumpPressed = anyOf(input.jump, keys.justDown);
                downHeld = anyOf(input.down, keys.down);
            }

            // Every action is sent every frame so a replay never depends on earlier held state.
            // Playback isolation is EventManager's job: it drops live Input events.
            send(events, target, InputActions::MoveX, axis);
            send(events, target, InputActions::JumpHeld, jumpHeld ? 1.0f : 0.0f);
            send(events, target, InputActions::JumpPressed, jumpPressed ? 1.0f : 0.0f);
            send(events, target, InputActions::DownHeld, downHeld ? 1.0f : 0.0f);
        });

    // The component actions above are NOT consumed. Chord actions follow them in the same frame.
    if (haveChordTarget && !playback)
    {
        for (const std::string &action : triggered)
        {
            send(events, chordTarget, action, 1.0f);
        }
    }
}