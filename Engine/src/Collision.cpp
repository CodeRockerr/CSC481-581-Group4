#include "Collision.h"
#include "Components.h"

#include <SDL3/SDL.h>

bool Collision::checkCollision(const Entity &a, const Entity &b)
{
    if (!a.active || !b.active)
    {
        return false;
    }

    SDL_FRect rectA = a.getBounds();
    SDL_FRect rectB = b.getBounds();
    return SDL_HasRectIntersectionFloat(&rectA, &rectB);
}

bool Collision::checkCollision(const GameObject &a, const GameObject &b)
{
    if (!a.active || !b.active)
    {
        return false;
    }

    const Transform *transformA = a.get<Transform>();
    const Transform *transformB = b.get<Transform>();
    if (!transformA || !transformB)
    {
        return false;
    }

    const Collider *colliderA = a.get<Collider>();
    const Collider *colliderB = b.get<Collider>();
    if ((colliderA && !colliderA->enabled) || (colliderB && !colliderB->enabled))
    {
        return false;
    }

    SDL_FRect rectA = colliderA ? colliderA->boundsFor(*transformA) : transformA->getBounds();
    SDL_FRect rectB = colliderB ? colliderB->boundsFor(*transformB) : transformB->getBounds();
    return SDL_HasRectIntersectionFloat(&rectA, &rectB);
}
