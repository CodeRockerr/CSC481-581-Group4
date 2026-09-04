#include "Engine.h"
#include "Collision.h"
#include <SDL3_image/SDL_image.h>
#include <cmath>
#include <stdexcept>

int main(int argc, char *argv[])
{
    Engine engine("Hello Kitty Adventure");

    EntityManager &entities = engine.getEntities();
    Renderer &renderer = engine.getRenderer();

    const int width = engine.getWindow().getWidth();
    const int height = engine.getWindow().getHeight();

    // --------------------------------
    // Load Background
    // --------------------------------

    SDL_Surface *backgroundSurface =
        IMG_Load("Games/ymherya/assets/background.png");

    if (!backgroundSurface)
    {
        throw std::runtime_error(
            std::string("Could not load background: ") + SDL_GetError());
    }

    SDL_Texture *backgroundTexture =
        SDL_CreateTextureFromSurface(
            renderer.getHandle(),
            backgroundSurface);

    SDL_DestroySurface(backgroundSurface);

    if (!backgroundTexture)
    {
        throw std::runtime_error(
            std::string("Could not create background texture: ") +
            SDL_GetError());
    }

    // --------------------------------
    // STATIC ENTITY
    // Background
    // --------------------------------

    Entity *background = entities.createEntity(
        0.0f,
        0.0f,
        static_cast<float>(width),
        static_cast<float>(height));

    entities.setTexture(background, backgroundTexture, 1);

    // --------------------------------
    // Load Hello Kitty
    // --------------------------------

    SDL_Surface *helloSurface =
        IMG_Load("Games/ymherya/assets/hello-kitty.png");

    if (!helloSurface)
    {
        throw std::runtime_error(
            std::string("Could not load Hello Kitty: ") + SDL_GetError());
    }

    SDL_Texture *helloTexture =
        SDL_CreateTextureFromSurface(
            renderer.getHandle(),
            helloSurface);

    SDL_DestroySurface(helloSurface);

    if (!helloTexture)
    {
        throw std::runtime_error(
            std::string("Could not create Hello Kitty texture: ") +
            SDL_GetError());
    }

    // --------------------------------
    // Load Kuromi
    // --------------------------------

    SDL_Surface *kuromiSurface =
        IMG_Load("Games/ymherya/assets/kuromi.png");

    if (!kuromiSurface)
    {
        throw std::runtime_error(
            std::string("Could not load Kuromi: ") + SDL_GetError());
    }

    SDL_Texture *kuromiTexture =
        SDL_CreateTextureFromSurface(
            renderer.getHandle(),
            kuromiSurface);

    SDL_DestroySurface(kuromiSurface);

    if (!kuromiTexture)
    {
        throw std::runtime_error(
            std::string("Could not create Kuromi texture: ") +
            SDL_GetError());
    }

    // --------------------------------
    // GROUND & PHYSICS CONSTANTS
    // --------------------------------

    const float groundY = height * 0.86f;
    const float gravity = 1500.0f;

    // --------------------------------
    // MATCHING CHARACTER SIZES
    // --------------------------------

    const float kittyHeight = height * 0.18f;
    const float kittyWidth = kittyHeight * 0.60f;
    const float kittyGroundOffset = 50.0f;

    const float kuromiHeight = height * 0.25f;
    const float kuromiWidth = kuromiHeight * 0.45f;

    // --------------------------------
    // HELLO KITTY (Player Character)
    // --------------------------------

    Entity *helloKitty = entities.createEntity(
        width * 0.55f,
        groundY - kittyHeight - kittyGroundOffset,
        kittyWidth,
        kittyHeight);

    entities.setTexture(
        helloKitty,
        helloTexture,
        8);

    helloKitty->affectedByGravity = false;
    bool helloKittyGrounded = true;

    // --------------------------------
    // KUROMI (Patrolling Character)
    // --------------------------------

    Entity *kuromi = entities.createEntity(
        width * 0.15f,
        groundY - kuromiHeight,
        kuromiWidth,
        kuromiHeight);

    entities.setTexture(
        kuromi,
        kuromiTexture,
        8);

    kuromi->affectedByGravity = false;

    // Patrol boundaries for Kuromi
    float kuromiMinX = width * 0.05f;
    float kuromiMaxX = width * 0.40f;
    float kuromiSpeed = 150.0f;
    int kuromiDirection = 1; // 1 = moving right, -1 = moving left

    // --------------------------------
    // ANIMATION
    // --------------------------------

    float helloKittyAnimationTime = 0.0f;
    float kuromiAnimationTime = 0.0f;

    // --------------------------------
    // GAME LOOP
    // --------------------------------

    engine.run([&](float deltaTime)
    {
        const int currentWidth =
            engine.getWindow().getWidth();

        const int currentHeight =
            engine.getWindow().getHeight();

        const float currentGroundY =
            currentHeight * 0.86f;

        // ==================================
        // BACKGROUND
        // ==================================

        background->width = static_cast<float>(currentWidth);
        background->height = static_cast<float>(currentHeight);

        // ==================================
        // HELLO KITTY MOVEMENT & INPUT
        // ==================================

        const float kittySpeed = 350.0f;
        bool kittyMoving = false;

        if (Input::isKeyPressed(SDL_SCANCODE_A) || Input::isKeyPressed(SDL_SCANCODE_LEFT))
        {
            helloKitty->x -= kittySpeed * deltaTime;
            kittyMoving = true;
        }

        if (Input::isKeyPressed(SDL_SCANCODE_D) || Input::isKeyPressed(SDL_SCANCODE_RIGHT))
        {
            helloKitty->x += kittySpeed * deltaTime;
            kittyMoving = true;
        }

        // Jump Command (W or UP Arrow)
        if (Input::isKeyJustPressed(SDL_SCANCODE_SPACE))
        {
            helloKitty->velocityY = -700.0f;
            helloKittyGrounded = false;
        }

        // ==================================
        // GRAVITY & JUMP UPDATE
        // ==================================

        if (!helloKittyGrounded)
        {
            helloKitty->velocityY += gravity * deltaTime;
            helloKitty->y += helloKitty->velocityY * deltaTime;
        }

        // ==================================
        // HELLO KITTY ANIMATION
        // ==================================

        if (kittyMoving && helloKittyGrounded)
        {
            helloKittyAnimationTime += deltaTime;
            helloKitty->spriteFrame =
                static_cast<int>(helloKittyAnimationTime * 12.0f) % 8;
        }
        else if (!helloKittyGrounded)
        {
            helloKitty->spriteFrame = 1; // Jumping frame
        }
        else
        {
            helloKittyAnimationTime = 0.0f;
            helloKitty->spriteFrame = 0;
        }

        // ==================================
        // HELLO KITTY GROUND COLLISION
        // ==================================

        float kittyGroundY = currentGroundY - kittyGroundOffset;
        float kittyBottom = helloKitty->y + helloKitty->height;

        if (kittyBottom >= kittyGroundY && helloKitty->velocityY >= 0.0f)
        {
            helloKitty->y = kittyGroundY - helloKitty->height;
            helloKitty->velocityY = 0.0f;
            helloKittyGrounded = true;
        }

        // ==================================
        // HELLO KITTY SCREEN BOUNDS
        // ==================================

        if (helloKitty->x < 0.0f)
        {
            helloKitty->x = 0.0f;
        }

        if (helloKitty->x + helloKitty->width > currentWidth)
        {
            helloKitty->x = currentWidth - helloKitty->width;
        }

        // ==================================
        // KUROMI PATROL LOGIC
        // ==================================

        kuromi->x += kuromiSpeed * kuromiDirection * deltaTime;
        kuromi->y = currentGroundY - kuromi->height; // Kept at exact same ground level

        // Reverse direction at patrol bounds
        if (kuromi->x >= kuromiMaxX)
        {
            kuromi->x = kuromiMaxX;
            kuromiDirection = -1;
        }
        else if (kuromi->x <= kuromiMinX)
        {
            kuromi->x = kuromiMinX;
            kuromiDirection = 1;
        }

        // Kuromi walking animation
        kuromiAnimationTime += deltaTime;
        kuromi->spriteFrame = static_cast<int>(kuromiAnimationTime * 10.0f) % 8;

        // ==================================
        // HELLO KITTY / KUROMI COLLISION
        // ==================================

        if (Collision::checkCollision(*helloKitty, *kuromi))
        {
            helloKitty->x = currentWidth * 0.55f;
            helloKitty->velocityY = 0.0f;
            helloKittyGrounded = true;
        }

        // ==================================
        // HELLO KITTY FALL RESET
        // ==================================

        if (helloKitty->y > currentHeight)
        {
            helloKitty->x = currentWidth * 0.55f;
            helloKitty->y = kittyGroundY - helloKitty->height;
            helloKitty->velocityY = 0.0f;
            helloKittyGrounded = true;
        }
    });

    // --------------------------------
    // Cleanup
    // --------------------------------

    SDL_DestroyTexture(backgroundTexture);
    SDL_DestroyTexture(helloTexture);
    SDL_DestroyTexture(kuromiTexture);

    return 0;
}