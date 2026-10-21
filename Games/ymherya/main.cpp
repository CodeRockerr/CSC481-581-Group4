#include "Engine.h"
#include "Collision.h"
#include "Image.h"
#include "NetworkClient.h"
#include "NetMessage.h"

#include "Components.h"
#include "GameObjectManager.h"
#include "GameplaySystems.h"
#include "MovementPath.h"
#include "SpawnPoint.h"
#include "DeathZone.h"
#include "SideScrollTrigger.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// Returns a smaller copy of an entity's box, used for collisions with
// dangerous things. The padding values are fractions of the entity's size
// removed from the left, right, top and bottom. This way transparent sprite
// edges and a foot that is only brushing something do not count as a touch.
static Entity bodyHitbox(const Entity &e, float padL, float padR, float padT, float padB)
{
    Entity box = e;
    box.x = e.x + e.width * padL;
    box.y = e.y + e.height * padT;
    box.width = e.width * (1.0f - padL - padR);
    box.height = e.height * (1.0f - padT - padB);
    return box;
}

// True when two axis-aligned boxes overlap.
static bool boxesOverlap(float ax, float ay, float aw, float ah,
                         float bx, float by, float bw, float bh)
{
    return ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by;
}

int main(int argc, char *argv[])
{
    // Networking arguments
    // Usage: ymherya_game --client 127.0.0.1 5555
    bool networked = false;
    const char *serverAddress = "127.0.0.1";
    uint16_t serverPort = 5555;

    if (argc >= 2 && std::string(argv[1]) == "--client") {
        networked = true;

        if (argc >= 3) {
            serverAddress = argv[2];
        }

        if (argc >= 4) {
            serverPort = static_cast<uint16_t>(std::stoi(argv[3]));
        }
    }

    // Engine
    Engine engine("Hello Kitty Adventure");

    EntityManager &entities = engine.getEntities();
    GameObjectManager& objects = engine.getObjects();
    Renderer &renderer = engine.getRenderer();

    const float gameWidth = 1280.0f;
    const float gameHeight = 800.0f;

    entities.setReferenceResolution(static_cast<int>(gameWidth), static_cast<int>(gameHeight));
    entities.setScaleMode(ScaleMode::Percentage);

    // Timelines
    // PlayerTime: Local Hello Kitty.
    // WorldTime: Kuromi, platforms and remote players.
    Timeline &playerTime = engine.getTimeline(Engine::PlayerTime);
    Timeline &worldTime = engine.getTimeline(Engine::WorldTime);

    // Networking
    std::unique_ptr<NetworkClient> network;

    if (networked) {
        network = std::make_unique<NetworkClient>(serverAddress, serverPort);

        if (!network->connect()) {
            std::cerr << "Could not connect to server.\n";
            return 1;
        }
    }

    // Load textures
    SDL_Surface *backgroundSurface = loadImage("Games/ymherya/assets/background.png");
    SDL_Surface *helloKittySurface = loadImage("Games/ymherya/assets/hello-kitty.png");
    SDL_Surface *kuromiSurface = loadImage("Games/ymherya/assets/kuromi.png");
    SDL_Surface* bigPlatformSurface = loadImage("Games/ymherya/assets/big_p.png");
    SDL_Surface* smallPlatformSurface = loadImage("Games/ymherya/assets/small_p.png");
    SDL_Surface* heartSurface = loadImage("Games/ymherya/assets/heart.png");
    SDL_Surface* brokenHeartSurface = loadImage("Games/ymherya/assets/no_heart.png");

    if (!backgroundSurface || !helloKittySurface || !kuromiSurface ||
        !bigPlatformSurface || !smallPlatformSurface || !heartSurface || !brokenHeartSurface) {
        throw std::runtime_error(std::string("Could not load one of the game images: ") + SDL_GetError());
    }

    SDL_Texture *backgroundTexture = SDL_CreateTextureFromSurface(renderer.getHandle(), backgroundSurface);
    SDL_Texture *helloKittyTexture = SDL_CreateTextureFromSurface(renderer.getHandle(), helloKittySurface);
    SDL_Texture *kuromiTexture = SDL_CreateTextureFromSurface(renderer.getHandle(), kuromiSurface);
    SDL_Texture* bigPlatformTexture = SDL_CreateTextureFromSurface(renderer.getHandle(), bigPlatformSurface);
    SDL_Texture* smallPlatformTexture = SDL_CreateTextureFromSurface(renderer.getHandle(), smallPlatformSurface);
    SDL_Texture* heartTexture = SDL_CreateTextureFromSurface(renderer.getHandle(), heartSurface);
    SDL_Texture* brokenHeartTexture = SDL_CreateTextureFromSurface(renderer.getHandle(), brokenHeartSurface);

    SDL_DestroySurface(backgroundSurface);
    SDL_DestroySurface(helloKittySurface);
    SDL_DestroySurface(kuromiSurface);
    SDL_DestroySurface(bigPlatformSurface);
    SDL_DestroySurface(smallPlatformSurface);
    SDL_DestroySurface(heartSurface);
    SDL_DestroySurface(brokenHeartSurface);

    if (!backgroundTexture || !helloKittyTexture || !kuromiTexture ||
        !bigPlatformTexture || !smallPlatformTexture || !heartTexture || !brokenHeartTexture) {
        throw std::runtime_error(std::string("Failed to create game textures: ") + SDL_GetError());
    }

    // Background
    const float groundY = gameHeight * 0.86f;
    Entity *background = entities.createEntity(0.0f, 0.0f, gameWidth, gameHeight);

    entities.setTexture(background, backgroundTexture, 1);

    background->affectedByGravity = false;
    background->timelineId = Engine::WorldTime;

    // Second background tile for side scrolling.
    Entity* backgroundNext = entities.createEntity(gameWidth, 0.0f, gameWidth, gameHeight);

    entities.setTexture(backgroundNext, backgroundTexture, 1);

    backgroundNext->affectedByGravity = false;
    backgroundNext->timelineId = Engine::WorldTime;

    // Hello Kitty dimensions
    const float kittyHeight = gameHeight * 0.18f;
    const float kittyWidth = kittyHeight * 0.60f;
    const float kittyGroundOffset = 50.0f;

    const float kittyStartX = gameWidth * 0.55f;
    const float kittyGroundY = groundY - kittyHeight - kittyGroundOffset;

    // Fraction of the sprite height that is empty space under the feet.
    // Raise this (for example 0.05f) if Kitty seems to float above the clouds.
    const float kittyFeetPad = 0.0f;

    // Where Kitty's feet were at the end of the previous update. The landing
    // check compares it with the current feet position to detect that she
    // crossed a cloud top, even if one very long frame moved her far down.
    float previousKittyFeet = kittyGroundY + kittyHeight * (1.0f - kittyFeetPad);

    // Local Hello Kitty
    Entity* helloKitty = entities.createEntity(kittyStartX, kittyGroundY, kittyWidth, kittyHeight);
    entities.setTexture(helloKitty, helloKittyTexture, 8);

    helloKitty->affectedByGravity = true;
    helloKitty->timelineId = Engine::PlayerTime;

    bool helloKittyGrounded = true;

    // Pink cloud platforms that Hello Kitty can jump onto.
    struct Ledge {
        Entity* entity;
    };

    std::vector<Ledge> ledges;

    // The walkable part of each cloud is read from the cloud entity every
    // frame, using fractions of its size. The landing logic therefore always
    // matches what is drawn, no matter how the cloud is moved or scaled.
    const float ledgeLeftFraction = 0.10f;
    const float ledgeRightFraction = 0.90f;
    const float ledgeSurfaceFraction = 0.30f;

    auto ledgeLeft = [&](const Ledge& ledge) {
        return ledge.entity->x + ledge.entity->width * ledgeLeftFraction;
    };

    auto ledgeRight = [&](const Ledge& ledge) {
        return ledge.entity->x + ledge.entity->width * ledgeRightFraction;
    };

    auto ledgeSurface = [&](const Ledge& ledge) {
        return ledge.entity->y + ledge.entity->height * ledgeSurfaceFraction;
    };

    auto createLedge = [&](float x, float y, float width, float height, SDL_Texture* texture) {
        Entity* platform = entities.createEntity(x, y, width, height);
        entities.setTexture(platform, texture, 1);

        platform->affectedByGravity = false;
        platform->timelineId = Engine::WorldTime;

        ledges.push_back({platform});
    };

    // Initial positions for the 1280 x 800 game window.
    createLedge(420.0f, 440.0f, 175.0f, 75.0f, smallPlatformTexture);
    createLedge(690.0f, 340.0f, 250.0f, 100.0f, bigPlatformTexture);
    createLedge(1010.0f, 475.0f, 175.0f, 75.0f, smallPlatformTexture);
    createLedge(200.0f, 300.0f, 175.0f, 75.0f, smallPlatformTexture);

    // Moving clouds. Each one follows a MovementPath; the game loop applies
    // the path's per-frame movement to the cloud's entity, so scrolling the
    // level never fights with the path.
    struct MovingCloud {
        Entity* entity;
        Transform* transform;
        float previousX;
    };

    std::vector<MovingCloud> movingClouds;

    // Turns the most recently created ledge into a moving cloud.
    auto makeMovingCloud = [&](const char* name, MovementPathType type, float speed, float distance) {
        Entity* cloud = ledges.back().entity;

        GameObject& path = objects.create(name);
        Transform& t = path.add<Transform>();

        t.x = cloud->x;
        t.y = cloud->y;
        t.width = cloud->width;
        t.height = cloud->height;

        path.add<MovementPath>(type, speed, distance);
        movingClouds.push_back({cloud, &t, t.x});
    };

    // Cloud 4 (index 3): your original vertical moving cloud.
    makeMovingCloud("MovingCloudPath", MovementPathType::Vertical, 0.7f, 100.0f);

    // ---- Level extension (world coordinates at scroll 0) ----
    createLedge(1290.0f, 470.0f, 175.0f, 75.0f, smallPlatformTexture);   // 4 static
    createLedge(1560.0f, 400.0f, 210.0f, 85.0f, smallPlatformTexture);   // 5 static (new size)
    createLedge(1950.0f, 330.0f, 175.0f, 75.0f, smallPlatformTexture);   // 6 moving, horizontal
    makeMovingCloud("MovingCloudHorizontal", MovementPathType::Horizontal, 0.8f, 100.0f);
    createLedge(2290.0f, 360.0f, 250.0f, 100.0f, bigPlatformTexture);    // 7 static
    createLedge(2650.0f, 330.0f, 175.0f, 75.0f, smallPlatformTexture);   // 8 moving, circular
    makeMovingCloud("MovingCloudCircular", MovementPathType::Circular, 0.9f, 70.0f);
    createLedge(2980.0f, 420.0f, 190.0f, 80.0f, smallPlatformTexture);   // 9 static
    createLedge(3280.0f, 350.0f, 250.0f, 100.0f, bigPlatformTexture);    // 10 static
    createLedge(3600.0f, 440.0f, 210.0f, 85.0f, smallPlatformTexture);   // 11 static

    // Story line printed when Kitty lands on each cloud (same order as above).
    const char* ledgeStories[12] = {
        "Hello Kitty reached a small cloud!",
        "Hello Kitty climbed onto a high pink cloud!",
        "Hello Kitty landed on another small cloud!",
        "Hello Kitty jumped onto the moving cloud!",
        "Hello Kitty has left the first screen behind!",
        "Hello Kitty hopped onto a wider cloud!",
        "Hello Kitty is riding a cloud that drifts sideways!",
        "Hello Kitty reached a big cloud above the pit!",
        "Hello Kitty is riding a cloud that goes in circles!",
        "Hello Kitty landed on a cloud past the pit!",
        "Hello Kitty climbed onto another big cloud!",
        "Hello Kitty reached the last cloud!"
    };
    int lastAnnouncedLedge = -1;

    // Kuromi dimensions
    const float kuromiHeight = gameHeight * 0.25f;
    const float kuromiWidth = kuromiHeight * 0.45f;

    // Kuromi patrols the whole width of the window, from the left edge to
    // the right edge, and starts at the left edge (far from Kitty's spawn).
    const float kuromiPatrolMinX = 0.0f;
    const float kuromiPatrolMaxX = gameWidth - kuromiWidth;
    const float kuromiPatrolRange = kuromiPatrolMaxX - kuromiPatrolMinX;
    const float kuromiY = groundY - kuromiHeight;

    // Hitbox padding (left, right, top, bottom as fractions of the sprite).
    // Kuromi's head is within a few pixels of the feet of Kitty when Kitty
    // stands on the right cloud, so the full sprite boxes overlap even though
    // they visually do not touch. The smaller hitboxes fix that.
    const float kittyHitPadSide = 0.20f;
    const float kittyHitPadTop = 0.10f;
    const float kittyHitPadBottom = 0.10f;

    const float kuromiHitPadSide = 0.20f;
    const float kuromiHitPadTop = 0.15f;
    const float kuromiHitPadBottom = 0.05f;

    // Speed of the offline movement path. The path now covers the full
    // width, so this is lower than before to keep Kuromi beatable.
    const float kuromiPathSpeed = 0.20f;

    // Kuromi
    // The server controls her position through PlatformState.
    Entity *kuromi = entities.createEntity(kuromiPatrolMinX, kuromiY, kuromiWidth, kuromiHeight);

    entities.setTexture(kuromi, kuromiTexture, 8);

    kuromi->affectedByGravity = false;
    kuromi->timelineId = Engine::WorldTime;

    // Remote Hello Kitties
    std::unordered_map<uint32_t, Entity *> remoteKitties;
    std::unordered_map<uint32_t, float> remotePreviousX;
    std::unordered_map<uint32_t, double> remoteAnimationTime;
    std::unordered_map<uint32_t, int64_t> lastTic;

    // Remote player creation queue
    std::mutex spawnMutex;
    std::vector<NetMessage> pendingSpawns;

    // Animation
    double helloKittyAnimationTime = 0.0;
    double kuromiAnimationTime = 0.0;
    const double animationFrameTime = 0.10;

    // Player constants
    const float kittySpeed = 350.0f;
    const float jumpVelocity = -700.0f;

    // Existing server PlatformState coordinates
    // Server: x = 520 + 180 * sin(seconds)
    // Range: 340 -> 700
    // We map this onto the full width of the window.
    const float serverPlatformMinX = 520.0f - 180.0f;
    const float serverPlatformMaxX = 520.0f + 180.0f;

    // Camera offset used to keep legacy Entity positions on screen.
    float worldScrollOffset = 0.0f;

    const float scrollAmount = gameWidth * 0.5f;
    bool legacyScrollApplied = false;

    // Level size and scroll limits.
    const float levelWidth = 4000.0f;
    const float maxScroll = levelWidth - gameWidth;
    const float leftScrollThreshold = gameWidth * 0.25f;
    const float rightScrollThreshold = gameWidth * 0.65f;

    // Hidden spawn point for Hello Kitty.
    GameObject& kittySpawn = objects.create("KittyStartSpawn");

    Transform& spawnTransform = kittySpawn.add<Transform>();
    spawnTransform.x = kittyStartX;
    spawnTransform.y = kittyGroundY;
    spawnTransform.width = kittyWidth;
    spawnTransform.height = kittyHeight;

    kittySpawn.add<SpawnPoint>("start");

    // Additional hidden spawn points/checkpoints for the later parts of the level.
    GameObject& middleSpawn = objects.create("KittyMiddleSpawn");
    Transform& middleSpawnTransform = middleSpawn.add<Transform>();
    middleSpawnTransform.x = 1480.0f;
    middleSpawnTransform.y = kittyGroundY;
    middleSpawnTransform.width = kittyWidth;
    middleSpawnTransform.height = kittyHeight;
    middleSpawn.add<SpawnPoint>("middle");

    GameObject& upperSpawn = objects.create("KittyUpperSpawn");
    Transform& upperSpawnTransform = upperSpawn.add<Transform>();
    upperSpawnTransform.x = 2350.0f;                                          // on cloud 7
    upperSpawnTransform.y = ledgeSurface(ledges[7]) - kittyHeight;
    upperSpawnTransform.width = kittyWidth;
    upperSpawnTransform.height = kittyHeight;
    upperSpawn.add<SpawnPoint>("upper");

    // Hidden pit death zones. Below the pits the floor does not exist, so
    // Kitty falls into the zone and is sent to the spawn point named here.
    struct PitZone {
        Transform* transform;
        float worldStart;
        float worldEnd;
        const char* spawnName;
    };

    std::vector<PitZone> pitZones;

    auto createPit = [&](const char* name, float worldStart, float worldEnd, const char* spawnName) {
        GameObject& pit = objects.create(name);
        Transform& t = pit.add<Transform>();

        t.x = worldStart - worldScrollOffset;
        t.y = gameHeight - 80.0f;
        t.width = worldEnd - worldStart;
        t.height = 300.0f;

        pit.add<Collider>(0.0f, 0.0f, t.width, t.height, true);
        pit.add<DeathZone>(spawnName);

        pitZones.push_back({&t, worldStart, worldEnd, spawnName});
    };

    createPit("PitZoneOne", 1900.0f, 2620.0f, "middle");
    createPit("PitZoneTwo", 2900.0f, 3560.0f, "upper");

    // Invisible proxy that follows the existing Hello Kitty Entity.
    GameObject& kittyProxy = objects.create("KittyGameplayProxy");

    Transform& kittyProxyTransform = kittyProxy.add<Transform>();
    kittyProxyTransform.x = kittyStartX;
    kittyProxyTransform.y = kittyGroundY;
    kittyProxyTransform.width = kittyWidth;
    kittyProxyTransform.height = kittyHeight;

    kittyProxy.add<Collider>(0.0f, 0.0f, kittyWidth, kittyHeight, true);

    // Invisible death zone that follows Kuromi.
    GameObject& kuromiDeathObject = objects.create("KuromiDeathZone");

    Transform& kuromiDeathTransform = kuromiDeathObject.add<Transform>();
    kuromiDeathTransform.x = kuromi->x;
    kuromiDeathTransform.y = kuromi->y;
    kuromiDeathTransform.width = kuromiWidth;
    kuromiDeathTransform.height = kuromiHeight;

    kuromiDeathObject.add<Collider>(0.0f, 0.0f, kuromiWidth, kuromiHeight, true);
    kuromiDeathObject.add<DeathZone>("start");

    // Movement path for offline Kuromi movement.
    // When networking is enabled, the server remains authoritative.
    //
    // The path swings around its starting x by +/- the amplitude. It is
    // centered on the LEFT edge with an amplitude equal to the full patrol
    // range, so the raw offset goes from -range to +range. The update loop
    // folds that offset with abs(), which turns it into a bounce between the
    // left edge and the right edge that begins at the left edge.
    GameObject& kuromiPathObject = objects.create("KuromiMovementPath");

    Transform& kuromiPathTransform = kuromiPathObject.add<Transform>();
    kuromiPathTransform.x = kuromiPatrolMinX;
    kuromiPathTransform.y = kuromiY;
    kuromiPathTransform.width = kuromiWidth;
    kuromiPathTransform.height = kuromiHeight;

    kuromiPathObject.add<MovementPath>(MovementPathType::Horizontal, kuromiPathSpeed, kuromiPatrolRange);

    // Invisible side-scroll trigger near the right side of the screen.
    GameObject& scrollTriggerObject = objects.create("SideScrollTriggerObject");
    Transform& scrollTriggerTransform = scrollTriggerObject.add<Transform>();

    scrollTriggerTransform.x = gameWidth * 0.65f;
    scrollTriggerTransform.y = 0.0f;
    scrollTriggerTransform.width = 24.0f;
    scrollTriggerTransform.height = gameHeight;

    scrollTriggerObject.add<Collider>(0.0f, 0.0f, 24.0f, gameHeight, true);
    scrollTriggerObject.add<SideScrollTrigger>(scrollAmount, 0.0f);

    // Invisible side-scroll boundary on the left side of the screen. When
    // Kitty overlaps it while the level is scrolled, the level scrolls back.
    GameObject& leftScrollObject = objects.create("SideScrollLeftObject");
    Transform& leftScrollTransform = leftScrollObject.add<Transform>();

    leftScrollTransform.x = 0.0f;
    leftScrollTransform.y = 0.0f;
    leftScrollTransform.width = leftScrollThreshold;
    leftScrollTransform.height = gameHeight;

    leftScrollObject.add<Collider>(0.0f, 0.0f, leftScrollThreshold, gameHeight, true);

    // Invisible side-scroll boundary covering the right part of the screen.
    // The scrolling no longer depends on the SideScrollTrigger component's
    // "activated" flag: when Kitty's box overlaps this object, the whole
    // level is translated to the left.
    GameObject& rightScrollObject = objects.create("SideScrollRightObject");
    Transform& rightScrollTransform = rightScrollObject.add<Transform>();

    rightScrollTransform.x = rightScrollThreshold;
    rightScrollTransform.y = 0.0f;
    rightScrollTransform.width = gameWidth - rightScrollThreshold;
    rightScrollTransform.height = gameHeight;

    rightScrollObject.add<Collider>(0.0f, 0.0f, rightScrollTransform.width, gameHeight, true);

    // Scrolls the whole level. delta > 0 scrolls right (scenery moves left),
    // delta < 0 scrolls back to the left. The two scroll boundaries stay
    // fixed on the screen, so they are not moved here.
    auto shiftWorld = [&](float delta) {
        if (delta == 0.0f) {
            return;
        }

        // Two background tiles that wrap around, so the background never runs out.
        background->x -= delta;
        backgroundNext->x -= delta;

        for (Entity* tile : {background, backgroundNext}) {
            while (tile->x + gameWidth <= 0.0f) {
                tile->x += 2.0f * gameWidth;
            }
            while (tile->x >= gameWidth) {
                tile->x -= 2.0f * gameWidth;
            }
        }

        for (Ledge& ledge : ledges) {
            ledge.entity->x -= delta;
        }

        // Kuromi is repositioned every frame inside the visible window.
        kuromi->x -= delta;

        for (auto& entry : remoteKitties) {
            entry.second->x -= delta;
        }

        spawnTransform.x -= delta;
        middleSpawnTransform.x -= delta;
        upperSpawnTransform.x -= delta;
        kuromiDeathTransform.x -= delta;

        for (PitZone& pit : pitZones) {
            pit.transform->x -= delta;
        }

        worldScrollOffset += delta;
    };

    // Lives, HUD and story output
    // These are created last so the hearts draw on top of everything else.
    int helloKittyDeaths = 0;
    int helloKittyLives = 3;
    const int maxLives = 3;

    // Seconds of protection after a respawn so one collision cannot
    // cost more than one life. The game also starts with this protection,
    // so Kitty can never be caught in the first moments of a session.
    const double deathCooldownDuration = 1.0;
    const double startGraceDuration = 2.0;
    double deathCooldown = startGraceDuration;

    // Three heart icons fixed at the top-left of the screen.
    std::vector<Entity*> lifeIcons;

    const float heartSize = 38.0f;
    const float heartGap = 10.0f;

    for (int i = 0; i < maxLives; ++i) {
        Entity* heart = entities.createEntity(24.0f + i * (heartSize + heartGap), 20.0f, heartSize, heartSize);

        entities.setTexture(heart, heartTexture, 1);
        heart->affectedByGravity = false;
        heart->timelineId = Engine::WorldTime;

        lifeIcons.push_back(heart);
    }

    auto updateLivesHUD = [&]() {
        for (int i = 0; i < maxLives; ++i) {
            entities.setTexture(lifeIcons[i], i < helloKittyLives ? heartTexture : brokenHeartTexture, 1);
        }
    };

    // Prints a story line to the console and shows it in the window title
    // together with the current number of lives.
    std::string story = "Jump across the pink clouds. Kuromi guards the ground.";

    auto announce = [&](const std::string& text) {
        story = text;
        SDL_Log("%s", story.c_str());

        char title[400];
        std::snprintf(title, sizeof(title), "Hello Kitty Adventure | lives %d/%d | %s",
                      helloKittyLives, maxLives, story.c_str());
        SDL_SetWindowTitle(engine.getWindow().getHandle(), title);
    };

    // Moves Hello Kitty to a position and clears her motion.
    auto respawnKitty = [&](float x, float y) {
        helloKitty->x = x;
        helloKitty->y = y;
        helloKitty->velocityX = 0.0f;
        helloKitty->velocityY = 0.0f;
        helloKitty->spriteFrame = 0;
        helloKittyAnimationTime = 0.0;
        helloKittyGrounded = false; // the landing check below re-grounds her next frame
        lastAnnouncedLedge = -1;

        // Restart the swept landing check from the new position.
        previousKittyFeet = y + helloKitty->height * (1.0f - kittyFeetPad);

        kittyProxyTransform.x = x;
        kittyProxyTransform.y = y;
    };

    // The single place where a life is lost.
    // When the last life is lost, the hearts refill and Kitty restarts
    // at the start spawn point.
    auto loseLife = [&](const std::string& reason, float respawnX, float respawnY) {
        if (deathCooldown > 0.0) {
            return;
        }

        ++helloKittyDeaths;
        --helloKittyLives;

        std::string message;

        if (helloKittyLives <= 0) {
            helloKittyLives = maxLives;
            respawnX = spawnTransform.x;
            respawnY = spawnTransform.y;
            message = reason + " Out of lives after " + std::to_string(helloKittyDeaths) +
                      " deaths. Hearts refilled, starting over.";
        } else {
            message = reason + " Lives left: " + std::to_string(helloKittyLives) +
                      "/" + std::to_string(maxLives) + ".";
        }

        updateLivesHUD();

        // respawnX is a screen position at the current scroll. Convert it to
        // a world position, scroll the level so that spot is on screen, then
        // place Kitty there.
        const float respawnWorldX = respawnX + worldScrollOffset;
        const float targetScroll = std::max(0.0f, std::min(maxScroll, respawnWorldX - kittyStartX));

        shiftWorld(targetScroll - worldScrollOffset);
        respawnKitty(respawnWorldX - worldScrollOffset, respawnY);
        deathCooldown = deathCooldownDuration;
        announce(message);
    };

    updateLivesHUD();
    announce(story);

    if (network) {
        SDL_Log("Connected to server %s:%d as client %u.",
                serverAddress, static_cast<int>(serverPort), network->getClientId());
    }

    // Main game loop
    engine.run(
        // GAME UPDATE
        [&](float deltaTime)
        {
            // Dragging or resizing the window can stall the loop and produce
            // one very long frame. Our own movement uses a clamped step so a
            // single long frame cannot throw Kitty or Kuromi across the level.
            const float stepDelta = std::min(deltaTime, 0.05f);

            // PlayerTime controls local Kitty, WorldTime controls the world.
            const float playerDeltaTime = playerTime.isPaused() ? 0.0f : stepDelta * static_cast<float>(playerTime.getScale());

            const float worldDeltaTime = worldTime.isPaused() ? 0.0f : stepDelta * static_cast<float>(worldTime.getScale());

            // Width of the visible play area, in the same units as the
            // entities. In Percentage mode the world is scaled with the
            // window, so this stays equal to gameWidth when the window is
            // resized. It is recomputed every frame so Kuromi and Kitty
            // always stay inside the visible area.
            const int currentWidth = engine.getWindow().getWidth();
            float visibleWidth = gameWidth;

            if (currentWidth > 0) {
                const float scaleX =
                    (entities.getScaleMode() == ScaleMode::Percentage)
                        ? static_cast<float>(currentWidth) / gameWidth
                        : 1.0f;

                visibleWidth = static_cast<float>(currentWidth) / scaleX;
            }

            const float kuromiRightLimit = std::max(0.0f, visibleWidth - kuromiWidth);

            // Update reusable patterned movement for platforms and offline Kuromi.
            GameplaySystems::updateMovementPaths(objects, worldDeltaTime);

            // Apply each moving cloud's path movement and carry Kitty if she stands on it.
            for (MovingCloud& cloud : movingClouds) {
                const float cloudDeltaX = cloud.transform->x - cloud.previousX;

                cloud.entity->x += cloudDeltaX;
                cloud.entity->y = cloud.transform->y;

                const float kittyBottom = helloKitty->y + helloKitty->height;
                const float cloudTop = cloud.entity->y + cloud.entity->height * 0.30f;

                const bool kittyOnCloud =
                    kittyBottom >= cloudTop - 8.0f &&
                    kittyBottom <= cloudTop + 8.0f &&
                    helloKitty->x + helloKitty->width > cloud.entity->x &&
                    helloKitty->x < cloud.entity->x + cloud.entity->width;

                if (kittyOnCloud) {
                    helloKitty->x += cloudDeltaX;
                    kittyProxyTransform.x += cloudDeltaX;
                }

                cloud.previousX = cloud.transform->x;
            }

            // Death cooldown
            if (deathCooldown > 0.0) {
                deathCooldown -= static_cast<double>(deltaTime);

                if (deathCooldown <= 0.0) {
                    deathCooldown = 0.0;
                }
            }

            // Create remote players queued by the network thread.
            {
                std::lock_guard<std::mutex> lock(spawnMutex);

                for (const NetMessage &message : pendingSpawns) {
                    if (remoteKitties.find(message.clientId) != remoteKitties.end()) {
                        continue;
                    }

                    Entity *remote = entities.createEntity(message.x - worldScrollOffset, message.y, kittyWidth, kittyHeight);
                    entities.setTexture(remote, helloKittyTexture, 8);

                    remote->affectedByGravity = false;
                    remote->timelineId = Engine::WorldTime;

                    remote->velocityX = 0.0f;
                    remote->velocityY = 0.0f;
                    remote->active = true;

                    remoteKitties[message.clientId] = remote;
                    remotePreviousX[message.clientId] = message.x;
                    remoteAnimationTime[message.clientId] = 0.0;
                    lastTic[message.clientId] = message.tic;

                    SDL_Log("Another Hello Kitty (client %u) joined the adventure.", message.clientId);
                }

                pendingSpawns.clear();
            }

            // Process server messages.
            if (network) {
                const std::vector<NetMessage> messages = network->getLatestMessages();

                std::unordered_set<uint32_t> activeClients;

                for (const NetMessage &message : messages) {
                    // SERVER-CONTROLLED KUROMI
                    if (message.type == NetMessageType::PlatformState) {
                        float normalizedX = (message.x - serverPlatformMinX) / (serverPlatformMaxX - serverPlatformMinX);

                        if (normalizedX < 0.0f) {
                            normalizedX = 0.0f;
                        }

                        if (normalizedX > 1.0f) {
                            normalizedX = 1.0f;
                        }

                        // Map the server's 0..1 position onto the full visible width.
                        const float mappedX = normalizedX * kuromiRightLimit;

                        {
                            std::lock_guard<std::mutex> lock(kuromi->stateMutex.get());

                            // mappedX is a world position; subtract the scroll so
                            // Kuromi stays in the world instead of on the screen.
                            kuromi->x = mappedX - worldScrollOffset;
                            kuromi->y = kuromiY;

                            // Position is controlled by the server.
                            kuromi->velocityX = 0.0f;
                            kuromi->velocityY = 0.0f;
                            kuromi->active = true;
                        }

                        // Server velocity determines direction.
                        if (message.velocityX > 0.0f) {
                            kuromi->flipHorizontal = false;
                        } else if (message.velocityX < 0.0f) {
                            kuromi->flipHorizontal = true;
                        }

                        // Server movement determines whether Kuromi is animated.
                        if (std::fabs(message.velocityX) > 0.01f) {
                            kuromiAnimationTime += deltaTime;

                            if (kuromiAnimationTime >= animationFrameTime) {
                                kuromiAnimationTime = 0.0;
                                kuromi->spriteFrame = (kuromi->spriteFrame + 1) % 8;
                            }
                        } else {
                            kuromi->spriteFrame = 0;
                            kuromiAnimationTime = 0.0;
                        }

                        continue;
                    }

                    // REMOTE HELLO KITTY
                    if (message.type != NetMessageType::PlayerState) {
                        continue;
                    }

                    if (message.clientId == network->getClientId()) {
                        continue;
                    }

                    activeClients.insert(message.clientId);

                    // Ignore stale messages.
                    auto ticIt = lastTic.find(message.clientId);

                    if (ticIt != lastTic.end() && message.tic < ticIt->second) {
                        continue;
                    }

                    lastTic[message.clientId] = message.tic;

                    // Existing remote Kitty.
                    auto remoteIt = remoteKitties.find(message.clientId);

                    if (remoteIt != remoteKitties.end()) {
                        Entity *remote = remoteIt->second;

                        const float previousX = remotePreviousX[message.clientId];
                        const float currentX = message.x;
                        const float movement = currentX - previousX;

                        // Determine direction.
                        if (movement > 0.01f) {
                            remote->flipHorizontal = false;
                        } else if (movement < -0.01f) {
                            remote->flipHorizontal = true;
                        }

                        // Animate if actually moving.
                        const bool remoteWalking = std::fabs(movement) > 0.01f;

                        if (remoteWalking) {
                            remoteAnimationTime[message.clientId] += deltaTime;

                            if (remoteAnimationTime[message.clientId] >= animationFrameTime) {
                                remoteAnimationTime[message.clientId] = 0.0;
                                remote->spriteFrame = (remote->spriteFrame + 1) % 8;
                            }
                        } else {
                            remote->spriteFrame = 0;
                            remoteAnimationTime[message.clientId] = 0.0;
                        }

                        // Apply server position.
                        {
                            std::lock_guard<std::mutex> lock(remote->stateMutex.get());

                            remote->x = message.x - worldScrollOffset;
                            remote->y = message.y;

                            remote->velocityX = 0.0f;
                            remote->velocityY = 0.0f;
                            remote->active = true;
                        }

                        remotePreviousX[message.clientId] = currentX;
                        continue;
                    }

                    // New remote Kitty.
                    {
                        std::lock_guard<std::mutex> lock(spawnMutex);

                        bool alreadyQueued = false;

                        for (const NetMessage &queued : pendingSpawns) {
                            if (queued.clientId == message.clientId) {
                                alreadyQueued = true;
                                break;
                            }
                        }

                        if (!alreadyQueued) {
                            pendingSpawns.push_back(message);
                        }
                    }
                }

                // Hide remote players that are no longer in the server snapshot.
                // (Only players missing from activeClients are hidden.)
                for (auto &entry : remoteKitties) {
                    Entity *remote = entry.second;
                    const bool stillConnected = activeClients.find(entry.first) != activeClients.end();

                    std::lock_guard<std::mutex> lock(remote->stateMutex.get());

                    if (remote->active && !stillConnected) {
                        SDL_Log("Hello Kitty (client %u) left the adventure.", entry.first);
                    }

                    remote->active = stillConnected;
                }
            }

            // Offline Kuromi movement.
            // The shared MovementPath system drives her. Its raw offset from
            // the left edge swings between -range and +range; folding it with
            // abs() makes her bounce between the left and right edges,
            // starting at the left edge. The 0..1 result is then scaled to
            // the current visible width, so she never leaves the window.
            if (!network) {
                // Kuromi patrols in WORLD space at a constant speed. Her screen
                // position is her world position minus the current scroll, so
                // she scrolls with the level instead of following Kitty.
                const float previousWorldX = kuromi->x + worldScrollOffset;

                float folded = std::fabs(kuromiPathTransform.x - kuromiPatrolMinX) / kuromiPatrolRange;

                if (folded < 0.0f) {
                    folded = 0.0f;
                }

                if (folded > 1.0f) {
                    folded = 1.0f;
                }

                const float newWorldX = folded * kuromiRightLimit;

                // Update Kuromi's position.
                {
                    std::lock_guard<std::mutex> lock(kuromi->stateMutex.get());

                    kuromi->x = newWorldX - worldScrollOffset;
                    kuromi->y = kuromiPathTransform.y;
                    kuromi->velocityX = 0.0f;
                    kuromi->velocityY = 0.0f;
                }

                // Face the direction Kuromi is moving.
                const float movement = newWorldX - previousWorldX;

                if (movement > 0.01f) {
                    kuromi->flipHorizontal = false;
                } else if (movement < -0.01f) {
                    kuromi->flipHorizontal = true;
                }

                // Animate Kuromi's legs while she moves.
                if (std::fabs(movement) > 0.01f && worldDeltaTime > 0.0f) {
                    kuromiAnimationTime += worldDeltaTime;

                    if (kuromiAnimationTime >= animationFrameTime) {
                        kuromiAnimationTime = 0.0;
                        kuromi->spriteFrame = (kuromi->spriteFrame + 1) % 8;
                    }
                } else {
                    kuromi->spriteFrame = 0;
                    kuromiAnimationTime = 0.0;
                }
            }

            // Local Hello Kitty
            if (!playerTime.isPaused()) {
                const bool kittyWalking = Input::isKeyPressed(SDL_SCANCODE_A) || Input::isKeyPressed(SDL_SCANCODE_LEFT) || Input::isKeyPressed(SDL_SCANCODE_D) || Input::isKeyPressed(SDL_SCANCODE_RIGHT);

                // Move left.
                if (Input::isKeyPressed(SDL_SCANCODE_A) || Input::isKeyPressed(SDL_SCANCODE_LEFT)) {
                    helloKitty->x -= kittySpeed * playerDeltaTime;
                    helloKitty->flipHorizontal = true;
                }

                // Move right.
                if (Input::isKeyPressed(SDL_SCANCODE_D) || Input::isKeyPressed(SDL_SCANCODE_RIGHT)) {
                    helloKitty->x += kittySpeed * playerDeltaTime;
                    helloKitty->flipHorizontal = false;
                }

                // Jump (only while standing on the floor or a cloud).
                if ((Input::isKeyJustPressed(SDL_SCANCODE_SPACE) || Input::isKeyJustPressed(SDL_SCANCODE_W) || Input::isKeyJustPressed(SDL_SCANCODE_UP)) && helloKittyGrounded) {
                    helloKitty->velocityY = jumpVelocity;
                    helloKittyGrounded = false;
                }

                // Walking animation.
                if (kittyWalking) {
                    helloKittyAnimationTime += playerDeltaTime;

                    if (helloKittyAnimationTime >= animationFrameTime) {
                        helloKittyAnimationTime = 0.0;
                        helloKitty->spriteFrame = (helloKitty->spriteFrame + 1) % 8;
                    }
                } else {
                    helloKitty->spriteFrame = 0;
                    helloKittyAnimationTime = 0.0;
                }
            } else {
                helloKitty->spriteFrame = 0;
                helloKittyAnimationTime = 0.0;
            }

            // Screen boundaries
            if (helloKitty->x < 0.0f) {
                helloKitty->x = 0.0f;
            }

            const float scrollThreshold = rightScrollThreshold;
            if (helloKitty->x + helloKitty->width > visibleWidth) {
                helloKitty->x = visibleWidth - helloKitty->width;
            }

            // Cloud-platform and floor collision.
            const float kittyFeet = helloKitty->y + helloKitty->height * (1.0f - kittyFeetPad);
            const float kittyMidX = helloKitty->x + helloKitty->width * 0.5f;

            int landedLedge = -1;
            float topmostSurface = 1.0e9f;

            // Only land on a cloud while falling. Kitty lands if her feet are
            // close to the cloud's walkable surface, OR if she crossed the
            // surface since the last update. The crossing test is what keeps
            // her on the cloud after a long frame (for example while the
            // window is being resized), where gravity would otherwise move
            // her straight through the cloud in a single step.
            if (helloKitty->velocityY >= 0.0f) {
                for (int i = 0; i < static_cast<int>(ledges.size()); ++i) {
                    const Ledge& ledge = ledges[i];
                    const float surface = ledgeSurface(ledge);
                    const bool overLedge = kittyMidX >= ledgeLeft(ledge) && kittyMidX <= ledgeRight(ledge);
                    const float feetGap = kittyFeet - surface;

                    const bool nearTop = feetGap >= -5.0f && feetGap <= 15.0f;
                    const bool crossedTop = previousKittyFeet <= surface + 5.0f && kittyFeet >= surface;

                    // Prefer the highest cloud (smallest y) she passed through.
                    if (overLedge && (nearTop || crossedTop) && surface < topmostSurface) {
                        topmostSurface = surface;
                        landedLedge = i;
                    }
                }
            }

            // The floor does not exist over the pits.
            bool overPit = false;
            const float kittyMidWorldX = kittyMidX + worldScrollOffset;

            for (const PitZone& pit : pitZones) {
                if (kittyMidWorldX >= pit.worldStart && kittyMidWorldX <= pit.worldEnd) {
                    overPit = true;
                    break;
                }
            }

            bool onFloor = false;

            if (landedLedge >= 0) {
                helloKitty->y = ledgeSurface(ledges[landedLedge]) - helloKitty->height * (1.0f - kittyFeetPad);
                helloKitty->velocityY = 0.0f;
            } else if (!overPit && helloKitty->y >= kittyGroundY && helloKitty->velocityY >= 0.0f) {
                helloKitty->y = kittyGroundY;
                helloKitty->velocityY = 0.0f;
                onFloor = true;
            }

            // Remember where the feet ended up for the next update's crossing test.
            previousKittyFeet = helloKitty->y + helloKitty->height * (1.0f - kittyFeetPad);

            // Kitty can jump only when actually standing on a surface.
            helloKittyGrounded = (landedLedge >= 0) || onFloor;

            // Announce a cloud only when Kitty lands on a different one.
            if (landedLedge >= 0 && landedLedge != lastAnnouncedLedge) {
                lastAnnouncedLedge = landedLedge;
                announce(ledgeStories[landedLedge]);
            } else if (onFloor) {
                lastAnnouncedLedge = -1;
            }

            // Kuromi collision.
            // loseLife ignores hits during the respawn cooldown, so one touch
            // never costs more than one life.
            // The check uses the smaller hitboxes, so standing on a cloud while
            // Kuromi walks underneath no longer counts as a touch.
            const Entity kittyHit = bodyHitbox(*helloKitty, kittyHitPadSide, kittyHitPadSide,
                                               kittyHitPadTop, kittyHitPadBottom);
            const Entity kuromiHit = bodyHitbox(*kuromi, kuromiHitPadSide, kuromiHitPadSide,
                                                kuromiHitPadTop, kuromiHitPadBottom);
            const bool touchingKuromi = Collision::checkCollision(kittyHit, kuromiHit);

            if (deathCooldown <= 0.0 && touchingKuromi) {
                loseLife("Oh no! Kuromi caught Hello Kitty!", spawnTransform.x, spawnTransform.y);
            }

            // Sync the gameplay proxies with the legacy Entities.
            kittyProxyTransform.x = helloKitty->x;
            kittyProxyTransform.y = helloKitty->y;

            kuromiDeathTransform.x = kuromi->x;
            kuromiDeathTransform.y = kuromi->y;

            // Death-zone collision and respawn.
            // The shared gameplay system moves the proxy to the named spawn
            // point. If the proxy moved, a death zone was hit, so we pay one
            // life and copy that position back to the legacy Hello Kitty.
            // The death zones here are full-size boxes, so they are only
            // checked when the smaller hitboxes really touch Kuromi. Otherwise
            // the death zone would still fire for the brushing overlap above.
            if (deathCooldown <= 0.0 && touchingKuromi) {
                const float previousX = kittyProxyTransform.x;
                const float previousY = kittyProxyTransform.y;

                GameplaySystems::updateDeathZones(objects, kittyProxy.getId());

                const bool wasRespawned = std::fabs(kittyProxyTransform.x - previousX) > 0.01f ||
                                          std::fabs(kittyProxyTransform.y - previousY) > 0.01f;

                if (wasRespawned) {
                    loseLife("Hello Kitty touched a death zone and was sent back to a spawn point.",
                             kittyProxyTransform.x, kittyProxyTransform.y);
                }
            }

            // Pit death zones. When Kitty's box touches one, the shared death-zone
            // system teleports her proxy to the zone's named spawn point.
            if (deathCooldown <= 0.0) {
                for (const PitZone& pit : pitZones) {
                    if (!boxesOverlap(helloKitty->x, helloKitty->y, helloKitty->width, helloKitty->height,
                                      pit.transform->x, pit.transform->y,
                                      pit.transform->width, pit.transform->height)) {
                        continue;
                    }

                    kittyProxyTransform.x = helloKitty->x;
                    kittyProxyTransform.y = helloKitty->y;

                    const float beforeX = kittyProxyTransform.x;
                    const float beforeY = kittyProxyTransform.y;

                    GameplaySystems::updateDeathZones(objects, kittyProxy.getId());

                    float spawnX = spawnTransform.x;
                    float spawnY = spawnTransform.y;

                    if (std::fabs(kittyProxyTransform.x - beforeX) > 0.01f ||
                        std::fabs(kittyProxyTransform.y - beforeY) > 0.01f) {
                        spawnX = kittyProxyTransform.x;
                        spawnY = kittyProxyTransform.y;
                    } else {
                        // Fallback: look the spawn point up by name ourselves.
                        GameObject* spawn = GameplaySystems::findSpawnPointByName(objects, pit.spawnName);

                        if (spawn) {
                            if (Transform* spawnPos = spawn->get<Transform>()) {
                                spawnX = spawnPos->x;
                                spawnY = spawnPos->y;
                            }
                        }
                    }

                    loseLife("Hello Kitty fell into a pit and was sent back to a spawn point.",
                             spawnX, spawnY);
                    break;
                }
            }

            // Update the proxy again before checking the scroll trigger.
            kittyProxyTransform.x = helloKitty->x;
            kittyProxyTransform.y = helloKitty->y;

            // Activate the reusable side-scroll trigger.
            // The component translates the engine camera once.
            GameplaySystems::updateSideScrollTriggers(objects, engine.getCamera(), kittyProxy.getId());
            SideScrollTrigger* scrollBehavior = scrollTriggerObject.get<SideScrollTrigger>();

            // The existing game still renders legacy Entity objects.
            // Shift those entities to make the transition visible.
            // Kuromi is not shifted: she always patrols the visible window.

            // Right boundary: Kitty pushes the level to the left.
            // (scrollBehavior is still updated above for the engine's own
            // trigger, but the scroll itself is driven by the overlap test.)
            (void)scrollBehavior;

            if (helloKitty->x > scrollThreshold && worldScrollOffset < maxScroll &&
                boxesOverlap(helloKitty->x, helloKitty->y, helloKitty->width, helloKitty->height,
                             rightScrollTransform.x, rightScrollTransform.y,
                             rightScrollTransform.width, rightScrollTransform.height)) {

                const float scrollDelta = std::min(helloKitty->x - scrollThreshold,
                                                   maxScroll - worldScrollOffset);

                helloKitty->x -= scrollDelta;   // she stays at the boundary
                shiftWorld(scrollDelta);
            }

            // Left boundary: Kitty pushes the level back to the right.
            if (worldScrollOffset > 0.0f &&
                helloKitty->x < leftScrollThreshold &&
                boxesOverlap(helloKitty->x, helloKitty->y, helloKitty->width, helloKitty->height,
                             leftScrollTransform.x, leftScrollTransform.y,
                             leftScrollTransform.width, leftScrollTransform.height)) {

                const float scrollDelta = std::min(leftScrollThreshold - helloKitty->x,
                                                   worldScrollOffset);

                helloKitty->x += scrollDelta;
                shiftWorld(-scrollDelta);
            }

            kittyProxyTransform.x = helloKitty->x;

            // Send local player state to server.
            if (network) {
                NetMessage state{};

                state.type = NetMessageType::PlayerState;
                state.tic = playerTime.getTime();

                {
                    std::lock_guard<std::mutex> lock(helloKitty->stateMutex.get());

                    state.x = helloKitty->x + worldScrollOffset;
                    state.y = helloKitty->y;
                    state.velocityX = helloKitty->velocityX;
                    state.velocityY = helloKitty->velocityY;
                }

                network->sendPlayerState(state);
            }
        },

        // WORLD UPDATE
        // Kuromi is server-controlled.
        [&](float deltaTime)
        {
            // Do not locally simulate Kuromi.
            // Her state comes from PlatformState messages received from the server.
            (void)deltaTime;
            (void)worldTime;
        });

    // Cleanup
    if (network) {
        network->disconnect();
    }

    SDL_DestroyTexture(backgroundTexture);
    SDL_DestroyTexture(helloKittyTexture);
    SDL_DestroyTexture(kuromiTexture);

    SDL_DestroyTexture(bigPlatformTexture);
    SDL_DestroyTexture(smallPlatformTexture);
    SDL_DestroyTexture(heartTexture);
    SDL_DestroyTexture(brokenHeartTexture);

    return 0;
}