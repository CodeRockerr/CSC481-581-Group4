#include "Collision.h"

bool Collision::checkCollision(const Entity& a, const Entity& b) {
    SDL_FRect rectA = a.getBounds();
    SDL_FRect rectB = b.getBounds();
    // SDL3's float-rect intersection test (SDL2 used SDL_HasIntersection
    // on integer SDL_Rects; SDL3 adds a float version since our entities
    // use floats for smooth movement).
    return SDL_HasRectIntersectionFloat(&rectA, &rectB);
}
