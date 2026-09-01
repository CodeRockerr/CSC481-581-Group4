#include "Engine.h"
#include "Collision.h"
#include <cmath>

int main(int argc, char *argv[])
{
    Engine engine("My Game - Milestone 1");
    EntityManager &entities = engine.getEntities();

    // Reference size, captured once. Entity SIZES are fixed relative to
    // this — they do NOT change just because the window is resized.
    // Only the Task 6 toggle (Pixel vs Percentage) changes actual size.
    const int referenceWidth = engine.getWindow().getWidth();
    const int referenceHeight = engine.getWindow().getHeight();

    const float platformWidth = referenceWidth * 0.23f;
    const float platformHeight = referenceHeight * 0.05f;
    const float playerSize = referenceWidth * 0.04f;
    const float enemySize = referenceWidth * 0.04f;

    Entity *platform = entities.createEntity(referenceWidth * 0.38f, referenceHeight * 0.85f, platformWidth, platformHeight, SDL_Color{20, 20, 20, 255});

    Entity *player = entities.createEntity(referenceWidth * 0.47f, referenceHeight * 0.12f, playerSize, playerSize, SDL_Color{135, 206, 250, 255});
    player->affectedByGravity = true;
    bool isGrounded = false;

    // Enemy starts anchored to the same ground line as the platform
    // surface, so it visually stands on the same floor as the player.
    Entity *enemy = entities.createEntity(referenceWidth * 0.23f, referenceHeight * 0.85f - enemySize, enemySize, enemySize, SDL_Color{220, 30, 30, 255});
    float enemyTime = 0.0f;

    engine.run([&](float deltaTime)
               {
        enemyTime += deltaTime;

        // Re-read the CURRENT window size every frame, since the user
        // can drag-resize or fullscreen at any time.
        int currentWidth = engine.getWindow().getWidth();
        int currentHeight = engine.getWindow().getHeight();
        float moveSpeed = referenceWidth * 0.23f; // fixed, not window-dependent

        player->velocityX = 0.0f;
        if (Input::isKeyPressed(SDL_SCANCODE_A)) player->velocityX = -moveSpeed;
        if (Input::isKeyPressed(SDL_SCANCODE_D)) player->velocityX = moveSpeed;
        if (Input::isKeyPressed(SDL_SCANCODE_W) && isGrounded) {
            player->velocityY = -600.0f;
            isGrounded = false;
        }

        // --- Task 6: Scaling toggle (Tab) ---
        // This only changes HOW entities are drawn (their visual size
        // relative to the window). It does not resize the window itself
        // — the window can still be freely resized/fullscreened by the
        // user at any time, independent of this toggle.
        if (Input::isKeyJustPressed(SDL_SCANCODE_TAB)) {
            entities.toggleScaleMode();
        }

        // Reposition the platform/enemy relative to CURRENT window size
        // each frame, so they track the window instead of drifting out
        // of view when it's resized. Their SIZE is never touched here.
        platform->x = currentWidth * 0.38f;
        platform->y = currentHeight * 0.85f;

        float enemyCenterX = currentWidth * 0.23f;
        enemy->x = enemyCenterX + std::sin(enemyTime) * (currentWidth * 0.15f);
        enemy->y = platform->y - enemy->height;

        bool touchingPlatform = Collision::checkCollision(*player, *platform);
        if (touchingPlatform && player->velocityY >= 0.0f) {
            player->y = platform->y - player->height;
            player->velocityY = 0.0f;
            isGrounded = true;
        } else if (!touchingPlatform) {
            isGrounded = false;
        }

        if (Collision::checkCollision(*player, *enemy)) {
            player->x = currentWidth * 0.47f;
            player->y = currentHeight * 0.12f;
            player->velocityY = 0.0f;
            isGrounded = false;
        }

        // Compare against the CURRENT height, so shrinking the window
        // doesn't cause an immediate false "fell off" trigger.
        if (player->y > currentHeight) {
            player->x = currentWidth * 0.47f;
            player->y = currentHeight * 0.12f;
            player->velocityY = 0.0f;
            isGrounded = false;
        } });

    return 0;
}