#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
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
    uint64_t getMessagesSent() const { return messagesSent.load(); }

private:
    void receiveLoop();
    bool reconnect();
    bool readSnapshot(std::vector<NetMessage> &snapshot);
    void sendLeave();

    const char *serverAddress;
    uint16_t serverPort;

    void *context;
    void *socket;

    std::atomic<bool> running;
    std::atomic<bool> leaveRequested;
    std::thread networkThread;

    std::mutex messageMutex;
    std::vector<NetMessage> latestMessages;

    std::mutex outgoingMutex;
    NetMessage outgoingPlayerState{};
    bool hasOutgoingPlayerState = false;

    uint32_t clientId = 0;
    std::string clientEndpoint;
    std::atomic<uint64_t> messagesSent{0};
};
