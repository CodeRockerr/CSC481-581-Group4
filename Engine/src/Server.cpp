#include "Server.h"

#include <chrono>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
    constexpr uint16_t CLIENT_PORT_BASE = 6000;

    constexpr float PLATFORM_CENTER_X = 960.0f;
    constexpr float PLATFORM_Y = 700.0f;
    constexpr float PLATFORM_AMPLITUDE = 300.0f;

    constexpr double PLATFORM_SPEED = 0.000001;
}

Server::Server(uint16_t serverPort)
    : port(serverPort),
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

NetMessage Server::createPlatformState() const
{
    using Clock = std::chrono::steady_clock;

    static const auto startTime = Clock::now();

    auto elapsed =
        std::chrono::duration_cast<
            std::chrono::microseconds>(
            Clock::now() - startTime);

    int64_t tic = elapsed.count();

    double time =
        static_cast<double>(tic) * PLATFORM_SPEED;

    NetMessage platform{};

    platform.type = NetMessageType::PlatformState;
    platform.clientId = 0;
    platform.tic = tic;

    platform.x =
        PLATFORM_CENTER_X +
        PLATFORM_AMPLITUDE *
            static_cast<float>(std::sin(time));

    platform.y = PLATFORM_Y;

    platform.velocityX =
        PLATFORM_AMPLITUDE *
        static_cast<float>(
            std::cos(time)) *
        static_cast<float>(PLATFORM_SPEED);

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

void Server::clientLoop(
    uint32_t clientId,
    uint16_t clientPort)
{
    void *socket = zmq_socket(context, ZMQ_REP);

    if (socket == nullptr)
    {
        std::cerr
            << "Failed to create socket for client "
            << clientId << '\n';

        return;
    }

    std::string endpoint =
        "tcp://*:" +
        std::to_string(clientPort);

    if (zmq_bind(socket, endpoint.c_str()) != 0)
    {
        std::cerr
            << "Failed to bind client "
            << clientId
            << " to port "
            << clientPort << '\n';

        zmq_close(socket);
        return;
    }

    std::cout
        << "Client "
        << clientId
        << " connected on port "
        << clientPort
        << '\n';

    while (running)
    {
        NetMessage message{};

        int received = zmq_recv(
            socket,
            &message,
            sizeof(NetMessage),
            0);

        if (received == -1)
        {
            break;
        }

        if (received != sizeof(NetMessage))
        {
            std::cerr
                << "Invalid message size from client "
                << clientId << '\n';

            break;
        }

        message.clientId = clientId;

        if (message.type == NetMessageType::Leave)
        {
            {
                std::lock_guard<std::mutex> lock(
                    stateMutex);

                players.erase(clientId);
            }

            NetMessage response{};
            response.type = NetMessageType::Leave;
            response.clientId = clientId;

            zmq_send(
                socket,
                &response,
                sizeof(NetMessage),
                0);

            break;
        }

        if (message.type ==
            NetMessageType::PlayerState)
        {
            std::cout
                << "Server received Player "
                << message.clientId
                << ": x="
                << message.x
                << " y="
                << message.y
                << '\n';

            updatePlayer(message);
        }

        std::vector<NetMessage> snapshot =
            buildSnapshot();

        for (size_t i = 0;
             i < snapshot.size();
             ++i)
        {
            int flags =
                (i + 1 < snapshot.size())
                    ? ZMQ_SNDMORE
                    : 0;

            if (zmq_send(
                    socket,
                    &snapshot[i],
                    sizeof(NetMessage),
                    flags) == -1)
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

    std::cout
        << "Client "
        << clientId
        << " disconnected\n";
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

        if (received != sizeof(NetMessage))
        {
            std::cerr
                << "Invalid handshake message\n";

            continue;
        }

        if (request.type !=
            NetMessageType::Join)
        {
            std::cerr
                << "Expected Join message\n";

            continue;
        }

        uint32_t clientId =
            nextClientId++;

        uint16_t clientPort =
            CLIENT_PORT_BASE +
            static_cast<uint16_t>(clientId);

        {
            std::lock_guard<std::mutex> lock(
                stateMutex);

            PlayerState player;

            player.message.type =
                NetMessageType::PlayerState;

            player.message.clientId =
                clientId;

            player.message.tic = 0;

            players[clientId] = player;
        }

        NetMessage response{};

        response.type =
            NetMessageType::Join;

        response.clientId =
            clientId;

        response.tic =
            clientPort;

        zmq_send(
            socket,
            &response,
            sizeof(NetMessage),
            0);

        {
            std::lock_guard<std::mutex> lock(
                threadMutex);

            clientThreads.emplace_back(
                &Server::clientLoop,
                this,
                clientId,
                clientPort);
        }
    }

    zmq_close(socket);
}