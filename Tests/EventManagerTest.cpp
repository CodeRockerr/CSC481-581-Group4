#include "EventManager.h"
#include "Timeline.h"
#include <atomic>
#include <iostream>
#include <set>
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

    Event makeEvent(EventType type, EventTime timestamp = -1, EventPriority priority = EventPriority::Normal)
    {
        Event event;
        event.type = type;
        event.timestamp = timestamp;
        event.priority = priority;
        return event;
    }

    void ordering()
    {
        EventTime time = 100;
        EventManager events([&]() { return time; }, 9);
        std::vector<uint64_t> order;
        events.subscribeAll([&](const Event &event) { order.push_back(event.id.sequence); });
        const auto future = events.raise(makeEvent(100, 200, EventPriority::Critical));
        const auto first = events.raise(makeEvent(100, 50, EventPriority::Low));
        const auto normal1 = events.raise(makeEvent(100));
        const auto high = events.raise(makeEvent(100, 100, EventPriority::High));
        const auto normal2 = events.raise(makeEvent(100));
        require(first.ownerId == 9 && order.empty(), "Raising must assign identity without invoking handlers");
        require(events.dispatch() == 4, "Only due events should dispatch");
        require(order == std::vector<uint64_t>{first.sequence, high.sequence, normal1.sequence, normal2.sequence},
                "Timestamp, priority and FIFO ordering failed");
        require(events.pending() == 1, "Future event was lost");
        const auto pending = events.getPendingEvents();
        require(pending.size() == 1 && pending.front().id == future, "Pending-event snapshot changed the queue");
        time = 199;
        require(events.dispatch() == 0, "Future event dispatched early");
        time = 200;
        require(events.dispatch() == 1 && order.back() == future.sequence, "Scheduled event did not dispatch");
        auto remote = makeEvent(100);
        remote.id = {72, 91};
        remote.origin = EventOrigin::Network;
        require(events.raise(remote) == remote.id, "Remote event identity must be preserved");
        events.clear();
        require(events.pending() == 0, "Queue clearing failed");
    }

    void listenersAndChains()
    {
        EventManager events([]() { return 100; });
        int removedCalls = 0, spawnCalls = 0, deaths = 0;
        EventManager::Subscription removed = 0;
        events.subscribe(EventTypes::Collision, [&](const Event &event)
        {
            events.unsubscribe(removed);
            Event death = makeEvent(EventTypes::Death);
            death.target = event.target;
            death.payload = DeathEvent{"hazard"};
            events.raise(death);
        });
        removed = events.subscribe(EventTypes::Collision, [&](const Event &) { ++removedCalls; });
        events.subscribe(EventTypes::Death, [&](const Event &event)
        {
            require(std::get<DeathEvent>(event.payload).reason == "hazard", "Death payload changed");
            ++deaths;
            Event spawn = makeEvent(EventTypes::Spawn);
            spawn.target = event.target;
            spawn.payload = SpawnEvent{"checkpoint"};
            events.raise(spawn);
        });
        events.subscribe(EventTypes::Spawn, [&](const Event &event)
        {
            require(event.target == EventObject{3, 7}, "Object identity was lost in the event chain");
            require(std::get<SpawnEvent>(event.payload).spawnPoint == "checkpoint", "Spawn payload changed");
            ++spawnCalls;
        });
        Event collision = makeEvent(EventTypes::Collision);
        collision.target = {3, 7};
        collision.payload = CollisionEvent{0, -1, true};
        events.raise(collision);
        require(events.dispatch() == 3 && deaths == 1 && spawnCalls == 1 && removedCalls == 0,
                "Death/spawn chaining or unsubscribe during dispatch failed");
        const auto token = events.subscribe(199, [&](const Event &) { ++removedCalls; });
        require(events.unsubscribe(token) && !events.unsubscribe(token), "Unsubscribe must report whether a listener existed");
        Event custom = makeEvent(199);
        custom.payload = EventFields{{"name", std::string("door")}, {"open", true}, {"count", int64_t{2}}};
        int customCalls = 0;
        events.subscribe(199, [&](const Event &event)
        {
            require(std::get<bool>(std::get<EventFields>(event.payload).at("open")), "Custom payload failed");
            ++customCalls;
        });
        events.raise(custom);
        events.dispatch();
        require(customCalls == 1, "Custom event types must not require an engine switch statement");
    }

    void producers()
    {
        EventManager events([]() { return 100; }, 4);
        const auto consumer = std::this_thread::get_id();
        std::set<uint64_t> ids;
        events.subscribe(100, [&](const Event &event)
        {
            require(std::this_thread::get_id() == consumer, "A producer thread executed a handler");
            ids.insert(event.id.sequence);
        });
        std::vector<std::thread> threads;
        for (int i = 0; i < 4; ++i)
            threads.emplace_back([&]()
            {
                for (int n = 0; n < 200; ++n)
                    events.raise(makeEvent(100));
            });
        for (auto &thread : threads)
            thread.join();
        require(ids.empty() && events.pending() == 800, "Concurrent raising must only enqueue");
        require(events.dispatch() == 800 && ids.size() == 800, "Concurrent events or IDs were lost");
    }

    void dispatchGuards()
    {
        EventManager events([]() { return 0; });
        bool rejected = false;
        events.subscribe(100, [&](const Event &)
        {
            try { events.dispatch(); }
            catch (const std::logic_error &) { rejected = true; }
        });
        events.raise(makeEvent(100));
        events.dispatch();
        require(rejected, "Recursive dispatch must be rejected");
        const auto cycle = events.subscribe(101, [&](const Event &) { events.raise(makeEvent(101)); });
        events.raise(makeEvent(101));
        require(events.dispatch(12) == 12 && events.pending() == 1, "Dispatch budget did not bound a cycle");
        events.unsubscribe(cycle);
        events.clear();
        const auto broken = events.subscribe(102, [](const Event &) { throw std::runtime_error("handler"); });
        events.raise(makeEvent(102));
        bool failed = false;
        try { events.dispatch(); }
        catch (const std::runtime_error &) { failed = true; }
        events.unsubscribe(broken);
        events.raise(makeEvent(100));
        require(failed && events.dispatch() == 1, "A handler exception left the dispatcher locked");
    }

    void replayGateAndClock()
    {
        EventTime time = 100;
        EventManager events([&]() { return time; });
        events.setTime(1000);
        time = 500;
        require(events.now() == 1000, "Manual event time should be stable");
        events.resumeTime();
        require(events.now() == 1000, "Resuming the clock should not jump");
        time = 600;
        require(events.now() == 1100, "Timeline time should advance after resuming");
        int inputCalls = 0, spawnCalls = 0;
        events.subscribe(EventTypes::Input, [&](const Event &) { ++inputCalls; });
        events.subscribe(EventTypes::Spawn, [&](const Event &event)
        {
            require(event.origin == EventOrigin::Replay && !event.shareable, "Replay child events must remain local");
            ++spawnCalls;
        });
        events.subscribe(EventTypes::Death, [&](const Event &)
        {
            auto spawn = makeEvent(EventTypes::Spawn);
            spawn.shareable = true;
            events.raise(spawn);
        });
        events.setPlaybackMode(true);
        require(events.raise(makeEvent(EventTypes::Input)).sequence == 0, "Live input must be ignored during playback");
        auto network = makeEvent(EventTypes::Spawn);
        network.origin = EventOrigin::Network;
        require(events.raise(network).sequence == 0, "Live network events must not alter local replay");
        auto recorded = makeEvent(EventTypes::Input);
        recorded.origin = EventOrigin::Replay;
        events.raise(recorded);
        auto death = makeEvent(EventTypes::Death);
        death.shareable = true;
        events.raise(death);
        events.dispatch();
        require(inputCalls == 1 && spawnCalls == 1, "Replay input or child handling failed");
        events.setPlaybackMode(false);
        death.origin = EventOrigin::Replay;
        events.raise(death);
        events.dispatch();
        require(spawnCalls == 2, "Replay origin must propagate through later child events");
    }
}

int main()
{
    try
    {
        ordering();
        listenersAndChains();
        producers();
        dispatchGuards();
        replayGateAndClock();
        Timeline timeline;
        Timeline gameTime(&timeline, 1000);
        EventManager timelineEvents(gameTime);
        gameTime.pause();
        const auto paused = timelineEvents.now();
        SDL_Delay(5);
        require(timelineEvents.now() == paused, "Event timestamps must respect their Timeline's pause");
        gameTime.unpause();
        SDL_Delay(5);
        require(timelineEvents.now() > paused, "Event timestamps must resume with their Timeline");
        std::cout << "PASS: event ordering, payloads, listeners, chains, concurrent producers, dispatch guards and replay isolation\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
