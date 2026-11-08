#include "SeaReplay.h"
#include "NetEventBridge.h"
#include "Server.h"
#include "InputSystem.h"
#include "SpawnPoint.h"
#include <chrono>
#include <iostream>
#include <thread>

namespace
{
    void require(bool condition, const char *message)
    {
        if (!condition) throw std::runtime_error(message);
    }

    struct State
    {
        std::vector<Sea::SavedObject> objects;
        Sea::Progress progress;
        Sea::PlayerMotion motion;
        Sea::EventState events;
        int spawns;
    };

    struct World
    {
        Engine engine{"Milestone 4 check", 320, 200};
        Sea::Progress progress;
        Sea::PlayerMotion motion;
        std::vector<uint32_t> pearlIds;
        GameObject *player = nullptr, *pearl = nullptr, *power = nullptr, *fish = nullptr, *urchin = nullptr;
        std::unique_ptr<Sea::Events> game;
        std::unique_ptr<NetEventBridge> bridge;
        int spawns = 0;

        explicit World(NetworkClient *client = nullptr)
        {
            auto &objects = engine.getObjects();
            player = &objects.create("captain");
            player->add<Transform>(80, 380, Sea::DiverWidth, Sea::DiverHeight);
            player->add<Collider>(.25f, .25f, .08f, 0);
            player->add<PhysicsBody>(-1, true);
            player->add<PlayerIntent>();
            player->add<Sprite>();
            if (client) player->add<NetworkIdentity>(client->getClientId(), Sea::KindDiver, player->getId());
            auto &ground = objects.create("dock");
            ground.add<Transform>(0, 460, 500, 100);
            ground.add<Collider>();
            ground.add<Sea::Ground>();
            ground.add<Sea::Shore>();
            for (const auto &spawn : Sea::Spawns)
            {
                auto &point = objects.create();
                point.add<Transform>(spawn.x, spawn.y, 24, 24);
                point.add<SpawnPoint>(spawn.name);
            }
            auto &pit = objects.create();
            pit.add<Transform>(0, Sea::WorldHeight - 60, Sea::WorldWidth, 10000);
            pit.add<Collider>(0, 0, 0, 0, true);
            pit.add<DeathZone>(Sea::Spawns[0].name);
            auto trigger = [&](const char *name, float x, float y, float w, float h) -> GameObject *
            {
                auto &object = objects.create(name);
                object.add<Transform>(x, y, w, h);
                object.add<Collider>(0, 0, 0, 0, true);
                object.add<Sprite>();
                return &object;
            };
            pearl = trigger("pearl", 300, 800, 34, 34);
            pearl->add<Sea::Pearl>();
            pearlIds.push_back(pearl->getId());
            power = trigger("power", 500, 800, 64, 64);
            power->add<Sea::PowerUp>();
            fish = trigger("fish", 700, 800, 100, 60);
            fish->add<Sea::Hazard>();
            urchin = trigger("urchin", 900, 900, 90, 36);
            urchin->add<Sea::Hazard>();
            urchin->add<Sea::Urchin>();
            engine.getPhysics().setGravity(Sea::Gravity);
            game = std::make_unique<Sea::Events>(engine, *player, progress, motion, pearlIds,
                [](const std::string &, float) {}, [&]() { ++spawns; }, client != nullptr);
            if (client) bridge = std::make_unique<NetEventBridge>(engine.getEvents(), *client);
            engine.getReplay().setStateCallbacks([&]() -> std::any { return capture(); }, [&](const std::any &saved)
            {
                const auto &state = std::any_cast<const State &>(saved);
                Sea::restoreObjects(engine.getObjects(), state.objects);
                progress = state.progress; motion = state.motion;
                game->restore(state.events);
                spawns = state.spawns;
            });
        }

        State capture() { return {Sea::captureObjects(engine.getObjects()), progress, motion, game->capture(), spawns}; }

        void contact(GameObject &object)
        {
            auto &t = *player->get<Transform>();
            const auto &target = *object.get<Transform>();
            t.x = target.x - 24; t.y = target.y - 52;
            game->collisions();
            engine.getEvents().dispatch();
        }

