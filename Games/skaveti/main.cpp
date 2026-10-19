// Cave Ninja - Milestone 3 (Sai Sumedh Kaveti)
//
// Built on the component-based GameObject model and the shared gameplay systems:
//   ninja             Transform, Sprite, Collider, PhysicsBody (PlayerTime), PlayerInput, NetworkIdentity
//   static platform   Transform, Sprite (tinted), Collider, Ground
//   moving platform   Transform, Sprite, Collider, Ground, MovementPath  - server-owned when networked
//   shadow wisp       Transform, Sprite, Collider (trigger), Patrol
//   spawn point       Transform, SpawnPoint                             - hidden (no Sprite)
//   death zone        Transform, Collider (trigger), DeathZone          - hidden
//   scroll boundary   Transform, Collider (trigger), ScrollBoundary     - hidden, moves the Camera
//
// Respawn rule: whatever kills the ninja (pit or wisp), it comes back at the last checkpoint
// (spawn point) it reached.
//
// Usage:
//   skaveti_game                                    offline
//   skaveti_game --client HOST [PORT] [binary|text] client-server
//   skaveti_game --p2p PORT [HOST:PORT ...]          peer-to-peer
//
// Keys: A/D or arrows move, W/Up/Space jump, P pause ninja time, 1/2/3 time scale,
//       TAB pixel/percentage scaling, H show/hide hidden objects (debug view).

