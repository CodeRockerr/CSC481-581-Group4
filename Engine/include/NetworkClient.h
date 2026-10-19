#pragma once
// Client side of the Milestone 3 client-server model.
//
// Game usage (main thread, inside the game update callback):
//     net.publish(engine.getObjects());               // send objects this client owns
//     net.sync(engine.getObjects(), onSpawn);         // create/update/destroy replicas
// Mark an object as owned and replicated with:
//     object.add<NetworkIdentity>(net.getClientId(), MyKinds::Ninja);
//
// A background thread does the 0MQ request/reply exchange so the game loop never blocks.
// The wire format (binary or text) is chosen per client and only changes the representation.

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

    // ---- object model API (Milestone 3) ----
    void publish(const GameObjectManager &objects);
    std::size_t sync(GameObjectManager &objects, const NetReplicator::SpawnFn &onSpawn = nullptr,
                     const NetReplicator::DespawnFn &onDespawn = nullptr);
    NetReplicator &getReplicator() { return replicator; }

    // Blocks until a snapshot newer than `seenSequence` arrives (or timeout). Returns the new
    // sequence number, or `seenSequence` on timeout. Used by headless benchmarks.
    uint64_t waitForSnapshot(uint64_t seenSequence, int timeoutMs);
    uint64_t getSnapshotSequence() const { return snapshotSequence.load(); }

    // Minimum time between requests. Default 16 ms (one per frame). 0 = back-to-back, used by
    // the benchmark to measure raw exchange cost.
    void setSendIntervalMs(int ms) { sendIntervalMs = ms < 0 ? 0 : ms; }

    // ---- statistics for the performance experiments ----
    uint64_t getMessagesSent() const { return messagesSent.load(); }
    uint64_t getBytesSent() const { return bytesSent.load(); }
    uint64_t getBytesReceived() const { return bytesReceived.load(); }
    // Round trip of the most recent request/reply, in microseconds.
    int64_t getLastRoundTripMicros() const { return lastRoundTripMicros.load(); }
    // Number of clients the server reported in its last snapshot.
    uint32_t getServerClientCount() const { return serverClientCount.load(); }

    // ---- legacy Milestone 2 view (deprecated) ----
    // Kept so games that have not moved to GameObjects still compile and play.
    // sendPlayerState publishes one Player object; getLatestMessages converts the newest snapshot
    // back into PlayerState / PlatformState messages.
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
