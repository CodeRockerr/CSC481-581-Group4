#include "PeerSession.h"

#include <cerrno>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace
{
    constexpr int kTimeoutMs = 300;
}

PeerSession::PeerSession(uint16_t port, WireFormat wireFormat)
    : bindPort(port), format(wireFormat), anchor(peerNowTic()), running(true), context(zmq_ctx_new()),
      acceptSocket(nullptr)
{
    if (context == nullptr)
    {
        throw std::runtime_error("Failed to create ZeroMQ context");
    }
    acceptSocket = zmq_socket(context, ZMQ_REP);
    if (acceptSocket == nullptr)
    {
        throw std::runtime_error("Failed to create peer socket");
    }

    setSocketOptions(acceptSocket);
    const std::string endpoint = "tcp://*:" + std::to_string(bindPort);
    if (zmq_bind(acceptSocket, endpoint.c_str()) != 0)
    {
        throw std::runtime_error("Failed to bind peer port");
    }

    replicator.setLocalOwner(bindPort);
    acceptThread = std::thread(&PeerSession::acceptLoop, this);
}

PeerSession::~PeerSession()
{
    running = false;
    if (acceptThread.joinable())
    {
        acceptThread.join();
    }

    std::vector<std::thread> threads;
    {
        std::lock_guard<std::mutex> lock(threadMutex);
        threads.swap(peerThreads);
    }
    for (std::thread &thread : threads)
    {
        if (thread.joinable())
        {
            thread.join();
        }
    }

    if (acceptSocket != nullptr)
    {
        zmq_close(acceptSocket);
        acceptSocket = nullptr;
    }
    if (context != nullptr)
    {
        zmq_ctx_destroy(context);
        context = nullptr;
    }
}

void PeerSession::connectTo(const std::string &host, uint16_t port)
{
    // Only the lower port dials, so each pair has exactly one connection.
    if (port <= bindPort)
    {
        return;
    }
    std::lock_guard<std::mutex> lock(threadMutex);
    peerThreads.emplace_back(&PeerSession::dialPeer, this, host, port);
}

void PeerSession::publish(const GameObjectManager &objects)
{
    replicator.publish(objects);
}

std::size_t PeerSession::sync(GameObjectManager &objects, const NetReplicator::SpawnFn &onSpawn,
                              const NetReplicator::DespawnFn &onDespawn)
{
    return replicator.sync(objects, onSpawn, onDespawn);
}

int64_t PeerSession::getAnchor() const
{
    return anchor.load();
}

float PeerSession::platformX() const
{
    return peerPlatformX(peerNowTic(), anchor.load());
}

float PeerSession::platformY() const
{
    return peerPlatformY();
}

void PeerSession::noteAnchor(int64_t tic)
{
    int64_t current = anchor.load();
    while (tic < current && !anchor.compare_exchange_weak(current, tic))
    {
    }
}

void PeerSession::setSocketOptions(void *socket)
{
    const int timeout = kTimeoutMs;
    const int linger = 0;
    zmq_setsockopt(socket, ZMQ_RCVTIMEO, &timeout, sizeof(timeout));
    zmq_setsockopt(socket, ZMQ_SNDTIMEO, &timeout, sizeof(timeout));
    zmq_setsockopt(socket, ZMQ_LINGER, &linger, sizeof(linger));
}

uint16_t PeerSession::readBoundPort(void *socket) const
{
    char endpoint[256] = {};
    size_t size = sizeof(endpoint);
    zmq_getsockopt(socket, ZMQ_LAST_ENDPOINT, endpoint, &size);
    const std::string text(endpoint);
    const auto colon = text.rfind(':');
    return static_cast<uint16_t>(std::stoi(text.substr(colon + 1)));
}

bool PeerSession::sendPacket(void *socket, const Packet &packet)
{
    const std::string bytes = NetProtocol::encode(format, packet);
    return zmq_send(socket, bytes.data(), bytes.size(), 0) != -1;
}

bool PeerSession::receivePacket(void *socket, Packet &packet, bool &timedOut)
{
    timedOut = false;
    zmq_msg_t message;
    zmq_msg_init(&message);
    const int received = zmq_msg_recv(&message, socket, 0);
    if (received == -1)
    {
        timedOut = errno == EAGAIN;
        zmq_msg_close(&message);
        return false;
    }
    const bool ok = NetProtocol::decode(zmq_msg_data(&message), zmq_msg_size(&message), packet);
    zmq_msg_close(&message);
    return ok;
}

Packet PeerSession::statePacket()
{
    Packet packet;
    packet.type = PacketType::State;
    packet.sender = bindPort;
    packet.tic = anchor.load();
    packet.objects = replicator.takeOutgoing();
    return packet;
}

void PeerSession::applyIncoming(const Packet &packet)
{
    if (packet.type != PacketType::State || packet.sender == 0)
    {
        return;
    }
    noteAnchor(packet.tic);
    replicator.replaceOwner(packet.sender, packet.objects);
}

