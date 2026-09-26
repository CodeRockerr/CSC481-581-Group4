#include "Engine.h"
#include <cstdio>
#include <string>

int main(int argc, char *argv[])
{
    Engine engine("Group 4 Engine Demo", 1280, 720);
    EntityManager &entities = engine.getEntities();
    engine.getPhysics().setGravity(0.0f);

    const float worldWidth = 1280.0f;
    const float platformSpeed = 250.0f;
    const float playerSpeed = 300.0f;

    Entity *platform = entities.createEntity(100.0f, 520.0f, 240.0f, 30.0f, {160, 160, 160, 255});
    platform->timelineId = Engine::WorldTime;
    platform->velocityX = platformSpeed;

    Entity *player = entities.createEntity(600.0f, 300.0f, 50.0f, 50.0f, {230, 60, 60, 255});
    player->timelineId = Engine::PlayerTime;

    Timeline &playerTime = engine.getTimeline(Engine::PlayerTime);
    Timeline &worldTime = engine.getTimeline(Engine::WorldTime);
    Timeline &globalTime = engine.getGlobalTimeline();
    int64_t lastTitleUpdate = 0;

    engine.run([&](float)
               {
        player->velocityX = 0.0f;
        player->velocityY = 0.0f;
        if (Input::isKeyPressed(SDL_SCANCODE_LEFT) || Input::isKeyPressed(SDL_SCANCODE_A))
        {
            player->velocityX = -playerSpeed;
        }
        if (Input::isKeyPressed(SDL_SCANCODE_RIGHT) || Input::isKeyPressed(SDL_SCANCODE_D))
        {
            player->velocityX = playerSpeed;
        }
        if (Input::isKeyPressed(SDL_SCANCODE_UP) || Input::isKeyPressed(SDL_SCANCODE_W))
        {
            player->velocityY = -playerSpeed;
        }
        if (Input::isKeyPressed(SDL_SCANCODE_DOWN) || Input::isKeyPressed(SDL_SCANCODE_S))
        {
            player->velocityY = playerSpeed;
        }

        if (platform->x <= 0.0f)
        {
            platform->x = 0.0f;
            platform->velocityX = platformSpeed;
        }
        if (platform->x + platform->width >= worldWidth)
        {
            platform->x = worldWidth - platform->width;
            platform->velocityX = -platformSpeed;
        }

        int64_t now = globalTime.getTime();
        if (now - lastTitleUpdate >= 250'000'000)
        {
            lastTitleUpdate = now;
            char title[160];
            std::snprintf(title, sizeof(title),
                          "playerTime: %s x%.1f tic %lld | worldTime: tic %lld",
                          playerTime.isPaused() ? "PAUSED" : "running",
                          playerTime.getScale(),
                          static_cast<long long>(playerTime.getTime()),
                          static_cast<long long>(worldTime.getTime()));
            SDL_SetWindowTitle(engine.getWindow().getHandle(), title);
        } });

    return 0;
}
