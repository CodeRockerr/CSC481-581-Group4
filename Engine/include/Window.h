#pragma once
#include <SDL3/SDL.h>
#include <string>

class Window
{
public:
    Window(const std::string &title, int width = 1920, int height = 1080);
    ~Window();

    SDL_Window *getHandle() const { return window; }
    int getWidth() const { return width; }
    int getHeight() const { return height; }

    void setSize(int w, int h)
    {
        width = w;
        height = h;
    }
    void resizeWindow(int w, int h);

private:
    SDL_Window *window = nullptr;
    int width, height;
};