        void frame(float liveDelta, int scripted = -1)
        {
            auto &replay = engine.getReplay();
            replay.beginFrame({liveDelta, {{liveDelta, 1, false}, {liveDelta, 1, false}}});
            if (replay.isPlaying()) game->input(InputActions::MoveX, 999);
            else if (scripted >= 0)
            {
                game->input(InputActions::MoveX, scripted < 2 ? 1 : -1);
                if (scripted == 1) game->input("sonar");
            }
            replay.dispatch(ReplayPhase::Input);
            const auto &timing = replay.getFrameTiming();
            const float dt = timing.timelines[1].delta;
            progress.tick(dt, timing.realDelta);
            game->tick(timing.realDelta);
            const auto &input = *player->get<PlayerIntent>();
            Sea::movePlayer(engine.getObjects(), *player, motion, engine.getPhysics(), dt,
                            input.moveX, input.jumpPressed, {}, input.jumpHeld, input.downHeld, progress.powered());
            game->collisions();
            replay.dispatch(ReplayPhase::Update);
            replay.dispatch(ReplayPhase::Workers);
            replay.endFrame();
        }

        void control(EventType type)
        {
            Event event;
            event.type = type;
            engine.getEvents().raise(event);
            engine.getEvents().dispatch();
        }
    };

    void history()
    {
        World world;
        const auto position = *world.player->get<Transform>();
        require(!world.game->historyVisible && world.game->recentEvents().empty(), "History must start hidden and empty");
        world.game->toggleHistory();
        require(world.game->historyVisible && world.progress.health == 3 &&
                world.player->get<Transform>()->x == position.x && world.game->counts.input == 0,
                "Showing history must not change the game or raise gameplay input");
        for (int i = 0; i < 30; ++i) world.game->input(InputActions::MoveX, 1);
        world.engine.getEvents().dispatch();
        require(world.game->recentEvents().empty() && world.game->counts.input == 30 &&
                world.player->get<PlayerIntent>()->moveX == 1, "Movement must still use Input events without appearing in history");
        world.game->input(InputActions::MoveX, 0);
        world.game->input(InputActions::DownHeld, 1);
        world.game->input(InputActions::JumpPressed, 1);
        world.engine.getEvents().dispatch();
        require(world.game->recentEvents().empty() && world.player->get<PlayerIntent>()->downHeld &&
                world.player->get<PlayerIntent>()->jumpPressed, "Swimming and jumping must not fill the outcome history");
        world.contact(*world.pearl);
        const auto saved = world.game->capture();
        for (int i = 0; i < 8; ++i)
        {
            world.game->tick(4);
            world.game->input("sonar");
            world.engine.getEvents().dispatch();
        }
        require(world.game->recentEvents().size() == 6, "History must retain only six recent entries");
        world.game->toggleHistory();
        world.game->restore(saved);
        require(!world.game->historyVisible && world.game->recentEvents() == saved.history,
                "Restoring history must preserve the viewer's H setting");
        world.game->input(InputActions::MoveX, 0);
        world.game->input(InputActions::DownHeld, 1);
        world.engine.getEvents().dispatch();
        require(world.game->recentEvents() == saved.history, "Movement must not replace restored gameplay outcomes");
        world.contact(*world.power);
        require(world.game->recentEvents().back() == "Overdrive collected - Collision",
                "Hidden history must still collect actual gameplay outcomes");
    }

    void gameOverAndRestart()
    {
        World world;
        world.contact(*world.pearl);
        for (int remaining = 2; remaining >= 0; --remaining)
        {
            world.progress.graceTime = 0;
            world.contact(*world.fish);
            require(world.progress.health == remaining, "Each unprotected death must lose one life");
        }
        require(world.progress.gameOver && world.spawns == 2 && world.progress.pearls[0],
                "The last life must end the dive without respawning or losing recovered pearls");
        world.progress.tick(10, 10);
        world.contact(*world.fish);
        require(world.progress.health == 0 && world.spawns == 2, "Game over must not restore lives or spawn again");
        world.game->onRestart = [&]()
        {
            world.progress = {};
            Sea::restoreCollectibles(world.engine.getObjects());
            world.game->freshStart();
            world.game->respawn();
        };
        world.game->input("restart");
        world.engine.getEvents().dispatch();
        require(world.progress.health == 3 && !world.progress.gameOver && world.progress.count() == 0 && world.spawns == 3,
                "Restart Input must reset the mission and request a Spawn");
        require(world.game->recentEvents() == std::vector<std::string>{"Placed at departure - Spawn"},
                "Restart history must describe the spawn outcome");
    }

