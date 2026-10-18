#include "GameObjectManager.h"
#include "Components.h"

#include <cassert>
#include <iostream>
#include <stdexcept>

int main()
{
    GameObjectManager objects;

    GameObject &player = objects.create("player");
    assert(player.getId() == 1);
    player.add<Transform>(10.0f, 20.0f, 30.0f, 40.0f);
    player.add<PhysicsBody>(1, true).velocityX = 5.0f;
    player.add<Sprite>(SDL_Color{255, 0, 0, 255});

    GameObject &platform = objects.create("platform");
    assert(platform.getId() == 2);
    platform.add<Transform>(0.0f, 100.0f, 200.0f, 20.0f);
    platform.add<Sprite>(SDL_Color{100, 100, 100, 255});

    GameObject &replica = objects.createWithId(50, "remote");
    assert(replica.getId() == 50);
    GameObject &nextLocal = objects.create("local");
    assert(nextLocal.getId() == 51);

    assert(objects.find(1) == &player);
    assert(objects.findByName("platform") == &platform);
    assert(objects.find(999) == nullptr);

    int withBoth = 0;
    objects.forEach<Transform, PhysicsBody>(
        [&](GameObject &, Transform &, PhysicsBody &)
        {
            ++withBoth;
        });
    assert(withBoth == 1);

    int withSprite = 0;
    objects.forEach<Sprite>(
        [&](GameObject &, Sprite &)
        {
            ++withSprite;
        });
    assert(withSprite == 2);

    objects.destroy(1);
    assert(objects.find(1) == &player);
    assert(objects.size() == 4);
    objects.flushDestroyed();
    assert(objects.find(1) == nullptr);
    assert(objects.size() == 3);

    objects.destroy(50);
    objects.destroy(50);
    objects.flushDestroyed();
    assert(objects.find(50) == nullptr);

    bool threw = false;
    try
    {
        objects.createWithId(2, "dup");
    }
    catch (const std::invalid_argument &)
    {
        threw = true;
    }
    assert(threw);

    objects.clear();
    assert(objects.size() == 0);
    assert(objects.create("fresh").getId() == 1);

    std::cout << "object_model_test: all checks passed\n";
    return 0;
}
