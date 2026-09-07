#pragma once
#include <SDL3/SDL.h>
#include "Window.h"

class Renderer
{
public:
    explicit Renderer(Window &window);
    ~Renderer();

    void clear();
    void present();

    SDL_Renderer *getHandle() const { return renderer; }

private:
    SDL_Renderer *renderer = nullptr;
};
