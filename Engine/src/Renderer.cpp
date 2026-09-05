#include "Renderer.h"
#include <stdexcept>

Renderer::Renderer(Window &window)
{
    renderer = SDL_CreateRenderer(window.getHandle(), nullptr);
    if (!renderer)
    {
        throw std::runtime_error(std::string("Failed to create renderer: ") + SDL_GetError());
    }
}

Renderer::~Renderer()
{
    if (renderer)
        SDL_DestroyRenderer(renderer);
}

void Renderer::clear()
{
    SDL_SetRenderDrawColor(renderer, 0, 90, 180, 255);
    SDL_RenderClear(renderer);
}

void Renderer::present()
{
    SDL_RenderPresent(renderer);
}
