#include "Engine.h"
#include <stdexcept>

namespace
{
    bool initializeSDL()
    {
        if (!SDL_Init(SDL_INIT_VIDEO))
        {
            throw std::runtime_error(std::string("SDL_Init failed: ") + SDL_GetError());
        }

        return true;
    }
}

Engine::Engine(const std::string &title, int width, int height)
    : initialized(initializeSDL()), window(title, width, height), renderer(window), physics(980.0f)
{
    entities.setReferenceResolution(window.getWidth(), window.getHeight());
    createTimeline(DefaultTicSize, 1.0);
    createTimeline(DefaultTicSize, 1.0);
}

int Engine::createTimeline(int64_t ticSize, double scale, int anchorId)
{
    Timeline *anchor = &globalTimeline;
    if (anchorId >= 0 && anchorId < static_cast<int>(timelines.size()))
        anchor = timelines[anchorId].get();
    timelines.push_back(std::make_unique<Timeline>(anchor, ticSize, scale));
    return static_cast<int>(timelines.size()) - 1;
}

void Engine::handleTimeKeys()
{
    Timeline &playerTime = getTimeline(PlayerTime);
    if (Input::isKeyJustPressed(SDL_SCANCODE_P))
    {
        if (playerTime.isPaused())
            playerTime.unpause();
        else
            playerTime.pause();
    }
    if (Input::isKeyJustPressed(SDL_SCANCODE_1))
        playerTime.setScale(0.5);
    if (Input::isKeyJustPressed(SDL_SCANCODE_2))
        playerTime.setScale(1.0);
    if (Input::isKeyJustPressed(SDL_SCANCODE_3))
        playerTime.setScale(2.0);
}

std::vector<float> Engine::stepTimelines()
{
    globalTimeline.step();
    std::vector<float> deltas;
    deltas.reserve(timelines.size());
    for (auto &t : timelines)
        deltas.push_back(static_cast<float>(t->step()));
    return deltas;
}

void Engine::run(const std::function<void(float)> &gameUpdate)
{
    stepTimelines();

    while (running)
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_EVENT_QUIT)
            {
                running = false;
            }
            if (event.type == SDL_EVENT_WINDOW_RESIZED)
            {
                window.setSize(event.window.data1, event.window.data2);
            }
        }

        Input::update();
        handleTimeKeys();

        std::vector<float> deltas = stepTimelines();
        physics.update(entities, deltas);

        if (gameUpdate)
            gameUpdate(deltas[WorldTime]);

        entities.updateAll(deltas);

        renderer.clear();
        entities.drawAll(renderer.getHandle(), window.getWidth(), window.getHeight());
        renderer.present();
    }
}
