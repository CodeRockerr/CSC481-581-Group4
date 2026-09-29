#pragma once

#include "NetMessage.h"
#include "PeerClock.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <zmq.h>

class PeerSession
{
public:
    explicit PeerSession(uint16_t bindPort);
    ~PeerSession();
    PeerSession(const PeerSession &) = delete;
    PeerSession &operator=(const PeerSession &) = delete;

    void connectTo(const std::string &host, uint16_t port);
    void sendPlayerState(const NetMessage &message);
    std::vector<NetMessage> getRemotePlayers() const;

    int64_t getAnchor() const;
    float platformX() const;
    float platformY() const;
    uint16_t getBindPort() const { return bindPort; }

private:
    void noteAnchor(int64_t tic);
    void acceptLoop();
    void servePeer(void *socket);
    void dialPeer(std::string host, uint16_t port);
    void rememberPlayer(const NetMessage &message);
    NetMessage currentPlayer() const;

    bool sendOne(void *socket, const NetMessage &message, int flags);
    bool sendPair(void *socket);
    bool recvAll(void *socket, std::vector<NetMessage> &messages, bool &timedOut);
    bool exchange(void *socket, bool replyFirst);
    void applyIncoming(const std::vector<NetMessage> &messages);
    void setSocketOptions(void *socket);
    uint16_t readBoundPort(void *socket) const;

    uint16_t bindPort;
    std::atomic<int64_t> anchor;
    std::atomic<bool> running;

    void *context;
    void *acceptSocket;
    std::thread acceptThread;

    mutable std::mutex stateMutex;
    NetMessage localPlayer{};
    bool hasLocalPlayer = false;
    std::unordered_map<uint32_t, NetMessage> remotePlayers;

    std::mutex threadMutex;
    std::vector<std::thread> peerThreads;
};
