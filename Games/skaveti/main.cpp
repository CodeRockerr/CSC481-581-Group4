#include "Engine.h"
#include "Collision.h"
#include "Image.h"
#include <cmath>
#include <stdexcept>
#include <string>

static SDL_Texture *loadTexture(SDL_Renderer *renderer, const char *path)
{
    SDL_Surface *surface = loadImage(path);
    if (!surface)
    {
        throw std::runtime_error(std::string("Could not load ") + path + ": " + SDL_GetError());
    }

    SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_DestroySurface(surface);

    if (!texture)
    {
        throw std::runtime_error(std::string("Could not create texture for ") + path + ": " + SDL_GetError());
    }
    SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_LINEAR);
    return texture;
}

static Entity paddedHitbox(const Entity &e, float padL, float padR, float padT, float padB)
{
    Entity box = e;
    box.x = e.x + e.width * padL;
    box.y = e.y + e.height * padT;
    box.width = e.width * (1.0f - padL - padR);
    box.height = e.height * (1.0f - padT - padB);
    return box;
}

int main(int argc, char *argv[])
{
    Engine engine("Cave Ninja - Milestone 1");
    EntityManager &entities = engine.getEntities();
    SDL_Renderer *renderer = engine.getRenderer().getHandle();

    int width = 0, height = 0;
    SDL_GetWindowSize(engine.getWindow().getHandle(), &width, &height);
    engine.getWindow().setSize(width, height);
    entities.setReferenceResolution(width, height);
    const int referenceWidth = width;
    const int referenceHeight = height;
    SDL_Texture *backgroundTexture = loadTexture(renderer, "Games/skaveti/assets/background.png");
    Entity *background = entities.createEntity(0.0f, 0.0f, float(referenceWidth), float(referenceHeight));
    entities.setTexture(background, backgroundTexture, 1);
    background->affectedByGravity = false;

    const float platformWidth = referenceWidth * 0.30f;
    const float platformHeight = referenceHeight * 0.11f;
    const float platformVisibleTopOffset = platformHeight * 0.55f;

    SDL_Texture *platformTexture = loadTexture(renderer, "Games/skaveti/assets/platform.png");
    Entity *platformLeft = entities.createEntity(referenceWidth * 0.12f, referenceHeight * 0.78f, platformWidth, platformHeight);
    entities.setTexture(platformLeft, platformTexture, 1);
    platformLeft->affectedByGravity = false;
    Entity *platformRight = entities.createEntity(referenceWidth * 0.58f, referenceHeight * 0.60f, platformWidth, platformHeight);
    entities.setTexture(platformRight, platformTexture, 1);
    platformRight->affectedByGravity = false;

    const float playerSize = referenceWidth * 0.095f;
    SDL_Texture *playerTexture = loadTexture(renderer, "Games/skaveti/assets/player.png");
    const int playerStartX = static_cast<int>(referenceWidth * 0.18f);
    const int playerStartY = static_cast<int>(referenceHeight * 0.10f);
    Entity *player = entities.createEntity(playerStartX, playerStartY, playerSize, playerSize);
    entities.setTexture(player, playerTexture, 4);

    player->affectedByGravity = true;
    bool isGrounded = false;
    float walkAnimTimer = 0.0f;
    const int runFrames[3] = {1, 2, 3};

    const float enemySize = referenceWidth * 0.09f;
    SDL_Texture *enemyTexture = loadTexture(renderer, "Games/skaveti/assets/shadow_wisp.png");
    Entity *enemy = entities.createEntity(platformRight->x, platformRight->y - enemySize, enemySize, enemySize);
    entities.setTexture(enemy, enemyTexture, 1);
    enemy->affectedByGravity = false;

    float enemyPatrolTime = 0.0f;
    float lastEnemyX = enemy->x;

    engine.run([&](float deltaTime)
               {
        enemyPatrolTime += deltaTime;

        int currentWidth = engine.getWindow().getWidth();
        int currentHeight = engine.getWindow().getHeight();
        float moveSpeed = referenceWidth * 0.32f; // noticeably faster than the enemy's patrol

        bool moving = false;
        player->velocityX = 0.0f;
        if (Input::isKeyPressed(SDL_SCANCODE_A) || Input::isKeyPressed(SDL_SCANCODE_LEFT)) {
            player->velocityX = -moveSpeed;
            player->flipHorizontal = true;
            moving = true;
        }
        if (Input::isKeyPressed(SDL_SCANCODE_D) || Input::isKeyPressed(SDL_SCANCODE_RIGHT)) {
            player->velocityX = moveSpeed;
            player->flipHorizontal = false;
            moving = true;
        }
        if ((Input::isKeyJustPressed(SDL_SCANCODE_W) ||
             Input::isKeyJustPressed(SDL_SCANCODE_UP) ||
             Input::isKeyJustPressed(SDL_SCANCODE_SPACE)) &&
            isGrounded) {
            player->velocityY = -700.0f;
            isGrounded = false;
        }

        if (moving && isGrounded) {
            walkAnimTimer += deltaTime;
            if (walkAnimTimer >= 0.12f) {
                walkAnimTimer = 0.0f;
                static int runIndex = 0;
                runIndex = (runIndex + 1) % 3;
                player->spriteFrame = runFrames[runIndex];
            }
        } else if (!isGrounded) {
            player->spriteFrame = 2;
        } else {
            walkAnimTimer = 0.0f;
            player->spriteFrame = 0;
        }

        if (Input::isKeyJustPressed(SDL_SCANCODE_TAB)) {
            entities.toggleScaleMode();
        }

        background->width = float(currentWidth);
        background->height = float(currentHeight);

        platformLeft->x = currentWidth * 0.12f;
        platformLeft->y = currentHeight * 0.78f;
        platformRight->x = currentWidth * 0.58f;
        platformRight->y = currentHeight * 0.60f;

        float patrolRange = (platformRight->width - enemy->width) * 0.5f;
        float patrolCenterX = platformRight->x + platformRight->width * 0.5f - enemy->width * 0.5f;
        
        lastEnemyX = enemy->x;
        enemy->x = patrolCenterX + std::sin(enemyPatrolTime) * patrolRange;
        enemy->y = platformRight->y + platformVisibleTopOffset - enemy->height;
        enemy->flipHorizontal = (enemy->x - lastEnemyX) > 0.0f;
        enemy->spriteFrame = 0; // single-frame sprite, no animation to cycle

        Entity leftVisible = *platformLeft;
        leftVisible.y += platformVisibleTopOffset;
        leftVisible.height -= platformVisibleTopOffset;

        Entity rightVisible = *platformRight;
        rightVisible.y += platformVisibleTopOffset;
        rightVisible.height -= platformVisibleTopOffset;

        Entity playerFeet = paddedHitbox(*player, 0.25f, 0.25f, 0.0f, 0.0f);
        bool touchingLeft = Collision::checkCollision(playerFeet, leftVisible);
        bool touchingRight = Collision::checkCollision(playerFeet, rightVisible);

        if (player->velocityY >= 0.0f && (touchingLeft || touchingRight)) {
            Entity &landedOn = touchingRight ? rightVisible : leftVisible;
            player->y = landedOn.y - player->height;
            player->velocityY = 0.0f;
            isGrounded = true;
        } else if (!touchingLeft && !touchingRight) {
            isGrounded = false;
        }

        Entity playerHit = paddedHitbox(*player, 0.28f, 0.28f, 0.10f, 0.05f);
        Entity enemyHit = paddedHitbox(*enemy, 0.20f, 0.20f, 0.15f, 0.15f);
        if (Collision::checkCollision(playerHit, enemyHit)) {
            SDL_Log("The ninja was killed by the shadow wisp!");
            player->x = playerStartX;
            player->y = playerStartY;
            player->velocityY = 0.0f;
            isGrounded = false;
        }

        if (player->y > currentHeight) {
            player->x = playerStartX;
            player->y = playerStartY;
            player->velocityY = 0.0f;
            isGrounded = false;
        } });

    SDL_DestroyTexture(backgroundTexture);
    SDL_DestroyTexture(platformTexture);
    SDL_DestroyTexture(playerTexture);
    SDL_DestroyTexture(enemyTexture);
    return 0;
}