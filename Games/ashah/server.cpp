#include "SeaLevel.h"

#include "Components.h"
#include "GamePlaySystems.h"
#include "MovementPath.h"
#include "NetworkIdentity.h"
#include "Server.h"

#include <iostream>
#include <string>

int main(int argc, char *argv[])
{
    std::cout << std::unitbuf;
    const uint16_t port = argc >= 2 ? static_cast<uint16_t>(std::stoi(argv[1])) : 5555;

    try
    {
        Server server(port);

        for (int i = 0; i < Sea::MoverCount; ++i)
        {
            const Sea::MovingDef &def = Sea::Movers[i];
            float x = 0.0f;
            float y = 0.0f;
            Sea::moverStart(i, x, y);

            const uint16_t kind = def.kind;
            const char *name = kind == Sea::KindFish ? "reef-fish" : kind == Sea::KindAngler ? "anglerfish"
                                                                                             : "moving-shelf";
            GameObject &mover = server.createObject(kind, name);
            mover.get<NetworkIdentity>()->netId = static_cast<uint32_t>(i + 1);
            mover.add<Transform>(x, y, def.width, def.height);
            mover.add<MovementPath>(def.path, def.speed, def.amplitude);
        }

        server.setSceneUpdate([](GameObjectManager &world, float deltaSeconds, double)
                              { GameplaySystems::updateMovementPaths(world, deltaSeconds); });

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
