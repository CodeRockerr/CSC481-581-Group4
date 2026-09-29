#include "Server.h"

#include <iostream>
#include <string>

int main(int argc, char *argv[])
{
    const uint16_t port = argc >= 2 ? static_cast<uint16_t>(std::stoi(argv[1])) : 5555;

    try
    {
        Server server(port);
        std::cout << "Starting Lost Under the Sea server on port " << port << "...\n";
        server.run();
    }
    catch (const std::exception &e)
    {
        std::cerr << "Server error: " << e.what() << '\n';
        return 1;
    }

    return 0;
}
