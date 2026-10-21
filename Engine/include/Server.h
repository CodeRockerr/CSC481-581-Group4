#pragma once

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
    using SceneUpdate = std::function<void(GameObjectManager &objects, float deltaSeconds, double elapsedSeconds)>;

    explicit Server(uint16_t port = 5555);
    ~Server();

    Server(const Server &) = delete;
    Server &operator=(const Server &) = delete;

    void setSceneUpdate(SceneUpdate update);
    void setTickRate(int hz);
    void setClientTimeoutMs(int ms);

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

    std::mutex worldMutex;
    GameObjectManager objects;
    SceneUpdate sceneUpdate;
    int tickRate = 60;
    int clientTimeoutMs = 3000;

    NetReplicator replicator;

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
