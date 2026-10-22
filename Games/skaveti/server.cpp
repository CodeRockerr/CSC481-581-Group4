#include "CaveLevel.h"
#include "Components.h"
#include "GamePlaySystems.h"
#include "MovementPath.h"
#include "NetworkIdentity.h"
#include "Server.h"

#include <iostream>
#include <string>

int main(int argc, char *argv[])
{
    const uint16_t port = argc >= 2 ? static_cast<uint16_t>(std::stoi(argv[1])) : 5555;

    try
    {
        Server server(port);

        for (int i = 0; i < Cave::MovingPlatformCount; ++i)
        {
            const Cave::MovingPlatformDef &def = Cave::MovingPlatforms[i];
            float x = 0.0f;
            float y = 0.0f;
            Cave::movingPlatformStart(i, x, y);
            GameObject &platform = server.createObject(Cave::KindMovingPlatform, "moving-platform");
            platform.get<NetworkIdentity>()->netId = static_cast<uint32_t>(i + 1);
            platform.add<Transform>(x, y, def.width, def.height);
            platform.add<MovementPath>(def.path, def.speed, def.amplitude);
        }

        server.setSceneUpdate([](GameObjectManager &world, float deltaSeconds, double)
                              { GameplaySystems::updateMovementPaths(world, deltaSeconds); });

        std::cout << "Starting Cave Ninja server on port " << port << "...\n";
        server.run();
    }
    catch (const std::exception &e)
    {
        std::cerr << "Server error: " << e.what() << '\n';
        return 1;
    }

    return 0;
}