void PeerSession::servePeer(void *socket)
{
    setSocketOptions(socket);
    uint32_t remoteId = 0;
    while (running)
    {
        bool timedOut = false;
        Packet incoming;
        if (!receivePacket(socket, incoming, timedOut))
        {
            if (timedOut)
            {
                continue;
            }
            Packet error;
            error.type = PacketType::Leave;
            sendPacket(socket, error);
            break;
        }
        if (incoming.type == PacketType::Leave)
        {
            Packet reply;
            reply.type = PacketType::Leave;
            sendPacket(socket, reply);
            break;
        }
        if (incoming.sender != 0)
        {
            remoteId = incoming.sender;
        }
        applyIncoming(incoming);
        if (!sendPacket(socket, statePacket()))
        {
            break;
        }
    }
    if (remoteId != 0)
    {
        replicator.forgetOwner(remoteId);
    }
    zmq_close(socket);
}

void PeerSession::acceptLoop()
{
    while (running)
    {
        bool timedOut = false;
        Packet request;
        if (!receivePacket(acceptSocket, request, timedOut))
        {
            if (timedOut)
            {
                continue;
            }
            Packet reject;
            reject.type = PacketType::Leave;
            sendPacket(acceptSocket, reject);
            continue;
        }
        if (request.type != PacketType::Join)
        {
            Packet reject;
            reject.type = PacketType::Leave;
            sendPacket(acceptSocket, reject);
            continue;
        }

        noteAnchor(request.tic);
        void *dedicated = zmq_socket(context, ZMQ_REP);
        if (dedicated == nullptr || zmq_bind(dedicated, "tcp://*:0") != 0)
        {
            if (dedicated != nullptr)
            {
                zmq_close(dedicated);
            }
            Packet reject;
            reject.type = PacketType::Leave;
            sendPacket(acceptSocket, reject);
            continue;
        }

        Packet reply;
        reply.type = PacketType::JoinAccept;
        reply.sender = bindPort;
        reply.tic = anchor.load();
        reply.aux = readBoundPort(dedicated);
        sendPacket(acceptSocket, reply);

        std::lock_guard<std::mutex> lock(threadMutex);
        peerThreads.emplace_back(&PeerSession::servePeer, this, dedicated);
    }
}

void PeerSession::dialPeer(std::string host, uint16_t port)
{
    const std::string joinEndpoint = "tcp://" + host + ":" + std::to_string(port);
    while (running)
    {
        void *joinSocket = zmq_socket(context, ZMQ_REQ);
        if (joinSocket == nullptr)
        {
            return;
        }
        setSocketOptions(joinSocket);
        if (zmq_connect(joinSocket, joinEndpoint.c_str()) != 0)
        {
            zmq_close(joinSocket);
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            continue;
        }

        Packet join;
        join.type = PacketType::Join;
        join.sender = bindPort;
        join.tic = anchor.load();

        Packet reply;
        bool timedOut = false;
        const bool ok = sendPacket(joinSocket, join) && receivePacket(joinSocket, reply, timedOut);
        zmq_close(joinSocket);
        if (!ok || reply.type != PacketType::JoinAccept)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            continue;
        }

        noteAnchor(reply.tic);
        const std::string gameEndpoint = "tcp://" + host + ":" + std::to_string(reply.aux);
        void *socket = zmq_socket(context, ZMQ_REQ);
        if (socket == nullptr)
        {
            return;
        }
        setSocketOptions(socket);
        if (zmq_connect(socket, gameEndpoint.c_str()) != 0)
        {
            zmq_close(socket);
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            continue;
        }

        while (running)
        {
            Packet incoming;
            if (!sendPacket(socket, statePacket()) || !receivePacket(socket, incoming, timedOut))
            {
                break;
            }
            applyIncoming(incoming);
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        replicator.forgetOwner(port);
        zmq_close(socket);
        if (running)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    }
}

// ---------------------------------------------------------------------------------------------
// Legacy NetMessage view (Milestone 2 API). Remove once all games use publish()/sync().

void PeerSession::sendPlayerState(const NetMessage &message)
{
    ObjectState state;
    state.netId = 1;
    state.kind = NetKind::Player;
    state.x = message.x;
    state.y = message.y;
    state.velocityX = message.velocityX;
    state.velocityY = message.velocityY;
    replicator.setOutgoing({state});
}

std::vector<NetMessage> PeerSession::getRemotePlayers() const
{
    std::vector<NetMessage> players;
    for (const ObjectState &state : replicator.remoteStates())
    {
        if (state.kind != NetKind::Player)
        {
            continue;
        }
        NetMessage message{};
        message.type = NetMessageType::PlayerState;
        message.clientId = state.owner;
        message.tic = peerNowTic();
        message.x = state.x;
        message.y = state.y;
        message.velocityX = state.velocityX;
        message.velocityY = state.velocityY;
        players.push_back(message);
    }
    return players;
}
