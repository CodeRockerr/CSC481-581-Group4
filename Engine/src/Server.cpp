#include "Server.h"

#include <cerrno>
#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

#include "Components.h"
#include "NetworkIdentity.h"

namespace
{
    constexpr uint16_t CLIENT_PORT_BASE = 6000;
    constexpr int ACCEPT_POLL_MS = 200;
    constexpr int CLIENT_POLL_MS = 250;

    using Clock = std::chrono::steady_clock;

    // Receives one whole frame of any size. Returns -1 on error/timeout (errno set by 0MQ).
    int receiveFrame(void *socket, std::string &out)
    {
        zmq_msg_t message;
        zmq_msg_init(&message);
        const int received = zmq_msg_recv(&message, socket, 0);
        if (received >= 0)
        {
            out.assign(static_cast<const char *>(zmq_msg_data(&message)), zmq_msg_size(&message));
        }
        zmq_msg_close(&message);
        return received;
    }

    bool sendPacket(void *socket, WireFormat format, const Packet &packet, std::string &buffer,
                    std::atomic<uint64_t> *counter = nullptr)
    {
        NetProtocol::encode(format, packet, buffer);
        if (zmq_send(socket, buffer.data(), buffer.size(), 0) == -1)
        {
            return false;
        }
        if (counter != nullptr)
        {
            counter->fetch_add(buffer.size());
        }
        return true;
    }

    WireFormat formatOf(const std::string &frame)
    {
        return (!frame.empty() && frame[0] == static_cast<char>(WireFormat::Text)) ? WireFormat::Text
                                                                                    : WireFormat::Binary;
    }

    Packet leavePacket(uint32_t sender)
    {
        Packet packet;
        packet.type = PacketType::Leave;
        packet.sender = sender;
        return packet;
    }
}

Server::Server(uint16_t serverPort)
    : port(serverPort), context(zmq_ctx_new())
{
    if (context == nullptr)
    {
        throw std::runtime_error("Failed to create ZeroMQ context");
    }
    replicator.setLocalOwner(0);
}

Server::~Server()
{
    stop();
    if (context != nullptr)
    {
        zmq_ctx_destroy(context);
        context = nullptr;
    }
}

void Server::setSceneUpdate(SceneUpdate update)
{
    std::lock_guard<std::mutex> lock(worldMutex);
    sceneUpdate = std::move(update);
}

void Server::setTickRate(int hz)
{
    tickRate = hz > 0 ? hz : 60;
}

void Server::setClientTimeoutMs(int ms)
{
    clientTimeoutMs = ms > 0 ? ms : 3000;
}

GameObject &Server::createObject(uint16_t kind, const std::string &name)
{
    GameObject &object = objects.create(name);
    object.add<NetworkIdentity>(0u, kind);
    return object;
}

void Server::installDefaultScene()
{
    // Milestone 2 behaviour, kept so games that still use the legacy NetMessage view work:
    // one platform moving horizontally around x = 520 with amplitude 180 at y = 520.
    GameObject &platform = createObject(NetKind::Platform, "legacy-platform");
    platform.add<Transform>(520.0f, 520.0f, 0.0f, 0.0f);
    platform.add<PhysicsBody>(0, false);
    const uint32_t id = platform.getId();
    sceneUpdate = [id](GameObjectManager &world, float, double seconds)
    {
        GameObject *object = world.find(id);
        if (object == nullptr)
        {
            return;
        }
        Transform *t = object->get<Transform>();
        PhysicsBody *body = object->get<PhysicsBody>();
        t->x = 520.0f + 180.0f * static_cast<float>(std::sin(seconds));
        t->y = 520.0f;
        body->velocityX = 180.0f * static_cast<float>(std::cos(seconds));
    };
}

