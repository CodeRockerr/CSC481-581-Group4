#include "Engine.h"
#include "NetworkClient.h"
#include "PeerSession.h"

#include <cstdio>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

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

    Engine engine("Group 4 Engine Demo", 1280, 720);
    EntityManager &entities = engine.getEntities();
    engine.getPhysics().setGravity(0.0f);

    const float worldWidth = 1280.0f;
    const float platformSpeed = 250.0f;
    const float playerSpeed = 300.0f;

    Entity *platform = entities.createEntity(100.0f, 520.0f, 240.0f, 30.0f, {160, 160, 160, 255});
    platform->timelineId = Engine::WorldTime;
    platform->velocityX = (net || peers) ? 0.0f : platformSpeed;

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

    auto showRemote = [&](const NetMessage &message)
    {
        if (message.tic < lastTic[message.clientId])
        {
            return;
        }
        lastTic[message.clientId] = message.tic;

        auto found = remotePlayers.find(message.clientId);
        if (found == remotePlayers.end())
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
    };

    auto applyServerSnapshot = [&]()
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
                showRemote(message);
            }
        }
    };

    auto applyPeerSnapshot = [&]()
    {
        {
            std::lock_guard<std::mutex> lock(platform->stateMutex.get());
            platform->x = peers->platformX();
            platform->y = peers->platformY();
            platform->velocityX = 0.0f;
            platform->velocityY = 0.0f;
        }

        for (const NetMessage &message : peers->getRemotePlayers())
        {
            if (message.clientId != peers->getBindPort())
            {
                showRemote(message);
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

    auto spawnRemotes = [&]()
    {
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

            if (net || peers)
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

            const int64_t now = globalTime.getTime();
            if (now - lastTitleUpdate >= 250'000'000)
            {
                lastTitleUpdate = now;
                char title[240];
                if (peers)
                {
                    std::snprintf(title, sizeof(title),
                                  "peer %u | anchor %lld | playerTime: %s x%.1f | frames P %llu W %llu",
                                  peers->getBindPort(),
                                  static_cast<long long>(peers->getAnchor()),
                                  playerTime.isPaused() ? "PAUSED" : "running",
                                  playerTime.getScale(),
                                  static_cast<unsigned long long>(engine.getPlayerFramesBuilt()),
                                  static_cast<unsigned long long>(engine.getWorldFramesBuilt()));
                }
                else
                {
                    std::snprintf(title, sizeof(title),
                                  "client %u | playerTime: %s x%.1f tic %lld | worldTime: tic %lld | frames P %llu W %llu",
                                  net ? net->getClientId() : 0u,
                                  playerTime.isPaused() ? "PAUSED" : "running",
                                  playerTime.getScale(),
                                  static_cast<long long>(playerTime.getTime()),
                                  static_cast<long long>(worldTime.getTime()),
                                  static_cast<unsigned long long>(engine.getPlayerFramesBuilt()),
                                  static_cast<unsigned long long>(engine.getWorldFramesBuilt()));
                }
                SDL_SetWindowTitle(engine.getWindow().getHandle(), title);
            }
        },
        [&](float)
        {
            if (peers)
            {
                applyPeerSnapshot();
            }
            else if (net)
            {
                applyServerSnapshot();
            }
            else
            {
                bouncePlatform();
            }
        });

    return 0;
}
