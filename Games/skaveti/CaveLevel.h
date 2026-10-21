#pragma once

#include <cstdint>

#include "Component.h"
#include "MovementPath.h"
#include "NetProtocol.h"

namespace Cave
{
    constexpr uint16_t KindNinja = NetKind::FirstGameKind + 1;
    constexpr uint16_t KindMovingPlatform = NetKind::FirstGameKind + 2;

    constexpr float ViewWidth = 1280.0f;
    constexpr float ViewHeight = 720.0f;
    constexpr float WorldWidth = 3600.0f;
    constexpr float WorldHeight = 720.0f;

    constexpr float PlatformTopPadding = 0.55f;

    struct StaticPlatformDef
    {
        float x, y, width, height;
        uint8_t r, g, b;
    };

    constexpr StaticPlatformDef StaticPlatforms[] = {
        {120.0f, 560.0f, 420.0f, 90.0f, 255, 255, 255},
        {1180.0f, 520.0f, 300.0f, 64.0f, 150, 190, 255},
        {2240.0f, 470.0f, 520.0f, 110.0f, 255, 150, 130},
        {3150.0f, 560.0f, 380.0f, 80.0f, 160, 255, 160},
    };

    struct MovingPlatformDef
    {
        MovementPathType path;
        float centerX, centerY;
        float amplitude;
        float speed;
        float width, height;
    };

    constexpr MovingPlatformDef MovingPlatforms[] = {
        {MovementPathType::Horizontal, 700.0f, 470.0f, 180.0f, 1.0f, 300.0f, 80.0f},
        {MovementPathType::Vertical, 1650.0f, 430.0f, 140.0f, 1.3f, 260.0f, 70.0f},
        {MovementPathType::Circular, 2900.0f, 330.0f, 110.0f, 0.9f, 240.0f, 70.0f},
    };
    constexpr int MovingPlatformCount = sizeof(MovingPlatforms) / sizeof(MovingPlatforms[0]);

    inline void movingPlatformStart(int index, float &x, float &y)
    {
        const MovingPlatformDef &p = MovingPlatforms[index];
        x = p.path == MovementPathType::Circular ? p.centerX + p.amplitude : p.centerX;
        y = p.centerY;
    }

    struct SpawnPointDef
    {
        const char *name;
        float x, y;
    };

    constexpr SpawnPointDef SpawnPoints[] = {
        {"start", 200.0f, 380.0f},
        {"mid", 1240.0f, 340.0f},
        {"late", 2320.0f, 290.0f},
    };
    constexpr int SpawnPointCount = sizeof(SpawnPoints) / sizeof(SpawnPoints[0]);

    struct DeathZoneDef
    {
        float x, y, width, height;
    };

    constexpr DeathZoneDef DeathZones[] = {
        {0.0f, 700.0f, 1600.0f, 100000.0f},
        {1600.0f, 700.0f, 2000.0f, 100000.0f},
    };
    constexpr int DeathZoneCount = sizeof(DeathZones) / sizeof(DeathZones[0]);

    constexpr float ScrollLeftEdge = ViewWidth * 0.30f;
    constexpr float ScrollRightEdge = ViewWidth * 0.60f;

    struct Ground : Component
    {
        explicit Ground(int movingIndex = -1) : movingIndex(movingIndex) {}
        int movingIndex = -1;
    };

    struct ScrollBoundary : Component
    {
        explicit ScrollBoundary(int direction = 1) : direction(direction) {}
        int direction = 1;
    };

    struct Patrol : Component
    {
        explicit Patrol(int platformIndex = 0) : platformIndex(platformIndex) {}
        int platformIndex = 0;
    };
}
