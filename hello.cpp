#include <SDL3/SDL.h>

int main()
{
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        SDL_Log("Unable to initialize SDL: %s", SDL_GetError());
        return 1;
    }

    SDL_Window *window = SDL_CreateWindow(
        "SDL3 Test",
        800,
        600,
        0);

    if (window == nullptr)
    {
        SDL_Log("Unable to create window: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    bool running = true;

    while (running)
    {
        SDL_Event event;

        while (SDL_PollEvent(&event))
        {
            if (event.type == SDL_EVENT_QUIT)
            {
                running = false;
            }
        }
    }

    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
