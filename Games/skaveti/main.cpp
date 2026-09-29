#include "Engine.h"
#include "Collision.h"
#include "Image.h"
#include "NetworkClient.h"
#include "PeerSession.h"
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

static SDL_Texture *loadTexture(SDL_Renderer *renderer, const char *path)
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

static Entity paddedHitbox(const Entity &e, float padL, float padR, float padT, float padB)
{
    Entity box = e;
    box.x = e.x + e.width * padL;
    box.y = e.y + e.height * padT;
    box.width = e.width * (1.0f - padL - padR);
    box.height = e.height * (1.0f - padT - padB);
    return box;
}

int main(int argc, char *argv[])
{
    const char *host = nullptr;
    int port = 5555;
    uint16_t peerPort = 0;
    std::vector<std::pair<std::string, uint16_t>> otherPeers;

    if (argc >= 3 && std::string(argv[1]) == "--client")
    {
        host = argv[2];
        if (argc >= 4)
        {
            port = std::stoi(argv[3]);
        }
    }
    else if (argc >= 3 && std::string(argv[1]) == "--p2p")
    {
        peerPort = static_cast<uint16_t>(std::stoi(argv[2]));
        for (int i = 3; i < argc; ++i)
        {
            const std::string text(argv[i]);
            const auto colon = text.rfind(':');
            if (colon == std::string::npos)
            {
                std::cerr << "Expected host:port, got " << text << '\n';
                return 1;
            }
            otherPeers.emplace_back(text.substr(0, colon), static_cast<uint16_t>(std::stoi(text.substr(colon + 1))));
        }
    }

    std::unique_ptr<NetworkClient> net;
    if (host != nullptr)
    {
        net = std::make_unique<NetworkClient>(host, static_cast<uint16_t>(port));
        if (!net->connect())
        {
            std::cerr << "Failed to connect to " << host << ":" << port << '\n';
            return 1;
        }
    }

    std::unique_ptr<PeerSession> peers;
    if (peerPort != 0)
    {
        peers = std::make_unique<PeerSession>(peerPort);
        for (const auto &other : otherPeers)
        {
            peers->connectTo(other.first, other.second);
        }
    }

    const uint32_t localId = net ? net->getClientId() : (peers ? peers->getBindPort() : 0u);

    Engine engine("Cave Ninja - Milestone 2", 1280, 720);
    EntityManager &entities = engine.getEntities();
    SDL_Renderer *renderer = engine.getRenderer().getHandle();

    const int referenceWidth = 1280;
    const int referenceHeight = 720;
    entities.setReferenceResolution(referenceWidth, referenceHeight);
    entities.setScaleMode(ScaleMode::Percentage);

    SDL_Texture *backgroundTexture = loadTexture(renderer, "Games/skaveti/assets/background.png");
    Entity *background = entities.createEntity(0.0f, 0.0f, float(referenceWidth), float(referenceHeight));
    entities.setTexture(background, backgroundTexture, 1);
    background->affectedByGravity = false;

    const float platformWidth = referenceWidth * 0.30f;
    const float platformHeight = referenceHeight * 0.11f;
    const float platformVisibleTopOffset = platformHeight * 0.55f;
    const float movingPlatformCenterX = 520.0f;
    const float movingPlatformAmplitude = 180.0f;
    const float movingPlatformY = referenceHeight * 0.60f;

    SDL_Texture *platformTexture = loadTexture(renderer, "Games/skaveti/assets/platform.png");
    Entity *platformLeft = entities.createEntity(referenceWidth * 0.12f, referenceHeight * 0.78f, platformWidth, platformHeight);
    entities.setTexture(platformLeft, platformTexture, 1);
    platformLeft->affectedByGravity = false;
    Entity *platformRight = entities.createEntity(movingPlatformCenterX, movingPlatformY, platformWidth, platformHeight);
    entities.setTexture(platformRight, platformTexture, 1);
    platformRight->affectedByGravity = false;
    platformRight->timelineId = Engine::WorldTime;

    const float playerSize = referenceWidth * 0.095f;
    SDL_Texture *playerTexture = loadTexture(renderer, "Games/skaveti/assets/player.png");
    const int playerStartX = static_cast<int>(referenceWidth * 0.18f) + 60 * static_cast<int>(localId % 4);
    const int playerStartY = static_cast<int>(referenceHeight * 0.10f);
    Entity *player = entities.createEntity(playerStartX, playerStartY, playerSize, playerSize);
    entities.setTexture(player, playerTexture, 4);
    player->timelineId = Engine::PlayerTime;

    player->affectedByGravity = true;
    bool isGrounded = false;
    bool onMovingPlatform = false;
    float lastFeetY = 0.0f;
    float walkAnimTimer = 0.0f;
    const int runFrames[3] = {1, 2, 3};

    const float enemySize = referenceWidth * 0.09f;
    SDL_Texture *enemyTexture = loadTexture(renderer, "Games/skaveti/assets/shadow_wisp.png");
    Entity *enemy = entities.createEntity(platformRight->x, platformRight->y - enemySize, enemySize, enemySize);
    entities.setTexture(enemy, enemyTexture, 1);
    enemy->affectedByGravity = false;
    enemy->timelineId = Engine::WorldTime;

    Timeline &playerTime = engine.getTimeline(Engine::PlayerTime);
    Timeline &worldTime = engine.getTimeline(Engine::WorldTime);
    Timeline &globalTime = engine.getGlobalTimeline();

    std::unordered_map<uint32_t, Entity *> remoteNinjas;
    std::unordered_map<uint32_t, int64_t> lastTic;
    std::mutex spawnMutex;
    std::vector<NetMessage> pendingSpawns;

    float lastPlatformX = platformRight->x;
    int64_t lastTitleUpdate = 0;
    uint64_t framesSinceTitle = 0;

    auto showRemote = [&](const NetMessage &message)
    {
        if (message.tic < lastTic[message.clientId])
        {
            return;
        }
        lastTic[message.clientId] = message.tic;

        auto found = remoteNinjas.find(message.clientId);
        if (found == remoteNinjas.end())
        {
            std::lock_guard<std::mutex> lock(spawnMutex);
            pendingSpawns.push_back(message);
            return;
        }

        Entity *remote = found->second;
        std::lock_guard<std::mutex> lock(remote->stateMutex.get());
        remote->x = message.x;
        remote->y = message.y;
        remote->velocityX = 0.0f;
        remote->velocityY = 0.0f;
        if (message.velocityX < 0.0f)
        {
            remote->flipHorizontal = true;
        }
        else if (message.velocityX > 0.0f)
        {
            remote->flipHorizontal = false;
        }
        if (message.velocityY != 0.0f)
        {
            remote->spriteFrame = 2;
        }
        else if (message.velocityX != 0.0f)
        {
            remote->spriteFrame = runFrames[static_cast<int>(worldTime.getSeconds() * 8.0) % 3];
        }
        else
        {
            remote->spriteFrame = 0;
        }
    };

    auto spawnRemotes = [&]()
    {
        std::vector<NetMessage> spawns;
        {
            std::lock_guard<std::mutex> lock(spawnMutex);
            spawns.swap(pendingSpawns);
        }
        for (const NetMessage &message : spawns)
        {
            if (remoteNinjas.count(message.clientId) != 0)
            {
                continue;
            }
            Entity *remote = entities.createEntity(message.x, message.y, playerSize, playerSize);
            entities.setTexture(remote, playerTexture, 4);
            remote->affectedByGravity = false;
            remote->timelineId = Engine::WorldTime;
            remoteNinjas[message.clientId] = remote;
        }
    };

    engine.run(
        [&](float)
        {
            const float playerDelta = static_cast<float>(playerTime.getLastDelta());
            float moveSpeed = referenceWidth * 0.32f; // noticeably faster than the enemy's patrol

            float platformX = 0.0f;
            {
                std::lock_guard<std::mutex> lock(platformRight->stateMutex.get());
                platformX = platformRight->x;
            }
            const float platformShift = platformX - lastPlatformX;
            lastPlatformX = platformX;

            {
                std::lock_guard<std::mutex> lock(player->stateMutex.get());

                if (onMovingPlatform && isGrounded && !playerTime.isPaused())
                {
                    player->x += platformShift;
                }

                bool moving = false;
                player->velocityX = 0.0f;
                if (Input::isKeyPressed(SDL_SCANCODE_A) || Input::isKeyPressed(SDL_SCANCODE_LEFT))
                {
                    player->velocityX = -moveSpeed;
                    player->flipHorizontal = true;
                    moving = true;
                }
                if (Input::isKeyPressed(SDL_SCANCODE_D) || Input::isKeyPressed(SDL_SCANCODE_RIGHT))
                {
                    player->velocityX = moveSpeed;
                    player->flipHorizontal = false;
                    moving = true;
                }
                if ((Input::isKeyJustPressed(SDL_SCANCODE_W) ||
                     Input::isKeyJustPressed(SDL_SCANCODE_UP) ||
                     Input::isKeyJustPressed(SDL_SCANCODE_SPACE)) &&
                    isGrounded && !playerTime.isPaused())
                {
                    player->velocityY = -700.0f;
                    isGrounded = false;
                }

                if (moving && isGrounded)
                {
                    walkAnimTimer += playerDelta;
                    if (walkAnimTimer >= 0.12f)
                    {
                        walkAnimTimer = 0.0f;
                        static int runIndex = 0;
                        runIndex = (runIndex + 1) % 3;
                        player->spriteFrame = runFrames[runIndex];
                    }
                }
                else if (!isGrounded)
                {
                    player->spriteFrame = 2;
                }
                else
                {
                    walkAnimTimer = 0.0f;
                    player->spriteFrame = 0;
                }

                if (Input::isKeyJustPressed(SDL_SCANCODE_TAB))
                {
                    entities.toggleScaleMode();
                }

                Entity leftVisible = *platformLeft;
                leftVisible.y += platformVisibleTopOffset;
                leftVisible.height -= platformVisibleTopOffset;

                Entity rightVisible = *platformRight;
                rightVisible.x = platformX;
                rightVisible.y += platformVisibleTopOffset;
                rightVisible.height -= platformVisibleTopOffset;

                Entity playerFeet = paddedHitbox(*player, 0.25f, 0.25f, 0.0f, 0.0f);
                bool touchingLeft = Collision::checkCollision(playerFeet, leftVisible);
                bool touchingRight = Collision::checkCollision(playerFeet, rightVisible);

                bool landLeft = touchingLeft && lastFeetY <= leftVisible.y + 1.0f;
                bool landRight = touchingRight && lastFeetY <= rightVisible.y + 1.0f;

                if (player->velocityY >= 0.0f && (landLeft || landRight))
                {
                    Entity &landedOn = landRight ? rightVisible : leftVisible;
                    player->y = landedOn.y - player->height;
                    player->velocityY = 0.0f;
                    isGrounded = true;
                    onMovingPlatform = landRight;
                }
                else if (!landLeft && !landRight)
                {
                    isGrounded = false;
                    onMovingPlatform = false;
                }

                const Entity enemyNow = [&]()
                {
                    std::lock_guard<std::mutex> enemyLock(enemy->stateMutex.get());
                    return *enemy;
                }();
                Entity playerHit = paddedHitbox(*player, 0.28f, 0.28f, 0.10f, 0.05f);
                Entity enemyHit = paddedHitbox(enemyNow, 0.20f, 0.20f, 0.15f, 0.15f);
                if (Collision::checkCollision(playerHit, enemyHit))
                {
                    SDL_Log("The ninja was killed by the shadow wisp!");
                    player->x = playerStartX;
                    player->y = playerStartY;
                    player->velocityY = 0.0f;
                    isGrounded = false;
                    onMovingPlatform = false;
                }

                if (player->y > referenceHeight)
                {
                    player->x = playerStartX;
                    player->y = playerStartY;
                    player->velocityY = 0.0f;
                    isGrounded = false;
                    onMovingPlatform = false;
                }
                lastFeetY = player->y + player->height;
            }

            if (net || peers)
            {
                NetMessage state{};
                state.tic = playerTime.getTime();
                {
                    std::lock_guard<std::mutex> lock(player->stateMutex.get());
                    state.x = player->x;
                    state.y = player->y;
                    state.velocityX = player->velocityX;
                    state.velocityY = isGrounded ? 0.0f : player->velocityY;
                }
                if (net)
                {
                    net->sendPlayerState(state);
                }
                if (peers)
                {
                    peers->sendPlayerState(state);
                }
                spawnRemotes();
            }

            ++framesSinceTitle;
            const int64_t now = globalTime.getTime();
            if (now - lastTitleUpdate >= 250'000'000)
            {
                const double seconds = static_cast<double>(now - lastTitleUpdate) / 1'000'000'000.0;
                const double loopHz = seconds > 0.0 ? static_cast<double>(framesSinceTitle) / seconds : 0.0;
                framesSinceTitle = 0;
                lastTitleUpdate = now;
                const char *mode = net ? "client" : (peers ? "peer" : "offline");
                char title[200];
                std::snprintf(title, sizeof(title),
                              "Cave Ninja | %s %u | ninjas %zu | loop %.0f Hz | ninja time: %s x%.1f",
                              mode,
                              localId,
                              remoteNinjas.size() + 1,
                              loopHz,
                              playerTime.isPaused() ? "PAUSED" : "running",
                              playerTime.getScale());
                SDL_SetWindowTitle(engine.getWindow().getHandle(), title);
            }
        },
        [&](float)
        {
            bool havePlatform = false;
            float platformX = 0.0f;

            if (net)
            {
                for (const NetMessage &message : net->getLatestMessages())
                {
                    if (message.type == NetMessageType::PlatformState)
                    {
                        platformX = message.x;
                        havePlatform = true;
                    }
                    else if (message.type == NetMessageType::PlayerState &&
                             message.clientId != net->getClientId())
                    {
                        showRemote(message);
                    }
                }
            }
            else if (peers)
            {
                platformX = peers->platformX();
                havePlatform = true;
                for (const NetMessage &message : peers->getRemotePlayers())
                {
                    if (message.clientId != peers->getBindPort())
                    {
                        showRemote(message);
                    }
                }
            }
            else
            {
                platformX = movingPlatformCenterX + movingPlatformAmplitude * static_cast<float>(std::sin(worldTime.getSeconds()));
                havePlatform = true;
            }

            if (!havePlatform)
            {
                return;
            }

            {
                std::lock_guard<std::mutex> lock(platformRight->stateMutex.get());
                platformRight->x = platformX;
                platformRight->y = movingPlatformY;
                platformRight->velocityX = 0.0f;
                platformRight->velocityY = 0.0f;
            }

            float phase = (platformX - movingPlatformCenterX) / movingPlatformAmplitude;
            if (phase > 1.0f)
            {
                phase = 1.0f;
            }
            if (phase < -1.0f)
            {
                phase = -1.0f;
            }
            const float patrolRange = (platformWidth - enemySize) * 0.5f;
            const float enemyX = platformX + platformWidth * 0.5f - enemySize * 0.5f - phase * patrolRange;

            std::lock_guard<std::mutex> lock(enemy->stateMutex.get());
            if (enemyX != enemy->x)
            {
                enemy->flipHorizontal = enemyX > enemy->x;
            }
            enemy->x = enemyX;
            enemy->y = movingPlatformY + platformVisibleTopOffset - enemy->height;
            enemy->velocityX = 0.0f;
            enemy->spriteFrame = 0; // single-frame sprite, no animation to cycle
        });

    SDL_DestroyTexture(backgroundTexture);
    SDL_DestroyTexture(platformTexture);
    SDL_DestroyTexture(playerTexture);
    SDL_DestroyTexture(enemyTexture);
    return 0;
}