void Server::simulationLoop()
{
    const auto period = std::chrono::nanoseconds(1'000'000'000LL / tickRate);
    const Clock::time_point start = Clock::now();
    Clock::time_point last = start;
    Clock::time_point next = start;

    while (running)
    {
        const Clock::time_point now = Clock::now();
        const float delta = std::chrono::duration<float>(now - last).count();
        const double elapsed = std::chrono::duration<double>(now - start).count();
        last = now;

        std::vector<ObjectState> captured;
        {
            std::lock_guard<std::mutex> lock(worldMutex);
            if (sceneUpdate)
            {
                sceneUpdate(objects, delta, elapsed);
            }
            // Mirror client-owned objects into the server's own object model so server-side
            // logic (collisions, triggers) can see players.
            replicator.sync(objects);
            objects.flushDestroyed();

            captured.reserve(objects.size());
            for (const auto &object : objects.getAll())
            {
                const NetworkIdentity *identity = object->get<NetworkIdentity>();
                if (identity == nullptr || identity->isReplica || identity->ownerId != 0)
                {
                    continue;
                }
                ObjectState state;
                if (NetReplicator::capture(*object, state))
                {
                    captured.push_back(state);
                }
            }
        }
        {
            std::lock_guard<std::mutex> lock(snapshotMutex);
            serverStates.swap(captured);
        }
        tick.fetch_add(1);

        next += period;
        const Clock::time_point after = Clock::now();
        if (next > after)
        {
            std::this_thread::sleep_for(next - after);
        }
        else
        {
            next = after; // fell behind; do not try to catch up with a burst
        }
    }
}

void Server::buildSnapshot(Packet &out)
{
    out.type = PacketType::Snapshot;
    out.sender = 0;
    out.tic = tick.load();
    out.aux = static_cast<uint32_t>(clientCount.load());
    {
        std::lock_guard<std::mutex> lock(snapshotMutex);
        out.objects = serverStates;
    }
    // Client objects come straight from the replicator so a client's newest state is relayed
    // on the very next request instead of waiting for a simulation tick.
    std::vector<ObjectState> clients = replicator.remoteStates();
    out.objects.insert(out.objects.end(), clients.begin(), clients.end());
}

void Server::clientLoop(uint32_t clientId, void *socket, std::shared_ptr<std::atomic<bool>> finished)
{
    const int linger = 0;
    const int receiveTimeout = CLIENT_POLL_MS;
    zmq_setsockopt(socket, ZMQ_LINGER, &linger, sizeof(linger));
    zmq_setsockopt(socket, ZMQ_RCVTIMEO, &receiveTimeout, sizeof(receiveTimeout));

    clientCount.fetch_add(1);
    std::cout << "Client " << clientId << " connected (" << clientCount.load() << " connected)\n";

    std::string frame;
    std::string buffer;
    Packet incoming;
    Packet snapshot;
    Clock::time_point lastHeard = Clock::now();
    const char *reason = "left";

    while (running)
    {
        if (receiveFrame(socket, frame) == -1)
        {
            if (errno == EAGAIN)
            {
                const auto silent = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - lastHeard);
                if (silent.count() > clientTimeoutMs)
                {
                    reason = "timed out";
                    break;
                }
                continue;
            }
            reason = "socket error";
            break;
        }
        lastHeard = Clock::now();
        bytesReceived.fetch_add(frame.size());
        const WireFormat format = formatOf(frame);

        if (!NetProtocol::decode(frame.data(), frame.size(), incoming))
        {
            sendPacket(socket, format, leavePacket(clientId), buffer);
            reason = "sent a malformed packet";
            break;
        }

        if (incoming.type == PacketType::Leave)
        {
            replicator.forgetOwner(clientId);
            sendPacket(socket, format, leavePacket(clientId), buffer);
            break;
        }

        if (incoming.type == PacketType::State)
        {
            // Force ownership: a client can only update its own objects.
            for (ObjectState &state : incoming.objects)
            {
                state.owner = clientId;
            }
            replicator.replaceOwner(clientId, incoming.objects);
        }

        buildSnapshot(snapshot);
        if (!sendPacket(socket, format, snapshot, buffer, &bytesSent))
        {
            reason = "send failed";
            break;
        }
    }

    replicator.forgetOwner(clientId);
    zmq_close(socket);
    clientCount.fetch_sub(1);
    std::cout << "Client " << clientId << " " << reason << " (" << clientCount.load() << " connected)\n";
    finished->store(true);
}

