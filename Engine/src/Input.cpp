#include "Input.h"

const bool *Input::keyboardState = nullptr;
int Input::numKeys = 0;
bool Input::currentState[512] = {false};
bool Input::previousState[512] = {false};

void Input::update()
{
    keyboardState = SDL_GetKeyboardState(&numKeys);
    const int count = (numKeys < 512) ? numKeys : 512;

    for (int i = 0; i < count; ++i)
    {
        previousState[i] = currentState[i];
        currentState[i] = keyboardState[i];
    }
}

bool Input::isKeyPressed(SDL_Scancode key)
{
    if (key >= numKeys || key >= 512)
        return false;
    return currentState[key];
}

bool Input::isKeyJustPressed(SDL_Scancode key)
{
    if (key >= numKeys || key >= 512)
        return false;
    return currentState[key] && !previousState[key];
}
