#include "Camera.h"
#include "Components.h"
#include "DeathZone.h"
#include "EventManager.h"
#include "EventObjects.h"
#include "GameObjectManager.h"
#include "GamePlayEvents.h"
#include "InputSystem.h"
#include "PlayerIntent.h"
#include "ReplayController.h"
#include "SpawnPoint.h"

#include <any>
#include <cstdio>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#define CHECK(cond)                                                              \
    do                                                                           \
    {                                                                            \
        if (!(cond))                                                             \
        {                                                                        \
            std::fprintf(stderr, "FAILED %s:%d  %s\n", __FILE__, __LINE__, #cond); \
            std::exit(1);                                                        \
        }                                                                        \
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
        DeathZone &zone() { return *objects.find(zoneId)->get<DeathZone>(); }
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

    // "Game over" rule: every death is counted, but nothing respawns the player, so the player
    // stays inside the zone (or out of bounds) it died in.
    GameplayEvents::Rules holdPlayerRules(int &deaths)
    {
        GameplayEvents::Rules rules;
        rules.onDeath = [&deaths](GameObject &, const Event &) -> std::optional<std::string>
        {
            ++deaths;
            return std::nullopt;
        };
        return rules;
    }

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
        CHECK(!w.zone().armed);
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
        CHECK(w.zone().armed);

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
        w.gameplay->resetTransientState(); // fresh start, not a replay restore
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
        e.target = {EventObjects::OfflineOwner, 99999}; // offline reference to a local id that does not exist
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

    // touching + every zone's armed flag survive a capture / reset / restore round trip.
    void testTransientStateRoundTrip()
    {
        World w;
        int deaths = 0;
        w.gameplay->setRules(holdPlayerRules(deaths));

        GameObject &second = w.objects.create("zone2");
        second.add<Transform>(900.0f, 900.0f, 10.0f, 10.0f);
        second.add<Collider>(0.0f, 0.0f, 0.0f, 0.0f, true);
        DeathZone &secondZone = second.add<DeathZone>("home");

        w.gameplay->produceCollisions(w.playerId); // player is inside zone 1: triggers it
        w.events.dispatch();
        CHECK(deaths == 1);
        CHECK(!w.zone().armed);
        secondZone.armed = false; // hand-made state so the two zones differ from the default

        const auto saved = w.gameplay->captureTransientState();
        CHECK(saved.touching.count(w.zoneId) == 1);
        CHECK(saved.deathZoneArmed.size() == 2);

        // Control: a plain reset forgets the trigger, so the same position fires again.
        w.gameplay->resetTransientState();
        CHECK(w.zone().armed && secondZone.armed);
        w.gameplay->produceCollisions(w.playerId);
        CHECK(w.events.pending() == 1); // the extra Collision (and then Death) the reviewer described
        w.events.clear();

        // Restore puts back the exact state: no second event for the zone we are still inside.
        w.gameplay->restoreTransientState(saved);
        CHECK(!w.zone().armed);
        CHECK(!secondZone.armed);
        w.gameplay->produceCollisions(w.playerId);
        CHECK(w.events.pending() == 0);
        CHECK(deaths == 1);
    }

    // The out-of-bounds latch is part of the captured state too.
    void testBoundsLatchRoundTrip()
    {
        World w;
        int deaths = 0;
        w.gameplay->setRules(holdPlayerRules(deaths));
        w.objects.find(w.zoneId)->active = false; // isolate from the death zone
        w.moveTo(0.0f, 5000.0f);

        w.gameplay->produceOutOfBounds(w.playerId, 2000.0f);
        w.events.dispatch();
        CHECK(deaths == 1); // player stays below the kill line

        const auto saved = w.gameplay->captureTransientState();
        CHECK(saved.boundsLatched);

        w.gameplay->resetTransientState(); // control: reset forgets the latch
        w.gameplay->produceOutOfBounds(w.playerId, 2000.0f);
        CHECK(w.events.pending() == 1);
        w.events.clear();

        w.gameplay->restoreTransientState(saved);
        w.gameplay->produceOutOfBounds(w.playerId, 2000.0f);
        CHECK(w.events.pending() == 0);
        CHECK(deaths == 1);
    }

    // Offline references and network references are different kinds. Owner 0 is the SERVER's id,
    // not "local", and a replica's local id is unrelated to its network id.
    void testEventObjectReferences()
    {
        World w;

        // Offline object (no NetworkIdentity): offline reference by local id.
        const EventObject offline = EventObjects::of(w.player());
        CHECK(offline.ownerId == EventObjects::OfflineOwner && offline.netId == w.playerId);
        CHECK(EventObjects::isOffline(offline));
        CHECK(EventObjects::resolve(w.objects, offline) == &w.player());

        // Replica owned by client 7 whose local id differs from its network id.
        GameObject &replica = w.objects.create("replica");
        NetworkIdentity &replicaId = replica.add<NetworkIdentity>(7u, NetKind::Unknown, 900u);
        replicaId.isReplica = true;
        CHECK(replica.getId() != 900u);
        const EventObject remote = EventObjects::of(replica);
        CHECK(remote.ownerId == 7u && remote.netId == 900u);
        CHECK(!EventObjects::isOffline(remote));
        CHECK(EventObjects::resolve(w.objects, remote) == &replica);
        // Resolution uses both ids: the local id is not a network id, and the owner must match.
        CHECK(EventObjects::resolve(w.objects, {7u, replica.getId()}) == nullptr);
        CHECK(EventObjects::resolve(w.objects, {8u, 900u}) == nullptr);
        CHECK(EventObjects::resolve(w.objects, {EventObjects::OfflineOwner, 900u}) == nullptr);
        // An offline reference by local id still finds it.
        CHECK(EventObjects::resolve(w.objects, {EventObjects::OfflineOwner, replica.getId()}) == &replica);

        // Server-owned object: owner 0 is a NETWORK owner. Its netId equals the player's local id,
        // and the reference must resolve to the server object, not to the player.
        GameObject &server = w.objects.create("server-owned");
        server.add<NetworkIdentity>(0u, NetKind::Unknown, w.playerId);
        const EventObject serverRef = EventObjects::of(server);
        CHECK(serverRef.ownerId == 0u && serverRef.netId == w.playerId);
        CHECK(!EventObjects::isOffline(serverRef));
        CHECK(EventObjects::resolve(w.objects, serverRef) == &server);
        CHECK(EventObjects::resolve(w.objects, offline) == &w.player());

        // A networked object that has no netId yet is still an offline reference.
        GameObject &pending = w.objects.create("not-yet-replicated");
        pending.add<NetworkIdentity>(7u, NetKind::Unknown);
        CHECK(EventObjects::isOffline(EventObjects::of(pending)));

        int deaths = 0;
        w.gameplay->setRules(holdPlayerRules(deaths));
        w.gameplay->raiseDeath(replica, nullptr, "remote");
        w.events.dispatch();
        CHECK(deaths == 0);
        replicaId.isReplica = false;
        w.gameplay->raiseDeath(replica, nullptr, "owned");
        w.events.dispatch();
        CHECK(deaths == 1);
    }

    struct Snapshot
    {
        float x = 0.0f;
        float y = 0.0f;
        int deaths = 0;
        GameplayEvents::TransientState transient;
    };

    // Full ReplayController flow: recording starts while the player is still inside a death zone
    // that already triggered. Playback must not produce a second Collision/Death for that zone.
    void testRecordingInsideTriggeredZone()
    {
        World w;
        int deaths = 0;
        w.gameplay->setRules(holdPlayerRules(deaths)); // the player stays in the zone it died in

        ReplayController replay(w.events);
        replay.setStateCallbacks(
            [&]() -> std::any
            {
                const Transform *t = w.player().get<Transform>();
                return Snapshot{t->x, t->y, deaths, w.gameplay->captureTransientState()};
            },
            [&](const std::any &state)
            {
                const auto &s = std::any_cast<const Snapshot &>(state);
                w.moveTo(s.x, s.y);
                deaths = s.deaths;
                w.gameplay->restoreTransientState(s.transient);
            });

        auto frame = [&]()
        {
            FrameTiming timing;
            timing.realDelta = 1.0f / 60.0f;
            timing.timelines.push_back({1.0f / 60.0f, 1.0, false});
            replay.beginFrame(timing);
            replay.dispatch(ReplayPhase::Input);
            w.gameplay->produceCollisions(w.playerId);
            replay.dispatch(ReplayPhase::Update);
            replay.dispatch(ReplayPhase::Workers);
            replay.endFrame();
        };

        frame(); // the player is in the zone: Collision -> Death, zone disarmed, player stays
        CHECK(deaths == 1);
        CHECK(!w.zone().armed);

        replay.startRecording(); // applied at the start of the next frame, capturing the state
        frame();
        frame();
        frame();
        CHECK(replay.isRecording());
        CHECK(deaths == 1); // still inside the same zone: nothing new while recording

        // By the end of the recording the world is somewhere else: player moved, zone re-armed.
        w.moveTo(500.0f, 500.0f);
        w.gameplay->resetTransientState();
        const int deathEventsBefore = w.count(EventTypes::Death);

        replay.stop();
        replay.startPlayback(); // Stop then Play are applied at the start of the next frame
        frame();
        CHECK(replay.isPlaying());
        CHECK(w.player().get<Transform>()->x == 0.0f); // restored inside the zone
        frame();
        frame();
        CHECK(!replay.isPlaying()); // all 3 recorded frames played

        CHECK(deaths == 1);
        CHECK(w.count(EventTypes::Death) == deathEventsBefore); // no extra death during playback
        CHECK(!w.zone().armed);
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
    testEventObjectReferences();
    testTransientStateRoundTrip();
    testBoundsLatchRoundTrip();
    testRecordingInsideTriggeredZone();
    std::puts("EventChainTest: all passed");
    return 0;
}
