#pragma once

#include "NetMessage.h"
#include "NetProtocol.h"
#include "NetReplicator.h"
#include "PeerClock.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <zmq.h>

class PeerSession
{
public:
    explicit PeerSession(uint16_t bindPort, WireFormat format = WireFormat::Binary);
    ~PeerSession();
    PeerSession(const PeerSession &) = delete;
    PeerSession &operator=(const PeerSession &) = delete;

    void connectTo(const std::string &host, uint16_t port);

    void publish(const GameObjectManager &objects);
    std::size_t sync(GameObjectManager &objects, const NetReplicator::SpawnFn &onSpawn = nullptr,
                     const NetReplicator::DespawnFn &onDespawn = nullptr);
    NetReplicator &getReplicator() { return replicator; }
    uint32_t getOwnerId() const { return bindPort; }

    int64_t getAnchor() const;
    float platformX() const;
    float platformY() const;
    uint16_t getBindPort() const { return bindPort; }

    void sendPlayerState(const NetMessage &message);
    std::vector<NetMessage> getRemotePlayers() const;

private:
    void noteAnchor(int64_t tic);
    void acceptLoop();
    void servePeer(void *socket);
    void dialPeer(std::string host, uint16_t port);

    bool sendPacket(void *socket, const Packet &packet);
    bool receivePacket(void *socket, Packet &packet, bool &timedOut);
    Packet statePacket();
    void applyIncoming(const Packet &packet);
    void setSocketOptions(void *socket);
    uint16_t readBoundPort(void *socket) const;

    uint16_t bindPort;
    WireFormat format;
    std::atomic<int64_t> anchor;
    std::atomic<bool> running;

    void *context;
    void *acceptSocket;
    std::thread acceptThread;

    NetReplicator replicator;

    std::mutex threadMutex;
    std::vector<std::thread> peerThreads;
};
