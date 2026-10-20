// Lost Under the Sea - Milestone 3 (Adit Shah)
//
// Step 2 shell: component GameObject model + Camera, offline playable.
// Networking publish/sync lands in a later step; --client / --p2p args are reserved.
//
// Objects (see SeaLevel.h):
//   diver          Transform, Sprite, Collider, PhysicsBody, PlayerInput
//   static shelf   Transform, Sprite (tinted), Collider, Ground
//   movers         Transform, Sprite, Collider, Ground|Hazard, MovementPath
//   spawn / death  hidden SpawnPoint / DeathZone
//   scroll edges   ScrollBoundary + Camera translate
//   pearls / buoy  unique collect + finish
//
// Keys: A/D or arrows move, W/Up/Space jump, P pause, 1/2/3 time scale,
//       TAB pixel/percentage scale, H show/hide triggers.

#include "SeaLevel.h"

#include "Components.h"
#include "DeathZone.h"
#include "Engine.h"
#include "GamePlaySystems.h"
#include "Image.h"
#include "MovementPath.h"
#include "SpawnPoint.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{
    bool isChromaMagenta(Uint8 r, Uint8 g, Uint8 b)
    {
        return r >= 160 && b >= 160 && g <= 90 && (r - g) > 70 && (b - g) > 70;
    }

    void applyMagentaKey(SDL_Surface *surface)
    {
        if (!SDL_LockSurface(surface))
            return;

        Uint8 *pixels = static_cast<Uint8 *>(surface->pixels);
        for (int y = 0; y < surface->h; ++y)
        {
            Uint8 *row = pixels + y * surface->pitch;
            for (int x = 0; x < surface->w; ++x)
            {
                Uint8 *px = row + x * 4;
                if (isChromaMagenta(px[0], px[1], px[2]))
                    px[3] = 0;
            }
        }

        SDL_UnlockSurface(surface);
    }

    SDL_Texture *loadTexture(SDL_Renderer *renderer, const char *path, bool chromaKey)
    {
        SDL_Surface *surface = loadImage(path);
        if (!surface)
        {
            throw std::runtime_error(std::string("Could not load ") + path + ": " + SDL_GetError());
        }

        if (chromaKey)
            applyMagentaKey(surface);

        SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);
        SDL_DestroySurface(surface);
        if (!texture)
        {
            throw std::runtime_error(std::string("Could not create texture for ") + path + ": " + SDL_GetError());
        }

        SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
        SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
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
            return {0.0f, 0.0f, 0.0f, 0.0f};
        return c != nullptr ? c->boundsFor(*t) : t->getBounds();
    }
}

