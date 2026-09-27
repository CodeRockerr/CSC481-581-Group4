#include "NetworkClient.h"

#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace
{
    void setSocketOptions(void *socket)
    {
        const int receiveTimeout = 100;
        const int sendTimeout = 1000;
        const int linger = 0;
        zmq_setsockopt(socket, ZMQ_RCVTIMEO, &receiveTimeout, sizeof(receiveTimeout));
        zmq_setsockopt(socket, ZMQ_SNDTIMEO, &sendTimeout, sizeof(sendTimeout));
        zmq_setsockopt(socket, ZMQ_LINGER, &linger, sizeof(linger));
    }
}

NetworkClient::NetworkClient(const char *address, uint16_t port)
    : serverAddress(address),
      serverPort(port),
      context(zmq_ctx_new()),
      socket(nullptr),
      running(false),
      leaveRequested(false)
{
    if (context == nullptr)
    {
        throw std::runtime_error("Failed to create ZeroMQ context");
    }
}

NetworkClient::~NetworkClient()
{
    disconnect();

    if (context != nullptr)
    {
        zmq_ctx_destroy(context);
        context = nullptr;
    }
}

bool NetworkClient::connect()
{
    if (running)
    {
        return true;
    }

    void *joinSocket = zmq_socket(context, ZMQ_REQ);
    if (joinSocket == nullptr)
    {
        std::cerr << "Failed to create join socket\n";
        return false;
    }

    setSocketOptions(joinSocket);

    const std::string serverEndpoint =
        "tcp://" + std::string(serverAddress) + ":" + std::to_string(serverPort);

    if (zmq_connect(joinSocket, serverEndpoint.c_str()) != 0)
    {
        std::cerr << "Failed to connect to server\n";
        zmq_close(joinSocket);
        return false;
    }

    NetMessage join{};
    join.type = NetMessageType::Join;

    if (zmq_send(joinSocket, &join, sizeof(NetMessage), 0) == -1)
    {
        std::cerr << "Failed to send Join message\n";
        zmq_close(joinSocket);
        return false;
    }

    NetMessage response{};
    const int received = zmq_recv(joinSocket, &response, sizeof(NetMessage), 0);
    zmq_close(joinSocket);

    if (received != sizeof(NetMessage) || response.type != NetMessageType::Join)
    {
        std::cerr << "Invalid Join response\n";
        return false;
    }

    clientId = response.clientId;
    clientEndpoint = "tcp://" + std::string(serverAddress) + ":" + std::to_string(response.tic);

    if (!reconnect())
    {
        std::cerr << "Failed to connect to client port\n";
        return false;
    }

    leaveRequested = false;
    running = true;
    networkThread = std::thread(&NetworkClient::receiveLoop, this);

    std::cout << "Connected to server as client " << clientId << std::endl;
    return true;
}

void NetworkClient::disconnect()
{
    if (!running)
    {
        return;
    }

    leaveRequested = true;

    if (networkThread.joinable())
    {
        networkThread.join();
    }

    running = false;

    if (socket != nullptr)
    {
        zmq_close(socket);
        socket = nullptr;
    }
}

void NetworkClient::sendPlayerState(const NetMessage &message)
{
    if (!running)
    {
        return;
    }

    NetMessage playerState = message;
    playerState.type = NetMessageType::PlayerState;
    playerState.clientId = clientId;

    std::lock_guard<std::mutex> lock(outgoingMutex);
    outgoingPlayerState = playerState;
    hasOutgoingPlayerState = true;
}

std::vector<NetMessage> NetworkClient::getLatestMessages()
{
    std::lock_guard<std::mutex> lock(messageMutex);
    return latestMessages;
}

uint32_t NetworkClient::getClientId() const
{
    return clientId;
}

bool NetworkClient::reconnect()
{
    if (socket != nullptr)
    {
        zmq_close(socket);
        socket = nullptr;
    }

    socket = zmq_socket(context, ZMQ_REQ);
    if (socket == nullptr)
    {
        return false;
    }

    setSocketOptions(socket);
    if (zmq_connect(socket, clientEndpoint.c_str()) != 0)
    {
        zmq_close(socket);
        socket = nullptr;
        return false;
    }

    return true;
}

bool NetworkClient::readSnapshot(std::vector<NetMessage> &snapshot)
{
    while (true)
    {
        NetMessage message{};
        const int received = zmq_recv(socket, &message, sizeof(NetMessage), 0);

        if (received != sizeof(NetMessage))
        {
            return false;
        }

        snapshot.push_back(message);

        int more = 0;
        size_t moreSize = sizeof(more);
        zmq_getsockopt(socket, ZMQ_RCVMORE, &more, &moreSize);

        if (!more)
        {
            return true;
        }
    }
}

void NetworkClient::sendLeave()
{
    NetMessage leave{};
    leave.type = NetMessageType::Leave;
    leave.clientId = clientId;

    if (zmq_send(socket, &leave, sizeof(NetMessage), 0) == -1)
    {
        if (!reconnect() || zmq_send(socket, &leave, sizeof(NetMessage), 0) == -1)
        {
            return;
        }
    }

    std::vector<NetMessage> ignored;
    readSnapshot(ignored);
}

void NetworkClient::receiveLoop()
{
    while (running && !leaveRequested)
    {
        NetMessage playerState{};
        bool shouldSend = false;

        {
            std::lock_guard<std::mutex> lock(outgoingMutex);
            if (hasOutgoingPlayerState)
            {
                playerState = outgoingPlayerState;
                hasOutgoingPlayerState = false;
                shouldSend = true;
            }
        }

        if (!shouldSend)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        if (zmq_send(socket, &playerState, sizeof(NetMessage), 0) == -1)
        {
            reconnect();
            continue;
        }

        std::vector<NetMessage> snapshot;
        if (!readSnapshot(snapshot))
        {
            reconnect();
            continue;
        }

        std::lock_guard<std::mutex> lock(messageMutex);
        latestMessages = std::move(snapshot);
    }

    if (leaveRequested && socket != nullptr)
    {
        sendLeave();
    }
}