    void fishContact()
    {
        World world;
        auto diver = std::make_shared<Sea::SpriteMask>();
        diver->width = diver->height = 10;
        diver->opaque.assign(100, 1);
        auto fish = std::make_shared<Sea::SpriteMask>(*diver);
        fish->opaque.assign(100, 0);
        for (int y = 2; y < 8; ++y)
            for (int x = 2; x < 8; ++x) fish->opaque[y * 10 + x] = 1;
        world.player->add<Sea::SpriteSilhouette>(diver, diver);
        world.fish->add<Sea::SpriteSilhouette>(fish);
        auto &p = *world.player->get<Transform>();
        const auto &f = *world.fish->get<Transform>();
        p.x = f.x - 68; p.y = f.y - 76;
        require(Collision::checkCollision(*world.player, *world.fish), "Regression must overlap the rectangular hitboxes");
        world.game->collisions();
        world.engine.getEvents().dispatch();
        require(world.progress.health == 3 && world.spawns == 0,
                "Transparent space around a fish must not cause a respawn");
        p.x = f.x + 5; p.y = f.y - 20;
        world.game->collisions();
        world.engine.getEvents().dispatch();
        require(world.progress.health == 2 && world.spawns == 1,
                "Visible contact after entering a transparent corner must cause one respawn");
        fish->frames = 2;
        fish->opaque.assign(200, 0);
        for (int y = 0; y < 10; ++y)
            for (int x = 0; x < 5; ++x)
            {
                fish->opaque[y * 20 + x] = 1;
                fish->opaque[y * 20 + 15 + x] = 1;
            }
        auto &sprite = *world.fish->get<Sprite>();
        sprite.frameCount = 2;
        const float left = f.x + f.width * .25f, right = f.x + f.width * .75f, middle = f.y + f.height * .5f;
        require(Sea::opaqueAt(*world.fish, left, middle) && !Sea::opaqueAt(*world.fish, right, middle),
                "Fish mask must follow the current animation frame");
        sprite.flipHorizontal = true;
        require(!Sea::opaqueAt(*world.fish, left, middle) && Sea::opaqueAt(*world.fish, right, middle),
                "Turning a fish must also turn its collision silhouette");
        sprite.frame = 1;
        require(Sea::opaqueAt(*world.fish, left, middle) && !Sea::opaqueAt(*world.fish, right, middle),
                "Animated fish must use their own frame pixels when flipped");
        auto swimming = std::make_shared<Sea::SpriteMask>(*diver);
        swimming->opaque.assign(100, 0);
        world.player->get<Sea::SpriteSilhouette>()->swimming = swimming;
        const auto draw = Sea::diverDrawBounds(p);
        const float cx = draw.x + draw.w * .5f, cy = draw.y + draw.h * .5f;
        world.player->get<Sprite>()->frame = 0;
        require(Sea::opaqueAt(*world.player, cx, cy), "Walking must use the standing diver silhouette");
        world.player->get<Sprite>()->frame = 8;
        require(!Sea::opaqueAt(*world.player, cx, cy), "Swimming must use its own diver silhouette");
    }

    void localEvents()
    {
        World world;
        world.game->input(InputActions::MoveX, 1);
        require(world.player->get<PlayerIntent>()->moveX == 0, "Input must be queued");
        world.engine.getEvents().dispatch();
        require(world.player->get<PlayerIntent>()->moveX == 1, "Input did not update movement intent");
        world.contact(*world.pearl);
        require(world.progress.pearls[0] && !world.pearl->active, "Collision did not collect the pearl");
        world.contact(*world.power);
        require(world.progress.powered(), "Collision did not activate Overdrive");
        world.contact(*world.fish);
        world.contact(*world.urchin);
        require(world.progress.health == 3 && world.spawns == 0, "Overdrive did not prevent damage");
        world.progress.powerTime = world.progress.graceTime = 0;
        world.game->collisions();
        world.engine.getEvents().dispatch();
        require(world.progress.health == 2 && world.spawns == 1, "Expired protection must allow damage during ongoing contact");
        require(world.player->get<Transform>()->x == Sea::Spawns[0].x, "Spawn did not return to the checkpoint");
        world.progress.activatePower();
        for (const auto &object : world.engine.getObjects().getAll())
            if (object->has<DeathZone>()) { world.contact(*object); break; }
        require(world.progress.health == 1 && world.spawns == 2, "The abyss must cost a life even during Overdrive");
        require(world.game->counts.input > 0 && world.game->counts.collision > 0 &&
                world.game->counts.death > 0 && world.game->counts.spawn > 0, "All four events must drive gameplay");
    }

