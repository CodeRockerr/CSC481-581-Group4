#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

class GameObject;
class GameObjectManager;
class Camera;

class GameplaySystems
{
public:
    // Spawn-point lookup. Index is zero-based.
    static GameObject *findSpawnPointById(
        GameObjectManager &objects,
        std::uint32_t objectId);

    static GameObject *findSpawnPointByName(
        GameObjectManager &objects,
        const std::string &name);

    static GameObject *findSpawnPointByIndex(
        GameObjectManager &objects,
        std::size_t index);

    static void updateMovementPaths(
        GameObjectManager &objects,
        float deltaTime);

    static void updateDeathZones(
        GameObjectManager &objects,
        std::uint32_t playerId);

    static void updateSideScrollTriggers(
        GameObjectManager &objects,
        Camera &camera,
        std::uint32_t playerId);
};
