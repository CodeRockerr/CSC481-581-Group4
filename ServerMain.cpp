#include "Components.h"
#include "NetworkIdentity.h"
#include "Server.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <iostream>
#include <string>
#include <vector>

namespace
{
    struct Mover
    {
        uint32_t id;
        int pattern;
        float centerX;
        float centerY;
        float radius;
        float speed;
    };

    void buildBenchmarkScene(Server &server, int count)
    {
        auto movers = std::make_shared<std::vector<Mover>>();
        for (int i = 0; i < count; ++i)
        {
            GameObject &object = server.createObject(NetKind::Platform, "bench-" + std::to_string(i));
            const float cx = 100.0f + static_cast<float>((i * 37) % 1100);
            const float cy = 100.0f + static_cast<float>((i * 53) % 550);
            object.add<Transform>(cx, cy, 40.0f, 12.0f);
            object.add<PhysicsBody>(0, false);
            movers->push_back({object.getId(), i % 3, cx, cy, 40.0f + static_cast<float>(i % 7) * 10.0f,
                               0.5f + static_cast<float>(i % 5) * 0.25f});
        }

        server.setSceneUpdate([movers](GameObjectManager &world, float, double seconds)
                              {
            for (const Mover &m : *movers)
            {
                GameObject *object = world.find(m.id);
                if (object == nullptr)
                {
                    continue;
                }
                Transform *t = object->get<Transform>();
                PhysicsBody *body = object->get<PhysicsBody>();
                const double phase = seconds * m.speed;
                const float s = static_cast<float>(std::sin(phase));
                const float c = static_cast<float>(std::cos(phase));
                switch (m.pattern)
                {
                case 0:
                    t->x = m.centerX + m.radius * s;
                    t->y = m.centerY;
                    body->velocityX = m.radius * m.speed * c;
                    body->velocityY = 0.0f;
                    break;
                case 1:
                    t->x = m.centerX;
                    t->y = m.centerY + m.radius * s;
                    body->velocityX = 0.0f;
                    body->velocityY = m.radius * m.speed * c;
                    break;
                default:
                    t->x = m.centerX + m.radius * c;
                    t->y = m.centerY + m.radius * s;
                    body->velocityX = -m.radius * m.speed * s;
                    body->velocityY = m.radius * m.speed * c;
                    break;
                }
            } });
    }
}

int main(int argc, char **argv)
{
    uint16_t port = 5555;
    int objects = -1;
    int tick = 60;
    int timeoutMs = 3000;

    for (int i = 1; i + 1 < argc; i += 2)
    {
        const std::string flag = argv[i];
        const int value = std::stoi(argv[i + 1]);
        if (flag == "--port")
            port = static_cast<uint16_t>(value);
        else if (flag == "--objects")
            objects = value;
        else if (flag == "--tick")
            tick = value;
        else if (flag == "--timeout-ms")
            timeoutMs = value;
        else
        {
            std::cerr << "Unknown option " << flag << '\n';
            return 1;
        }
    }

    try
    {
        Server server(port);
        server.setTickRate(tick);
        server.setClientTimeoutMs(timeoutMs);
        if (objects >= 0)
        {
            buildBenchmarkScene(server, objects);
            std::cout << "Benchmark scene with " << objects << " moving objects\n";
        }
        std::cout << "Starting Group 4 game server...\n";
        server.run();
    }
    catch (const std::exception &e)
    {
        std::cerr << "Server error: " << e.what() << '\n';
        return 1;
    }
    return 0;
}
