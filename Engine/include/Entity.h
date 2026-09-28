#pragma once
#include <SDL3/SDL.h>
#include <mutex>

class EntityMutex
{
public:
    EntityMutex() = default;
    EntityMutex(const EntityMutex &) {}
    EntityMutex &operator=(const EntityMutex &) { return *this; }

    std::mutex &get() const { return mutex; }

private:
    mutable std::mutex mutex;
};

class Entity
{
public:
    Entity(float x, float y, float w, float h,
           SDL_Color color = {255, 255, 255, 255});

    float x, y;
    float width, height;
    float velocityX = 0.0f;
    float velocityY = 0.0f;

    SDL_Color color;

    bool affectedByGravity = false;
    bool active = true;

    int timelineId = 0;
    EntityMutex stateMutex;

    SDL_Texture *texture = nullptr;
    int spriteFrame = 0;
    int spriteFrameCount = 1;
    bool flipHorizontal = false;

    SDL_FRect getBounds() const;
};
