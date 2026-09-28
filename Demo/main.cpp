#include "Engine.h"
#include "NetworkClient.h"

#include <cstdio>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

int main(int argc, char *argv[])
{
    const char *host = nullptr;
    int port = 5555;

    if (argc >= 3 && std::string(argv[1]) == "--client")
    {
        host = argv[2];
        if (argc >= 4)
        {
            port = std::stoi(argv[3]);
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

    Engine engine("Group 4 Engine Demo", 1280, 720);
    EntityManager &entities = engine.getEntities();
    engine.getPhysics().setGravity(0.0f);

    const float worldWidth = 1280.0f;
    const float platformSpeed = 250.0f;
    const float playerSpeed = 300.0f;

    Entity *platform = entities.createEntity(100.0f, 520.0f, 240.0f, 30.0f, {160, 160, 160, 255});
    platform->timelineId = Engine::WorldTime;
    platform->velocityX = net ? 0.0f : platformSpeed;

    Entity *player = entities.createEntity(600.0f, 300.0f, 50.0f, 50.0f, {230, 60, 60, 255});
    player->timelineId = Engine::PlayerTime;

    Timeline &playerTime = engine.getTimeline(Engine::PlayerTime);
    Timeline &worldTime = engine.getTimeline(Engine::WorldTime);
    Timeline &globalTime = engine.getGlobalTimeline();
    int64_t lastTitleUpdate = 0;

    std::unordered_map<uint32_t, Entity *> remotePlayers;
    std::unordered_map<uint32_t, int64_t> lastTic;
    std::mutex spawnMutex;
    std::vector<NetMessage> pendingSpawns;

    const SDL_Color remoteColors[] = {
        {60, 120, 230, 255},
        {60, 200, 120, 255},
        {230, 200, 60, 255},
        {200, 80, 220, 255},
    };

    auto applySnapshot = [&]()
    {
        for (const NetMessage &message : net->getLatestMessages())
        {
            if (message.type == NetMessageType::PlatformState)
            {
                std::lock_guard<std::mutex> lock(platform->stateMutex.get());
                platform->x = message.x;
                platform->y = message.y;
                platform->velocityX = 0.0f;
                platform->velocityY = 0.0f;
            }
            else if (message.type == NetMessageType::PlayerState &&
                     message.clientId != net->getClientId())
            {
                if (message.tic < lastTic[message.clientId])
                {
                    continue;
                }
                lastTic[message.clientId] = message.tic;

                auto found = remotePlayers.find(message.clientId);
                if (found == remotePlayers.end())
                {
                    std::lock_guard<std::mutex> lock(spawnMutex);
                    pendingSpawns.push_back(message);
                    continue;
                }

                Entity *remote = found->second;
                std::lock_guard<std::mutex> lock(remote->stateMutex.get());
                remote->x = message.x;
                remote->y = message.y;
                remote->velocityX = 0.0f;
                remote->velocityY = 0.0f;
            }
        }
    };

    auto bouncePlatform = [&]()
    {
        std::lock_guard<std::mutex> lock(platform->stateMutex.get());
        if (platform->x <= 0.0f)
        {
            platform->x = 0.0f;
            platform->velocityX = platformSpeed;
        }
        if (platform->x + platform->width >= worldWidth)
        {
            platform->x = worldWidth - platform->width;
            platform->velocityX = -platformSpeed;
        }
    };

    engine.run(
        [&](float)
        {
            {
                std::lock_guard<std::mutex> lock(player->stateMutex.get());
                player->velocityX = 0.0f;
                player->velocityY = 0.0f;
                if (Input::isKeyPressed(SDL_SCANCODE_LEFT) || Input::isKeyPressed(SDL_SCANCODE_A))
                {
                    player->velocityX = -playerSpeed;
                }
                if (Input::isKeyPressed(SDL_SCANCODE_RIGHT) || Input::isKeyPressed(SDL_SCANCODE_D))
                {
                    player->velocityX = playerSpeed;
                }
                if (Input::isKeyPressed(SDL_SCANCODE_UP) || Input::isKeyPressed(SDL_SCANCODE_W))
                {
                    player->velocityY = -playerSpeed;
                }
                if (Input::isKeyPressed(SDL_SCANCODE_DOWN) || Input::isKeyPressed(SDL_SCANCODE_S))
                {
                    player->velocityY = playerSpeed;
                }
            }

            if (net)
            {
                NetMessage state{};
                state.tic = playerTime.getTime();
                {
                    std::lock_guard<std::mutex> lock(player->stateMutex.get());
                    state.x = player->x;
                    state.y = player->y;
                    state.velocityX = player->velocityX;
                    state.velocityY = player->velocityY;
                }
                net->sendPlayerState(state);

                std::vector<NetMessage> spawns;
                {
                    std::lock_guard<std::mutex> lock(spawnMutex);
                    spawns.swap(pendingSpawns);
                }
                for (const NetMessage &message : spawns)
                {
                    if (remotePlayers.count(message.clientId) != 0)
                    {
                        continue;
                    }
                    const SDL_Color color = remoteColors[message.clientId % 4];
                    Entity *remote = entities.createEntity(message.x, message.y, 50.0f, 50.0f, color);
                    remote->timelineId = Engine::PlayerTime;
                    remotePlayers[message.clientId] = remote;
                }
            }

            const int64_t now = globalTime.getTime();
            if (now - lastTitleUpdate >= 250'000'000)
            {
                lastTitleUpdate = now;
                char title[220];
                std::snprintf(title, sizeof(title),
                              "client %u | playerTime: %s x%.1f tic %lld | worldTime: tic %lld | frames P %llu W %llu",
                              net ? net->getClientId() : 0u,
                              playerTime.isPaused() ? "PAUSED" : "running",
                              playerTime.getScale(),
                              static_cast<long long>(playerTime.getTime()),
                              static_cast<long long>(worldTime.getTime()),
                              static_cast<unsigned long long>(engine.getPlayerFramesBuilt()),
                              static_cast<unsigned long long>(engine.getWorldFramesBuilt()));
                SDL_SetWindowTitle(engine.getWindow().getHandle(), title);
            }
        },
        [&](float)
        {
            if (net)
            {
                applySnapshot();
            }
            else
            {
                bouncePlatform();
            }
        });

    return 0;
}
