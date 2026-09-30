#include "Engine.h"
#include "Collision.h"
#include "Image.h"
#include "NetworkClient.h"
#include "NetMessage.h"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

int main(int argc, char *argv[])
{
    /*
     * Networking arguments
     * Usage: ymherya_game --client 127.0.0.1 5555
     */
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

    /*
     * Engine
     */
    Engine engine("Hello Kitty Adventure");

    EntityManager &entities = engine.getEntities();
    Renderer &renderer = engine.getRenderer();

    const float gameWidth = 1280.0f;
    const float gameHeight = 800.0f;

    entities.setReferenceResolution(static_cast<int>(gameWidth), static_cast<int>(gameHeight));
    entities.setScaleMode(ScaleMode::Percentage);

    /*
     * Timelines
     * PlayerTime: Local Hello Kitty.
     * WorldTime: Kuromi and remote players.
     */
    Timeline &playerTime = engine.getTimeline(Engine::PlayerTime);

    Timeline &worldTime = engine.getTimeline(Engine::WorldTime);

    /*
     * Networking
     */
    std::unique_ptr<NetworkClient> network;

    if (networked) {
        network = std::make_unique<NetworkClient>(serverAddress, serverPort);

        if (!network->connect()) {
            std::cerr << "Could not connect to server.\n";
            return 1;
        }
    }

    /*
     * Load textures
     */
    SDL_Surface *backgroundSurface = loadImage("Games/ymherya/assets/background.png");
    SDL_Surface *helloKittySurface = loadImage("Games/ymherya/assets/hello-kitty.png");
    SDL_Surface *kuromiSurface = loadImage("Games/ymherya/assets/kuromi.png");
    SDL_Texture *backgroundTexture = SDL_CreateTextureFromSurface(renderer.getHandle(), backgroundSurface);
    SDL_Texture *helloKittyTexture = SDL_CreateTextureFromSurface(renderer.getHandle(), helloKittySurface);
    SDL_Texture *kuromiTexture = SDL_CreateTextureFromSurface(renderer.getHandle(), kuromiSurface);

    SDL_DestroySurface(backgroundSurface);
    SDL_DestroySurface(helloKittySurface);
    SDL_DestroySurface(kuromiSurface);

    if (backgroundTexture == nullptr || helloKittyTexture == nullptr || kuromiTexture == nullptr) {
        throw std::runtime_error(std::string("Failed to create game textures: ") + SDL_GetError());
    }

    /*
     * Background
     */
    const float groundY = gameHeight * 0.86f;
    Entity *background = entities.createEntity( 0.0f, 0.0f, gameWidth, gameHeight);

    entities.setTexture(background, backgroundTexture, 1);

    background->affectedByGravity = false;
    background->timelineId = Engine::WorldTime;

    /*
     * Hello Kitty dimensions
     */
    const float kittyHeight = gameHeight * 0.18f;
    const float kittyWidth = kittyHeight * 0.60f;
    const float kittyGroundOffset = 50.0f;

    /*
     * Local Hello Kitty
     */
    Entity *helloKitty = entities.createEntity(gameWidth * 0.55f, groundY - kittyHeight - kittyGroundOffset, kittyWidth, kittyHeight);

    entities.setTexture(helloKitty, helloKittyTexture, 8);

    helloKitty->affectedByGravity = true;
    helloKitty->timelineId = Engine::PlayerTime;

    bool helloKittyGrounded = true;

    /*
     * Death counter
     */
    int helloKittyDeaths = 0;

    /*
     * Prevents the same collision from counting multiple
     * deaths while Kitty is still overlapping Kuromi.
     */
    bool helloKittyRecentlyHit = false;

    /*
     * Number of seconds Kitty remains protected after
     * being respawned.
     */
    double deathCooldown = 0.0;

    const double deathCooldownDuration = 1.0;

    /*
     * Kuromi dimensions
     */
    const float kuromiHeight = gameHeight * 0.25f;
    const float kuromiWidth = kuromiHeight * 0.45f;

    /*
     * Original Kuromi patrol area
     */
    const float kuromiMinX = gameWidth * 0.05f;
    const float kuromiMaxX = gameWidth * 0.40f;
    const float kuromiY = groundY - kuromiHeight;

    /*
     * Kuromi
     * The server controls her position through PlatformState.
     */
    Entity *kuromi = entities.createEntity(gameWidth * 0.15f, kuromiY, kuromiWidth, kuromiHeight);

    entities.setTexture(kuromi, kuromiTexture, 8);

    kuromi->affectedByGravity = false;
    kuromi->timelineId = Engine::WorldTime;

    /*
     * Remote Hello Kitties
     */
    std::unordered_map<uint32_t, Entity *> remoteKitties;
    std::unordered_map<uint32_t, float> remotePreviousX;
    std::unordered_map<uint32_t, double> remoteAnimationTime;
    std::unordered_map<uint32_t, int64_t> lastTic;

    /*
     * Remote player creation queue
     */
    std::mutex spawnMutex;
    std::vector<NetMessage> pendingSpawns;

    /*
     * Animation
     */
    double helloKittyAnimationTime = 0.0;
    double kuromiAnimationTime = 0.0;
    const double animationFrameTime = 0.10;

    /*
     * Player constants
     */
    const float kittySpeed = 350.0f;
    const float jumpVelocity = -700.0f;

    /*
     * Existing server PlatformState coordinates
     * Server: x = 520 + 180 * sin(seconds)
     * Range: 340 -> 700
     * We map this into the original Kuromi patrol area.
     */
    const float serverPlatformMinX = 520.0f - 180.0f;
    const float serverPlatformMaxX = 520.0f + 180.0f;
    const float kuromiPatrolMinX = kuromiMinX;
    const float kuromiPatrolMaxX = kuromiMaxX - kuromiWidth;

    /*
     * Main game loop
     */
    engine.run(
        /*
         * GAME UPDATE
         */
        [&](float deltaTime)
        {
            /*
             * PlayerTime controls local Kitty.
             */
            const float playerDeltaTime = playerTime.isPaused() ? 0.0 : deltaTime * static_cast<float>(playerTime.getScale());

            /*
             * Death cooldown
             */
            if (deathCooldown > 0.0) {
                deathCooldown -= static_cast<double>(deltaTime);

                if (deathCooldown <= 0.0) {
                    deathCooldown = 0.0;
                    helloKittyRecentlyHit = false;
                }
            }

            /*
             * Create remote players queued by the network thread.
             */
            {
                std::lock_guard<std::mutex> lock(spawnMutex);

                for (const NetMessage &message : pendingSpawns) {
                    if (remoteKitties.find(message.clientId) != remoteKitties.end()) {
                        continue;
                    }

                    Entity *remote = entities.createEntity(message.x, message.y, kittyWidth, kittyHeight);
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
                }

                pendingSpawns.clear();
            }

            /*
             * Process server messages.
             */
            if (network) {
                const std::vector<NetMessage> messages = network->getLatestMessages();

                std::unordered_set<uint32_t> activeClients;

                for (const NetMessage &message : messages) {
                    /*
                     * SERVER-CONTROLLED KUROMI
                     */
                    if (message.type == NetMessageType::PlatformState) {
                        float normalizedX = (message.x - serverPlatformMinX) / (serverPlatformMaxX - serverPlatformMinX);

                        if (normalizedX < 0.0f) {
                            normalizedX = 0.0f;
                        }

                        if (normalizedX > 1.0f) {
                            normalizedX = 1.0f;
                        }

                        const float mappedX = kuromiPatrolMinX + normalizedX * (kuromiPatrolMaxX - kuromiPatrolMinX);

                        {
                            std::lock_guard<std::mutex> lock(kuromi->stateMutex.get());

                            kuromi->x = mappedX;
                            kuromi->y = kuromiY;

                            /*
                             * Position is controlled by
                             * the server.
                             */
                            kuromi->velocityX = 0.0f;
                            kuromi->velocityY = 0.0f;

                            kuromi->active = true;
                        }

                        /*
                         * Server velocity determines direction.
                         */
                        if (message.velocityX > 0.0f) {
                            kuromi->flipHorizontal = false;
                        } else if (message.velocityX < 0.0f) {
                            kuromi->flipHorizontal = true;
                        }

                        /*
                         * Server movement determines
                         * whether Kuromi is animated.
                         */
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

                    /*
                     * REMOTE HELLO KITTY
                     */
                    if (message.type != NetMessageType::PlayerState) {
                        continue;
                    }

                    if (message.clientId == network->getClientId()) {
                        continue;
                    }

                    activeClients.insert(message.clientId);

                    /*
                     * Ignore stale messages.
                     */
                    auto ticIt = lastTic.find(message.clientId);

                    if (ticIt != lastTic.end() && message.tic < ticIt->second) {
                        continue;
                    }

                    lastTic[message.clientId] = message.tic;

                    /*
                     * Existing remote Kitty.
                     */
                    auto remoteIt = remoteKitties.find(message.clientId);

                    if (remoteIt != remoteKitties.end()) {
                        Entity *remote = remoteIt->second;

                        const float previousX = remotePreviousX[message.clientId];
                        const float currentX = message.x;
                        const float movement = currentX - previousX;

                        /*
                         * Determine direction.
                         */
                        if (movement > 0.01f) {
                            remote->flipHorizontal = false;
                        } else if (movement < -0.01f) {
                            remote->flipHorizontal = true;
                        }

                        /*
                         * Animate if actually moving.
                         */
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

                        /*
                         * Apply server position.
                         */
                        {
                            std::lock_guard<std::mutex> lock(remote->stateMutex.get());

                            remote->x = message.x;
                            remote->y = message.y;

                            remote->velocityX = 0.0f;
                            remote->velocityY = 0.0f;

                            remote->active = true;
                        }

                        remotePreviousX[message.clientId] = currentX;
                        continue;
                    }

                    /*
                     * New remote Kitty.
                     */
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

                /*
                 * Hide disconnected remote players.
                 */
                for (auto &entry : remoteKitties) {
                    const uint32_t clientId = entry.first;

                    Entity *remote = entry.second;

                    if (activeClients.find(clientId) == activeClients.end()) {
                        std::lock_guard<std::mutex> lock(remote->stateMutex.get());
                        remote->active = false;
                    }
                }
            }

            /*
             * Local Hello Kitty
             */
            if (!playerTime.isPaused()) {
                const bool kittyWalking = Input::isKeyPressed(SDL_SCANCODE_A) || Input::isKeyPressed(SDL_SCANCODE_LEFT) || Input::isKeyPressed(SDL_SCANCODE_D) || Input::isKeyPressed(SDL_SCANCODE_RIGHT);

                /*
                 * Move left.
                 */
                if (Input::isKeyPressed(SDL_SCANCODE_A) || Input::isKeyPressed(SDL_SCANCODE_LEFT)) {
                    helloKitty->x -= kittySpeed * playerDeltaTime;
                    helloKitty->flipHorizontal = true;
                }

                /*
                 * Move right.
                 */
                if (Input::isKeyPressed(SDL_SCANCODE_D) || Input::isKeyPressed(SDL_SCANCODE_RIGHT)) {
                    helloKitty->x += kittySpeed * playerDeltaTime;
                    helloKitty->flipHorizontal = false;
                }

                /*
                 * Jump.
                 */
                if ((Input::isKeyJustPressed(SDL_SCANCODE_SPACE) || Input::isKeyJustPressed(SDL_SCANCODE_W) || Input::isKeyJustPressed(SDL_SCANCODE_UP)) && helloKittyGrounded) {
                    helloKitty->velocityY = jumpVelocity;
                    helloKittyGrounded = false;
                }

                /*
                 * Walking animation.
                 */
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

            /*
             * Screen boundaries
             */
            if (helloKitty->x < 0.0f) {
                helloKitty->x = 0.0f;
            }

            if (helloKitty->x + helloKitty->width > gameWidth) {
                helloKitty->x = gameWidth - helloKitty->width;
            }

            /*
             * Ground collision
             */
            const float kittyGroundY = groundY - kittyHeight - kittyGroundOffset;

            if (helloKitty->y >= kittyGroundY && helloKitty->velocityY >= 0.0f) {
                helloKitty->y = kittyGroundY;
                helloKitty->velocityY = 0.0f;
                helloKittyGrounded = true;
            } else {
                helloKittyGrounded = false;
            }

            /*
             * Kuromi collision
             * Only count one death per collision.
             */
            if (!helloKittyRecentlyHit && deathCooldown <= 0.0 && Collision::checkCollision(*helloKitty, *kuromi)) {
                helloKittyDeaths++;

                std::cout
                    << "Hello Kitty was killed by Kuromi! "
                    << "Deaths: "
                    << helloKittyDeaths
                    << '\n';

                /*
                 * Respawn Kitty.
                 */
                helloKitty->x = gameWidth * 0.55f;
                helloKitty->y = kittyGroundY;
                helloKitty->velocityY = 0.0f;
                helloKittyGrounded = true;
                helloKitty->spriteFrame = 0;
                helloKittyAnimationTime = 0.0;

                /*
                 * Prevent the same collision from
                 * counting repeatedly.
                 */
                helloKittyRecentlyHit = true;
                deathCooldown = deathCooldownDuration;
            }

            /*
             * Send local player state to server.
             */
            if (network) {
                NetMessage state{};

                state.type = NetMessageType::PlayerState;
                state.tic = playerTime.getTime();

                {
                    std::lock_guard<std::mutex> lock(helloKitty->stateMutex.get());

                    state.x = helloKitty->x;
                    state.y = helloKitty->y;
                    state.velocityX = helloKitty->velocityX;
                    state.velocityY = helloKitty->velocityY;
                }

                network->sendPlayerState(state);
            }
        },

        /*
         * WORLD UPDATE
         * Kuromi is server-controlled.
         */
        [&](float deltaTime)
        {
            /*
             * Do not locally simulate Kuromi.
             * Her state comes from PlatformState messages
             * received from the server.
             */
            (void)deltaTime;
            (void)worldTime;
        });

    /*
     * Cleanup
     */
    if (network) {
        network->disconnect();
    }

    SDL_DestroyTexture(backgroundTexture);
    SDL_DestroyTexture(helloKittyTexture);
    SDL_DestroyTexture(kuromiTexture);

    return 0;
}