#pragma once
#include <SDL3/SDL.h>

class Input
{
public:
    static void update();
    static bool isKeyPressed(SDL_Scancode key);

    static bool isKeyJustPressed(SDL_Scancode key);

private:
    static const bool *keyboardState;
    static int numKeys;
    static bool currentState[512];
    static bool previousState[512];
};
