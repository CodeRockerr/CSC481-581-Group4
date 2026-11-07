#include "CaveLevel.h"
#include "Components.h"
#include "DeathZone.h"
#include "Engine.h"
#include "Event.h"
#include "GamePlaySystems.h"
#include "Image.h"
#include "MovementPath.h"
#include "NetEventBridge.h"
#include "NetworkClient.h"
#include "NetworkIdentity.h"
#include "PeerSession.h"
#include "SpawnPoint.h"

#include <algorithm>
#include <any>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace
{
    SDL_Texture *loadTexture(SDL_Renderer *renderer, const char *path)
    {
        SDL_Surface *surface = loadImage(path);
        if (!surface)
        {
            throw std::runtime_error(std::string("Could not load ") + path + ": " + SDL_GetError());
        }
        SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);
        SDL_DestroySurface(surface);
        if (!texture)
        {
            throw std::runtime_error(std::string("Could not create texture for ") + path + ": " + SDL_GetError());
        }
        SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_LINEAR);
        return texture;
    }

    bool overlaps(const SDL_FRect &a, const SDL_FRect &b)
    {
        return a.x < b.x + b.w && a.x + a.w > b.x && a.y < b.y + b.h && a.y + a.h > b.y;
    }

    SDL_FRect colliderBounds(const GameObject &object)
    {
        const Transform *t = object.get<Transform>();
        const Collider *c = object.get<Collider>();
        if (t == nullptr)
        {
            return {0.0f, 0.0f, 0.0f, 0.0f};
        }
        return c != nullptr ? c->boundsFor(*t) : t->getBounds();
    }

    struct Options
    {
        std::string host;
        uint16_t port = 5555;
        WireFormat format = WireFormat::Binary;
        uint16_t peerPort = 0;
        std::vector<std::pair<std::string, uint16_t>> peers;
    };

    bool parseArgs(int argc, char *argv[], Options &options)
    {
        if (argc >= 3 && std::string(argv[1]) == "--client")
        {
            options.host = argv[2];
            if (argc >= 4)
                options.port = static_cast<uint16_t>(std::stoi(argv[3]));
            if (argc >= 5 && !NetProtocol::parseFormat(argv[4], options.format))
            {
                std::cerr << "Format must be binary or text\n";
                return false;
            }
        }
        else if (argc >= 3 && std::string(argv[1]) == "--p2p")
        {
            options.peerPort = static_cast<uint16_t>(std::stoi(argv[2]));
            for (int i = 3; i < argc; ++i)
            {
                const std::string text(argv[i]);
                const auto colon = text.rfind(':');
                if (colon == std::string::npos)
                {
                    std::cerr << "Expected host:port, got " << text << '\n';
                    return false;
                }
                options.peers.emplace_back(text.substr(0, colon),
                                           static_cast<uint16_t>(std::stoi(text.substr(colon + 1))));
            }
        }
        return true;
    }
}

