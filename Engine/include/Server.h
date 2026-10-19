#pragma once
// Milestone 3 server: authoritative owner of server-side objects (platforms, hazards, ...) and
// relay for client-owned objects (players). Uses the same GameObject model as every client.
//
// Threads:
//   - run() caller: accepts Join requests on `port` and hands each client a dedicated port.
//   - one thread per connected client: request/reply exchange on its dedicated REP socket.
//   - one simulation thread: runs the scene update at a fixed rate and refreshes the snapshot.
//
// Disconnects: a client is dropped when it sends Leave or when nothing arrives from it for
// `clientTimeoutMs`. Its objects are removed from the server and vanish from every other
// client's next snapshot, so their replicas are destroyed by NetReplicator::sync().

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <zmq.h>

#include "GameObjectManager.h"
#include "NetProtocol.h"
#include "NetReplicator.h"

class Server
{
public:
    // Runs on the simulation thread with the world locked. Move server-owned objects here.
    using SceneUpdate = std::function<void(GameObjectManager &objects, float deltaSeconds, double elapsedSeconds)>;

    explicit Server(uint16_t port = 5555);
    ~Server();

    Server(const Server &) = delete;
    Server &operator=(const Server &) = delete;

    // Configuration: call before run().
    void setSceneUpdate(SceneUpdate update);
    void setTickRate(int hz);
    void setClientTimeoutMs(int ms);

    // Creates a server-owned replicated object (owner 0). Call before run() or from inside the
    // scene update. Add Transform and any other components to the returned object.
    GameObject &createObject(uint16_t kind, const std::string &name = "");
    GameObjectManager &getObjects() { return objects; }

    void run();
    void stop();

    std::size_t getClientCount() const { return clientCount.load(); }
    uint64_t getBytesSent() const { return bytesSent.load(); }
    uint64_t getBytesReceived() const { return bytesReceived.load(); }

private:
    struct ClientThread
    {
        std::thread thread;
        std::shared_ptr<std::atomic<bool>> finished;
    };

    void simulationLoop();
    void clientLoop(uint32_t clientId, void *socket, std::shared_ptr<std::atomic<bool>> finished);
    void buildSnapshot(Packet &out);
    void reapFinishedClients();
    void installDefaultScene();

    uint16_t port;
    void *context;
    std::atomic<bool> running{false};

    // World: touched only by the simulation thread, or before run().
    std::mutex worldMutex;
    GameObjectManager objects;
    SceneUpdate sceneUpdate;
    int tickRate = 60;
    int clientTimeoutMs = 3000;

    // Client objects arriving from client threads.
    NetReplicator replicator;

    // Server-owned object states captured once per tick.
    std::mutex snapshotMutex;
    std::vector<ObjectState> serverStates;
    std::atomic<int64_t> tick{0};

    std::thread simulationThread;
    std::mutex threadMutex;
    std::vector<ClientThread> clientThreads;
    uint32_t nextClientId = 1;

    std::atomic<std::size_t> clientCount{0};
    std::atomic<uint64_t> bytesSent{0};
    std::atomic<uint64_t> bytesReceived{0};
};
