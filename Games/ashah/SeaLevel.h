#pragma once
#include <cstdint>
#include "Component.h"
#include "MovementPath.h"
#include "NetProtocol.h"

namespace Sea
{
    constexpr uint16_t KindDiver = NetKind::FirstGameKind + 10;
    constexpr uint16_t KindMovingShelf = NetKind::FirstGameKind + 11;
    constexpr uint16_t KindFish = NetKind::FirstGameKind + 12;
    constexpr uint16_t KindAngler = NetKind::FirstGameKind + 13;
    constexpr float ViewWidth = 1280, ViewHeight = 720;
    constexpr float WorldWidth = 1280, WorldHeight = 4200;
    constexpr float DiverHeight = 80, DiverWidth = 96;
    constexpr float DiverDrawWidth = 128, DiverDrawHeight = 100;
    constexpr float DiverBottomPadding = 0;
    constexpr float PlatformSidePad = 0.04f;
    constexpr float MoveSpeed = 280, BoostSpeed = 390;
    constexpr float JumpSpeed = 380, Gravity = 1250;
    constexpr float BreachSpeed = 520;
    constexpr float MaxFallSpeed = 180, SwimSpeed = 220;
    constexpr float PowerDuration = 3;
    constexpr int MaxHealth = 3;
    constexpr float SurfaceY = 520;
    constexpr float GateLeft = 988, GateRight = 1052, GateY = SurfaceY - 40;
    constexpr float BuoyWidth = 260, BuoyHeight = 78;
    constexpr float BuoyX = 890, BuoyY = SurfaceY - 70;
    constexpr float RegionStarts[] = {SurfaceY, 1640, 2760};
    constexpr float RegionHeights[] = {1120, 1120, 1440};
    constexpr float RecoveryDepth = 3760;
    constexpr float LaunchTop = 3840;