#include "CaveLevel.h"
#include "Components.h"
#include "DeathZone.h"
#include "Engine.h"
#include "GamePlaySystems.h"
#include "Image.h"
#include "MovementPath.h"
#include "NetworkClient.h"
#include "NetworkIdentity.h"
#include "PeerSession.h"
#include "SpawnPoint.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
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

    // ---------------------------------------------------------------- networking
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

    // ---------------------------------------------------------------- engine
    Engine engine("Cave Ninja - Milestone 3", 1280, 720);
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

    // Background: tiles across the whole world, every other tile mirrored so the seams match.
    for (float x = 0.0f; x < Cave::WorldWidth; x += Cave::ViewWidth)
    {
        GameObject &tile = objects.create("background");
        tile.add<Transform>(x, 0.0f, Cave::ViewWidth, Cave::ViewHeight);
        Sprite &sprite = tile.add<Sprite>(backgroundTexture);
        sprite.layer = -10;
        sprite.flipHorizontal = static_cast<int>(x / Cave::ViewWidth) % 2 == 1;
    }

    // Static platforms.
    for (const Cave::StaticPlatformDef &def : Cave::StaticPlatforms)
    {
        GameObject &platform = objects.create("static-platform");
        platform.add<Transform>(def.x, def.y, def.width, def.height);
        Sprite &sprite = platform.add<Sprite>(platformTexture);
        sprite.color = SDL_Color{def.r, def.g, def.b, 255};
        platform.add<Collider>(0.0f, 0.0f, Cave::PlatformTopPadding, 0.0f);
        platform.add<Cave::Ground>();
    }

    // Moving platforms: built locally (with MovementPath) offline / in P2P; replicated from the
    // server otherwise, where the server runs the MovementPath.
    auto dressMovingPlatform = [&](GameObject &platform, int index)
    {
        Sprite *sprite = platform.get<Sprite>();
        if (sprite == nullptr)
            sprite = &platform.add<Sprite>();
        sprite->texture = platformTexture;
        sprite->frameCount = 1;
        sprite->color = SDL_Color{255, 225, 140, 255}; // gold: moving platforms stand out
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

    // Shadow wisp patrolling the horizontal moving platform.
    const float wispSize = 115.0f;
    GameObject &wisp = objects.create("shadow-wisp");
    wisp.add<Transform>(0.0f, 0.0f, wispSize, wispSize);
    wisp.add<Sprite>(wispTexture).layer = 1;
    wisp.add<Collider>(0.20f, 0.20f, 0.15f, 0.15f, true);
    wisp.add<Cave::Patrol>(0);
    wisp.active = false; // until its platform exists
    const uint32_t wispId = wisp.getId();

    // Hidden objects (shared engine components): spawn points, death zones, scroll triggers.
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
        zone.add<DeathZone>(Cave::SpawnPoints[0].name); // updated to the latest checkpoint as you play
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

    // The ninja.
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

    // Replicas from other endpoints get their look here.
    const NetReplicator::SpawnFn onSpawn = [&](GameObject &replica, const ObjectState &state)
    {
        if (state.kind == Cave::KindNinja)
        {
            Sprite *sprite = replica.get<Sprite>();
            sprite->texture = playerTexture;
            sprite->frameCount = 4;
            sprite->layer = 1;
            sprite->color = SDL_Color{200, 200, 255, 230}; // slightly tinted so you can tell yourself apart
        }
        else if (state.kind == Cave::KindMovingPlatform)
        {
            const int index = static_cast<int>(state.netId) - 1;
            dressMovingPlatform(replica, index >= 0 && index < Cave::MovingPlatformCount ? index : 0);
        }
    };

    // ---------------------------------------------------------------- game state
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

    // Keep the hidden scroll boundaries pinned to the current view.
    auto pinBoundaries = [&]()
    {
        objects.find(leftBoundaryId)->get<Transform>()->x = camera.getX();
        objects.find(rightBoundaryId)->get<Transform>()->x = camera.getX() + Cave::ScrollRightEdge;
    };

    // After a teleport the ninja can be far outside the view; put the camera back on it.
    auto snapCameraToPlayer = [&]()
    {
        const float x = objects.find(playerId)->get<Transform>()->x;
        camera.setPosition(x - Cave::ScrollLeftEdge - 40.0f, 0.0f); // clamped to the world by the camera
        pinBoundaries();
    };

    // Every death zone sends the ninja to the latest checkpoint.
    auto setCheckpoint = [&](int index)
    {
        checkpoint = index;
        objects.forEach<DeathZone>([&](GameObject &, DeathZone &dz)
                                   { dz.spawnPointName = Cave::SpawnPoints[index].name; });
        SDL_Log("Checkpoint %s reached", Cave::SpawnPoints[index].name);
    };

    // Common bookkeeping after the ninja was teleported to a spawn point.
    auto afterRespawn = [&]()
    {
        GameObject *ninja = objects.find(playerId);
        Transform *t = ninja->get<Transform>();
        grounded = false;
        ridingId = 0;
        lastFeetY = t->y + t->height;
        ++deaths;
        snapCameraToPlayer();
    };

    // Teleport to a spawn point by name (used for the wisp and the below-the-world safety net;
    // death zones teleport through GameplaySystems::updateDeathZones).
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
            camera.setViewport(Cave::ViewWidth, Cave::ViewHeight); // world units, whatever the window size

            // 1. Receive: create / update / destroy replicas (remote ninjas, server platforms).
            if (net)
                net->sync(objects, onSpawn);
            if (peers)
                peers->sync(objects, onSpawn);

            // 2. Locally driven moving platforms (shared MovementPath system). Peers align the path
            //    clock to the shared anchor so every peer sees the same platform positions.
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

            // 3. Track how far every surface moved this frame (to carry riders).
            std::unordered_map<uint32_t, SDL_FPoint> groundDelta;
            objects.forEach<Transform, Cave::Ground>([&](GameObject &g, Transform &t, Cave::Ground &)
                                                     {
                auto found = lastGroundPos.find(g.getId());
                groundDelta[g.getId()] = found == lastGroundPos.end() ? SDL_FPoint{0.0f, 0.0f}
                                                                     : SDL_FPoint{t.x - found->second.x, t.y - found->second.y};
                lastGroundPos[g.getId()] = SDL_FPoint{t.x, t.y}; });

            // 4. Wisp patrols its platform (derived from the platform, so it agrees on every client).
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

            // 5. The ninja.
            GameObject &ninja = *objects.find(playerId);
            Transform &t = *ninja.get<Transform>();
            PhysicsBody &body = *ninja.get<PhysicsBody>();
            Sprite &sprite = *ninja.get<Sprite>();
            const PlayerInput &input = *ninja.get<PlayerInput>();
            {
                std::lock_guard<std::mutex> lock(ninja.getMutex());
                const bool paused = playerTime.isPaused();

                // Ride the surface we stand on.
                if (grounded && ridingId != 0 && !paused && objects.find(ridingId) != nullptr)
                {
                    const SDL_FPoint d = groundDelta[ridingId];
                    t.x += d.x;
                    t.y += d.y;
                }

                const float axis = input.horizontalAxis();
                body.velocityX = axis * input.moveSpeed;
                if (axis < 0.0f)
                    sprite.flipHorizontal = true;
                else if (axis > 0.0f)
                    sprite.flipHorizontal = false;

                if (input.jumpJustPressed() && grounded && !paused)
                {
                    body.velocityY = -input.jumpSpeed;
                    grounded = false;
                    ridingId = 0;
                }

                // Landing: feet are the middle half of the sprite.
                const SDL_FRect feet{t.x + t.width * 0.25f, t.y, t.width * 0.5f, t.height};
                bool landed = false;
                objects.forEach<Cave::Ground>([&](GameObject &g, Cave::Ground &)
                                              {
                    if (landed)
                        return;
                    const SDL_FRect top = colliderBounds(g);
                    const float rise = std::max(0.0f, -groundDelta[g.getId()].y); // surface moving up
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
                    // Still standing if the feet rest exactly on the surface we rode.
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

                // Animation: 0 idle, 1-3 run, 2 in the air.
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

            // 6. Checkpoints: the furthest spawn point the ninja has walked past.
            for (int i = 0; i < Cave::SpawnPointCount; ++i)
            {
                if (t.x >= Cave::SpawnPoints[i].x && i > checkpoint)
                    setCheckpoint(i);
            }

            // 7. Deaths. Hidden death zones go through the shared system, which teleports the ninja
            //    to the zone's designated spawn point (the latest checkpoint); we only need to notice
            //    that it fired. The wisp and the safety net use the same checkpoint.
            std::vector<bool> wasArmed;
            objects.forEach<DeathZone>([&](GameObject &, DeathZone &dz)
                                       { wasArmed.push_back(dz.armed); });
            GameplaySystems::updateDeathZones(objects, playerId);
            bool died = false;
            std::size_t zoneIndex = 0;
            objects.forEach<DeathZone>([&](GameObject &, DeathZone &dz)
                                       {
                if (zoneIndex < wasArmed.size() && wasArmed[zoneIndex] && !dz.armed)
                    died = true;
                ++zoneIndex; });
            if (died)
            {
                afterRespawn();
            }
            else if (GameObject *w = objects.find(wispId); w && w->active && overlaps(colliderBounds(ninja), colliderBounds(*w)))
            {
                SDL_Log("The ninja was killed by the shadow wisp!");
                respawnAt(Cave::SpawnPoints[checkpoint].name);
            }
            else if (t.y > Cave::WorldHeight + 200.0f)
            {
                // Safety net: should be impossible with the deep death zones, but never strand the player.
                respawnAt(Cave::SpawnPoints[checkpoint].name);
            }

            // 8. Side scrolling: past the right boundary -> camera moves right by the overshoot,
            //    past the left boundary -> left. Compared by position (not just overlap), so it still
            //    works if the ninja moved a long way in one frame.
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

            // 9. Send what this endpoint owns.
            if (net)
                net->publish(objects);
            if (peers)
                peers->publish(objects);

            // Window title: mode, ids, players, loop rate, network round trip.
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
                              "Cave Ninja M3 | %s %u (%s) | ninjas %zu | checkpoint %s | deaths %d | loop %.0f Hz | rtt %.1f ms | %s x%.1f",
                              mode, localOwner, NetProtocol::formatName(options.format), ninjas,
                              Cave::SpawnPoints[checkpoint].name, deaths, loopHz,
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