int main(int argc, char *argv[])
{
    Options options;
    if (!parseArgs(argc, argv, options))
    {
        return 1;
    }

    std::unique_ptr<NetworkClient> net;
    if (!options.host.empty())
    {
        net = std::make_unique<NetworkClient>(options.host.c_str(), options.port, options.format);
        if (!net->connect())
        {
            std::cerr << "Failed to connect to " << options.host << ":" << options.port << '\n';
            return 1;
        }
    }
    std::unique_ptr<PeerSession> peers;
    if (options.peerPort != 0)
    {
        peers = std::make_unique<PeerSession>(options.peerPort, options.format);
        for (const auto &other : options.peers)
        {
            peers->connectTo(other.first, other.second);
        }
    }
    const uint32_t localOwner = net ? net->getClientId() : (peers ? peers->getOwnerId() : 0u);

    Engine engine("Cave Ninja - Milestone 4", 1280, 720);
    GameObjectManager &objects = engine.getObjects();
    Camera &camera = engine.getCamera();
    SDL_Renderer *renderer = engine.getRenderer().getHandle();

    objects.setReferenceResolution(static_cast<int>(Cave::ViewWidth), static_cast<int>(Cave::ViewHeight));
    objects.setScaleMode(ScaleMode::Percentage);
    camera.setViewport(Cave::ViewWidth, Cave::ViewHeight);
    camera.setBounds(0.0f, 0.0f, Cave::WorldWidth, Cave::WorldHeight);

    SDL_Texture *backgroundTexture = loadTexture(renderer, "Games/skaveti/assets/background.png");
    SDL_Texture *platformTexture = loadTexture(renderer, "Games/skaveti/assets/platform.png");
    SDL_Texture *playerTexture = loadTexture(renderer, "Games/skaveti/assets/player.png");
    SDL_Texture *wispTexture = loadTexture(renderer, "Games/skaveti/assets/shadow_wisp.png");

    for (float x = 0.0f; x < Cave::WorldWidth; x += Cave::ViewWidth)
    {
        GameObject &tile = objects.create("background");
        tile.add<Transform>(x, 0.0f, Cave::ViewWidth, Cave::ViewHeight);
        Sprite &sprite = tile.add<Sprite>(backgroundTexture);
        sprite.layer = -10;
        sprite.flipHorizontal = static_cast<int>(x / Cave::ViewWidth) % 2 == 1;
    }

    for (const Cave::StaticPlatformDef &def : Cave::StaticPlatforms)
    {
        GameObject &platform = objects.create("static-platform");
        platform.add<Transform>(def.x, def.y, def.width, def.height);
        Sprite &sprite = platform.add<Sprite>(platformTexture);
        sprite.color = SDL_Color{def.r, def.g, def.b, 255};
        platform.add<Collider>(0.0f, 0.0f, Cave::PlatformTopPadding, 0.0f);
        platform.add<Cave::Ground>();
    }

    auto dressMovingPlatform = [&](GameObject &platform, int index)
    {
        Sprite *sprite = platform.get<Sprite>();
        if (sprite == nullptr)
            sprite = &platform.add<Sprite>();
        sprite->texture = platformTexture;
        sprite->frameCount = 1;
        sprite->color = SDL_Color{255, 225, 140, 255};
        platform.add<Collider>(0.0f, 0.0f, Cave::PlatformTopPadding, 0.0f);
        platform.add<Cave::Ground>(index);
    };
    if (!net)
    {
        for (int i = 0; i < Cave::MovingPlatformCount; ++i)
        {
            const Cave::MovingPlatformDef &def = Cave::MovingPlatforms[i];
            float x = 0.0f;
            float y = 0.0f;
            Cave::movingPlatformStart(i, x, y);
            GameObject &platform = objects.create("moving-platform");
            platform.add<Transform>(x, y, def.width, def.height);
            platform.add<MovementPath>(def.path, def.speed, def.amplitude);
            dressMovingPlatform(platform, i);
        }
    }

    const float wispSize = 115.0f;
    GameObject &wisp = objects.create("shadow-wisp");
    wisp.add<Transform>(0.0f, 0.0f, wispSize, wispSize);
    wisp.add<Sprite>(wispTexture).layer = 1;
    wisp.add<Collider>(0.20f, 0.20f, 0.15f, 0.15f, true);
    wisp.add<Cave::Patrol>(0);
    wisp.active = false;
    const uint32_t wispId = wisp.getId();

    for (const Cave::SpawnPointDef &def : Cave::SpawnPoints)
    {
        GameObject &spawn = objects.create("spawn-point");
        spawn.add<Transform>(def.x, def.y, 24.0f, 24.0f);
        spawn.add<SpawnPoint>(def.name);
    }
    for (const Cave::DeathZoneDef &def : Cave::DeathZones)
    {
        GameObject &zone = objects.create("death-zone");
        zone.add<Transform>(def.x, def.y, def.width, def.height);
        zone.add<Collider>(0.0f, 0.0f, 0.0f, 0.0f, true);
        zone.add<DeathZone>(Cave::SpawnPoints[0].name);
    }
    GameObject &leftBoundary = objects.create("scroll-left");
    leftBoundary.add<Transform>(0.0f, -2000.0f, Cave::ScrollLeftEdge, 4000.0f);
    leftBoundary.add<Collider>(0.0f, 0.0f, 0.0f, 0.0f, true);
    leftBoundary.add<Cave::ScrollBoundary>(-1);
    GameObject &rightBoundary = objects.create("scroll-right");
    rightBoundary.add<Transform>(Cave::ScrollRightEdge, -2000.0f, Cave::ViewWidth - Cave::ScrollRightEdge, 4000.0f);
    rightBoundary.add<Collider>(0.0f, 0.0f, 0.0f, 0.0f, true);
    rightBoundary.add<Cave::ScrollBoundary>(1);
    const uint32_t leftBoundaryId = leftBoundary.getId();
    const uint32_t rightBoundaryId = rightBoundary.getId();

    const float playerSize = 1280.0f * 0.095f;
    const float moveSpeed = 1280.0f * 0.32f;
    const float jumpSpeed = 700.0f;
    const float spawnOffset = 40.0f * static_cast<float>(localOwner % 4);
    GameObject &player = objects.create("ninja");
    player.add<Transform>(Cave::SpawnPoints[0].x + spawnOffset, Cave::SpawnPoints[0].y, playerSize, playerSize);
    Sprite &playerSprite = player.add<Sprite>(playerTexture, 4);
    playerSprite.layer = 2;
    player.add<Collider>(0.28f, 0.28f, 0.10f, 0.05f);
    player.add<PhysicsBody>(Engine::PlayerTime, true);
    player.add<PlayerInput>(moveSpeed, jumpSpeed);
    if (net || peers)
    {
        player.add<NetworkIdentity>(localOwner, Cave::KindNinja);
    }
    const uint32_t playerId = player.getId();

    const NetReplicator::SpawnFn onSpawn = [&](GameObject &replica, const ObjectState &state)
    {
        if (state.kind == Cave::KindNinja)
        {
            Sprite *sprite = replica.get<Sprite>();
            sprite->texture = playerTexture;
            sprite->frameCount = 4;
            sprite->layer = 1;
            sprite->color = SDL_Color{200, 200, 255, 230};
        }
        else if (state.kind == Cave::KindMovingPlatform)
        {
            const int index = static_cast<int>(state.netId) - 1;
            dressMovingPlatform(replica, index >= 0 && index < Cave::MovingPlatformCount ? index : 0);
        }
    };

    Timeline &playerTime = engine.getTimeline(Engine::PlayerTime);
    Timeline &globalTime = engine.getGlobalTimeline();

    bool grounded = false;
    uint32_t ridingId = 0;
    float lastFeetY = 0.0f;
    float walkTimer = 0.0f;
    int runIndex = 0;
    int checkpoint = 0;
    bool showHidden = false;
    int deaths = 0;
    std::unordered_map<uint32_t, SDL_FPoint> lastGroundPos;

    int64_t lastTitleUpdate = 0;
    uint64_t framesSinceTitle = 0;

    auto pinBoundaries = [&]()
    {
        objects.find(leftBoundaryId)->get<Transform>()->x = camera.getX();
        objects.find(rightBoundaryId)->get<Transform>()->x = camera.getX() + Cave::ScrollRightEdge;
    };

    auto snapCameraToPlayer = [&]()
    {
        const float x = objects.find(playerId)->get<Transform>()->x;
        camera.setPosition(x - Cave::ScrollLeftEdge - 40.0f, 0.0f);
        pinBoundaries();
    };

    auto setCheckpoint = [&](int index)
    {
        checkpoint = index;
        objects.forEach<DeathZone>([&](GameObject &, DeathZone &dz)
                                   { dz.spawnPointName = Cave::SpawnPoints[index].name; });
        SDL_Log("Checkpoint %s reached", Cave::SpawnPoints[index].name);
    };

    auto afterRespawn = [&]()
    {
        GameObject *ninja = objects.find(playerId);
        Transform *t = ninja->get<Transform>();
        grounded = false;
        ridingId = 0;
        lastFeetY = t->y + t->height;
        snapCameraToPlayer();
    };

    auto respawnAt = [&](const std::string &name)
    {
        GameObject *spawn = GameplaySystems::findSpawnPointByName(objects, name);
        if (spawn == nullptr)
            return;
        GameObject *ninja = objects.find(playerId);
        Transform *t = ninja->get<Transform>();
        PhysicsBody *body = ninja->get<PhysicsBody>();
        t->x = spawn->get<Transform>()->x;
        t->y = spawn->get<Transform>()->y;
        body->velocityX = 0.0f;
        body->velocityY = 0.0f;
        afterRespawn();
    };


    // ------------------------------------------------------------------------------------
    // Milestone 4: gameplay is driven by events (see Docs/Design/Design_Events.md).
    //   keys           -> Input events      -> movement intent (moveAxis / jumpQueued)
    //   overlap checks -> Collision events  -> hazard, wisp, checkpoint responses
    //   Collision      -> Death event       -> life count, then raises a Spawn event
    //   Death          -> Spawn event       -> respawn at the checkpoint
    //   Death (shareable) crosses the network; the other client sees the ninja flash red.
    // ------------------------------------------------------------------------------------
    EventManager &events = engine.getEvents();
    ReplayController &replay = engine.getReplay();
    std::unique_ptr<NetEventBridge> bridge;
    if (net)
        bridge = std::make_unique<NetEventBridge>(events, *net); // after connect(): sets the owner id

    constexpr EventTime InvulnerableMicros = 150'000; // one death per hit, not one per overlapping hazard
    constexpr int64_t FlashNanos = 1'200'000'000;
    float moveAxis = 0.0f;
    bool jumpQueued = false;
    EventTime invulnerableUntil = 0;
    int remoteDeaths = 0;
    std::unordered_set<uint32_t> touching; // hazards/triggers overlapped last frame (edge detection)
    std::unordered_map<uint32_t, int64_t> flashUntil; // replica object id -> global time (ns)

    auto raiseDeath = [&](const std::string &reason, uint32_t causeId)
    {
        Event death;
        death.type = EventTypes::Death;
        death.priority = EventPriority::High;
        death.shareable = true; // the only gameplay event shared over the network
        death.source = {0, causeId};
        death.target = {0, playerId};
        death.payload = DeathEvent{reason};
        events.raise(death);
    };

    events.subscribe(EventTypes::Input, [&](const Event &e)
                     {
        const InputEvent *in = std::get_if<InputEvent>(&e.payload);
        if (in == nullptr)
            return;
        if (in->action == "move")
            moveAxis = in->value;
        else if (in->action == "jump" && in->value > 0.0f)
            jumpQueued = true; });

    events.subscribe(EventTypes::Collision, [&](const Event &e)
                     {
        if (e.origin == EventOrigin::Network)
            return;
        GameObject *other = objects.find(e.target.netId);
        if (other == nullptr)
            return;
        if (other->has<SpawnPoint>())
        {
            const SpawnPoint *point = other->get<SpawnPoint>();
            for (int i = checkpoint + 1; i < Cave::SpawnPointCount; ++i)
                if (point->name == Cave::SpawnPoints[i].name)
                    setCheckpoint(i);
            return;
        }
        if (events.now() < invulnerableUntil)
            return;
        if (other->has<DeathZone>())
            raiseDeath("hazard", other->getId());
        else if (other->has<Cave::Patrol>())
            raiseDeath("shadow wisp", other->getId()); });

    events.subscribe(EventTypes::Death, [&](const Event &e)
                     {
        const DeathEvent *d = std::get_if<DeathEvent>(&e.payload);
        const std::string reason = d != nullptr ? d->reason : "unknown";
        if (e.origin == EventOrigin::Network)
        {
            // Another player died: flash their replica red and count it.
            ++remoteDeaths;
            objects.forEach<NetworkIdentity>([&](GameObject &g, NetworkIdentity &id)
                                             {
                if (id.isReplica && id.kind == Cave::KindNinja && id.ownerId == e.target.ownerId)
                    flashUntil[g.getId()] = globalTime.getTime() + FlashNanos; });
            SDL_Log("Player %u died (%s)", e.target.ownerId, reason.c_str());
            return;
        }
        ++deaths;
        invulnerableUntil = events.now() + InvulnerableMicros;
        SDL_Log("The ninja died (%s)", reason.c_str());
        Event spawn;
        spawn.type = EventTypes::Spawn;
        spawn.target = {0, playerId};
        spawn.payload = SpawnEvent{Cave::SpawnPoints[checkpoint].name};
        events.raise(spawn); });

    events.subscribe(EventTypes::Spawn, [&](const Event &e)
                     {
        const SpawnEvent *s = std::get_if<SpawnEvent>(&e.payload);
        if (s != nullptr && e.origin != EventOrigin::Network)
            respawnAt(s->spawnPoint); });

    // Replay (offline only): record input + timing, then regenerate the rest on playback.
    struct PathState
    {
        float x, y, elapsed, startX, startY, centerX, centerY;
        bool initialized;
    };
    struct ReplayState
    {
        float x, y, vx, vy, lastFeetY, walkTimer, moveAxis, camX, camY;
        bool grounded, jumpQueued, flip;
        int frame, runIndex, checkpoint, deaths;
        uint32_t ridingId;
        EventTime invulnerableUntil;
        std::unordered_map<uint32_t, PathState> paths;
        std::unordered_map<uint32_t, SDL_FPoint> groundPos;
        std::unordered_set<uint32_t> touching;
    };
    if (!net && !peers)
    {
        replay.setStateCallbacks(
            [&]() -> std::any
            {
                ReplayState st{};
                GameObject *ninja = objects.find(playerId);
                const Transform *t = ninja->get<Transform>();
                const PhysicsBody *b = ninja->get<PhysicsBody>();
                const Sprite *sp = ninja->get<Sprite>();
                st.x = t->x;
                st.y = t->y;
                st.vx = b->velocityX;
                st.vy = b->velocityY;
                st.flip = sp->flipHorizontal;
                st.frame = sp->frame;
                st.lastFeetY = lastFeetY;
                st.walkTimer = walkTimer;
                st.moveAxis = moveAxis;
                st.camX = camera.getX();
                st.camY = camera.getY();
                st.grounded = grounded;
                st.jumpQueued = jumpQueued;
                st.runIndex = runIndex;
                st.checkpoint = checkpoint;
                st.deaths = deaths;
                st.ridingId = ridingId;
                st.invulnerableUntil = invulnerableUntil;
                st.groundPos = lastGroundPos;
                st.touching = touching;
                objects.forEach<MovementPath, Transform>([&](GameObject &g, MovementPath &m, Transform &pt)
                                                         { st.paths[g.getId()] = {pt.x, pt.y, m.elapsedTime, m.startX, m.startY,
                                                                                  m.centerX, m.centerY, m.initialized}; });
                return st;
            },
            [&](const std::any &state)
            {
                const ReplayState &st = std::any_cast<const ReplayState &>(state);
                GameObject *ninja = objects.find(playerId);
                Transform *t = ninja->get<Transform>();
                PhysicsBody *b = ninja->get<PhysicsBody>();
                Sprite *sp = ninja->get<Sprite>();
                t->x = st.x;
                t->y = st.y;
                b->velocityX = st.vx;
                b->velocityY = st.vy;
                sp->flipHorizontal = st.flip;
                sp->frame = st.frame;
                lastFeetY = st.lastFeetY;
                walkTimer = st.walkTimer;
                moveAxis = st.moveAxis;
                grounded = st.grounded;
                jumpQueued = st.jumpQueued;
                runIndex = st.runIndex;
                checkpoint = st.checkpoint;
                deaths = st.deaths;
                ridingId = st.ridingId;
                invulnerableUntil = st.invulnerableUntil;
                lastGroundPos = st.groundPos;
                touching = st.touching;
                objects.forEach<MovementPath, Transform>([&](GameObject &g, MovementPath &m, Transform &pt)
                                                         {
                    auto found = st.paths.find(g.getId());
                    if (found == st.paths.end())
                        return;
                    pt.x = found->second.x;
                    pt.y = found->second.y;
                    m.elapsedTime = found->second.elapsed;
                    m.startX = found->second.startX;
                    m.startY = found->second.startY;
                    m.centerX = found->second.centerX;
                    m.centerY = found->second.centerY;
                    m.initialized = found->second.initialized; });
                setCheckpoint(checkpoint);
                camera.setPosition(st.camX, st.camY);
                pinBoundaries();
            });
    }

    auto toggleHiddenDebug = [&]()
    {
        showHidden = !showHidden;
        for (const auto &object : objects.getAll())
        {
            SDL_Color color{0, 0, 0, 0};
            if (object->has<SpawnPoint>())
                color = SDL_Color{80, 255, 120, 160};
            else if (object->has<DeathZone>())
                color = SDL_Color{255, 40, 40, 90};
            else if (object->has<Cave::ScrollBoundary>())
                color = SDL_Color{80, 160, 255, 90};
            else
                continue;
            if (showHidden)
                object->add<Sprite>(color).layer = 20;
            else
                object->remove<Sprite>();
        }
    };

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    engine.run(
        [&](float worldDelta)
        {
            const float playerDelta = static_cast<float>(playerTime.getLastDelta());
            camera.setViewport(Cave::ViewWidth, Cave::ViewHeight);

            // During local replay the recorded run owns the world, so live snapshots are not applied.
            if (net && !events.isPlaybackMode())
                net->sync(objects, onSpawn);
            if (peers && !events.isPlaybackMode())
                peers->sync(objects, onSpawn);
            if (bridge)
                bridge->pump(); // network -> EventManager (handled in this frame's dispatch)

            // Flash the replica of a player who just died on another client.
            objects.forEach<NetworkIdentity, Sprite>([&](GameObject &g, NetworkIdentity &id, Sprite &sp)
                                                     {
                if (!id.isReplica || id.kind != Cave::KindNinja)
                    return;
                auto flash = flashUntil.find(g.getId());
                const bool flashing = flash != flashUntil.end() && globalTime.getTime() < flash->second;
                sp.color = flashing ? SDL_Color{255, 60, 60, 255} : SDL_Color{200, 200, 255, 230}; });

            if (!net)
            {
                if (peers)
                {
                    const float shared = static_cast<float>(peerNowTic() - peers->getAnchor()) / 1'000'000.0f;
                    objects.forEach<MovementPath>([&](GameObject &, MovementPath &path)
                                                  {
                        if (path.initialized)
                            path.elapsedTime = shared - worldDelta; });
                }
                GameplaySystems::updateMovementPaths(objects, worldDelta);
            }

            std::unordered_map<uint32_t, SDL_FPoint> groundDelta;
            objects.forEach<Transform, Cave::Ground>([&](GameObject &g, Transform &t, Cave::Ground &)
                                                     {
                auto found = lastGroundPos.find(g.getId());
                groundDelta[g.getId()] = found == lastGroundPos.end() ? SDL_FPoint{0.0f, 0.0f}
                                                                     : SDL_FPoint{t.x - found->second.x, t.y - found->second.y};
                lastGroundPos[g.getId()] = SDL_FPoint{t.x, t.y}; });

            if (GameObject *w = objects.find(wispId))
            {
                bool placed = false;
                objects.forEach<Transform, Cave::Ground>([&](GameObject &, Transform &pt, Cave::Ground &g)
                                                         {
                    if (placed || g.movingIndex != w->get<Cave::Patrol>()->platformIndex)
                        return;
                    const Cave::MovingPlatformDef &def = Cave::MovingPlatforms[g.movingIndex];
                    const float phase = std::clamp((pt.x - def.centerX) / def.amplitude, -1.0f, 1.0f);
                    const float range = (pt.width - wispSize) * 0.5f;
                    Transform *wt = w->get<Transform>();
                    const float newX = pt.x + pt.width * 0.5f - wispSize * 0.5f - phase * range;
                    if (newX != wt->x)
                        w->get<Sprite>()->flipHorizontal = newX > wt->x;
                    wt->x = newX;
                    wt->y = pt.y + pt.height * Cave::PlatformTopPadding - wispSize;
                    placed = true; });
                w->active = placed;
            }

            if (Input::isKeyJustPressed(SDL_SCANCODE_TAB))
                objects.toggleScaleMode();
            if (Input::isKeyJustPressed(SDL_SCANCODE_H))
                toggleHiddenDebug();

            GameObject &ninja = *objects.find(playerId);
            Transform &t = *ninja.get<Transform>();
            PhysicsBody &body = *ninja.get<PhysicsBody>();
            Sprite &sprite = *ninja.get<Sprite>();
            const PlayerInput &input = *ninja.get<PlayerInput>();

            // Input producer: keys become Input events. EventManager drops live Input events
            // while a replay plays, so recorded input is the only input then.
            {
                Event move;
                move.type = EventTypes::Input;
                move.target = {0, playerId};
                move.payload = InputEvent{"move", input.horizontalAxis()};
                events.raise(move);
                if (input.jumpJustPressed())
                {
                    Event jump;
                    jump.type = EventTypes::Input;
                    jump.target = {0, playerId};
                    jump.payload = InputEvent{"jump", 1.0f};
                    events.raise(jump);
                }
            }
            if (!net && !peers)
            {
                // R: start recording, T: stop, Y: play back (replay controls are events too).
                for (const auto &[key, type] : {std::pair<SDL_Scancode, EventType>{SDL_SCANCODE_R, EventTypes::ReplayStart},
                                                {SDL_SCANCODE_T, EventTypes::ReplayStop},
                                                {SDL_SCANCODE_Y, EventTypes::ReplayPlayback}})
                {
                    if (Input::isKeyJustPressed(key))
                    {
                        Event control;
                        control.type = type;
                        events.raise(control);
                    }
                }
            }
            {
                std::lock_guard<std::mutex> lock(ninja.getMutex());
                const bool paused = playerTime.isPaused();

                if (grounded && ridingId != 0 && !paused && objects.find(ridingId) != nullptr)
                {
                    const SDL_FPoint d = groundDelta[ridingId];
                    t.x += d.x;
                    t.y += d.y;
                }

                const float axis = moveAxis;
                body.velocityX = axis * input.moveSpeed;
                if (axis < 0.0f)
                    sprite.flipHorizontal = true;
                else if (axis > 0.0f)
                    sprite.flipHorizontal = false;

                if (jumpQueued && grounded && !paused)
                {
                    body.velocityY = -input.jumpSpeed;
                    grounded = false;
                    ridingId = 0;
                }
                jumpQueued = false;

                const SDL_FRect feet{t.x + t.width * 0.25f, t.y, t.width * 0.5f, t.height};
                bool landed = false;
                objects.forEach<Cave::Ground>([&](GameObject &g, Cave::Ground &)
                                              {
                    if (landed)
                        return;
                    const SDL_FRect top = colliderBounds(g);
                    const float rise = std::max(0.0f, -groundDelta[g.getId()].y);
                    if (body.velocityY >= 0.0f && overlaps(feet, top) && lastFeetY <= top.y + 1.0f + rise)
                    {
                        t.y = top.y - t.height;
                        body.velocityY = 0.0f;
                        grounded = true;
                        ridingId = g.getId();
                        landed = true;
                    } });
                if (!landed)
                {
                    bool stillOn = false;
                    if (grounded && ridingId != 0)
                    {
                        if (GameObject *g = objects.find(ridingId))
                        {
                            const SDL_FRect top = colliderBounds(*g);
                            stillOn = feet.x < top.x + top.w && feet.x + feet.w > top.x &&
                                      std::fabs(t.y + t.height - top.y) < 2.0f;
                        }
                    }
                    if (!stillOn)
                    {
                        grounded = false;
                        ridingId = 0;
                    }
                }

                const int runFrames[3] = {1, 2, 3};
                if (!grounded)
                    sprite.frame = 2;
                else if (axis != 0.0f)
                {
                    walkTimer += playerDelta;
                    if (walkTimer >= 0.12f)
                    {
                        walkTimer = 0.0f;
                        runIndex = (runIndex + 1) % 3;
                    }
                    sprite.frame = runFrames[runIndex];
                }
                else
                {
                    walkTimer = 0.0f;
                    sprite.frame = 0;
                }

                t.x = std::clamp(t.x, 0.0f, Cave::WorldWidth - t.width);
                lastFeetY = t.y + t.height;
            }

            // Collision producer: new overlaps (edge-triggered) become Collision events.
            // The handlers above decide what each contact means.
            {
                std::unordered_set<uint32_t> overlapping;
                auto contact = [&](GameObject &other, bool trigger)
                {
                    overlapping.insert(other.getId());
                    if (touching.count(other.getId()) != 0)
                        return;
                    Event hit;
                    hit.type = EventTypes::Collision;
                    hit.source = {0, playerId};
                    hit.target = {0, other.getId()};
                    hit.payload = CollisionEvent{0.0f, -1.0f, trigger};
                    events.raise(hit);
                };
                const SDL_FRect body = colliderBounds(ninja);
                objects.forEach<DeathZone, Collider>([&](GameObject &zone, DeathZone &, Collider &)
                                                     {
                    if (overlaps(body, colliderBounds(zone)))
                        contact(zone, true); });
                if (GameObject *w = objects.find(wispId); w && w->active && overlaps(body, colliderBounds(*w)))
                    contact(*w, false);
                for (int i = checkpoint + 1; i < Cave::SpawnPointCount; ++i)
                {
                    if (t.x >= Cave::SpawnPoints[i].x)
                        if (GameObject *point = GameplaySystems::findSpawnPointByName(objects, Cave::SpawnPoints[i].name))
                            contact(*point, true);
                }
                touching = std::move(overlapping);
                if (t.y > Cave::WorldHeight + 200.0f && events.now() >= invulnerableUntil)
                    raiseDeath("fell", 0);
            }

            {
                const SDL_FRect box = colliderBounds(ninja);
                const SDL_FRect right = colliderBounds(*objects.find(rightBoundaryId));
                const SDL_FRect left = colliderBounds(*objects.find(leftBoundaryId));
                if (box.x + box.w > right.x)
                    camera.translate(box.x + box.w - right.x, 0.0f);
                else if (box.x < left.x + left.w)
                    camera.translate(box.x - (left.x + left.w), 0.0f);
                pinBoundaries();
            }

            // ...and the replayed state is never published to other players.
            if (net && !events.isPlaybackMode())
                net->publish(objects);
            if (peers && !events.isPlaybackMode())
                peers->publish(objects);

            ++framesSinceTitle;
            const int64_t now = globalTime.getTime();
            if (now - lastTitleUpdate >= 250'000'000)
            {
                const double seconds = static_cast<double>(now - lastTitleUpdate) / 1'000'000'000.0;
                const double loopHz = seconds > 0.0 ? static_cast<double>(framesSinceTitle) / seconds : 0.0;
                framesSinceTitle = 0;
                lastTitleUpdate = now;
                std::size_t ninjas = 1;
                objects.forEach<NetworkIdentity>([&](GameObject &, NetworkIdentity &id)
                                                 {
                    if (id.isReplica && id.kind == Cave::KindNinja)
                        ++ninjas; });
                const char *mode = net ? "client" : (peers ? "peer" : "offline");
                char title[256];
                std::snprintf(title, sizeof(title),
                              "Cave Ninja M4 | %s %u (%s) | ninjas %zu | checkpoint %s | deaths %d | remote deaths %d | %s | loop %.0f Hz | rtt %.1f ms | %s x%.1f",
                              mode, localOwner, NetProtocol::formatName(options.format), ninjas,
                              Cave::SpawnPoints[checkpoint].name, deaths, remoteDeaths,
                              replay.isRecording() ? "REC" : (replay.isPlaying() ? "REPLAY" : "live"), loopHz,
                              net ? static_cast<double>(net->getLastRoundTripMicros()) / 1000.0 : 0.0,
                              playerTime.isPaused() ? "PAUSED" : "running", playerTime.getScale());
                SDL_SetWindowTitle(engine.getWindow().getHandle(), title);
            }
        });

    if (net)
        net->disconnect();
    SDL_DestroyTexture(backgroundTexture);
    SDL_DestroyTexture(platformTexture);
    SDL_DestroyTexture(playerTexture);
    SDL_DestroyTexture(wispTexture);
    return 0;
}
