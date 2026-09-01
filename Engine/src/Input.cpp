#include "Input.h"

const bool *Input::keyboardState = nullptr;
int Input::numKeys = 0;
bool Input::previousState[512] = {false};

void Input::update()
{
    if (keyboardState)
    {
        for (int i = 0; i < numKeys && i < 512; ++i)
        {
            previousState[i] = keyboardState[i];
        }
    }
    keyboardState = SDL_GetKeyboardState(&numKeys);
}

bool Input::isKeyPressed(SDL_Scancode key)
{
    if (!keyboardState || key >= numKeys)
        return false;
    return keyboardState[key];
}

bool Input::isKeyJustPressed(SDL_Scancode key)
{
    if (!keyboardState || key >= numKeys || key >= 512)
        return false;
    return keyboardState[key] && !previousState[key];
}