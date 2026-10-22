#include "KittyLevel.h"

#include "Components.h"
#include "DeathZone.h"
#include "Engine.h"
#include "GamePlaySystems.h"
#include "Image.h"
#include "MovementPath.h"
#include "NetworkClient.h"
#include "NetworkIdentity.h"
#include "PeerClock.h"
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

    Engine engine("Hello Kitty Adventure", static_cast<int>(Kitty::ViewWidth), static_cast<int>(Kitty::ViewHeight));
    GameObjectManager &objects = engine.getObjects();
    Camera &camera = engine.getCamera();
    SDL_Renderer *renderer = engine.getRenderer().getHandle();

    objects.setReferenceResolution(static_cast<int>(Kitty::ViewWidth), static_cast<int>(Kitty::ViewHeight));
    objects.setScaleMode(ScaleMode::Percentage);
    camera.setViewport(Kitty::ViewWidth, Kitty::ViewHeight);
    camera.setBounds(0.0f, 0.0f, Kitty::WorldWidth, Kitty::WorldHeight);

    SDL_Texture *backgroundTexture = loadTexture(renderer, "Games/ymherya/assets/background.png");
    SDL_Texture *kittyTexture = loadTexture(renderer, "Games/ymherya/assets/hello-kitty.png");
    SDL_Texture *kuromiTexture = loadTexture(renderer, "Games/ymherya/assets/kuromi.png");
    SDL_Texture *bigCloudTexture = loadTexture(renderer, "Games/ymherya/assets/big_p.png");
    SDL_Texture *smallCloudTexture = loadTexture(renderer, "Games/ymherya/assets/small_p.png");
    SDL_Texture *heartTexture = loadTexture(renderer, "Games/ymherya/assets/heart.png");
    SDL_Texture *brokenHeartTexture = loadTexture(renderer, "Games/ymherya/assets/no_heart.png");

    for (float x = 0.0f; x < Kitty::WorldWidth; x += Kitty::ViewWidth)
    {
        GameObject &tile = objects.create("background");
        tile.add<Transform>(x, 0.0f, Kitty::ViewWidth, Kitty::ViewHeight);
        Sprite &sprite = tile.add<Sprite>(backgroundTexture);
        sprite.layer = -10;
    }

    const float groundHeight = Kitty::ViewHeight - Kitty::GroundY;
    for (const Kitty::GroundStripDef &strip : Kitty::GroundStrips)
    {
        GameObject &ground = objects.create("ground");
        ground.add<Transform>(strip.x, Kitty::GroundY, strip.width, groundHeight);
        ground.add<Collider>(0.0f, 0.0f, 0.0f, 0.0f);
        ground.add<Kitty::Ground>();
    }

    auto dressCloud = [&](GameObject &cloud, bool big, int movingIndex = -1)
    {
        Sprite *sprite = cloud.get<Sprite>();
        if (sprite == nullptr)
            sprite = &cloud.add<Sprite>();
        sprite->texture = big ? bigCloudTexture : smallCloudTexture;
        sprite->frameCount = 1;
        sprite->layer = 0;
        cloud.add<Collider>(Kitty::CloudPadLeft, Kitty::CloudPadRight, Kitty::CloudPadTop, 0.0f);
        cloud.add<Kitty::Ground>(movingIndex);
    };

    for (const Kitty::StaticCloudDef &def : Kitty::StaticClouds)
    {
        GameObject &cloud = objects.create("static-cloud");
        cloud.add<Transform>(def.x, def.y, def.width, def.height);
        dressCloud(cloud, def.big);
    }

    if (!net)
    {
        for (int i = 0; i < Kitty::MovingCloudCount; ++i)
        {
            const Kitty::MovingCloudDef &def = Kitty::MovingClouds[i];
            float x = 0.0f;
            float y = 0.0f;
            Kitty::movingCloudStart(i, x, y);
            GameObject &cloud = objects.create("moving-cloud");
            cloud.add<Transform>(x, y, def.width, def.height);
            cloud.add<MovementPath>(def.path, def.speed, def.amplitude);
            dressCloud(cloud, def.big, i);
        }
    }

    uint32_t kuromiId = 0;
    if (!net)
    {
        GameObject &kuromi = objects.create("kuromi");
        kuromi.add<Transform>(Kitty::KuromiCenterX, Kitty::KuromiY, Kitty::KuromiWidth, Kitty::KuromiHeight);
        Sprite &sprite = kuromi.add<Sprite>(kuromiTexture, 8);
        sprite.layer = 1;
        kuromi.add<Collider>(0.20f, 0.20f, 0.15f, 0.05f, true);
        kuromi.add<MovementPath>(MovementPathType::Horizontal, Kitty::KuromiSpeed, Kitty::KuromiAmplitude);
        kuromi.add<Kitty::Hazard>();
        kuromiId = kuromi.getId();
    }

    for (const Kitty::SpawnPointDef &def : Kitty::SpawnPoints)
    {
        GameObject &spawn = objects.create("spawn-point");
        spawn.add<Transform>(def.x, def.y, 24.0f, 24.0f);
        spawn.add<SpawnPoint>(def.name);
    }

    for (const Kitty::DeathZoneDef &def : Kitty::DeathZones)
    {
        GameObject &zone = objects.create("death-zone");
        zone.add<Transform>(def.x, def.y, def.width, def.height);
        zone.add<Collider>(0.0f, 0.0f, 0.0f, 0.0f, true);
        zone.add<DeathZone>(def.spawnName);
    }

    GameObject &leftBoundary = objects.create("scroll-left");
    leftBoundary.add<Transform>(0.0f, -2000.0f, Kitty::ScrollLeftEdge, 4000.0f);
    leftBoundary.add<Collider>(0.0f, 0.0f, 0.0f, 0.0f, true);
    leftBoundary.add<Kitty::ScrollBoundary>(-1);
    GameObject &rightBoundary = objects.create("scroll-right");
    rightBoundary.add<Transform>(Kitty::ScrollRightEdge, -2000.0f, Kitty::ViewWidth - Kitty::ScrollRightEdge, 4000.0f);
    rightBoundary.add<Collider>(0.0f, 0.0f, 0.0f, 0.0f, true);
    rightBoundary.add<Kitty::ScrollBoundary>(1);
    const uint32_t leftBoundaryId = leftBoundary.getId();
    const uint32_t rightBoundaryId = rightBoundary.getId();

    const float moveSpeed = 350.0f;
    const float jumpSpeed = 700.0f;
    const float spawnOffset = 40.0f * static_cast<float>(localOwner % 4);
    GameObject &player = objects.create("hello-kitty");
    player.add<Transform>(Kitty::SpawnPoints[0].x + spawnOffset, Kitty::SpawnPoints[0].y, Kitty::KittyWidth, Kitty::KittyHeight);
    Sprite &playerSprite = player.add<Sprite>(kittyTexture, 8);
    playerSprite.layer = 2;
    player.add<Collider>(0.20f, 0.20f, 0.10f, 0.10f);
    player.add<PhysicsBody>(Engine::PlayerTime, true);
    player.add<PlayerInput>(moveSpeed, jumpSpeed);
    if (net || peers)
    {
        player.add<NetworkIdentity>(localOwner, Kitty::KindKitty);
    }
    const uint32_t playerId = player.getId();

    constexpr int MaxLives = 3;
    const float heartSize = 38.0f;
    const float heartGap = 10.0f;
    std::vector<uint32_t> heartIds;
    for (int i = 0; i < MaxLives; ++i)
    {
        GameObject &heart = objects.create("heart");
        heart.add<Transform>(24.0f + i * (heartSize + heartGap), 20.0f, heartSize, heartSize);
        Sprite &sprite = heart.add<Sprite>(heartTexture);
        sprite.layer = 30;
        heart.add<Kitty::HudHeart>(i);
        heartIds.push_back(heart.getId());
    }

    const NetReplicator::SpawnFn onSpawn = [&](GameObject &replica, const ObjectState &state)
    {
        if (state.kind == Kitty::KindKitty)
        {
            Sprite *sprite = replica.get<Sprite>();
            sprite->texture = kittyTexture;
            sprite->frameCount = 8;
            sprite->layer = 1;
            sprite->color = SDL_Color{255, 200, 220, 230};
        }
        else if (state.kind == Kitty::KindMovingCloud)
        {
            const int index = static_cast<int>(state.netId) - 1;
            const bool big = index >= 0 && index < Kitty::MovingCloudCount ? Kitty::MovingClouds[index].big : false;
            dressCloud(replica, big, index >= 0 ? index : -1);
        }
        else if (state.kind == Kitty::KindKuromi)
        {
            Sprite *sprite = replica.get<Sprite>();
            sprite->texture = kuromiTexture;
            sprite->frameCount = 8;
            sprite->layer = 1;
            replica.add<Collider>(0.20f, 0.20f, 0.15f, 0.05f, true);
            replica.add<Kitty::Hazard>();
            kuromiId = replica.getId();
        }
    };

    Timeline &playerTime = engine.getTimeline(Engine::PlayerTime);
    Timeline &globalTime = engine.getGlobalTimeline();

    bool grounded = false;
    uint32_t ridingId = 0;
    float lastFeetY = 0.0f;
    float walkTimer = 0.0f;
    float kuromiAnimTimer = 0.0f;
    float kuromiPrevX = Kitty::KuromiCenterX;
    int lives = MaxLives;
    int deaths = 0;
    double deathCooldown = 2.0;
    int lastAnnouncedCloud = -1;
    std::unordered_map<uint32_t, SDL_FPoint> lastGroundPos;

    int64_t lastTitleUpdate = 0;
    uint64_t framesSinceTitle = 0;
    std::string story = "Jump across the pink clouds. Kuromi guards the ground.";

    auto pinBoundaries = [&]()
    {
        objects.find(leftBoundaryId)->get<Transform>()->x = camera.getX();
        objects.find(rightBoundaryId)->get<Transform>()->x = camera.getX() + Kitty::ScrollRightEdge;
    };

    auto snapCameraToPlayer = [&]()
    {
        const float x = objects.find(playerId)->get<Transform>()->x;
        camera.setPosition(x - Kitty::ScrollLeftEdge - 40.0f, 0.0f);
        pinBoundaries();
    };

    auto updateHearts = [&]()
    {
        for (int i = 0; i < MaxLives; ++i)
        {
            if (GameObject *heart = objects.find(heartIds[static_cast<size_t>(i)]))
            {
                Transform *t = heart->get<Transform>();
                t->x = camera.getX() + 24.0f + i * (heartSize + heartGap);
                t->y = camera.getY() + 20.0f;
                heart->get<Sprite>()->texture = i < lives ? heartTexture : brokenHeartTexture;
            }
        }
    };

    auto announce = [&](const std::string &text)
    {
        story = text;
        SDL_Log("%s", story.c_str());
    };

    auto afterRespawn = [&]()
    {
        GameObject *kitty = objects.find(playerId);
        Transform *t = kitty->get<Transform>();
        grounded = false;
        ridingId = 0;
        lastFeetY = t->y + t->height;
        lastAnnouncedCloud = -1;
        snapCameraToPlayer();
        updateHearts();
    };

    auto respawnAt = [&](const std::string &name)
    {
        GameObject *spawn = GameplaySystems::findSpawnPointByName(objects, name);
        if (spawn == nullptr)
            return;
        GameObject *kitty = objects.find(playerId);
        Transform *t = kitty->get<Transform>();
        PhysicsBody *body = kitty->get<PhysicsBody>();
        t->x = spawn->get<Transform>()->x;
        t->y = spawn->get<Transform>()->y;
        body->velocityX = 0.0f;
        body->velocityY = 0.0f;
        afterRespawn();
    };

    auto loseLife = [&](const std::string &reason, const std::string &spawnName)
    {
        if (deathCooldown > 0.0)
            return;
        ++deaths;
        --lives;
        std::string message = reason;
        std::string target = spawnName;
        if (lives <= 0)
        {
            lives = MaxLives;
            target = Kitty::SpawnPoints[0].name;
            message += " Out of lives after " + std::to_string(deaths) + " deaths. Hearts refilled.";
        }
        else
        {
            message += " Lives left: " + std::to_string(lives) + "/" + std::to_string(MaxLives) + ".";
        }
        deathCooldown = 1.0;
        respawnAt(target);
        announce(message);
    };

    announce(story);
    updateHearts();
    if (net)
    {
        SDL_Log("Connected to server as client %u.", localOwner);
    }

    engine.run(
        [&](float worldDelta)
        {
            const float playerDelta = static_cast<float>(playerTime.getLastDelta());
            camera.setViewport(Kitty::ViewWidth, Kitty::ViewHeight);

            if (deathCooldown > 0.0)
            {
                deathCooldown -= static_cast<double>(worldDelta);
                if (deathCooldown < 0.0)
                    deathCooldown = 0.0;
            }

            if (net)
                net->sync(objects, onSpawn);
            if (peers)
                peers->sync(objects, onSpawn);

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
            objects.forEach<Transform, Kitty::Ground>([&](GameObject &g, Transform &t, Kitty::Ground &)
                                                      {
                auto found = lastGroundPos.find(g.getId());
                groundDelta[g.getId()] = found == lastGroundPos.end() ? SDL_FPoint{0.0f, 0.0f}
                                                                      : SDL_FPoint{t.x - found->second.x, t.y - found->second.y};
                lastGroundPos[g.getId()] = SDL_FPoint{t.x, t.y}; });

            if (Input::isKeyJustPressed(SDL_SCANCODE_TAB))
                objects.toggleScaleMode();

            GameObject &kitty = *objects.find(playerId);
            Transform &t = *kitty.get<Transform>();
            PhysicsBody &body = *kitty.get<PhysicsBody>();
            Sprite &sprite = *kitty.get<Sprite>();
            const PlayerInput &input = *kitty.get<PlayerInput>();
            {
                std::lock_guard<std::mutex> lock(kitty.getMutex());
                const bool paused = playerTime.isPaused();

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

                const SDL_FRect feet{t.x + t.width * 0.25f, t.y, t.width * 0.5f, t.height};
                bool landed = false;
                int landedIndex = -1;
                objects.forEach<Kitty::Ground>([&](GameObject &g, Kitty::Ground &ground)
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
                        landedIndex = ground.movingIndex;
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

                if (axis != 0.0f && grounded)
                {
                    walkTimer += playerDelta;
                    if (walkTimer >= 0.10f)
                    {
                        walkTimer = 0.0f;
                        sprite.frame = (sprite.frame + 1) % sprite.frameCount;
                    }
                }
                else if (!grounded)
                {
                    sprite.frame = 2 % sprite.frameCount;
                }
                else
                {
                    walkTimer = 0.0f;
                    sprite.frame = 0;
                }

                t.x = std::clamp(t.x, 0.0f, Kitty::WorldWidth - t.width);
                lastFeetY = t.y + t.height;

                if (landed && landedIndex != lastAnnouncedCloud && ridingId != 0)
                {
                    if (GameObject *g = objects.find(ridingId); g && g->has<Sprite>())
                    {
                        lastAnnouncedCloud = landedIndex;
                        announce(g->has<MovementPath>() ? "Hello Kitty hopped onto a moving cloud!"
                                                        : "Hello Kitty landed on a pink cloud!");
                    }
                }
            }

            if (GameObject *kuromi = kuromiId != 0 ? objects.find(kuromiId) : nullptr)
            {
                Transform *kt = kuromi->get<Transform>();
                Sprite *ks = kuromi->get<Sprite>();
                if (kt && ks)
                {
                    const float dx = kt->x - kuromiPrevX;
                    if (dx > 0.01f)
                        ks->flipHorizontal = false;
                    else if (dx < -0.01f)
                        ks->flipHorizontal = true;
                    if (std::fabs(dx) > 0.01f)
                    {
                        kuromiAnimTimer += worldDelta;
                        if (kuromiAnimTimer >= 0.10f)
                        {
                            kuromiAnimTimer = 0.0f;
                            ks->frame = (ks->frame + 1) % ks->frameCount;
                        }
                    }
                    else
                    {
                        ks->frame = 0;
                        kuromiAnimTimer = 0.0f;
                    }
                    kuromiPrevX = kt->x;
                }
            }
            else if (net)
            {
                objects.forEach<NetworkIdentity, Kitty::Hazard>([&](GameObject &object, NetworkIdentity &, Kitty::Hazard &)
                                                               { kuromiId = object.getId(); });
            }

            std::vector<bool> wasArmed;
            objects.forEach<DeathZone>([&](GameObject &, DeathZone &dz)
                                       { wasArmed.push_back(dz.armed); });
            GameplaySystems::updateDeathZones(objects, playerId);
            bool pitDeath = false;
            std::string pitSpawn = Kitty::SpawnPoints[0].name;
            std::size_t zoneIndex = 0;
            objects.forEach<DeathZone>([&](GameObject &, DeathZone &dz)
                                       {
                if (zoneIndex < wasArmed.size() && wasArmed[zoneIndex] && !dz.armed)
                {
                    pitDeath = true;
                    pitSpawn = dz.spawnPointName;
                }
                ++zoneIndex; });
            if (pitDeath)
            {
                loseLife("Hello Kitty fell into a pit!", pitSpawn);
            }
            else if (deathCooldown <= 0.0 && kuromiId != 0)
            {
                if (GameObject *kuromi = objects.find(kuromiId);
                    kuromi && kuromi->active && overlaps(colliderBounds(kitty), colliderBounds(*kuromi)))
                {
                    loseLife("Oh no! Kuromi caught Hello Kitty!", Kitty::SpawnPoints[0].name);
                }
            }
            else if (t.y > Kitty::WorldHeight + 200.0f)
            {
                loseLife("Hello Kitty fell out of the sky!", Kitty::SpawnPoints[0].name);
            }

            {
                const SDL_FRect box = colliderBounds(kitty);
                const SDL_FRect right = colliderBounds(*objects.find(rightBoundaryId));
                const SDL_FRect left = colliderBounds(*objects.find(leftBoundaryId));
                if (box.x + box.w > right.x)
                    camera.translate(box.x + box.w - right.x, 0.0f);
                else if (box.x < left.x + left.w)
                    camera.translate(box.x - (left.x + left.w), 0.0f);
                pinBoundaries();
            }

            updateHearts();

            if (net)
                net->publish(objects);
            if (peers)
                peers->publish(objects);

            ++framesSinceTitle;
            const int64_t now = globalTime.getTime();
            if (now - lastTitleUpdate >= 250'000'000)
            {
                const double seconds = static_cast<double>(now - lastTitleUpdate) / 1'000'000'000.0;
                const double loopHz = seconds > 0.0 ? static_cast<double>(framesSinceTitle) / seconds : 0.0;
                framesSinceTitle = 0;
                lastTitleUpdate = now;
                std::size_t kitties = 1;
                objects.forEach<NetworkIdentity>([&](GameObject &, NetworkIdentity &id)
                                                 {
                    if (id.isReplica && id.kind == Kitty::KindKitty)
                        ++kitties; });
                const char *mode = net ? "client" : (peers ? "peer" : "offline");
                char title[320];
                std::snprintf(title, sizeof(title),
                              "Hello Kitty Adventure | %s | lives %d/%d | kitties %zu | %.0f Hz | %s",
                              mode, lives, MaxLives, kitties, loopHz, story.c_str());
                SDL_SetWindowTitle(engine.getWindow().getHandle(), title);
            }
        });

    if (net)
        net->disconnect();
    SDL_DestroyTexture(backgroundTexture);
    SDL_DestroyTexture(kittyTexture);
    SDL_DestroyTexture(kuromiTexture);
    SDL_DestroyTexture(bigCloudTexture);
    SDL_DestroyTexture(smallCloudTexture);
    SDL_DestroyTexture(heartTexture);
    SDL_DestroyTexture(brokenHeartTexture);
    return 0;
}
