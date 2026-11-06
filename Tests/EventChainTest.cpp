// Local event-chain tests for Yeva's gameplay producers and handlers.
// Covers input intent, collision/death/spawn, re-arming, rules overrides,
// out-of-bounds handling, listener cleanup, and replay safety.

#include "Camera.h"
#include "Components.h"
#include "DeathZone.h"
#include "EventManager.h"
#include "EventObjects.h"
#include "GameObjectManager.h"
#include "GamePlayEvents.h"
#include "InputSystem.h"
#include "PlayerIntent.h"
#include "SpawnPoint.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::fprintf(stderr, "FAILED %s:%d  %s\n",                    \
                         __FILE__, __LINE__, #cond);                        \
            std::exit(1);                                                  \
        }                                                                  \
    } while (0)
    
namespace
{
    EventTime fakeNow = 1'000'000; // microseconds; fixed so every raised event is immediately due

    struct World
    {
        EventManager events{[]
                            { return fakeNow; }};
        GameObjectManager objects;
        Camera camera;
        std::unique_ptr<GameplayEvents> gameplay;
        std::uint32_t playerId = 0;
        std::uint32_t zoneId = 0;
        std::vector<EventType> seen;

        World()
        {
            events.subscribeAll([this](const Event &e)
                                { seen.push_back(e.type); });
            gameplay = std::make_unique<GameplayEvents>(events, objects, camera);

            GameObject &player = objects.create("player");
            player.add<Transform>(0.0f, 0.0f, 10.0f, 10.0f);
            player.add<Collider>(0.0f, 0.0f, 0.0f, 0.0f);
            PhysicsBody &body = player.add<PhysicsBody>(0, true);
            body.velocityX = 5.0f;
            body.velocityY = 5.0f;
            player.add<PlayerInput>(300.0f, 500.0f);
            player.add<PlayerIntent>();
            playerId = player.getId();

            GameObject &zone = objects.create("zone");
            zone.add<Transform>(0.0f, 0.0f, 10.0f, 10.0f);
            zone.add<Collider>(0.0f, 0.0f, 0.0f, 0.0f, true);
            zone.add<DeathZone>("home");
            zoneId = zone.getId();

            GameObject &home = objects.create("home");
            home.add<Transform>(500.0f, 500.0f, 24.0f, 24.0f);
            home.add<SpawnPoint>("home");
        }

        GameObject &player() { return *objects.find(playerId); }
        void moveTo(float x, float y)
        {
            player().get<Transform>()->x = x;
            player().get<Transform>()->y = y;
        }
        int count(EventType type) const
        {
            int n = 0;
            for (auto t : seen)
                n += t == type;
            return n;
        }
    };

    void testInputBecomesIntent()
    {
        World w;
        auto send = [&](const char *action, float value)
        {
            Event e;
            e.type = EventTypes::Input;
            e.target = EventObjects::of(w.player());
            e.payload = InputEvent{action, value};
            w.events.raise(std::move(e));
        };
        send(InputActions::MoveX, -1.0f);
        send(InputActions::JumpPressed, 1.0f);
        CHECK(w.player().get<PlayerIntent>()->moveX == 0.0f); // not applied until dispatch
        w.events.dispatch();
        CHECK(w.player().get<PlayerIntent>()->moveX == -1.0f);
        CHECK(w.player().get<PlayerIntent>()->jumpPressed);
        send(InputActions::JumpPressed, 0.0f);
        w.events.dispatch();
        CHECK(!w.player().get<PlayerIntent>()->jumpPressed);
    }

    void testCollisionDeathSpawnChain()
    {
        World w;
        w.gameplay->produceCollisions(w.playerId);
        CHECK(w.events.pending() >= 1);
        w.events.dispatch();

        // Collision (zone) -> Death -> Spawn, all in one drain, in causal order.
        int collision = -1, death = -1, spawn = -1;
        for (std::size_t i = 0; i < w.seen.size(); ++i)
        {
            if (w.seen[i] == EventTypes::Collision && collision < 0) collision = static_cast<int>(i);
            if (w.seen[i] == EventTypes::Death) death = static_cast<int>(i);
            if (w.seen[i] == EventTypes::Spawn) spawn = static_cast<int>(i);
        }
        CHECK(collision >= 0 && death > collision && spawn > death);
        CHECK(w.count(EventTypes::Death) == 1);
        CHECK(w.count(EventTypes::Spawn) == 1);

        const Transform *t = w.player().get<Transform>();
        CHECK(t->x == 500.0f && t->y == 500.0f);
        const PhysicsBody *b = w.player().get<PhysicsBody>();
        CHECK(b->velocityX == 0.0f && b->velocityY == 0.0f);
        CHECK(!w.objects.find(w.zoneId)->get<DeathZone>()->armed);
    }

