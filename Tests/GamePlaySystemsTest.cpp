#include "Camera.h"
#include "Components.h"
#include "DeathZone.h"
#include "GameObjectManager.h"
#include "GamePlaySystems.h"
#include "MovementPath.h"
#include "SideScrollTrigger.h"
#include "SpawnPoint.h"

#include <cassert>
#include <cmath>
#include <iostream>

namespace
{
    bool near(float actual, float expected)
    {
        return std::abs(actual - expected) < 0.01f;
    }
}

int main()
{
    // Circular first tick must keep the authored start position (no jump).
    {
        GameObjectManager objects;
        GameObject &circular = objects.create("circular-start");
        circular.add<Transform>(50.0f, 50.0f, 10.0f, 10.0f);
        circular.add<MovementPath>(MovementPathType::Circular, 1.0f, 10.0f);

        GameplaySystems::updateMovementPaths(objects, 0.001f);
        assert(near(circular.get<Transform>()->x, 50.0f));
        assert(near(circular.get<Transform>()->y, 50.0f));
    }

    GameObjectManager objects;

    constexpr float halfPi = 1.57079632679f;

    GameObject &horizontal = objects.create("horizontal-mover");
    horizontal.add<Transform>(10.0f, 20.0f, 10.0f, 10.0f);
    horizontal.add<MovementPath>(
        MovementPathType::Horizontal, 1.0f, 5.0f);

    GameObject &vertical = objects.create("vertical-mover");
    vertical.add<Transform>(30.0f, 40.0f, 10.0f, 10.0f);
    vertical.add<MovementPath>(
        MovementPathType::Vertical, 1.0f, 5.0f);

    GameObject &circular = objects.create("circular-mover");
    circular.add<Transform>(50.0f, 50.0f, 10.0f, 10.0f);
    circular.add<MovementPath>(
        MovementPathType::Circular, 1.0f, 10.0f);

    GameplaySystems::updateMovementPaths(objects, halfPi);

    assert(near(horizontal.get<Transform>()->x, 15.0f));
    assert(near(horizontal.get<Transform>()->y, 20.0f));

    assert(near(vertical.get<Transform>()->x, 30.0f));
    assert(near(vertical.get<Transform>()->y, 45.0f));

    assert(near(circular.get<Transform>()->x, 50.0f));
    assert(near(circular.get<Transform>()->y, 60.0f));

    GameObject &player = objects.create("player");
    player.add<Transform>(5.0f, 5.0f, 10.0f, 10.0f);
    player.add<Collider>(0.0f, 0.0f, 0.0f, 0.0f);
    player.add<PhysicsBody>(0, false);

    player.get<PhysicsBody>()->velocityX = 7.0f;
    player.get<PhysicsBody>()->velocityY = -9.0f;

    // Hidden checkpoint: no Sprite component is attached.
    GameObject &spawn = objects.create("safe-spawn");
    spawn.add<Transform>(100.0f, 200.0f, 10.0f, 10.0f);
    spawn.add<SpawnPoint>("checkpoint");

    assert(
        GameplaySystems::findSpawnPointByName(objects, "checkpoint")
        == &spawn);

    assert(
        GameplaySystems::findSpawnPointById(objects, spawn.getId())
        == &spawn);

    assert(
        GameplaySystems::findSpawnPointByIndex(objects, 0)
        == &spawn);

    assert(
        GameplaySystems::findSpawnPointByName(objects, "missing")
        == nullptr);

    assert(
        GameplaySystems::findSpawnPointByIndex(objects, 1)
        == nullptr);

    GameObject &deathZone = objects.create("death-zone");
    deathZone.add<Transform>(5.0f, 5.0f, 20.0f, 20.0f);
    deathZone.add<Collider>(
        0.0f, 0.0f, 0.0f, 0.0f, true);
    deathZone.add<DeathZone>("checkpoint");

    GameplaySystems::updateDeathZones(objects, player.getId());

    assert(near(player.get<Transform>()->x, 100.0f));
    assert(near(player.get<Transform>()->y, 200.0f));
    assert(near(player.get<PhysicsBody>()->velocityX, 0.0f));
    assert(near(player.get<PhysicsBody>()->velocityY, 0.0f));
    assert(!deathZone.get<DeathZone>()->armed);

    // Still overlapping the same zone must not re-fire until the player leaves.
    player.get<Transform>()->x = 5.0f;
    player.get<Transform>()->y = 5.0f;
    player.get<PhysicsBody>()->velocityX = 3.0f;
    GameplaySystems::updateDeathZones(objects, player.getId());
    assert(near(player.get<Transform>()->x, 5.0f));
    assert(near(player.get<PhysicsBody>()->velocityX, 3.0f));

    // Leave the zone to re-arm, then re-enter.
    player.get<Transform>()->x = 500.0f;
    player.get<Transform>()->y = 500.0f;
    GameplaySystems::updateDeathZones(objects, player.getId());
    assert(deathZone.get<DeathZone>()->armed);

    player.get<Transform>()->x = 5.0f;
    player.get<Transform>()->y = 5.0f;
    GameplaySystems::updateDeathZones(objects, player.getId());
    assert(near(player.get<Transform>()->x, 100.0f));
    assert(near(player.get<Transform>()->y, 200.0f));

    GameObject &cameraTrigger = objects.create("camera-trigger");
    cameraTrigger.add<Transform>(250.0f, 300.0f, 20.0f, 20.0f);
    cameraTrigger.add<Collider>(
        0.0f, 0.0f, 0.0f, 0.0f, true);
    cameraTrigger.add<SideScrollTrigger>(75.0f, -10.0f);

    player.get<Transform>()->x = 250.0f;
    player.get<Transform>()->y = 300.0f;

    Camera camera;

    GameplaySystems::updateSideScrollTriggers(
        objects, camera, player.getId());

    assert(near(camera.getX(), 75.0f));
    assert(near(camera.getY(), -10.0f));

    GameplaySystems::updateSideScrollTriggers(
        objects, camera, player.getId());

    assert(near(camera.getX(), 75.0f));
    assert(near(camera.getY(), -10.0f));

    std::cout << "gameplay_systems_test: all checks passed\n";

    return 0;
}
