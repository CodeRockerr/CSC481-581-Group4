#include "ReplayController.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace
{
    void require(bool condition, const char *message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    Event control(EventType type)
    {
        Event event;
        event.type = type;
        return event;
    }

    Event input(float value)
    {
        Event event;
        event.type = EventTypes::Input;
        event.payload = InputEvent{"horizontal", value};
        return event;
    }

    FrameTiming timing(float delta)
    {
        return {delta, {{delta, 1, false}, {delta, 1, false}}};
    }

    struct State
    {
        float x = 0;
        float velocity = 0;
        float platformPhase = 0;
        float power = .3f;
        int lives = 3;
        int spawns = 0;
    };

    struct Fixture
    {
        EventTime wallTime = 1000;
        EventManager events{[this]() { return wallTime; }};
        ReplayController replay{events};
        State state;
        int deathNotifications = 0;

        Fixture()
        {
            replay.setStateCallbacks([this]() { return std::any(state); },
                                     [this](const std::any &snapshot) { state = std::any_cast<State>(snapshot); });
            events.subscribe(EventTypes::Input, [this](const Event &event)
            {
                state.velocity = std::get<InputEvent>(event.payload).value;
            });
            events.subscribe(EventTypes::Collision, [this](const Event &)
            {
                Event death = control(EventTypes::Death);
                death.payload = DeathEvent{"obstacle"};
                death.shareable = true;
                events.raise(death);
            });
            events.subscribe(EventTypes::Death, [this](const Event &)
            {
                ++deathNotifications;
                --state.lives;
                Event spawn = control(EventTypes::Spawn);
                spawn.payload = SpawnEvent{"checkpoint"};
                spawn.shareable = true;
                events.raise(spawn);
            });
            events.subscribe(EventTypes::Spawn, [this](const Event &)
            {
                state.x = -2;
                state.velocity = 0;
                ++state.spawns;
            });
            events.subscribeAll([](const Event &event)
            {
                require(event.origin != EventOrigin::Replay || !event.shareable,
                        "Playback events must not be sent back to the network");
            });
            events.subscribe(EventTypes::FirstGame, [this](const Event &) { state.platformPhase += 10; });
        }

        void phase(ReplayPhase phase)
        {
            if (replay.isPlaying())
            {
                require(events.raise(input(999)).sequence == 0, "Live gameplay input leaked into playback");
                Event incoming = control(EventTypes::Spawn);
                incoming.origin = EventOrigin::Network;
                require(events.raise(incoming).sequence == 0, "Incoming network events changed local playback");
            }
            replay.dispatch(phase);
        }

        void frame(FrameTiming live, int index = -1)
        {
            wallTime += 500'000;
            replay.beginFrame(live);
            if (!replay.isPlaying())
            {
                if (index == 0) events.raise(input(8));
                if (index == 3) events.raise(input(4));
                if (index == 4) events.raise(control(EventTypes::ReplayStop));
            }
            phase(ReplayPhase::Input);
            const auto &selected = replay.getFrameTiming();
            state.x += state.velocity * selected.timelines[1].delta;
            state.platformPhase += selected.timelines[0].delta;
            state.power = std::max(0.f, state.power - selected.realDelta);
            if (state.x > 1)
            {
                Event collision = control(EventTypes::Collision);
                collision.payload = CollisionEvent{};
                events.raise(collision);
            }
            if (!replay.isPlaying() && index == 1) events.raise(input(12));
            phase(ReplayPhase::Update);
            if (!replay.isPlaying() && index == 3) events.raise(input(-3));
            phase(ReplayPhase::Workers);
            replay.endFrame();
        }
    };

    void compare(const State &a, const State &b)
    {
        require(std::fabs(a.x - b.x) < .00001f && std::fabs(a.velocity - b.velocity) < .00001f,
                "Replay did not reproduce movement and velocity");
        require(std::fabs(a.platformPhase - b.platformPhase) < .00001f && std::fabs(a.power - b.power) < .00001f,
                "Replay did not reproduce autonomous motion and timers");
        require(a.lives == b.lives && a.spawns == b.spawns, "Replay duplicated or lost death/spawn effects");
    }

    void recordedGameplay()
    {
        Fixture f;
        auto scheduled = control(EventTypes::FirstGame);
        scheduled.timestamp = f.events.now() + 150'000;
        f.events.raise(scheduled);
        f.events.raise(control(EventTypes::ReplayStart));
        f.frame(timing(0));
        const float deltas[] = {.04f, .08f, .12f, .05f, .1f};
        for (int i = 0; i < 5; ++i)
        {
            auto frame = timing(deltas[i]);
            if (i == 2)
                frame.timelines[0] = {0, .5, true};
            f.frame(frame, i);
        }
        const auto expected = f.state;
        require(expected.lives == 2 && expected.spawns == 1, "Recording did not exercise the collision/death/spawn chain");
        require(f.replay.getRecording().frames.size() == 5, "Recording did not retain every frame");
        require(f.replay.getRecording().pendingEvents.size() == 1 && expected.platformPhase > 10,
                "Recording must retain pre-existing scheduled gameplay events");
        f.frame(timing(0));
        require(!f.replay.isRecording(), "Stop-recording event was not applied at the frame boundary");
        std::size_t inputs = 0;
        for (const auto &frame : f.replay.getRecording().frames)
            for (const auto &phase : frame.inputs)
                for (const auto &event : phase)
                {
                    require(event.type == EventTypes::Input, "Replay should record input, not derived events or controls");
                    ++inputs;
                }
        require(inputs == 4, "Input events from different dispatch phases were lost");

        for (int attempt = 0; attempt < 2; ++attempt)
        {
            f.events.raise(control(EventTypes::ReplayPlayback));
            f.frame(timing(0));
            f.state = {900, 900, 900, 900, 0, 900};
            const int previousDeaths = f.deathNotifications;
            for (int i = 0; i < 5; ++i)
                f.frame(timing(.9f));
            compare(f.state, expected);
            require(!f.replay.isPlaying() && !f.events.isPlaybackMode(), "Playback did not finish or restore live input");
            require(f.deathNotifications - previousDeaths == 1, "Replay applied a death more than once");
            require(f.replay.getRecording().frames.size() == 5, "Playback must not append to the recording");
        }
        f.events.raise(input(2));
        f.frame(timing(.01f));
        require(f.state.velocity == 2, "Live input did not resume after playback");
    }

    void controlsAndValidation()
    {
        EventTime wallTime = 100;
        EventManager events([&]() { return wallTime; });
        ReplayController replay(events);
        wallTime += 1'000'000;
        require(events.now() == 100, "Startup time must not move queued events ahead of the simulation clock");
        replay.startPlayback();
        replay.beginFrame(timing(0));
        replay.dispatch(ReplayPhase::Input);
        bool duplicate = false;
        try { replay.dispatch(ReplayPhase::Input); }
        catch (const std::logic_error &) { duplicate = true; }
        require(duplicate && !replay.isPlaying(), "Empty playback or duplicate phase handling failed");
        replay.dispatch(ReplayPhase::Update);
        replay.dispatch(ReplayPhase::Workers);
        replay.endFrame();
        replay.startRecording();
        bool missingState = false;
        try { replay.beginFrame(timing(.1f)); }
        catch (const std::logic_error &) { missingState = true; }
        require(missingState && !replay.isRecording(), "Replay must require an initial-state contract");
        bool invalid = false;
        try { replay.beginFrame(timing(-1)); }
        catch (const std::invalid_argument &) { invalid = true; }
        require(invalid, "Invalid frame timing must be rejected");
    }
}

int main()
{
    try
    {
        recordedGameplay();
        controlsAndValidation();
        std::cout << "PASS: replay controls, state restore, recorded timing/phases, repeated playback, regenerated events and live-input isolation\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
