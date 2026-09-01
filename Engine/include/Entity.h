#pragma once
#include <SDL3/SDL.h>

// Task 2: Generic Entity System
//
// An Entity is deliberately "dumb" — it only holds data (position, size,
// velocity, color, and flags). It does NOT know how to draw itself, how
// physics works, or how input works. That's what makes it generic: the
// engine can update/draw ANY entity without caring what it represents
// (player, wall, enemy, etc). Game-specific behavior lives in game/ code,
// not here.
class Entity {
public:
    Entity(float x, float y, float w, float h,
           SDL_Color color = {255, 255, 255, 255});

    // --- Transform ---
    float x, y;          // position (top-left corner)
    float width, height; // size, used for rendering + collision box

    // --- Motion ---
    float velocityX = 0.0f;
    float velocityY = 0.0f;

    // --- Rendering ---
    SDL_Color color;

    // --- Flags used by other systems (kept generic on purpose) ---
    bool affectedByGravity = false; // Physics system checks this
    bool active = true;             // EntityManager skips inactive entities

    // Convenience for the Collision system
    SDL_FRect getBounds() const;
};
