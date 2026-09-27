#include "NetworkClient.h"

#include <chrono>
#include <iostream>
#include <string>
#include <thread>

int main(int argc, char **argv)
{
    int clientNumber = 1;

    if (argc > 1)
    {
        clientNumber = std::stoi(argv[1]);
    }

    NetworkClient client("127.0.0.1", 5555);

    if (!client.connect())
    {
        std::cerr << "Failed to connect to server\n";
        return 1;
    }

    std::cout
        << "Connected as client "
        << client.getClientId()
        << '\n';

    /*
     * Client 1 sends every 100 ms.
     * Client 2 sends every 500 ms.
     */
    int updateDelay =
        (clientNumber == 1) ? 100 : 500;

    for (int i = 0; i < 10; ++i)
    {
        NetMessage player{};

        player.type = NetMessageType::PlayerState;

        player.x =
            static_cast<float>(
                client.getClientId() * 100 +
                i * 10);

        player.y =
            static_cast<float>(
                client.getClientId() * 100);

        player.velocityX = 10.0f;
        player.velocityY = 0.0f;
        player.tic = i;

        client.sendPlayerState(player);

        std::this_thread::sleep_for(
            std::chrono::milliseconds(updateDelay));

        std::vector<NetMessage> messages =
            client.getLatestMessages();

        std::cout
            << "Client "
            << client.getClientId()
            << " received "
            << messages.size()
            << " messages:\n";

        for (const NetMessage &message : messages)
        {
            if (message.type ==
                NetMessageType::PlatformState)
            {
                std::cout
                    << "  Platform: x="
                    << message.x
                    << " y="
                    << message.y
                    << '\n';
            }
            else if (message.type ==
                     NetMessageType::PlayerState)
            {
                std::cout
                    << "  Player "
                    << message.clientId
                    << ": x="
                    << message.x
                    << " y="
                    << message.y
                    << '\n';
            }
        }
    }

    client.disconnect();

    std::cout
        << "Client "
        << client.getClientId()
        << " finished\n";

    return 0;
}