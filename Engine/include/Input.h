#pragma once
#include <SDL3/SDL.h>

class Input
{
public:
    static void update();
    static bool isKeyPressed(SDL_Scancode key);

    // True only on the single frame a key transitions from up to down —
    // needed for the scaling toggle so holding the key doesn't flicker
    // between modes every frame.
    static bool isKeyJustPressed(SDL_Scancode key);

private:
    static const bool *keyboardState;
    static int numKeys;
    static bool currentState[512];
    static bool previousState[512];
};
