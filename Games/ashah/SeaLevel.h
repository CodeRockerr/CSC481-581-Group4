#pragma once
// Lost Under the Sea — Milestone 3 level data (shared by main.cpp and ashah_server later).
// Required: player, ≥3 static platforms, ≥2 movement patterns, spawns, death zones, side-scroll.
// Unique: dual suits, pearls to collect, rescue buoy finish, fish hazard + rising current platform.

#include <cstdint>

#include "Component.h"
#include "MovementPath.h"
#include "NetProtocol.h"

namespace Sea
{
    // ---- network kinds (games use ≥ FirstGameKind) ----
    constexpr uint16_t KindDiver = NetKind::FirstGameKind + 10;
    constexpr uint16_t KindMovingShelf = NetKind::FirstGameKind + 11;
    constexpr uint16_t KindFish = NetKind::FirstGameKind + 12;

    // ---- world / view ----
    constexpr float ViewWidth = 1280.0f;
    constexpr float ViewHeight = 720.0f;
    constexpr float WorldWidth = 3200.0f;
    constexpr float WorldHeight = 720.0f;

    // Shelf art has empty pixels above the walk surface.
    constexpr float ShelfTopPadding = 0.20f;
    constexpr float DiverBottomPadding = 0.10f;

    // ---- static platforms (≥3 unique position / size / tint) ----
    struct StaticPlatformDef
    {
        float x, y, width, height;
        uint8_t r, g, b;
        const char *label; // for docs / debug
    };

    constexpr StaticPlatformDef StaticPlatforms[] = {
        {80.0f, 520.0f, 520.0f, 100.0f, 255, 255, 255, "home_shelf"},    // wide start shelf
        {780.0f, 430.0f, 220.0f, 70.0f, 120, 200, 255, "coral_ledge"},   // small cyan ledge
        {1280.0f, 480.0f, 360.0f, 90.0f, 255, 170, 90, "wreck_plank"},   // mid wreck, warm tint
        {2100.0f, 400.0f, 280.0f, 80.0f, 160, 255, 180, "kelp_shelf"},   // late green shelf
        {2750.0f, 520.0f, 400.0f, 100.0f, 255, 220, 100, "rescue_dock"}, // finish dock
    };
    constexpr int StaticPlatformCount =
        sizeof(StaticPlatforms) / sizeof(StaticPlatforms[0]);

    // ---- moving platforms / hazards (≥2 MovementPath patterns) ----
    struct MovingDef
    {
        MovementPathType path;
        float centerX, centerY;
        float amplitude;
        float speed; // rad/s
        float width, height;
        uint16_t kind; // KindMovingShelf or KindFish
        uint8_t r, g, b;
    };

    // Horizontal fish patrol + vertical rising shelf + circular wreck piece.
    constexpr MovingDef Movers[] = {
        {MovementPathType::Horizontal, 980.0f, 360.0f, 160.0f, 1.1f, 90.0f, 60.0f,
         KindFish, 255, 255, 255},
        {MovementPathType::Vertical, 1650.0f, 380.0f, 120.0f, 1.0f, 240.0f, 70.0f,
         KindMovingShelf, 180, 220, 255},
        {MovementPathType::Circular, 2400.0f, 320.0f, 90.0f, 0.85f, 200.0f, 60.0f,
         KindMovingShelf, 255, 190, 140},
    };
    constexpr int MoverCount = sizeof(Movers) / sizeof(Movers[0]);

    // MovementPath circular centers one radius left of the start position.
    inline void moverStart(int index, float &x, float &y)
    {
        const MovingDef &m = Movers[index];
        x = m.path == MovementPathType::Circular ? m.centerX + m.amplitude : m.centerX;
        y = m.centerY;
    }

    // ---- hidden spawn points (multiple) ----
    struct SpawnDef
    {
        const char *name;
        float x, y;
    };

    constexpr SpawnDef Spawns[] = {
        {"start", 160.0f, 420.0f},
        {"mid", 1320.0f, 380.0f},
        {"late", 2150.0f, 300.0f},
    };
    constexpr int SpawnCount = sizeof(Spawns) / sizeof(Spawns[0]);

    // ---- hidden death zones (pit) ----
    struct DeathZoneDef
    {
        float x, y, width, height;
        const char *spawnName; // default respawn; game can override to last checkpoint
    };

    constexpr DeathZoneDef DeathZones[] = {
        {0.0f, 680.0f, 1600.0f, 100000.0f, "start"},
        {1600.0f, 680.0f, 1600.0f, 100000.0f, "mid"},
    };
    constexpr int DeathZoneCount = sizeof(DeathZones) / sizeof(DeathZones[0]);

    // ---- side-scroll camera edges (smooth, Cave Ninja style) ----
    constexpr float ScrollLeftEdge = ViewWidth * 0.30f;
    constexpr float ScrollRightEdge = ViewWidth * 0.60f;

    // ---- unique: pearls to collect ----
    struct PearlDef
    {
        float x, y, size;
    };

    constexpr PearlDef Pearls[] = {
        {900.0f, 360.0f, 28.0f},
        {1400.0f, 300.0f, 28.0f},
        {1900.0f, 250.0f, 28.0f},
        {2500.0f, 280.0f, 28.0f},
    };
    constexpr int PearlCount = sizeof(Pearls) / sizeof(Pearls[0]);
    constexpr int PearlsNeededToWin = 3; // need at least this many before buoy counts

    // ---- unique: rescue buoy (finish) ----
    constexpr float BuoyX = 2920.0f;
    constexpr float BuoyY = 400.0f;
    constexpr float BuoyW = 70.0f;
    constexpr float BuoyH = 90.0f;

    // ---- player ----
    constexpr float MoveSpeed = 320.0f;
    constexpr float JumpSpeed = 620.0f;
    constexpr float Gravity = 1600.0f;
    constexpr int MaxHealth = 3;

    // ---- game-only components ----

    struct Ground : Component
    {
        explicit Ground(int movingIndex = -1) : movingIndex(movingIndex) {}
        int movingIndex = -1; // ≥0 if this is Movers[index]
    };

    struct ScrollBoundary : Component
    {
        explicit ScrollBoundary(int direction = 1) : direction(direction) {}
        int direction = 1; // -1 left, +1 right
    };

    // Touching the fish costs a heart (then respawn), like M2.
    struct Hazard : Component
    {
    };

    struct Pearl : Component
    {
        bool collected = false;
    };

    struct RescueBuoy : Component
    {
    };

    // Local HUD hearts are not networked.
    struct HudHeart : Component
    {
        int index = 0;
    };
}
