#include "Window.h"
#include <stdexcept>

Window::Window(const std::string &title, int width, int height)
    : width(width), height(height)
{

    if (this->width <= 0 || this->height <= 0)
    {
        SDL_Rect bounds;
        SDL_DisplayID display = SDL_GetPrimaryDisplay();
        if (display != 0 && SDL_GetDisplayUsableBounds(display, &bounds))
        {
            this->width = static_cast<int>(bounds.w * 0.8f);
            this->height = static_cast<int>(bounds.h * 0.8f);
        }
        else
        {
            // Fallback if SDL can't detect the display for some reason.
            this->width = 1280;
            this->height = 800;
        }
    }

    window = SDL_CreateWindow(title.c_str(), this->width, this->height, SDL_WINDOW_RESIZABLE);
    if (!window)
    {
        throw std::runtime_error(std::string("Failed to create window: ") + SDL_GetError());
    }
}

Window::~Window()
{
    if (window)
        SDL_DestroyWindow(window);
}

void Window::resizeWindow(int w, int h)
{
    SDL_SetWindowSize(window, w, h);
    width = w;
    height = h;
}