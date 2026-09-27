#include "Server.h"

#include <iostream>

int main()
{
    try
    {
        Server server(5555);

        std::cout << "Starting Group 4 game server...\n";
        server.run();
    }
    catch (const std::exception &e)
    {
        std::cerr << "Server error: "
                  << e.what() << '\n';

        return 1;
    }

    return 0;
}