    void replay()
    {
        World world;
        world.player->get<Transform>()->x = 260;
        world.player->get<Transform>()->y = 752;
        world.fish->get<Transform>()->x = 330;
        world.control(EventTypes::ReplayStart);
        const float deltas[] = {.08f, .12f, .1f, .13f};
        for (int i = 0; i < 4; ++i) world.frame(deltas[i], i);
        const auto expected = world.capture();
        require(expected.progress.pearls[0] && expected.progress.health == 2 && expected.spawns == 1,
                "Replay recording must exercise pickup, death and spawn");
        world.control(EventTypes::ReplayStop);
        world.frame(0);
        for (int repeat = 0; repeat < 2; ++repeat)
        {
            world.progress.health = 99;
            world.player->get<Transform>()->x = 999;
            world.control(EventTypes::ReplayPlayback);
            for (int i = 0; i < 4; ++i) world.frame(.9f);
            const auto actual = world.capture();
            const auto &a = *world.player->get<Transform>();
            const auto &b = *expected.objects.front().transform;
            require(std::abs(a.x - b.x) < .0001f && std::abs(a.y - b.y) < .0001f,
                    "Replay did not reproduce the recorded position");
            require(actual.progress.health == expected.progress.health && actual.progress.pearls == expected.progress.pearls &&
                    actual.spawns == expected.spawns && actual.events.counts.death == expected.events.counts.death &&
                    actual.events.counts.spawn == expected.events.counts.spawn && actual.events.counts.sonar == expected.events.counts.sonar,
                    "Replay duplicated or lost gameplay effects");
            require(actual.events.history == expected.events.history, "Replay must reproduce the gameplay outcome history");
            require(!world.engine.getReplay().isPlaying(), "Replay did not finish");
        }
    }

    void network(WireFormat format)
    {
        const uint16_t port = format == WireFormat::Binary ? 5681 : 5682;
        Server server(port);
        std::thread thread([&]() { server.run(); });
        struct Cleanup
        {
            Server &server;
            std::thread &thread;
            ~Cleanup() { server.stop(); thread.join(); }
        } cleanup{server, thread};
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        {
            const auto otherFormat = format == WireFormat::Binary ? WireFormat::Text : WireFormat::Binary;
            NetworkClient a("127.0.0.1", port, format), b("127.0.0.1", port, otherFormat);
            require(a.connect() && b.connect(), "Network clients did not connect");
            World first(&a), second(&b);
            first.game->input("sonar");
            first.engine.getEvents().dispatch();
            auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
            while (second.game->counts.remoteSonar == 0 && std::chrono::steady_clock::now() < deadline)
            {
                second.bridge->pump(); second.engine.getEvents().dispatch();
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            require(second.game->counts.remoteSonar == 1 && second.progress.protectedNow(),
                    "Network sonar did not grant nearby teammate protection");
            require(second.game->recentEvents().back() == "Teammate sonar received - Network",
                    "History must identify a received network event");
            second.contact(*second.fish);
            require(second.progress.health == 3, "Network protection did not prevent hazard damage");
            second.progress.graceTime = 0;
            second.game->collisions(); second.engine.getEvents().dispatch();
            require(second.progress.health == 2, "Damage did not resume after network protection expired");
            first.game->input("sonar"); first.engine.getEvents().dispatch();
            require(first.game->counts.sonar == 1, "Sonar cooldown did not prevent immediate reuse");
        }
    }
}

int main(int argc, char **argv)
{
    try
    {
        if (argc == 3 && std::string(argv[1]) == "--network")
        {
            WireFormat format;
            require(NetProtocol::parseFormat(argv[2], format), "Unknown wire format");
            network(format);
        }
        else { history(); gameOverAndRestart(); fishContact(); localEvents(); replay(); }
        std::cout << "PASS: Milestone 4 game events, replay and sonar checks\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