int main(int argc, char *argv[])
{
    if (argc >= 2)
    {
        const std::string flag(argv[1]);
        if (flag == "--client" || flag == "--p2p")
        {
            std::cerr << "Networking wiring is not in this step yet. Run offline: ./ashah_game\n";
            return 1;
        }
    }

    Engine engine("Lost Under the Sea - Milestone 3", 1280, 720);
    GameObjectManager &objects = engine.getObjects();
    Camera &camera = engine.getCamera();
    SDL_Renderer *renderer = engine.getRenderer().getHandle();

    engine.getPhysics().setGravity(Sea::Gravity);
    objects.setReferenceResolution(static_cast<int>(Sea::ViewWidth), static_cast<int>(Sea::ViewHeight));
    objects.setScaleMode(ScaleMode::Percentage);
    camera.setViewport(Sea::ViewWidth, Sea::ViewHeight);
    camera.setBounds(0.0f, 0.0f, Sea::WorldWidth, Sea::WorldHeight);

    SDL_Texture *backgroundTex = loadTexture(renderer, "Games/ashah/assets/background.png", false);
    SDL_Texture *shelfTex = loadTexture(renderer, "Games/ashah/assets/shelf.png", true);
    SDL_Texture *diverTex = loadTexture(renderer, "Games/ashah/assets/diver.png", true);
    SDL_Texture *diverAltTex = loadTexture(renderer, "Games/ashah/assets/diver_alt.png", true);
    SDL_Texture *fishTex = loadTexture(renderer, "Games/ashah/assets/fish.png", true);
    SDL_Texture *bubbleTex = loadTexture(renderer, "Games/ashah/assets/bubble.png", true);

    // Background tiles across the wide world.
    for (float x = 0.0f; x < Sea::WorldWidth; x += Sea::ViewWidth)
    {
        GameObject &tile = objects.create("background");
        tile.add<Transform>(x, 0.0f, Sea::ViewWidth, Sea::ViewHeight);
        Sprite &sprite = tile.add<Sprite>(backgroundTex);
        sprite.layer = -10;
        sprite.flipHorizontal = static_cast<int>(x / Sea::ViewWidth) % 2 == 1;
    }

    // Static platforms (≥3 unique size/tint combos).
    for (const Sea::StaticPlatformDef &def : Sea::StaticPlatforms)
    {
        GameObject &platform = objects.create(def.label);
        platform.add<Transform>(def.x, def.y, def.width, def.height);
        Sprite &sprite = platform.add<Sprite>(shelfTex);
        sprite.color = SDL_Color{def.r, def.g, def.b, 255};
        platform.add<Collider>(0.0f, 0.0f, Sea::ShelfTopPadding, 0.0f);
        platform.add<Sea::Ground>();
    }

    // Movers: fish (hazard) + moving shelves.
    uint32_t fishId = 0;
    for (int i = 0; i < Sea::MoverCount; ++i)
    {
        const Sea::MovingDef &def = Sea::Movers[i];
        float x = 0.0f;
        float y = 0.0f;
        Sea::moverStart(i, x, y);

        GameObject &mover = objects.create(def.kind == Sea::KindFish ? "fish" : "moving-shelf");
        mover.add<Transform>(x, y, def.width, def.height);
        mover.add<MovementPath>(def.path, def.speed, def.amplitude);

        if (def.kind == Sea::KindFish)
        {
            Sprite &sprite = mover.add<Sprite>(fishTex, 4);
            sprite.layer = 1;
            mover.add<Collider>(0.15f, 0.15f, 0.20f, 0.15f, true);
            mover.add<Sea::Hazard>();
            fishId = mover.getId();
        }
        else
        {
            Sprite &sprite = mover.add<Sprite>(shelfTex);
            sprite.color = SDL_Color{def.r, def.g, def.b, 255};
            mover.add<Collider>(0.0f, 0.0f, Sea::ShelfTopPadding, 0.0f);
            mover.add<Sea::Ground>(i);
        }
    }

    // Hidden spawns, death zones, scroll boundaries.
    for (const Sea::SpawnDef &def : Sea::Spawns)
    {
        GameObject &spawn = objects.create("spawn-point");
        spawn.add<Transform>(def.x, def.y, 24.0f, 24.0f);
        spawn.add<SpawnPoint>(def.name);
    }
    for (const Sea::DeathZoneDef &def : Sea::DeathZones)
    {
        GameObject &zone = objects.create("death-zone");
        zone.add<Transform>(def.x, def.y, def.width, def.height);
        zone.add<Collider>(0.0f, 0.0f, 0.0f, 0.0f, true);
        zone.add<DeathZone>(def.spawnName);
    }

    GameObject &leftBoundary = objects.create("scroll-left");
    leftBoundary.add<Transform>(0.0f, -2000.0f, Sea::ScrollLeftEdge, 4000.0f);
    leftBoundary.add<Collider>(0.0f, 0.0f, 0.0f, 0.0f, true);
    leftBoundary.add<Sea::ScrollBoundary>(-1);
    GameObject &rightBoundary = objects.create("scroll-right");
    rightBoundary.add<Transform>(Sea::ScrollRightEdge, -2000.0f,
                                 Sea::ViewWidth - Sea::ScrollRightEdge, 4000.0f);
    rightBoundary.add<Collider>(0.0f, 0.0f, 0.0f, 0.0f, true);
    rightBoundary.add<Sea::ScrollBoundary>(1);
    const uint32_t leftBoundaryId = leftBoundary.getId();
    const uint32_t rightBoundaryId = rightBoundary.getId();

    // Pearls (reuse bubble art until custom sprites arrive).
    for (int i = 0; i < Sea::PearlCount; ++i)
    {
        const Sea::PearlDef &def = Sea::Pearls[i];
        GameObject &pearl = objects.create("pearl");
        pearl.add<Transform>(def.x, def.y, def.size, def.size);
        Sprite &sprite = pearl.add<Sprite>(bubbleTex, 3);
        sprite.layer = 1;
        sprite.color = SDL_Color{255, 230, 120, 255};
        pearl.add<Collider>(0.1f, 0.1f, 0.1f, 0.1f, true);
        pearl.add<Sea::Pearl>();
    }

    // Rescue buoy (placeholder colored sprite).
    GameObject &buoy = objects.create("rescue-buoy");
    buoy.add<Transform>(Sea::BuoyX, Sea::BuoyY, Sea::BuoyW, Sea::BuoyH);
    Sprite &buoySprite = buoy.add<Sprite>(SDL_Color{255, 80, 80, 255});
    buoySprite.layer = 1;
    buoy.add<Collider>(0.0f, 0.0f, 0.0f, 0.0f, true);
    buoy.add<Sea::RescueBuoy>();

    // Diver (orange suit offline; alt suit reserved for networked peers later).
    float diverTexW = 0.0f;
    float diverTexH = 0.0f;
    SDL_GetTextureSize(diverTex, &diverTexW, &diverTexH);
    const float diverH = Sea::ViewHeight * 0.13f;
    const float diverW = diverH * ((diverTexW / 4.0f) / diverTexH) * 1.20f;

    GameObject &diver = objects.create("diver");
    diver.add<Transform>(Sea::Spawns[0].x, Sea::Spawns[0].y, diverW, diverH);
    Sprite &diverSprite = diver.add<Sprite>(diverTex, 4);
    diverSprite.layer = 2;
    diver.add<Collider>(0.20f, 0.20f, 0.08f, Sea::DiverBottomPadding);
    diver.add<PhysicsBody>(Engine::PlayerTime, true);
    diver.add<PlayerInput>(Sea::MoveSpeed, Sea::JumpSpeed);
    const uint32_t diverId = diver.getId();
    (void)diverAltTex; // used when networking lands

    // HUD hearts (screen-space-ish: follow camera each frame).
    uint32_t heartIds[Sea::MaxHealth];
    for (int i = 0; i < Sea::MaxHealth; ++i)
    {
        GameObject &heart = objects.create("heart");
        heart.add<Transform>(28.0f + i * 36.0f, 28.0f, 24.0f, 24.0f);
        heart.add<Sprite>(SDL_Color{220, 40, 50, 255}).layer = 50;
        heart.add<Sea::HudHeart>().index = i;
        heartIds[i] = heart.getId();
    }

    Timeline &playerTime = engine.getTimeline(Engine::PlayerTime);
    Timeline &globalTime = engine.getGlobalTimeline();

    bool grounded = false;
    uint32_t ridingId = 0;
    float lastFeetY = 0.0f;
    float walkTimer = 0.0f;
    int runIndex = 0;
    int checkpoint = 0;
    int health = Sea::MaxHealth;
    int pearlsCollected = 0;
    bool rescued = false;
    bool showHidden = false;
    bool fishContact = false;
    const char *story = "Collect pearls, avoid the fish, reach the red buoy.";

    std::unordered_map<uint32_t, SDL_FPoint> lastGroundPos;
    int64_t lastTitleUpdate = 0;
    uint64_t framesSinceTitle = 0;

    auto pinBoundaries = [&]()
    {
        objects.find(leftBoundaryId)->get<Transform>()->x = camera.getX();
        objects.find(rightBoundaryId)->get<Transform>()->x = camera.getX() + Sea::ScrollRightEdge;
    };

    auto snapCameraToDiver = [&]()
    {
        const float x = objects.find(diverId)->get<Transform>()->x;
        camera.setPosition(x - Sea::ScrollLeftEdge - 40.0f, 0.0f);
        pinBoundaries();
    };

    auto setCheckpoint = [&](int index)
    {
        checkpoint = index;
        objects.forEach<DeathZone>([&](GameObject &, DeathZone &dz)
                                   { dz.spawnPointName = Sea::Spawns[index].name; });
    };

    auto refreshHearts = [&]()
    {
        for (int i = 0; i < Sea::MaxHealth; ++i)
        {
            if (GameObject *h = objects.find(heartIds[i]))
                h->active = i < health;
        }
    };

    auto placeHeartsOnCamera = [&]()
    {
        for (int i = 0; i < Sea::MaxHealth; ++i)
        {
            if (GameObject *h = objects.find(heartIds[i]))
            {
                Transform *t = h->get<Transform>();
                t->x = camera.getX() + 28.0f + i * 36.0f;
                t->y = camera.getY() + 28.0f;
            }
        }
    };

    auto afterRespawn = [&]()
    {
        grounded = false;
        ridingId = 0;
        fishContact = false;
        Transform *t = objects.find(diverId)->get<Transform>();
        lastFeetY = t->y + t->height;
        snapCameraToDiver();
    };

    auto respawnAt = [&](const std::string &name)
    {
        GameObject *spawn = GameplaySystems::findSpawnPointByName(objects, name);
        if (spawn == nullptr)
            return;
        GameObject *player = objects.find(diverId);
        Transform *t = player->get<Transform>();
        PhysicsBody *body = player->get<PhysicsBody>();
        t->x = spawn->get<Transform>()->x;
        t->y = spawn->get<Transform>()->y;
        body->velocityX = 0.0f;
        body->velocityY = 0.0f;
        afterRespawn();
    };

    auto loseLife = [&](const char *message)
    {
        if (health > 0)
            --health;
        refreshHearts();
        story = message;
        if (health == 0)
        {
            health = Sea::MaxHealth;
            pearlsCollected = 0;
            rescued = false;
            objects.forEach<Sea::Pearl>([&](GameObject &o, Sea::Pearl &pearl)
                                        {
                pearl.collected = false;
                o.active = true;
                if (Sprite *s = o.get<Sprite>())
                    s->visible = true; });
            refreshHearts();
            story = "Out of air — try again. Collect pearls, reach the buoy.";
            setCheckpoint(0);
        }
        respawnAt(Sea::Spawns[checkpoint].name);
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
            else if (object->has<Sea::ScrollBoundary>())
                color = SDL_Color{80, 160, 255, 90};
            else
                continue;

            if (showHidden)
                object->add<Sprite>(color).layer = 20;
            else
                object->remove<Sprite>();
        }
    };

    refreshHearts();
    snapCameraToDiver();
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    engine.run(
        [&](float worldDelta)
        {
            const float playerDelta = static_cast<float>(playerTime.getLastDelta());
            camera.setViewport(Sea::ViewWidth, Sea::ViewHeight);

            GameplaySystems::updateMovementPaths(objects, worldDelta);

            std::unordered_map<uint32_t, SDL_FPoint> groundDelta;
            objects.forEach<Transform, Sea::Ground>(
                [&](GameObject &g, Transform &t, Sea::Ground &)
                {
                    auto found = lastGroundPos.find(g.getId());
                    groundDelta[g.getId()] = found == lastGroundPos.end()
                                                 ? SDL_FPoint{0.0f, 0.0f}
                                                 : SDL_FPoint{t.x - found->second.x, t.y - found->second.y};
                    lastGroundPos[g.getId()] = SDL_FPoint{t.x, t.y};
                });

            if (Input::isKeyJustPressed(SDL_SCANCODE_TAB))
                objects.toggleScaleMode();
            if (Input::isKeyJustPressed(SDL_SCANCODE_H))
                toggleHiddenDebug();

            GameObject &player = *objects.find(diverId);
            Transform &t = *player.get<Transform>();
            PhysicsBody &body = *player.get<PhysicsBody>();
            Sprite &sprite = *player.get<Sprite>();
            const PlayerInput &input = *player.get<PlayerInput>();
            {
                std::lock_guard<std::mutex> lock(player.getMutex());
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
                objects.forEach<Sea::Ground>(
                    [&](GameObject &g, Sea::Ground &)
                    {
                        if (landed)
                            return;
                        const SDL_FRect top = colliderBounds(g);
                        const float rise = std::max(0.0f, -groundDelta[g.getId()].y);
                        if (body.velocityY >= 0.0f && overlaps(feet, top) &&
                            lastFeetY <= top.y + 1.0f + rise)
                        {
                            t.y = top.y - t.height;
                            body.velocityY = 0.0f;
                            grounded = true;
                            ridingId = g.getId();
                            landed = true;
                        }
                    });

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
                    sprite.frame = 1 + runIndex;
                }
                else
                {
                    walkTimer = 0.0f;
                    sprite.frame = 0;
                }

                t.x = std::clamp(t.x, 0.0f, Sea::WorldWidth - t.width);
                lastFeetY = t.y + t.height;
            }

            // Checkpoints by walking past spawn x.
            for (int i = 0; i < Sea::SpawnCount; ++i)
            {
                if (t.x >= Sea::Spawns[i].x && i > checkpoint)
                    setCheckpoint(i);
            }

            // Death zones.
            std::vector<bool> wasArmed;
            objects.forEach<DeathZone>([&](GameObject &, DeathZone &dz)
                                       { wasArmed.push_back(dz.armed); });
            GameplaySystems::updateDeathZones(objects, diverId);
            bool fellInPit = false;
            std::size_t zoneIndex = 0;
            objects.forEach<DeathZone>(
                [&](GameObject &, DeathZone &dz)
                {
                    if (zoneIndex < wasArmed.size() && wasArmed[zoneIndex] && !dz.armed)
                        fellInPit = true;
                    ++zoneIndex;
                });
            if (fellInPit)
            {
                loseLife("Fell into the deep — respawned.");
            }
            else if (t.y > Sea::WorldHeight + 200.0f)
            {
                loseLife("Lost in the depths — respawned.");
            }

            // Fish hazard.
            if (GameObject *fish = objects.find(fishId); fish && fish->active)
            {
                if (Sprite *fs = fish->get<Sprite>())
                    fs->frame = static_cast<int>(globalTime.getSeconds() * 8.0) % 4;

                if (overlaps(colliderBounds(player), colliderBounds(*fish)))
                {
                    if (!fishContact)
                    {
                        fishContact = true;
                        loseLife("The fish bit you!");
                    }
                }
                else
                {
                    fishContact = false;
                }
            }

            // Pearls.
            objects.forEach<Transform, Sea::Pearl>(
                [&](GameObject &o, Transform &, Sea::Pearl &pearl)
                {
                    if (pearl.collected || !o.active)
                        return;
                    if (overlaps(colliderBounds(player), colliderBounds(o)))
                    {
                        pearl.collected = true;
                        o.active = false;
                        if (Sprite *s = o.get<Sprite>())
                            s->visible = false;
                        ++pearlsCollected;
                        char buf[96];
                        std::snprintf(buf, sizeof(buf), "Pearl %d/%d", pearlsCollected, Sea::PearlsNeededToWin);
                        story = buf;
                    }
                });

            // Rescue buoy win.
            if (!rescued && overlaps(colliderBounds(player), colliderBounds(buoy)))
            {
                if (pearlsCollected >= Sea::PearlsNeededToWin)
                {
                    rescued = true;
                    story = "Rescued! You gathered enough pearls.";
                }
                else
                {
                    story = "Need more pearls before the buoy will take you.";
                }
            }

            // Side scroll.
            {
                const SDL_FRect box = colliderBounds(player);
                const SDL_FRect right = colliderBounds(*objects.find(rightBoundaryId));
                const SDL_FRect left = colliderBounds(*objects.find(leftBoundaryId));
                if (box.x + box.w > right.x)
                    camera.translate(box.x + box.w - right.x, 0.0f);
                else if (box.x < left.x + left.w)
                    camera.translate(box.x - (left.x + left.w), 0.0f);
                pinBoundaries();
            }

            placeHeartsOnCamera();

            ++framesSinceTitle;
            const int64_t now = globalTime.getTime();
            if (now - lastTitleUpdate >= 250'000'000)
            {
                framesSinceTitle = 0;
                lastTitleUpdate = now;
                char title[320];
                std::snprintf(title, sizeof(title),
                              "Lost Under the Sea | pearls %d/%d | HP %d | %s | %s",
                              pearlsCollected, Sea::PearlsNeededToWin, health,
                              playerTime.isPaused() ? "PAUSED" : "running",
                              story);
                SDL_SetWindowTitle(engine.getWindow().getHandle(), title);
            }
        });

    return 0;
}
