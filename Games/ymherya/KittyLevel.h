#pragma once

#include <cstdint>

#include "Component.h"
#include "MovementPath.h"
#include "NetProtocol.h"

namespace Kitty
{
    constexpr uint16_t KindKitty = NetKind::FirstGameKind + 1;
    constexpr uint16_t KindMovingCloud = NetKind::FirstGameKind + 2;
    constexpr uint16_t KindKuromi = NetKind::FirstGameKind + 3;

    constexpr float ViewWidth = 1280.0f;
    constexpr float ViewHeight = 800.0f;
    constexpr float WorldWidth = 4000.0f;
    constexpr float WorldHeight = 800.0f;

    // Walkable cloud top sits about 30% down the sprite; soft left/right trim.
    constexpr float CloudPadLeft = 0.10f;
    constexpr float CloudPadRight = 0.10f;
    constexpr float CloudPadTop = 0.30f;

    constexpr float GroundY = ViewHeight * 0.86f;

    struct StaticCloudDef
    {
        float x, y, width, height;
        bool big;
    };

    constexpr StaticCloudDef StaticClouds[] = {
        {420.0f, 440.0f, 175.0f, 75.0f, false},
        {690.0f, 340.0f, 250.0f, 100.0f, true},
        {1010.0f, 475.0f, 175.0f, 75.0f, false},
        {1290.0f, 470.0f, 175.0f, 75.0f, false},
        {1560.0f, 400.0f, 210.0f, 85.0f, false},
        {2290.0f, 360.0f, 250.0f, 100.0f, true},
        {2980.0f, 420.0f, 190.0f, 80.0f, false},
        {3280.0f, 350.0f, 250.0f, 100.0f, true},
        {3600.0f, 440.0f, 210.0f, 85.0f, false},
    };
    constexpr int StaticCloudCount = sizeof(StaticClouds) / sizeof(StaticClouds[0]);

    struct MovingCloudDef
    {
        MovementPathType path;
        float centerX, centerY;
        float amplitude;
        float speed;
        float width, height;
        bool big;
    };

    constexpr MovingCloudDef MovingClouds[] = {
        {MovementPathType::Vertical, 200.0f, 300.0f, 100.0f, 0.7f, 175.0f, 75.0f, false},
        {MovementPathType::Horizontal, 1950.0f, 330.0f, 100.0f, 0.8f, 175.0f, 75.0f, false},
        {MovementPathType::Circular, 2650.0f, 330.0f, 70.0f, 0.9f, 175.0f, 75.0f, false},
    };
    constexpr int MovingCloudCount = sizeof(MovingClouds) / sizeof(MovingClouds[0]);

    inline void movingCloudStart(int index, float &x, float &y)
    {
        const MovingCloudDef &p = MovingClouds[index];
        x = p.path == MovementPathType::Circular ? p.centerX + p.amplitude : p.centerX;
        y = p.centerY;
    }

    // Solid ground strips with gaps for the pits.
    struct GroundStripDef
    {
        float x, width;
    };

    constexpr GroundStripDef GroundStrips[] = {
        {0.0f, 1900.0f},
        {2620.0f, 280.0f},
        {3560.0f, WorldWidth - 3560.0f},
    };
    constexpr int GroundStripCount = sizeof(GroundStrips) / sizeof(GroundStrips[0]);

    struct SpawnPointDef
    {
        const char *name;
        float x, y;
    };

    constexpr float KittyHeight = ViewHeight * 0.18f;
    constexpr float KittyWidth = KittyHeight * 0.60f;
    constexpr float KittyGroundOffset = 50.0f;
    constexpr float KittyGroundY = GroundY - KittyHeight - KittyGroundOffset;

    constexpr SpawnPointDef SpawnPoints[] = {
        {"start", ViewWidth * 0.55f, KittyGroundY},
        {"middle", 1480.0f, KittyGroundY},
        {"upper", 2350.0f, 360.0f + 100.0f * CloudPadTop - KittyHeight},
    };
    constexpr int SpawnPointCount = sizeof(SpawnPoints) / sizeof(SpawnPoints[0]);

    struct DeathZoneDef
    {
        float x, y, width, height;
        const char *spawnName;
    };

    constexpr DeathZoneDef DeathZones[] = {
        {1900.0f, ViewHeight - 80.0f, 720.0f, 300.0f, "middle"},
        {2900.0f, ViewHeight - 80.0f, 660.0f, 300.0f, "upper"},
    };
    constexpr int DeathZoneCount = sizeof(DeathZones) / sizeof(DeathZones[0]);

    constexpr float KuromiHeight = ViewHeight * 0.25f;
    constexpr float KuromiWidth = KuromiHeight * 0.45f;
    constexpr float KuromiY = GroundY - KuromiHeight;
    constexpr float KuromiCenterX = 520.0f;
    constexpr float KuromiAmplitude = 480.0f;
    constexpr float KuromiSpeed = 0.35f;

    constexpr float ScrollLeftEdge = ViewWidth * 0.25f;
    constexpr float ScrollRightEdge = ViewWidth * 0.65f;

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

    struct Hazard : Component
    {
    };

    struct HudHeart : Component
    {
        explicit HudHeart(int index = 0) : index(index) {}
        int index = 0;
    };
}
