#include "Window.h"
#include <stdexcept>

Window::Window(const std::string &title, int width, int height)
    : width(width), height(height)
{

    SDL_Rect bounds{};
    SDL_DisplayID display = SDL_GetPrimaryDisplay();
    const bool haveBounds = (display != 0 && SDL_GetDisplayUsableBounds(display, &bounds));

    if (this->width <= 0 || this->height <= 0)
    {
        if (haveBounds)
        {
            this->width = static_cast<int>(bounds.w * 0.8f);
            this->height = static_cast<int>(bounds.h * 0.8f);
        }
        else
        {
            this->width = 1280;
            this->height = 800;
        }
    }
    else if (haveBounds)
    {
        // A 1920x1080 request on a laptop is larger than the screen.
        // Cocoa then marks the window zoomed, and SDL_SetWindowSize is
        // ignored — which is why Tab appeared to do nothing.
        const int maxW = bounds.w - 40;
        const int maxH = bounds.h - 80;
        if (maxW > 0 && maxH > 0 && (this->width > maxW || this->height > maxH))
        {
            const float scaleX = static_cast<float>(maxW) / static_cast<float>(this->width);
            const float scaleY = static_cast<float>(maxH) / static_cast<float>(this->height);
            const float scale = (scaleX < scaleY) ? scaleX : scaleY;
            this->width = static_cast<int>(this->width * scale);
            this->height = static_cast<int>(this->height * scale);
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
    // Cocoa refuses SDL_SetWindowSize while the window is zoomed/maximized.
    SDL_SetWindowFullscreen(window, false);
    SDL_RestoreWindow(window);
    SDL_SyncWindow(window);

    if (!SDL_SetWindowSize(window, w, h))
    {
        SDL_Log("SDL_SetWindowSize(%d, %d) failed: %s", w, h, SDL_GetError());
    }
    SDL_SyncWindow(window);

    int actualW = w;
    int actualH = h;
    SDL_GetWindowSize(window, &actualW, &actualH);
    width = actualW;
    height = actualH;
    SDL_Log("Window resized to %dx%d (requested %dx%d)", actualW, actualH, w, h);
}