void Server::reapFinishedClients()
{
    std::lock_guard<std::mutex> lock(threadMutex);
    for (auto it = clientThreads.begin(); it != clientThreads.end();)
    {
        if (it->finished->load())
        {
            if (it->thread.joinable())
            {
                it->thread.join();
            }
            it = clientThreads.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

void Server::run()
{
    void *socket = zmq_socket(context, ZMQ_REP);
    if (socket == nullptr)
    {
        throw std::runtime_error("Failed to create server socket");
    }
    const int linger = 0;
    const int receiveTimeout = ACCEPT_POLL_MS;
    zmq_setsockopt(socket, ZMQ_LINGER, &linger, sizeof(linger));
    zmq_setsockopt(socket, ZMQ_RCVTIMEO, &receiveTimeout, sizeof(receiveTimeout));

    const std::string endpoint = "tcp://*:" + std::to_string(port);
    if (zmq_bind(socket, endpoint.c_str()) != 0)
    {
        zmq_close(socket);
        throw std::runtime_error("Failed to bind server socket on " + endpoint);
    }

    {
        std::lock_guard<std::mutex> lock(worldMutex);
        if (!sceneUpdate && objects.size() == 0)
        {
            installDefaultScene();
        }
    }

    running = true;
    simulationThread = std::thread(&Server::simulationLoop, this);
    std::cout << "Server listening on port " << port << '\n';

    std::string frame;
    std::string buffer;
    Packet request;

    while (running)
    {
        if (receiveFrame(socket, frame) == -1)
        {
            if (errno == EAGAIN)
            {
                reapFinishedClients();
                continue;
            }
            break;
        }

        const WireFormat format = formatOf(frame);
        if (!NetProtocol::decode(frame.data(), frame.size(), request) || request.type != PacketType::Join)
        {
            std::cerr << "Expected Join packet\n";
            sendPacket(socket, format, leavePacket(0), buffer);
            continue;
        }

        reapFinishedClients();

        const uint32_t clientId = nextClientId++;
        const uint16_t clientPort = static_cast<uint16_t>(CLIENT_PORT_BASE + clientId);
        void *clientSocket = zmq_socket(context, ZMQ_REP);
        const std::string clientEndpoint = "tcp://*:" + std::to_string(clientPort);

        if (clientSocket == nullptr || zmq_bind(clientSocket, clientEndpoint.c_str()) != 0)
        {
            std::cerr << "Failed to bind client " << clientId << " on " << clientEndpoint << '\n';
            if (clientSocket != nullptr)
            {
                zmq_close(clientSocket);
            }
            sendPacket(socket, format, leavePacket(0), buffer);
            continue;
        }

        Packet accept;
        accept.type = PacketType::JoinAccept;
        accept.sender = clientId;
        accept.aux = clientPort;
        accept.tic = tick.load();
        sendPacket(socket, format, accept, buffer);

        auto finished = std::make_shared<std::atomic<bool>>(false);
        std::lock_guard<std::mutex> lock(threadMutex);
        clientThreads.push_back({std::thread(&Server::clientLoop, this, clientId, clientSocket, finished), finished});
    }

    zmq_close(socket);
}

void Server::stop()
{
    running = false;
    if (simulationThread.joinable())
    {
        simulationThread.join();
    }

    std::vector<ClientThread> threads;
    {
        std::lock_guard<std::mutex> lock(threadMutex);
        threads.swap(clientThreads);
    }
    for (ClientThread &client : threads)
    {
        if (client.thread.joinable())
        {
            client.thread.join();
        }
    }
}
