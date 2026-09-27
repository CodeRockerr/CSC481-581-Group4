#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

#include <zmq.h>

#include "NetMessage.h"

class NetworkClient
{
public:
    NetworkClient(const char *serverAddress, uint16_t serverPort = 5555);
    ~NetworkClient();

    NetworkClient(const NetworkClient &) = delete;
    NetworkClient &operator=(const NetworkClient &) = delete;

    bool connect();
    void disconnect();

    void sendPlayerState(const NetMessage &message);

    std::vector<NetMessage> getLatestMessages();

    uint32_t getClientId() const;

private:
    void receiveLoop();

    const char *serverAddress;
    uint16_t serverPort;

    void *context;
    void *socket;

    std::atomic<bool> running;
    std::thread networkThread;

    std::mutex messageMutex;
    std::vector<NetMessage> latestMessages;

    std::mutex outgoingMutex;
    NetMessage outgoingPlayerState{};
    bool hasOutgoingPlayerState = false;

    uint32_t clientId = 0;
    uint16_t clientPort = 0;
};