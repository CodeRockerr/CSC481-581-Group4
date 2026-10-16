#pragma once
#include <cstddef>
#include <SDL3/SDL.h>
#include "Component.h"
#include "Input.h"

struct Transform : Component
{
    Transform() = default;
    Transform(float x, float y, float width, float height)
        : x(x), y(y), width(width), height(height) {}
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;

    SDL_FRect getBounds() const { return {x, y, width, height}; }
};

struct Sprite : Component
{
    Sprite() = default;
    explicit Sprite(SDL_Color color) : color(color) {}
    Sprite(SDL_Texture *texture, int frameCount = 1) : texture(texture), frameCount(frameCount) {}
    SDL_Texture *texture = nullptr;
    SDL_Color color{255, 255, 255, 255};
    int frame = 0;
    int frameCount = 1;
    bool flipHorizontal = false;
    bool visible = true;
    int layer = 0;
};

struct Collider : Component
{
    Collider() = default;
    Collider(float padLeft, float padRight, float padTop, float padBottom, bool isTrigger = false)
        : padLeft(padLeft), padRight(padRight), padTop(padTop), padBottom(padBottom), isTrigger(isTrigger) {}

    float padLeft = 0.0f;
    float padRight = 0.0f;
    float padTop = 0.0f;
    float padBottom = 0.0f;
    bool isTrigger = false;
    bool enabled = true;

    SDL_FRect boundsFor(const Transform &t) const
    {
        return {t.x + t.width * padLeft, t.y + t.height * padTop, t.width * (1.0f - padLeft - padRight), t.height * (1.0f - padTop - padBottom)};
    }
};

struct PhysicsBody : Component
{
    PhysicsBody() = default;
    PhysicsBody(int timelineId, bool affectedByGravity)
        : affectedByGravity(affectedByGravity), timelineId(timelineId) {}
    float velocityX = 0.0f;
    float velocityY = 0.0f;
    bool affectedByGravity = false;
    int timelineId = 0;
};

struct PlayerInput : Component
{
    PlayerInput() = default;
    PlayerInput(float moveSpeed, float jumpSpeed) : moveSpeed(moveSpeed), jumpSpeed(jumpSpeed) {}

    SDL_Scancode left[2] = {SDL_SCANCODE_A, SDL_SCANCODE_LEFT};
    SDL_Scancode right[2] = {SDL_SCANCODE_D, SDL_SCANCODE_RIGHT};
    SDL_Scancode jump[3] = {SDL_SCANCODE_W, SDL_SCANCODE_UP, SDL_SCANCODE_SPACE};
    SDL_Scancode down[2] = {SDL_SCANCODE_S, SDL_SCANCODE_DOWN};
    float moveSpeed = 300.0f;
    float jumpSpeed = 500.0f;
    bool enabled = true;

    float horizontalAxis() const
    {
        if (!enabled)
            return 0.0f;
        float axis = 0.0f;
        if (anyPressed(left))
            axis -= 1.0f;
        if (anyPressed(right))
            axis += 1.0f;
        return axis;
    }

    bool jumpPressed() const { return enabled && anyPressed(jump); }
    bool jumpJustPressed() const { return enabled && anyJustPressed(jump); }
    bool downPressed() const { return enabled && anyPressed(down); }

private:
    template <std::size_t N>
    static bool anyPressed(const SDL_Scancode (&keys)[N])
    {
        for (SDL_Scancode key : keys)
        {
            if (Input::isKeyPressed(key))
                return true;
        }
        return false;
    }

    template <std::size_t N>
    static bool anyJustPressed(const SDL_Scancode (&keys)[N])
    {
        for (SDL_Scancode key : keys)
        {
            if (Input::isKeyJustPressed(key))
                return true;
        }
        return false;
    }
};
