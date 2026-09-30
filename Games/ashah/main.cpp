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
#include <unordered_set>
#include <utility>
#include <vector>

static bool isChromaMagenta(Uint8 r, Uint8 g, Uint8 b)
{
    return r >= 160 && b >= 160 && g <= 90 && (r - g) > 70 && (b - g) > 70;
}

static void applyMagentaKey(SDL_Surface *surface)
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

static SDL_Texture *loadTexture(SDL_Renderer *renderer, const char *path, bool chromaKey)
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

static Entity bodyHitbox(const Entity &e, float padL, float padR, float padT, float padB)
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

    Engine engine("Lost Under the Sea");
    EntityManager &entities = engine.getEntities();
    SDL_Renderer *renderer = engine.getRenderer().getHandle();

    engine.getPhysics().setGravity(1600.0f);

    int width = 0;
    int height = 0;
    SDL_GetWindowSize(engine.getWindow().getHandle(), &width, &height);
    engine.getWindow().setSize(width, height);
    entities.setReferenceResolution(width, height);
    const int referenceWidth = width;
    const int referenceHeight = height;

    SDL_Texture *backgroundTex = loadTexture(renderer, "Games/ashah/assets/background.png", false);
    SDL_Texture *shelfTex = loadTexture(renderer, "Games/ashah/assets/shelf.png", true);
    SDL_Texture *diverTex = loadTexture(renderer, "Games/ashah/assets/diver.png", true);
    SDL_Texture *diverAltTex = loadTexture(renderer, "Games/ashah/assets/diver_alt.png", true);
    SDL_Texture *fishTex = loadTexture(renderer, "Games/ashah/assets/fish.png", true);
    SDL_Texture *bubbleTex = loadTexture(renderer, "Games/ashah/assets/bubble.png", true);

    Entity *background = entities.createEntity(0.0f, 0.0f, float(width), float(height));
    entities.setTexture(background, backgroundTex, 1);
    background->affectedByGravity = false;

    const float shelfPadTop = 0.20f;
    const float diverPadBottom = 0.10f;

    const float shelfWidth = width * 0.70f;
    const float shelfHeight = height * 0.14f;
    const float shelfX = width * 0.15f;
    const float shelfY = height * 0.72f;
    Entity *shelf = entities.createEntity(shelfX, shelfY, shelfWidth, shelfHeight);
    entities.setTexture(shelf, shelfTex, 1);
    shelf->affectedByGravity = false;

    const float walkLeft = shelfX;
    const float walkRight = shelfX + shelfWidth;

    struct Ledge
    {
        Entity *entity;
        float left;
        float right;
        float surfaceY;
    };

    const float shelfWalkY = shelfY + shelfHeight * shelfPadTop;
    const float ledgeHeight = shelfHeight;
    const float ledgeWidth = shelfWidth * 0.26f;
    auto makeLedge = [&](float left, float surfaceY)
    {
        const float y = surfaceY - ledgeHeight * shelfPadTop;
        Entity *ledge = entities.createEntity(left, y, ledgeWidth, ledgeHeight);
        entities.setTexture(ledge, shelfTex, 1);
        ledge->affectedByGravity = false;
        return Ledge{ledge, left, left + ledgeWidth, surfaceY};
    };

    Ledge ledges[3] = {
        Ledge{shelf, walkLeft, walkRight, shelfWalkY},
        makeLedge(shelfX + shelfWidth * 0.06f, shelfWalkY - 90.0f),
        makeLedge(shelfX + shelfWidth * 0.36f, shelfWalkY - 150.0f),
    };

    float diverTexW = 0.0f;
    float diverTexH = 0.0f;
    SDL_GetTextureSize(diverTex, &diverTexW, &diverTexH);
    const float diverFrameAspect = (diverTexW / 4.0f) / diverTexH;
    const float diverH = height * 0.13f;
    const float diverW = diverH * diverFrameAspect * 1.20f;
    float diverAltTexW = 0.0f;
    float diverAltTexH = 0.0f;
    SDL_GetTextureSize(diverAltTex, &diverAltTexW, &diverAltTexH);
    // The yellow sheet has more empty space around the body, so the same box height
    // draws a much smaller diver. Scale the box until the body matches the orange one.
    const float diverAltH = diverH * 1.50f;
    const float diverAltW = diverAltH * ((diverAltTexW / 4.0f) / diverAltTexH) * 1.20f;
    const auto altSuit = [](uint32_t id)
    {
        return id != 0 && (id % 2u) == 0u;
    };
    const bool localAltSuit = altSuit(localId);

    const float spawnX = width * 0.40f + 80.0f * static_cast<float>(localId % 4);
    const float standY = shelfWalkY - diverH * (1.0f - diverPadBottom);
    const float spawnY = shelfWalkY - (localAltSuit ? diverAltH : diverH) * (1.0f - diverPadBottom);
    Entity *diver = entities.createEntity(spawnX, spawnY, localAltSuit ? diverAltW : diverW, localAltSuit ? diverAltH : diverH);
    entities.setTexture(diver, localAltSuit ? diverAltTex : diverTex, 4);
    diver->timelineId = Engine::PlayerTime;
    diver->affectedByGravity = true;
    bool grounded = true;
    bool respawnOnWorldTime = false;
    float diverAnim = 0.0f;
    Timeline &playerTime = engine.getTimeline(Engine::PlayerTime);
    Timeline &worldTime = engine.getTimeline(Engine::WorldTime);

    float fishTexW = 0.0f;
    float fishTexH = 0.0f;
    SDL_GetTextureSize(fishTex, &fishTexW, &fishTexH);
    const float fishFrameAspect = (fishTexW / 4.0f) / fishTexH;
    const float fishH = height * 0.10f;
    const float fishW = fishH * fishFrameAspect * 1.25f;
    const float fishY = standY + diverH * 0.45f - fishH * 0.50f;
    Entity *fish = entities.createEntity(shelfX + shelfWidth * 0.55f, fishY, fishW, fishH);
    entities.setTexture(fish, fishTex, 4);
    fish->affectedByGravity = false;
    float fishMinX = walkLeft;
    float fishMaxX = walkRight - fishW;
    float fishSpeed = 140.0f;
    float fishAnim = 0.0f;

    auto dropInFromTop = [&]()
    {
        diver->x = spawnX;
        diver->y = -diver->height;
        diver->velocityX = 0.0f;
        diver->velocityY = 80.0f;
        grounded = false;
        if (playerTime.isPaused())
        {
            diver->timelineId = Engine::WorldTime;
            respawnOnWorldTime = true;
        }
    };

    int health = 3;
    bool escaped = false;
    bool fishContact = false;
    const char *story = "Climb the ledges. The fish owns the lower shelf.";
    Entity *hearts[3];
    for (int i = 0; i < 3; ++i)
    {
        hearts[i] = entities.createEntity(28.0f + i * 36.0f, 28.0f, 24.0f, 24.0f, {220, 40, 50, 255});
        hearts[i]->affectedByGravity = false;
    }

    auto refreshHearts = [&]()
    {
        for (int i = 0; i < 3; ++i)
        {
            hearts[i]->active = i < health;
        }
    };

    auto loseLife = [&](const char *hitStory, const char *emptyStory)
    {
        if (health > 0)
        {
            --health;
        }
        if (health == 0)
        {
            health = 3;
            escaped = false;
            story = emptyStory;
        }
        else
        {
            story = hitStory;
        }
        refreshHearts();
        SDL_Log("%s", story);
        char title[320];
        std::snprintf(title, sizeof(title), "Lost Under the Sea | health %d | %s", health, story);
        SDL_SetWindowTitle(engine.getWindow().getHandle(), title);
        dropInFromTop();
    };

    auto caughtByFish = [&]()
    {
        if (fishContact)
        {
            return;
        }
        fishContact = true;
        loseLife("The fish struck. Climb above it.", "The fish caught the diver. Climb again.");
    };

    SDL_SetWindowTitle(engine.getWindow().getHandle(),
                       "Lost Under the Sea | health 3 | Climb the ledges. The fish owns the lower shelf.");

    const float fishCenter = (fishMinX + fishMaxX) * 0.5f;
    const float fishAmplitude = (fishMaxX - fishMinX) * 0.5f;
    const float fishOmega = fishSpeed / fishAmplitude;

    std::unordered_map<uint32_t, Entity *> remoteDivers;
    std::unordered_map<uint32_t, int64_t> lastTic;
    std::mutex spawnMutex;
    std::vector<NetMessage> pendingSpawns;
    int64_t lastTitleUpdate = 0;
    uint64_t framesSinceTitle = 0;

    auto showRemote = [&](const NetMessage &message)
    {
        if (message.tic < lastTic[message.clientId])
        {
            return;
        }
        lastTic[message.clientId] = message.tic;

        auto found = remoteDivers.find(message.clientId);
        if (found == remoteDivers.end())
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
        remote->active = true;
        remote->flipHorizontal = message.velocityX < 0.0f;
        if (message.velocityY != 0.0f)
        {
            remote->spriteFrame = altSuit(message.clientId) ? 2 : 1;
        }
        else
        {
            remote->spriteFrame = message.velocityX != 0.0f ? 1 : 0;
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
            if (remoteDivers.count(message.clientId) != 0)
            {
                continue;
            }
            const bool alt = altSuit(message.clientId);
            Entity *remote = entities.createEntity(
                message.x, message.y, alt ? diverAltW : diverW, alt ? diverAltH : diverH);
            entities.setTexture(remote, alt ? diverAltTex : diverTex, 4);
            remote->affectedByGravity = false;
            remote->timelineId = Engine::WorldTime;
            remoteDivers[message.clientId] = remote;
        }
    };

    auto placeFish = [&](float phase)
    {
        if (phase > 1.0f)
        {
            phase = 1.0f;
        }
        if (phase < -1.0f)
        {
            phase = -1.0f;
        }
        const float nextX = fishCenter + fishAmplitude * phase;
        if (nextX > fish->x + 0.25f)
        {
            fish->flipHorizontal = false;
        }
        else if (nextX < fish->x - 0.25f)
        {
            fish->flipHorizontal = true;
        }
        fish->x = nextX;
        fish->y = fishY;
        fish->velocityX = 0.0f;
        fish->velocityY = 0.0f;
    };

    const int bubbleCount = 5;
    Entity *bubbles[5];
    float bubblePhase[5];
    float bubblePop[5];
    for (int i = 0; i < bubbleCount; i++)
    {
        float size = 18.0f + i * 6.0f;
        bubbles[i] = entities.createEntity(
            width * (0.08f + i * 0.16f),
            height * (0.20f + i * 0.12f),
            size,
            size);
        entities.setTexture(bubbles[i], bubbleTex, 3);
        bubbles[i]->affectedByGravity = false;
        bubbles[i]->velocityY = -45.0f - i * 12.0f;
        bubblePhase[i] = i * 0.8f;
        bubblePop[i] = -1.0f;
    }

    engine.run([&](float deltaTime)
               {
        if (Input::isKeyJustPressed(SDL_SCANCODE_TAB) ||
            Input::isKeyJustPressed(SDL_SCANCODE_T))
        {
            entities.toggleScaleMode();
            if (entities.getScaleMode() == ScaleMode::Percentage)
            {
                engine.getWindow().resizeWindow(
                    static_cast<int>(referenceWidth * 0.65f),
                    static_cast<int>(referenceHeight * 0.65f));
            }
            else
            {
                engine.getWindow().resizeWindow(referenceWidth, referenceHeight);
            }
            const char *modeName =
                entities.getScaleMode() == ScaleMode::Pixel ? "Pixel" : "Percentage";
            char title[320];
            std::snprintf(title, sizeof(title),
                          "Lost Under the Sea | health %d | %s | %s",
                          health, story, modeName);
            SDL_SetWindowTitle(engine.getWindow().getHandle(), title);
            SDL_Log("Scale mode: %s", modeName);
        }

        const int currentWidth = engine.getWindow().getWidth();
        const int currentHeight = engine.getWindow().getHeight();

        SDL_SetRenderLogicalPresentation(
            renderer,
            currentWidth,
            currentHeight,
            SDL_LOGICAL_PRESENTATION_STRETCH);

        const float scaleX =
            (entities.getScaleMode() == ScaleMode::Percentage)
                ? static_cast<float>(currentWidth) / static_cast<float>(referenceWidth)
                : 1.0f;
        const float scaleY =
            (entities.getScaleMode() == ScaleMode::Percentage)
                ? static_cast<float>(currentHeight) / static_cast<float>(referenceHeight)
                : 1.0f;
        background->width = currentWidth / scaleX;
        background->height = currentHeight / scaleY;
        if (respawnOnWorldTime && !playerTime.isPaused())
        {
            diver->timelineId = Engine::PlayerTime;
            respawnOnWorldTime = false;
        }

        const float moveSpeed = 380.0f;
        const float playerDelta = static_cast<float>(playerTime.getLastDelta());
        bool moving = false;
        diver->velocityX = 0.0f;

        if (Input::isKeyPressed(SDL_SCANCODE_A) || Input::isKeyPressed(SDL_SCANCODE_LEFT))
        {
            diver->velocityX = -moveSpeed;
            diver->flipHorizontal = true;
            moving = true;
        }
        if (Input::isKeyPressed(SDL_SCANCODE_D) || Input::isKeyPressed(SDL_SCANCODE_RIGHT))
        {
            diver->velocityX = moveSpeed;
            diver->flipHorizontal = false;
            moving = true;
        }
        if ((Input::isKeyJustPressed(SDL_SCANCODE_W) ||
             Input::isKeyJustPressed(SDL_SCANCODE_UP) ||
             Input::isKeyJustPressed(SDL_SCANCODE_SPACE)) &&
            grounded && !playerTime.isPaused())
        {
            diver->velocityY = -720.0f;
            grounded = false;
        }

        if (moving && grounded)
        {
            diverAnim += playerDelta;
            if (localAltSuit)
            {
                diver->spriteFrame = (static_cast<int>(diverAnim * 8.0f) % 2) == 0 ? 1 : 3;
            }
            else
            {
                diver->spriteFrame = 1 + (static_cast<int>(diverAnim * 8.0f) % 3);
            }
        }
        else if (!grounded)
        {
            diver->spriteFrame = localAltSuit ? 2 : 1;
        }
        else
        {
            diverAnim = 0.0f;
            diver->spriteFrame = 0;
        }
        if (!net && !peers)
        {
            const double fishSeconds = worldTime.getSeconds();
            placeFish(static_cast<float>(std::sin(fishOmega * fishSeconds)));
            fishAnim += deltaTime;
            fish->spriteFrame = static_cast<int>(fishAnim * 8.0f) % 4;
        }

        if (net || peers)
        {
            NetMessage state{};
            state.tic = playerTime.getTime();
            state.x = diver->x;
            state.y = diver->y;
            state.velocityX = diver->velocityX;
            state.velocityY = grounded ? 0.0f : diver->velocityY;
            if (net)
            {
                net->sendPlayerState(state);
            }
            if (peers)
            {
                peers->sendPlayerState(state);
            }
            spawnRemotes();

            ++framesSinceTitle;
            const int64_t now = engine.getGlobalTimeline().getTime();
            if (now - lastTitleUpdate >= 250'000'000)
            {
                const double seconds = static_cast<double>(now - lastTitleUpdate) / 1'000'000'000.0;
                const double loopHz = seconds > 0.0 ? static_cast<double>(framesSinceTitle) / seconds : 0.0;
                framesSinceTitle = 0;
                lastTitleUpdate = now;
                char title[320];
                if (peers)
                {
                    std::snprintf(title, sizeof(title),
                                  "Lost Under the Sea | health %d | %s | peer %u | anchor %lld | loop %.0f Hz",
                                  health,
                                  story,
                                  peers->getBindPort(),
                                  static_cast<long long>(peers->getAnchor()),
                                  loopHz);
                }
                else
                {
                    std::snprintf(title, sizeof(title),
                                  "Lost Under the Sea | health %d | %s | client %u | loop %.0f Hz",
                                  health,
                                  story,
                                  localId,
                                  loopHz);
                }
                SDL_SetWindowTitle(engine.getWindow().getHandle(), title);
            }
        }

        for (int i = 0; i < bubbleCount; ++i)
        {
            bubblePhase[i] += deltaTime;
            bubbles[i]->x += std::sin(bubblePhase[i] * 2.0f) * 18.0f * deltaTime;
            if (bubblePop[i] < 0.0f)
            {
                bubbles[i]->spriteFrame = 0;
                if (bubbles[i]->y <= referenceHeight * 0.10f)
                {
                    bubblePop[i] = 0.0f;
                    bubbles[i]->velocityY = 0.0f;
                }
            }
            else
            {
                bubblePop[i] += deltaTime;
                bubbles[i]->spriteFrame = (bubblePop[i] < 0.12f) ? 1 : 2;
                if (bubblePop[i] > 0.28f)
                {
                    bubbles[i]->x = (i + 1) * (referenceWidth / 6.0f);
                    bubbles[i]->y = static_cast<float>(referenceHeight);
                    bubbles[i]->velocityY = -45.0f - i * 12.0f;
                    bubblePop[i] = -1.0f;
                }
            }
        }

        const float diverFeet = diver->y + diver->height * (1.0f - diverPadBottom);
        const float diverMidX = diver->x + diver->width * 0.5f;
        int landed = -1;
        float bestSurface = -1.0e9f;
        for (int i = 0; i < 3; ++i)
        {
            const bool overLedge = diverMidX > ledges[i].left && diverMidX < ledges[i].right;
            const bool touching = Collision::checkCollision(*diver, *ledges[i].entity);
            const float feetGap = diverFeet - ledges[i].surfaceY;
            if (touching && overLedge && diver->velocityY >= 0.0f &&
                feetGap >= -36.0f && feetGap <= 40.0f && ledges[i].surfaceY > bestSurface)
            {
                bestSurface = ledges[i].surfaceY;
                landed = i;
            }
        }

        if (landed >= 0)
        {
            diver->y = ledges[landed].surfaceY - diver->height * (1.0f - diverPadBottom);
            diver->velocityY = 0.0f;
            grounded = true;
            if (respawnOnWorldTime)
            {
                diver->timelineId = Engine::PlayerTime;
                respawnOnWorldTime = false;
            }
            if (landed == 2 && !escaped)
            {
                escaped = true;
                story = "The diver reached the high ledge and found air.";
                SDL_Log("%s", story);
                char title[320];
                std::snprintf(title, sizeof(title), "Lost Under the Sea | health %d | %s", health, story);
                SDL_SetWindowTitle(engine.getWindow().getHandle(), title);
            }
        }
        else
        {
            grounded = false;
        }

        const Entity diverHit = bodyHitbox(*diver, 0.30f, 0.22f, 0.10f, 0.12f);
        const Entity fishHit = bodyHitbox(*fish, 0.20f, 0.18f, 0.42f, 0.22f);
        if (Collision::checkCollision(diverHit, fishHit))
        {
            caughtByFish();
        }
        else
        {
            fishContact = false;
        }

        if (diver->y > static_cast<float>(referenceHeight))
        {
            loseLife("The diver fell off the shelf.", "The diver fell. Climb again.");
        } },
        [&](float worldDelta)
        {
            bool haveFish = false;
            float phase = 0.0f;
            bool haveSnapshot = false;
            std::unordered_set<uint32_t> present;

            if (peers)
            {
                phase = (peers->platformX() - 520.0f) / 180.0f;
                haveFish = true;
                haveSnapshot = true;
                for (const NetMessage &message : peers->getRemotePlayers())
                {
                    if (message.clientId != peers->getBindPort())
                    {
                        present.insert(message.clientId);
                        showRemote(message);
                    }
                }
            }
            else if (net)
            {
                const std::vector<NetMessage> messages = net->getLatestMessages();
                if (!messages.empty())
                {
                    haveSnapshot = true;
                }
                for (const NetMessage &message : messages)
                {
                    if (message.type == NetMessageType::PlatformState)
                    {
                        phase = (message.x - 520.0f) / 180.0f;
                        haveFish = true;
                    }
                    else if (message.type == NetMessageType::PlayerState &&
                             message.clientId != net->getClientId())
                    {
                        present.insert(message.clientId);
                        showRemote(message);
                    }
                }
            }

            if (haveSnapshot)
            {
                for (auto &entry : remoteDivers)
                {
                    std::lock_guard<std::mutex> lock(entry.second->stateMutex.get());
                    entry.second->active = present.count(entry.first) != 0;
                }
            }

            if (haveFish)
            {
                placeFish(phase);
                fishAnim += worldDelta;
                fish->spriteFrame = static_cast<int>(fishAnim * 8.0f) % 4;
            }
        });

    SDL_DestroyTexture(backgroundTex);
    SDL_DestroyTexture(shelfTex);
    SDL_DestroyTexture(diverTex);
    SDL_DestroyTexture(diverAltTex);
    SDL_DestroyTexture(fishTex);
    SDL_DestroyTexture(bubbleTex);
    return 0;
}
