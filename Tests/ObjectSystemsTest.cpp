#include "GameObjectManager.h"
#include "Components.h"
#include "Physics.h"
#include "Collision.h"

#include <cassert>
#include <iostream>

int main()
{
    GameObjectManager objects;
    Physics physics(100.0f);

    GameObject &player = objects.create("player");
    player.add<Transform>(0.0f, 0.0f, 10.0f, 10.0f);
    player.add<PhysicsBody>(1, true);
    player.add<Sprite>(SDL_Color{255, 0, 0, 255});
    player.add<Collider>(0.1f, 0.1f, 0.0f, 0.0f);

    GameObject &platform = objects.create("platform");
    platform.add<Transform>(5.0f, 5.0f, 10.0f, 10.0f);
    platform.add<Sprite>(SDL_Color{0, 255, 0, 255});
    platform.add<Collider>();

    GameObject &spawn = objects.create("spawn");
    spawn.add<Transform>(0.0f, 0.0f, 1.0f, 1.0f);

    assert(Collision::checkCollision(player, platform));

    physics.updateTimeline(objects, 1, 1.0f);
    assert(player.get<PhysicsBody>()->velocityY == 100.0f);

    objects.updateTimeline(1, 1.0f);
    assert(player.get<Transform>()->y == 100.0f);
    assert(!Collision::checkCollision(player, platform));

    std::vector<RenderItem> snapshot;
    objects.copySnapshot(snapshot);
    assert(snapshot.size() == 2);

    objects.destroy(player.getId());
    objects.flushDestroyed();
    assert(objects.size() == 2);

    std::cout << "object_systems_test: all checks passed\n";
    return 0;
}
