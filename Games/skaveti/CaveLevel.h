#pragma once
// Cave Ninja level data, shared by the game (main.cpp) and the Cave Ninja server (server.cpp) so
// both build the same world. Spawn points, death zones and movement paths use the shared engine
// components (SpawnPoint, DeathZone, MovementPath). The components below are specific to this game.

#include <cstdint>

#include "Component.h"
#include "MovementPath.h"
#include "NetProtocol.h"

namespace Cave
{
    // ---- network object kinds ----
    constexpr uint16_t KindNinja = NetKind::FirstGameKind + 1;
    constexpr uint16_t KindMovingPlatform = NetKind::FirstGameKind + 2;

    // ---- world size (world units; the window shows 1280 x 720 of it) ----
    constexpr float ViewWidth = 1280.0f;
    constexpr float ViewHeight = 720.0f;
    constexpr float WorldWidth = 3600.0f;
    constexpr float WorldHeight = 720.0f;

    // The platform art has empty space above the rock; this fraction of its height is not solid.
    constexpr float PlatformTopPadding = 0.55f;

    struct StaticPlatformDef
    {
        float x, y, width, height;
        uint8_t r, g, b; // tint applied to the platform texture
    };

    // Static platforms, each with a unique position / size / colour combination.
    constexpr StaticPlatformDef StaticPlatforms[] = {
        {120.0f, 560.0f, 420.0f, 90.0f, 255, 255, 255},  // start ledge, natural stone
        {1180.0f, 520.0f, 300.0f, 64.0f, 150, 190, 255}, // narrow, icy blue
        {2240.0f, 470.0f, 520.0f, 110.0f, 255, 150, 130}, // wide and thick, ember red
        {3150.0f, 560.0f, 380.0f, 80.0f, 160, 255, 160},  // exit ledge, mossy green
    };

    struct MovingPlatformDef
    {
        MovementPathType path;
        float centerX, centerY; // centre of the motion
        float amplitude;        // travel either side (radius for circular)
        float speed;            // radians per second
        float width, height;
    };

    // Three movement patterns, driven by the shared MovementPath component.
    constexpr MovingPlatformDef MovingPlatforms[] = {
        {MovementPathType::Horizontal, 700.0f, 470.0f, 180.0f, 1.0f, 300.0f, 80.0f},
        {MovementPathType::Vertical, 1650.0f, 430.0f, 140.0f, 1.3f, 260.0f, 70.0f},
        {MovementPathType::Circular, 2900.0f, 330.0f, 110.0f, 0.9f, 240.0f, 70.0f},
    };
    constexpr int MovingPlatformCount = sizeof(MovingPlatforms) / sizeof(MovingPlatforms[0]);

    // Where the platform starts so MovementPath moves it around (centerX, centerY).
    // MovementPath puts a circular path's centre one radius to the left of the start position.
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

    // Hidden spawn points: start, middle checkpoint, late checkpoint.
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

    // Hidden death zones along the cave floor. Every zone's designated spawn point is the last
    // checkpoint the ninja reached (the game updates DeathZone::spawnPointName when a checkpoint
    // is passed), so the rule is always the same: you come back at your last checkpoint.
    // They reach far below the world so a long frame (window drag, app switch, resize) cannot carry
    // the ninja past them in one step.
    constexpr DeathZoneDef DeathZones[] = {
        {0.0f, 700.0f, 1600.0f, 100000.0f},
        {1600.0f, 700.0f, 2000.0f, 100000.0f},
    };
    constexpr int DeathZoneCount = sizeof(DeathZones) / sizeof(DeathZones[0]);

    // Side scrolling: two hidden boundary objects stay pinned to the view at 30% and 60% of its
    // width. When the ninja moves past one, the camera translates by the overshoot, so the world
    // scrolls smoothly in both directions.
    constexpr float ScrollLeftEdge = ViewWidth * 0.30f;
    constexpr float ScrollRightEdge = ViewWidth * 0.60f;

    // ---- game components ----

    // Solid surface the ninja can stand on. movingIndex >= 0 for moving platforms.
    struct Ground : Component
    {
        explicit Ground(int movingIndex = -1) : movingIndex(movingIndex) {}
        int movingIndex = -1;
    };

    // Hidden boundary pinned to one edge of the view; pushing past it scrolls the camera.
    // (The shared SideScrollTrigger moves the camera by a fixed jump once; Cave Ninja wants smooth
    // two-way scrolling, so it uses this instead.)
    struct ScrollBoundary : Component
    {
        explicit ScrollBoundary(int direction = 1) : direction(direction) {}
        int direction = 1; // -1 left edge, +1 right edge
    };

    // Enemy that patrols along a moving platform; touching it is lethal.
    struct Patrol : Component
    {
        explicit Patrol(int platformIndex = 0) : platformIndex(platformIndex) {}
        int platformIndex = 0;
    };
}
