#include "Collision.h"

bool Collision::checkCollision(const Entity &a, const Entity &b)
{
    if (!a.active || !b.active)
        return false;
    SDL_FRect rectA = a.getBounds();
    SDL_FRect rectB = b.getBounds();

    return SDL_HasRectIntersectionFloat(&rectA, &rectB);
}
