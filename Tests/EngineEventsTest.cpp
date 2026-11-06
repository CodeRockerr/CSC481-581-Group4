#include "Engine.h"
#include "Components.h"
#include <any>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

namespace
{
    void require(bool condition, const char *message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    struct State
    {
        float x;
        float velocity;
        int notifications;
    };

    Event eventOf(EventType type)
    {
        Event event;
        event.type = type;
        return event;
    }
}

int main()
{
    try
    {
        Engine engine("Event integration test", 320, 200);
        auto &events = engine.getEvents();
        auto &replay = engine.getReplay();
        auto &object = engine.getObjects().create("actor");
        auto &transform = object.add<Transform>(0, 0, 10, 10);
        auto &body = object.add<PhysicsBody>(Engine::PlayerTime, false);
        const auto mainThread = std::this_thread::get_id();
        int notifications = 0, frame = 0, recordedFrames = 0, playedFrames = 0;
        bool requestedPlay = false, verified = false;
        std::vector<float> recordedPositions, playedPositions;
        auto capture = [&]() { return std::any(State{transform.x, body.velocityX, notifications}); };
        replay.setStateCallbacks(capture, [&](const std::any &snapshot)
        {
            const auto state = std::any_cast<State>(snapshot);
            transform.x = state.x;
            body.velocityX = state.velocity;
            notifications = state.notifications;
        });
        events.subscribe(EventTypes::Input, [&](const Event &event)
        {
            require(std::this_thread::get_id() == mainThread, "Input handler ran off the main thread");
            body.velocityX = std::get<InputEvent>(event.payload).value;
        });
        events.subscribe(EventTypes::FirstGame, [&](const Event &)
        {
            require(std::this_thread::get_id() == mainThread, "Worker event handler ran on the worker");
            ++notifications;
            if (replay.isRecording()) recordedPositions.push_back(transform.x);
            if (replay.isPlaying()) playedPositions.push_back(transform.x);
        });
        engine.run([&](float delta)
        {
            require(delta == engine.getFrameDelta(Engine::WorldTime), "Game callback did not receive frame timing");
            ++frame;
            if (frame == 1) events.raise(eventOf(EventTypes::ReplayStart));
            if (replay.isRecording())
            {
                Event input = eventOf(EventTypes::Input);
                input.payload = InputEvent{"horizontal", 20.f + recordedFrames};
                events.raise(input);
                if (++recordedFrames == 5) events.raise(eventOf(EventTypes::ReplayStop));
            }
            if (!replay.isRecording() && recordedFrames == 5 && !requestedPlay)
            {
                requestedPlay = true;
                transform.x = 999;
                body.velocityX = 999;
                events.raise(eventOf(EventTypes::ReplayPlayback));
            }
            if (replay.isPlaying())
            {
                ++playedFrames;
                Event input = eventOf(EventTypes::Input);
                input.payload = InputEvent{"horizontal", 1000};
                require(events.raise(input).sequence == 0, "Live input was accepted during engine playback");
                SDL_Delay(2);
            }
            if (playedFrames == 5 && !replay.isPlaying() && !verified)
            {
                require(recordedPositions.size() == 5 && playedPositions.size() == 5, "Worker phase lost recorded frames");
                for (std::size_t i = 0; i < recordedPositions.size(); ++i)
                    require(std::fabs(recordedPositions[i] - playedPositions[i]) < .00001f,
                            "Native engine physics did not reproduce recorded positions");
                require(engine.getPlayerFramesBuilt() > 0 && engine.getWorldFramesBuilt() > 0,
                        "Engine workers did not run");
                verified = true;
                SDL_Event quit{};
                quit.type = SDL_EVENT_QUIT;
                SDL_PushEvent(&quit);
            }
            require(frame < 100, "Engine replay integration timed out");
        }, [&](float)
        {
            events.raise(eventOf(EventTypes::FirstGame));
        });
        require(verified, "Engine integration exited before verification");
        std::cout << "PASS: main-thread dispatch after worker completion and native-physics replay through Engine::run\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
