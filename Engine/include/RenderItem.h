#pragma once
#include <SDL3/SDL.h>

struct RenderItem
{
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    SDL_Color color{255, 255, 255, 255};
    SDL_Texture *texture = nullptr;
    int frame = 0;
    int frameCount = 1;
    bool flipHorizontal = false;
    bool visible = true;
    int layer = 0;
    bool active = true;
};
