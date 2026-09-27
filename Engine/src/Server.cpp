#include "Server.h"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
    constexpr uint16_t CLIENT_PORT_BASE = 6000;

    constexpr float PLATFORM_CENTER_X = 520.0f;
    constexpr float PLATFORM_Y = 520.0f;
    constexpr float PLATFORM_AMPLITUDE = 180.0f;
    bool sendAll(void *socket, const NetMessage &message, int flags)
    {
        return zmq_send(socket, &message, sizeof(NetMessage), flags) != -1;
    }
}

Server::SdlClock::SdlClock()
{
    if (!SDL_Init(SDL_INIT_EVENTS))
    {
        throw std::runtime_error(SDL_GetError());
    }
}

Server::SdlClock::~SdlClock()
{
    SDL_Quit();
}

Server::Server(uint16_t serverPort)
    : sdlClock(),
      globalTimeline(),
      worldTimeline(&globalTimeline, 1'000'000, 1.0),
      port(serverPort),
      context(zmq_ctx_new()),
      running(false)
{
    if (context == nullptr)
    {
        throw std::runtime_error(
            "Failed to create ZeroMQ context");
    }
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

void Server::stop()
{
    running = false;

    std::lock_guard<std::mutex> lock(threadMutex);

    for (std::thread &thread : clientThreads)
    {
        if (thread.joinable())
        {
            thread.join();
        }
    }

    clientThreads.clear();
}

void Server::updatePlayer(const NetMessage &message)
{
    std::lock_guard<std::mutex> lock(stateMutex);

    players[message.clientId].message = message;
}

NetMessage Server::createPlatformState()
{
    const double seconds = worldTimeline.getSeconds();

    NetMessage platform{};

    platform.type = NetMessageType::PlatformState;
    platform.clientId = 0;
    platform.tic = worldTimeline.getTime();

    platform.x =
        PLATFORM_CENTER_X +
        PLATFORM_AMPLITUDE *
            static_cast<float>(std::sin(seconds));

    platform.y = PLATFORM_Y;

    platform.velocityX =
        PLATFORM_AMPLITUDE *
        static_cast<float>(std::cos(seconds));

    platform.velocityY = 0.0f;

    return platform;
}

std::vector<NetMessage> Server::buildSnapshot()
{
    std::vector<NetMessage> snapshot;

    snapshot.push_back(createPlatformState());

    std::lock_guard<std::mutex> lock(stateMutex);

    for (const auto &entry : players)
    {
        snapshot.push_back(entry.second.message);
    }

    return snapshot;
}

void Server::sendHandshakeReply(void *socket, const NetMessage &response)
{
    sendAll(socket, response, 0);
}

void Server::clientLoop(
    uint32_t clientId,
    void *socket)
{
    int linger = 0;
    zmq_setsockopt(socket, ZMQ_LINGER, &linger, sizeof(linger));
    std::cout << "Client " << clientId << " connected\n";
    while (running)
    {
        NetMessage message{};
        const int received = zmq_recv(socket, &message, sizeof(NetMessage), 0);
        if (received == -1)
        {
            break;
        }
        if (received != sizeof(NetMessage))
        {
            NetMessage error{};
            error.type = NetMessageType::Leave;
            error.clientId = clientId;
            sendAll(socket, error, 0);
            break;
        }
        message.clientId = clientId;
        if (message.type == NetMessageType::Leave)
        {
            {
                std::lock_guard<std::mutex> lock(stateMutex);
                players.erase(clientId);
            }
            NetMessage response{};
            response.type = NetMessageType::Leave;
            response.clientId = clientId;
            sendAll(socket, response, 0);
            break;
        }

        if (message.type == NetMessageType::PlayerState)
        {
            updatePlayer(message);
        }
        const std::vector<NetMessage> snapshot = buildSnapshot();
        for (size_t i = 0; i < snapshot.size(); ++i)
        {
            const int flags = (i + 1 < snapshot.size()) ? ZMQ_SNDMORE : 0;
            if (!sendAll(socket, snapshot[i], flags))
            {
                break;
            }
        }
    }
    zmq_close(socket);
    {
        std::lock_guard<std::mutex> lock(stateMutex);
        players.erase(clientId);
    }
    std::cout << "Client " << clientId << " disconnected\n";
}

void Server::run()
{
    running = true;

    void *socket =
        zmq_socket(context, ZMQ_REP);

    if (socket == nullptr)
    {
        throw std::runtime_error(
            "Failed to create server socket");
    }

    std::string endpoint =
        "tcp://*:" +
        std::to_string(port);

    if (zmq_bind(
            socket,
            endpoint.c_str()) != 0)
    {
        zmq_close(socket);

        throw std::runtime_error(
            "Failed to bind server socket");
    }

    std::cout
        << "Server listening on port "
        << port << '\n';

    while (running)
    {
        NetMessage request{};

        int received = zmq_recv(
            socket,
            &request,
            sizeof(NetMessage),
            0);

        if (received == -1)
        {
            break;
        }

        auto reject = [&]()
        {
            NetMessage response{};
            response.type = NetMessageType::Leave;
            sendHandshakeReply(socket, response);
        };

        if (received != sizeof(NetMessage) || request.type != NetMessageType::Join)
        {
            std::cerr << "Expected Join message\n";
            reject();
            continue;
        }

        const uint32_t clientId = nextClientId++;
        const uint16_t clientPort = static_cast<uint16_t>(CLIENT_PORT_BASE + clientId);

        void *clientSocket = zmq_socket(context, ZMQ_REP);
        const std::string clientEndpoint = "tcp://*:" + std::to_string(clientPort);

        if (clientSocket == nullptr || zmq_bind(clientSocket, clientEndpoint.c_str()) != 0)
        {
            std::cerr << "Failed to bind client " << clientId << '\n';
            if (clientSocket != nullptr)
            {
                zmq_close(clientSocket);
            }
            reject();
            continue;
        }

        NetMessage response{};
        response.type = NetMessageType::Join;
        response.clientId = clientId;
        response.tic = clientPort;
        sendHandshakeReply(socket, response);

        {
            std::lock_guard<std::mutex> lock(threadMutex);
            clientThreads.emplace_back(&Server::clientLoop, this, clientId, clientSocket);
        }
    }

    zmq_close(socket);
}
