#include "EntityManager.h"

Entity *EntityManager::createEntity(float x, float y, float w, float h, SDL_Color color)
{
    entities.push_back(std::make_unique<Entity>(x, y, w, h, color));
    return entities.back().get();
}

void EntityManager::drawAll(SDL_Renderer *renderer, int windowWidth, int windowHeight) const
{
    float scaleX = 1.0f;
    float scaleY = 1.0f;

    if (scaleMode == ScaleMode::Percentage)
    {
        scaleX = static_cast<float>(windowWidth) / static_cast<float>(referenceWidth);
        scaleY = static_cast<float>(windowHeight) / static_cast<float>(referenceHeight);
    }
    // In Pixel mode, scaleX/scaleY stay at 1.0 — entities render at their
    // exact literal size regardless of window size.

    for (const auto &e : entities)
    {
        if (!e->active)
            continue;
        SDL_SetRenderDrawColor(renderer, e->color.r, e->color.g, e->color.b, e->color.a);
        SDL_FRect rect{e->x * scaleX, e->y * scaleY, e->width * scaleX, e->height * scaleY};
        SDL_RenderFillRect(renderer, &rect);
    }
}

void EntityManager::updateAll(float deltaTime)
{
    for (auto &e : entities)
    {
        if (!e->active)
            continue;
        e->x += e->velocityX * deltaTime;
        e->y += e->velocityY * deltaTime;
    }
}