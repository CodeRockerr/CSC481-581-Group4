#include "PeerSession.h"

#include <chrono>
#include <cerrno>
#include <iostream>
#include <thread>
#include <stdexcept>

namespace
{
    constexpr int kTimeoutMs = 300;
}

PeerSession::PeerSession(uint16_t port)
    : bindPort(port), anchor(peerNowTic()), running(true), context(zmq_ctx_new()), acceptSocket(nullptr)
{
    if (context == nullptr)
    {
        throw std::runtime_error("Failed to create ZeroMQ context");
    }
    acceptSocket = zmq_socket(context, ZMQ_REP);
    if (acceptSocket == nullptr)
    {
        throw std::runtime_error("Failed to create peer scoket");
    }

    setSocketOptions(acceptSocket);
    const std::string endpoint = "tcp://*:" + std::to_string(bindPort);
    if (zmq_bind(acceptSocket, endpoint.c_str()) != 0)
    {
        throw std::runtime_error("Failed to bind peer port");
    }

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
    if (port <= bindPort)
    {
        return;
    }
    std::lock_guard<std::mutex> lock(threadMutex);
    peerThreads.emplace_back(&PeerSession::dialPeer, this, host, port);
}

void PeerSession::sendPlayerState(const NetMessage &message)
{
    std::lock_guard<std::mutex> lock(stateMutex);
    localPlayer = message;
    localPlayer.type = NetMessageType::PlayerState;
    localPlayer.clientId = bindPort;
    hasLocalPlayer = true;
}

std::vector<NetMessage> PeerSession::getRemotePlayers() const
{
    std::lock_guard<std::mutex> lock(stateMutex);
    std::vector<NetMessage> players;
    players.reserve(remotePlayers.size());
    for (const auto &entry : remotePlayers)
    {
        players.push_back(entry.second);
    }
    return players;
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

void PeerSession::rememberPlayer(const NetMessage &message)
{
    if (message.clientId == 0)
    {
        return;
    }
    std::lock_guard<std::mutex> lock(stateMutex);
    const auto found = remotePlayers.find(message.clientId);
    if (found != remotePlayers.end() && message.tic < found->second.tic)
    {
        return;
    }
    remotePlayers[message.clientId] = message;
}

void PeerSession::forgetPlayer(uint32_t clientId)
{
    if (clientId == 0)
    {
        return;
    }
    std::lock_guard<std::mutex> lock(stateMutex);
    remotePlayers.erase(clientId);
}

NetMessage PeerSession::currentPlayer() const
{
    std::lock_guard<std::mutex> lock(stateMutex);
    if (!hasLocalPlayer)
    {
        NetMessage empty{};
        empty.type = NetMessageType::PlayerState;
        empty.clientId = 0;
        return empty;
    }
    return localPlayer;
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

bool PeerSession::sendOne(void *socket, const NetMessage &message, int flags)
{
    return zmq_send(socket, &message, sizeof(message), flags) != -1;
}

bool PeerSession::sendPair(void *socket)
{
    NetMessage anchorMessage{};
    anchorMessage.type = NetMessageType::PlatformState;
    anchorMessage.clientId = bindPort;
    anchorMessage.tic = anchor.load();
    return sendOne(socket, anchorMessage, ZMQ_SNDMORE) && sendOne(socket, currentPlayer(), 0);
}

bool PeerSession::recvAll(void *socket, std::vector<NetMessage> &messages, bool &timedOut)
{
    messages.clear();
    timedOut = false;
    while (true)
    {
        NetMessage message{};
        const int received = zmq_recv(socket, &message, sizeof(NetMessage), 0);
        if (received == -1)
        {
            timedOut = messages.empty() && errno == EAGAIN;
            return false;
        }
        if (received != sizeof(NetMessage))
        {
            return false;
        }
        messages.push_back(message);
        int more = 0;
        size_t moreSize = sizeof(more);
        zmq_getsockopt(socket, ZMQ_RCVMORE, &more, &moreSize);
        if (!more)
        {
            return true;
        }
    }
}

void PeerSession::applyIncoming(const std::vector<NetMessage> &messages)
{
    for (const NetMessage &message : messages)
    {
        if (message.type == NetMessageType::PlatformState)
        {
            noteAnchor(message.tic);
        }
        else if (message.type == NetMessageType::PlayerState)
        {
            rememberPlayer(message);
        }
    }
}

bool PeerSession::exchange(void *socket, bool replyFirst)
{
    bool timedOut = false;
    std::vector<NetMessage> incoming;

    if (replyFirst)
    {
        if (!recvAll(socket, incoming, timedOut))
        {
            return false;
        }
        applyIncoming(incoming);
        return sendPair(socket);
    }

    if (!sendPair(socket))
    {
        return false;
    }
    if (!recvAll(socket, incoming, timedOut))
    {
        return false;
    }
    applyIncoming(incoming);
    return true;
}

void PeerSession::servePeer(void *socket)
{
    setSocketOptions(socket);
    uint32_t remoteId = 0;
    while (running)
    {
        bool timedOut = false;
        std::vector<NetMessage> incoming;
        if (!recvAll(socket, incoming, timedOut))
        {
            if (timedOut)
            {
                continue;
            }
            NetMessage error{};
            error.type = NetMessageType::Leave;
            sendOne(socket, error, 0);
            break;
        }
        for (const NetMessage &message : incoming)
        {
            if (message.type == NetMessageType::PlayerState && message.clientId != 0)
            {
                remoteId = message.clientId;
            }
        }
        if (!incoming.empty() && incoming.front().type == NetMessageType::Leave)
        {
            NetMessage reply{};
            reply.type = NetMessageType::Leave;
            sendOne(socket, reply, 0);
            break;
        }
        applyIncoming(incoming);
        if (!sendPair(socket))
        {
            break;
        }
    }
    forgetPlayer(remoteId);
    zmq_close(socket);
}

void PeerSession::acceptLoop()
{
    while (running)
    {
        NetMessage request{};
        const int received = zmq_recv(acceptSocket, &request, sizeof(NetMessage), 0);
        if (received == -1)
        {
            continue;
        }

        auto reject = [&]()
        {
            NetMessage reply{};
            reply.type = NetMessageType::Leave;
            sendOne(acceptSocket, reply, 0);
        };
        if (received != sizeof(NetMessage) || request.type != NetMessageType::Join)
        {
            reject();
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
            reject();
            continue;
        }

        NetMessage reply{};
        reply.type = NetMessageType::Join;
        reply.clientId = bindPort;
        reply.tic = anchor.load();
        reply.x = static_cast<float>(readBoundPort(dedicated));
        sendOne(acceptSocket, reply, 0);

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
        NetMessage join{};
        join.type = NetMessageType::Join;
        join.clientId = bindPort;
        join.tic = anchor.load();

        NetMessage reply{};
        const bool sent = sendOne(joinSocket, join, 0);
        const int received = sent ? zmq_recv(joinSocket, &reply, sizeof(NetMessage), 0) : -1;
        zmq_close(joinSocket);
        if (received != sizeof(NetMessage) || reply.type != NetMessageType::Join)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            continue;
        }

        noteAnchor(reply.tic);
        const std::string gameEndpoint = "tcp://" + host + ":" + std::to_string(static_cast<uint16_t>(reply.x));

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

        while (running && exchange(socket, false))
        {
        }

        forgetPlayer(port);
        zmq_close(socket);
        if (running)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    }
}
