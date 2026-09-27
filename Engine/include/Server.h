#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

#include <zmq.h>

#include "NetMessage.h"

class Server
{
public:
    explicit Server(uint16_t port = 5555);
    ~Server();

    Server(const Server &) = delete;
    Server &operator=(const Server &) = delete;

    void run();
    void stop();

private:
    struct PlayerState
    {
        NetMessage message{};
    };

    void clientLoop(uint32_t clientId, uint16_t clientPort);

    void updatePlayer(const NetMessage &message);

    std::vector<NetMessage> buildSnapshot();

    NetMessage createPlatformState() const;

    uint16_t port;

    void *context;

    std::atomic<bool> running;

    std::mutex stateMutex;

    std::unordered_map<uint32_t, PlayerState> players;

    uint32_t nextClientId = 1;

    std::mutex threadMutex;

    std::vector<std::thread> clientThreads;
};