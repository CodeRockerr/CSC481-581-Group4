#include "Engine.h"

Engine::Engine(const std::string &title, int width, int height)
    : window(title, width, height), renderer(window), physics(980.0f)
{
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        throw std::runtime_error(std::string("SDL_Init failed: ") + SDL_GetError());
    }
    // Capture whatever size the window actually ended up at (after
    // auto-detecting the screen) as the reference resolution for Task 6's
    // percentage scaling mode.
    entities.setReferenceResolution(window.getWidth(), window.getHeight());
}

void Engine::run(const std::function<void(float)> &gameUpdate)
{
    Uint64 lastTicks = SDL_GetTicksNS();

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

        Uint64 nowTicks = SDL_GetTicksNS();
        float deltaTime = (nowTicks - lastTicks) / 1'000'000'000.0f;
        lastTicks = nowTicks;

        Input::update();
        physics.update(entities, deltaTime);

        if (gameUpdate)
            gameUpdate(deltaTime);

        entities.updateAll(deltaTime);

        renderer.clear();
        entities.drawAll(renderer.getHandle(), window.getWidth(), window.getHeight());
        renderer.present();
    }
}