    void testEnterOnlyAndRearm()
    {
        World w;
        w.gameplay->produceCollisions(w.playerId);
        w.gameplay->produceCollisions(w.playerId); // still overlapping: no second event
        CHECK(w.events.pending() == 1);
        w.events.dispatch();
        CHECK(w.count(EventTypes::Death) == 1);

        w.gameplay->produceCollisions(w.playerId); // player now at the spawn point, left the zone
        CHECK(w.objects.find(w.zoneId)->get<DeathZone>()->armed);

        w.moveTo(0.0f, 0.0f); // walk back in: dies again
        w.gameplay->produceCollisions(w.playerId);
        w.events.dispatch();
        CHECK(w.count(EventTypes::Death) == 2);
    }

    void testRulesOverride()
    {
        World w;
        int deaths = 0, spawned = 0;
        GameplayEvents::Rules rules;
        rules.onDeath = [&](GameObject &, const Event &) -> std::optional<std::string>
        {
            ++deaths;
            return std::nullopt; // game over: no respawn
        };
        rules.onSpawned = [&](GameObject &, const Event &)
        { ++spawned; };
        w.gameplay->setRules(rules);
        w.gameplay->produceCollisions(w.playerId);
        w.events.dispatch();
        CHECK(deaths == 1 && spawned == 0);
        CHECK(w.count(EventTypes::Spawn) == 0);
        CHECK(w.player().get<Transform>()->x == 0.0f);

        // Custom respawn target + onSpawned hook.
        rules.onDeath = [&](GameObject &, const Event &) -> std::optional<std::string>
        { return std::string("home"); };
        w.gameplay->setRules(rules);
        w.gameplay->resetTransientState();
        w.gameplay->produceCollisions(w.playerId);
        w.events.dispatch();
        CHECK(spawned == 1);
        CHECK(w.player().get<Transform>()->x == 500.0f);
    }

    void testOutOfBoundsLatch()
    {
        World w;
        w.objects.find(w.zoneId)->active = false; // isolate from the death zone
        w.moveTo(0.0f, 5000.0f);
        w.gameplay->produceOutOfBounds(w.playerId, 2000.0f);
        w.gameplay->produceOutOfBounds(w.playerId, 2000.0f); // latched: no duplicate
        w.events.dispatch();
        CHECK(w.count(EventTypes::Death) == 1);
        CHECK(w.count(EventTypes::Spawn) == 1); // default rule respawns at the first spawn point

        w.gameplay->produceOutOfBounds(w.playerId, 2000.0f); // back in bounds: latch resets
        w.moveTo(0.0f, 5000.0f);
        w.gameplay->produceOutOfBounds(w.playerId, 2000.0f);
        w.events.dispatch();
        CHECK(w.count(EventTypes::Death) == 2);
    }

    void testListenersRemovedOnDestroy()
    {
        World w;
        w.gameplay.reset(); // unsubscribes
        Event e;
        e.type = EventTypes::Death;
        e.target = EventObjects::of(w.player());
        e.payload = DeathEvent{"test"};
        w.events.raise(std::move(e));
        w.events.dispatch(); // must not crash or spawn
        CHECK(w.count(EventTypes::Spawn) == 0);
    }

    void testDestroyedTargetIsIgnored()
    {
        World w;
        Event e;
        e.type = EventTypes::Death;
        e.target = {0, 99999}; // no such object
        e.payload = DeathEvent{"ghost"};
        w.events.raise(std::move(e));
        w.events.dispatch();
        CHECK(w.count(EventTypes::Spawn) == 0);
    }

    void testReplaySafety()
    {
        World w;
        w.events.setPlaybackMode(true);
        // Live input is dropped during playback...
        Event input;
        input.type = EventTypes::Input;
        input.target = EventObjects::of(w.player());
        input.payload = InputEvent{InputActions::MoveX, 1.0f};
        w.events.raise(std::move(input));
        CHECK(w.events.pending() == 0);
        // ...but regenerated gameplay events still flow and are tagged as Replay origin.
        w.gameplay->produceCollisions(w.playerId);
        auto pending = w.events.getPendingEvents();
        CHECK(!pending.empty());
        for (const Event &e : pending)
            CHECK(e.origin == EventOrigin::Replay && !e.shareable);
        w.events.dispatch();
        CHECK(w.player().get<Transform>()->x == 500.0f);
        w.events.setPlaybackMode(false);
    }
}

int main()
{
    testInputBecomesIntent();
    testCollisionDeathSpawnChain();
    testEnterOnlyAndRearm();
    testRulesOverride();
    testOutOfBoundsLatch();
    testListenersRemovedOnDestroy();
    testDestroyedTargetIsIgnored();
    testReplaySafety();
    std::puts("EventChainTest: all passed");
    return 0;
}