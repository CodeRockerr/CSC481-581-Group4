#include "KittyLevel.h"

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

        for (int i = 0; i < Kitty::MovingCloudCount; ++i)
        {
            const Kitty::MovingCloudDef &def = Kitty::MovingClouds[i];
            float x = 0.0f;
            float y = 0.0f;
            Kitty::movingCloudStart(i, x, y);

            GameObject &cloud = server.createObject(Kitty::KindMovingCloud, "moving-cloud");
            cloud.get<NetworkIdentity>()->netId = static_cast<uint32_t>(i + 1);
            cloud.add<Transform>(x, y, def.width, def.height);
            cloud.add<MovementPath>(def.path, def.speed, def.amplitude);
        }

        GameObject &kuromi = server.createObject(Kitty::KindKuromi, "kuromi");
        kuromi.get<NetworkIdentity>()->netId = static_cast<uint32_t>(Kitty::MovingCloudCount + 1);
        kuromi.add<Transform>(Kitty::KuromiCenterX, Kitty::KuromiY, Kitty::KuromiWidth, Kitty::KuromiHeight);
        kuromi.add<MovementPath>(MovementPathType::Horizontal, Kitty::KuromiSpeed, Kitty::KuromiAmplitude);

        server.setSceneUpdate([](GameObjectManager &world, float deltaSeconds, double)
                              { GameplaySystems::updateMovementPaths(world, deltaSeconds); });

        std::cout << "Starting Hello Kitty Adventure server on port " << port << "...\n";
        server.run();
    }
    catch (const std::exception &e)
    {
        std::cerr << "Server error: " << e.what() << '\n';
        return 1;
    }

    return 0;
}
