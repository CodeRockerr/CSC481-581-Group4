
#include "GamePlaySystems.h"

#include "Camera.h"
#include "Collision.h"
#include "Components.h"
#include "DeathZone.h"
#include "GameObject.h"
#include "GameObjectManager.h"
#include "MovementPath.h"
#include "SideScrollTrigger.h"
#include "SpawnPoint.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>

GameObject* GameplaySystems::findSpawnPointById(
    GameObjectManager& objects,
    std::uint32_t objectId)
{
    GameObject* point = objects.find(objectId);

    if (!point ||
        !point->active ||
        !point->has<Transform>() ||
        !point->has<SpawnPoint>())
    {
        return nullptr;
    }

    return point;
}

GameObject* GameplaySystems::findSpawnPointByName(
    GameObjectManager& objects,
    const std::string& name)
{
    GameObject* result = nullptr;

    objects.forEach<Transform, SpawnPoint>(
        [&](GameObject& object, Transform&, SpawnPoint& spawn)
        {
            if (!result && spawn.name == name)
            {
                result = &object;
            }
        });

    return result;
}

GameObject* GameplaySystems::findSpawnPointByIndex(
    GameObjectManager& objects,
    std::size_t index)
{
    GameObject* result = nullptr;
    std::size_t currentIndex = 0;

    objects.forEach<Transform, SpawnPoint>(
        [&](GameObject& object, Transform&, SpawnPoint&)
        {
            if (!result && currentIndex == index)
            {
                result = &object;
            }

            ++currentIndex;
        });

    return result;
}

void GameplaySystems::updateMovementPaths(
    GameObjectManager& objects,
    float deltaTime)
{
    if (deltaTime <= 0.0f)
    {
        return;
    }

    objects.forEach<Transform, MovementPath>(
        [&](GameObject&, Transform& transform, MovementPath& path)
        {
            if (!path.initialized)
            {
                path.startX = transform.x;
                path.startY = transform.y;
                path.centerX = transform.x;
                path.centerY = transform.y;
                path.initialized = true;
            }

            path.elapsedTime += deltaTime;

            const float angle = path.elapsedTime * path.speed;

            switch (path.type)
            {
                case MovementPathType::Horizontal:
                    transform.x =
                        path.startX +
                        std::sin(angle) * path.distance;
                    break;

                case MovementPathType::Vertical:
                    transform.y =
                        path.startY +
                        std::sin(angle) * path.distance;
                    break;

                case MovementPathType::Circular:
                    transform.x =
                        path.centerX +
                        std::cos(angle) * path.distance;

                    transform.y =
                        path.centerY +
                        std::sin(angle) * path.distance;
                    break;
            }
        });
}

void GameplaySystems::updateDeathZones(
    GameObjectManager& objects,
    unsigned int playerId)
{
    GameObject* player = objects.find(playerId);

    if (!player ||
        !player->active ||
        !player->has<Transform>() ||
        !player->has<Collider>())
    {
        return;
    }

    objects.forEach<Transform, DeathZone, Collider>(
        [&](GameObject& zone,
            Transform&,
            DeathZone& deathZone,
            Collider&)
        {
            if (!player->active ||
                !Collision::checkCollision(*player, zone))
            {
                return;
            }

            GameObject* spawn =
                GameplaySystems::findSpawnPointByName(
                    objects, deathZone.spawnPointName);

            if (!spawn)
            {
                return;
            }

            Transform* spawnTransform = spawn->get<Transform>();
            Transform* playerTransform = player->get<Transform>();

            if (!spawnTransform || !playerTransform)
            {
                return;
            }

            playerTransform->x = spawnTransform->x;
            playerTransform->y = spawnTransform->y;

            if (PhysicsBody* body = player->get<PhysicsBody>())
            {
                body->velocityX = 0.0f;
                body->velocityY = 0.0f;
            }
        });
}

void GameplaySystems::updateSideScrollTriggers(
    GameObjectManager& objects,
    Camera& camera,
    unsigned int playerId)
{
    GameObject* player = objects.find(playerId);

    if (!player ||
        !player->active ||
        !player->has<Transform>() ||
        !player->has<Collider>())
    {
        return;
    }

    objects.forEach<Transform, SideScrollTrigger, Collider>(
        [&](GameObject& triggerObject,
            Transform&,
            SideScrollTrigger& trigger,
            Collider&)
        {
            if (trigger.activated)
            {
                return;
            }

            if (Collision::checkCollision(*player, triggerObject))
            {
                camera.translate(
                    trigger.offsetX,
                    trigger.offsetY);

                trigger.activated = true;
            }
        });
}
