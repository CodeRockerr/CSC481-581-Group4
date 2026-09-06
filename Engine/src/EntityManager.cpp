#include "EntityManager.h"

Entity *EntityManager::createEntity(float x, float y, float w, float h, SDL_Color color)
{
    entities.push_back(std::make_unique<Entity>(x, y, w, h, color));
    return entities.back().get();
}

void EntityManager::setTexture(Entity *entity, SDL_Texture *texture, int frameCount)
{
    if (entity)
    {
        entity->texture = texture;
        entity->spriteFrameCount = frameCount;
    }
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

    for (const auto &e : entities)
    {
        if (!e->active)
            continue;

        SDL_FRect rect{
            e->x * scaleX,
            e->y * scaleY,
            e->width * scaleX,
            e->height * scaleY
        };

        if (e->texture)
        {
            float textureWidth = 0.0f;
            float textureHeight = 0.0f;

            SDL_GetTextureSize(e->texture, &textureWidth, &textureHeight);

            float frameWidth =
                textureWidth / static_cast<float>(e->spriteFrameCount);

            SDL_FRect source{
                frameWidth * e->spriteFrame,
                0.0f,
                frameWidth,
                textureHeight
            };

            const SDL_FlipMode flip =
                e->flipHorizontal ? SDL_FLIP_HORIZONTAL : SDL_FLIP_NONE;
            SDL_RenderTextureRotated(
                renderer, e->texture, &source, &rect, 0.0, nullptr, flip);
        }
        else
        {
            SDL_SetRenderDrawColor(
                renderer,
                e->color.r,
                e->color.g,
                e->color.b,
                e->color.a
            );

            SDL_RenderFillRect(renderer, &rect);
        }
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