#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <zmq.h>

#include "NetMessage.h"
#include "NetProtocol.h"
#include "NetReplicator.h"

class NetworkClient
{
public:
    NetworkClient(const char *serverAddress, uint16_t serverPort = 5555, WireFormat format = WireFormat::Binary);
    ~NetworkClient();

    NetworkClient(const NetworkClient &) = delete;
    NetworkClient &operator=(const NetworkClient &) = delete;

    bool connect();
    void disconnect();
    bool isConnected() const { return running.load(); }

    uint32_t getClientId() const;
    WireFormat getFormat() const { return format; }

    void publish(const GameObjectManager &objects);
    std::size_t sync(GameObjectManager &objects, const NetReplicator::SpawnFn &onSpawn = nullptr,
                     const NetReplicator::DespawnFn &onDespawn = nullptr);
    NetReplicator &getReplicator() { return replicator; }

    uint64_t waitForSnapshot(uint64_t seenSequence, int timeoutMs);
    uint64_t getSnapshotSequence() const { return snapshotSequence.load(); }

    void setSendIntervalMs(int ms) { sendIntervalMs = ms < 0 ? 0 : ms; }

    uint64_t getMessagesSent() const { return messagesSent.load(); }
    uint64_t getBytesSent() const { return bytesSent.load(); }
    uint64_t getBytesReceived() const { return bytesReceived.load(); }
    int64_t getLastRoundTripMicros() const { return lastRoundTripMicros.load(); }
    uint32_t getServerClientCount() const { return serverClientCount.load(); }

    void sendPlayerState(const NetMessage &message);
    std::vector<NetMessage> getLatestMessages();

private:
    void networkLoop();
    bool openGameSocket();
    bool exchange(const Packet &request, Packet &reply);
    void sendLeave();

    std::string serverAddress;
    uint16_t serverPort;
    WireFormat format;

    void *context;
    void *socket = nullptr;

    std::atomic<bool> running{false};
    std::atomic<bool> leaveRequested{false};
    std::atomic<int> sendIntervalMs{16};
    std::thread networkThread;

    NetReplicator replicator;

    std::mutex snapshotMutex;
    std::condition_variable snapshotArrived;
    std::atomic<uint64_t> snapshotSequence{0};
    std::atomic<int64_t> lastSnapshotTic{0};

    std::atomic<uint32_t> clientId{0};
    std::string clientEndpoint;

    std::atomic<uint64_t> messagesSent{0};
    std::atomic<uint64_t> bytesSent{0};
    std::atomic<uint64_t> bytesReceived{0};
    std::atomic<int64_t> lastRoundTripMicros{0};
    std::atomic<uint32_t> serverClientCount{0};

    std::string sendBuffer;
    std::string receiveBuffer;
};