    enum class PlatformArt : uint8_t
    {
        CoralPainted,
        WreckPainted,
        KelpPainted,
        SheetCoral,
        SheetWreck,
        SheetKelp,
        SheetCurrent,
        LegacyShelf
    };
    constexpr float topPadding(PlatformArt art)
    {
        switch (art)
        {
        case PlatformArt::CoralPainted:
            return 0.13f;
        case PlatformArt::WreckPainted:
            return 0.12f;
        case PlatformArt::KelpPainted:
            return 0.06f;
        case PlatformArt::SheetCoral:
            return 0.30f;
        case PlatformArt::SheetWreck:
            return 0.18f;
        case PlatformArt::SheetKelp:
            return 0.24f;
        case PlatformArt::LegacyShelf:
            return 0.20f;
        default:
            return 0.10f;
        }
    }
    struct StaticPlatformDef
    {
        float x, width, solidTopY, height;
        PlatformArt art;
        const char *label;
        constexpr float y() const { return solidTopY - height * topPadding(art); }
    };
    constexpr StaticPlatformDef StaticPlatforms[] = {
        {-80, 580, 460, 200, PlatformArt::WreckPainted, "shore-dock"},
        {100, 340, 920, 120, PlatformArt::SheetCoral, "coral-shelf"},
        {790, 310, 1240, 115, PlatformArt::CoralPainted, "coral-ridge"},
        {130, 330, 1570, 100, PlatformArt::WreckPainted, "wreck-checkpoint"},
        {760, 300, 1870, 105, PlatformArt::SheetWreck, "wreck-overhang"},
        {160, 280, 2230, 95, PlatformArt::LegacyShelf, "volcanic-ledge"},
        {810, 290, 2570, 130, PlatformArt::SheetKelp, "deep-kelp"},
        {100, 340, 2930, 130, PlatformArt::KelpPainted, "abyss-checkpoint"},
        {730, 300, 3320, 115, PlatformArt::SheetCoral, "abyss-ridge"},
        {340, 600, LaunchTop, 130, PlatformArt::WreckPainted, "recovery-bed"},
    };
    constexpr int StaticPlatformCount = sizeof(StaticPlatforms) / sizeof(StaticPlatforms[0]);
    struct MovingDef
    {
        MovementPathType path;
        float centerX, centerY, amplitude, speed, width, height;
        uint16_t kind;
        PlatformArt art;
    };
    constexpr MovingDef Movers[] = {
        {MovementPathType::Horizontal, 460, 760, 250, 1.0f, 105, 65, KindFish, PlatformArt::LegacyShelf},
        {MovementPathType::Horizontal, 580, 1400, 290, 1.0f, 130, 80, KindAngler, PlatformArt::LegacyShelf},
        {MovementPathType::Vertical, 560, 1630, 90, 0.9f, 220, 95, KindMovingShelf, PlatformArt::SheetCurrent},
        {MovementPathType::Horizontal, 470, 2030, 300, 1.2f, 110, 70, KindFish, PlatformArt::LegacyShelf},
        {MovementPathType::Circular, 590, 2390, 170, 0.95f, 135, 80, KindAngler, PlatformArt::LegacyShelf},
        {MovementPathType::Circular, 590, 2770, 95, 0.6f, 210, 100, KindMovingShelf, PlatformArt::SheetKelp},
        {MovementPathType::Horizontal, 490, 3100, 305, 1.35f, 112, 70, KindFish, PlatformArt::LegacyShelf},
        {MovementPathType::Horizontal, 530, 3540, 255, 1.2f, 140, 85, KindAngler, PlatformArt::LegacyShelf},
        {MovementPathType::Vertical, 700, 2160, 120, 1.15f, 110, 70, KindFish, PlatformArt::LegacyShelf},
        {MovementPathType::Horizontal, 580, 3650, 270, 1.4f, 120, 75, KindFish, PlatformArt::LegacyShelf},
    };
    constexpr int MoverCount = sizeof(Movers) / sizeof(Movers[0]);
    inline void moverStart(int index, float &x, float &y)
    {
        const auto &m = Movers[index];
        x = m.centerX + (m.path == MovementPathType::Circular ? m.amplitude : 0);
        y = m.centerY;
    }
    struct SpawnDef
    {
        const char *name;
        float x, y;
    };
    constexpr SpawnDef Spawns[] = {
        {"departure", 80, 460 - DiverHeight},
        {"wreck", 270, 1570 - DiverHeight},
        {"abyss", 230, 2930 - DiverHeight},
    };
    constexpr int SpawnCount = sizeof(Spawns) / sizeof(Spawns[0]);
    struct PearlDef
    {
        float x, y, size;
        int movingIndex = -1;
    };
    constexpr PearlDef Pearls[] = {
        {260, 860, 34},
        {940, 1180, 34},
        {310, 1510, 34},
        {910, 1810, 34},
        {290, 2170, 34},
        {950, 2510, 34},
        {773, 2754, 34, 5},
        {270, 2870, 34},
        {650, 3780, 34},
    };
    constexpr int PearlCount = sizeof(Pearls) / sizeof(Pearls[0]);
    constexpr int PearlsNeededToWin = PearlCount;
    constexpr float UrchinHeight = 36;
    struct UrchinDef
    {
        int platform;
        float offset, width;
        constexpr float x() const { return StaticPlatforms[platform].x + offset; }
        constexpr float y() const { return StaticPlatforms[platform].solidTopY - UrchinHeight + 4; }
    };
    constexpr UrchinDef Urchins[] = {
        {1, 18, 75},
        {2, 210, 80},
        {4, 18, 85},
        {5, 18, 75},
        {6, 190, 95},
        {7, 18, 80},
        {8, 195, 80},
        {9, 18, 85},
    };
    inline int urchinCount(float width) { return static_cast<int>(width / 30) + 1; }
    inline float urchinRadius(float width) { return width / urchinCount(width) * .5f; }
    struct PowerDef
    {
        float x, y;
    };
    constexpr PowerDef Powers[] = {{540, 1060}, {620, 2140}, {600, 3130}};
    constexpr int PowerCount = sizeof(Powers) / sizeof(Powers[0]);

    struct Ground : Component
    {
        explicit Ground(int movingIndex = -1) : movingIndex(movingIndex) {}
        int movingIndex;
    };
    struct RecoveryBed : Component
    {
    };
    struct Shore : Component
    {
    };
    struct Hazard : Component
    {
    };
    struct Urchin : Component
    {
    };
    struct Pearl : Component
    {
        bool collected = false;
    };
    struct PowerUp : Component
    {
        bool collected = false;
    };
}
