// Chords through the real input path: InputSystem::poll -> EventManager -> a game-style handler,
// plus replay. Keys are faked, so no window is needed.
#include "Components.h"
#include "EventManager.h"
#include "EventObjects.h"
#include "GameObjectManager.h"
#include "InputSystem.h"
#include "PlayerIntent.h"
#include "ReplayController.h"

#include <any>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <string>
#include <utility>
#include <vector>

#define CHECK(cond)                                                                \
    do                                                                             \
    {                                                                              \
        if (!(cond))                                                               \
        {                                                                          \
            std::fprintf(stderr, "FAILED %s:%d  %s\n", __FILE__, __LINE__, #cond); \
            std::exit(1);                                                          \
        }                                                                          \
    } while (0)

namespace
{
    EventTime fakeNow = 1'000'000; // fixed, so every raised event is immediately due

    struct World
    {
        EventManager events{[]
                            { return fakeNow; }};
        GameObjectManager objects;
        std::set<SDL_Scancode> held;
        std::set<SDL_Scancode> justPressed;
        std::vector<std::pair<std::string, float>> seen; // every Input event, in dispatch order
        std::uint32_t playerId = 0;
        int dashes = 0;

        World()
        {
            InputSystem::clearChords();
            InputSystem::registerChord(InputActions::Dash, {SDL_SCANCODE_A, SDL_SCANCODE_SPACE});

            GameObject &player = objects.create("player");
            player.add<Transform>(0.0f, 0.0f, 10.0f, 10.0f);
            player.add<PhysicsBody>(0, false);
            player.add<PlayerInput>(300.0f, 500.0f);
            player.add<PlayerIntent>();
            playerId = player.getId();

            events.subscribe(EventTypes::Input, [this](const Event &e)
                             {
                                 const auto *in = std::get_if<InputEvent>(&e.payload);
                                 if (!in)
                                     return;
                                 seen.emplace_back(in->action, in->value);
                                 // The game's handler: the chord's BEHAVIOUR lives here, not in InputSystem.
                                 if (in->action == InputActions::Dash)
                                 {
                                     ++dashes;
                                     if (GameObject *who = EventObjects::resolve(objects, e.target))
                                         if (PhysicsBody *body = who->get<PhysicsBody>())
                                             body->velocityX = -800.0f; // A is "left"
                                 }
                             });
        }

        ~World() { InputSystem::clearChords(); }

        InputSystem::KeySource keys()
        {
            return {[this](SDL_Scancode k)
                    { return held.count(k) != 0; },
                    [this](SDL_Scancode k)
                    { return justPressed.count(k) != 0; }};
        }

        // One frame outside replay.
        void frame()
        {
            InputSystem::poll(events, objects, keys());
            events.dispatch();
            justPressed.clear();
        }

        void press(SDL_Scancode k)
        {
            held.insert(k);
            justPressed.insert(k);
        }
        void release(SDL_Scancode k) { held.erase(k); }

        PhysicsBody &body() { return *objects.find(playerId)->get<PhysicsBody>(); }

        int count(const std::string &action) const
        {
            int n = 0;
            for (const auto &s : seen)
                n += s.first == action;
            return n;
        }
        float last(const std::string &action) const
        {
            float v = -99.0f;
            for (const auto &s : seen)
                if (s.first == action)
                    v = s.second;
            return v;
        }
    };

    // Each component key already does its own job.
    void testIndividualKeys()
    {
        World w;
        w.press(SDL_SCANCODE_A);
        w.frame();
        CHECK(w.last(InputActions::MoveX) == -1.0f);
        CHECK(w.last(InputActions::JumpHeld) == 0.0f);
        CHECK(w.dashes == 0);

        w.release(SDL_SCANCODE_A);
        w.press(SDL_SCANCODE_SPACE);
        w.frame();
        CHECK(w.last(InputActions::MoveX) == 0.0f);
        CHECK(w.last(InputActions::JumpHeld) == 1.0f);
        CHECK(w.last(InputActions::JumpPressed) == 1.0f);
        CHECK(w.dashes == 0);

        // The handler path from the earlier milestone is untouched: intent is not set here because
        // GameplayEvents is not part of this test, so just confirm no chord leaked through.
        CHECK(w.count(InputActions::Dash) == 0);
    }

    void testChordActivatesOnceAndKeepsIndividualActions()
    {
        World w;
        w.press(SDL_SCANCODE_A);
        w.frame();
        CHECK(w.count(InputActions::Dash) == 0);

        w.press(SDL_SCANCODE_SPACE);
        w.frame();
        CHECK(w.count(InputActions::Dash) == 1);
        CHECK(w.dashes == 1);
        CHECK(w.body().velocityX == -800.0f); // the game's handler ran
        // Preserved, not consumed: A still moves and Space still jumps in the same frame.
        CHECK(w.last(InputActions::MoveX) == -1.0f);
        CHECK(w.last(InputActions::JumpHeld) == 1.0f);
        CHECK(w.last(InputActions::JumpPressed) == 1.0f);
        // The chord comes after this frame's component events.
        CHECK(w.seen.back().first == InputActions::Dash);
    }

    void testHeldDoesNotRepeatAndReleaseRetriggers()
    {
        World w;
        w.press(SDL_SCANCODE_A);
        w.press(SDL_SCANCODE_SPACE);
        w.frame();
        for (int i = 0; i < 10; ++i)
            w.frame();
        CHECK(w.dashes == 1); // held for 11 frames, one dash

        w.release(SDL_SCANCODE_SPACE);
        w.frame();
        CHECK(w.dashes == 1);
        w.press(SDL_SCANCODE_SPACE);
        w.frame();
        CHECK(w.dashes == 2);

        w.release(SDL_SCANCODE_A);
        w.release(SDL_SCANCODE_SPACE);
        w.frame();
        w.press(SDL_SCANCODE_SPACE);
        w.press(SDL_SCANCODE_A);
        w.frame();
        CHECK(w.dashes == 3); // either order
    }

    void testDisabledInput()
    {
        World w;
        w.objects.find(w.playerId)->get<PlayerInput>()->enabled = false;
        w.press(SDL_SCANCODE_A);
        w.press(SDL_SCANCODE_SPACE);
        w.frame();
        CHECK(w.dashes == 0);
        CHECK(w.last(InputActions::MoveX) == 0.0f); // neutral, as before
        CHECK(w.last(InputActions::JumpHeld) == 0.0f);

        // Re-enable while the keys are still held: no sudden dash.
        w.objects.find(w.playerId)->get<PlayerInput>()->enabled = true;
        w.frame();
        CHECK(w.dashes == 0);
        CHECK(w.last(InputActions::MoveX) == -1.0f); // normal controls are back

        // A fresh activation works.
        w.release(SDL_SCANCODE_SPACE);
        w.frame();
        w.press(SDL_SCANCODE_SPACE);
        w.frame();
        CHECK(w.dashes == 1);
    }

    void testTwoPlayersStillOneChordEvent()
    {
        World w;
        GameObject &second = w.objects.create("player2");
        second.add<Transform>(50.0f, 0.0f, 10.0f, 10.0f);
        second.add<PlayerInput>(300.0f, 500.0f);
        second.add<PlayerIntent>();

        w.press(SDL_SCANCODE_A);
        w.press(SDL_SCANCODE_SPACE);
        w.frame();
        CHECK(w.count(InputActions::Dash) == 1);
    }

    void testBindingsCanBeRemoved()
    {
        World w;
        InputSystem::removeChord(InputActions::Dash);
        w.press(SDL_SCANCODE_A);
        w.press(SDL_SCANCODE_SPACE);
        w.frame();
        CHECK(w.dashes == 0);
        CHECK(w.last(InputActions::MoveX) == -1.0f); // individual controls unaffected
    }

    // Live keys during playback never make chord events.
    void testPlaybackModeDropsLiveChords()
    {
        World w;
        w.events.setPlaybackMode(true);
        w.press(SDL_SCANCODE_A);
        w.press(SDL_SCANCODE_SPACE);
        InputSystem::poll(w.events, w.objects, w.keys());
        CHECK(w.events.pending() == 0);
        w.events.setPlaybackMode(false);

        // The chord was latched while keys were held, so leaving playback does not fire it.
        w.justPressed.clear();
        w.frame();
        CHECK(w.dashes == 0);
    }

    // Record a chord, play it back: exactly one chord action, from the recording, even if the
    // player is holding the same keys live during playback.
    void testReplayDoesNotDuplicateChord()
    {
        World w;
        ReplayController replay(w.events);
        replay.setStateCallbacks(
            [&]() -> std::any
            { return 0; },
            [&](const std::any &)
            { InputSystem::resetChords(); });

        // Same order as Engine::run.
        auto frame = [&]()
        {
            FrameTiming timing;
            timing.realDelta = 1.0f / 60.0f;
            timing.timelines.push_back({1.0f / 60.0f, 1.0, false});
            replay.beginFrame(timing);
            InputSystem::poll(w.events, w.objects, w.keys());
            replay.dispatch(ReplayPhase::Input);
            replay.dispatch(ReplayPhase::Update);
            replay.dispatch(ReplayPhase::Workers);
            replay.endFrame();
            w.justPressed.clear();
        };

        replay.startRecording();
        frame();
        w.press(SDL_SCANCODE_A);
        w.press(SDL_SCANCODE_SPACE);
        frame(); // chord fires
        frame();
        w.release(SDL_SCANCODE_A);
        w.release(SDL_SCANCODE_SPACE);
        frame();
        CHECK(replay.isRecording());
        CHECK(w.dashes == 1);

        replay.stop();
        replay.startPlayback();
        w.seen.clear();
        w.dashes = 0;
        w.press(SDL_SCANCODE_A); // live keys held DURING playback
        w.press(SDL_SCANCODE_SPACE);
        frame();
        CHECK(replay.isPlaying());
        frame();
        frame();
        frame();
        CHECK(!replay.isPlaying());
        CHECK(w.dashes == 1); // exactly the recorded one, no live duplicate
        CHECK(w.count(InputActions::Dash) == 1);
    }
}

int main()
{
    testIndividualKeys();
    testChordActivatesOnceAndKeepsIndividualActions();
    testHeldDoesNotRepeatAndReleaseRetriggers();
    testDisabledInput();
    testTwoPlayersStillOneChordEvent();
    testBindingsCanBeRemoved();
    testPlaybackModeDropsLiveChords();
    testReplayDoesNotDuplicateChord();
    std::puts("InputChordIntegrationTest: all passed");
    return 0;
}
