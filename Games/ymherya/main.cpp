// Hello Kitty Adventure - Milestone 4 (CSC 481)
// Gameplay is driven by the shared event system (see Docs/Design/Design_Events.md and GameplayEvents.md):
//   keys            -> Input events      -> PlayerIntent (movement), plus the R "rescue" action
//   overlaps        -> Collision events  -> Kuromi hit, pit (DeathZone), scroll triggers
//   Collision/world -> Death event       -> lose a heart, then the rules name a spawn point
//   Death           -> Spawn event       -> engine moves Kitty to the checkpoint
//   Networked: Death raises FriendFell, R raises Rescue; both are shareable and cross the server
//   through NetEventBridge. The fallen friend's Kitty turns red and a countdown bar shows the rescue window.
// Replay is optional for 481 and is not used here.

#include "KittyLevel.h"

#include "Components.h"
#include "DeathZone.h"
#include "Engine.h"
#include "EventObjects.h"
#include "GamePlayEvents.h"
#include "GamePlaySystems.h"
#include "Image.h"
#include "MovementPath.h"
#include "NetEventBridge.h"
#include "NetworkClient.h"
#include "NetworkIdentity.h"
#include "PlayerIntent.h"
#include "RescueTracker.h"
#include "PeerClock.h"
#include "PeerSession.h"
#include "SpawnPoint.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
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
        // Raise the walkable top by KittyFeetLift so Kitty's feet sit on the ground line (the strip is
        // invisible; its bottom edge stays where it was). Kuromi still uses GroundY directly.
        ground.add<Transform>(strip.x, Kitty::GroundY - Kitty::KittyFeetLift, strip.width,
                              groundHeight + Kitty::KittyFeetLift);
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
    // Moved to the rescuer's position when a friend pulls Kitty out (see the Rescue handler).
    GameObject &rescuePoint = objects.create("rescue-point");
    rescuePoint.add<Transform>(0.0f, 0.0f, 24.0f, 24.0f);
    rescuePoint.add<SpawnPoint>("rescue");

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
    player.add<PlayerIntent>(); // movement comes from Input events (see InputSystem::poll)
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
    // Networked Kuromi is a replica: her x only changes when a server snapshot arrives, so she steps and
    // looks laggy. These smooth her on the client (dead reckoning between snapshots, then easing).
    bool kuromiSmoothInit = false;
    float kuromiSmoothX = 0.0f;   // x actually drawn
    float kuromiTargetX = 0.0f;   // where snapshots say she is (extrapolated between snapshots)
    float kuromiLastWritten = 0.0f;
    float kuromiVelX = 0.0f;      // estimated px/s from consecutive snapshots
    float kuromiSinceSnap = 0.0f;
    int kuromiFrame = 0;          // Kuromi's own walk frame and facing (see the animation block)
    bool kuromiFlip = false;
    float kuromiLastRaw = 0.0f;   // previous snapshot x
    int lives = MaxLives;
    // Rescue bookkeeping (see RescueTracker.h): every fall has an id, and only the first matching rescue counts.
    constexpr double RescueWindow = 6.0;
    constexpr double LatencyGrace = 1.0; // the fallen player's window is a little longer than the rescuer's
    Kitty::FallTracker myFall(localOwner);
    Kitty::PendingRescues pending;
    Kitty::RescueAttempts attempts; // rescues this client sent and is waiting to hear about
    int deaths = 0;
    double deathCooldown = 2.0;
    int lastAnnouncedCloud = -1;
    double flashTimer = 0.0;                       // Kitty flashes red when hurt, yellow when rescued
    SDL_Color flashColor{255, 255, 255, 255};
    std::unordered_map<uint32_t, SDL_FPoint> lastGroundPos;

    int64_t lastTitleUpdate = 0;
    uint64_t framesSinceTitle = 0;
    std::string story = "Jump across the pink clouds. Kuromi guards the ground. Press R to rescue a fallen friend.";

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

    // ---- Gameplay events: Input -> Collision -> Death -> Spawn ----
    // Producers (InputSystem::poll in Engine::run, produceCollisions/produceOutOfBounds below) only raise
    // events. The rules here are the handlers' game-specific half: Kitty decides lives, cooldown and
    // where to respawn. The engine moves the player to the spawn point and zeroes its velocity.
    GameplayEvents gameplay(engine.getEvents(), objects, camera);

    GameplayEvents::Rules rules;

    // Collision: touching Kuromi (a trigger collider) is a hit unless Kitty is still in her post-death grace.
    rules.onCollision = [&](GameObject &who, GameObject &other, const Event &)
    {
        if (who.getId() != playerId || !other.has<Kitty::Hazard>() || deathCooldown > 0.0)
            return;
        gameplay.raiseDeath(who, &other, "kuromi");
    };

    // Death: lose a life (unless in grace) and say where to respawn. Returning a spawn name raises the Spawn event.
    rules.onDeath = [&](GameObject &victim, const Event &death) -> std::optional<std::string>
    {
        if (victim.getId() != playerId)
            return std::nullopt;

        const auto *payload = std::get_if<DeathEvent>(&death.payload);
        const std::string reason = payload ? payload->reason : std::string();
        std::string message;
        std::string target = Kitty::SpawnPoints[0].name;
        if (reason == "deathzone")
        {
            message = "Hello Kitty fell into a pit!";
            target = gameplay.defaultSpawnFor(death); // the pit's own checkpoint
        }
        else if (reason == "kuromi")
            message = "Oh no! Kuromi caught Hello Kitty!";
        else
            message = "Hello Kitty fell out of the sky!";

        if (deathCooldown > 0.0)
            return target; // still respawn (never leave her falling), but no life is lost

        ++deaths;
        --lives;
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
        flashTimer = 0.5;
        flashColor = SDL_Color{255, 80, 80, 255};
        announce(message);

        // Networked interaction (1/3): tell the other players this Kitty fell so they can rescue her.
        Event fell;
        fell.type = Kitty::EventFriendFell;
        fell.priority = EventPriority::High;
        fell.shareable = (net != nullptr); // shareable events are forwarded to the server
        const int64_t fallId = myFall.beginFall(RescueWindow + LatencyGrace);
        fell.payload = EventFields{{"owner", static_cast<int64_t>(localOwner)}, {"fall", fallId}};
        engine.getEvents().raise(std::move(fell));
        return target;
    };

    // Spawn: the engine has already set position/velocity; reset Kitty's own movement state and the camera.
    rules.onSpawned = [&](GameObject &subject, const Event &)
    {
        if (subject.getId() == playerId)
            afterRespawn();
    };

    gameplay.setRules(rules);

    // ---- Networked interaction: cloud rescue (client <-> server <-> client) ----
    //  1. A real death raises EventFriendFell (shareable).
    //  2. NetEventBridge hands shareable local events to NetworkClient::sendEvent; the server orders
    //     them and delivers them to every OTHER client, which enqueues them (never handles on the network thread).
    //  3. A friend sees "Kitty N fell", presses R within the window, which raises an Input event; its handler
    //     raises EventRescue (shareable) carrying the rescuer's position.
    //  4. The fallen player receives EventRescue and raises a Spawn event at that position (the engine's Spawn
    //     handler moves her, zeroes velocity, and onSpawned resets her state). She also gets a heart back.
    // Needs `--client host port`; --p2p has no event transport.
    EventManager &events = engine.getEvents();

    // NetEventBridge (Sai) sets the event owner id, forwards shareable Local events to
    // NetworkClient::sendEvent, and on pump() raises events received from the server as Network-origin.
    std::unique_ptr<NetEventBridge> bridge;
    if (net)
        bridge = std::make_unique<NetEventBridge>(events, *net);


    // On-screen feedback (the engine has no text rendering, so the HUD uses shapes and colors):
    //  - a bar at the top centre shrinks while a fallen friend can still be rescued (press R before it empties)
    //  - the fallen friend's Kitty turns red while she can be rescued
    //  - Kitty flashes red when she loses a life and yellow when she is rescued
    constexpr float RescueBarWidth = 320.0f;
    GameObject &rescueBar = objects.create("rescue-bar");
    rescueBar.add<Transform>(0.0f, 0.0f, RescueBarWidth, 16.0f);
    Sprite &rescueBarSprite = rescueBar.add<Sprite>(SDL_Color{255, 90, 140, 255});
    rescueBarSprite.layer = 31;
    rescueBarSprite.visible = false;
    const uint32_t rescueBarId = rescueBar.getId();

    auto fieldNumber = [](const Event &e, const char *key, double fallback) -> double
    {
        const auto *fields = std::get_if<EventFields>(&e.payload);
        if (fields == nullptr)
            return fallback;
        const auto found = fields->find(key);
        if (found == fields->end())
            return fallback;
        if (const auto *whole = std::get_if<int64_t>(&found->second))
            return static_cast<double>(*whole);
        if (const auto *real = std::get_if<double>(&found->second))
            return *real;
        return fallback;
    };

    // A friend fell (arrived over the network).
    events.subscribe(Kitty::EventFriendFell, [&](const Event &e)
                     {
        if (e.origin != EventOrigin::Network)
            return;
        const uint32_t who = static_cast<uint32_t>(fieldNumber(e, "owner", 0.0));
        if (who == 0 || who == localOwner)
            return;
        pending.add(who, static_cast<int64_t>(fieldNumber(e, "fall", 0.0)), RescueWindow);
        announce("Kitty " + std::to_string(who) + " fell! Press R to rescue her."); });

    // Local R press (an Input event) -> send a Rescue event for the first friend who is still waiting.
    events.subscribe(EventTypes::Input, [&](const Event &e)
                     {
        const auto *input = std::get_if<InputEvent>(&e.payload);
        if (input == nullptr || input->action != "rescue" || e.origin == EventOrigin::Network)
            return;
        const std::optional<Kitty::PendingRescues::Entry> target = pending.takeFirst();
        if (!target)
        {
            announce("Nobody needs rescuing right now.");
            return;
        }
        const uint32_t who = target->owner;
        const Transform *me = objects.find(playerId)->get<Transform>();

        Event rescue;
        rescue.type = Kitty::EventRescue;
        rescue.priority = EventPriority::High;
        rescue.shareable = (net != nullptr);
        rescue.payload = EventFields{{"owner", static_cast<int64_t>(who)},
                                     {"fall", target->fallId},
                                     {"by", static_cast<int64_t>(localOwner)},
                                     {"x", static_cast<double>(me->x)},
                                     {"y", static_cast<double>(me->y)}};
        events.raise(std::move(rescue));
        attempts.sent(target->fallId);
        // Sending is not winning: the fallen player accepts only the first rescue and announces the winner.
        announce("Rescue sent to Kitty " + std::to_string(who) + "..."); });

    // We were rescued (arrived over the network): spawn at the rescuer's position.
    events.subscribe(Kitty::EventRescue, [&](const Event &e)
                     {
        if (e.origin != EventOrigin::Network)
            return;
        const uint32_t rescuedOwner = static_cast<uint32_t>(fieldNumber(e, "owner", 0.0));
        const int64_t fallId = static_cast<int64_t>(fieldNumber(e, "fall", 0.0));
        if (rescuedOwner != localOwner)
        {
            // Not ours. Pending lists are cleared by the fallen player's RescueAccepted event, not by
            // seeing a rescue attempt, because an attempt can still be rejected (expired or too late).
            return;
        }
        // Accept only the first rescue that matches our open fall and arrives before it expires.
        if (!myFall.tryAccept(fallId))
        {
            SDL_Log("Ignored a rescue for fall %lld (already rescued, expired or not ours).", static_cast<long long>(fallId));
            return;
        }
        GameObject *point = GameplaySystems::findSpawnPointByName(objects, "rescue");
        GameObject *me = objects.find(playerId);
        if (point == nullptr || me == nullptr)
            return;
        Transform *pointTransform = point->get<Transform>();
        pointTransform->x = static_cast<float>(fieldNumber(e, "x", pointTransform->x));
        pointTransform->y = static_cast<float>(fieldNumber(e, "y", pointTransform->y));
        if (lives < MaxLives)
            ++lives;
        deathCooldown = std::max(deathCooldown, 1.0);
        // Tell everyone which rescue won, so the other rescuers drop this fall and learn they were too late.
        Event accepted;
        accepted.type = Kitty::EventRescueAccepted;
        accepted.priority = EventPriority::High;
        accepted.shareable = (net != nullptr);
        accepted.payload = EventFields{{"owner", static_cast<int64_t>(localOwner)},
                                       {"fall", fallId},
                                       {"by", static_cast<int64_t>(fieldNumber(e, "by", 0.0))}};
        events.raise(std::move(accepted));
        gameplay.raiseSpawn(*me, "rescue");
        flashTimer = 0.8;
        flashColor = SDL_Color{255, 240, 90, 255};
        announce("A friend rescued you! Heart restored."); });

    // Dragging/resizing the window freezes the main loop on Windows, so the next frame has a huge delta.
    // Physics then integrates gravity over that whole gap and Kitty drops through the floor ("fell out of
    // the sky"), respawns at the start and meets Kuromi. A stalled frame is detected here and her position is
    // restored, with no death raised for it.
    constexpr float StallSeconds = 0.25f;
    bool stallRecover = false;
    float goodX = 0.0f;
    float goodY = 0.0f;

    // The fallen player says whose rescue was accepted (arrived over the network).
    events.subscribe(Kitty::EventRescueAccepted, [&](const Event &e)
                     {
        if (e.origin != EventOrigin::Network)
            return;
        const uint32_t owner = static_cast<uint32_t>(fieldNumber(e, "owner", 0.0));
        const int64_t fallId = static_cast<int64_t>(fieldNumber(e, "fall", 0.0));
        const uint32_t winner = static_cast<uint32_t>(fieldNumber(e, "by", 0.0));
        pending.clear(owner, fallId); // nobody else can rescue this fall any more
        switch (attempts.resolve(fallId, winner, localOwner))
        {
        case Kitty::RescueAttempts::Result::Won:
            announce("You rescued Kitty " + std::to_string(owner) + "!");
            break;
        case Kitty::RescueAttempts::Result::TooLate:
            announce("Too late - Kitty " + std::to_string(owner) + " was already rescued by Kitty " +
                     std::to_string(winner) + ".");
            break;
        case Kitty::RescueAttempts::Result::NotMine:
            break;
        } });

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
            const bool stalled = worldDelta > StallSeconds;
            camera.setViewport(Kitty::ViewWidth, Kitty::ViewHeight);

            // Events other players sent (ordered by the server) are enqueued here, on the main thread.
            if (bridge)
                bridge->pump();

            // Window drags are not game time, so a stalled frame does not use up rescue windows.
            pending.tick(stalled ? 0.0 : static_cast<double>(worldDelta));
            myFall.tick(stalled ? 0.0 : static_cast<double>(worldDelta));

            if (Input::isKeyJustPressed(SDL_SCANCODE_R))
            {
                Event press;
                press.type = EventTypes::Input;
                press.target = EventObjects::of(*objects.find(playerId));
                press.payload = InputEvent{"rescue", 1.0f};
                events.raise(std::move(press));
            }

            bool graceEnded = false;
            if (deathCooldown > 0.0)
            {
                deathCooldown -= stalled ? 0.0 : static_cast<double>(worldDelta); // a window drag is not game time
                if (deathCooldown <= 0.0)
                {
                    deathCooldown = 0.0;
                    graceEnded = true;
                }
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

            bool skipProducers = stalled;
            if (stallRecover)
            {
                // The frame after a stall: physics has just run with the huge delta. Undo it.
                t.x = goodX;
                t.y = goodY;
                body.velocityX = 0.0f;
                body.velocityY = 0.0f;
                stallRecover = false;
                skipProducers = true;
            }
            else if (stalled)
            {
                stallRecover = true; // physics will run after this update with the huge delta
                SDL_Log("Window stall (%.2f s): keeping Hello Kitty where she was.", static_cast<double>(worldDelta));
            }
            else
            {
                goodX = t.x;
                goodY = t.y;
            }
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
                    if (net)
                    {
                        kuromiSinceSnap += worldDelta;
                        if (!kuromiSmoothInit)
                        {
                            kuromiSmoothInit = true;
                            kuromiSmoothX = kuromiTargetX = kuromiLastWritten = kuromiLastRaw = kt->x;
                        }
                        else if (kt->x != kuromiLastWritten && kt->x != kuromiLastRaw)
                        {
                            // A new snapshot arrived: estimate her speed from it and re-aim.
                            if (kuromiSinceSnap > 0.001f)
                                kuromiVelX = std::clamp((kt->x - kuromiLastRaw) / kuromiSinceSnap, -400.0f, 400.0f) * 0.5f +
                                             kuromiVelX * 0.5f;
                            kuromiLastRaw = kt->x;
                            kuromiTargetX = kt->x;
                            kuromiSinceSnap = 0.0f;
                        }
                        else
                        {
                            // no new snapshot (the replica may still hold the old raw value)
                            kuromiTargetX += kuromiVelX * worldDelta; // keep walking between snapshots
                        }
                        kuromiSmoothX += (kuromiTargetX - kuromiSmoothX) * (1.0f - std::exp(-12.0f * worldDelta));
                        kt->x = kuromiSmoothX;
                        kuromiLastWritten = kuromiSmoothX;
                    }

                    // Animate from Kuromi's OWN frame and facing, then write them to the sprite every
                    // frame. A networked replica's sprite is overwritten by net->sync each frame, so
                    // reading ks->frame back (as before) restarted the walk cycle constantly and made
                    // her feet vibrate instead of stepping.
                    const float dx = kt->x - kuromiPrevX;
                    if (dx > 0.5f)
                        kuromiFlip = false;
                    else if (dx < -0.5f)
                        kuromiFlip = true;
                    kuromiAnimTimer += worldDelta;
                    if (kuromiAnimTimer >= 0.12f)
                    {
                        kuromiAnimTimer = 0.0f;
                        kuromiFrame = (kuromiFrame + 1) % ks->frameCount;
                    }
                    ks->frame = kuromiFrame;
                    ks->flipHorizontal = kuromiFlip;
                    kuromiPrevX = kt->x;
                }
            }
            else if (net)
            {
                objects.forEach<NetworkIdentity, Kitty::Hazard>([&](GameObject &object, NetworkIdentity &, Kitty::Hazard &)
                                                               { kuromiId = object.getId(); });
            }

            // Producers: these only raise Collision / Death events. Pits, Kuromi and the world floor are
            // all handled by the rules above when the events dispatch.
            if (!skipProducers)
            {
                gameplay.produceCollisions(playerId);
                gameplay.produceOutOfBounds(playerId, Kitty::WorldHeight + 200.0f, "fell out of the sky");
            }

            // Collision events fire on entering, so if Kuromi is still touching Kitty when her grace
            // period ends, raise the hit now (a game-specific Death producer).
            if (graceEnded && !skipProducers && kuromiId != 0)
                if (GameObject *kuromiObject = objects.find(kuromiId);
                    kuromiObject && kuromiObject->active &&
                    overlaps(colliderBounds(kitty), colliderBounds(*kuromiObject)))
                    gameplay.raiseDeath(kitty, kuromiObject, "kuromi");

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

            // Rescue HUD: bar for the friend R would rescue, red tint on fallen friends, flash on Kitty.
            if (GameObject *bar = objects.find(rescueBarId))
            {
                bar->get<Sprite>()->visible = !pending.empty();
                if (!pending.empty())
                {
                    Transform *barTransform = bar->get<Transform>();
                    const float fraction = static_cast<float>(pending.firstTimeLeft() / RescueWindow);
                    barTransform->width = RescueBarWidth * std::clamp(fraction, 0.0f, 1.0f);
                    barTransform->x = camera.getX() + (Kitty::ViewWidth - RescueBarWidth) * 0.5f;
                    barTransform->y = camera.getY() + 20.0f;
                }
            }
            objects.forEach<NetworkIdentity, Sprite>([&](GameObject &, NetworkIdentity &id, Sprite &friendSprite)
                                                     {
                if (id.isReplica && id.kind == Kitty::KindKitty)
                    friendSprite.color = pending.has(id.ownerId) ? SDL_Color{255, 90, 90, 230}
                                                                             : SDL_Color{255, 200, 220, 230}; });
            if (flashTimer > 0.0)
                flashTimer -= static_cast<double>(worldDelta);
            sprite.color = flashTimer > 0.0 ? flashColor : SDL_Color{255, 255, 255, 255};

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