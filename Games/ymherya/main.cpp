#include "Engine.h"
#include "Collision.h"
#include "Image.h"
#include <cmath>
#include <stdexcept>

int main(int argc, char *argv[])
{
    Engine engine("Hello Kitty Adventure");

    EntityManager &entities = engine.getEntities();
    Renderer &renderer = engine.getRenderer();

    // --------------------------------
    // Logical game resolution
    // --------------------------------

    const float gameWidth = 1280.0f;
    const float gameHeight = 800.0f;

    entities.setReferenceResolution(
        static_cast<int>(gameWidth),
        static_cast<int>(gameHeight));

    entities.setScaleMode(ScaleMode::Percentage);

    // --------------------------------
    // Load Background
    // --------------------------------

    SDL_Surface *backgroundSurface =
        loadImage("Games/ymherya/assets/background.png");

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

    Entity *background = entities.createEntity(
        0.0f,
        0.0f,
        gameWidth,
        gameHeight);

    entities.setTexture(background, backgroundTexture, 1);

    // --------------------------------
    // Load Hello Kitty
    // --------------------------------

    SDL_Surface *helloSurface =
        loadImage("Games/ymherya/assets/hello-kitty.png");

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
        loadImage("Games/ymherya/assets/kuromi.png");

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
    // Game object sizes
    // --------------------------------

    const float groundY = gameHeight * 0.86f;

    const float kittyHeight = gameHeight * 0.18f;
    const float kittyWidth = kittyHeight * 0.60f;

    // Hello Kitty stays 50 pixels above Kuromi/the ground.
    const float kittyGroundOffset = 50.0f;

    const float kuromiHeight = gameHeight * 0.25f;
    const float kuromiWidth = kuromiHeight * 0.45f;

    // --------------------------------
    // Hello Kitty Entity
    // --------------------------------

    Entity *helloKitty = entities.createEntity(
        gameWidth * 0.55f,
        groundY - kittyHeight - kittyGroundOffset,
        kittyWidth,
        kittyHeight);

    entities.setTexture(
        helloKitty,
        helloTexture,
        8);

    // Use the engine's physics system for gravity.
    helloKitty->affectedByGravity = true;

    bool helloKittyGrounded = true;

    // --------------------------------
    // Kuromi Entity
    // --------------------------------

    Entity *kuromi = entities.createEntity(
        gameWidth * 0.15f,
        groundY - kuromiHeight,
        kuromiWidth,
        kuromiHeight);

    entities.setTexture(
        kuromi,
        kuromiTexture,
        8);

    // Kuromi does not fall because she is controlled
    // by the predefined patrol path.
    kuromi->affectedByGravity = false;

    // --------------------------------
    // Kuromi Patrol Settings
    // --------------------------------

    float kuromiMinX = gameWidth * 0.05f;
    float kuromiMaxX = gameWidth * 0.40f;

    float kuromiSpeed = 150.0f;

    // 1 = moving right
    // -1 = moving left
    int kuromiDirection = 1;

    // --------------------------------
    // Animation timers
    // --------------------------------

    float helloKittyAnimationTime = 0.0f;
    float kuromiAnimationTime = 0.0f;

    // --------------------------------
    // Game Loop
    // --------------------------------

    engine.run([&](float deltaTime)
               {
        const int currentWidth =
            engine.getWindow().getWidth();

        const int currentHeight =
            engine.getWindow().getHeight();

        // The game world uses the fixed logical resolution.
        // The engine scales it to the actual window.
        const float currentGroundY =
            gameHeight * 0.86f;

        // Keep the background at the logical game size.
        background->width = gameWidth;
        background->height = gameHeight;

        // --------------------------------
        // Hello Kitty Movement
        // --------------------------------

        const float kittySpeed = 350.0f;
        bool kittyMoving = false;

        if (Input::isKeyPressed(SDL_SCANCODE_A) ||
            Input::isKeyPressed(SDL_SCANCODE_LEFT))
        {
            helloKitty->x -= kittySpeed * deltaTime;
            kittyMoving = true;
        }

        if (Input::isKeyPressed(SDL_SCANCODE_D) ||
            Input::isKeyPressed(SDL_SCANCODE_RIGHT))
        {
            helloKitty->x += kittySpeed * deltaTime;
            kittyMoving = true;
        }

        // --------------------------------
        // Hello Kitty Jump
        // --------------------------------

        if (Input::isKeyJustPressed(SDL_SCANCODE_SPACE) ||
            Input::isKeyJustPressed(SDL_SCANCODE_W) ||
            Input::isKeyJustPressed(SDL_SCANCODE_UP))
        {
            if (helloKittyGrounded)
            {
                helloKitty->velocityY = -700.0f;
                helloKittyGrounded = false;
            }
        }

        // --------------------------------
        // Hello Kitty Animation
        // --------------------------------

        if (kittyMoving && helloKittyGrounded)
        {
            helloKittyAnimationTime += deltaTime;

            helloKitty->spriteFrame =
                static_cast<int>(
                    helloKittyAnimationTime * 12.0f) % 8;
        }
        else if (!helloKittyGrounded)
        {
            // Jumping frame
            helloKitty->spriteFrame = 1;
        }
        else
        {
            helloKittyAnimationTime = 0.0f;
            helloKitty->spriteFrame = 0;
        }

        // --------------------------------
        // Ground Collision
        // --------------------------------

        float kittyGroundY =
            currentGroundY - kittyGroundOffset;

        float kittyBottom =
            helloKitty->y + helloKitty->height;

        if (kittyBottom >= kittyGroundY &&
            helloKitty->velocityY >= 0.0f)
        {
            helloKitty->y =
                kittyGroundY - helloKitty->height;

            helloKitty->velocityY = 0.0f;
            helloKittyGrounded = true;
        }

        // --------------------------------
        // Keep Hello Kitty inside screen
        // --------------------------------

        if (helloKitty->x < 0.0f)
        {
            helloKitty->x = 0.0f;
        }

        if (helloKitty->x + helloKitty->width > gameWidth)
        {
            helloKitty->x =
                gameWidth - helloKitty->width;
        }

        // --------------------------------
        // Kuromi Automatic Patrol
        // --------------------------------

        kuromi->x +=
            kuromiSpeed *
            kuromiDirection *
            deltaTime;

        // Keep Kuromi at ground level.
        kuromi->y =
            currentGroundY - kuromi->height;

        // Reverse direction at patrol boundaries.
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

        // --------------------------------
        // Kuromi Animation
        // --------------------------------

        kuromiAnimationTime += deltaTime;

        kuromi->spriteFrame =
            static_cast<int>(
                kuromiAnimationTime * 10.0f) % 8;

        // --------------------------------
        // Entity Collision
        // --------------------------------

        if (Collision::checkCollision(
                *helloKitty,
                *kuromi))
        {
            // Reset Hello Kitty toward her starting position.
            helloKitty->x =
                gameWidth * 0.55f;

            helloKitty->velocityY = 0.0f;

            helloKittyGrounded = true;
        }

        // --------------------------------
        // Fall Protection
        // --------------------------------

        if (helloKitty->y > gameHeight)
        {
            helloKitty->x =
                gameWidth * 0.55f;

            helloKitty->y =
                kittyGroundY - helloKitty->height;

            helloKitty->velocityY = 0.0f;

            helloKittyGrounded = true;
        }

        (void)currentWidth;
        (void)currentHeight;
    });

    // --------------------------------
    // Cleanup
    // --------------------------------

    SDL_DestroyTexture(backgroundTexture);
    SDL_DestroyTexture(helloTexture);
    SDL_DestroyTexture(kuromiTexture);

    return 0;
}