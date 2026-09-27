#include "NetworkClient.h"

#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>

NetworkClient::NetworkClient(
    const char *address,
    uint16_t port)
    : serverAddress(address),
      serverPort(port),
      context(zmq_ctx_new()),
      socket(nullptr),
      running(false)
{
    if (context == nullptr)
    {
        throw std::runtime_error(
            "Failed to create ZeroMQ context");
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

    /*
     * Use a temporary REQ socket for the Join handshake.
     */
    void *joinSocket = zmq_socket(context, ZMQ_REQ);

    if (joinSocket == nullptr)
    {
        std::cerr << "Failed to create join socket\n";
        return false;
    }

    std::string serverEndpoint =
        "tcp://" +
        std::string(serverAddress) +
        ":" +
        std::to_string(serverPort);

    if (zmq_connect(
            joinSocket,
            serverEndpoint.c_str()) != 0)
    {
        std::cerr << "Failed to connect to server\n";

        zmq_close(joinSocket);
        return false;
    }

    NetMessage join{};
    join.type = NetMessageType::Join;

    if (zmq_send(
            joinSocket,
            &join,
            sizeof(NetMessage),
            0) == -1)
    {
        std::cerr << "Failed to send Join message\n";

        zmq_close(joinSocket);
        return false;
    }

    NetMessage response{};

    int received = zmq_recv(
        joinSocket,
        &response,
        sizeof(NetMessage),
        0);

    if (received != sizeof(NetMessage) ||
        response.type != NetMessageType::Join)
    {
        std::cerr << "Invalid Join response\n";

        zmq_close(joinSocket);
        return false;
    }

    clientId = response.clientId;
    clientPort = static_cast<uint16_t>(response.tic);

    zmq_close(joinSocket);

    /*
     * Create the dedicated REQ socket for this client.
     * Only the network thread will use this socket.
     */
    socket = zmq_socket(context, ZMQ_REQ);

    if (socket == nullptr)
    {
        std::cerr << "Failed to create client socket\n";
        return false;
    }

    std::string clientEndpoint =
        "tcp://" +
        std::string(serverAddress) +
        ":" +
        std::to_string(clientPort);

    if (zmq_connect(
            socket,
            clientEndpoint.c_str()) != 0)
    {
        std::cerr << "Failed to connect to client port\n";

        zmq_close(socket);
        socket = nullptr;

        return false;
    }

    int receiveTimeout = 100;

    zmq_setsockopt(
        socket,
        ZMQ_RCVTIMEO,
        &receiveTimeout,
        sizeof(receiveTimeout));

    int linger = 0;

    zmq_setsockopt(
        socket,
        ZMQ_LINGER,
        &linger,
        sizeof(linger));

    running = true;

    networkThread =
        std::thread(&NetworkClient::receiveLoop, this);

    std::cout << "Connected to server as client "
              << clientId << '\n';

    return true;
}

void NetworkClient::disconnect()
{
    if (!running)
    {
        return;
    }

    running = false;

    /*
     * Give the network thread a chance to finish its
     * current request/reply cycle.
     */
    if (networkThread.joinable())
    {
        networkThread.join();
    }

    if (socket != nullptr)
    {
        zmq_close(socket);
        socket = nullptr;
    }
}

void NetworkClient::sendPlayerState(
    const NetMessage &message)
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

std::vector<NetMessage>
NetworkClient::getLatestMessages()
{
    std::lock_guard<std::mutex> lock(messageMutex);

    return latestMessages;
}

uint32_t NetworkClient::getClientId() const
{
    return clientId;
}

void NetworkClient::receiveLoop()
{
    while (running)
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

        /*
         * Only send when the game has provided a new
         * player state. This prevents the network thread
         * from continuously sending empty/default states.
         */
        if (!shouldSend)
        {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(1));

            continue;
        }

        /*
         * The network thread is the only thread that
         * accesses the ZeroMQ socket.
         */
        if (zmq_send(
                socket,
                &playerState,
                sizeof(NetMessage),
                0) == -1)
        {
            break;
        }

        std::vector<NetMessage> snapshot;

        while (true)
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
                    << "Invalid network message size\n";

                break;
            }

            snapshot.push_back(message);

            int more = 0;
            size_t moreSize = sizeof(more);

            zmq_getsockopt(
                socket,
                ZMQ_RCVMORE,
                &more,
                &moreSize);

            if (!more)
            {
                break;
            }
        }

        {
            std::lock_guard<std::mutex> lock(messageMutex);

            latestMessages = std::move(snapshot);
        }
    }
}